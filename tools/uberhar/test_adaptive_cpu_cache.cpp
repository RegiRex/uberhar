// CodexAstraLocal: Exercise the fixed CPU ownership policy with real pipeline keys,
// AsyncHandle and one real ThreadWorker. GPU completion/commands and Vulkan
// creation/destruction are controlled endpoints, not target-driver execution.
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include "common/async_handle.h"
#include "common/logging/log.h"
#include "common/thread_worker.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h"

namespace Common::Log {
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*, fmt::string_view,
                       const fmt::format_args&) {}
}
namespace {
using namespace Vulkan::AdaptiveCpu;
unsigned checks{};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
struct Key {
    Vulkan::StaticPipelineInfo state{};
    Pica::Shader::FSConfig fs{Pica::RegsInternal{}};
    Pica::Shader::Profile profile{};
    bool dynamic{};
    bool operator==(const Key& other) const noexcept {
        return dynamic == other.dynamic && state.ExecutionEquals(other.state, dynamic) &&
            fs == other.fs && profile == other.profile;
    }
};
static_assert(std::is_nothrow_copy_constructible_v<Key>);
Key MakeKey(unsigned id) {
    Key key;
    key.state.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::List);
    key.state.attachments.color = VideoCore::PixelFormat::RGBA8;
    key.state.attachments.depth = VideoCore::PixelFormat::D24S8;
    key.state.vertex_layout.binding_count = 1;
    key.state.vertex_layout.bindings[0].byte_count.Assign(88);
    key.state.shader_ids[1] = id;
    key.state.blending.color_write_mask = id & 15;
    key.state.depth_stencil.depth_test_enable.Assign(1);
    key.state.depth_stencil.depth_compare_op.Assign(
        static_cast<Pica::FramebufferRegs::CompareFunc>((id >> 4) & 7));
    key.fs.texture.fog_flip.Assign(id & 1);
    key.profile.is_vulkan = true;
    key.profile.has_separable_shaders = true;
    return key;
}

