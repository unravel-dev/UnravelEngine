#include "particle_system_soa.h"

#include <bgfx/bgfx.h>
#include <bx/allocator.h>
#include <bx/bx.h>
#include <bx/handlealloc.h>
#include <bx/rng.h>
#include <core/logging/logging.h>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/gpu_program.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>

#define POOLSTL_STD_SUPPLEMENT 1
#include <poolstl/poolstl.hpp>

namespace unravel
{
namespace ps_soa
{
namespace
{

constexpr float k_min_particle_lifespan = 1.0e-4f;
constexpr float k_emit_dir_zero_len_sq = 1.0e-12f;
// pos+pivotX | rotation | scale3d+pivotY | uv | color | renderMode
constexpr uint16_t k_instance_stride = 96;
// Emitter updates already run under poolstl::par in particle_system.
// Do not nest poolstl::par inside update — it oversubscribes the pool and
// inflates both wall time and summed "Update Emitter" thread time.
constexpr uint32_t k_parallel_particle_threshold = 512;
constexpr uint32_t k_min_rows_per_job = 256;

particle_sim_backend g_default_sim_backend = particle_sim_backend::cpu;
bool g_gpu_sim_available = false;
std::shared_ptr<gpu_program> g_pack_program;
bgfx::UniformHandle g_u_pack0 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_pack1 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_pack2 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_pack3 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_pack4 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_pack5 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_u_local_to_world = BGFX_INVALID_HANDLE;
bgfx::VertexLayout g_gpu_vec4_layout;
bgfx::VertexLayout g_gpu_instance_layout;
bool g_gpu_layouts_ready = false;

void ensure_gpu_layouts()
{
    if(g_gpu_layouts_ready)
    {
        return;
    }
    g_gpu_vec4_layout.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
    g_gpu_instance_layout.begin()
        .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord3, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
        .end();
    g_gpu_layouts_ready = true;
}

struct emitter_gpu_resources
{
    bool pending_pack = false;
    particle_sim_backend backend_override = particle_sim_backend::gpu;
    bool has_backend_override = false;
    bool luts_valid = false;
    bool luts_gpu_dirty = true;
    bool order_identity_uploaded = false;
    bool cached_need_color_speed = false;
    bool cached_need_ease = false;
    emitter_feature cached_features = emitter_feature::none;
    bx::EaseFn cached_ease_pos = nullptr;
    uint32_t gpu_capacity = 0;
    bgfx::DynamicVertexBufferHandle sim_vb = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle instance_vb = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle order_ib = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle color_lut_vb = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle color_speed_lut_vb = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle ease_lut_vb = BGFX_INVALID_HANDLE;
    std::vector<gpu_sim_particle> sim_staging;
    std::vector<math::vec4> color_lut;
    std::vector<math::vec4> color_speed_lut;
    std::vector<math::vec4> ease_lut;
    std::vector<uint32_t> order_staging;
    emitter_sim_constants constants{};

    void destroy_buffers()
    {
        if(bgfx::isValid(sim_vb))
        {
            bgfx::destroy(sim_vb);
            sim_vb = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(instance_vb))
        {
            bgfx::destroy(instance_vb);
            instance_vb = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(order_ib))
        {
            bgfx::destroy(order_ib);
            order_ib = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(color_lut_vb))
        {
            bgfx::destroy(color_lut_vb);
            color_lut_vb = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(color_speed_lut_vb))
        {
            bgfx::destroy(color_speed_lut_vb);
            color_speed_lut_vb = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(ease_lut_vb))
        {
            bgfx::destroy(ease_lut_vb);
            ease_lut_vb = BGFX_INVALID_HANDLE;
        }
        gpu_capacity = 0;
        pending_pack = false;
        luts_gpu_dirty = true;
        order_identity_uploaded = false;
    }

    void ensure_capacity(uint32_t max_particles)
    {
        ensure_gpu_layouts();
        if(gpu_capacity >= max_particles && bgfx::isValid(sim_vb))
        {
            return;
        }
        const bool keep_pending = pending_pack;
        destroy_buffers();
        pending_pack = keep_pending;
        luts_gpu_dirty = true;
        gpu_capacity = max_particles;
        const uint16_t sim_flags = BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE |
                                   BGFX_BUFFER_COMPUTE_FORMAT_32X4 | BGFX_BUFFER_COMPUTE_TYPE_FLOAT;
        const uint16_t instance_flags = BGFX_BUFFER_COMPUTE_WRITE | BGFX_BUFFER_ALLOW_RESIZE |
                                        BGFX_BUFFER_COMPUTE_FORMAT_32X4 | BGFX_BUFFER_COMPUTE_TYPE_FLOAT;
        const uint16_t order_flags = BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE | BGFX_BUFFER_INDEX32 |
                                    BGFX_BUFFER_COMPUTE_FORMAT_32X1 | BGFX_BUFFER_COMPUTE_TYPE_UINT;
        sim_vb = bgfx::createDynamicVertexBuffer(max_particles * k_gpu_sim_vec4s_per_particle,
                                                 g_gpu_vec4_layout,
                                                 sim_flags);
        instance_vb = bgfx::createDynamicVertexBuffer(max_particles, g_gpu_instance_layout, instance_flags);
        order_ib = bgfx::createDynamicIndexBuffer(max_particles, order_flags);
        color_lut_vb = bgfx::createDynamicVertexBuffer(k_gpu_lut_size, g_gpu_vec4_layout, sim_flags);
        color_speed_lut_vb = bgfx::createDynamicVertexBuffer(k_gpu_lut_size, g_gpu_vec4_layout, sim_flags);
        ease_lut_vb = bgfx::createDynamicVertexBuffer(k_gpu_lut_size, g_gpu_vec4_layout, sim_flags);
        order_staging.resize(max_particles);
        for(uint32_t i = 0; i < max_particles; ++i)
        {
            order_staging[i] = i;
        }
        bgfx::update(order_ib, 0, bgfx::copy(order_staging.data(), uint32_t(sizeof(uint32_t) * max_particles)));
        order_identity_uploaded = true;
    }
};

auto thread_local_rng() -> bx::RngMwc&
{
    thread_local bx::RngMwc rng;
    return rng;
}

auto frand01(bx::RngMwc& rng) -> float
{
    return bx::frnd(&rng);
}

auto frand_range(bx::RngMwc& rng, float lo, float hi) -> float
{
    return math::mix(lo, hi, frand01(rng));
}

auto random_unit_vector(bx::RngMwc& rng) -> math::vec3
{
    // Marsaglia method for uniform direction on sphere surface.
    float x = 0.0f;
    float y = 0.0f;
    float s = 0.0f;
    do
    {
        x = frand_range(rng, -1.0f, 1.0f);
        y = frand_range(rng, -1.0f, 1.0f);
        s = x * x + y * y;
    } while(s >= 1.0f || s <= 0.0f);
    const float f = 2.0f * std::sqrt(1.0f - s);
    return math::vec3(x * f, y * f, 1.0f - 2.0f * s);
}

auto random_in_unit_ball(bx::RngMwc& rng) -> math::vec3
{
    math::vec3 p;
    do
    {
        p = math::vec3(frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f));
    } while(math::dot(p, p) > 1.0f);
    return p;
}

auto random_in_unit_disk(bx::RngMwc& rng) -> math::vec2
{
    math::vec2 p;
    do
    {
        p = math::vec2(frand_range(rng, -1.0f, 1.0f), frand_range(rng, -1.0f, 1.0f));
    } while(math::dot(p, p) > 1.0f);
    return p;
}

auto random_on_unit_circle(bx::RngMwc& rng) -> math::vec2
{
    const float a = frand_range(rng, 0.0f, 6.28318530718f);
    return math::vec2(std::cos(a), std::sin(a));
}

struct particle_vertex
{
    float x, y, z, u, v;

    static void init()
    {
        ms_layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    }

