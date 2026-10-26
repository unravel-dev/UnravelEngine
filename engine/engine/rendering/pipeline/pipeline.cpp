#include "pipeline.h"
#include "bx/bx.h"
#include "engine/rendering/camera.h"
#include "glm/ext.hpp"
#include <engine/rendering/batch_collector.h>
#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/text_component.h>
#include <engine/ui/ecs/components/ui_document_component.h>

#include <engine/rendering/ecs/components/assao_component.h>
#include <engine/rendering/ecs/components/auto_exposure_component.h>
#include <engine/rendering/ecs/components/bloom_component.h>
#include <engine/rendering/ecs/components/fxaa_component.h>
#include <engine/rendering/ecs/components/taa_component.h>
#include <engine/rendering/ecs/components/tonemapping_component.h>
#include <engine/rendering/ecs/components/ssr_component.h>
#include <engine/rendering/ecs/components/gi_component.h>
#include <engine/rendering/ecs/components/ssil_component.h>
#include <engine/rendering/ecs/components/fog_component.h>
#include <engine/rendering/ecs/components/gtao_component.h>
#include <engine/rendering/pipeline/volume_resolver.h>
#include <engine/rendering/ecs/components/particle_emitter_component.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/shadow.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <engine/ui/ecs/systems/ui_system.h>
#include <engine/engine.h>
#include <RmlUi/Core/ElementDocument.h>

#include <graphics/format.h>
#include <graphics/graphics.h>
#include <graphics/vertex_decl.h>

#include <engine/profiler/profiler.h>
#include <concurrency/parallel.h>

#include <concurrency/concurrentqueue.h>

#include <algorithm>
#include <atomic>

namespace unravel
{
namespace rendering
{
namespace
{
/// The opaque depth soft particles fade against: this frame's Hi-Z first mip when the pipeline
/// built one, otherwise a copy of the depth buffer blitted in @p view, before the particles draw.
/// The depth buffer itself is that view's depth target, so it cannot be sampled while they draw.
auto resolve_particle_scene_depth(gfx::render_view& rview, const gfx::texture::ptr& hiz_depth, bgfx::ViewId view)
    -> bgfx::TextureHandle
{
    if(hiz_depth)
    {
        return hiz_depth->native_handle();
    }
    const auto& depth = rview.tex_find("DEPTH");
    if(!depth)
    {
        return BGFX_INVALID_HANDLE;
    }
    const auto& copy = rview.tex_acquire("PARTICLE_DEPTH",
                                         {.size = depth->get_size(),
                                          .format = bgfx::TextureFormat::D32F,
                                          .flags = BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP});
    bgfx::blit(view,
               bgfx::TextureRegion{.handle = copy->native_handle()},
               bgfx::TextureRegion{.handle = depth->native_handle()});
    return copy->native_handle();
}

/// Nearby point / spot lights a lit particle draw reads (particle_lighting.sh, PARTICLE_MAX_LIGHTS), and the
/// vec4s each takes in u_particle_lights.
constexpr size_t k_particle_max_lights = 4;
constexpr size_t k_particle_light_vec4s = k_particle_max_lights * 3;
/// SH coefficients of the sky's irradiance texture (IRRADIANCE_SH's first texels).
constexpr uint16_t k_irradiance_sh_coefficients = 9;
/// Stages of the lit particles' sky irradiance and sun shadow cascades (particle_lighting.sh).
constexpr uint8_t k_particle_irradiance_stage = 12;
/// Surface lighting's normal map.
constexpr uint8_t k_particle_normal_stage = 14;
constexpr uint8_t k_particle_shadow_stage = 8;
/// The volumetric fog's froxel column in the unlit programs (particle_fragment.sh); the lit ones hold the sun's
/// cascades there.
constexpr uint8_t k_particle_fog_volume_stage = 8;
/// Cosine a point light's cone mask passes everything at (LocalLightSpotMask).
constexpr float k_point_light_cos_outer = -2.0f;

} // namespace

/// What lit particles take from the scene for one view (particle_lighting.sh).
struct particle_scene_lighting
{
    struct local_light
    {
        /// Position, 1 / range.
        math::vec4 placement{0.0f};
        /// Axis, cos outer half angle (k_point_light_cos_outer for a point light).
        math::vec4 cone{0.0f};
        /// Linear color x candela x pre-exposure, 1 / (cos inner - cos outer half angle).
        math::vec4 intensity{0.0f};
        float range = 0.0f;
        /// Candela, for picking the brightest lights of a draw.
        float candela = 0.0f;
    };

    bgfx::TextureHandle irradiance = BGFX_INVALID_HANDLE;
    /// The direction the sun's light travels; w: its shadow cascades (0 = none read).
    math::vec4 sun_direction{0.0f, -1.0f, 0.0f, 0.0f};
    /// The sun's irradiance (linear, lux, pre-exposed); w: the stored depth offset of linear shadow maps.
    math::vec4 sun_color{0.0f};
    const shadow::shadowmap_generator* shadow = nullptr;
    float pre_exposure = 1.0f;
    std::vector<local_light> lights;
};

namespace
{

/// The sky's SH stand-in (zero) and the sun shadow's (far depth: everything lit).
void create_particle_light_fallbacks(std::shared_ptr<gfx::texture>& no_sky, std::shared_ptr<gfx::texture>& no_shadow)
{
    const std::array<float, k_irradiance_sh_coefficients * 4> zero_sh{};
    no_sky = std::make_shared<gfx::texture>(k_irradiance_sh_coefficients,
                                            uint16_t(1),
                                            false,
                                            uint16_t(1),
                                            bgfx::TextureFormat::RGBA32F,
                                            BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                            bgfx::copy(zero_sh.data(), uint32_t(sizeof(zero_sh))));
    const float far_depth = 1.0f;
    no_shadow = std::make_shared<gfx::texture>(uint16_t(1),
                                               uint16_t(1),
                                               false,
                                               uint16_t(1),
                                               bgfx::TextureFormat::R32F,
                                               BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP,
                                               bgfx::copy(&far_depth, uint32_t(sizeof(far_depth))));
}

/// The sky, the strongest directional light (with its shadow when @p use_shadow_maps) and every point / spot
/// light, as lit particles read them.
auto gather_particle_lighting(scene& scn, gfx::render_view& rview, float pre_exposure, bool use_shadow_maps)
    -> particle_scene_lighting
{
    particle_scene_lighting lighting;
    lighting.pre_exposure = pre_exposure;
    if(const auto& sky = rview.tex_find("IRRADIANCE_SH"))
    {
        lighting.irradiance = sky->native_handle();
    }
    float strongest_sun = -1.0f;
    scn.registry->view<transform_component, light_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& light_comp, auto&& active)
        {
            if(!light_comp.is_enabled())
            {
                return;
            }
            const auto& light = light_comp.get_light();
            auto world = transform_comp.get_transform_global();
            world.reset_scale();
            const math::vec3 color = light_comp.get_linear_color() * light.intensity * pre_exposure;
            if(light.type == light_type::directional)
            {
                if(light.intensity <= strongest_sun)
                {
                    return;
                }
                strongest_sun = light.intensity;
                const auto& generator = light_comp.get_shadowmap_generator();
                const bool has_shadow = use_shadow_maps && light.casts_shadows && light.shadow_params.type != sm_impl::esm &&
                                        bgfx::isValid(generator.get_rt_texture(0));
                const float stored_depth_offset = light.shadow_params.depth == sm_depth::linear ? 0.5f : 0.0f;
                lighting.sun_direction = math::vec4(world.z_unit_axis(),
                                                    has_shadow ? float(light.directional_shadow_params.num_splits) : 0.0f);
                lighting.sun_color = math::vec4(color, stored_depth_offset);
                lighting.shadow = has_shadow ? &generator : nullptr;
                return;
            }
            particle_scene_lighting::local_light local;
            local.candela = light.intensity;
            if(light.type == light_type::spot)
            {
                const float cos_inner = math::cos(math::radians(light.spot_data.get_inner_angle() * 0.5f));
                const float cos_outer = math::cos(math::radians(light.spot_data.get_outer_angle() * 0.5f));
                local.range = light.spot_data.get_range();
                local.cone = math::vec4(world.z_unit_axis(), cos_outer);
                local.intensity = math::vec4(color, 1.0f / math::max(cos_inner - cos_outer, 1.0e-4f));
            }
            else
            {
                local.range = light.point_data.range;
                local.cone = math::vec4(0.0f, 0.0f, 1.0f, k_point_light_cos_outer);
                local.intensity = math::vec4(color, 1.0f);
            }
            local.placement = math::vec4(world.get_position(), 1.0f / math::max(local.range, 1.0e-4f));
            lighting.lights.push_back(local);
        });
    return lighting;
}

/// The (up to k_particle_max_lights) brightest local lights reaching @p bounds, packed for u_particle_lights.
auto pick_particle_lights(const particle_scene_lighting& lighting,
                          const math::bbox& bounds,
                          std::array<math::vec4, k_particle_light_vec4s>& out) -> uint32_t
{
    struct candidate
    {
        size_t index;
        float score;
    };
    hpp::small_vector<candidate, 16> candidates;
    const math::vec3 center = bounds.get_center();
    for(size_t i = 0; i < lighting.lights.size(); ++i)
    {
        const auto& light = lighting.lights[i];
        const math::vec3 position(light.placement);
        const math::vec3 closest = math::clamp(position, bounds.min, bounds.max);
        if(math::distance(position, closest) > light.range)
        {
            continue;
        }
        candidates.push_back({i, light.candela / math::max(math::distance2(position, center), 1.0f)});
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const candidate& a, const candidate& b)
              {
                  return a.score > b.score;
              });
    const uint32_t count = uint32_t(math::min(candidates.size(), k_particle_max_lights));
    for(uint32_t i = 0; i < count; ++i)
    {
        const auto& light = lighting.lights[candidates[i].index];
        out[i * 3] = light.placement;
        out[i * 3 + 1] = light.cone;
        out[i * 3 + 2] = light.intensity;
    }
    return count;
}