// CodexAstraLocal: The owner pool deliberately reuses addresses, so a stale
// pointer-based bind cache cannot hide behind allocator behavior in the ABA test.
struct Owner : Common::AsyncHandle {
    inline static u64 next_id{}, known_gpu{}, destruction_errors{}, frontend_destroys{};
    inline static std::set<Owner*> live;
    inline static thread_local bool worker_context{};
    inline static bool teardown{};
    inline static std::array<void*, 64> freed{};
    inline static std::size_t free_count{};
    u64 id = ++next_id, required_tick{};
    bool failed{}, usable{};
    std::atomic<bool> compiler_using{};
    Owner() : Common::AsyncHandle{false} { live.insert(this); }
    ~Owner() {
        if (!teardown) {
            destruction_errors += compiler_using.load() || known_gpu < required_tick;
            frontend_destroys += !worker_context;
        }
        live.erase(this);
    }
    static void* operator new(std::size_t size) {
        if (free_count) return freed[--free_count];
        return ::operator new(size);
    }
    static void operator delete(void* value) noexcept {
        if (free_count < freed.size()) freed[free_count++] = value;
        else ::operator delete(value);
    }
    void MarkFailed() { failed = true; MarkDone(); }
    bool HasFailed() const { return failed; }
    u64 Handle() const { return usable ? id : 0; }
    void Ready() { usable = true; MarkDone(); }
};
using Bank = Cache<Key, Owner>;
static_assert(sizeof(Bank) < 256 * 1024);
struct Queue {
    std::deque<std::function<void()>> tasks;
    bool fail_next{};
    unsigned scheduled{};
    template<class F> void QueueWork(F&& job) {
        if (std::exchange(fail_next, false)) throw std::bad_alloc{};
        tasks.emplace_back(std::forward<F>(job));
        ++scheduled;
    }
    void Run() {
        Owner::worker_context = true;
        while (!tasks.empty()) { auto job = std::move(tasks.front()); tasks.pop_front(); job(); }
        Owner::worker_context = false;
    }
};
struct Harness {
    Bank bank;
    Queue queue;
    std::size_t combined{}, other_owned{};
    Owner* bound_pipeline{};
    u64 current_handle{}, tick = 1, accepted{}, completed{};
    unsigned binds{}, draws{}, generic{}, drain_count{};
    std::vector<std::function<void()>> commands;
    ~Harness() {
        queue.Run();
        Drain();
        Owner::teardown = true;
        bank.ResetAfterDrain();
        Owner::teardown = false;
    }
    void Bind(Owner* selected) {
        commands.push_back([this, selected] {
            Check(Owner::live.contains(selected), "queued bind owner alive");
            const bool is_dirty = false;
            struct Buffer {
                Harness* h;
                void bindPipeline(vk::PipelineBindPoint, bool) {
                    throw std::runtime_error("handle collapsed to bool");
                }
                void bindPipeline(vk::PipelineBindPoint, u64 handle) {
                    h->current_handle = handle; ++h->binds;
                }
            } cmdbuf{this};
            // The production identity branch is extracted; the only endpoint
            // adaptation below gives the controlled Owner its unique handle.
#include "bind_identity.inc"
        });
    }
    void Drain() {
        for (auto& command : commands) command();
        commands.clear();
        ++drain_count;
    }
    void Flush() { Drain(); accepted = tick++; }
    void Draw(Bank::Slot& slot, bool flush_between = false) {
        auto token = bank.Selected(slot, tick);
        auto* owner = slot.owner.get();
        owner->required_tick = std::max(owner->required_tick, tick);
        Bind(owner);
        if (flush_between) Flush();
        const auto draw_tick = tick;
        owner->required_tick = std::max(owner->required_tick, draw_tick);
        commands.push_back([this, owner, id = owner->id] {
            Check(Owner::live.contains(owner), "queued draw owner alive");
            Check(current_handle == id, "actual selected pipeline was bound (ABA)");
            ++draws;
        });
        bank.DrawQueued(token, tick);
    }
    void Frame(bool complete = true) {
        // Models the existing frame's queue work followed by WaitWorker. GPU
        // completion is intentionally independently controllable.
        Flush();
        if (complete) completed = accepted;
        Owner::known_gpu = completed;
        bank.FrameAfterDrain(completed, other_owned, combined, false, queue, [this](Owner* owner) {
            Check(commands.empty(), "bound invalidation only after worker drain");
            Check(!owner || !owner->compiler_using.load(), "compiler released before retirement");
            if (bound_pipeline == owner) bound_pipeline = nullptr;
        });
    }
    void Observe(u64 hash, const Key& key, unsigned count = 4) {
        for (unsigned i = 0; i < count; ++i) bank.Observe(hash, key);
    }
    void Qualify(u64 hash, const Key& key) {
        for (unsigned n = 0; n < 4; ++n) { Observe(hash, key); if (n != 3) Frame(); }
    }
    Bank::Slot* Create(unsigned id) {
        auto key = MakeKey(id);
        Qualify(id, key);
        for (unsigned i = 0; i < 5 && !bank.CanCreate(other_owned, combined, false); ++i) {
            Frame(); queue.Run(); Observe(id, key, 16);
        }
        auto* slot = bank.Reserve(id, key, id & 1, other_owned, combined, false);
        Check(slot != nullptr, "qualified free slot reserved");
        Check(bank.Start(*slot, std::make_unique<Owner>(), id, queue,
                         [](Owner& owner) { owner.Ready(); return true; }), "build queued");
        queue.Run();
        Check(slot->compiler_released.load() == slot->generation, "compiler final release");
        return slot;
    }
};

void ProbationKeysAndBounds() {
    Harness h;
    auto key = MakeKey(1);
    h.Observe(1, key, 1000);
    Check(!h.bank.Reserve(1, key, false, 0, h.combined, false), "one-swap burst cannot qualify");
    for (unsigned i = 0; i < 3; ++i) { h.Frame(); h.Observe(1, key, 1); }
    auto* slot = h.bank.Reserve(1, key, false, 0, h.combined, false);
    Check(slot, "four swaps with16 requests qualifies");
    h.bank.AllocationFailed(*slot);
    Check(!h.bank.Observe(1, key), "failed exact key never retried");
    auto collision = MakeKey(65);
    Check(!(key == collision), "forced collision uses distinct consumed fields");
    {
        Harness fresh;
        fresh.Qualify(1,key);
        Check(!fresh.bank.Observe(1,collision), "distinct-key collision resets probation before failure ledger");
        Check(!fresh.bank.Reserve(1,collision,false,0,fresh.combined,false),
              "collision cannot spend an unearned admission");
    }
    h.Observe(1, collision, 1000);
    Check(!h.bank.Reserve(1, collision, false, 0, h.combined, false), "hash collision loses probation");
    Check(!h.bank.CanCreate(255, h.combined, false), "combined occupancy includes failed slot");
    Check(!h.bank.CanCreate(0, 256, false), "combined lifetime work budget");
    Check(!h.bank.CanCreate(0, 0, true), "shared external job defers");
    Check(h.bank.Owned() == 1, "failure keeps physical ownership slot");
    Check(sizeof(Bank) < 256 * 1024, "all metadata statically bounded");
    auto moved = key;
    moved.state.vertex_layout.bindings[0].byte_count.Assign(96);
    Check(!(key == moved), "software ABI affects observation equality");
    moved = key; moved.profile.enable_accurate_mul = 1;
    Check(!(key == moved), "full profile affects observation equality");
    moved = key; moved.fs.texture.fog_flip.Assign(!moved.fs.texture.fog_flip);
    Check(!(key == moved), "fragment config affects observation equality");
}