    static bgfx::VertexLayout ms_layout;
};

bgfx::VertexLayout particle_vertex::ms_layout;

static particle_vertex s_quad_vertices[4] = {
    {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
    {0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
    {0.5f, 0.5f, 0.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, 0.0f, 0.0f, 0.0f},
};

static const uint16_t s_quad_indices[6] = {0, 1, 2, 2, 3, 0};

struct particle_soa
{
    std::vector<math::vec3> start;
    std::vector<math::vec3> end0;
    std::vector<math::vec3> end1;
    std::vector<float> scale_start;
    std::vector<float> scale_end;
    std::vector<float> life;
    std::vector<float> lifespan;
    std::vector<float> texsheet_seed;
    std::vector<math::vec3> position;
    std::vector<math::color> color;
    std::vector<float> scale;
    std::vector<float> cached_speed;
    std::vector<math::vec2> uv_offset;
    std::vector<math::vec2> uv_scale;
    std::vector<math::quat> rotation;
    uint32_t count = 0;
    uint32_t capacity = 0;

    void resize(uint32_t max_particles)
    {
        capacity = max_particles;
        count = 0;
        start.resize(max_particles);
        end0.resize(max_particles);
        end1.resize(max_particles);
        scale_start.resize(max_particles);
        scale_end.resize(max_particles);
        life.resize(max_particles);
        lifespan.resize(max_particles);
        texsheet_seed.resize(max_particles);
        position.resize(max_particles);
        color.resize(max_particles);
        scale.resize(max_particles);
        cached_speed.resize(max_particles);
        uv_offset.resize(max_particles);
        uv_scale.resize(max_particles);
        rotation.resize(max_particles);
    }

    void clear_live()
    {
        count = 0;
    }

    // Compact only sim streams; render caches are rebuilt immediately after.
    void move_sim_particle(uint32_t dst, uint32_t src)
    {
        start[dst] = start[src];
        end0[dst] = end0[src];
        end1[dst] = end1[src];
        scale_start[dst] = scale_start[src];
        scale_end[dst] = scale_end[src];
        life[dst] = life[src];
        lifespan[dst] = lifespan[src];
        texsheet_seed[dst] = texsheet_seed[src];
    }
};

void expand_aabb_sphere(math::bbox& aabb, const math::vec3& pos, float radius)
{
    const math::vec3 pad(radius);
    aabb.add_point(pos - pad);
    aabb.add_point(pos + pad);
}

void bake_constants(const emitter_desc& desc,
                    const emitter_transform_state& transform,
                    emitter_sim_constants& out_constants)
{
    const math::vec3 scale = transform.current.get_scale();
    out_constants.features = desc.bake_features();
    out_constants.space = desc.motion.space;
    out_constants.opacity = desc.appearance.opacity;
    out_constants.color_intensity = desc.appearance.color_intensity;
    out_constants.avg_system_scale = (scale.x + scale.y + scale.z) / 3.0f;
    out_constants.particle_scale_3d = desc.appearance.initial_scale_3d;
    out_constants.pivot = desc.render.pivot;
    out_constants.render_mode = desc.render.render_mode;
    out_constants.blend_mode = desc.render.blend_mode;
    out_constants.texture_mode = desc.render.texture_mode;
    out_constants.tex_sheet_tiles = desc.render.tex_sheet_tiles;
    out_constants.tex_sheet_cycles = desc.render.tex_sheet_cycles;
    out_constants.tex_sheet_randomize = desc.render.tex_sheet_randomize;
    out_constants.size_by_speed_range = desc.appearance.size_by_speed_range;
    out_constants.size_by_speed_velocity_range = desc.appearance.size_by_speed_velocity_range;
    out_constants.color_by_speed_velocity_range = desc.appearance.color_by_speed_velocity_range;
    const float size_span = desc.appearance.size_by_speed_velocity_range.max - desc.appearance.size_by_speed_velocity_range.min;
    const float color_span =
        desc.appearance.color_by_speed_velocity_range.max - desc.appearance.color_by_speed_velocity_range.min;
    out_constants.inv_size_by_speed_velocity_span = (size_span > 0.0f) ? (1.0f / size_span) : 0.0f;
    out_constants.inv_color_by_speed_velocity_span = (color_span > 0.0f) ? (1.0f / color_span) : 0.0f;
    out_constants.ease_pos = bx::getEaseFunc(desc.motion.position_easing);
    out_constants.local_to_world = transform.current;
}

float calculate_particle_speed(const math::vec3& start,
                               const math::vec3& end0,
                               const math::vec3& end1,
                               float lifespan,
                               float tt_pos)
{
    const math::vec3 initial_velocity = end0 - start;
    const math::vec3 final_velocity = end1 - end0;
    const math::vec3 current_velocity = math::mix(initial_velocity, final_velocity, tt_pos);
    const math::vec3 velocity_per_second = current_velocity * (1.0f / lifespan);
    return math::length(velocity_per_second);
}

// Fast path: world space, linear ease, no align/texsheet/speed effects.
void update_particle_basic(particle_soa& particles,
                           uint32_t index,
                           const emitter_desc& desc,
                           const emitter_sim_constants& constants)
{
    const float life = particles.life[index];
    math::color sampled_color = desc.appearance.color_gradient.sample(life);
    sampled_color.value.a *= constants.opacity;
    sampled_color.value.r *= constants.color_intensity;
    sampled_color.value.g *= constants.color_intensity;
    sampled_color.value.b *= constants.color_intensity;
    particles.color[index] = sampled_color;
    particles.scale[index] =
        math::mix(particles.scale_start[index], particles.scale_end[index], life) * constants.avg_system_scale;
    const math::vec3 p0 = math::mix(particles.start[index], particles.end0[index], life);
    const math::vec3 p1 = math::mix(particles.end0[index], particles.end1[index], life);
    particles.position[index] = math::mix(p0, p1, life);
    // Shader treats xyz~0 as "no rotation" (glm quat is w,x,y,z).
    particles.rotation[index] = math::identity<math::quat>();
    particles.uv_offset[index] = math::vec2(0.0f, 0.0f);
    particles.uv_scale[index] = math::vec2(1.0f, 1.0f);
}

void update_particle_full(particle_soa& particles,
                          uint32_t index,
                          const emitter_desc& desc,
                          const emitter_sim_constants& constants)
{
    const float life = particles.life[index];
    const float tt_pos = constants.ease_pos ? constants.ease_pos(life) : life;
    const bool need_speed = has_feature(constants.features, emitter_feature::color_by_speed) ||
                            has_feature(constants.features, emitter_feature::size_by_speed) ||
                            has_feature(constants.features, emitter_feature::align_to_direction);
    float particle_speed = 0.0f;
    if(need_speed)
    {
        particle_speed = calculate_particle_speed(particles.start[index],
                                                  particles.end0[index],
                                                  particles.end1[index],
                                                  particles.lifespan[index],
                                                  tt_pos);
        particles.cached_speed[index] = particle_speed;
    }
    math::color sampled_color = desc.appearance.color_gradient.sample(life);
    if(has_feature(constants.features, emitter_feature::color_by_speed))
    {
        const float speed_factor = math::clamp(
            (particle_speed - constants.color_by_speed_velocity_range.min) * constants.inv_color_by_speed_velocity_span,
            0.0f,
            1.0f);
        const math::color speed_color = desc.appearance.color_by_speed_gradient.sample(speed_factor);
        sampled_color.value *= speed_color.value;
    }
    sampled_color.value.a *= constants.opacity;
    sampled_color.value.r *= constants.color_intensity;
    sampled_color.value.g *= constants.color_intensity;
    sampled_color.value.b *= constants.color_intensity;
    particles.color[index] = sampled_color;
    float scale = math::mix(particles.scale_start[index], particles.scale_end[index], life) * constants.avg_system_scale;
    if(has_feature(constants.features, emitter_feature::size_by_speed))
    {
        const float speed_factor = math::clamp(
            (particle_speed - constants.size_by_speed_velocity_range.min) * constants.inv_size_by_speed_velocity_span,
            0.0f,
            1.0f);
        scale *= math::mix(constants.size_by_speed_range.min, constants.size_by_speed_range.max, speed_factor);
    }
    particles.scale[index] = scale;
    const math::vec3 p0 = math::mix(particles.start[index], particles.end0[index], tt_pos);
    const math::vec3 p1 = math::mix(particles.end0[index], particles.end1[index], tt_pos);
    const math::vec3 local_pos = math::mix(p0, p1, tt_pos);
    if(has_feature(constants.features, emitter_feature::local_space))
    {
        const math::vec4 world_pos4 = constants.local_to_world * math::vec4(local_pos, 1.0f);
        particles.position[index] = math::vec3(world_pos4.x, world_pos4.y, world_pos4.z);
    }
    else
    {
        particles.position[index] = local_pos;
    }
    if(has_feature(constants.features, emitter_feature::align_to_direction))
    {
        const math::vec3 velocity0 = particles.end0[index] - particles.start[index];
        const math::vec3 velocity1 = particles.end1[index] - particles.end0[index];
        const math::vec3 current_velocity = math::mix(velocity0, velocity1, tt_pos);
        const float velocity_len_sq = math::dot(current_velocity, current_velocity);
        if(velocity_len_sq > 0.0001f)
        {
            const math::vec3 direction = math::normalize(current_velocity);
            math::vec3 up_ref(0.0f, 1.0f, 0.0f);
            if(math::abs(math::dot(direction, up_ref)) > 0.99f)
            {
                up_ref = math::vec3(1.0f, 0.0f, 0.0f);
            }
            particles.rotation[index] = math::look_rotation(direction, up_ref);
        }
        else
        {
            particles.rotation[index] = math::identity<math::quat>();
        }
    }
    if(has_feature(constants.features, emitter_feature::texsheet))
    {
        const float uv_scale_x = 1.0f / constants.tex_sheet_tiles.x;
        const float uv_scale_y = 1.0f / constants.tex_sheet_tiles.y;
        const uint32_t total_frames =
            uint32_t(constants.tex_sheet_tiles.x) * uint32_t(constants.tex_sheet_tiles.y);
        float anim_progress = life * constants.tex_sheet_cycles;
        if(constants.tex_sheet_randomize)
        {
            anim_progress += particles.texsheet_seed[index];
        }
        anim_progress = math::fmod(anim_progress, 1.0f);
        const uint32_t current_frame = uint32_t(anim_progress * float(total_frames)) % total_frames;
        const uint32_t tile_x = current_frame % uint32_t(constants.tex_sheet_tiles.x);
        const uint32_t tile_y = current_frame / uint32_t(constants.tex_sheet_tiles.x);
        particles.uv_offset[index] = math::vec2(float(tile_x) * uv_scale_x, float(tile_y) * uv_scale_y);
        particles.uv_scale[index] = math::vec2(uv_scale_x, uv_scale_y);
    }
    else
    {
        particles.uv_offset[index] = math::vec2(0.0f, 0.0f);
        particles.uv_scale[index] = math::vec2(1.0f, 1.0f);
    }
}

auto has_heavy_features(emitter_feature features) -> bool
{
    const emitter_feature heavy = features & (emitter_feature::align_to_direction | emitter_feature::texsheet |
                                              emitter_feature::color_by_speed | emitter_feature::size_by_speed |
                                              emitter_feature::local_space | emitter_feature::non_linear_ease);
    return static_cast<uint32_t>(heavy) != 0u;
}

void update_particle_properties(particle_soa& particles,
                                uint32_t index,
                                const emitter_desc& desc,
                                const emitter_sim_constants& constants)
{
    if(!has_heavy_features(constants.features))
    {
        update_particle_basic(particles, index, desc, constants);
        return;
    }
    update_particle_full(particles, index, desc, constants);
}

void update_particles_range(particle_soa& particles,
                            uint32_t begin,
                            uint32_t end,
                            const emitter_desc& desc,
                            const emitter_sim_constants& constants)
{
    if(!has_heavy_features(constants.features))
    {
        for(uint32_t i = begin; i < end; ++i)
        {
            update_particle_basic(particles, i, desc, constants);
        }
        return;
    }
    for(uint32_t i = begin; i < end; ++i)
    {
        update_particle_full(particles, i, desc, constants);
    }
}

struct emitter
{
    void create(emitter_shape shape, emitter_direction direction, uint32_t max_particles)
    {
        sim.reset();
        shape_ = shape;
        direction_ = direction;
        particles_.resize(max_particles);
        rng_.reset();
    }

    void destroy()
    {
        gpu_.destroy_buffers();
        particles_ = particle_soa{};
    }

    void reset()
    {
        sim.reset();
        particles_.clear_live();
        rng_.reset();
        gpu_.pending_pack = false;
        gpu_.luts_valid = false;
    }

    auto resolve_backend() const -> particle_sim_backend
    {
        if(!g_gpu_sim_available)
        {
            return particle_sim_backend::cpu;
        }
        if(gpu_.has_backend_override)
        {
            return gpu_.backend_override;
        }
        return g_default_sim_backend;
    }

    auto wants_gpu_pack() const -> bool
    {
        // Artist / component selects backend; no particle-count gate.
        return g_gpu_sim_available && resolve_backend() == particle_sim_backend::gpu;
    }

    void fill_gradient_lut(const math::gradient<math::color>& gradient, std::vector<math::vec4>& out_lut)
    {
        out_lut.resize(k_gpu_lut_size);
        for(uint32_t i = 0; i < k_gpu_lut_size; ++i)
        {
            const float t = float(i) / float(k_gpu_lut_size - 1);
            const math::color c = gradient.sample(t);
            out_lut[i] = math::vec4(c.value.r, c.value.g, c.value.b, c.value.a);
        }
    }

    void ensure_gpu_luts(const emitter_desc& desc, const emitter_sim_constants& constants)
    {
        const bool need_color_speed = has_feature(constants.features, emitter_feature::color_by_speed);
        const bool need_ease = has_feature(constants.features, emitter_feature::non_linear_ease);
        if(gpu_.luts_valid && gpu_.cached_ease_pos == constants.ease_pos &&
           gpu_.cached_need_color_speed == need_color_speed && gpu_.cached_need_ease == need_ease &&
           gpu_.cached_features == constants.features)
        {
            return;
        }
        fill_gradient_lut(desc.appearance.color_gradient, gpu_.color_lut);
        if(need_color_speed)
        {
            fill_gradient_lut(desc.appearance.color_by_speed_gradient, gpu_.color_speed_lut);
        }
        else if(gpu_.color_speed_lut.size() != k_gpu_lut_size)
        {
            gpu_.color_speed_lut.assign(k_gpu_lut_size, math::vec4(1.0f));
        }
        gpu_.ease_lut.resize(k_gpu_lut_size);
        for(uint32_t i = 0; i < k_gpu_lut_size; ++i)
        {
            const float t = float(i) / float(k_gpu_lut_size - 1);
            const float eased = (need_ease && constants.ease_pos) ? constants.ease_pos(t) : t;
            gpu_.ease_lut[i] = math::vec4(eased, 0.0f, 0.0f, 0.0f);
        }
        gpu_.cached_ease_pos = constants.ease_pos;
        gpu_.cached_need_color_speed = need_color_speed;
        gpu_.cached_need_ease = need_ease;
        gpu_.cached_features = constants.features;
        gpu_.luts_valid = true;
        gpu_.luts_gpu_dirty = true;
    }

    void prepare_gpu_pack(const emitter_desc& desc, const emitter_sim_constants& constants, math::bbox& aabb)
    {
        APP_SCOPE_PERF("Particles/SOA Prepare GPU Pack");
        const uint32_t count = particles_.count;
        gpu_.constants = constants;
        gpu_.sim_staging.resize(count);
        ensure_gpu_luts(desc, constants);
        const bool need_align = has_feature(constants.features, emitter_feature::align_to_direction);
        const bool need_ease = has_feature(constants.features, emitter_feature::non_linear_ease);
        const bool need_local = has_feature(constants.features, emitter_feature::local_space);
        const bool need_size_speed = has_feature(constants.features, emitter_feature::size_by_speed);
        const float base_extent = math::max(constants.particle_scale_3d.x,
                                            math::max(constants.particle_scale_3d.y, constants.particle_scale_3d.z)) *
                                  0.5f;
        const math::vec2 pivot_offset = constants.pivot - math::vec2(0.5f, 0.5f);
        const float pivot_pad_factor = math::max(math::abs(pivot_offset.x), math::abs(pivot_offset.y)) * 2.0f;
        const float size_speed_mul =
            need_size_speed ? math::max(constants.size_by_speed_range.min, constants.size_by_speed_range.max) : 1.0f;
        for(uint32_t i = 0; i < count; ++i)
        {
            const float life = particles_.life[i];
            const float tt = (need_ease && constants.ease_pos) ? constants.ease_pos(life) : life;
            gpu_sim_particle& dst = gpu_.sim_staging[i];
            dst.start_x = particles_.start[i].x;
            dst.start_y = particles_.start[i].y;
            dst.start_z = particles_.start[i].z;
            dst.life = life;
            dst.end0_x = particles_.end0[i].x;
            dst.end0_y = particles_.end0[i].y;
            dst.end0_z = particles_.end0[i].z;
            dst.lifespan = particles_.lifespan[i];
            dst.end1_x = particles_.end1[i].x;
            dst.end1_y = particles_.end1[i].y;
            dst.end1_z = particles_.end1[i].z;
            dst.scale_start = particles_.scale_start[i];
            dst.scale_end = particles_.scale_end[i];
            dst.texsheet_seed = particles_.texsheet_seed[i];
            dst.pad0 = 0.0f;
            dst.pad1 = 0.0f;
            math::quat rot = math::identity<math::quat>();
            if(need_align)
            {
                const math::vec3 velocity0 = particles_.end0[i] - particles_.start[i];
                const math::vec3 velocity1 = particles_.end1[i] - particles_.end0[i];
                const math::vec3 current_velocity = math::mix(velocity0, velocity1, tt);
                const float velocity_len_sq = math::dot(current_velocity, current_velocity);
                if(velocity_len_sq > 0.0001f)
                {
                    const math::vec3 direction = math::normalize(current_velocity);
                    math::vec3 up_ref(0.0f, 1.0f, 0.0f);
                    if(math::abs(math::dot(direction, up_ref)) > 0.99f)
                    {
                        up_ref = math::vec3(1.0f, 0.0f, 0.0f);
                    }
                    rot = math::look_rotation(direction, up_ref);
                }
            }
            dst.rot_x = rot.x;
            dst.rot_y = rot.y;
            dst.rot_z = rot.z;
            dst.rot_w = rot.w;
            const math::vec3 p0 = math::mix(particles_.start[i], particles_.end0[i], tt);
            const math::vec3 p1 = math::mix(particles_.end0[i], particles_.end1[i], tt);
            math::vec3 local_pos = math::mix(p0, p1, tt);
            if(need_local)
            {
                const math::vec4 world_pos4 = constants.local_to_world * math::vec4(local_pos, 1.0f);
                particles_.position[i] = math::vec3(world_pos4.x, world_pos4.y, world_pos4.z);
            }
            else
            {
                particles_.position[i] = local_pos;
            }
            const float scale =
                math::mix(particles_.scale_start[i], particles_.scale_end[i], life) * constants.avg_system_scale *
                size_speed_mul;
            particles_.scale[i] = scale;
            const float max_extent = base_extent * scale;
            const float radius = max_extent + pivot_pad_factor * max_extent;
            expand_aabb_sphere(aabb, particles_.position[i], radius);
        }
        gpu_.pending_pack = count > 0;
    }

    void update(float dt,
                const emitter_desc& desc,
                emitter_transform_state& transform,
                emitter_playback_desc& playback)
    {
        if(sim.first_update)
        {
            transform.previous = transform.current;
        }
        const bool was_playing = sim.playing;
        const bool was_loop = sim.loop;
        sim.playing = playback.playing;
        sim.loop = desc.emission.loop;
        if(was_playing != sim.playing || was_loop != sim.loop)
        {
            sim.total_particles_spawned = 0;
            if(sim.playing && !was_playing)
            {
                sim.start_delay_elapsed = 0.0f;
            }
        }
        float sim_dt = dt;
        if(playback.paused)
        {
            sim_dt = 0.0f;
        }
        else if(sim.playing)
        {
            sim.start_delay_elapsed += dt;
        }
        const math::vec3 current_pos = transform.current.get_position();
        if(!playback.paused)
        {
            sim.push_temporal_sample(current_pos, sim_dt);
        }
        if(!desc.emission.loop && sim.total_particles_spawned >= particles_.capacity)
        {
            playback.playing = false;
            sim.playing = false;
            sim.total_particles_spawned = 0;
        }
        emitter_sim_constants constants{};
        bake_constants(desc, transform, constants);
        sim.features = constants.features;
        math::bbox aabb;
        aabb.reset();
        aabb.add_point(current_pos - math::vec3(0.5f));
        aabb.add_point(current_pos + math::vec3(0.5f));
        compact_alive(sim_dt);
        const bool use_gpu = wants_gpu_pack();
        if(!use_gpu)
        {
            APP_SCOPE_PERF("Particles/SOA Update Properties");
            update_particles_range(particles_, 0, particles_.count, desc, constants);
        }
        else
        {
            gpu_.pending_pack = false;
        }
        if(desc.emission.emission_lifetime > 0.0f && playback.playing)
        {
            const bool start_delay_elapsed = sim.start_delay_elapsed >= desc.emission.start_delay;
            const bool initial_emission_complete = sim.total_particles_spawned >= particles_.capacity;
            if(start_delay_elapsed && (desc.emission.loop || !initial_emission_complete))
            {
                spawn(desc, constants, transform, sim_dt, use_gpu);
            }
        }
        particles_.count = math::min(particles_.count, particles_.capacity);
        if(use_gpu)
        {
            prepare_gpu_pack(desc, constants, aabb);
        }
        else
        {
            accumulate_world_bounds(aabb, constants);
        }
        if(sim.first_update)
        {
            sim.first_update = false;
        }
        sim.world_bounds = aabb;
        transform.previous = transform.current;
    }

    void compact_alive(float sim_dt)
    {
        const uint32_t old_count = particles_.count;
        if(old_count == 0)
        {
            return;
        }
        APP_SCOPE_PERF("Particles/SOA Compact");
        uint32_t write = 0;
        for(uint32_t i = 0; i < old_count; ++i)
        {
            if(particles_.lifespan[i] <= 0.0f)
            {
                continue;
            }
            particles_.life[i] += sim_dt / particles_.lifespan[i];
            if(particles_.life[i] > 1.0f)
            {
                continue;
            }
            if(write != i)
            {
                particles_.move_sim_particle(write, i);
            }
            ++write;
        }
        particles_.count = write;
    }

    void accumulate_world_bounds(math::bbox& aabb, const emitter_sim_constants& constants)
    {
        if(particles_.count == 0)
        {
            return;
        }
        APP_SCOPE_PERF("Particles/SOA Bounds");
        const float base_extent = math::max(constants.particle_scale_3d.x,
                                            math::max(constants.particle_scale_3d.y, constants.particle_scale_3d.z)) *
                                  0.5f;
        const math::vec2 pivot_offset = constants.pivot - math::vec2(0.5f, 0.5f);
        const float pivot_pad_factor = math::max(math::abs(pivot_offset.x), math::abs(pivot_offset.y)) * 2.0f;
        for(uint32_t i = 0; i < particles_.count; ++i)
        {
            const float max_extent = base_extent * particles_.scale[i];
            const float radius = max_extent + pivot_pad_factor * max_extent;
            expand_aabb_sphere(aabb, particles_.position[i], radius);
        }
    }

    void spawn(const emitter_desc& desc,
               const emitter_sim_constants& constants,
               const emitter_transform_state& transform,
               float dt,
               bool skip_cpu_properties)
    {
        if(desc.emission.particles_per_second <= 0.0f)
        {
            return;
        }
        const float time_per_particle = 1.0f / desc.emission.particles_per_second;
        sim.emission_time_accum += dt;
        const uint32_t num_to_emit = uint32_t(sim.emission_time_accum / time_per_particle);
        sim.emission_time_accum -= float(num_to_emit) * time_per_particle;
        const uint32_t max_emittable = particles_.capacity - particles_.count;
        const uint32_t actual_emit_count = math::min(num_to_emit, max_emittable);
        if(actual_emit_count == 0)
        {
            return;
        }
        const math::vec3 effective_position = transform.current.get_position();
        const math::vec3 system_scale = transform.current.get_scale();
        const math::vec3 emission_shape_scale = desc.emission.shape_scale;
        const math::mat4 effective_transform = transform.current;
        const math::mat3 rotation_matrix = math::mat3(effective_transform);
        float lifetime_multiplier = 1.0f;
        if(has_feature(constants.features, emitter_feature::lifetime_by_emitter_speed))
        {
            const float emitter_speed =
                sim.calculate_smoothed_emitter_speed(desc.motion.lifetime_by_emitter_speed_range.max);
            const float speed_factor = math::clamp(
                (emitter_speed - desc.motion.lifetime_by_emitter_speed_range.min) /
                    (desc.motion.lifetime_by_emitter_speed_range.max - desc.motion.lifetime_by_emitter_speed_range.min),
                0.0f,
                1.0f);
            lifetime_multiplier = desc.motion.lifetime_by_emitter_speed_gradient.sample(speed_factor);
        }
        const float life_span = math::max(desc.motion.lifetime * lifetime_multiplier, k_min_particle_lifespan);
        const float life_span_squared = life_span * life_span;
        math::vec3 gravity_vector(0.0f, -9.81f * desc.motion.gravity_scale * life_span_squared, 0.0f);
        math::vec3 force_vector = desc.motion.force_over_lifetime * life_span_squared;
        if(desc.motion.space == simulation_space::world)
        {
            gravity_vector.y *= system_scale.y;
            force_vector *= system_scale;
        }
        const float velocity_damping_factor = (1.0f - desc.motion.velocity_damping);
        math::vec3 prev_pos = transform.previous.get_position();
        if(sim.temporal_count >= 2)
        {
            prev_pos = sim.temporal_positions[sim.temporal_count - 2];
        }
        const uint32_t base_index = particles_.count;
        particles_.count += actual_emit_count;
        sim.total_particles_spawned += actual_emit_count;
        const auto emit_one = [&](uint32_t ii, bx::RngMwc& rng)
        {
            const float base_emission_phase = float(ii) / float(actual_emit_count);
            const float emission_phase = base_emission_phase * desc.motion.temporal_motion;
            const uint32_t index = base_index + ii;
            const math::vec3 up(0.0f, 1.0f, 0.0f);
            math::vec3 pos;
            if(desc.emission.spawn_location == spawn_location::surface)
            {
                switch(shape_)
                {
                    default:
                    case emitter_shape::sphere:
                        pos = random_unit_vector(rng);
                        break;
                    case emitter_shape::hemisphere:
                    {
                        math::vec3 sphere_pos = random_unit_vector(rng);
                        if(sphere_pos.y < 0.0f)
                        {
                            sphere_pos.y = -sphere_pos.y;
                        }
                        pos = sphere_pos;
                    }
                    break;
                    case emitter_shape::circle:
                    {
                        const math::vec2 circle_pos = random_on_unit_circle(rng);
                        pos = math::vec3(circle_pos.x, 0.0f, circle_pos.y);
                    }
                    break;
                    case emitter_shape::box:
                    {
                        const int face_index = int(frand01(rng) * 6.0f);
                        const float u = frand_range(rng, -1.0f, 1.0f);
                        const float v = frand_range(rng, -1.0f, 1.0f);
                        switch(face_index)
                        {
                            case 0: pos = math::vec3(1.0f, u, v); break;
                            case 1: pos = math::vec3(-1.0f, u, v); break;
                            case 2: pos = math::vec3(u, 1.0f, v); break;
                            case 3: pos = math::vec3(u, -1.0f, v); break;
                            case 4: pos = math::vec3(u, v, 1.0f); break;
                            default: pos = math::vec3(u, v, -1.0f); break;
                        }
                    }
                    break;
                    case emitter_shape::rect:
                    {
                        const int edge_index = int(frand01(rng) * 4.0f);
                        const float t = frand_range(rng, -1.0f, 1.0f);
                        switch(edge_index)
                        {
                            case 0: pos = math::vec3(t, 0.0f, 1.0f); break;
                            case 1: pos = math::vec3(1.0f, 0.0f, t); break;
                            case 2: pos = math::vec3(t, 0.0f, -1.0f); break;
                            default: pos = math::vec3(-1.0f, 0.0f, t); break;
                        }
                    }
                    break;
                }
            }
            else
            {
                switch(shape_)
                {
                    default:
                    case emitter_shape::sphere:
                        pos = random_in_unit_ball(rng);
                        break;
                    case emitter_shape::hemisphere:
                    {
                        math::vec3 sphere_pos = random_in_unit_ball(rng);
                        if(math::dot(sphere_pos, up) < 0.0f)
                        {
                            sphere_pos = -sphere_pos;
                        }
                        pos = sphere_pos;
                    }
                    break;
                    case emitter_shape::circle:
                    {
                        const math::vec2 circle_pos = random_in_unit_disk(rng);
                        pos = math::vec3(circle_pos.x, 0.0f, circle_pos.y);
                    }
                    break;
                    case emitter_shape::box:
                        pos = math::vec3(frand_range(rng, -1.0f, 1.0f),
                                         frand_range(rng, -1.0f, 1.0f),
                                         frand_range(rng, -1.0f, 1.0f));
                        break;
                    case emitter_shape::rect:
                        pos = math::vec3(frand_range(rng, -1.0f, 1.0f), 0.0f, frand_range(rng, -1.0f, 1.0f));
                        break;
                }
            }
            pos = (desc.emission.shape_position + pos) * emission_shape_scale;
            math::vec3 dir;
            switch(direction_)
            {
                default:
                case emitter_direction::up:
                    dir = up;
                    break;
                case emitter_direction::outward:
                {
                    const float len_sq = math::dot(pos, pos);
                    dir = (len_sq > k_emit_dir_zero_len_sq) ? math::normalize(pos) : up;
                }
                break;
                case emitter_direction::inward:
                {
                    const float len_sq = math::dot(pos, pos);
                    dir = (len_sq > k_emit_dir_zero_len_sq) ? math::normalize(pos) : up;
                }
                break;
            }
            math::vec3 start = pos;
            const frange_t end_velocity_range = desc.motion.velocity_gradient.sample(1.0f);
            const float end_velocity = math::mix(end_velocity_range.min, end_velocity_range.max, frand01(rng));
            math::vec3 end = dir * end_velocity + start;
            if(direction_ == emitter_direction::inward)
            {
                std::swap(start, end);
                dir *= -1.0f;
            }
            particles_.lifespan[index] = life_span;
            particles_.life[index] = 0.0f;
            const math::vec3 interpolated_emitter_pos = math::mix(prev_pos, effective_position, emission_phase);
            if(desc.motion.space == simulation_space::local)
            {
                particles_.start[index] = start;
                particles_.end0[index] = end;
            }
            else
            {
                particles_.start[index] = rotation_matrix * start + interpolated_emitter_pos;
                particles_.end0[index] = rotation_matrix * end + interpolated_emitter_pos;
            }
            if(desc.motion.velocity_damping > 0.0f)
            {
                const math::vec3 velocity = particles_.end0[index] - particles_.start[index];
                particles_.end0[index] = particles_.start[index] + velocity * velocity_damping_factor;
            }
            particles_.end1[index] = particles_.end0[index] + gravity_vector + force_vector;
            const frange_t start_scale_range = desc.appearance.scale_gradient.sample(0.0f);
            const frange_t end_scale_range = desc.appearance.scale_gradient.sample(1.0f);
            particles_.scale_start[index] = math::mix(start_scale_range.min, start_scale_range.max, frand01(rng));
            particles_.scale_end[index] = math::mix(end_scale_range.min, end_scale_range.max, frand01(rng));
            particles_.texsheet_seed[index] = frand01(rng);
            if(!skip_cpu_properties)
            {
                update_particle_properties(particles_, index, desc, constants);
            }
        };
        APP_SCOPE_PERF("Particles/SOA Spawn");
        for(uint32_t ii = 0; ii < actual_emit_count; ++ii)
        {
            emit_one(ii, rng_);
        }
    }

    emitter_shape shape_ = emitter_shape::sphere;
    emitter_direction direction_ = emitter_direction::up;
    particle_soa particles_;
    emitter_sim_state sim;
    bx::RngMwc rng_;
    emitter_sim_constants cached_constants_{};
    emitter_gpu_resources gpu_;
};

struct batched_particle
{
    float dist = 0.0f;
    uint32_t emitter_idx = 0;
    uint32_t particle_idx = 0;
};

auto float_to_sortable_uint(float value) -> uint32_t
{
    uint32_t bits = 0;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(uint32_t));
    const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(bits >> 31)) | 0x80000000u;
    return bits ^ mask;
}

void radix_sort_desc_distances(std::vector<batched_particle>& items, std::vector<batched_particle>& scratch)
{
    const uint32_t n = static_cast<uint32_t>(items.size());
    if(n < 2)
    {
        return;
    }
    scratch.resize(n);
    constexpr uint32_t k_bits = 8;
    constexpr uint32_t k_bins = 1u << k_bits;
    constexpr uint32_t k_passes = 4;
    uint32_t counts[k_bins];
    for(uint32_t pass = 0; pass < k_passes; ++pass)
    {
        const uint32_t shift = pass * k_bits;
        std::memset(counts, 0, sizeof(counts));
        for(uint32_t i = 0; i < n; ++i)
        {
            const uint32_t key = ~float_to_sortable_uint(items[i].dist);
            ++counts[(key >> shift) & (k_bins - 1u)];
        }
        uint32_t sum = 0;
        for(uint32_t b = 0; b < k_bins; ++b)
        {
            const uint32_t c = counts[b];
            counts[b] = sum;
            sum += c;
        }
        for(uint32_t i = 0; i < n; ++i)
        {
            const uint32_t key = ~float_to_sortable_uint(items[i].dist);
            const uint32_t bin = (key >> shift) & (k_bins - 1u);
            scratch[counts[bin]++] = items[i];
        }
        items.swap(scratch);
    }
}

struct particle_system_soa
{
    void init(uint16_t max_emitters)
    {
        static bx::DefaultAllocator allocator;
        allocator_ = &allocator;
        emitter_alloc_ = bx::createHandleAlloc(allocator_, max_emitters);
        emitters_.resize(max_emitters);
        particle_vertex::init();
        quad_vbh_ = bgfx::createVertexBuffer(bgfx::makeRef(s_quad_vertices, sizeof(s_quad_vertices)),
                                             particle_vertex::ms_layout);
        quad_ibh_ = bgfx::createIndexBuffer(bgfx::makeRef(s_quad_indices, sizeof(s_quad_indices)));
        tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
        view_camera_ = bgfx::createUniform("u_viewCamera", bgfx::UniformType::Mat4);
        eye_pos_ = bgfx::createUniform("u_eyePos", bgfx::UniformType::Vec4);
    }