/// Closest distance, metres, below which an emitter's screen size stops growing (the camera is inside it).
constexpr float k_min_screen_size_distance = 0.01f;
/// Fastest camera motion particles stretch against, m/s: a cut would otherwise streak them for a frame.
constexpr float k_max_stretch_camera_speed = 50.0f;

/// The camera's velocity over the last frame (m/s), capped at k_max_stretch_camera_speed.
auto get_camera_velocity(const camera& camera, delta_t dt) -> math::vec3
{
    const float seconds = dt.count();
    if(seconds <= 0.0f)
    {
        return math::vec3(0.0f);
    }
    const math::vec3 previous = math::inverse(camera.get_prev_view()).get_position();
    const math::vec3 velocity = (camera.get_position() - previous) / seconds;
    const float speed = math::length(velocity);
    return speed > k_max_stretch_camera_speed ? velocity * (k_max_stretch_camera_speed / speed) : velocity;
}

/// The normal map a surface-lit emitter's batch lights with, once it has loaded; nil otherwise (it never splits a
/// batch then).
auto get_normal_texture_uid(const particle_emitter_component& emitter) -> hpp::uuid
{
    const auto& normal_texture = emitter.get_normal_texture();
    if(ps_soa::get_capped_lighting(emitter.get_lighting()) != ps_soa::particle_lighting::surface ||
       emitter.get_blend_mode() == ps_soa::blend_mode::multiply || !normal_texture.is_valid())
    {
        return {};
    }
    // A scene loads its asset handles deferred: try_get starts the load, is_ready alone never would.
    const auto loaded = normal_texture.try_get();
    if(!loaded || asset_handle<gfx::texture>::is_empty(loaded))
    {
        return {};
    }
    return normal_texture.uid();
}

/// How large an emitter's bounds look: their radius as a fraction of the view's height.
/// Whether every corner of @p bounds is a finite number.
auto is_finite(const math::bbox& bounds) -> bool
{
    for(int axis = 0; axis < 3; ++axis)
    {
        if(!std::isfinite(bounds.min[axis]) || !std::isfinite(bounds.max[axis]))
        {
            return false;
        }
    }
    return true;
}

auto get_screen_size(const camera& camera, const math::bbox& bounds, const math::vec3& eye) -> float
{
    const float radius = math::length(bounds.get_extents());
    const float projection_scale = camera.get_projection()[1][1];
    if(camera.get_projection_mode() != projection_mode::perspective)
    {
        return radius * projection_scale;
    }
    const float distance = math::max(math::distance(eye, bounds.get_center()) - radius, k_min_screen_size_distance);
    return radius * projection_scale / distance;
}

