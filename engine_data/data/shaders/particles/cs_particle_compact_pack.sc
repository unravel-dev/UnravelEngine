/*
 * Advance life, atomically compact alive particles into dense instance rows.
 * Sim buffer stays sparse (CPU freelist / spawn slots); only instances are dense.
 */

#include <bgfx_compute.sh>

BUFFER_RW(s_sim, vec4, 0);
BUFFER_WO(s_instances, vec4, 1);
BUFFER_RW(s_counter, uint, 2);
BUFFER_RO(s_color_lut, vec4, 3);
BUFFER_RO(s_color_speed_lut, vec4, 4);
BUFFER_RO(s_ease_lut, vec4, 5);

uniform vec4 u_pack0;
uniform vec4 u_pack1;
uniform vec4 u_pack2;
uniform vec4 u_pack3;
uniform vec4 u_pack4;
uniform vec4 u_pack5;
uniform mat4 u_localToWorld;

#define u_opacity           u_pack0.x
#define u_color_intensity   u_pack0.y
#define u_avg_scale         u_pack0.z
#define u_render_mode       u_pack0.w
#define u_pivot_x           u_pack1.x
#define u_pivot_y           u_pack1.y
#define u_capacity          uint(u_pack1.z)
#define u_features          uint(u_pack1.w)
#define u_scale3d           u_pack2.xyz
#define u_tex_cycles        u_pack2.w
#define u_tex_tiles         u_pack3.xy
#define u_tex_randomize     u_pack3.z
#define u_size_speed_range  u_pack4.xy
#define u_inv_size_span     u_pack4.z
#define u_size_speed_min    u_pack4.w
#define u_inv_color_span    u_pack5.x
#define u_color_speed_min   u_pack5.y
#define u_dt                u_pack5.z

#define FEAT_ALIGN      1u
#define FEAT_TEXSHEET   2u
#define FEAT_COLOR_SPD  4u
#define FEAT_SIZE_SPD   8u
#define FEAT_LOCAL      32u
#define FEAT_EASE       64u

vec4 sample_color_lut(float t)
{
    float u = clamp(t, 0.0, 1.0);
    float idx_f = u * 255.0;
    uint i0 = uint(idx_f);
    uint i1 = min(i0 + 1u, 255u);
    float f = fract(idx_f);
    return mix(s_color_lut[i0], s_color_lut[i1], f);
}

vec4 sample_color_speed_lut(float t)
{
    float u = clamp(t, 0.0, 1.0);
    float idx_f = u * 255.0;
    uint i0 = uint(idx_f);
    uint i1 = min(i0 + 1u, 255u);
    float f = fract(idx_f);
    return mix(s_color_speed_lut[i0], s_color_speed_lut[i1], f);
}

float sample_ease(float t)
{
    float u = clamp(t, 0.0, 1.0);
    float idx_f = u * 255.0;
    uint i0 = uint(idx_f);
    uint i1 = min(i0 + 1u, 255u);
    float f = fract(idx_f);
    return mix(s_ease_lut[i0].x, s_ease_lut[i1].x, f);
}

