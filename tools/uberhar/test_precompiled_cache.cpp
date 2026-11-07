// CodexAstraUlt-2: Execute the production bank against instrumented Vulkan owner
// stand-ins; driver performance and shader semantics remain separate device gates.
#include <cassert>
#include <iostream>
#include "video_core/renderer_vulkan/uberhar_precompiled_cache.h"

using namespace Vulkan;

static Precompile::Recipe Recipe(unsigned fs, unsigned state, std::uint64_t uses = 1) {
    return {{fs}, {{}, state}, uses};
}

static std::unique_ptr<Shader> Compile(const Pica::Shader::FSConfig&) {
    return std::make_unique<Shader>();
}

int main() {
    Instance instance;
    RenderManager renderpass;
    Shader trivial;
    std::atomic_bool stop{};
    const auto make = [&] {
        return std::make_unique<PrecompiledCache>(instance, renderpass, trivial, 7, 9);
    };

    // CodexAstraUlt-2: Collision-safe lookup, live dynamic state and duplicate
    // records must not create a second executable or borrow the wrong fragment.
    {
        auto bank = make();
        const std::array recipes{Recipe(1, 1), Recipe(5, 1), Recipe(1, 1, 20)};
        bank->Prepare(recipes, stop, {}, Compile);
        assert(bank->Stats().eligible == 2 && bank->Stats().duplicates == 1);
        assert(bank->ResidentPipelineCount() == 2 && bank->ResidentModuleCount() == 2);
        assert(bank->Lookup(recipes[0].config, recipes[0].pipeline));
        assert(bank->Lookup(recipes[1].config, recipes[1].pipeline));
        assert(!bank->Lookup(Recipe(9, 1).config, recipes[0].pipeline));
        auto changed = recipes[0].pipeline;
        changed.inactive = 99;
        assert(bank->Lookup(recipes[0].config, changed));
        changed.state = 99;
        assert(!bank->Lookup(recipes[0].config, changed));
        changed = recipes[0].pipeline;
        changed.eligible = false;
        assert(!bank->Lookup(recipes[0].config, changed));
        bank->Clear();
        assert(bank->ResidentPipelineCount() == 0 && bank->ResidentModuleCount() == 0);
    }
    {
        instance.dynamic = false;
        auto bank = make();
        const std::array recipes{Recipe(1, 1)};
        bank->Prepare(recipes, stop, {}, Compile);
        auto changed = recipes[0].pipeline;
        changed.inactive = 99;
        assert(!bank->Lookup(recipes[0].config, changed));
        instance.dynamic = true;
    }

    // CodexAstraUlt-2: All known PSOs are attempted, but only ranked winners are
    // resident. Transient warming must not silently become a selectable pipeline.
    {
        auto bank = make();
        std::vector<Precompile::Recipe> recipes;
        for (unsigned i = 0; i < 150; ++i) recipes.push_back(Recipe(1, i));
        recipes.back().uses = 10000;
        const auto builds = GraphicsPipeline::builds;
        bank->Prepare(recipes, stop, {}, Compile);
        assert(GraphicsPipeline::builds - builds == 150);
        assert(bank->Stats().shader_attempts == 1);
        assert(bank->Stats().resident_ready == 128 && bank->Stats().warmed_only == 22);
        assert(bank->ResidentModuleCount() == 1);
        assert(bank->Lookup(recipes.back().config, recipes.back().pipeline));
        assert(!bank->Lookup(recipes[148].config, recipes[148].pipeline));
        assert(bank->Stats().unattempted == 0);
    }
    {
        auto bank = make();
        std::vector<Precompile::Recipe> recipes;
        for (unsigned i = 0; i < 70; ++i) recipes.push_back(Recipe(i, i));
        Shader::peak = Shader::live;
        bank->Prepare(recipes, stop, {}, Compile);
        assert(bank->Stats().ready_modules == 70);
        assert(bank->ResidentModuleCount() == 64);
        assert(bank->Stats().resident_ready == 64 && bank->Stats().warmed_only == 6);
        assert(Shader::peak <= 66); // CodexAstraUlt-2: 64 resident, one transient, trivial VS.
    }
    for (const std::size_t budget : {std::size_t{0}, std::size_t{1}, std::size_t{64}}) {
        auto bank = make();
        std::vector<Precompile::Recipe> recipes;
        for (unsigned i = 0; i < 70; ++i) recipes.push_back(Recipe(i, i));
        Shader::peak = Shader::live;
        bank->Prepare(recipes, stop, {}, Compile, budget);
        assert(!bank->Stats().failed);
        if (!budget) {
            assert(bank->Stats().pipeline_attempts == 0 && bank->Stats().budget_deferred == 70);
        } else {
            assert(bank->Stats().pipeline_attempts == 70);
            assert(bank->ResidentModuleCount() == budget - 1);
            assert(Shader::peak <= budget + 1);
        }
    }

    // CodexAstraUlt-2: Null, pending and failed shader results must not reach the
    // driver; failure aborts subsequent recipes without skipping a gameplay draw.
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto bank = make();
        const std::array recipes{Recipe(1, 1), Recipe(2, 2)};
        const auto builds = GraphicsPipeline::builds;
        bank->Prepare(recipes, stop, {}, [&](const auto&) -> std::unique_ptr<Shader> {
            if (mode == 0) return {};
            auto result = Compile({});
            if (mode == 1) result->done = false;
            if (mode == 2) result->failed = true;
            return result;
        });
        assert(GraphicsPipeline::builds == builds);
        assert(bank->Stats().failed == 1 && bank->Stats().aborted);
        assert(bank->Stats().unattempted == 2);
        assert(!bank->Lookup(recipes[0].config, recipes[0].pipeline));
    }
    {
        auto bank = make();
        const std::array recipes{Recipe(1, 1), Recipe(2, 2)};
        GraphicsPipeline::fail_next = true;
        bank->Prepare(recipes, stop, {}, Compile);
        assert(bank->Stats().pipeline_attempts == 1 && bank->Stats().failed == 1);
        assert(bank->Stats().unattempted == 1 && bank->Stats().resident_ready == 0);
    }
    {
        auto bank = make();
        const std::array recipes{Recipe(1, 1)};
        GraphicsPipeline::lose_next = true;
        bool propagated{};
        try { bank->Prepare(recipes, stop, {}, Compile); }
        catch (const OptionalCompileError& error) {
            propagated = error.kind == OptionalCompileFailure::DeviceLost;
        }
        assert(propagated && bank->Stats().failed == 1 && bank->Stats().aborted);
        assert(bank->ResidentPipelineCount() == 0);
    }
    {
        auto bank = make();
        const std::array recipes{Recipe(1, 1)};
        bool propagated{};
        try {
            bank->Prepare(recipes, stop, {}, [](const auto&) -> std::unique_ptr<Shader> {
                throw OptionalCompileError(OptionalCompileFailure::DeviceLost, "injected device loss");
            });
        } catch (const OptionalCompileError& error) {
            propagated = error.kind == OptionalCompileFailure::DeviceLost;
        }
        assert(propagated && bank->Stats().aborted && bank->Stats().failed == 1);
    }
    {
        auto bank = make();
        const std::array recipes{Recipe(1, 1)};
        bank->Prepare(recipes, stop, {}, [](const auto&) -> std::unique_ptr<Shader> {
            throw std::bad_alloc{};
        });
        assert(bank->Stats().aborted && bank->Stats().failed == 1);
    }

    // CodexAstraUlt-2: Cancellation retains completed resident owners and stops
    // new driver calls. Destruction checks every pipeline-before-module lifetime.
    {
        auto bank = make();
        std::vector<Precompile::Recipe> recipes;
        for (unsigned i = 0; i < 10; ++i) recipes.push_back(Recipe(1, i));
        bank->Prepare(recipes, stop, [&](auto, auto current, auto, const auto&) {
            if (current == 3) stop = true;
        }, Compile);
        assert(bank->Stats().cancelled && bank->Stats().pipeline_attempts == 3);
        assert(bank->Stats().unattempted == 7 && bank->ResidentPipelineCount() == 3);
        assert(bank->Lookup(recipes[0].config, recipes[0].pipeline));
        stop = false;
    }
    assert(Shader::live == 1 && GraphicsPipeline::live == 0);
    std::cout << "PASS precompiled bank: exact lookup, grouping, residency, warm-through, "
                 "shared budgets, null/pending/failure, device loss, cancellation and owner lifetime\n";
}
