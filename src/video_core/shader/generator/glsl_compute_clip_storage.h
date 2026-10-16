// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <string_view>
namespace Pica::Shader::Generator::GLSL {
// CodexAstraLocal: Exact clipping equations use indexed invocation-private lanes.
// Keep aggregate feature-off source as a differential compiler-structure control.
inline constexpr std::string_view FlatComputeClipBody=R"glsl(// CodexAstraLocal: Quaternion sign correction precedes homogeneous clipping,
// exactly where the accelerated 88-byte transport applies it to each triangle.
const uint hardware_source[22]=uint[](0,1,2,3,8,9,10,11,12,13,14,15,22,23,16,4,5,6,7,18,19,20);
bool live_lane(uint lane) {
    return lane<4u || meta[193]==0u || (meta[193]&(1u<<hardware_source[lane]))!=0u;
}
// CodexAstraLocal: Integer slot handles replace aggregate arguments, returns and
// output copies. All24 slots are private to the single ordered invocation.
float clip_lanes[24u*23u];
#define CLIP_LANE(v,c) clip_lanes[(v)*23u+(c)]
void copy_clip(uint destination,uint source) {
    for(uint c=0u;c<23u;++c)CLIP_LANE(destination,c)=CLIP_LANE(source,c);
}
// CodexAstraLocal: One invocation owns assembly and its two bounded polygons.
// Avoid Function-local array/inout copybacks across nested clipping calls; the
// captured FEA status resembled addresses rather than these scalar values.
// This is a compiler-structure correction candidate, not a diagnosed driver bug.
// CodexAstraLocal: Slots0..9 current,10..19 destination,20..23 edge/diagnostic scratch.
uint count,next_count,vertex_count;
#if CALC_DIAGNOSTICS
// CodexAstraLocal TEST-ONLY LOG: Success-only counts, no atomics/new descriptor.
// Remove after the storage/inside/clipping candidates have hardware qualification.
uint inside_triangles=0u,polygon_triangles=0u,clip_intersections=0u;
#endif
uvec2 tail;
uint index,written;
bool ready,winding;
void hardware_vertex(uint reference,uint first,uint value) {
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
    for(uint c=0u;c<22u;++c) {
        uint word=result[semantic_word(reference)+hardware_source[c]];
        if(flip&&c>=15u&&c<19u)word^=0x80000000u;
        CLIP_LANE(value,c)=uintBitsToFloat(word);
    }
    // CodexAstraLocal: Match production SanitizeVertex before homogeneous
    // clipping, without changing the original semantic persistent tail.
    float ndc_z=CLIP_LANE(value,2)/CLIP_LANE(value,3);
    if(ndc_z>0.0&&ndc_z<0.000001)CLIP_LANE(value,2)=0.0;
    if(ndc_z< -1.0&&ndc_z> -1.00001)CLIP_LANE(value,2)=-CLIP_LANE(value,3);
    // CodexAstraLocal: Native exports this vertex clip distance before clipping.
    // Carry it through every later intersection; re-evaluating the dot after a
    // standard-plane snap would change the varying's interpolation semantics.
    CLIP_LANE(value,22u)=0.0;
    if(meta[188]!=0u) {
        vec4 coefficient=uintBitsToFloat(uvec4(meta[189],meta[190],meta[191],meta[192]));
        CLIP_LANE(value,22u)=dot(coefficient,vec4(CLIP_LANE(value,0),CLIP_LANE(value,1),CLIP_LANE(value,2),CLIP_LANE(value,3)));
    }
}
// CodexAstraLocal: Clip against all six standard homogeneous half spaces. The
// same parameter interpolates every original lane before perspective division.
// Only proven zero-coverage original geometry emits nothing; unknown nonfinite
// or nonprojectable generated geometry still refuses the complete batch.
float distance_to_plane(uint v,uint plane) {
    if(plane==0u)return -CLIP_LANE(v,2);
    if(plane==1u)return CLIP_LANE(v,3)+CLIP_LANE(v,2);
    if(plane==2u)return CLIP_LANE(v,3)+CLIP_LANE(v,0);
    if(plane==3u)return CLIP_LANE(v,3)-CLIP_LANE(v,0);
    if(plane==4u)return CLIP_LANE(v,3)+CLIP_LANE(v,1);
    if(plane==5u)return CLIP_LANE(v,3)-CLIP_LANE(v,1);
    return CLIP_LANE(v,22u);
}
bool same_vertex(uint a,uint b) {
    for(uint lane=0u;lane<22u;++lane)
        if(floatBitsToUint(CLIP_LANE(a,lane))!=floatBitsToUint(CLIP_LANE(b,lane)))return false;
    return floatBitsToUint(CLIP_LANE(a,22u))==floatBitsToUint(CLIP_LANE(b,22u));
}
// CodexAstraLocal: Record only the first terminal reason/location in the
// unpublished completion header; success and persistent tail storage are unchanged.
// CodexAstraLocal TEST-ONLY LOG: The raw original/edge operand fields diagnose
// the current hardware first fault without mapping the vertex stream. Remove
// those fields and their host log after the fault has a retained regression;
// keep the permanent reason/location and terminal refusal.
uint clip_ordinal=0u;
uvec3 clip_references;
// CodexAstraLocal: Keep the nested clipping failure edge small. Passing whole
// varying arrays and reconstructing three vertices here allowed diagnostic work
// to expand inside each clipping branch. Avoid that structural compiler burden;
// whether it caused the .45 Adreno rejection remains unproven.
#define CLIP_POSITION(v) vec4(CLIP_LANE(v,0u),CLIP_LANE(v,1u),CLIP_LANE(v,2u),CLIP_LANE(v,3u))
bool fail_clip(uint reason,uint plane,uint count,uint cursor,vec4 a,vec4 b,
               float custom_a,float custom_b,float da,float db,float t,float divisor) {
    if(result[7]!=0u)return false;
    result[7]=reason;result[8]=clip_ordinal;result[9]=plane;
    result[10]=count;result[11]=cursor;result[12]=triangle_capacity()*24u;
    result[13]=meta[14];result[14]=meta[136];result[15]=meta[137]|(meta[138]<<1u);
    for(uint c=0u;c<4u;++c) {
        result[28u+c]=floatBitsToUint(a[c]);
        result[33u+c]=floatBitsToUint(b[c]);
        result[56u+c]=meta[189u+c];
    }
    result[32]=floatBitsToUint(custom_a);result[37]=floatBitsToUint(custom_b);
    result[38]=floatBitsToUint(da);result[39]=floatBitsToUint(db);
    result[40]=floatBitsToUint(t);result[41]=floatBitsToUint(divisor);
    for(uint v=0u;v<3u;++v) {
        result[45u+v]=clip_references[v];
    }
    result[48]=meta[15];result[49]=meta[1];result[50]=meta[188];
    result[60]=0x43465031u;
    return false;
}
// CodexAstraLocal TEST-ONLY LOG: Reconstruct original operands at the single
// terminal exit, after clipping has unwound its polygon arrays. This preserves
// every diagnostic word without cloning that work into each clipping branch.
// Remove this epilogue with the raw record after a retained hardware regression;
// the permanent reason/location and terminal status remain.
void finish_clip_failure() {
    for(uint v=0u;v<3u;++v) {
        uint original=23u;hardware_vertex(clip_references[v],clip_references.x,original);
        for(uint c=0u;c<4u;++c)result[16u+v*4u+c]=floatBitsToUint(CLIP_LANE(original,c));
        result[42u+v]=floatBitsToUint(CLIP_LANE(original,22u));
    }
    result[1]=3u;
}
// CodexAstraLocal: Append directly to invocation-owned destination storage;
// no ten-vertex value/result parameter is copied at every clipping edge.
bool append_clip(uint v) {
    if(next_count>0u&&same_vertex((10u+(next_count-1u)),v))return true;
    if(next_count==10u)return false;
    copy_clip(10u+next_count,v);++next_count;return true;
}
bool intersection(uint a,uint b,uint plane,uint count,uint cursor,uint value) {
#if CALC_DIAGNOSTICS
    ++clip_intersections;
#endif
    // CodexAstraLocal: Adjacent triangles may traverse one edge oppositely.
    // Position-only canonical ordering gives identical crossing arithmetic;
    // exact endpoints preserve all original bits and avoid duplicate clip slots.
    bool swap_endpoints=false;
    for(uint lane=0u;lane<4u;++lane) {
        uint aw=floatBitsToUint(CLIP_LANE(a,lane)),bw=floatBitsToUint(CLIP_LANE(b,lane));
        if(aw!=bw){swap_endpoints=aw>bw;break;}
    }
    if(swap_endpoints){uint old=a;a=b;b=old;}
    precise float da=distance_to_plane(a,plane),db=distance_to_plane(b,plane);
    if(da==0.0){copy_clip(value,a);return true;}
    if(db==0.0){copy_clip(value,b);return true;}
    precise float divisor=da-db;
    precise float t=da/divisor;
    if(!finite_value(divisor)||!finite_value(t)||t<0.0||t>1.0)
        return fail_clip(CLIP_INTERSECTION_PARAMETER,plane,count,cursor,CLIP_POSITION(a),CLIP_POSITION(b),
            CLIP_LANE(a,22u),CLIP_LANE(b,22u),da,db,t,divisor);
    precise float inverse=1.0-t;
    for(uint lane=0u;lane<22u;++lane) {
        precise float left=CLIP_LANE(a,lane)*inverse,right=CLIP_LANE(b,lane)*t;
        precise float sum=left+right;
        if(live_lane(lane)&&!finite_value(sum)) {
            // CodexAstraLocal: A non-position varying can fail too; retain its
            // lane and both original operands instead of only position words.
            result[51]=lane;result[52]=floatBitsToUint(CLIP_LANE(a,lane));
            result[53]=floatBitsToUint(CLIP_LANE(b,lane));result[54]=floatBitsToUint(sum);
            result[55]=floatBitsToUint(inverse);
            return fail_clip(CLIP_INTERPOLATED_FIELD,plane,count,cursor,CLIP_POSITION(a),CLIP_POSITION(b),
                CLIP_LANE(a,22u),CLIP_LANE(b,22u),da,db,t,divisor);
        }
        CLIP_LANE(value,lane)=sum;
    }
    precise float custom_left=CLIP_LANE(a,22u)*inverse,custom_right=CLIP_LANE(b,22u)*t;
    precise float custom_sum=custom_left+custom_right;
    if(!finite_value(custom_sum))
        return fail_clip(CLIP_CUSTOM_DISTANCE,plane,count,cursor,CLIP_POSITION(a),CLIP_POSITION(b),
            CLIP_LANE(a,22u),CLIP_LANE(b,22u),da,db,t,divisor);
    CLIP_LANE(value,22u)=plane==6u?0.0:custom_sum;
    // Exact plane coordinates avoid creating an outside endpoint by rounding;
    // other lanes retain the unmodified homogeneous interpolation result.
    if(plane==0u)CLIP_LANE(value,2)=0.0;
    if(plane==1u)CLIP_LANE(value,2)=-CLIP_LANE(value,3);
    if(plane==2u)CLIP_LANE(value,0)=-CLIP_LANE(value,3);
    if(plane==3u)CLIP_LANE(value,0)=CLIP_LANE(value,3);
    if(plane==4u)CLIP_LANE(value,1)=-CLIP_LANE(value,3);
    if(plane==5u)CLIP_LANE(value,1)=CLIP_LANE(value,3);
    return true;
}
// CodexAstraLocal: The output cursor is invocation-owned, never an inout
// Function pointer. Every triangle retains the same bounded emission order.
bool clip_triangle(uvec3 references) {
    count=3u;
    clip_references=references;
    bool homogeneous_origin=false;
    for(uint i=0u;i<3u;++i) {
        hardware_vertex(references[i],references.x,i);
        vec4 position=CLIP_POSITION((i));
        homogeneous_origin=homogeneous_origin||all(equal(position,vec4(0.0)));
    }
    // CodexAstraLocal: An ORIGINAL exact homogeneous zero plus two position
    // vectors spans at most a projective line, so this triangle has no filled
    // pixels. Preserve its semantic assembly and every other triangle. W==0
    // with nonzero XYZ is not this case: it can clip into visible geometry.
    if(homogeneous_origin){++clip_ordinal;return true;}
    // CodexAstraLocal: Do not replace nonfinite data. A live varying stops the
    // whole batch; dead varyings cannot sample a texture in the paired evaluator.
    for(uint i=0u;i<3u;++i)for(uint lane=0u;lane<22u;++lane) {
        if(live_lane(lane)&&!finite_value(CLIP_LANE((i),lane))) {
            result[51]=lane;result[52]=floatBitsToUint(CLIP_LANE((i),lane));
            result[53]=hardware_source[lane];result[54]=references[i];result[55]=meta[193];
            return fail_clip(CLIP_LIVE_SEMANTIC,7u,count,vertex_count,CLIP_POSITION((i)),CLIP_POSITION((i)),
                CLIP_LANE((i),22u),CLIP_LANE((i),22u),0.0,0.0,0.0,0.0);
        }
    }
    // CodexAstraLocal: Preserve exact original lanes when all vertices already
    // satisfy every clip plane and projectability check. Duplicate vertices
    // retain the old polygon path because its exact deduplication changes count.
    #if CALC_INSIDE_TRIANGLE
    bool inside_all=!same_vertex((0),(1))&&
        !same_vertex((1),(2))&&!same_vertex((2),(0));
    for(uint i=0u;i<3u;++i) {
        float w=CLIP_LANE((i),3);
        inside_all=inside_all&&w>0.0&&finite_value(1.0/w);
        for(uint plane=0u;plane<6u+meta[188];++plane) {
            float distance=distance_to_plane((i),plane);
            inside_all=inside_all&&finite_value(distance)&&distance>=0.0;
        }
    }
    if(inside_all) {
#if CALC_DIAGNOSTICS
        ++inside_triangles;
#endif
        uint capacity=triangle_capacity()*24u;
        if(vertex_count>capacity||3u>capacity-vertex_count)
            return fail_clip(CLIP_OUTPUT_CAPACITY,7u,3u,vertex_count,
                CLIP_POSITION((0)),CLIP_POSITION((0)),
                CLIP_LANE((0),22u),CLIP_LANE((0),22u),0.0,0.0,0.0,0.0);
        uint base=hardware_base();
        for(uint corner=0u;corner<3u;++corner)for(uint lane=0u;lane<22u;++lane)
            result[base+(vertex_count+corner)*22u+lane]=floatBitsToUint(CLIP_LANE((corner),lane));
        vertex_count+=3u;++clip_ordinal;return true;
    }
    #endif
#if CALC_DIAGNOSTICS
    ++polygon_triangles;
#endif
    for(uint plane=0u;plane<6u+meta[188];++plane) {
        if(count==0u){++clip_ordinal;return true;}
        next_count=0u;
        uint previous=20u;copy_clip(previous,count-1u);
        float pd=distance_to_plane(previous,plane);
        if(!finite_value(pd))
            return fail_clip(CLIP_PLANE_DISTANCE,plane,count,vertex_count,CLIP_POSITION(previous),CLIP_POSITION(previous),
                CLIP_LANE(previous,22u),CLIP_LANE(previous,22u),pd,pd,0.0,0.0);
        bool previous_inside=pd>=0.0;
        for(uint i=0u;i<count;++i) {
            uint current=21u;copy_clip(current,i);float cd=distance_to_plane(current,plane);
            if(!finite_value(cd))
                return fail_clip(CLIP_PLANE_DISTANCE,plane,count,vertex_count,CLIP_POSITION(previous),CLIP_POSITION(current),
                    CLIP_LANE(previous,22u),CLIP_LANE(current,22u),pd,cd,0.0,0.0);
            bool inside=cd>=0.0;
            if(inside!=previous_inside) {
                uint crossing=22u;
                if(!intersection(previous,current,plane,count,vertex_count,crossing))return false;
                if(!append_clip(crossing))
                    return fail_clip(CLIP_POLYGON_CAPACITY,plane,next_count,vertex_count,CLIP_POSITION(previous),CLIP_POSITION(current),
                        CLIP_LANE(previous,22u),CLIP_LANE(current,22u),pd,cd,0.0,0.0);
            }
            if(inside&&!append_clip(current))
                return fail_clip(CLIP_POLYGON_CAPACITY,plane,next_count,vertex_count,CLIP_POSITION(previous),CLIP_POSITION(current),
                    CLIP_LANE(previous,22u),CLIP_LANE(current,22u),pd,cd,0.0,0.0);
            copy_clip(previous,current);previous_inside=inside;pd=cd;
        }
        if(next_count>1u&&same_vertex((10u+(0)),(10u+(next_count-1u))))--next_count;
        count=next_count;
        for(uint i=0u;i<count;++i)copy_clip(i,10u+i);
    }
    if(count<3u){++clip_ordinal;return true;}
    for(uint i=0u;i<count;++i) {
        float w=CLIP_LANE((i),3);
        if(!(w>0.0))
            return fail_clip(CLIP_NONPOSITIVE_W,7u,count,vertex_count,CLIP_POSITION((i)),CLIP_POSITION((i)),
                CLIP_LANE((i),22u),CLIP_LANE((i),22u),w,w,0.0,0.0);
        if(!finite_value(1.0/w))
            return fail_clip(CLIP_RECIPROCAL,7u,count,vertex_count,CLIP_POSITION((i)),CLIP_POSITION((i)),
                CLIP_LANE((i),22u),CLIP_LANE((i),22u),w,w,0.0,0.0);
        for(uint plane=0u;plane<6u+meta[188];++plane)
            if(distance_to_plane((i),plane)<0.0)
                return fail_clip(CLIP_OUTSIDE_PLANE,plane,count,vertex_count,CLIP_POSITION((i)),CLIP_POSITION((i)),
                    CLIP_LANE((i),22u),CLIP_LANE((i),22u),
                    distance_to_plane((i),plane),0.0,0.0,0.0);
    }
    uint emitted=3u*(count-2u),capacity=triangle_capacity()*24u;
    if(vertex_count>capacity||emitted>capacity-vertex_count)
        return fail_clip(CLIP_OUTPUT_CAPACITY,7u,count,vertex_count,CLIP_POSITION((0)),CLIP_POSITION((0)),
            CLIP_LANE((0),22u),CLIP_LANE((0),22u),0.0,0.0,0.0,0.0);
    uint base=hardware_base();
    for(uint triangle=1u;triangle+1u<count;++triangle) {
        uvec3 selected=uvec3(0u,triangle,triangle+1u);
        for(uint corner=0u;corner<3u;++corner)for(uint lane=0u;lane<22u;++lane)
            result[base+(vertex_count+corner)*22u+lane]=floatBitsToUint(CLIP_LANE((selected[corner]),lane));
        vertex_count+=3u;
    }
    ++clip_ordinal;return true;
}

// CodexAstraLocal: Flat slots require invocation-owned assembly storage.
#define CLIP_TRIANGLE(v) clip_triangle(v)
)glsl";
} // namespace Pica::Shader::Generator::GLSL