void RetirementAndAddressReuse() {
    Harness h;
    h.other_owned = 255;
    auto* slot = h.Create(1);
    auto* first_address = slot->owner.get();
    h.Draw(*slot, true);
    Check(slot->last_use_tick == h.tick, "post-map-flush draw extends use tick");
    // The accepted bind tick has completed, but the later draw remains incomplete.
    const auto held_tick = h.tick;
    h.completed = held_tick - 1;
    for (unsigned i = 0; i < 10; ++i) { h.Observe(2, MakeKey(2), 16); h.Frame(false); }
    Check(slot->owner != nullptr && h.queue.tasks.empty(), "uncompleted draw forbids destruction");
    Check(slot->state == State::Retired, "stale entry removed after drain");
    Check(h.bound_pipeline == nullptr, "retirement invalidates raw bind cache");
    Check(!h.bank.CanCreate(h.other_owned,h.combined,false),
          "retired owner remains inside combined admission cap");
    Check(h.bank.Owned() == 1, "retired owner still counts");
    h.completed = h.accepted; Owner::known_gpu = h.completed;
    h.bank.FrameAfterDrain(h.completed, h.other_owned, h.combined, false, h.queue, [](Owner*) {});
    Check(slot->state == State::DestroyQueued && slot->owner, "deletion queued without moving owner");
    Check(h.bank.Owned() == 1, "queued deletion cannot free capacity");
    h.queue.Run();
    Check(slot->owner == nullptr && h.bank.Owned() == 1, "completion awaits producer acquisition");
    h.Frame();
    Check(h.bank.Owned() == 0, "matching destruction generation frees slot");
    auto* replacement = h.Create(2);
    Check(replacement->owner.get() == first_address, "allocator really reused address");
    h.Draw(*replacement); h.Frame();
    Check(h.binds == 2 && h.draws == 2, "replacement received a real new bind");
    h.bank.DrawQueued({0, replacement->generation - 1}, h.tick + 9);
    Check(h.bank.stats.stale_tokens == 1, "old generation token rejected");
    Check(Owner::destruction_errors == 0 && Owner::frontend_destroys == 0,
          "runtime destroy only on worker after last completed use");
}

void CapsChurnAndFailure() {
    Harness h;
    std::array<Bank::Slot*, 8> slots{};
    // Keep all existing residents requested, so filling the eight slots does not
    // accidentally retire an older entry during probation of the next one.
    for (unsigned id = 1; id <= 8; ++id) {
        const auto key = MakeKey(id);
        for (unsigned n = 0; n < 4; ++n) {
            for (unsigned old = 0; old < id - 1; ++old) h.bank.Requested(*slots[old]);
            h.Observe(id, key, 16); h.Frame();
        }
        h.Observe(id, key, 16);
        auto* s = h.bank.Reserve(id, key, false, 0, h.combined, false);
        Check(s, "eight live slots can be filled");
        h.bank.Start(*s, std::make_unique<Owner>(), id, h.queue,
                     [](Owner& p) { p.Ready(); return true; });
        h.queue.Run(); slots[id - 1] = s;
    }
    Check(h.bank.Owned() == 8 && Owner::live.size() == 8, "physical CPU cap exactly8");
    for (unsigned n = 0; n < 20; ++n) {
        for (auto* s : slots) h.bank.Requested(*s);
        h.Observe(9, MakeKey(9), 16); h.Frame();
        Check(!h.bank.CanCreate(0, h.combined, false), "stable hot8 prevents ninth object");
    }
    Check(h.bank.stats.retirements == 0, "hot residents do not churn");
    for (unsigned n = 0; n < 20; ++n) { h.Observe(9, MakeKey(9), 16); h.Frame(); }
    Check(h.bank.Owned() == 8 && h.queue.tasks.size() == 1,
          "all retired/deleting remain counted and one deletion queued");
    Check(!h.bank.CanCreate(0, h.combined, false), "no work while physical bank full");
    h.queue.Run(); h.Frame();
    Check(h.bank.Owned() == 7, "one completion releases one slot");
    Check(h.bank.stats.max_owned == 8, "cap never exceeded");
    Check(h.bank.stats.retirements == 1, "one miss does not drain more than one owner");
    // Queue rejection on the next capacity-needed retirement retains that owner
    // and terminally stops adaptation, rather than retrying allocation each swap.
    auto* replacement = h.Create(9);
    Check(replacement && h.bank.Owned() == 8, "new owner reuses only released capacity");
    h.queue.fail_next = true;
    for (unsigned n = 0; n < 4; ++n) { h.Observe(10, MakeKey(10), 16); h.Frame(); }
    Check(h.bank.Disabled(), "destruction queue failure disables adaptation");
    Check(h.bank.Owned() > 0, "queue failure retains bounded owner");
    Check(!h.bank.CanCreate(0, h.combined, false), "disabled policy creates no work");
}

