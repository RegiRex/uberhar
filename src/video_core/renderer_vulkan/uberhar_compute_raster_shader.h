// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <array>
#include <string_view>
#include "common/common_types.h"

namespace Vulkan {

// CodexAstraLocal: One draw owns its viewport, merger and complete triangle-list
// range. Keep the minimum Vulkan 128-byte push-constant limit; fragment uniforms
// and texture/LUT resources retain their separate production descriptors.
struct ComputeRasterPush {
    std::array<u32, 4> extent;   // width, height, vertices, subpixel bits
    std::array<float, 4> viewport;
    std::array<s32, 4> rectangle; // left, bottom, right, top (exclusive upper)
    std::array<u32, 4> merger;  // channel mask, depth compare, flags, logic op
    std::array<u32, 4> stencil; // compare, reference, read|write<<8, fail|zfail<<8|pass<<16
    std::array<u32, 4> blend;   // RGB equation, A equation, four factor nibbles, constant
    std::array<u32, 4> geometry; // cull, flip Y, first vertex word, stencil word offset
    std::array<u32, 4> reserved{}; // native color/depth encoding, unused, proctex coord
};
static_assert(sizeof(ComputeRasterPush) == 128);

// CodexAstraLocal: Append after the shared production fragment evaluator. Each
// invocation owns one pixel, visits triangles in submission order, and performs
// the complete fixed-function merger. Ordinary textures still come from the
// real rasterizer cache; no CPU-generated colors or oracle pixels enter here.
inline constexpr std::string_view ComputeRasterKernel = R"(
layout(local_size_x=8,local_size_y=8) in;
layout(push_constant) uniform RasterDraw {
    uvec4 extent;
    vec4 viewport;
    ivec4 rectangle;
    uvec4 merger;
    uvec4 stencil;
    uvec4 blend;
    uvec4 geometry;
    uvec4 reserved;
} draw;
layout(set=2,binding=0,std430) readonly buffer Vertices { uint words[]; } vertices;
layout(set=2,binding=1,std430) buffer DepthStencil { uint words[]; } depth_stencil;
layout(set=2,binding=2,r32ui) uniform uimage2D target_color;
// CodexAstraLocal: Packed targets expand to one native word per pixel so
// neighboring 16-bit pixels never race during ordered read/modify/write.
layout(set=2,binding=3,std430) buffer ColorWords { uint words[]; } target_words;

float vertex_word(uint vertex, uint component) {
    return uintBitsToFloat(vertices.words[draw.geometry.z+vertex*22u+component]);
}
vec4 vertex_position(uint vertex) {
    return vec4(vertex_word(vertex,0u),vertex_word(vertex,1u),
                vertex_word(vertex,2u),vertex_word(vertex,3u));
}

// CodexAstraLocal: Two-word signed edges avoid overflowing 32-bit products at
// actual framebuffer scales without requiring the optional shaderInt64 feature.
uvec2 wide_product(int a,int b) {
    int high,low; imulExtended(a,b,high,low); return uvec2(uint(low),uint(high));
}
uvec2 wide_subtract(uvec2 a,uvec2 b) {
    return uvec2(a.x-b.x,a.y-b.y-uint(a.x<b.x));
}
uvec2 edge(ivec2 a,ivec2 b,ivec2 p) {
    return wide_subtract(wide_product(b.x-a.x,p.y-a.y),wide_product(b.y-a.y,p.x-a.x));
}
bool negative(uvec2 e) { return (e.y&0x80000000u)!=0u; }
bool zero_edge(uvec2 e) { return (e.x|e.y)==0u; }
bool covered(uvec2 e,ivec2 a,ivec2 b) {
    ivec2 d=b-a;
    return !negative(e) && (!zero_edge(e) || d.y<0 || (d.y==0 && d.x<0));
}
vec3 project(vec4 p) {
    precise float inverse_w=1.0/p.w;
    precise vec2 ndc=p.xy*inverse_w;
    if(draw.geometry.y!=0u) ndc.y=-ndc.y;
    precise vec2 half_extent=draw.viewport.zw*0.5;
    precise vec2 screen=ndc*half_extent;
    screen=screen+(draw.viewport.xy+half_extent);
    precise float window_depth=-p.z*inverse_w;
    return vec3(screen,window_depth);
}

// CodexAstraLocal: Preserve the retained general-W plane formulation. Gradients
// use unsnapped positions while directed-edge coverage uses subpixel endpoints.
vec3 plane(vec3 values,vec2 a,vec2 b,vec2 c) {
    precise vec2 u=a-b,v=c-a;
    precise float determinant=u.x*v.y-u.y*v.x;
    precise float inverse_area=1.0/determinant;
    u=u*inverse_area;v=v*inverse_area;
    precise float da=values.x-values.y,dc=values.z-values.x;
    precise float gx=da*v.y-dc*u.y;
    precise float gy=dc*u.x-da*v.x;
    precise vec2 anchor=a-vec2(0.5);
    precise float terms=gx*anchor.x+gy*anchor.y;
    precise float origin=values.x-terms;
    return vec3(origin,gx,gy);
}
float evaluate_plane(vec3 coefficients,vec2 pixel) {
    precise float value=coefficients.x+coefficients.y*pixel.x;
    value=value+coefficients.z*pixel.y;
    return value;
}
vec3 varying_plane(uvec3 ids,uint component,vec3 inverse_w,vec2 a,vec2 b,vec2 c) {
    precise vec3 values=vec3(vertex_word(ids.x,component),vertex_word(ids.y,component),
                            vertex_word(ids.z,component))*inverse_w;
    return plane(values,a,b,c);
}
float perspective_value(vec3 coefficients,vec3 w_plane,vec2 pixel) {
    precise float result=evaluate_plane(coefficients,pixel)/evaluate_plane(w_plane,pixel);
    return result;
}
bool compare_value(uint function,uint incoming,uint stored) {
    switch(function) {
    case 0u:return false;case 1u:return true;case 2u:return incoming==stored;
    case 3u:return incoming!=stored;case 4u:return incoming<stored;
    case 5u:return incoming<=stored;case 6u:return incoming>stored;
    case 7u:return incoming>=stored;
    } return false;
}
// CodexAstraLocal: Floating fallback depth compares values, not their bit
// patterns; this preserves IEEE zero/NaN comparisons instead of uint ordering.
bool compare_depth(uint function,uint incoming,uint stored) {
    if(draw.reserved.y!=18u) return compare_value(function,incoming,stored);
    float a=uintBitsToFloat(incoming),b=uintBitsToFloat(stored);
    switch(function) {
    case 0u:return false;case 1u:return true;case 2u:return a==b;
    case 3u:return a!=b;case 4u:return a<b;case 5u:return a<=b;
    case 6u:return a>b;case 7u:return a>=b;
    } return false;
}
uint stencil_action(uint action,uint old_value) {
    switch(action) {
    case 0u:return old_value;case 1u:return 0u;case 2u:return draw.stencil.y;
    case 3u:return min(old_value+1u,255u);case 4u:return old_value==0u?0u:old_value-1u;
    case 5u:return old_value^255u;case 6u:return (old_value+1u)&255u;
    case 7u:return (old_value-1u)&255u;
    } return old_value;
}
uint update_stencil(uint action,uint stored) {
    uint mask=(draw.stencil.z>>8u)&255u;
    return (stored&~mask)|(stencil_action(action,stored)&mask);
}
vec4 blend_factor(uint factor,vec4 src,vec4 dst,vec4 constant_color) {
    switch(factor) {
    case 0u:return vec4(0.0);case 1u:return vec4(1.0);
    case 2u:return src;case 3u:return vec4(1.0)-src;
    case 4u:return dst;case 5u:return vec4(1.0)-dst;
    case 6u:return src.aaaa;case 7u:return vec4(1.0)-src.aaaa;
    case 8u:return dst.aaaa;case 9u:return vec4(1.0)-dst.aaaa;
    case 10u:return constant_color;case 11u:return vec4(1.0)-constant_color;
    case 12u:return constant_color.aaaa;case 13u:return vec4(1.0)-constant_color.aaaa;
    case 14u:return vec4(vec3(min(src.a,1.0-dst.a)),1.0);
    } return vec4(0.0);
}
vec4 blend_equation(uint equation,vec4 src,vec4 dst) {
    switch(equation) {
    case 0u:return src+dst;case 1u:return src-dst;case 2u:return dst-src;
    case 3u:return min(src,dst);case 4u:return max(src,dst);
    } return vec4(0.0);
}
uint logic_operation(uint operation,uint src,uint dst) {
    switch(operation) {
    case 0u:return 0u;case 1u:return src&dst;case 2u:return src&~dst;case 3u:return src;
    case 4u:return 0xffffffffu;case 5u:return ~src;case 6u:return dst;case 7u:return ~dst;
    case 8u:return ~(src&dst);case 9u:return src|dst;case 10u:return ~(src|dst);
    case 11u:return src^dst;case 12u:return ~(src^dst);case 13u:return ~src&dst;
    case 14u:return src|~dst;case 15u:return ~src|dst;
    } return dst;
}
uint merge_color(vec4 incoming,uint old_color) {
    uint next_color;
    if((draw.merger.z&32u)!=0u) {
        vec4 dst=DecodeColor(old_color,draw.reserved.x),constant_color=unpackUnorm4x8(draw.blend.w);
        uint factors=draw.blend.z;
        vec4 rgb=blend_equation(draw.blend.x,
            incoming*blend_factor(factors&15u,incoming,dst,constant_color),
            dst*blend_factor((factors>>4u)&15u,incoming,dst,constant_color));
        vec4 alpha=blend_equation(draw.blend.y,
            incoming*blend_factor((factors>>8u)&15u,incoming,dst,constant_color),
            dst*blend_factor((factors>>12u)&15u,incoming,dst,constant_color));
        next_color=EncodeColor(vec4(rgb.rgb,alpha.a),draw.reserved.x);
    } else next_color=logic_operation(draw.merger.w,EncodeColor(incoming,draw.reserved.x),old_color);
    // CodexAstraLocal: Mask and quantize in the native attachment precision
    // after every primitive, not after an intermediate RGBA8 accumulation.
    uint mask=ColorWriteMask(draw.merger.x,draw.reserved.x);
    return (old_color&~mask)|(next_color&mask);
}

void main() {
    uvec2 p=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(p,draw.extent.xy)) ||
       int(p.x)<draw.rectangle.x || int(p.y)<draw.rectangle.y ||
       int(p.x)>=draw.rectangle.z || int(p.y)>=draw.rectangle.w) return;
    // CodexAstraLocal: The owner admits complete validated triangle lists only.
    // The GPU producer entry must establish this contract before this kernel.
    uint pixel=p.y*draw.extent.x+p.x;
    bool has_depth=(draw.merger.z&1u)!=0u;
    bool has_stencil=(draw.merger.z&8u)!=0u;
    uint depth=has_depth ? depth_stencil.words[pixel] : 0u;
    if(draw.reserved.y==17u) depth&=0xffffffu;
    uint stencil_word=draw.geometry.w+pixel/4u,stencil_shift=(pixel%4u)*8u;
    // CodexAstraLocal: Adjacent invocations own different bytes of this packed
    // word. Reads are atomic too, avoiding a non-atomic/atomic access race.
    uint stencil_value=has_stencil ? (atomicAdd(depth_stencil.words[stencil_word],0u)>>stencil_shift)&255u : 0u;
    bool has_color=(draw.merger.z&64u)!=0u;
    bool buffered_color=(draw.merger.z&256u)!=0u;
    uint color=has_color ? (buffered_color ? target_words.words[pixel] :
                                          imageLoad(target_color,ivec2(p)).x) : 0u;
    bool color_changed=false,depth_changed=false,stencil_changed=false;
    int scale=1<<draw.extent.w;
    ivec2 point=ivec2(p)*scale+ivec2(scale/2);
    vec2 fp=vec2(p);
    // CodexAstraLocal: These four helper positions are evaluated from each same
    // primitive even outside coverage, preserving derivative direction and quad parity.
    vec2 dx0=vec2(p.x&~1u,p.y),dx1=dx0+vec2(1.0,0.0);
    vec2 dy0=vec2(p.x,p.y&~1u),dy1=dy0+vec2(0.0,1.0);
    for(uint first=0u;first<draw.extent.z;first+=3u) {
        uvec3 ids=uvec3(first,first+1u,first+2u);
        vec4 va=vertex_position(ids.x),vb=vertex_position(ids.y),vc=vertex_position(ids.z);
        vec3 a=project(va),b=project(vb),c=project(vc);
        ivec2 ia=ivec2(roundEven(a.xy*float(scale))),ib=ivec2(roundEven(b.xy*float(scale))),
              ic=ivec2(roundEven(c.xy*float(scale)));
        uvec2 area=edge(ia,ib,ic);
        if(zero_edge(area)) continue;
        bool reversed=negative(area);
        // CodexAstraLocal: Match the existing Vulkan front-face/cull selection;
        // its viewport-flip policy changes which original winding survives.
        bool counter_clockwise=reversed;
        bool front=draw.geometry.x==2u ? !counter_clockwise : counter_clockwise;
        if((draw.geometry.x==1u || draw.geometry.x==2u) &&
           (draw.geometry.y!=0u ? front : !front)) continue;
        if(reversed) { uint id=ids.y;ids.y=ids.z;ids.z=id;
            vec3 v=b;b=c;c=v;ivec2 iv=ib;ib=ic;ic=iv;vec4 q=vb;vb=vc;vc=q; }
        // CodexAstraLocal: Subpixel rounding can give collinear floating points
        // nonzero coverage area. Reject their undefined interpolation plane.
        precise vec2 plane_u=a.xy-b.xy,plane_v=c.xy-a.xy;
        precise float determinant=plane_u.x*plane_v.y-plane_u.y*plane_v.x;
        if(determinant==0.0||isnan(determinant)||isinf(determinant))continue;
        if(!covered(edge(ib,ic,point),ib,ic) || !covered(edge(ic,ia,point),ic,ia) ||
           !covered(edge(ia,ib,point),ia,ib)) continue;
        vec3 reciprocal=vec3(1.0/va.w,1.0/vb.w,1.0/vc.w);
        vec3 wp=plane(reciprocal,a.xy,b.xy,c.xy);
        vec3 zp=plane(vec3(a.z,b.z,c.z),a.xy,b.xy,c.xy);
        UberharFragmentInput attributes;
        for(uint component=0u;component<4u;++component)
            attributes.primary_color[component]=perspective_value(varying_plane(ids,4u+component,reciprocal,a.xy,b.xy,c.xy),wp,fp);
        for(uint tex=0u;tex<3u;++tex) for(uint component=0u;component<2u;++component) {
            vec3 plane_uv=varying_plane(ids,8u+tex*2u+component,reciprocal,a.xy,b.xy,c.xy);
            float uv=perspective_value(plane_uv,wp,fp);
            if(tex==0u) attributes.texcoord0[component]=uv;
            else if(tex==1u) attributes.texcoord1[component]=uv;
            else attributes.texcoord2[component]=uv;
            attributes.texcoord_dx[tex][component]=perspective_value(plane_uv,wp,dx1)-perspective_value(plane_uv,wp,dx0);
            attributes.texcoord_dy[tex][component]=perspective_value(plane_uv,wp,dy1)-perspective_value(plane_uv,wp,dy0);
            // CodexAstraLocal: Preserve helper coordinates themselves for
            // projected/cube sampling; derivatives cannot reconstruct projection
            // correctly from only a center value and a raw-UV difference.
            if(tex==0u) {
                attributes.texture0_dx0[component]=perspective_value(plane_uv,wp,dx0);
                attributes.texture0_dx1[component]=perspective_value(plane_uv,wp,dx1);
                attributes.texture0_dy0[component]=perspective_value(plane_uv,wp,dy0);
                attributes.texture0_dy1[component]=perspective_value(plane_uv,wp,dy1);
            }
            if(tex==draw.reserved.w) {
                attributes.proctex_dx0[component]=perspective_value(plane_uv,wp,dx0);
                attributes.proctex_dx1[component]=perspective_value(plane_uv,wp,dx1);
                attributes.proctex_dy0[component]=perspective_value(plane_uv,wp,dy0);
                attributes.proctex_dy1[component]=perspective_value(plane_uv,wp,dy1);
            }
        }
        vec3 texture_q=varying_plane(ids,14u,reciprocal,a.xy,b.xy,c.xy);
        attributes.texcoord0_w=perspective_value(texture_q,wp,fp);
        attributes.texture0_dx0.z=perspective_value(texture_q,wp,dx0);
        attributes.texture0_dx1.z=perspective_value(texture_q,wp,dx1);
        attributes.texture0_dy0.z=perspective_value(texture_q,wp,dy0);
        attributes.texture0_dy1.z=perspective_value(texture_q,wp,dy1);
        for(uint component=0u;component<4u;++component)
            attributes.normquat[component]=perspective_value(varying_plane(ids,15u+component,reciprocal,a.xy,b.xy,c.xy),wp,fp);
        for(uint component=0u;component<3u;++component)
            attributes.view[component]=perspective_value(varying_plane(ids,19u+component,reciprocal,a.xy,b.xy,c.xy),wp,fp);
        attributes.fragment_coord=vec4(fp+vec2(0.5),evaluate_plane(zp,fp),evaluate_plane(wp,fp));
        vec4 fragment_color;float fragment_depth;
        if(!UberharEvaluateFragment(attributes,fragment_color,fragment_depth)) continue;
        // CodexAstraLocal: Shadow output uses the production unrounded TEV
        // green and truncating depth/shade update. Pixel ownership supplies
        // primitive order without CAS, and ordinary blend/depth state is inert.
        if((draw.merger.z&128u)!=0u) {
            uint updated=UpdateShadow(color,ShadowDepth(fragment_depth),
                ShadowShade(fragment_color.g),shadow_bias_constant,shadow_bias_linear);
            color_changed=color_changed || updated!=color;color=updated;continue;
        }
        uint candidate=EncodeDepth(fragment_depth,draw.reserved.y);
        if((draw.merger.z&16u)!=0u &&
           !compare_value(draw.stencil.x,draw.stencil.y&(draw.stencil.z&255u),stencil_value&(draw.stencil.z&255u))) {
            uint updated=update_stencil(draw.stencil.w&255u,stencil_value);
            stencil_changed=stencil_changed || updated!=stencil_value;stencil_value=updated;continue;
        }
        bool depth_pass=(draw.merger.z&2u)==0u || compare_depth(draw.merger.y,candidate,depth);
        if((draw.merger.z&16u)!=0u) {
            uint action=(draw.stencil.w>>(depth_pass?16u:8u))&255u;
            uint updated=update_stencil(action,stencil_value);
            stencil_changed=stencil_changed || updated!=stencil_value;stencil_value=updated;
        }
        if(!depth_pass) continue;
        if((draw.merger.z&4u)!=0u) {depth=candidate;depth_changed=true;}
        uint next_color=has_color ? merge_color(fragment_color,color) : color;
        color_changed=color_changed || next_color!=color;color=next_color;
    }
    if(color_changed) {
        if(buffered_color) target_words.words[pixel]=color;
        else imageStore(target_color,ivec2(p),uvec4(color));
    }
    if(depth_changed) depth_stencil.words[pixel]=depth;
    if(stencil_changed) {
        // CodexAstraLocal: Vulkan copies stencil as packed bytes. Atomic masked
        // updates preserve other invocations' distinct byte lanes in the same word.
        atomicAnd(depth_stencil.words[stencil_word],~(255u<<stencil_shift));
        atomicOr(depth_stencil.words[stencil_word],stencil_value<<stencil_shift);
    }
}
)";
} // namespace Vulkan
