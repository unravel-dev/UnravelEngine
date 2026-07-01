$output v_color

#include "common.sh"
#include <bgfx_compute.sh>

uniform vec4 u_wf_params[3];
#define u_wfColor         u_wf_params[0].xyz
#define u_wfOpacity       u_wf_params[0].w
#define u_wfThickness     u_wf_params[1].x
#define u_wfStride        u_wf_params[1].y
#define u_wfPosOffset     u_wf_params[1].z
#define u_wfIndexOffset   u_wf_params[1].w
#define u_wfWeightOffset  u_wf_params[2].x
#define u_wfIndicesOffset u_wf_params[2].y

// Raw read-only views of the mesh vertex/index buffers, exposed identically
// to the non-skinned variant. u_wfIndicesOffset / u_wfWeightOffset locate the
// bone index/weight attributes within each vertex (both stored as Float4).
BUFFER_RO(u_positions, float, 0);
BUFFER_RO(u_indices,   uint,  1);

#define NEAR_EPSILON   0.001
#define LENGTH_EPSILON 0.0001

vec3 get_position(uint index)
{
    uint stride = uint(u_wfStride);
    uint offset = uint(u_wfPosOffset);
    uint base   = index * stride + offset;
    return vec3(u_positions[base + 0u], u_positions[base + 1u], u_positions[base + 2u]);
}

vec4 get_weight(uint index)
{
    uint stride = uint(u_wfStride);
    uint offset = uint(u_wfWeightOffset);
    uint base   = index * stride + offset;
    return vec4(u_positions[base + 0u],
                u_positions[base + 1u],
                u_positions[base + 2u],
                u_positions[base + 3u]);
}

vec4 get_bone_indices(uint index)
{
    uint stride = uint(u_wfStride);
    uint offset = uint(u_wfIndicesOffset);
    uint base   = index * stride + offset;
    return vec4(u_positions[base + 0u],
                u_positions[base + 1u],
                u_positions[base + 2u],
                u_positions[base + 3u]);
}

// Skinned world transform. u_world[i] is filled by gfx::set_world_transform(bones)
// with per-bone skinning matrices before submitting each submesh.
mat4 get_world_matrix(uint index)
{
    vec4 weights = get_weight(index);
    vec4 bones   = get_bone_indices(index);
    return weights.x * u_world[int(bones.x)]
         + weights.y * u_world[int(bones.y)]
         + weights.z * u_world[int(bones.z)]
         + weights.w * u_world[int(bones.w)];
}

void main()
{
    uint vertex_id   = uint(gl_VertexID);
    uint line_index  = vertex_id / 6u;
    uint local_index = vertex_id % 6u;

    uint edge_index      = line_index % 3u;
    uint tri_first_index = line_index - edge_index;

    uint ib_base = uint(u_wfIndexOffset) + tri_first_index;

    uint i0 = u_indices[ib_base + edge_index];
    uint i1 = u_indices[ib_base + ((edge_index + 1u) % 3u)];

    vec3 p0 = get_position(i0);
    vec3 p1 = get_position(i1);

    vec4 p0_world = mul(get_world_matrix(i0), vec4(p0, 1.0));
    vec4 p1_world = mul(get_world_matrix(i1), vec4(p1, 1.0));

    vec4 p0_clip = mul(u_viewProj, p0_world);
    vec4 p1_clip = mul(u_viewProj, p1_world);

    vec4 base_color = vec4(u_wfColor, u_wfOpacity);
    vec4 c0 = base_color;
    vec4 c1 = base_color;

    vec4 clipped_p0     = p0_clip;
    vec4 clipped_p1     = p1_clip;
    vec4 clipped_color0 = c0;
    vec4 clipped_color1 = c1;

    if (p0_clip.w < NEAR_EPSILON)
    {
        float t = (NEAR_EPSILON - p0_clip.w) / (p1_clip.w - p0_clip.w);
        clipped_p0     = mix(p0_clip, p1_clip, t);
        clipped_color0 = mix(c0, c1, t);
    }
    if (p1_clip.w < NEAR_EPSILON)
    {
        float t = (NEAR_EPSILON - p1_clip.w) / (p0_clip.w - p1_clip.w);
        clipped_p1     = mix(p1_clip, p0_clip, t);
        clipped_color1 = mix(c1, c0, t);
    }

    vec2 offsets[6];
    offsets[0] = vec2(-1.0, -1.0);
    offsets[1] = vec2(-1.0,  1.0);
    offsets[2] = vec2( 1.0, -1.0);
    offsets[3] = vec2( 1.0, -1.0);
    offsets[4] = vec2(-1.0,  1.0);
    offsets[5] = vec2( 1.0,  1.0);

    vec2 offset = offsets[local_index];
    vec2 uv     = offset * 0.5 + 0.5;

    vec4 p     = mix(clipped_p0, clipped_p1, uv.x);
    vec4 color = mix(clipped_color0, clipped_color1, uv.x);

    vec2 ndp0      = clipped_p0.xy / clipped_p0.w;
    vec2 ndp1      = clipped_p1.xy / clipped_p1.w;
    vec2 dir       = ndp1 - ndp0;
    float dir_len  = length(dir);

    if (dir_len > LENGTH_EPSILON)
    {
        vec2 tangent = dir / dir_len;
        vec2 normal  = vec2(-tangent.y, tangent.x);

        float px_to_ndc_x = 2.0 / u_viewRect.z;
        float px_to_ndc_y = 2.0 / u_viewRect.w;

        float side = 2.0 * uv.y - 1.0;
        vec2 offset_ndc = normal * (0.5 * u_wfThickness) * side * vec2(px_to_ndc_x, px_to_ndc_y);

        p.xy = p.xy + (offset_ndc * p.w);
    }

    v_color     = color;
    gl_Position = p;
}