void RealWorkerRelease() {
    Bank bank;
    Common::ThreadWorker worker{1, "Private CPU PSO"};
    std::mutex mutex;
    std::condition_variable cv;
    bool published{}, leave{};
    struct Shared { std::mutex* mutex; std::condition_variable* cv; bool* published; bool* leave; };
    Shared shared{&mutex, &cv, &published, &leave};
    std::size_t combined{};
    auto key = MakeKey(41);
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned n = 0; n < 16; ++n) bank.Observe(41, key);
        if (i != 3) bank.FrameAfterDrain(0, 255, combined, false, worker, [](Owner*) {});
    }
    auto* slot = bank.Reserve(41, key, true, 0, combined, false);
    Check(slot != nullptr, "real worker slot reserved");
    bank.Start(*slot, std::make_unique<Owner>(), 41, worker, [shared](Owner& owner) {
        Owner::worker_context = true;
        owner.compiler_using = true;
        owner.Ready(); // Deliberately publishes IsDone before final owner access.
        std::unique_lock lock{*shared.mutex};
        *shared.published = true; shared.cv->notify_all();
        shared.cv->wait(lock, [&] { return *shared.leave; });
        Check(owner.Handle(), "final compiler/log access owner alive");
        owner.compiler_using = false;
        return true;
    });
    // CodexAstraLocal: A failed main-thread assertion must release the blocked
    // actual worker before unwinding, so negative controls fail deterministically.
    struct Unblock {
        std::mutex& mutex; std::condition_variable& cv; bool& leave;
        Common::ThreadWorker& worker;
        ~Unblock() {
            { std::scoped_lock lock{mutex}; leave=true; }
            cv.notify_all(); worker.WaitForRequests();
        }
    } unblock{mutex,cv,leave,worker};
    {
        std::unique_lock lock{mutex};
        Check(cv.wait_for(lock, std::chrono::seconds{3}, [&] { return published; }),
              "bounded actual worker publication");
    }
    Check(slot->owner->IsDone() && slot->compiler_released.load() != slot->generation,
          "IsDone is distinct from compiler release");
    for (unsigned i = 0; i < 12; ++i) {
        for (unsigned n = 0; n < 16; ++n) bank.Observe(42, MakeKey(42));
        bank.FrameAfterDrain(999, 255, combined, false, worker, [&](Owner* p) {
            Check(!p->compiler_using, "never retire still-referenced compiler owner");
        });
    }
    Check(slot->state == State::Building, "no retirement at early IsDone");
    { std::scoped_lock lock{mutex}; leave = true; } cv.notify_all();
    worker.WaitForRequests();
    Check(slot->compiler_released.load() == slot->generation, "actual worker final release published");
    Owner::known_gpu = 999;
    for (unsigned n = 0; n < 16; ++n) bank.Observe(42, MakeKey(42));
    bank.FrameAfterDrain(999, 255, combined, false, worker, [](Owner*) {});
    worker.WaitForRequests();
    bank.FrameAfterDrain(999, 255, combined, false, worker, [](Owner*) {});
    Check(bank.Owned() == 0, "actual worker deletes after final release");
    Check(Owner::destruction_errors == 0 && Owner::frontend_destroys == 0,
          "actual worker destruction respects CPU/GPU ownership");
}