    void shutdown()
    {
        for(auto& em : emitters_)
        {
            em.gpu_.destroy_buffers();
        }
        if(bgfx::isValid(g_u_pack0))
        {
            bgfx::destroy(g_u_pack0);
            g_u_pack0 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_pack1))
        {
            bgfx::destroy(g_u_pack1);
            g_u_pack1 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_pack2))
        {
            bgfx::destroy(g_u_pack2);
            g_u_pack2 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_pack3))
        {
            bgfx::destroy(g_u_pack3);
            g_u_pack3 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_pack4))
        {
            bgfx::destroy(g_u_pack4);
            g_u_pack4 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_pack5))
        {
            bgfx::destroy(g_u_pack5);
            g_u_pack5 = BGFX_INVALID_HANDLE;
        }
        if(bgfx::isValid(g_u_local_to_world))
        {
            bgfx::destroy(g_u_local_to_world);
            g_u_local_to_world = BGFX_INVALID_HANDLE;
        }
        g_pack_program.reset();
        g_gpu_sim_available = false;
        g_default_sim_backend = particle_sim_backend::cpu;
        bgfx::destroy(tex_color_);
        bgfx::destroy(view_camera_);
        bgfx::destroy(eye_pos_);
        bgfx::destroy(quad_vbh_);
        bgfx::destroy(quad_ibh_);
        bx::destroyHandleAlloc(allocator_, emitter_alloc_);
        allocator_ = nullptr;
    }

    
    void init_gpu(rtti::context& ctx)
    {
        auto& am = ctx.get_cached<asset_manager>();
        auto cs = am.get_asset<gfx::shader>("engine:/data/shaders/particles/cs_particle_pack.sc");
        if(!cs)
        {
            APPLOG_WARNING("Particles: GPU pack shader missing; CPU backend only");
            g_gpu_sim_available = false;
            return;
        }
        g_pack_program = std::make_shared<gpu_program>(cs);
        if(!g_pack_program || !g_pack_program->is_valid())
        {
            APPLOG_WARNING("Particles: GPU pack program invalid; CPU backend only");
            g_pack_program.reset();
            g_gpu_sim_available = false;
            return;
        }
        g_u_pack0 = bgfx::createUniform("u_pack0", bgfx::UniformType::Vec4);
        g_u_pack1 = bgfx::createUniform("u_pack1", bgfx::UniformType::Vec4);
        g_u_pack2 = bgfx::createUniform("u_pack2", bgfx::UniformType::Vec4);
        g_u_pack3 = bgfx::createUniform("u_pack3", bgfx::UniformType::Vec4);
        g_u_pack4 = bgfx::createUniform("u_pack4", bgfx::UniformType::Vec4);
        g_u_pack5 = bgfx::createUniform("u_pack5", bgfx::UniformType::Vec4);
        g_u_local_to_world = bgfx::createUniform("u_localToWorld", bgfx::UniformType::Mat4);
        ensure_gpu_layouts();
        g_gpu_sim_available = true;
        APPLOG_INFO("Particles: GPU pack available (per-emitter Simulation Backend)");
    }

    auto dispatch_gpu_pack(emitter& em, bool sort_by_depth, const math::vec3& eye, bgfx::ViewId pack_view) -> bool
    {
        if(!em.gpu_.pending_pack || em.particles_.count == 0 || !g_pack_program)
        {
            return false;
        }
        APP_SCOPE_PERF("Particles/SOA GPU Pack Dispatch");
        const uint32_t count = em.particles_.count;
        em.gpu_.ensure_capacity(em.particles_.capacity);
        if(!bgfx::isValid(em.gpu_.sim_vb) || !bgfx::isValid(em.gpu_.instance_vb))
        {
            return false;
        }
        if(sort_by_depth)
        {
            em.gpu_.order_staging.resize(count);
            gpu_sort_scratch_.resize(count);
            for(uint32_t i = 0; i < count; ++i)
            {
                const math::vec3 delta = eye - em.particles_.position[i];
                gpu_sort_scratch_[i] = batched_particle{math::dot(delta, delta), 0, i};
            }
            if(count >= 512)
            {
                radix_sort_desc_distances(gpu_sort_scratch_, radix_scratch_);
            }
            else
            {
                std::sort(gpu_sort_scratch_.begin(),
                          gpu_sort_scratch_.end(),
                          [](const batched_particle& a, const batched_particle& b)
                          {
                              return a.dist > b.dist;
                          });
            }
            for(uint32_t i = 0; i < count; ++i)
            {
                em.gpu_.order_staging[i] = gpu_sort_scratch_[i].particle_idx;
            }
            bgfx::update(em.gpu_.order_ib,
                         0,
                         bgfx::copy(em.gpu_.order_staging.data(), uint32_t(sizeof(uint32_t) * count)));
            em.gpu_.order_identity_uploaded = false;
        }
        else if(!em.gpu_.order_identity_uploaded)
        {
            em.gpu_.order_staging.resize(em.gpu_.gpu_capacity);
            for(uint32_t i = 0; i < em.gpu_.gpu_capacity; ++i)
            {
                em.gpu_.order_staging[i] = i;
            }
            bgfx::update(em.gpu_.order_ib,
                         0,
                         bgfx::copy(em.gpu_.order_staging.data(),
                                    uint32_t(sizeof(uint32_t) * em.gpu_.gpu_capacity)));
            em.gpu_.order_identity_uploaded = true;
        }
        bgfx::update(em.gpu_.sim_vb,
                     0,
                     bgfx::copy(em.gpu_.sim_staging.data(), uint32_t(sizeof(gpu_sim_particle) * count)));
        if(em.gpu_.luts_gpu_dirty)
        {
            bgfx::update(em.gpu_.color_lut_vb,
                         0,
                         bgfx::copy(em.gpu_.color_lut.data(), uint32_t(sizeof(math::vec4) * k_gpu_lut_size)));
            bgfx::update(em.gpu_.color_speed_lut_vb,
                         0,
                         bgfx::copy(em.gpu_.color_speed_lut.data(), uint32_t(sizeof(math::vec4) * k_gpu_lut_size)));
            bgfx::update(em.gpu_.ease_lut_vb,
                         0,
                         bgfx::copy(em.gpu_.ease_lut.data(), uint32_t(sizeof(math::vec4) * k_gpu_lut_size)));
            em.gpu_.luts_gpu_dirty = false;
        }
        const auto& c = em.gpu_.constants;
        float pack0[4] = {c.opacity,
                          c.color_intensity,
                          c.avg_system_scale,
                          float(static_cast<int>(c.render_mode))};
        float pack1[4] = {c.pivot.x, c.pivot.y, float(count), float(gpu_feature_mask(c.features))};
        float pack2[4] = {c.particle_scale_3d.x, c.particle_scale_3d.y, c.particle_scale_3d.z, c.tex_sheet_cycles};
        float pack3[4] = {c.tex_sheet_tiles.x,
                          c.tex_sheet_tiles.y,
                          c.tex_sheet_randomize ? 1.0f : 0.0f,
                          0.0f};
        float pack4[4] = {c.size_by_speed_range.min,
                          c.size_by_speed_range.max,
                          c.inv_size_by_speed_velocity_span,
                          c.size_by_speed_velocity_range.min};
        float pack5[4] = {c.inv_color_by_speed_velocity_span, c.color_by_speed_velocity_range.min, 0.0f, 0.0f};
        if(!g_pack_program->begin())
        {
            return false;
        }
        bgfx::setBuffer(0, em.gpu_.sim_vb, bgfx::Access::Read);
        bgfx::setBuffer(1, em.gpu_.order_ib, bgfx::Access::Read);
        bgfx::setBuffer(2, em.gpu_.instance_vb, bgfx::Access::Write);
        bgfx::setBuffer(3, em.gpu_.color_lut_vb, bgfx::Access::Read);
        bgfx::setBuffer(4, em.gpu_.color_speed_lut_vb, bgfx::Access::Read);
        bgfx::setBuffer(5, em.gpu_.ease_lut_vb, bgfx::Access::Read);
        bgfx::setUniform(g_u_pack0, pack0);
        bgfx::setUniform(g_u_pack1, pack1);
        bgfx::setUniform(g_u_pack2, pack2);
        bgfx::setUniform(g_u_pack3, pack3);
        bgfx::setUniform(g_u_pack4, pack4);
        bgfx::setUniform(g_u_pack5, pack5);
        bgfx::setUniform(g_u_local_to_world, &c.local_to_world[0][0]);
        const uint32_t groups = (count + k_gpu_cs_threads - 1) / k_gpu_cs_threads;
        bgfx::dispatch(pack_view, g_pack_program->native_handle(), groups, 1, 1);
        g_pack_program->end();
        return true;
    }

    auto draw_gpu_emitter(emitter& em,
                          uint8_t view,
                          bgfx::ProgramHandle program,
                          const float* view_camera,
                          const float* eye_pos_vec4,
                          bgfx::TextureHandle texture,
                          uint64_t blend_state) -> uint32_t
    {
        APP_SCOPE_PERF("Particles/SOA GPU Draw");
        bgfx::setVertexBuffer(0, quad_vbh_);
        bgfx::setIndexBuffer(quad_ibh_);
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       blend_state);
        bgfx::setTexture(0, tex_color_, texture);
        bgfx::setUniform(view_camera_, view_camera);
        bgfx::setUniform(eye_pos_, eye_pos_vec4);
        bgfx::setInstanceDataBuffer(em.gpu_.instance_vb, 0, em.particles_.count);
        bgfx::submit(view, program);
        return em.particles_.count;
    }

    auto create_emitter(emitter_shape shape, emitter_direction direction, uint32_t max_particles) -> emitter_handle
    {
        emitter_handle handle{emitter_alloc_->alloc()};
        if(is_valid(handle))
        {
            emitters_[handle.idx].create(shape, direction, max_particles);
        }
        return handle;
    }

    void destroy_emitter(emitter_handle handle)
    {
        BX_ASSERT(is_valid(handle), "destroy_emitter invalid handle");
        emitters_[handle.idx].destroy();
        emitter_alloc_->free(handle.idx);
    }

    void reset_emitter(emitter_handle handle)
    {
        BX_ASSERT(is_valid(handle), "reset_emitter invalid handle");
        emitters_[handle.idx].reset();
    }

    void update_emitter(emitter_handle handle,
                        float dt,
                        const emitter_desc& desc,
                        emitter_transform_state& transform,
                        emitter_playback_desc& playback)
    {
        BX_ASSERT(is_valid(handle), "update_emitter invalid handle");
        emitters_[handle.idx].update(dt, desc, transform, playback);
        bake_constants(desc, transform, emitters_[handle.idx].cached_constants_);
    }

    auto has_updated(emitter_handle handle) -> bool
    {
        BX_ASSERT(is_valid(handle), "has_updated invalid handle");
        return !emitters_[handle.idx].sim.first_update;
    }

    void get_aabb(emitter_handle handle, math::bbox& out_aabb)
    {
        BX_ASSERT(is_valid(handle), "get_aabb invalid handle");
        out_aabb = emitters_[handle.idx].sim.world_bounds;
    }

    auto get_num_particles(emitter_handle handle) -> uint32_t
    {
        BX_ASSERT(is_valid(handle), "get_num_particles invalid handle");
        return emitters_[handle.idx].particles_.count;
    }

    void set_emitter_sim_backend(emitter_handle handle, particle_sim_backend backend)
    {
        BX_ASSERT(is_valid(handle), "set_emitter_sim_backend invalid handle");
        emitters_[handle.idx].gpu_.backend_override = backend;
        emitters_[handle.idx].gpu_.has_backend_override = true;
    }

    auto get_emitter_sim_backend(emitter_handle handle) -> particle_sim_backend
    {
        BX_ASSERT(is_valid(handle), "get_emitter_sim_backend invalid handle");
        return emitters_[handle.idx].resolve_backend();
    }

    static void write_instance_row(uint8_t* row, const emitter& em, uint32_t particle_idx)
    {
        const auto& p = em.particles_;
        float* pos = reinterpret_cast<float*>(row);
        pos[0] = p.position[particle_idx].x;
        pos[1] = p.position[particle_idx].y;
        pos[2] = p.position[particle_idx].z;
        pos[3] = em.cached_constants_.pivot.x;
        float* rot = reinterpret_cast<float*>(row + 16);
        rot[0] = p.rotation[particle_idx].x;
        rot[1] = p.rotation[particle_idx].y;
        rot[2] = p.rotation[particle_idx].z;
        rot[3] = p.rotation[particle_idx].w;
        float* scale3d = reinterpret_cast<float*>(row + 32);
        scale3d[0] = p.scale[particle_idx] * em.cached_constants_.particle_scale_3d.x;
        scale3d[1] = p.scale[particle_idx] * em.cached_constants_.particle_scale_3d.y;
        scale3d[2] = p.scale[particle_idx] * em.cached_constants_.particle_scale_3d.z;
        scale3d[3] = em.cached_constants_.pivot.y;
        float* uv = reinterpret_cast<float*>(row + 48);
        uv[0] = p.uv_offset[particle_idx].x;
        uv[1] = p.uv_offset[particle_idx].y;
        uv[2] = p.uv_scale[particle_idx].x;
        uv[3] = p.uv_scale[particle_idx].y;
        float* color = reinterpret_cast<float*>(row + 64);
        color[0] = p.color[particle_idx].value.r;
        color[1] = p.color[particle_idx].value.g;
        color[2] = p.color[particle_idx].value.b;
        color[3] = p.color[particle_idx].value.a;
        float* facing = reinterpret_cast<float*>(row + 80);
        facing[0] = static_cast<float>(static_cast<int>(em.cached_constants_.render_mode));
        facing[1] = 0.0f;
        facing[2] = 0.0f;
        facing[3] = 0.0f;
    }

    auto build_prefixes(const emitter_handle* handles, uint32_t count) -> uint32_t
    {
        prefix_scratch_.resize(count + 1);
        prefix_scratch_[0] = 0;
        for(uint32_t i = 0; i < count; ++i)
        {
            uint32_t n = 0;
            if(is_valid(handles[i]))
            {
                n = emitters_[handles[i].idx].particles_.count;
            }
            prefix_scratch_[i + 1] = prefix_scratch_[i] + n;
        }
        return prefix_scratch_[count];
    }

    void build_sorted(const emitter_handle* handles, uint32_t count, const math::vec3& eye, uint32_t total)
    {
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/Build Sorted");
        batched_scratch_.resize(total);
        {
            APP_SCOPE_PERF("Rendering/Particle Pass SOA/Sort Keys");
            const auto fill_emitter_keys = [&](uint32_t emitter_idx)
            {
                const uint32_t start = prefix_scratch_[emitter_idx];
                const uint32_t end = prefix_scratch_[emitter_idx + 1];
                if(start == end)
                {
                    return;
                }
                const auto& em = emitters_[handles[emitter_idx].idx];
                for(uint32_t p = 0; p < end - start; ++p)
                {
                    const math::vec3 delta = eye - em.particles_.position[p];
                    batched_scratch_[start + p] = batched_particle{math::dot(delta, delta), emitter_idx, p};
                }
            };
            constexpr uint32_t k_parallel_particle_threshold = 2048;
            constexpr uint32_t k_min_rows_per_job = 512;
            constexpr uint32_t k_parallel_emitter_threshold = 16;
            constexpr uint32_t k_min_emitters_per_job = 16;
            if(total >= k_parallel_particle_threshold && count <= 4)
            {
                // Few large emitters: parallelize by particle rows across the batch.
                const uint32_t num_jobs = (total + k_min_rows_per_job - 1) / k_min_rows_per_job;
                std::for_each(poolstl::par,
                              poolstl::iota_iter<uint32_t>(0),
                              poolstl::iota_iter<uint32_t>(num_jobs),
                              [&](uint32_t job)
                              {
                                  const uint32_t global_begin = job * k_min_rows_per_job;
                                  const uint32_t global_end = math::min(global_begin + k_min_rows_per_job, total);
                                  for(uint32_t emitter_idx = 0; emitter_idx < count; ++emitter_idx)
                                  {
                                      const uint32_t emit_begin = prefix_scratch_[emitter_idx];
                                      const uint32_t emit_end = prefix_scratch_[emitter_idx + 1];
                                      const uint32_t range_begin = math::max(emit_begin, global_begin);
                                      const uint32_t range_end = math::min(emit_end, global_end);
                                      if(range_begin >= range_end)
                                      {
                                          continue;
                                      }
                                      const auto& em = emitters_[handles[emitter_idx].idx];
                                      for(uint32_t g = range_begin; g < range_end; ++g)
                                      {
                                          const uint32_t p = g - emit_begin;
                                          const math::vec3 delta = eye - em.particles_.position[p];
                                          batched_scratch_[g] = batched_particle{math::dot(delta, delta), emitter_idx, p};
                                      }
                                  }
                              });
            }
            else if(count >= k_parallel_emitter_threshold)
            {
                const uint32_t num_jobs = (count + k_min_emitters_per_job - 1) / k_min_emitters_per_job;
                std::for_each(poolstl::par,
                              poolstl::iota_iter<uint32_t>(0),
                              poolstl::iota_iter<uint32_t>(num_jobs),
                              [&](uint32_t job)
                              {
                                  const uint32_t begin = job * k_min_emitters_per_job;
                                  const uint32_t end = math::min(begin + k_min_emitters_per_job, count);
                                  for(uint32_t emitter_idx = begin; emitter_idx < end; ++emitter_idx)
                                  {
                                      fill_emitter_keys(emitter_idx);
                                  }
                              });
            }
            else
            {
                for(uint32_t emitter_idx = 0; emitter_idx < count; ++emitter_idx)
                {
                    fill_emitter_keys(emitter_idx);
                }
            }
        }
        constexpr uint32_t k_radix_sort_threshold = 512;
        if(total < k_radix_sort_threshold)
        {
            APP_SCOPE_PERF("Rendering/Particle Pass SOA/Sort");
            std::sort(batched_scratch_.begin(),
                      batched_scratch_.end(),
                      [](const batched_particle& a, const batched_particle& b)
                      {
                          return a.dist > b.dist;
                      });
        }
        else
        {
            APP_SCOPE_PERF("Rendering/Particle Pass SOA/Sort Radix");
            radix_sort_desc_distances(batched_scratch_, radix_scratch_);
        }
    }

    void write_sorted_chunk(uint8_t* data, uint32_t start, uint32_t count, const emitter_handle* handles)
    {
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/Write Sorted Chunk");
        constexpr uint32_t k_parallel_write_threshold = 128;
        constexpr uint32_t k_min_rows_per_job = 128;
        const auto write_row = [&](uint32_t i)
        {
            const auto& key = batched_scratch_[start + i];
            const auto& em = emitters_[handles[key.emitter_idx].idx];
            write_instance_row(data + size_t(i) * k_instance_stride, em, key.particle_idx);
        };
        if(count < k_parallel_write_threshold)
        {
            for(uint32_t i = 0; i < count; ++i)
            {
                write_row(i);
            }
            return;
        }
        const uint32_t num_jobs = (count + k_min_rows_per_job - 1) / k_min_rows_per_job;
        std::for_each(poolstl::par,
                      poolstl::iota_iter<uint32_t>(0),
                      poolstl::iota_iter<uint32_t>(num_jobs),
                      [&](uint32_t job)
                      {
                          const uint32_t begin = job * k_min_rows_per_job;
                          const uint32_t end = math::min(begin + k_min_rows_per_job, count);
                          for(uint32_t i = begin; i < end; ++i)
                          {
                              write_row(i);
                          }
                      });
    }

    void write_direct_chunk(uint8_t* data,
                            uint32_t global_start,
                            uint32_t count,
                            const emitter_handle* handles,
                            uint32_t emitter_count)
    {
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/Write Direct Chunk");
        constexpr uint32_t k_parallel_emitter_threshold = 16;
        constexpr uint32_t k_min_emitters_per_job = 16;
        const auto write_emitter_range = [&](uint32_t emit_begin, uint32_t emit_end)
        {
            for(uint32_t emitter_idx = emit_begin; emitter_idx < emit_end; ++emitter_idx)
            {
                const uint32_t range_begin_global = prefix_scratch_[emitter_idx];
                const uint32_t range_end_global = prefix_scratch_[emitter_idx + 1];
                const uint32_t range_begin = math::max(range_begin_global, global_start);
                const uint32_t range_end = math::min(range_end_global, global_start + count);
                if(range_begin >= range_end)
                {
                    continue;
                }
                const auto& em = emitters_[handles[emitter_idx].idx];
                for(uint32_t g = range_begin; g < range_end; ++g)
                {
                    const uint32_t particle_idx = g - range_begin_global;
                    const uint32_t out_idx = g - global_start;
                    write_instance_row(data + size_t(out_idx) * k_instance_stride, em, particle_idx);
                }
            }
        };
        if(emitter_count < k_parallel_emitter_threshold)
        {
            write_emitter_range(0, emitter_count);
            return;
        }
        const uint32_t num_jobs = (emitter_count + k_min_emitters_per_job - 1) / k_min_emitters_per_job;
        std::for_each(poolstl::par,
                      poolstl::iota_iter<uint32_t>(0),
                      poolstl::iota_iter<uint32_t>(num_jobs),
                      [&](uint32_t job)
                      {
                          const uint32_t begin = job * k_min_emitters_per_job;
                          const uint32_t end = math::min(begin + k_min_emitters_per_job, emitter_count);
                          write_emitter_range(begin, end);
                      });
    }

    auto render_cpu_batch(const emitter_handle* handles,
                          uint32_t count,
                          uint8_t view,
                          bgfx::ProgramHandle program,
                          const float* view_camera,
                          const float* eye_pos_vec4,
                          const math::vec3& eye,
                          bgfx::TextureHandle texture,
                          uint64_t blend_state,
                          bool sort_by_depth) -> uint32_t
    {
        const uint32_t total = build_prefixes(handles, count);
        if(total == 0)
        {
            return 0;
        }
        if(sort_by_depth)
        {
            build_sorted(handles, count, eye, total);
        }
        bgfx::setVertexBuffer(0, quad_vbh_);
        bgfx::setIndexBuffer(quad_ibh_);
        bgfx::setState(0 | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW |
                       blend_state);
        bgfx::setTexture(0, tex_color_, texture);
        bgfx::setUniform(view_camera_, view_camera);
        bgfx::setUniform(eye_pos_, eye_pos_vec4);
        const auto write_chunk = [&](uint8_t* data, uint32_t start, uint32_t chunk_count)
        {
            if(sort_by_depth)
            {
                write_sorted_chunk(data, start, chunk_count, handles);
            }
            else
            {
                write_direct_chunk(data, start, chunk_count, handles, count);
            }
        };
        const uint32_t avail_all = bgfx::getAvailInstanceDataBuffer(total, k_instance_stride);
        if(avail_all >= total)
        {
            bgfx::InstanceDataBuffer idb{};
            bgfx::allocInstanceDataBuffer(&idb, total, k_instance_stride);
            write_chunk(idb.data, 0, total);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::submit(view, program);
            return total;
        }
        uint32_t rendered = 0;
        uint32_t offset = 0;
        while(offset < total)
        {
            const uint32_t remaining = total - offset;
            uint32_t chunk = bgfx::getAvailInstanceDataBuffer(remaining, k_instance_stride);
            if(chunk == 0)
            {
                break;
            }
            chunk = math::min(chunk, remaining);
            bgfx::InstanceDataBuffer idb{};
            bgfx::allocInstanceDataBuffer(&idb, chunk, k_instance_stride);
            write_chunk(idb.data, offset, chunk);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::submit(view, program);
            rendered += chunk;
            offset += chunk;
        }
        return rendered;
    }

    auto render_batch(const emitter_handle* handles,
                      uint32_t count,
                      uint8_t view,
                      bgfx::ProgramHandle program,
                      const float* mtx_view,
                      const math::vec3& eye,
                      bgfx::TextureHandle texture,
                      uint64_t blend_state,
                      bool sort_by_depth) -> uint32_t
    {
        if(count == 0 || !bgfx::isValid(texture))
        {
            return 0;
        }
        APP_SCOPE_PERF("Rendering/Particle Pass SOA/Render Batched Emitters");
        float view_camera[16];
        view_camera[0] = mtx_view[0];
        view_camera[1] = mtx_view[4];
        view_camera[2] = mtx_view[8];
        view_camera[3] = 0.0f;
        view_camera[4] = mtx_view[1];
        view_camera[5] = mtx_view[5];
        view_camera[6] = mtx_view[9];
        view_camera[7] = 0.0f;
        view_camera[8] = mtx_view[2];
        view_camera[9] = mtx_view[6];
        view_camera[10] = mtx_view[10];
        view_camera[11] = 0.0f;
        view_camera[12] = 0.0f;
        view_camera[13] = 0.0f;
        view_camera[14] = 0.0f;
        view_camera[15] = 1.0f;
        float eye_pos_vec4[4] = {eye.x, eye.y, eye.z, 0.0f};
        gpu_emitters_scratch_.clear();
        cpu_handles_scratch_.clear();
        for(uint32_t i = 0; i < count; ++i)
        {
            if(!is_valid(handles[i]))
            {
                continue;
            }
            emitter& em = emitters_[handles[i].idx];
            if(em.gpu_.pending_pack && em.wants_gpu_pack())
            {
                gpu_emitters_scratch_.push_back(handles[i]);
            }
            else
            {
                cpu_handles_scratch_.push_back(handles[i]);
            }
        }
        uint32_t rendered = 0;
        if(!cpu_handles_scratch_.empty())
        {
            rendered += render_cpu_batch(cpu_handles_scratch_.data(),
                                        uint32_t(cpu_handles_scratch_.size()),
                                        view,
                                        program,
                                        view_camera,
                                        eye_pos_vec4,
                                        eye,
                                        texture,
                                        blend_state,
                                        sort_by_depth);
        }
        if(!gpu_emitters_scratch_.empty())
        {
            // One compute view for the whole batch — do not allocate a pass per emitter.
            gfx::render_pass pack_pass("Particles/GPU Pack");
            for(emitter_handle handle : gpu_emitters_scratch_)
            {
                emitter& em = emitters_[handle.idx];
                if(!dispatch_gpu_pack(em, sort_by_depth, eye, pack_pass.id))
                {
                    continue;
                }
                rendered += draw_gpu_emitter(em, view, program, view_camera, eye_pos_vec4, texture, blend_state);
            }
        }
        return rendered;
    }

    bx::AllocatorI* allocator_ = nullptr;
    bx::HandleAlloc* emitter_alloc_ = nullptr;
    std::vector<emitter> emitters_;
    std::vector<batched_particle> batched_scratch_;
    std::vector<batched_particle> radix_scratch_;
    std::vector<batched_particle> gpu_sort_scratch_;
    std::vector<uint32_t> prefix_scratch_;
    std::vector<emitter_handle> gpu_emitters_scratch_;
    std::vector<emitter_handle> cpu_handles_scratch_;
    bgfx::VertexBufferHandle quad_vbh_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle quad_ibh_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle tex_color_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle view_camera_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle eye_pos_ = BGFX_INVALID_HANDLE;
};