vec4 quat_look_rotation(vec3 direction)
{
    vec3 fwd = normalize(direction);
    vec3 up_ref = vec3(0.0, 1.0, 0.0);
    if(abs(dot(fwd, up_ref)) > 0.99)
    {
        up_ref = vec3(1.0, 0.0, 0.0);
    }
    vec3 right = normalize(cross(up_ref, fwd));
    vec3 up = cross(fwd, right);
    // Rotation matrix -> quaternion (xyzw).
    float tr = right.x + up.y + fwd.z;
    vec4 q;
    if(tr > 0.0)
    {
        float s = sqrt(tr + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (up.z - fwd.y) / s;
        q.y = (fwd.x - right.z) / s;
        q.z = (right.y - up.x) / s;
    }
    else if(right.x > up.y && right.x > fwd.z)
    {
        float s = sqrt(1.0 + right.x - up.y - fwd.z) * 2.0;
        q.w = (up.z - fwd.y) / s;
        q.x = 0.25 * s;
        q.y = (up.x + right.y) / s;
        q.z = (fwd.x + right.z) / s;
    }
    else if(up.y > fwd.z)
    {
        float s = sqrt(1.0 + up.y - right.x - fwd.z) * 2.0;
        q.w = (fwd.x - right.z) / s;
        q.x = (up.x + right.y) / s;
        q.y = 0.25 * s;
        q.z = (fwd.y + up.z) / s;
    }
    else
    {
        float s = sqrt(1.0 + fwd.z - right.x - up.y) * 2.0;
        q.w = (right.y - up.x) / s;
        q.x = (fwd.x + right.z) / s;
        q.y = (fwd.y + up.z) / s;
        q.z = 0.25 * s;
    }
    return q;
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if(i >= u_capacity)
    {
        return;
    }

    uint base = i * 5u;
    vec4 s0 = s_sim[base + 0u];
    vec4 s1 = s_sim[base + 1u];
    vec4 s2 = s_sim[base + 2u];
    vec4 s3 = s_sim[base + 3u];
    vec4 s4 = s_sim[base + 4u];

    float life = s0.w;
    float lifespan = s1.w;
    if(lifespan <= 0.0)
    {
        return;
    }
    life += u_dt / max(lifespan, 1e-4);
    if(life > 1.0)
    {
        s0.w = 0.0;
        s1.w = 0.0;
        s_sim[base + 0u] = s0;
        s_sim[base + 1u] = s1;
        return;
    }
    s0.w = life;
    s_sim[base + 0u] = s0;

    uint dst;
    atomicFetchAndAdd(s_counter[0], 1u, dst);

    vec3 start = s0.xyz;
    vec3 end0 = s1.xyz;
    vec3 end1 = s2.xyz;
    float scale_start = s2.w;
    float scale_end = s3.x;
    float texsheet_seed = s3.y;
    vec4 rotation = s4;

    float tt = life;
    if((u_features & FEAT_EASE) != 0u)
    {
        tt = sample_ease(life);
    }

    bool need_speed = ((u_features & (FEAT_COLOR_SPD | FEAT_SIZE_SPD | FEAT_ALIGN)) != 0u);
    float particle_speed = 0.0;
    vec3 current_velocity = vec3_splat(0.0);
    if(need_speed)
    {
        vec3 initial_velocity = end0 - start;
        vec3 final_velocity = end1 - end0;
        current_velocity = mix(initial_velocity, final_velocity, tt);
        particle_speed = length(current_velocity / max(lifespan, 1e-4));
    }

    if((u_features & FEAT_ALIGN) != 0u)
    {
        float velocity_len_sq = dot(current_velocity, current_velocity);
        if(velocity_len_sq > 0.0001)
        {
            rotation = quat_look_rotation(current_velocity);
        }
    }

    vec4 color = sample_color_lut(life);
    if((u_features & FEAT_COLOR_SPD) != 0u)
    {
        float speed_factor = clamp((particle_speed - u_color_speed_min) * u_inv_color_span, 0.0, 1.0);
        color *= sample_color_speed_lut(speed_factor);
    }
    color.a *= u_opacity;
    color.rgb *= u_color_intensity;

    float scale = mix(scale_start, scale_end, life) * u_avg_scale;
    if((u_features & FEAT_SIZE_SPD) != 0u)
    {
        float speed_factor = clamp((particle_speed - u_size_speed_min) * u_inv_size_span, 0.0, 1.0);
        scale *= mix(u_size_speed_range.x, u_size_speed_range.y, speed_factor);
    }

    vec3 p0 = mix(start, end0, tt);
    vec3 p1 = mix(end0, end1, tt);
    vec3 local_pos = mix(p0, p1, tt);
    vec3 world_pos = local_pos;
    if((u_features & FEAT_LOCAL) != 0u)
    {
        vec4 wp = mul(u_localToWorld, vec4(local_pos, 1.0));
        world_pos = wp.xyz;
    }

    vec2 uv_offset = vec2_splat(0.0);
    vec2 uv_scale = vec2_splat(1.0);
    if((u_features & FEAT_TEXSHEET) != 0u)
    {
        float uv_scale_x = 1.0 / max(u_tex_tiles.x, 1.0);
        float uv_scale_y = 1.0 / max(u_tex_tiles.y, 1.0);
        uint total_frames = max(uint(u_tex_tiles.x) * uint(u_tex_tiles.y), 1u);
        float anim_progress = life * u_tex_cycles;
        if(u_tex_randomize > 0.5)
        {
            anim_progress += texsheet_seed;
        }
        anim_progress = fract(anim_progress);
        uint current_frame = uint(anim_progress * float(total_frames)) % total_frames;
        uint tile_x = current_frame % uint(u_tex_tiles.x);
        uint tile_y = current_frame / uint(u_tex_tiles.x);
        uv_offset = vec2(float(tile_x) * uv_scale_x, float(tile_y) * uv_scale_y);
        uv_scale = vec2(uv_scale_x, uv_scale_y);
    }

    uint inst = dst * 6u;
    s_instances[inst + 0u] = vec4(world_pos, u_pivot_x);
    s_instances[inst + 1u] = rotation;
    s_instances[inst + 2u] = vec4(u_scale3d * scale, u_pivot_y);
    s_instances[inst + 3u] = vec4(uv_offset, uv_scale);
    s_instances[inst + 4u] = color;
    s_instances[inst + 5u] = vec4(u_render_mode, 0.0, 0.0, 0.0);
}