// CodexAstraLocal: Lifetime work budgets, failed-key reclamation and title
// generation reset are separate from successful compilation controls.
void BudgetsFailuresAndReset() {
    {
        Harness h;
        auto* ready = h.Create(1);
        for (unsigned n=0;n<20;++n) { h.Observe(2,MakeKey(2),16); h.Frame(); }
        Check(h.bank.stats.retirements==0,"free physical slots do not cause retirement");
        h.other_owned=255;
        h.combined=256;
        for (unsigned n=0;n<20;++n) { h.Observe(2,MakeKey(2),16); h.Frame(); }
        Check(h.bank.stats.retirements==0,"exhausted combined work prevents new retirement");
        Check(h.bank.Find(1)==ready,"existing ready hit survives work exhaustion");
        h.Draw(*ready);h.Frame();
    }
    {
        Harness h; h.other_owned=255;
        for(unsigned attempt=0;attempt<64;++attempt) {
            const unsigned id=200+attempt;
            const auto key=MakeKey(id);
            h.Qualify(id,key);
            auto* slot=h.bank.Reserve(id,key,false,h.other_owned,h.combined,false);
            Check(slot!=nullptr,"finite attempt reserved before failure");
            h.bank.AllocationFailed(*slot);
            Check(h.bank.Attempts()==attempt+1 && h.combined==attempt+1,
                  "allocation failure consumes both creation tokens");
            if(attempt==63) break;
            // The failed key remains hot, but never becomes selectable or retries.
            for(unsigned n=0;n<4;++n) {
                h.bank.Requested(*slot); h.Observe(id,key,100);
                h.Observe(id+1,MakeKey(id+1),16);h.Frame();
            }
            Check(slot->state==State::DestroyQueued,
                  "hot failed owner cannot pin bank against new qualified key");
            Check(!h.bank.Find(id),"destroy-queued owner never exposed to lookup");
            h.queue.Run();h.Frame();
            Check(h.bank.Owned()==0,"failed allocation slot reclaims after acquired completion");
        }
        Check(h.bank.Attempts()==64 && h.combined==64,"CPU attempts exact finite maximum");
        unsigned census{};
        h.bank.VisitAttempts([&](std::size_t ordinal,const Key&,u64,bool failed) {
            Check(ordinal==++census && failed,"census preserves every failed attempted key");
        });
        Check(census==64,"attempt census itself bounded to creation budget");
        const auto retired=h.bank.stats.retirements;
        for(unsigned n=0;n<20;++n) { h.Observe(999,MakeKey(999),16);h.Frame(); }
        Check(h.bank.stats.retirements==retired,"CPU token exhaustion forbids new retirement");
        Check(!h.bank.CanCreate(0,h.combined,false),"no sixty-fifth attempt");
        const auto generation=h.bank.AllSlots()[0].generation;
        h.queue.Run();h.Drain();
        Owner::teardown=true;h.bank.ResetAfterDrain();Owner::teardown=false;
        h.combined=0;
        auto* next=h.Create(300);
        Check(next->generation>generation,"title reset never reuses slot generation");
        h.bank.DrawQueued({0,generation},h.tick);
        Check(h.bank.stats.stale_tokens==1,"old-title completion token cannot extend new owner");
    }
    for(unsigned failure=0;failure<4;++failure) {
        Harness h;auto key=MakeKey(77);h.Qualify(77,key);
        auto* slot=h.bank.Reserve(77,key,false,0,h.combined,false);
        Check(slot!=nullptr,"failure control reserve");
        if(failure==3) h.queue.fail_next=true;
        const bool queued=h.bank.Start(*slot,std::make_unique<Owner>(),77,h.queue,
            [failure](Owner&) -> bool {
                if(failure==1) throw std::runtime_error("standard failure");
                if(failure==2) throw 47;
                return false;
            });
        Check(queued==(failure!=3),"queue acceptance distinguished from compiler failure");
        h.queue.Run();h.Frame();
        Check(slot->state==State::Failed && slot->owner->IsDone() && slot->owner->HasFailed(),
              "every optional failure publishes terminal owner");
        Check(slot->compiler_released.load()==slot->generation,"failure releases compiler access");
        Check(!h.bank.Observe(77,key),"failed build ledger forbids repeated attempt");
    }
}
} // namespace

int main() {
    try {
        ProbationKeysAndBounds(); RetirementAndAddressReuse(); CapsChurnAndFailure();
        RealWorkerRelease(); BudgetsFailuresAndReset();
        Check(Owner::live.empty(), "all modeled owners released");
        std::cout << "{\"checks\":" << checks << ",\"key_bytes\":" << sizeof(Key)
                  << ",\"bank_bytes\":" << sizeof(Bank)
                  << ",\"lifetime_errors\":" << Owner::destruction_errors
                  << ",\"frontend_destroys\":" << Owner::frontend_destroys << "}\n";
        for (std::size_t i = 0; i < Owner::free_count; ++i) ::operator delete(Owner::freed[i]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
}
