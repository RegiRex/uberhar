#!/usr/bin/env python3
"""CodexAstraLocal: Exercise actual synchronous preparation with modeled endpoints.

The source-derived prologue checks reuse, fresh memory lookup and error ordering.
It does not claim guest shader execution or replace the production loader tests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/vertex-preparation'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    args.output.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='run-', dir=args.output)).resolve()
    path = root / 'src/video_core/pica/pica_core.cpp'
    source = path.read_text()
    function = source[source.index('void PicaCore::LoadVertices('):]
    begin = function.index('    // Read and validate vertex information')
    end = function.index('    // CodexAstraLocal TEST-ONLY LOG: Choose Software observation')
    actual = function[begin:end]
    prefix = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
using u8=std::uint8_t; using u16=std::uint16_t; using u32=std::uint32_t; using PAddr=u32;
#define ASSERT(x) assert(x)
std::vector<int> events;
struct Memory { bool valid=true; u8 value=1; u8 read{};
    u8* GetPhysicalPointer(PAddr) { events.push_back(3); read=value; return valid? &value: nullptr; }
};
struct Pipeline { struct { PAddr GetPhysicalBaseAddress() const { return 0; } } vertex_attributes;
    struct { u32 offset{}; u32 format{}; } index_array; };
struct VertexLoader { VertexLoader(Memory&,const Pipeline&) { events.push_back(1); } };
struct ShaderUnit {};
struct Raster { void ValidateSemantics() { events.push_back(2); } };
struct Shader { bool fail{}; void SetupBatch(int&,u32) { events.push_back(4); if(fail) throw std::runtime_error("setup"); } };
struct Geometry { void Reconfigure() {events.push_back(5);} void Setup(Shader*) {events.push_back(6);}
    bool NeedIndexInput() const {return false;} };
struct Timing { void Unsupported() {events.push_back(7);} };
struct Owner {
    struct PreparedVertexDraw { std::optional<VertexLoader> loader; bool semantics_validated{}; bool batch_setup_ready{}; };
    struct { struct { Pipeline pipeline; Raster rasterizer; struct {u32 main_offset{};} vs; } internal; } regs;
    Memory memory; std::unique_ptr<Shader> shader_engine=std::make_unique<Shader>();
    Geometry geometry_pipeline; int vs_setup{}; Timing timing; Timing* vertex_timing=&timing;
    void Run(bool is_indexed, PreparedVertexDraw* draw_preparation=nullptr) {
"""
    suffix = r"""
    }
};
void Check(bool value) { if(!value) throw std::runtime_error("preparation contract"); }
int main() {
    Owner owner;
    events.clear(); owner.Run(false);
    Check(events==std::vector<int>({1,2,3,4,5,6}));
    // CodexAstraLocal: Model refusal after completed setup and a subsequent
    // readback changing input bytes. Reuse must still fetch the current bytes.
    Owner::PreparedVertexDraw ready; ready.loader.emplace(owner.memory,owner.regs.internal.pipeline);
    ready.semantics_validated=true; ready.batch_setup_ready=true;
    owner.memory.value=42; events.clear(); owner.Run(false,&ready);
    Check(events==std::vector<int>({3}) && owner.memory.read==42);
    // CodexAstraLocal: Refusal before setup cannot skip the original setup path.
    Owner::PreparedVertexDraw partial; partial.loader.emplace(owner.memory,owner.regs.internal.pipeline);
    events.clear(); owner.Run(false,&partial);
    Check(events==std::vector<int>({2,3,4,5,6}));
    partial.semantics_validated=true; events.clear(); owner.Run(false,&partial);
    Check(events==std::vector<int>({3,4,5,6}));
    // CodexAstraLocal: Original invalid-index return precedes any shader or
    // geometry setup, including when only the loader was previously prepared.
    owner.memory.valid=false; events.clear(); owner.Run(false);
    Check(events==std::vector<int>({1,2,3,7}));
    events.clear(); owner.Run(false,&partial);
    Check(events==std::vector<int>({3,7}));
    // CodexAstraLocal: Failed shader setup never advances geometry or marks a
    // partial preparation as completed; a later invocation must try setup.
    owner.memory.valid=true; owner.shader_engine->fail=true; events.clear();
    bool failed=false; try {owner.Run(false,&partial);} catch(const std::runtime_error&) {failed=true;}
    Check(failed && events==std::vector<int>({3,4}) && !partial.batch_setup_ready);
    owner.shader_engine->fail=false; events.clear(); owner.Run(false,&partial);
    Check(events==std::vector<int>({3,4,5,6}));
}
"""
    cpp = out/'preparation.cpp'; cpp.write_text(prefix+actual+suffix)
    steps=[]
    for name, command in [('compile',['c++','-std=c++20','-O2',str(cpp),'-o',str(out/'preparation')]),
                          ('execute',[str(out/'preparation')])]:
        with (out/(name+'.stdout')).open('w') as stdout, (out/(name+'.stderr')).open('w') as stderr:
            result=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=60)
        steps.append({'name':name,'command':command,'exit_code':result.returncode})
        if result.returncode: break
    (out/'provenance.json').write_text(json.dumps({'author':'CodexAstraLocal',
        'source':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
        'steps':steps,'scope':'Actual LoadVertices prologue; modeled memory/shader/geometry endpoints',
        'pass':len(steps)==2 and all(s['exit_code']==0 for s in steps)},indent=2)+'\n')
    if len(steps)!=2 or any(s['exit_code'] for s in steps):
        raise SystemExit('FAIL: see '+str(out))
    print('PASS: same-draw preparation reuse, live memory lookup and failure order; evidence '+str(out))


if __name__=='__main__':
    main()
