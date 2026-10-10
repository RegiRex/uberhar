// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <fmt/format.h>
#include "video_core/shader/generator/glsl_compute_vertex_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_decompiler.h"

namespace Pica::Shader::Generator::GLSL {
namespace {
// CodexAstraLocal: Keep original compact loads and FIFO carry in one invocation.
// The output has a small completion/tail header, semantic scratch, then a packed
// 88-byte clipped triangle stream. Only the final status authorizes rasterization.
constexpr std::string_view Prefix = R"glsl(#version 450
layout(local_size_x=1) in;
layout(set=0,binding=0,std430) readonly buffer Raw { uint raw[]; };
layout(set=0,binding=1,std430) readonly buffer Meta { uint meta[]; };
layout(set=0,binding=2,std430) buffer Result { uint result[]; };
struct Uniforms { uint b; uvec4 i[4]; vec4 f[96]; };
Uniforms uniforms;
vec4 input_regs[16];
vec4 output_regs[16];
)glsl";

constexpr std::string_view Body = R"glsl(
uint byte_at(uint p) { return (raw[p/4u] >> ((p%4u)*8u)) & 255u; }
uint word_at(uint p) { return byte_at(p)|(byte_at(p+1u)<<8)|(byte_at(p+2u)<<16)|(byte_at(p+3u)<<24); }
uint short_at(uint p) { return byte_at(p)|(byte_at(p+1u)<<8); }
uint vertex_at(uint ordinal) {
    if (meta[5]==0u) return meta[2]+ordinal;
    return meta[8]==1u ? byte_at(meta[7]+ordinal) : short_at(meta[7]+ordinal*2u);
}
bool fits(uint offset,uint bytes) { return offset<=meta[6] && bytes<=meta[6]-offset; }
// CodexAstraLocal: A ready Strip/Fan or two-vertex List tail can emit a triangle
// from just one new input. Six standard planes plus one custom plane can leave
// ten polygon vertices, so reserve eight clipped triangles per assembled one.
uint triangle_capacity() {
    if(meta[14]==0u||meta[14]==3u)return (meta[136]+meta[1])/3u;
    uint total=(meta[137]!=0u?2u:meta[136])+meta[1];
    return total>2u?total-2u:0u;
}
uint semantic_word(uint reference) { return 64u+reference*24u; }
uint hardware_base() { return 64u+(meta[1]+2u)*24u; }
bool valid_input() {
    if (meta.length()!=193 || meta[0]!=0x43565231u || meta[1]==0u || meta[1]>4096u ||
        meta[3]<meta[2] || meta[3]>65535u || meta[4]==0u || meta[4]>16u || meta[5]>1u ||
        meta[6]>uint(raw.length())*4u || meta[14]>3u ||
        meta[15]!=COMPUTE_ENTRY) return false;
    if(meta[137]>1u||meta[138]>1u)return false;
    uint live;
    if(meta[14]==0u||meta[14]==3u) {
        if(meta[136]>2u||meta[137]!=0u)return false;
        live=(1u<<meta[136])-1u;
    } else {
        if(meta[136]>1u||(meta[14]==2u&&meta[137]!=0u&&meta[136]!=1u))return false;
        live=meta[137]!=0u?3u:meta[136]!=0u?1u:0u;
    }
    if(meta[139]!=live || uint(result.length())!=hardware_base()+triangle_capacity()*24u*22u ||
       meta[188]>1u) return false;
    if(meta[188]!=0u)for(uint c=0u;c<4u;++c)
        if(isnan(uintBitsToFloat(meta[189u+c]))||isinf(uintBitsToFloat(meta[189u+c])))return false;
    if (!fits(meta[9],256u)||!fits(meta[10],1536u)||!fits(meta[11],16u)||!fits(meta[12],16u)) return false;
    if (meta[5]!=0u && (meta[8]<1u||meta[8]>2u||!fits(meta[7],meta[1]*meta[8]))) return false;
    for(uint a=0u;a<meta[4];++a) {
        uint b=16u+a*6u;
        if (meta[b]>15u || meta[b+5u]>1u) return false;
        if (meta[b+5u]!=0u) continue;
        uint f=meta[b+1u],e=meta[b+2u],stride=meta[b+3u];
        if(f>3u||e<1u||e>4u||stride>255u) return false;
        uint width=e*(f==3u?4u:f==2u?2u:1u);
        if (!fits(meta[b+4u],stride*(meta[3]-meta[2])+width)) return false;
    }
    for(uint s=0u;s<24u;++s) if(meta[112u+s]>64u) return false;
    for(uint o=0u;o<meta[1];++o) { uint v=vertex_at(o); if(v<meta[2]||v>meta[3]) return false; }
    for(uint b=0u;b<16u;++b) if(byte_at(meta[11]+b)>1u) return false;
    return true;
}
// CodexAstraLocal: Attribute order preserves aliases and untouched input carry;
// zero-stride data remains a live captured attribute, not a default substitution.
void load_vertex(uint v) {
    for(uint a=0u;a<meta[4];++a) {
        uint b=16u+a*6u;
        vec4 value=vec4(0,0,0,1);
        if(meta[b+5u]!=0u) {
            for(uint c=0u;c<4u;++c) value[c]=uintBitsToFloat(word_at(meta[9]+a*16u+c*4u));
        } else {
            uint f=meta[b+1u],width=f==3u?4u:f==2u?2u:1u;
            uint p=meta[b+4u]+(v-meta[2])*meta[b+3u];
            for(uint c=0u;c<meta[b+2u];++c) {
                uint q=p+c*width;
                if(f==3u)value[c]=uintBitsToFloat(word_at(q));
                else if(f==2u)value[c]=float(int(short_at(q)<<16)>>16);
                else if(f==1u)value[c]=float(byte_at(q));
                else value[c]=float(int(byte_at(q)<<24)>>24);
            }
        }
        input_regs[meta[b]]=value;
    }
}
bool finite_value(float v) { return !isnan(v)&&!isinf(v); }
bool store_vertex(uint ordinal) {
    for(uint c=0u;c<24u;++c) {
        uint source=meta[112u+c];
        float value=source==64u?1.0:output_regs[source/4u][source%4u];
        if(c>=8u&&c<12u) { value=abs(value); value=value<1.0?value:1.0; }
        if(c!=17u&&c!=21u&&!finite_value(value))return false;
        result[semantic_word(ordinal+2u)+c]=floatBitsToUint(value);
    }
    return true;
}

// CodexAstraLocal: Quaternion sign correction precedes homogeneous clipping,
// exactly where the accelerated 88-byte transport applies it to each triangle.
const uint hardware_source[22]=uint[](0,1,2,3,8,9,10,11,12,13,14,15,22,23,16,4,5,6,7,18,19,20);
struct ClipVertex { float lane[22]; float custom_distance; };
ClipVertex hardware_vertex(uint reference,uint first) {
    bool flip=false;
    if(reference!=first) {
        vec4 a,b;
        for(uint c=0u;c<4u;++c) {
            a[c]=uintBitsToFloat(result[semantic_word(first)+4u+c]);
            b[c]=uintBitsToFloat(result[semantic_word(reference)+4u+c]);
        }
        precise vec4 product=a*b;
        precise float lo=product.x+product.y,hi=product.z+product.w;
        precise float sum=lo+hi;
        flip=sum<0.0;
    }
    ClipVertex value;
    for(uint c=0u;c<22u;++c) {
        uint word=result[semantic_word(reference)+hardware_source[c]];
        if(flip&&c>=15u&&c<19u)word^=0x80000000u;
        value.lane[c]=uintBitsToFloat(word);
    }
    // CodexAstraLocal: Match production SanitizeVertex before homogeneous
    // clipping, without changing the original semantic persistent tail.
    float ndc_z=value.lane[2]/value.lane[3];
    if(ndc_z>0.0&&ndc_z<0.000001)value.lane[2]=0.0;
    if(ndc_z< -1.0&&ndc_z> -1.00001)value.lane[2]=-value.lane[3];
    // CodexAstraLocal: Native exports this vertex clip distance before clipping.
    // Carry it through every later intersection; re-evaluating the dot after a
    // standard-plane snap would change the varying's interpolation semantics.
    value.custom_distance=0.0;
    if(meta[188]!=0u) {
        vec4 coefficient=uintBitsToFloat(uvec4(meta[189],meta[190],meta[191],meta[192]));
        value.custom_distance=dot(coefficient,vec4(value.lane[0],value.lane[1],value.lane[2],value.lane[3]));
    }
    return value;
}
// CodexAstraLocal: Clip against all six standard homogeneous half spaces. The
// same parameter interpolates every original lane before perspective division.
// Nonfinite/zero-W generated vertices refuse the whole batch, not partial pixels.
float distance_to_plane(ClipVertex v,uint plane) {
    if(plane==0u)return -v.lane[2];
    if(plane==1u)return v.lane[3]+v.lane[2];
    if(plane==2u)return v.lane[3]+v.lane[0];
    if(plane==3u)return v.lane[3]-v.lane[0];
    if(plane==4u)return v.lane[3]+v.lane[1];
    if(plane==5u)return v.lane[3]-v.lane[1];
    return v.custom_distance;
}
bool same_vertex(ClipVertex a,ClipVertex b) {
    for(uint lane=0u;lane<22u;++lane)
        if(floatBitsToUint(a.lane[lane])!=floatBitsToUint(b.lane[lane]))return false;
    return floatBitsToUint(a.custom_distance)==floatBitsToUint(b.custom_distance);
}
bool append_clip(inout ClipVertex vertices[10],inout uint count,ClipVertex v) {
    if(count>0u&&same_vertex(vertices[count-1u],v))return true;
    if(count==10u)return false;
    vertices[count++]=v;return true;
}
bool intersection(ClipVertex a,ClipVertex b,uint plane,out ClipVertex value) {
    // CodexAstraLocal: Adjacent triangles may traverse one edge oppositely.
    // Position-only canonical ordering gives identical crossing arithmetic;
    // exact endpoints preserve all original bits and avoid duplicate clip slots.
    bool swap_endpoints=false;
    for(uint lane=0u;lane<4u;++lane) {
        uint aw=floatBitsToUint(a.lane[lane]),bw=floatBitsToUint(b.lane[lane]);
        if(aw!=bw){swap_endpoints=aw>bw;break;}
    }
    if(swap_endpoints){ClipVertex old=a;a=b;b=old;}
    precise float da=distance_to_plane(a,plane),db=distance_to_plane(b,plane);
    if(da==0.0){value=a;return true;}
    if(db==0.0){value=b;return true;}
    precise float divisor=da-db;
    precise float t=da/divisor;
    if(!finite_value(divisor)||!finite_value(t)||t<0.0||t>1.0)return false;
    precise float inverse=1.0-t;
    for(uint lane=0u;lane<22u;++lane) {
        precise float left=a.lane[lane]*inverse,right=b.lane[lane]*t;
        precise float sum=left+right;
        if(!finite_value(sum))return false;
        value.lane[lane]=sum;
    }
    precise float custom_left=a.custom_distance*inverse,custom_right=b.custom_distance*t;
    precise float custom_sum=custom_left+custom_right;
    if(!finite_value(custom_sum))return false;
    value.custom_distance=plane==6u?0.0:custom_sum;
    // Exact plane coordinates avoid creating an outside endpoint by rounding;
    // other lanes retain the unmodified homogeneous interpolation result.
    if(plane==0u)value.lane[2]=0.0;
    if(plane==1u)value.lane[2]=-value.lane[3];
    if(plane==2u)value.lane[0]=-value.lane[3];
    if(plane==3u)value.lane[0]=value.lane[3];
    if(plane==4u)value.lane[1]=-value.lane[3];
    if(plane==5u)value.lane[1]=value.lane[3];
    return true;
}
bool clip_triangle(uvec3 references,inout uint cursor) {
    ClipVertex polygon[10];uint count=3u;
    for(uint i=0u;i<3u;++i)polygon[i]=hardware_vertex(references[i],references.x);
    for(uint plane=0u;plane<6u+meta[188];++plane) {
        if(count==0u)return true;
        ClipVertex next_polygon[10];uint next_count=0u;
        ClipVertex previous=polygon[count-1u];
        float pd=distance_to_plane(previous,plane);
        if(!finite_value(pd))return false;
        bool previous_inside=pd>=0.0;
        for(uint i=0u;i<count;++i) {
            ClipVertex current=polygon[i];float cd=distance_to_plane(current,plane);
            if(!finite_value(cd))return false;
            bool inside=cd>=0.0;
            if(inside!=previous_inside) {
                ClipVertex crossing;
                if(!intersection(previous,current,plane,crossing)||
                    !append_clip(next_polygon,next_count,crossing))return false;
            }
            if(inside&&!append_clip(next_polygon,next_count,current))return false;
            previous=current;previous_inside=inside;
        }
        if(next_count>1u&&same_vertex(next_polygon[0],next_polygon[next_count-1u]))--next_count;
        count=next_count;
        for(uint i=0u;i<count;++i)polygon[i]=next_polygon[i];
    }
    if(count<3u)return true;
    for(uint i=0u;i<count;++i) {
        float w=polygon[i].lane[3];
        if(!(w>0.0)||!finite_value(1.0/w))return false;
        for(uint plane=0u;plane<6u+meta[188];++plane)
            if(distance_to_plane(polygon[i],plane)<0.0)return false;
    }
    uint emitted=3u*(count-2u),capacity=triangle_capacity()*24u;
    if(cursor>capacity||emitted>capacity-cursor)return false;
    uint base=hardware_base();
    for(uint triangle=1u;triangle+1u<count;++triangle) {
        uvec3 selected=uvec3(0u,triangle,triangle+1u);
        for(uint corner=0u;corner<3u;++corner)for(uint lane=0u;lane<22u;++lane)
            result[base+(cursor+corner)*22u+lane]=floatBitsToUint(polygon[selected[corner]].lane[lane]);
        cursor+=3u;
    }
    return true;
}
void main() {
    result[0]=0u;result[1]=1u;
    if(!valid_input())return;
    uint n=meta[1],hits=0u,misses=0u,cursor=0u,size=0u;
    uint keys[64],references[64];
    for(uint r=0u;r<16u;++r) { input_regs[r]=vec4(0);output_regs[r]=vec4(0); }
    reset_compute_temporaries();
    conditional_code=bvec2(false);address_registers=ivec3(0);
    uniforms.b=0u;
    for(uint i=0u;i<16u;++i) {
        uniforms.b|=byte_at(meta[11]+i)<<i;
        uniforms.i[i/4u][i%4u]=byte_at(meta[12]+i);
    }
    for(uint i=0u;i<384u;++i)uniforms.f[i/4u][i%4u]=uintBitsToFloat(word_at(meta[10]+i*4u));
    for(uint slot=0u;slot<2u;++slot)if((meta[139]&(1u<<slot))!=0u)
        for(uint c=0u;c<24u;++c)result[semantic_word(slot)+c]=meta[140u+slot*24u+c];
    for(uint ordinal=0u;ordinal<n;++ordinal) {
        uint key=vertex_at(ordinal);int hit=-1;
        if(meta[5]!=0u)for(uint slot=0u;slot<size;++slot)if(keys[slot]==key){hit=int(slot);break;}
        if(hit>=0) {
            ++hits;uint reference=references[hit];
            for(uint c=0u;c<24u;++c)
                result[semantic_word(ordinal+2u)+c]=result[semantic_word(reference+2u)+c];
        } else {
            ++misses;load_vertex(key);exec_shader();
            if(!store_vertex(ordinal)){result[1]=2u;return;}
            if(meta[5]!=0u){keys[cursor]=key;references[cursor]=ordinal;cursor=(cursor+1u)%64u;size=min(64u,size+1u);}
        }
    }
    // CodexAstraLocal: Port the actual PICA SubmitVertex ordering, including
    // Strip ABC,CBD,CDE and pending Shader winding. FIFO references are draw-local;
    // old tails are original semantic outputs from their original shader/map.
    uvec2 tail=uvec2(0u,1u);
    uint index=meta[136],written=0u;
    bool ready=meta[137]!=0u,winding=meta[138]!=0u;
    uint vertex_count=0u;
    for(uint ordinal=0u;ordinal<n;++ordinal) {
        uint reference=ordinal+2u;
        if(meta[14]==0u||meta[14]==3u) {
            if(index<2u){tail[index]=reference;written|=1u<<index;++index;}
            else {
                index=0u;
                uvec3 triangle=uvec3(tail,reference);
                if(meta[14]==3u&&winding)triangle=triangle.yxz;
                if(!clip_triangle(triangle,vertex_count)){result[1]=3u;return;}
                if(meta[14]==3u)winding=false;
            }
        } else {
            if(ready&&!clip_triangle(uvec3(tail,reference),vertex_count)){result[1]=3u;return;}
            tail[index]=reference;written|=1u<<index;ready=ready||index==1u;
            index=meta[14]==1u?index^1u:1u;
        }
    }
    for(uint slot=0u;slot<2u;++slot)if((written&(1u<<slot))!=0u)
        for(uint c=0u;c<24u;++c)result[8u+slot*24u+c]=result[semantic_word(tail[slot])+c];
    result[2]=vertex_count;result[3]=n;result[4]=misses;result[5]=hits;
    result[6]=hardware_base();result[7]=0u;
    result[56]=meta[14];result[57]=index;
    result[58]=uint(ready)|(uint(winding)<<1u);result[59]=written;
    result[1]=0u;result[0]=1u;
}
)glsl";
} // namespace

std::string GenerateComputeGuestVertex(const ProgramCode& program, const SwizzleData& swizzles,
                                       u32 entry, bool sanitize_mul) {
    if (entry >= program.size()) return {};
    const auto translated = DecompileProgram(program, swizzles, entry,
        [](u32 reg) { return fmt::format("input_regs[{}]", reg); },
        [](u32 reg) { return fmt::format("output_regs[{}]", reg); }, sanitize_mul, true, true);
    if (translated.empty()) return {};
    auto source = std::string{Prefix} + translated;
    source += "\nvoid reset_compute_temporaries() {\n";
    // CodexAstraLocal: ShaderUnit is zeroed once per draw, not on each FIFO miss.
    for (u32 i = 0; i < 16; ++i) source += fmt::format("reg_tmp{}=vec4(0);\n", i);
    source += "}\n";
    source += fmt::format("const uint COMPUTE_ENTRY={}u;\n", entry);
    source += Body;
    return source;
}
} // namespace Pica::Shader::Generator::GLSL
