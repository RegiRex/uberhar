#!/usr/bin/env python3
"""AstraPro: Exercise the production fixed-attribute writer with real PICA/layout types.
Only its owning rasterizer and stream-buffer allocation are modeled. This checks
reserved bytes and written layout, NOT shader or device image parity.
"""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, default=Path('src/video_core/renderer_vulkan/vk_rasterizer.cpp'))
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
s = a.source.read_text()
start = s.index('void RasterizerVulkan::SetupFixedAttribs()')
end = s.index('\n}\n', start) + 3
body = s[start:end]
header = r'''
#include <cstdio>
#include <cstring>
#include <vector>
#include <tuple>
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_unit.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
namespace Vulkan {
struct StreamStub {
    std::vector<u8> storage;
    u32 reserved{}, committed{};
    auto Map(u32 n, u32) {
        reserved = n; storage.assign(n + 32, 0xA5);
        return std::tuple{storage.data(), 0U, false};
    }
    void Commit(u32 n) { committed = n; }
};
struct RasterizerVulkan {
    Pica::RegsInternal regs{};
    struct { Pica::AttributeBuffer input_default_attributes{}; } pica;
    PipelineInfo pipeline_info{};
    std::array<bool, 16> enable_attributes{};
    std::array<u32, 16> binding_offsets{};
    StreamStub stream_buffer;
    u32 fixed_attribute_max_bytes{};
    u64 fixed_attribute_over_legacy{};
    void SetupFixedAttribs();
};
'''
tests = r'''
}
int main() {
    unsigned checks = 0, failures = 0;
    for (unsigned pattern = 0; pattern < 2; ++pattern) {
        for (unsigned enabled = 0; enabled < 65536; ++enabled) {
            Vulkan::RasterizerVulkan r;
            r.regs.pipeline.vertex_attributes.attribute_mask.Assign(0xFFF);
            r.regs.vs.input_attribute_to_register_map_low = pattern ? 0 : 0x76543210;
            r.regs.vs.input_attribute_to_register_map_high = pattern ? 0 : 0xFEDCBA98;
            for (unsigned i = 0; i < 16; ++i) {
                r.enable_attributes[i] = (enabled >> i) & 1;
                for (unsigned c = 0; c < 4; ++c)
                    r.pica.input_default_attributes[i][c] = Pica::f24::FromFloat32(float(i*4+c));
            }
            r.SetupFixedAttribs();
            const auto& b = r.stream_buffer;
            bool guard = true;
            for (unsigned i = b.reserved; i < b.storage.size(); ++i)
                guard &= b.storage[i] == 0xA5;
            const unsigned distinct = pattern ? ((enabled & 1) ? 0 : 1)
                                                : 16 - std::popcount(enabled);
            ++checks;
            if (b.committed != (distinct + 1) * 16 || b.committed > b.reserved || !guard)
                ++failures;
        }
    }
    std::printf("Fixed attribute reserve: %u cases, %u failures; production writer, modeled buffer\n", checks, failures);
    return failures ? 1 : 0;
}
'''
out=Path('build/uberhar-probe/fixed-reserve');out.parent.mkdir(parents=True,exist_ok=True)
cpp=out.with_suffix('.cpp');cpp.write_text(header + body + tests)
flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else ['-O2']
subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*flags,'-DFMT_HEADER_ONLY','-DXXH_INLINE_ALL','-Isrc','-Iexternals/fmt/include','-Iexternals/boost','-Iexternals/xxHash','-Iexternals/vulkan-headers/include',str(cpp),'-o',str(out)],check=True)
subprocess.run([str(out)],check=True)