/// The particle pass's targets: the light buffer and its depth, with the TAA reactive coverage (R8), cleared for
/// this frame, as the second color target.
auto acquire_particle_targets(gfx::render_view& rview, const gfx::frame_buffer::ptr& lbuffer_depth)
    -> gfx::frame_buffer::ptr
{
    const auto& color = lbuffer_depth->get_texture(0);
    const auto& depth = lbuffer_depth->get_texture(1);
    const auto& reactive = rview.tex_acquire(
        "PARTICLE_REACTIVE",
        {.size = lbuffer_depth->get_size(), .format = bgfx::TextureFormat::R8, .flags = BGFX_TEXTURE_RT});
    auto& reactive_fbo = rview.fbo_acquire("PARTICLE_REACTIVE");
    if(gfx::needs_recreate(reactive_fbo, {reactive}))
    {
        reactive_fbo.reset();
        reactive_fbo = std::make_shared<gfx::frame_buffer>();
        reactive_fbo->populate({reactive});
    }
    gfx::render_pass clear_pass("Particles/Reactive Clear");
    clear_pass.bind(reactive_fbo.get());
    clear_pass.clear(BGFX_CLEAR_COLOR, 0x00000000);
    auto& fbo = rview.fbo_acquire("LBUFFER_PARTICLES");
    if(gfx::needs_recreate(fbo, {color, reactive, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({color, reactive, depth});
    }
    return fbo;
}

/// The overdraw debug view's target: the particle layers per pixel (R16F, cleared for this frame) over the scene's
/// depth.
auto acquire_particle_overdraw_target(gfx::render_view& rview, const gfx::frame_buffer::ptr& lbuffer_depth)
    -> gfx::frame_buffer::ptr
{
    const auto& depth = lbuffer_depth->get_texture(1);
    const auto& overdraw = rview.tex_acquire(
        "PARTICLE_OVERDRAW",
        {.size = lbuffer_depth->get_size(), .format = bgfx::TextureFormat::R16F, .flags = BGFX_TEXTURE_RT});
    auto& fbo = rview.fbo_acquire("PARTICLE_OVERDRAW");
    if(gfx::needs_recreate(fbo, {overdraw, depth}))
    {
        fbo.reset();
        fbo = std::make_shared<gfx::frame_buffer>();
        fbo->populate({overdraw, depth});
    }
    gfx::render_pass clear_pass("Particles/Overdraw Clear");
    clear_pass.bind(fbo.get());
    clear_pass.clear(BGFX_CLEAR_COLOR, 0x00000000);
    return fbo;
}

/**
 * @brief What orders and batches one emitter's particle draw.
 *
 * Emitters draw by sort priority, then back to front by distance, whatever their blend mode: additive and
 * alpha-blended layers only composite correctly against each other in depth order (fire in front of smoke adds over
 * it, smoke in front of fire hides it). The material key only breaks ties, so neighbouring emitters of one material
 * coalesce into one draw, whose particles sort together.
 */
struct particle_draw_key
{
    particle_emitter_component* component;
    ps_soa::blend_mode blend_mode;
    ps_soa::texture_mode texture_mode;
    ps_soa::sort_mode sort_mode;
    ps_soa::particle_primitive primitive;
    ps_soa::particle_sort_backend backend;
    hpp::uuid texture_uid;
    /// Surface-lit emitters: their normal map (nil without one, and for every other lighting).
    hpp::uuid normal_texture_uid;
    float distance;
    int32_t sort_priority;
    /// Drawn by the half-resolution pass (never for multiply blend).
    bool is_half_resolution;
    /// Also drawn by the velocity pass.
    bool writes_velocity;
    /// Screen size (get_screen_size) and live particles: the budget keeps the largest emitters.
    float significance;
    uint32_t particle_count;
    ps_soa::particle_lighting lighting;
};
using particle_draw_keys = hpp::small_vector<particle_draw_key, 16>;
using particle_batch_list = hpp::small_vector<ps_soa::render_batch, 16>;

/// A view's particle draws: the full-resolution, half-resolution and velocity pass batches.
struct particle_batch_lists
{
    particle_batch_list full;
    particle_batch_list half;
    particle_batch_list velocity;
    bool has_lit = false;
};

/// The geometry an emitter draws: its primitive, or the strip its trail or ribbon mode needs.
auto get_drawn_primitive(const particle_emitter_component& emitter) -> ps_soa::particle_primitive
{
    switch(emitter.get_render_mode())
    {
        case ps_soa::render_mode::trail:
            return ps_soa::particle_primitive::trail;
        case ps_soa::render_mode::ribbon:
            return ps_soa::particle_primitive::ribbon;
        default:
            return emitter.get_primitive();
    }
}

auto make_particle_draw_key(particle_emitter_component& emitter,
                            float distance,
                            float significance,
                            uint32_t particle_count,
                            const ps_soa::particle_quality& quality) -> particle_draw_key
{
    const bool is_multiply = emitter.get_blend_mode() == ps_soa::blend_mode::multiply;
    return {&emitter,
            emitter.get_blend_mode(),
            emitter.get_texture_mode(),
            emitter.get_sort_mode(),
            get_drawn_primitive(emitter),
            ps_soa::get_emitter_sort_backend(emitter.get_emitter_handle()),
            emitter.get_drawn_texture().uid(),
            get_normal_texture_uid(emitter),
            distance,
            emitter.get_sort_priority(),
            (emitter.get_half_resolution() || quality.force_half_resolution) && !is_multiply,
            emitter.get_write_velocity(),
            significance,
            particle_count,
            // Multiply blend filters the scene: it stays unlit.
            is_multiply ? ps_soa::particle_lighting::unlit : ps_soa::get_capped_lighting(emitter.get_lighting())};
}

/// The view's particle budget: the emitters largest on screen are drawn while they fit. The largest is always
/// drawn, and one that does not fit leaves room for smaller ones after it.
void apply_particle_budget(particle_draw_keys& emitters, uint32_t budget, pipeline_stats& stats)
{
    if(budget == 0)
    {
        return;
    }
    std::stable_sort(emitters.begin(),
                     emitters.end(),
                     [](const particle_draw_key& a, const particle_draw_key& b)
                     {
                         return a.significance > b.significance;
                     });
    uint64_t spent = 0;
    particle_draw_keys kept;
    for(const particle_draw_key& emitter : emitters)
    {
        if(kept.empty() || spent + emitter.particle_count <= budget)
        {
            spent += emitter.particle_count;
            kept.push_back(emitter);
            continue;
        }
        stats.skipped_particle_emitters++;
        stats.skipped_particles += emitter.particle_count;
    }
    emitters = std::move(kept);
}

/// Draw order: sort priority, then back to front, then the material (so neighbours of one material batch).
void sort_particle_draw_keys(particle_draw_keys& emitters)
{
    std::sort(emitters.begin(),
              emitters.end(),
              [](const particle_draw_key& a, const particle_draw_key& b)
              {
                  if(a.sort_priority != b.sort_priority)
                  {
                      return a.sort_priority < b.sort_priority;
                  }
                  if(a.distance != b.distance)
                  {
                      // Back to front: farther first.
                      return a.distance > b.distance;
                  }
                  if(a.blend_mode != b.blend_mode)
                  {
                      return a.blend_mode < b.blend_mode;
                  }
                  if(a.texture_uid != b.texture_uid)
                  {
                      return a.texture_uid < b.texture_uid;
                  }
                  return a.texture_mode < b.texture_mode;
              });
}

/// Whether @p next draws in the batch @p open started: same texture, texture mode, sort mode, primitive, backend
/// and sort priority (a change of any must not reorder what draws first); the color passes also need the same blend,
/// resolution and no lighting (lit emitters pick their lights where they are, so they draw on their own). A ribbon
/// threads one emitter's particles: it never shares its draw.
auto is_same_particle_batch(const particle_draw_key& open, const particle_draw_key& next, bool is_velocity) -> bool
{
    if(next.primitive == ps_soa::particle_primitive::ribbon)
    {
        return false;
    }
    const bool has_same_geometry = open.texture_uid == next.texture_uid &&
                                   open.normal_texture_uid == next.normal_texture_uid &&
                                   open.texture_mode == next.texture_mode && open.sort_mode == next.sort_mode &&
                                   open.primitive == next.primitive && open.backend == next.backend &&
                                   open.sort_priority == next.sort_priority;
    if(!has_same_geometry || is_velocity)
    {
        return has_same_geometry;
    }
    return open.blend_mode == next.blend_mode && open.is_half_resolution == next.is_half_resolution &&
           open.lighting == next.lighting && next.lighting == ps_soa::particle_lighting::unlit;
}

/// The batch of the run of @p count emitters from @p first in @p handles that @p key opened.
auto make_particle_batch(const particle_draw_key& key,
                         const std::vector<ps_soa::emitter_handle>& handles,
                         uint32_t first,
                         uint32_t count,
                         bool is_velocity) -> ps_soa::render_batch
{
    ps_soa::render_batch batch;
    batch.emitters = handles.data() + first;
    batch.count = count;
    batch.texture = key.component->get_drawn_texture().get()->native_handle();
    batch.texture_mode = key.texture_mode;
    batch.blend_mode = key.blend_mode;
    batch.primitive = key.primitive;
    batch.lighting = is_velocity ? ps_soa::particle_lighting::unlit : key.lighting;
    if(batch.lighting == ps_soa::particle_lighting::surface && !key.normal_texture_uid.is_nil())
    {
        const auto normal_texture = key.component->get_normal_texture().get();
        batch.normal_texture = normal_texture->native_handle();
        batch.is_normal_texture_two_channel = gfx::normal_map_needs_z_reconstruction(normal_texture->info.format);
    }
    return batch;
}

/**
 * @brief The view's draws: one batch per run of neighbouring emitters that may share a draw (is_same_particle_batch),
 * for the color passes and again for the velocity pass. @p handles holds the batches' emitters.
 */
auto build_particle_batches(const particle_draw_keys& emitters, std::vector<ps_soa::emitter_handle>& handles)
    -> particle_batch_lists
{
    struct batch_range
    {
        uint32_t first = 0;
        uint32_t count = 0;
        const particle_draw_key* key = nullptr;
        bool is_velocity = false;
    };
    handles.clear();
    hpp::small_vector<batch_range, 16> ranges;
    for(const bool is_velocity : {false, true})
    {
        const particle_draw_key* open = nullptr;
        for(const auto& emitter : emitters)
        {
            const auto handle = emitter.component->get_emitter_handle();
            const bool is_drawn = !is_velocity || emitter.writes_velocity;
            if(!is_drawn || !ps_soa::is_valid(handle) || !emitter.component->get_drawn_texture().is_valid())
            {
                continue;
            }
            if(open == nullptr || !is_same_particle_batch(*open, emitter, is_velocity))
            {
                ranges.push_back({uint32_t(handles.size()), 0, &emitter, is_velocity});
                open = &emitter;
            }
            handles.push_back(handle);
            ranges.back().count++;
        }
    }
    // The handles are all in: the batches point into their final storage.
    particle_batch_lists lists;
    for(const auto& range : ranges)
    {
        const ps_soa::render_batch batch = make_particle_batch(*range.key, handles, range.first, range.count, range.is_velocity);
        lists.has_lit = lists.has_lit || batch.lighting != ps_soa::particle_lighting::unlit;
        auto& list = range.is_velocity ? lists.velocity : range.key->is_half_resolution ? lists.half : lists.full;
        list.push_back(batch);
    }
    return lists;
}

/// Counts @p emitter's particles as active and, when it advanced, simulated.
void count_particle_emitter(const particle_emitter_component& emitter, uint32_t particle_count, pipeline_stats& stats)
{
    stats.active_particle_emitters++;
    stats.active_particles += particle_count;
    if(emitter.is_simulated())
    {
        stats.simulated_particle_emitters++;
        stats.simulated_particles += particle_count;
    }
}

/// Starts the particle counts of a view over.
void reset_particle_stats(pipeline_stats& stats)
{
    stats.active_particle_emitters = 0;
    stats.simulated_particle_emitters = 0;
    stats.drawn_particle_emitters = 0;
    stats.active_particles = 0;
    stats.simulated_particles = 0;
    stats.drawn_particles = 0;
    stats.drawn_particles_batches = 0;
    stats.skipped_particle_emitters = 0;
    stats.skipped_particles = 0;
    stats.sorted_particle_keys = 0;
}

/**
 * @brief The quad emitters the view draws, by the draw key: enabled, on a rendered layer, in the frustum (their
 * stretch against the camera included), within their max draw distance, large enough on screen and within the budget.
 * Mesh emitters only count (the scene's geometry draws them). Every drawn or in-view emitter is marked rendered
 * (renderer-based culling keeps it running).
 */
auto gather_particle_emitters(scene& scn,
                              const camera& camera,
                              float camera_speed,
                              const layer_mask& render_mask,
                              pipeline_stats& stats) -> particle_draw_keys
{
    APP_SCOPE_PERF("Rendering/Particle Pass/Cull Emitters");
    const auto eye = camera.get_position();
    const ps_soa::particle_quality& quality = ps_soa::get_quality();
    const auto frame = uint64_t(gfx::get_render_frame());
    particle_draw_keys emitters;
    scn.registry->view<transform_component, particle_emitter_component, active_component>().each(
        [&](auto e, auto&& /*transform_comp*/, auto&& emitter, auto&& /*active*/)
        {
            if(!emitter.is_enabled() || !pipeline::is_on_rendered_layer(*scn.registry, e, render_mask))
            {
                return;
            }
            const uint32_t particle_count = emitter.get_num_particles();
            count_particle_emitter(emitter, particle_count, stats);
            if(emitter.get_render_mode() == ps_soa::render_mode::mesh)
            {
                // Mesh particles are drawn with the scene's geometry (the deferred pipeline).
                if(emitter.is_mesh_render() && camera.test_aabb(emitter.get_mesh_bounds()))
                {
                    stats.drawn_particle_emitters++;
                    stats.drawn_particles += particle_count;
                }
                return;
            }
            math::bbox bounds = emitter.get_world_bounds();
            const auto handle = emitter.get_emitter_handle();
            if(ps_soa::is_valid(handle))
            {
                bounds.inflate(ps_soa::get_camera_stretch_reach(handle, camera_speed));
            }
            // A degenerate emitter (a non-finite transform) has nothing to draw and would break the sorts.
            if(!is_finite(bounds) || !camera.test_aabb(bounds))
            {
                return;
            }
            // Beyond its max draw distance, or smaller on screen than the project allows, an emitter is not drawn,
            // and renderer-based culling freezes it.
            const float max_draw_distance = emitter.get_max_draw_distance();
            const float significance = get_screen_size(camera, bounds, eye);
            if((max_draw_distance > 0.0f && math::distance(eye, math::clamp(eye, bounds.min, bounds.max)) > max_draw_distance) ||
               (quality.min_screen_size > 0.0f && significance < quality.min_screen_size))
            {
                stats.skipped_particle_emitters++;
                stats.skipped_particles += particle_count;
                return;
            }
            if(particle_count == 0)
            {
                // Nothing to draw yet, but in view: renderer-based culling keeps it running.
                emitter.set_last_render_frame(frame);
                return;
            }
            const float distance = math::distance2(bounds.get_center(), eye);
            emitters.push_back(make_particle_draw_key(emitter, distance, significance, particle_count, quality));
        });
    apply_particle_budget(emitters, quality.max_drawn_particles, stats);
    for(const auto& emitter : emitters)
    {
        // Renderer-based culling feedback (same as model_component).
        emitter.component->set_last_render_frame(frame);
    }
    return emitters;
}

/// Whether any of the view's emitters fades softly into the scene (the pass then needs the scene's depth).
auto has_soft_particles(const particle_draw_keys& emitters) -> bool
{
    return std::any_of(emitters.begin(),
                       emitters.end(),
                       [](const particle_draw_key& emitter)
                       {
                           return emitter.component->get_soft_fade_distance() > 0.0f;
                       });
}

} // namespace
auto pipeline::init(rtti::context& ctx) -> bool
{
    cache_entity_ = cache_registry_.create();

    prefilter_pass_.init(ctx);
    blit_pass_.init(ctx);
    upscale_pass_.init(ctx);
    atmospheric_pass_perez_.init(ctx);
    atmospheric_pass_skybox_.init(ctx);
    auto_exposure_pass_.init(ctx);
    bloom_pass_.init(ctx);
    fxaa_pass_.init(ctx);
    // Before the particle programs: they read the fog's uniforms, which it creates.
    fog_pass_.init(ctx);
    taa_pass_.init(ctx);
    tonemapping_pass_.init(ctx);
    assao_pass_.init(ctx);
    ssr_pass_.init(ctx);
    hiz_pass_.init(ctx);
    ssil_pass_.init(ctx);
    gtao_pass_.init(ctx);
    gi_clipmap_compose_pass_.init(ctx);
    temporal_probe_pass_.init(ctx);
    gi_gather_pass_.init(ctx);
    gi_surface_cache_pass_.init(ctx);
    gi_visualize_pass_.init(ctx);
    shader_print_.init(ctx);
    gi_reflection_pass_.init(ctx);
    scene_history_pass_.init(ctx);
    particle_half_res_pass_.init(ctx);

    auto& am = ctx.get_cached<asset_manager>();

    auto load_program = [&](const std::string& vs, const std::string& fs)
    {
        auto vs_shader = am.get_asset<gfx::shader>("engine:/data/shaders/" + vs + ".sc");
        auto fs_shadfer = am.get_asset<gfx::shader>("engine:/data/shaders/" + fs + ".sc");

        return std::make_unique<gpu_program>(vs_shader, fs_shadfer);
    };

    // Uniforms before programs (GL uniform order contract, gpu_program.h).
    particle_pre_exposure_uniform_ = std::make_shared<gfx::uniform>("u_pre_exposure", bgfx::UniformType::Vec4);
    particle_program_ = load_program("particles/vs_particle", "particles/fs_particle");
    particle_program_mask_ = load_program("particles/vs_particle", "particles/fs_particle_mask");
    particle_prev_view_proj_uniform_ = std::make_shared<gfx::uniform>("u_prev_view_proj", bgfx::UniformType::Mat4);
    particle_sun_direction_uniform_ = std::make_shared<gfx::uniform>("u_particle_sun_direction", bgfx::UniformType::Vec4);
    particle_sun_color_uniform_ = std::make_shared<gfx::uniform>("u_particle_sun_color", bgfx::UniformType::Vec4);
    particle_lighting_uniform_ = std::make_shared<gfx::uniform>("u_particle_lighting", bgfx::UniformType::Vec4);
    particle_lights_uniform_ =
        std::make_shared<gfx::uniform>("u_particle_lights", bgfx::UniformType::Vec4, uint16_t(k_particle_light_vec4s));
    particle_irradiance_sampler_ = std::make_shared<gfx::uniform>("s_particle_irradiance", bgfx::UniformType::Sampler);
    particle_normal_sampler_ = std::make_shared<gfx::uniform>("s_particle_normal", bgfx::UniformType::Sampler);
    for(size_t i = 0; i < particle_shadow_samplers_.size(); ++i)
    {
        particle_shadow_samplers_[i] =
            std::make_shared<gfx::uniform>("s_shadowMap" + std::to_string(i), bgfx::UniformType::Sampler);
    }
    particle_program_lit_ = load_program("particles/vs_particle_lit", "particles/fs_particle_lit");
    particle_program_lit_mask_ = load_program("particles/vs_particle_lit", "particles/fs_particle_mask_lit");
    create_particle_light_fallbacks(particle_no_sky_, particle_no_shadow_);
    particle_velocity_program_ = load_program("particles/vs_particle_velocity", "particles/fs_particle_velocity");
    particle_overdraw_program_ = load_program("particles/vs_particle", "particles/fs_particle_overdraw");
    world_quad_program_ = load_program("rmlui_world/vs_world_quad", "rmlui_world/fs_world_quad");

    return true;
}

namespace
{
// LOD levels added on top of the main-view selection when rendering shadow maps.
std::atomic<float> shadow_lod_bias{1.0f};
} // namespace

auto pipeline::get_shadow_lod_bias() -> float
{
    return shadow_lod_bias.load(std::memory_order_relaxed);
}

void pipeline::set_shadow_lod_bias(float bias)
{
    shadow_lod_bias.store(bias, std::memory_order_relaxed);
}

void pipeline::gather_visible_models(scene& scn,
    const camera* cam, 
    visibility_flags query, 
    const layer_mask& render_mask, 
    delta_t dt,
    const std::function<void(entt::handle entity, const lod_data& lod_data)>& lod_data_callback,
    const camera* lod_reference_cam)
{
    
    APP_SCOPE_PERF(cam ? "Rendering/Cull Models  " : "Rendering/Gather Models");
    static const std::string thread_name = "Rendering/Gather Models Thread";
    tpp::this_thread::register_this_thread(thread_name, true);
    auto view = scn.registry->view<transform_component, model_component, layer_component, active_component>();

    if(cam)
    {
        // first force get the frustum to ensure it is up to date. it is internally cached and we don't want
        // to updated it in the loop since it is not thread safe. Force it here.
        auto& frustum = cam->get_frustum();
        BX_UNUSED(frustum);
    }

    // Dispatched over the view's LEADING POOL (for_each_entity_par): its packed entity array is
    // random access, so the pool carves chunks in O(1), and the view's filter - the three
    // membership tests its forward-only iterator ran serially per step - runs inside each task.
    // One result slot per packed position, sized once per call and reused across calls.
    const auto* leading = view.handle();
    if(leading == nullptr)
    {
        return;
    }
    const size_t slot_count = leading->size();
    cull_lod_data_.resize(slot_count);
    cull_visible_.assign(slot_count, 0u);
    const uint64_t frame = gfx::get_render_frame();

    // get_lod_data_for_camera is not thread safe, but each entity is visited by exactly one
    // task, so the per-entity state is never shared. Results land in per-entity slots and are
    // emitted below in pool order: the visibility set is deterministic frame to frame (the
    // concurrent queue this replaced returned them in whatever order the pool threads finished)
    // and nothing is allocated per call.
    poolstl::for_each_entity_par(true,
        view,
        [&](const entt::entity entity)
        {
            tpp::this_thread::register_this_thread(thread_name, true);
            const size_t index = leading->index(entity);

            auto&& [transform_comp, model_comp, layer_comp, active_comp] = view.get(entity);
            
            // Get layer component if it exists, otherwise use default layer
            auto entity_layer = layer_comp.layers;
            
            // Layer filtering - check if entity's layer matches camera's render mask
            if((entity_layer.mask & render_mask.mask) == 0)
            {
                return; // Entity's layer is not visible to this camera
            }

            // Early exit checks
            if(!model_comp.is_enabled()) 
            {
                return;
            }
            if((query & visibility_query::is_static) && !model_comp.is_static()) 
            {
                return;
            }
            if((query & visibility_query::is_shadow_caster) && !model_comp.casts_shadow()) 
            {
                return;
            }
            
            auto& current_lod_data = model_comp.get_lod_data_for_camera(cam, frame);
            bool is_visible = true;

            if(cam)
            {
                const auto& model = model_comp.get_model();


                if(!model.is_valid())
                {
                    return;
                }

                // LOD and culling both measure the pose-aware world AABB (static bounds
                // unioned with the cached per-submesh/skinned pose bounds) - one source of
                // truth for "where the rendered geometry actually is". Refreshed by
                // model_system::on_frame_before_render, which runs before any render path.
                const auto& world_bounds = model_comp.get_world_bounds();

                if(!model.calculate_lod_data(current_lod_data, world_bounds, *cam, dt.count()))
                {
                    return;
                }

                // Bind-pose local bounds are never used for culling or LOD - they don't
                // track node/bone animation.
                is_visible = cam->get_frustum().test_aabb(world_bounds);
            }
            else if(lod_reference_cam)
            {
                // No frustum culling (e.g. shadow gathering renders casters outside the view),
                // but select a distance-appropriate LOD from the reference camera plus the
                // shadow bias instead of always rendering LOD 0.
                const auto& model = model_comp.get_model();

                if(model.is_valid())
                {
                    const auto lod = model.compute_lod_index(model_comp.get_world_bounds(),
                                                             *lod_reference_cam,
                                                             get_shadow_lod_bias());
                    current_lod_data.current_lod_index = lod;
                    current_lod_data.target_lod_index = lod;
                    current_lod_data.current_time = 0.0f;
                    current_lod_data.transition_time = 0.0f;
                }
            }

            if(is_visible)
            {
                cull_lod_data_[index] = current_lod_data;
                cull_visible_[index] = 1u;
            }
        });

    // Nothing touches the pool between the dispatch and here, so a slot's packed position still
    // names its entity.
    const entt::entity* packed = leading->data();
    for(size_t index = 0; index < slot_count; ++index)
    {
        if(cull_visible_[index] != 0u)
        {
            lod_data_callback(scn.create_handle(packed[index]), cull_lod_data_[index]);
        }
    }
}


auto pipeline::create_run_params(entt::handle camera_ent) const -> rendering::pipeline::run_params
{
    rendering::pipeline::run_params params;

    if(auto assao_comp = camera_ent.try_get<assao_component>(); assao_comp && assao_comp->enabled)
    {
        params.fill_assao_params = [camera_ent](assao_pass::run_params& params)
        {
            if(auto assao_comp = camera_ent.try_get<assao_component>())
            {
                params.params = assao_comp->settings;
            }
        };
    }
    
    if(auto ae_comp = camera_ent.try_get<auto_exposure_component>(); ae_comp && ae_comp->enabled)
    {
        params.fill_auto_exposure_params = [camera_ent](auto_exposure_pass::run_params& params)
        {
            if(auto ae_comp = camera_ent.try_get<auto_exposure_component>())
            {
                params.config = ae_comp->settings;
            }
        };
    }
    if(auto bloom_comp = camera_ent.try_get<bloom_component>(); bloom_comp && bloom_comp->enabled)
    {
        params.fill_bloom_params = [camera_ent](bloom_pass::run_params& params)
        {
            if(auto bloom_comp = camera_ent.try_get<bloom_component>())
            {
                params.config = bloom_comp->settings;
            }
        };
    }
    if(auto tonemapping_comp = camera_ent.try_get<tonemapping_component>(); tonemapping_comp && tonemapping_comp->enabled)
    {
        params.fill_hdr_params = [camera_ent](tonemapping_pass::run_params& params)
        {
            if(auto tonemapping_comp = camera_ent.try_get<tonemapping_component>())
            {
                params.config = tonemapping_comp->settings;
            }
        };
    }
    else
    {
        // Always set up tonemapping params but with disabled method if component is disabled
        params.fill_hdr_params = [camera_ent](tonemapping_pass::run_params& params)
        {
            params.config.method = tonemapping_method::none;
        };
    }
    
    if(auto taa_comp = camera_ent.try_get<taa_component>(); taa_comp && taa_comp->enabled)
    {
        params.fill_taa_params = [camera_ent](taa_pass::run_params& p) -> void
        {
            if(auto c = camera_ent.try_get<taa_component>())
            {
                p.config = c->settings;
            }
        };
        params.apply_taa_params = [camera_ent](camera& cam, const usize32_t& viewport_size) -> void
        {
            if(auto c = camera_ent.try_get<taa_component>())
            {
                const std::uint32_t sample_count = (std::min)(std::uint32_t(16),
                                                                (std::max)(std::uint32_t(2), c->settings.temporal_sample_count));
                cam.set_aa_data(viewport_size,
                                  static_cast<std::uint32_t>(gfx::get_render_frame()),
                                  sample_count,
                                  c->settings.jitter_amplitude);
            }
        };
    }

    if(auto fxaa_comp = camera_ent.try_get<fxaa_component>(); fxaa_comp && fxaa_comp->enabled && !params.fill_taa_params)
    {
        params.fill_fxaa_params = [camera_ent](fxaa_pass::run_params& params)
        {
            if(auto fxaa_comp = camera_ent.try_get<fxaa_component>())
            {
                // Fill FXAA parameters
            }
        };
    }
    
    if(auto ssr_comp = camera_ent.try_get<ssr_component>(); ssr_comp && ssr_comp->enabled)
    {
        params.fill_ssr_params = [camera_ent](ssr_pass::run_params& params)
        {
            if(auto ssr_comp = camera_ent.try_get<ssr_component>())
            {
                params.settings = ssr_comp->settings;
            }
        };
    }

    if(auto ssil_comp = camera_ent.try_get<ssil_component>(); ssil_comp && ssil_comp->enabled)
    {
        params.fill_ssil_params = [camera_ent](ssil_pass::run_params& params)
        {
            if(auto ssil_comp = camera_ent.try_get<ssil_component>())
            {
                params.settings = ssil_comp->settings;
            }
        };
    }

    if(auto gtao_comp = camera_ent.try_get<gtao_component>(); gtao_comp && gtao_comp->enabled)
    {
        params.fill_gtao_params = [camera_ent](gtao_pass::run_params& params)
        {
            if(auto gtao_comp = camera_ent.try_get<gtao_component>())
            {
                params.config = gtao_comp->settings;
            }
        };
    }

    if(auto gi_comp = camera_ent.try_get<gi_component>(); gi_comp && gi_comp->enabled)
    {
        params.fill_gi_params = [camera_ent](gi_settings& gi)
        {
            if(auto gi_comp = camera_ent.try_get<gi_component>())
            {
                gi = gi_comp->settings;
            }
        };
    }

    if(auto fog_comp = camera_ent.try_get<fog_component>(); fog_comp && fog_comp->enabled)
    {
        params.fill_fog_params = [camera_ent](fog_pass::run_params& params)
        {
            if(auto fog_comp = camera_ent.try_get<fog_component>())
            {
                params.config = fog_comp->settings;
            }
        };
    }

    return params;
}

auto pipeline::create_run_params(entt::handle camera_ent, scene* scn, const camera* cam) const -> rendering::pipeline::run_params
{
    auto params = create_run_params(camera_ent);
    if(!scn || !cam)
    {
        return params;
    }
    auto resolved = resolve_post_process_volumes(*scn, cam->get_position(), camera_ent);
    const bool has_any_volume = resolved.has_auto_exposure || resolved.has_bloom || resolved.has_tonemapping ||
                                resolved.has_fxaa || resolved.has_taa || resolved.has_ssr || resolved.has_assao || resolved.has_ssil || resolved.has_gtao ||
                                resolved.has_gi || resolved.has_fog;
    if(!has_any_volume)
    {
        return params;
    }
    if(resolved.has_auto_exposure)
    {
        auto_exposure_pass::settings s = resolved.auto_exposure;
        params.fill_auto_exposure_params = [s](auto_exposure_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_bloom)
    {
        bloom_pass::settings s = resolved.bloom;
        params.fill_bloom_params = [s](bloom_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_tonemapping)
    {
        tonemapping_pass::settings s = resolved.tonemapping;
        params.fill_hdr_params = [s](tonemapping_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_fxaa)
    {
        params.fill_fxaa_params = [](fxaa_pass::run_params&) {};
    }
    if(resolved.has_taa)
    {
        taa_pass::settings s = resolved.taa;
        params.fill_taa_params = [s](taa_pass::run_params& p) -> void { p.config = s; };
        params.apply_taa_params = [s](camera& cam, const usize32_t& viewport_size) -> void
        {
            const std::uint32_t sample_count =
                (std::min)(std::uint32_t(16), (std::max)(std::uint32_t(2), s.temporal_sample_count));
            cam.set_aa_data(viewport_size,
                              static_cast<std::uint32_t>(gfx::get_render_frame()),
                              sample_count,
                              s.jitter_amplitude);
        };
    }
    if(resolved.has_ssr)
    {
        ssr_pass::ssr_settings s = resolved.ssr;
        params.fill_ssr_params = [s](ssr_pass::run_params& p) { p.settings = s; };
    }
    if(resolved.has_assao)
    {
        assao_pass::settings s = resolved.assao;
        params.fill_assao_params = [s](assao_pass::run_params& p) { p.params = s; };
    }
    if(resolved.has_ssil)
    {
        ssil_pass::ssil_settings s = resolved.ssil;
        params.fill_ssil_params = [s](ssil_pass::run_params& p) { p.settings = s; };
    }
    if(resolved.has_gtao)
    {
        gtao_pass::settings s = resolved.gtao;
        params.fill_gtao_params = [s](gtao_pass::run_params& p) { p.config = s; };
    }
    if(resolved.has_gi)
    {
        gi_settings s = resolved.gi;
        params.fill_gi_params = [s](gi_settings& gi)
        {
            gi = s;
        };
    }
    if(resolved.has_fog)
    {
        fog_pass::settings s = resolved.fog;
        params.fill_fog_params = [s](fog_pass::run_params& p) { p.config = s; };
    }
    if(params.fill_taa_params)
    {
        params.fill_fxaa_params = {};
    }
    return params;
}

void pipeline::apply_feature_limits(run_params& params, const feature_limits& limits)
{
    params.limits = limits;
    if(!limits.global_illumination)
    {
        params.fill_gi_params = {};
    }
    if(!limits.screen_space_reflections)
    {
        params.fill_ssr_params = {};
    }
    if(!limits.screen_space_indirect_lighting)
    {
        params.fill_ssil_params = {};
    }
    if(!limits.ambient_occlusion)
    {
        params.fill_gtao_params = {};
        params.fill_assao_params = {};
    }
    if(!limits.bloom)
    {
        params.fill_bloom_params = {};
    }
    if(limits.max_anti_aliasing != anti_aliasing_limit::temporal && params.fill_taa_params)
    {
        params.fill_taa_params = {};
        params.apply_taa_params = {};
        params.fill_fxaa_params = [](fxaa_pass::run_params&) {};
    }
    if(limits.max_anti_aliasing == anti_aliasing_limit::none)
    {
        params.fill_fxaa_params = {};
    }
}

void pipeline::run_ui_pass(scene& scn, const camera& camera, gfx::render_view& rview, const gfx::frame_buffer::ptr& output)
{
    APP_SCOPE_PERF("Rendering/3D Text Pass");

    const auto& view = camera.get_view();
    // This pass draws into the final image after temporal AA has resolved the frame, so
    // nothing removes the subpixel jitter afterwards: a jittered projection would shift
    // every document and text by up to half a pixel each frame.
    const auto proj = camera.get_projection_unjittered();

    // Documents drawn straight into the output draw in views of their own: what follows them draws in
    // a view started after theirs, so the order stays far to near.
    const auto start_ui_view = [&]() -> bgfx::ViewId
    {
        gfx::render_pass pass("World UI/Elements Pass");
        pass.bind(output.get());
        pass.set_view_proj(view, proj);
        return pass.id;
    };
    bgfx::ViewId ui_view_id = start_ui_view();

    struct world_space_ui_entry
    {
        entt::handle handle;
        ui_document_component* comp = nullptr;
        transform_component* transform_comp = nullptr;
        float distance = 0.0f;
    };
    struct world_space_ui_cache
    {
        std::vector<world_space_ui_entry> entries;
    };
    auto& ui_cache = cache_registry_.get_or_emplace<world_space_ui_cache>(cache_entity_);
    ui_cache.entries.clear();

    // World-space UI documents: draw quad with framebuffer texture
    if(world_quad_program_ && world_quad_program_->begin())
    {
        // Premultiplied blend: the document texture holds premultiplied alpha, and blending it
        // by source alpha again would darken every partly transparent pixel.
        constexpr uint64_t world_ui_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                                            BGFX_STATE_DEPTH_TEST_LESS |
                                            BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

        scn.registry->view<transform_component, ui_document_component, active_component>().each(
            [&](auto e, auto&& transform_comp, auto&& ui_comp, auto&&)
            {
                if(ui_comp.render_mode != ui_render_mode::world_space)
                {
                    return;
                }
                if(!ui_comp.is_enabled() || !ui_comp.document || !ui_comp.document->IsVisible())
                {
                    return;
                }
                if(!ui_comp.draws_into_scene() && !ui_comp.framebuffer)
                {
                    return;
                }

                const auto& world_transform = transform_comp.get_transform_global();
                auto bbox = ui_comp.get_bounds();
                if(!camera.test_obb(bbox, world_transform))
                {
                    return;
                }
                auto handle = scn.create_handle(e);
                float dist = math::distance2(camera.get_position(), world_transform.get_position());
                ui_cache.entries.push_back({handle, &ui_comp, &transform_comp, dist});
            });

        std::sort(ui_cache.entries.begin(), ui_cache.entries.end(), [](const world_space_ui_entry& a, const world_space_ui_entry& b)
        {
            return a.distance > b.distance;
        });

        auto& ui = engine::context().get_cached<ui_system>();
        const math::mat4 world_to_clip = proj.get_matrix() * view.get_matrix();
        for(const auto& entry : ui_cache.entries)
        {
            auto& ui_comp = *entry.comp;
            auto handle = entry.handle;
            const auto& world_transform = entry.transform_comp->get_transform_global();
            if(ui_comp.draws_into_scene())
            {
                ui.render_world_space_into(ui_comp, output, world_to_clip * ui_comp.get_document_to_world(world_transform));
                ui_view_id = start_ui_view();
                continue;
            }
            const auto scale = ui_comp.get_world_space_scale();
            const auto model = world_transform * math::transform::scaling(scale);
            auto topology = gfx::clip_quad(0.0f, 0.5f, 0.5f);
            bgfx::setState(topology | world_ui_state);
            world_quad_program_->set_texture(0, "s_tex", ui_comp.framebuffer.get(), 0);
            gfx::set_world_transform(model);
            bgfx::submit(ui_view_id, world_quad_program_->native_handle());
        }

        ui_cache.entries.clear();

        world_quad_program_->end();
    }

    
    struct world_space_text_entry
    {
        entt::handle handle;
        text_component* comp = nullptr;
        transform_component* transform_comp = nullptr;
        float distance = 0.0f;
    };
    struct world_space_text_cache
    {
        std::vector<world_space_text_entry> entries;
    };
    
    //this approach preserves the capacities of the vector
    auto& text_cache = cache_registry_.get_or_emplace<world_space_text_cache>(cache_entity_);
    text_cache.entries.clear();

    scn.registry->view<transform_component, text_component, active_component>().each(
        [&](auto e, auto&& transform_comp, auto&& text_comp, auto&& active)
        {
            const auto& world_transform = transform_comp.get_transform_global();
            auto bbox = text_comp.get_bounds();

            if(!camera.test_obb(bbox, world_transform))
            {
                return;
            }

            float dist = math::distance2(camera.get_position(), world_transform.get_position());
            auto handle = scn.create_handle(e);
            text_cache.entries.push_back({handle, &text_comp, &transform_comp, dist});
        });

    std::sort(text_cache.entries.begin(), text_cache.entries.end(), [](const world_space_text_entry& a, const world_space_text_entry& b)
    {
        return a.distance > b.distance;
    });

    for(const auto& entry : text_cache.entries)
    {
        auto& text_comp = *entry.comp;
        auto handle = entry.handle;
        const auto& world_transform = entry.transform_comp->get_transform_global();
        text_comp.submit(ui_view_id, world_transform, BGFX_STATE_DEPTH_TEST_LESS);
    }

    text_cache.entries.clear();

    bgfx::discard();
}


auto pipeline::is_on_rendered_layer(const entt::registry& registry, entt::entity entity, const layer_mask& render_mask)
    -> bool
{
    const auto* layer = registry.try_get<layer_component>(entity);
    return layer == nullptr || (layer->layers.mask & render_mask.mask) != 0;
}

auto pipeline::get_particle_screen_size(const camera& camera, const math::bbox& bounds, const math::vec3& eye) -> float
{
    return get_screen_size(camera, bounds, eye);
}

auto pipeline::run_particle_pass(scene& scn,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 const gfx::frame_buffer::ptr& output,
                                 const pre_exposure_state& pre_exposure,
                                 const gfx::texture::ptr& hiz_depth,
                                 bool use_shadow_maps,
                                 bool wants_reactive,
                                 delta_t dt,
                                 const fog_pass::shader_params& fog,
                                 layer_mask render_mask) -> gfx::texture::ptr
{
    APP_SCOPE_PERF("Rendering/Particle Pass");

    auto lbuffer_depth = rview.fbo_get("LBUFFER_DEPTH");
    gfx::texture::ptr reactive;
    reset_particle_stats(stats_);
    const auto overdraw_target = is_particle_overdraw_view_ && lbuffer_depth
                                     ? acquire_particle_overdraw_target(rview, lbuffer_depth)
                                     : gfx::frame_buffer::ptr{};
    const math::vec3 camera_velocity = get_camera_velocity(camera, dt);
    const float camera_speed = math::length(camera_velocity);

    if(particle_program_ && particle_program_mask_ && particle_program_->begin() && particle_program_mask_->begin())
    {
        const auto cam_pos = camera.get_position();
        particle_draw_keys particle_emitters = gather_particle_emitters(scn, camera, camera_speed, render_mask, stats_);
        const bool wants_scene_depth = has_soft_particles(particle_emitters);
        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Sort Emitters");
            sort_particle_draw_keys(particle_emitters);
        }
        {
            APP_SCOPE_PERF("Rendering/Particle Pass/Submit Emitter Batches");
            const particle_batch_lists batches = build_particle_batches(particle_emitters, particle_handles_scratch_);
            const particle_batch_list& full_batches = batches.full;
            const particle_batch_list& half_batches = batches.half;
            const particle_batch_list& velocity_batches = batches.velocity;
            const bool has_lit_batches = batches.has_lit;
            if((!full_batches.empty() || !half_batches.empty()) && lbuffer_depth)
            {
                // The TAA lowers its history weight where particles cover the pixel (they have no motion
                // vectors). The coverage needs independent blend, and without a TAA reading it the particles
                // write the light buffer alone (a second target costs fill on every particle pixel).
                const bool has_reactive_target =
                    wants_reactive && (bgfx::getCaps()->supported & BGFX_CAPS_BLEND_INDEPENDENT) != 0;
                const auto targets = has_reactive_target ? acquire_particle_targets(rview, lbuffer_depth) : lbuffer_depth;
                ps_soa::render_view_params params;
                params.has_reactive_target = has_reactive_target;
                params.program = particle_program_->native_handle();
                params.mask_program = particle_program_mask_->native_handle();
                params.view_matrix = &camera.get_view()[0][0];
                params.eye = cam_pos;
                params.camera_velocity = camera_velocity;
                params.forward = camera.get_view_inverse().z_unit_axis();
                const auto& planes = camera.get_frustum().planes;
                for(size_t i = 0; i < planes.size(); ++i)
                {
                    params.frustum_planes[i] = planes[i].data;
                }
                const auto pre_exposure_values = pre_exposure.to_uniform();
                params.pre_exposure = math::vec4(pre_exposure_values[0], pre_exposure_values[1], pre_exposure_values[2], pre_exposure_values[3]);
                params.pre_exposure_uniform = particle_pre_exposure_uniform_->native_handle();
                // Every particle program reads the GPU's share of the pre-exposure from the environment SH at the lit
                // programs' sky stage (particle_fragment.sh), so each draw binds it there.
                params.environment_texture = pre_exposure.gpu_texture;
                params.environment_sampler = particle_irradiance_sampler_->native_handle();
                params.environment_stage = k_particle_irradiance_stage;
                // Every draw sets the fog, also when it is off: uniform values outlive the draw that set them, so a
                // draw that left them would fog with whichever view set them last.
                params.bind_fog = [&](bool is_lit)
                {
                    fog_pass_.bind_uniforms(fog);
                    if(!is_lit)
                    {
                        fog_pass_.bind_particle_volume(k_particle_fog_volume_stage, fog);
                    }
                };
                // Lit batches read the scene's light, gathered once per view and bound before each of their draws.
                particle_scene_lighting lighting;
                if(has_lit_batches && particle_program_lit_->begin() && particle_program_lit_mask_->begin())
                {
                    lighting = gather_particle_lighting(scn, rview, pre_exposure_values[0], use_shadow_maps);
                    params.lit_program = particle_program_lit_->native_handle();
                    params.lit_mask_program = particle_program_lit_mask_->native_handle();
                    params.bind_lighting = [&](const ps_soa::render_batch& batch)
                    {
                        bind_particle_lighting(lighting, batch);
                    };
                }
                const auto draw_batches = [&](bgfx::ViewId view, const particle_batch_list& batches)
                {
                    params.view = view;
                    const auto view_stats = ps_soa::render_view(params, batches.data(), uint32_t(batches.size()));
                    stats_.drawn_particles += view_stats.drawn_particles;
                    stats_.drawn_particles_batches += view_stats.draws;
                    stats_.drawn_particle_emitters += view_stats.drawn_emitters;
                    stats_.sorted_particle_keys += view_stats.sorted_keys;
                    return view_stats.draws > 0;
                };
                bool has_drawn = false;
                if(!full_batches.empty())
                {
                    gfx::render_pass pass("Particles/Pass");
                    pass.bind(targets.get());
                    pass.set_view_proj(camera.get_view(), camera.get_projection());
                    params.scene_depth = wants_scene_depth ? resolve_particle_scene_depth(rview, hiz_depth, pass.id)
                                                           : bgfx::TextureHandle(BGFX_INVALID_HANDLE);
                    has_drawn = draw_batches(pass.id, full_batches);
                }
                if(!half_batches.empty())
                {
                    // The half-size targets do not attach the scene depth: soft particles read it as it is.
                    const auto& scene_depth = hiz_depth ? hiz_depth : lbuffer_depth->get_texture(1);
                    params.scene_depth = wants_scene_depth ? scene_depth->native_handle()
                                                           : bgfx::TextureHandle(BGFX_INVALID_HANDLE);
                    params.is_transmittance_target = true;
                    particle_half_res_pass::run_params half_params;
                    half_params.cam = &camera;
                    half_params.lbuffer_depth = lbuffer_depth;
                    half_params.reactive = has_reactive_target ? targets->get_texture(1) : gfx::texture::ptr{};
                    half_params.has_independent_blend = has_reactive_target;
                    half_params.draw_particles = [&](bgfx::ViewId view) { return draw_batches(view, half_batches); };
                    has_drawn = particle_half_res_pass_.run(rview, half_params) || has_drawn;
                }
                if(has_reactive_target && has_drawn)
                {
                    reactive = targets->get_texture(1);
                }
                if(params.bind_lighting)
                {
                    particle_program_lit_->end();
                    particle_program_lit_mask_->end();
                    params.bind_lighting = nullptr;
                    params.lit_program = BGFX_INVALID_HANDLE;
                    params.lit_mask_program = BGFX_INVALID_HANDLE;
                }
                const auto& velocity_target = rview.fbo_find("VELOCITY_FBO");
                if(!velocity_batches.empty() && velocity_target && particle_velocity_program_->begin())
                {
                    // Nearly opaque particles replace the scene's motion vectors with their own, so the TAA
                    // reprojects them (rasterized jittered like the scene's movers, previous frame unjittered).
                    gfx::render_pass pass("Particles/Velocity");
                    pass.bind(velocity_target.get());
                    pass.set_view_proj(camera.get_view(), camera.get_projection());
                    const auto prev_view_proj = camera.get_prev_view_projection_unjittered();
                    params.view = pass.id;
                    params.program = particle_velocity_program_->native_handle();
                    params.mask_program = params.program;
                    params.scene_depth = BGFX_INVALID_HANDLE;
                    params.is_transmittance_target = false;
                    params.has_reactive_target = false;
                    params.prev_view_proj = &prev_view_proj.get_matrix()[0][0];
                    params.prev_view_proj_uniform = particle_prev_view_proj_uniform_->native_handle();
                    const auto view_stats =
                        ps_soa::render_view(params, velocity_batches.data(), uint32_t(velocity_batches.size()));
                    stats_.drawn_particles_batches += view_stats.draws;
                    stats_.sorted_particle_keys += view_stats.sorted_keys;
                    particle_velocity_program_->end();
                }
                if(overdraw_target && particle_overdraw_program_->begin())
                {
                    // Every draw of the pass again, counting the fragments it rasterizes: half-resolution ones
                    // shade a quarter of the pixels they cover, the velocity draws as many again. Each list
                    // gets its own view: a view runs all its culling dispatches before any of its draws.
                    constexpr float half_resolution_weight = 0.25f;
                    params.program = particle_overdraw_program_->native_handle();
                    params.mask_program = params.program;
                    params.scene_depth = BGFX_INVALID_HANDLE;
                    params.is_transmittance_target = false;
                    params.has_reactive_target = false;
                    params.prev_view_proj = nullptr;
                    const auto count_batches = [&](const particle_batch_list& batches, float weight)
                    {
                        if(batches.empty())
                        {
                            return;
                        }
                        gfx::render_pass pass("Particles/Overdraw");
                        pass.bind(overdraw_target.get());
                        pass.set_view_proj(camera.get_view(), camera.get_projection());
                        params.view = pass.id;
                        params.overdraw_weight = weight;
                        ps_soa::render_view(params, batches.data(), uint32_t(batches.size()));
                    };
                    count_batches(full_batches, 1.0f);
                    count_batches(half_batches, half_resolution_weight);
                    if(velocity_target)
                    {
                        count_batches(velocity_batches, 1.0f);
                    }
                    params.overdraw_weight = 0.0f;
                    particle_overdraw_program_->end();
                }
            }
        }

        particle_program_->end();
        particle_program_mask_->end();
    }
    return reactive;
}

void pipeline::bind_particle_lighting(const particle_scene_lighting& lighting, const ps_soa::render_batch& batch)
{
    math::bbox bounds;
    for(uint32_t e = 0; e < batch.count; ++e)
    {
        math::bbox emitter_bounds;
        ps_soa::get_aabb(batch.emitters[e], emitter_bounds);
        if(e == 0)
        {
            bounds = emitter_bounds;
        }
        else
        {
            bounds.add_point(emitter_bounds.min);
            bounds.add_point(emitter_bounds.max);
        }
    }
    std::array<math::vec4, k_particle_light_vec4s> lights{};
    const uint32_t light_count = pick_particle_lights(lighting, bounds, lights);
    const bool has_normal_map = bgfx::isValid(batch.normal_texture);
    const float lighting_params[4] = {float(light_count),
                                      lighting.pre_exposure,
                                      has_normal_map ? 1.0f : 0.0f,
                                      has_normal_map && batch.is_normal_texture_two_channel ? 1.0f : 0.0f};
    // Without a normal map the stage still holds a texture: the shader does not read it then.
    bgfx::setTexture(k_particle_normal_stage,
                     particle_normal_sampler_->native_handle(),
                     has_normal_map ? batch.normal_texture : particle_no_sky_->native_handle());
    gfx::set_uniform(particle_sun_direction_uniform_, lighting.sun_direction);
    gfx::set_uniform(particle_sun_color_uniform_, lighting.sun_color);
    gfx::set_uniform(particle_lighting_uniform_, lighting_params);
    gfx::set_uniform(particle_lights_uniform_, lights.data(), uint16_t(k_particle_light_vec4s));
    const bgfx::TextureHandle sky = bgfx::isValid(lighting.irradiance) ? lighting.irradiance : particle_no_sky_->native_handle();
    bgfx::setTexture(k_particle_irradiance_stage, particle_irradiance_sampler_->native_handle(), sky);
    if(lighting.shadow)
    {
        lighting.shadow->submit_uniforms(k_particle_shadow_stage);
        return;
    }
    for(size_t i = 0; i < particle_shadow_samplers_.size(); ++i)
    {
        bgfx::setTexture(uint8_t(k_particle_shadow_stage + i),
                         particle_shadow_samplers_[i]->native_handle(),
                         particle_no_shadow_->native_handle());
    }
}

// pipeline_stats implementation
void pipeline_stats::add_batch_stats(const batch_stats& stats)
{
    batching_stats.total_batches += stats.total_batches;
    batching_stats.total_instances += stats.total_instances;
    batching_stats.collection_time_ms += stats.collection_time_ms;
    batching_stats.preparation_time_ms += stats.preparation_time_ms;
    batching_stats.submission_time_ms += stats.submission_time_ms;
    batching_stats.instance_buffer_memory_used += stats.instance_buffer_memory_used;
    batching_stats.split_batches += stats.split_batches;
    
    // Recalculate derived stats
    batching_stats.calculate_derived_stats();
}

void pipeline_stats::add_stats(const pipeline_stats& stats)
{
    drawn_models += stats.drawn_models;
    drawn_static_submeshes += stats.drawn_static_submeshes;
    drawn_skinned_models += stats.drawn_skinned_models;
    drawn_skinned_submeshes += stats.drawn_skinned_submeshes;
    drawn_models_for_shadows += stats.drawn_models_for_shadows;
    drawn_submeshes_for_shadows += stats.drawn_submeshes_for_shadows;
    drawn_skinned_models_for_shadows += stats.drawn_skinned_models_for_shadows;
    drawn_skinned_submeshes_for_shadows += stats.drawn_skinned_submeshes_for_shadows;
    drawn_lights += stats.drawn_lights;
    drawn_lights_casting_shadows += stats.drawn_lights_casting_shadows;
    shadow_maps_rendered += stats.shadow_maps_rendered;
    shadow_maps_reused += stats.shadow_maps_reused;
    shadow_maps_restored += stats.shadow_maps_restored;
    // Every camera of a scene counts the same emitters as active and simulated: kept, not summed.
    active_particle_emitters = (std::max)(active_particle_emitters, stats.active_particle_emitters);
    simulated_particle_emitters = (std::max)(simulated_particle_emitters, stats.simulated_particle_emitters);
    active_particles = (std::max)(active_particles, stats.active_particles);
    simulated_particles = (std::max)(simulated_particles, stats.simulated_particles);
    drawn_particle_emitters += stats.drawn_particle_emitters;
    drawn_particles += stats.drawn_particles;
    drawn_particles_batches += stats.drawn_particles_batches;
    skipped_particle_emitters += stats.skipped_particle_emitters;
    skipped_particles += stats.skipped_particles;
    sorted_particle_keys += stats.sorted_particle_keys;

    add_batch_stats(stats.batching_stats);
}

} // namespace rendering
} // namespace unravel