particle_system_soa g_system;

} // namespace

void init(uint16_t max_emitters)
{
    g_system.init(max_emitters);
}

void init_gpu(rtti::context& ctx)
{
    g_system.init_gpu(ctx);
}

void shutdown()
{
    g_system.shutdown();
}

void set_default_sim_backend(particle_sim_backend backend)
{
    g_default_sim_backend = backend;
}

auto get_default_sim_backend() -> particle_sim_backend
{
    return g_default_sim_backend;
}

void set_emitter_sim_backend(emitter_handle handle, particle_sim_backend backend)
{
    g_system.set_emitter_sim_backend(handle, backend);
}

auto get_emitter_sim_backend(emitter_handle handle) -> particle_sim_backend
{
    return g_system.get_emitter_sim_backend(handle);
}

auto is_gpu_sim_available() -> bool
{
    return g_gpu_sim_available;
}

auto create_emitter(emitter_shape shape, emitter_direction direction, uint32_t max_particles) -> emitter_handle
{
    return g_system.create_emitter(shape, direction, max_particles);
}

void destroy_emitter(emitter_handle handle)
{
    g_system.destroy_emitter(handle);
}

void reset_emitter(emitter_handle handle)
{
    g_system.reset_emitter(handle);
}

void update_emitter(emitter_handle handle,
                    float dt,
                    const emitter_desc& desc,
                    emitter_transform_state& transform,
                    emitter_playback_desc& playback)
{
    g_system.update_emitter(handle, dt, desc, transform, playback);
}

auto has_updated(emitter_handle handle) -> bool
{
    return g_system.has_updated(handle);
}

void get_aabb(emitter_handle handle, math::bbox& out_aabb)
{
    g_system.get_aabb(handle, out_aabb);
}

auto get_num_particles(emitter_handle handle) -> uint32_t
{
    return g_system.get_num_particles(handle);
}

auto render_emitter_batch(const emitter_handle* handles,
                          uint32_t count,
                          uint8_t view,
                          bgfx::ProgramHandle program,
                          const float* mtx_view,
                          const math::vec3& eye,
                          bgfx::TextureHandle texture,
                          uint64_t blend_state,
                          bool sort_by_depth) -> uint32_t
{
    return g_system.render_batch(handles, count, view, program, mtx_view, eye, texture, blend_state, sort_by_depth);
}

} // namespace ps_soa
} // namespace unravel
