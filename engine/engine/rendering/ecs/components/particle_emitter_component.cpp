#include "particle_emitter_component.h"
#include "math/transform.hpp"
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/engine.h>
#include <graphics/utils/bgfx_utils.h>
#include <engine/ecs/ecs_utils.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/ecs/components/transform_component.h>
#include <graphics/graphics.h>
#include <logging/logging.h>

#include <cmath>
#include <cstring>

namespace unravel
{
namespace
{

/// Shortest emission cycle, seconds: a zero-length cycle would never emit or end.
constexpr float k_min_duration = 0.05f;
/// Most bounces a particle counts (the simulation keeps the count in a byte) and most collision planes.
constexpr uint32_t k_max_bounces = 255;
constexpr size_t k_max_collision_planes = 16;
/// Shortest time between two firings of one burst, seconds (the simulation's floor too).
constexpr float k_min_burst_interval = 0.01f;
/// Ranges the inspector offers, held by the setters as well so every writer stays inside them.
constexpr float k_max_color_intensity = 100.0f;
constexpr float k_max_screen_size_fraction = 4.0f;
constexpr float k_min_size_by_speed = 0.1f;
constexpr float k_max_size_by_speed = 5.0f;
constexpr float k_max_size_by_speed_input = 100.0f;

/// The faces of one submesh, in the mesh's space: its vertices (node transform applied, each once) and its
/// triangles with their running area.
void append_submesh_triangles(mesh& source,
                              uint32_t face_begin,
                              uint32_t face_end,
                              const math::transform& node,
                              std::vector<int32_t>& remap,
                              ps_soa::emission_mesh& out)
{
    const uint8_t* vertex_data = source.get_system_vb();
    const uint32_t* index_data = source.get_system_ib();
    const uint32_t vertex_count = source.get_vertex_count();
    const auto& layout = source.get_vertex_format();
    const bool has_normals = layout.has(bgfx::Attrib::Normal);
    remap.assign(vertex_count, -1);
    for(uint32_t face = face_begin; face < face_end; ++face)
    {
        uint32_t corners[3]{};
        for(uint32_t k = 0; k < 3; ++k)
        {
            const uint32_t index = math::min(index_data[face * 3 + k], vertex_count - 1);
            if(remap[index] < 0)
            {
                float position[4]{};
                float normal[4]{0.0f, 1.0f, 0.0f, 0.0f};
                bgfx::vertexUnpack(position, bgfx::Attrib::Position, layout, vertex_data, index);
                if(has_normals)
                {
                    bgfx::vertexUnpack(normal, bgfx::Attrib::Normal, layout, vertex_data, index);
                }
                const math::vec3 n = node.transform_normal(math::vec3(normal[0], normal[1], normal[2]));
                const float n_length = math::length(n);
                remap[index] = int32_t(out.positions.size());
                out.positions.push_back(node.transform_coord(math::vec3(position[0], position[1], position[2])));
                out.normals.push_back(n_length > 0.0f ? n / n_length : math::vec3(0.0f, 1.0f, 0.0f));
            }
            corners[k] = uint32_t(remap[index]);
            out.indices.push_back(corners[k]);
        }
        const math::vec3 edge_a = out.positions[corners[1]] - out.positions[corners[0]];
        const math::vec3 edge_b = out.positions[corners[2]] - out.positions[corners[0]];
        const float area = 0.5f * math::length(math::cross(edge_a, edge_b));
        out.cumulative_area.push_back((out.cumulative_area.empty() ? 0.0f : out.cumulative_area.back()) + area);
    }
}

/// What a mesh shape spawns on: every submesh's triangles in the mesh's space.
auto build_emission_mesh(mesh& source) -> std::shared_ptr<ps_soa::emission_mesh>
{
    auto result = std::make_shared<ps_soa::emission_mesh>();
    const uint32_t face_count = source.get_face_count();
    if(!source.get_system_vb() || !source.get_system_ib() || source.get_vertex_count() == 0 || face_count == 0 ||
       !source.get_vertex_format().has(bgfx::Attrib::Position))
    {
        return result;
    }
    std::vector<int32_t> remap;
    const auto& submeshes = source.get_submeshes();
    const auto node_transforms = source.get_submesh_node_transforms();
    if(submeshes.empty())
    {
        append_submesh_triangles(source, 0, face_count, math::transform{}, remap, *result);
    }
    for(size_t s = 0; s < submeshes.size(); ++s)
    {
        const auto* submesh = submeshes[s];
        if(!submesh || submesh->face_start < 0 || uint32_t(submesh->face_start) >= face_count)
        {
            continue;
        }
        const uint32_t face_begin = uint32_t(submesh->face_start);
        const uint32_t face_end = math::min(face_begin + submesh->face_count, face_count);
        const math::transform node = s < node_transforms.size() ? node_transforms[s] : math::transform{};
        append_submesh_triangles(source, face_begin, face_end, node, remap, *result);
    }
    if(!result->positions.empty())
    {
        result->bounds = math::bbox(result->positions.front(), result->positions.front());
        for(const auto& position : result->positions)
        {
            result->bounds.add_point(position);
        }
    }
    return result;
}

} // namespace

void particle_emitter_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    auto& component = entity.get<particle_emitter_component>();
    component.set_owner(entity);
    component.desc_.reset();
    component.recreate_emitter();
}

void particle_emitter_component::on_destroy_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    if(entity.all_of<particle_emitter_component>())
    {
        auto& component = entity.get<particle_emitter_component>();
        if(ps_soa::is_valid(component.emitter_handle_))
        {
            ps_soa::destroy_emitter(component.emitter_handle_);
            component.emitter_handle_ = {};
        }
    }
}

void particle_emitter_component::set_enabled(bool enabled)
{
    enabled_ = enabled;
}

auto particle_emitter_component::is_enabled() const -> bool
{
    return enabled_;
}

auto particle_emitter_component::get_emitter_handle() const -> ps_soa::emitter_handle
{
    return emitter_handle_;
}

void particle_emitter_component::set_shape(ps_soa::emitter_shape shape)
{
    // Read at every spawn: the live particles keep where they were spawned.
    desc_.emission.shape = shape;
}

auto particle_emitter_component::get_shape() const -> ps_soa::emitter_shape
{
    return desc_.emission.shape;
}

void particle_emitter_component::set_direction(ps_soa::emitter_direction direction)
{
    desc_.emission.direction = direction;
}

auto particle_emitter_component::get_direction() const -> ps_soa::emitter_direction
{
    return desc_.emission.direction;
}

void particle_emitter_component::set_spawn_location(ps_soa::spawn_location spawn_location)
{
    desc_.emission.spawn_location = spawn_location;
}

auto particle_emitter_component::get_spawn_location() const -> ps_soa::spawn_location
{
    return desc_.emission.spawn_location;
}

void particle_emitter_component::set_max_particles(uint32_t max_particles)
{
    max_particles = math::max(max_particles, 1u);
    if(max_particles_ != max_particles)
    {
        max_particles_ = max_particles;
        recreate_emitter();
    }
}

auto particle_emitter_component::get_max_particles() const -> uint32_t
{
    return max_particles_;
}

void particle_emitter_component::set_duration(std::chrono::duration<float> duration)
{
    desc_.emission.duration = math::max(duration.count(), k_min_duration);
}

auto particle_emitter_component::get_duration() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(desc_.emission.duration);
}

void particle_emitter_component::set_gravity_scale(float scale)
{
    desc_.motion.gravity_scale = scale;
}

auto particle_emitter_component::get_gravity_scale() const -> float
{
    return desc_.motion.gravity_scale;
}

void particle_emitter_component::set_emission_rate(float emission_rate)
{
    // The rate is read at every spawn, so a change keeps the live particles: a script may drive it
    // each frame.
    desc_.emission.particles_per_second = math::max(emission_rate, 0.0f);
}

auto particle_emitter_component::get_emission_rate() const -> float
{
    return desc_.emission.particles_per_second;
}

void particle_emitter_component::set_spawn_along_path(float spawn_along_path)
{
    desc_.motion.spawn_along_path = math::clamp(spawn_along_path, 0.0f, 1.0f);
}

auto particle_emitter_component::get_spawn_along_path() const -> float
{
    return desc_.motion.spawn_along_path;
}

void particle_emitter_component::set_drag(float drag)
{
    desc_.motion.drag = math::max(drag, 0.0f);
}

auto particle_emitter_component::get_drag() const -> float
{
    return desc_.motion.drag;
}

void particle_emitter_component::set_start_speed(const frange_t& speed)
{
    desc_.motion.start_speed = speed;
}

auto particle_emitter_component::get_start_speed() const -> const frange_t&
{
    return desc_.motion.start_speed;
}

void particle_emitter_component::set_speed_over_life(const math::gradient<float>& curve)
{
    desc_.motion.speed_over_life = curve;
    desc_.motion.speed_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_speed_over_life() const -> const math::gradient<float>&
{
    return desc_.motion.speed_over_life;
}

void particle_emitter_component::set_inherit_velocity(float fraction)
{
    desc_.motion.inherit_velocity = fraction;
}

auto particle_emitter_component::get_inherit_velocity() const -> float
{
    return desc_.motion.inherit_velocity;
}

void particle_emitter_component::set_direction_spread(float degrees)
{
    desc_.emission.direction_spread = math::clamp(degrees, 0.0f, 180.0f);
}

auto particle_emitter_component::get_direction_spread() const -> float
{
    return desc_.emission.direction_spread;
}

void particle_emitter_component::set_use_fixed_seed(bool use_fixed_seed)
{
    desc_.emission.use_fixed_seed = use_fixed_seed;
}

auto particle_emitter_component::get_use_fixed_seed() const -> bool
{
    return desc_.emission.use_fixed_seed;
}

void particle_emitter_component::set_random_seed(uint32_t seed)
{
    desc_.emission.random_seed = seed;
}

auto particle_emitter_component::get_random_seed() const -> uint32_t
{
    return desc_.emission.random_seed;
}

void particle_emitter_component::set_acceleration(const math::vec3& force)
{
    desc_.motion.acceleration = force;
}

auto particle_emitter_component::get_acceleration() const -> math::vec3
{
    return desc_.motion.acceleration;
}

void particle_emitter_component::set_shape_position(const math::vec3& position)
{
    desc_.emission.shape_position = position;
}

auto particle_emitter_component::get_shape_position() const -> math::vec3
{
    return desc_.emission.shape_position;
}

void particle_emitter_component::set_shape_scale(const math::vec3& scale)
{
    desc_.emission.shape_scale = scale;
}

auto particle_emitter_component::get_shape_scale() const -> math::vec3
{
    return desc_.emission.shape_scale;
}

void particle_emitter_component::set_size_by_speed_multiplier(const frange_t& size_range)
{
    desc_.appearance.size_by_speed_multiplier = frange_t(math::clamp(size_range.min, k_min_size_by_speed, k_max_size_by_speed),
                                                    math::clamp(size_range.max, k_min_size_by_speed, k_max_size_by_speed));
}

auto particle_emitter_component::get_size_by_speed_multiplier() const -> const frange_t&
{
    return desc_.appearance.size_by_speed_multiplier;
}

void particle_emitter_component::set_size_by_speed_speed_range(const frange_t& velocity_range)
{
    desc_.appearance.size_by_speed_speed_range = frange_t(math::clamp(velocity_range.min, 0.0f, k_max_size_by_speed_input),
                                                             math::clamp(velocity_range.max, 0.0f, k_max_size_by_speed_input));
}

auto particle_emitter_component::get_size_by_speed_speed_range() const -> const frange_t&
{
    return desc_.appearance.size_by_speed_speed_range;
}

void particle_emitter_component::set_color_by_speed_gradient(const math::gradient<math::color>& gradient)
{
    desc_.appearance.color_by_speed_gradient = gradient;
    desc_.appearance.color_by_speed_gradient.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_color_by_speed_gradient() const -> const math::gradient<math::color>&
{
    return desc_.appearance.color_by_speed_gradient;
}

void particle_emitter_component::set_color_by_speed_speed_range(const frange_t& velocity_range)
{
    desc_.appearance.color_by_speed_speed_range = velocity_range;
}

auto particle_emitter_component::get_color_by_speed_speed_range() const -> const frange_t&
{
    return desc_.appearance.color_by_speed_speed_range;
}

void particle_emitter_component::set_lifetime_by_emitter_speed_curve(const math::gradient<float>& gradient)
{
    desc_.motion.lifetime_by_emitter_speed_curve = gradient;
    desc_.motion.lifetime_by_emitter_speed_curve.bake(ps_soa::lifetime_by_emitter_speed_bake_resolution);
}

auto particle_emitter_component::get_lifetime_by_emitter_speed_curve() const -> const math::gradient<float>&
{
    return desc_.motion.lifetime_by_emitter_speed_curve;
}

void particle_emitter_component::set_lifetime_by_emitter_speed_range(const frange_t& speed_range)
{
    desc_.motion.lifetime_by_emitter_speed_range = speed_range;
}

auto particle_emitter_component::get_lifetime_by_emitter_speed_range() const -> const frange_t&
{
    return desc_.motion.lifetime_by_emitter_speed_range;
}

void particle_emitter_component::set_lifetime(const frange_t& lifetime)
{
    // Read at every spawn: the live particles keep the lifetime they were born with.
    const float minimum = math::clamp(lifetime.min, 0.0f, ps_soa::max_particle_lifetime);
    desc_.motion.lifetime = frange_t(minimum, math::clamp(lifetime.max, minimum, ps_soa::max_particle_lifetime));
}

auto particle_emitter_component::get_lifetime() const -> const frange_t&
{
    return desc_.motion.lifetime;
}

void particle_emitter_component::set_size_over_life(const math::gradient<frange_t>& gradient)
{
    desc_.appearance.size_over_life = gradient;
    desc_.appearance.size_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_size_over_life() const -> const math::gradient<frange_t>&
{
    return desc_.appearance.size_over_life;
}

void particle_emitter_component::set_size_axes(const math::vec3& scale)
{
    desc_.appearance.size_axes = scale;
}

auto particle_emitter_component::get_size_axes() const -> math::vec3
{
    return desc_.appearance.size_axes;
}

void particle_emitter_component::set_opacity(float opacity)
{
    desc_.appearance.opacity = math::clamp(opacity, 0.0f, 1.0f);
}

auto particle_emitter_component::get_opacity() const -> float
{
    return desc_.appearance.opacity;
}

void particle_emitter_component::set_color_intensity(float intensity)
{
    desc_.appearance.color_intensity = math::clamp(intensity, 0.0f, k_max_color_intensity);
}

auto particle_emitter_component::get_color_intensity() const -> float
{
    return desc_.appearance.color_intensity;
}

void particle_emitter_component::play()
{
    desc_.playback.playing = true;
    desc_.playback.paused = false;
}

void particle_emitter_component::stop()
{
    desc_.playback.playing = false;
    desc_.playback.paused = false;
}

void particle_emitter_component::stop_and_reset()
{
    desc_.playback.playing = false;
    desc_.playback.paused = false;
    reset_emitter();
}

void particle_emitter_component::pause()
{
    if(desc_.playback.playing)
    {
        desc_.playback.paused = true;
    }
}

void particle_emitter_component::resume()
{
    if(desc_.playback.playing)
    {
        desc_.playback.paused = false;
    }
}

void particle_emitter_component::play_child_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.play();
                                                                   });
    }
}

void particle_emitter_component::stop_child_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.stop();
                                                                   });
    }
}

void particle_emitter_component::stop_and_reset_child_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.stop_and_reset();
                                                                   });
    }
}

void particle_emitter_component::pause_child_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.pause();
                                                                   });
    }
}

void particle_emitter_component::resume_child_emitters()
{
    auto owner = get_owner();
    if(owner.valid())
    {
        for_each_component_in_children<particle_emitter_component>(owner,
                                                                   [](particle_emitter_component& emitter)
                                                                   {
                                                                       emitter.resume();
                                                                   });
    }
}

auto particle_emitter_component::is_playing() const -> bool
{
    return desc_.playback.playing;
}

auto particle_emitter_component::is_paused() const -> bool
{
    return desc_.playback.paused;
}

auto particle_emitter_component::is_stopped() const -> bool
{
    return !desc_.playback.playing && !desc_.playback.paused;
}

void particle_emitter_component::set_loop(bool loop)
{
    desc_.emission.loop = loop;
}

auto particle_emitter_component::is_loop() const -> bool
{
    return desc_.emission.loop;
}

void particle_emitter_component::set_start_delay(std::chrono::duration<float> delay)
{
    const float start_delay = math::max(0.0f, delay.count());
    if(start_delay == desc_.emission.start_delay)
    {
        return;
    }
    desc_.emission.start_delay = start_delay;
    reset_emitter();
}

auto particle_emitter_component::get_start_delay() const -> std::chrono::duration<float>
{
    return std::chrono::duration<float>(desc_.emission.start_delay);
}

void particle_emitter_component::set_align_to_direction(bool align)
{
    desc_.render.align_to_direction = align;
}

auto particle_emitter_component::get_align_to_direction() const -> bool
{
    return desc_.render.align_to_direction;
}

void particle_emitter_component::set_pivot(const math::vec2& pivot)
{
    desc_.render.pivot = math::clamp(pivot, math::vec2(0.0f), math::vec2(1.0f));
}

auto particle_emitter_component::get_pivot() const -> math::vec2
{
    return desc_.render.pivot;
}

void particle_emitter_component::set_color_gradient(const math::gradient<math::color>& gradient)
{
    desc_.appearance.color_gradient = gradient;
    desc_.appearance.color_gradient.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_color_gradient() const -> const math::gradient<math::color>&
{
    return desc_.appearance.color_gradient;
}

void particle_emitter_component::set_motion_easing(bx::Easing::Enum easing)
{
    desc_.motion.motion_easing = easing;
}

auto particle_emitter_component::get_motion_easing() const -> bx::Easing::Enum
{
    return desc_.motion.motion_easing;
}

auto particle_emitter_component::get_num_particles() const -> uint32_t
{
    return ps_soa::get_num_particles(emitter_handle_);
}

auto particle_emitter_component::get_world_bounds() const -> math::bbox
{
    math::bbox bounds(math::vec3(-1.0f), math::vec3(1.0f));
    ps_soa::get_aabb(emitter_handle_, bounds);
    return bounds;
}

auto particle_emitter_component::get_updated_world_bounds(const math::transform& world_transform) const -> math::bbox
{
    auto bounds = get_world_bounds();
    if(!ps_soa::has_updated(emitter_handle_))
    {
        bounds.mul(world_transform);
    }
    return bounds;
}

void particle_emitter_component::set_texture(const asset_handle<gfx::texture>& texture)
{
    texture_ = texture;
}

auto particle_emitter_component::get_texture() const -> const asset_handle<gfx::texture>&
{
    return texture_;
}

auto particle_emitter_component::get_drawn_texture() const -> const asset_handle<gfx::texture>&
{
    if(texture_.is_valid())
    {
        return texture_;
    }
    return material::default_color_map();
}

void particle_emitter_component::set_normal_texture(const asset_handle<gfx::texture>& value)
{
    normal_texture_ = value;
}

auto particle_emitter_component::get_normal_texture() const -> const asset_handle<gfx::texture>&
{
    return normal_texture_;
}

void particle_emitter_component::set_texture_mode(ps_soa::texture_mode mode)
{
    desc_.render.texture_mode = mode;
}

auto particle_emitter_component::get_texture_mode() const -> ps_soa::texture_mode
{
    return desc_.render.texture_mode;
}

void particle_emitter_component::set_render_mode(ps_soa::render_mode mode)
{
    desc_.render.render_mode = mode;
}

auto particle_emitter_component::get_render_mode() const -> ps_soa::render_mode
{
    return desc_.render.render_mode;
}

void particle_emitter_component::set_blend_mode(ps_soa::blend_mode mode)
{
    desc_.render.blend_mode = mode;
}

auto particle_emitter_component::get_blend_mode() const -> ps_soa::blend_mode
{
    return desc_.render.blend_mode;
}

void particle_emitter_component::set_texture_sheet_tiles(math::vec2 tiles)
{
    // Whole frames only: the shader, the cutout bake and the frame picks all count in whole tiles.
    desc_.render.tex_sheet_tiles = math::vec2(math::max(std::floor(tiles.x), 1.0f), math::max(std::floor(tiles.y), 1.0f));
}

auto particle_emitter_component::get_texture_sheet_tiles() const -> math::vec2
{
    return desc_.render.tex_sheet_tiles;
}

void particle_emitter_component::set_texture_sheet_cycles(float cycles)
{
    desc_.render.tex_sheet_cycles = math::max(cycles, 0.0f);
}

auto particle_emitter_component::get_texture_sheet_cycles() const -> float
{
    return desc_.render.tex_sheet_cycles;
}

void particle_emitter_component::set_frame_blending(bool blend)
{
    desc_.render.frame_blending = blend;
}

auto particle_emitter_component::get_frame_blending() const -> bool
{
    return desc_.render.frame_blending;
}

void particle_emitter_component::set_start_rotation(const frange_t& degrees)
{
    desc_.render.start_rotation = degrees;
}

auto particle_emitter_component::get_start_rotation() const -> const frange_t&
{
    return desc_.render.start_rotation;
}

void particle_emitter_component::set_spin_speed(const frange_t& degrees_per_second)
{
    desc_.render.spin_speed = degrees_per_second;
}

auto particle_emitter_component::get_spin_speed() const -> const frange_t&
{
    return desc_.render.spin_speed;
}

void particle_emitter_component::set_random_spin_direction(bool random)
{
    desc_.render.random_spin_direction = random;
}

auto particle_emitter_component::get_random_spin_direction() const -> bool
{
    return desc_.render.random_spin_direction;
}

void particle_emitter_component::set_soft_fade_distance(float distance)
{
    desc_.render.soft_fade_distance = math::max(distance, 0.0f);
}

auto particle_emitter_component::get_soft_fade_distance() const -> float
{
    return desc_.render.soft_fade_distance;
}

void particle_emitter_component::set_prewarm(bool prewarm)
{
    desc_.emission.prewarm = prewarm;
}

auto particle_emitter_component::get_prewarm() const -> bool
{
    return desc_.emission.prewarm;
}

void particle_emitter_component::set_auto_play(bool auto_play)
{
    desc_.playback.auto_play = auto_play;
    // Loading or instantiating sets it before the emitter's first frame, which is when it decides.
    if(is_newly_created())
    {
        desc_.playback.playing = auto_play;
    }
}

auto particle_emitter_component::get_auto_play() const -> bool
{
    return desc_.playback.auto_play;
}

void particle_emitter_component::set_sort_priority(int32_t priority)
{
    desc_.render.sort_priority = priority;
}

auto particle_emitter_component::get_sort_priority() const -> int32_t
{
    return desc_.render.sort_priority;
}

void particle_emitter_component::set_stretch_length_scale(float value)
{
    desc_.render.stretch_length_scale = math::max(value, 0.0f);
}

auto particle_emitter_component::get_stretch_length_scale() const -> float
{
    return desc_.render.stretch_length_scale;
}

void particle_emitter_component::set_stretch_speed_scale(float value)
{
    desc_.render.stretch_speed_scale = math::max(value, 0.0f);
}

auto particle_emitter_component::get_stretch_speed_scale() const -> float
{
    return desc_.render.stretch_speed_scale;
}

void particle_emitter_component::set_stretch_camera_velocity_scale(float value)
{
    desc_.render.stretch_camera_velocity_scale = math::max(value, 0.0f);
}

auto particle_emitter_component::get_stretch_camera_velocity_scale() const -> float
{
    return desc_.render.stretch_camera_velocity_scale;
}

void particle_emitter_component::set_facing_axis(const math::vec3& value)
{
    constexpr float min_axis_length = 1.0e-6f;
    const float length = math::length(value);
    desc_.render.facing_axis = length > min_axis_length ? value / length : math::vec3(0.0f, 1.0f, 0.0f);
}

auto particle_emitter_component::get_facing_axis() const -> const math::vec3&
{
    return desc_.render.facing_axis;
}

void particle_emitter_component::set_camera_offset(float value)
{
    desc_.render.camera_offset = math::max(value, 0.0f);
}

auto particle_emitter_component::get_camera_offset() const -> float
{
    return desc_.render.camera_offset;
}

void particle_emitter_component::set_max_screen_size(float value)
{
    desc_.render.max_screen_size = math::clamp(value, 0.0f, k_max_screen_size_fraction);
}

auto particle_emitter_component::get_max_screen_size() const -> float
{
    return desc_.render.max_screen_size;
}

void particle_emitter_component::set_taa_response(float value)
{
    desc_.render.taa_response = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_taa_response() const -> float
{
    return desc_.render.taa_response;
}

void particle_emitter_component::set_camera_fade_range(const frange_t& range)
{
    const float near_distance = math::max(range.min, 0.0f);
    desc_.render.camera_fade_range = frange_t(near_distance, math::max(range.max, near_distance));
}

auto particle_emitter_component::get_camera_fade_range() const -> const frange_t&
{
    return desc_.render.camera_fade_range;
}

void particle_emitter_component::set_subpixel_fade(bool fade)
{
    desc_.render.subpixel_fade = fade;
}

auto particle_emitter_component::get_subpixel_fade() const -> bool
{
    return desc_.render.subpixel_fade;
}

void particle_emitter_component::set_erosion_over_life(const math::gradient<float>& curve)
{
    desc_.render.erosion_over_life = curve;
    desc_.render.erosion_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_erosion_over_life() const -> const math::gradient<float>&
{
    return desc_.render.erosion_over_life;
}

void particle_emitter_component::set_sort_mode(ps_soa::sort_mode mode)
{
    desc_.render.sort_mode = mode;
}

auto particle_emitter_component::get_sort_mode() const -> ps_soa::sort_mode
{
    return desc_.render.sort_mode;
}

void particle_emitter_component::set_primitive(ps_soa::particle_primitive primitive)
{
    desc_.render.primitive = primitive;
}

auto particle_emitter_component::get_primitive() const -> ps_soa::particle_primitive
{
    return desc_.render.primitive;
}

void particle_emitter_component::set_half_resolution(bool half_resolution)
{
    desc_.render.half_resolution = half_resolution;
}

auto particle_emitter_component::get_half_resolution() const -> bool
{
    return desc_.render.half_resolution;
}

void particle_emitter_component::set_write_velocity(bool write_velocity)
{
    desc_.render.write_velocity = write_velocity;
}

auto particle_emitter_component::get_write_velocity() const -> bool
{
    return desc_.render.write_velocity;
}

void particle_emitter_component::set_velocity_alpha_threshold(float threshold)
{
    desc_.render.velocity_alpha_threshold = math::clamp(threshold, 0.0f, 1.0f);
}

auto particle_emitter_component::get_velocity_alpha_threshold() const -> float
{
    return desc_.render.velocity_alpha_threshold;
}

void particle_emitter_component::set_trail_length(float value)
{
    desc_.render.trail_length = math::max(value, 0.0f);
}

auto particle_emitter_component::get_trail_length() const -> float
{
    return desc_.render.trail_length;
}

void particle_emitter_component::set_trail_width_end(float value)
{
    desc_.render.trail_width_end = math::max(value, 0.0f);
}

auto particle_emitter_component::get_trail_width_end() const -> float
{
    return desc_.render.trail_width_end;
}

void particle_emitter_component::set_ribbon_texture_mode(ps_soa::ribbon_texture_mode mode)
{
    desc_.render.ribbon_texture_mode = mode;
}

auto particle_emitter_component::get_ribbon_texture_mode() const -> ps_soa::ribbon_texture_mode
{
    return desc_.render.ribbon_texture_mode;
}

void particle_emitter_component::set_ribbon_tile_time(float value)
{
    desc_.render.ribbon_tile_time = math::max(value, ps_soa::k_min_ribbon_tile_time);
}

auto particle_emitter_component::get_ribbon_tile_time() const -> float
{
    return desc_.render.ribbon_tile_time;
}

void particle_emitter_component::set_restitution(float value)
{
    desc_.motion.restitution = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_restitution() const -> float
{
    return desc_.motion.restitution;
}

void particle_emitter_component::set_friction(float value)
{
    desc_.motion.friction = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_friction() const -> float
{
    return desc_.motion.friction;
}

void particle_emitter_component::set_max_bounces(uint32_t value)
{
    desc_.motion.max_bounces = math::min(value, k_max_bounces);
}

auto particle_emitter_component::get_max_bounces() const -> uint32_t
{
    return desc_.motion.max_bounces;
}

void particle_emitter_component::set_collision_radius_scale(float value)
{
    desc_.motion.collision_radius_scale = math::max(value, 0.0f);
}

auto particle_emitter_component::get_collision_radius_scale() const -> float
{
    return desc_.motion.collision_radius_scale;
}

void particle_emitter_component::set_collision(ps_soa::collision_mode mode)
{
    desc_.motion.collision = mode;
}

auto particle_emitter_component::get_collision() const -> ps_soa::collision_mode
{
    return desc_.motion.collision;
}

void particle_emitter_component::set_collision_planes(const std::vector<math::vec4>& planes)
{
    desc_.motion.collision_planes.clear();
    for(const auto& plane : planes)
    {
        const math::vec3 normal(plane);
        const float length = math::length(normal);
        if(length > 0.0f && desc_.motion.collision_planes.size() < k_max_collision_planes)
        {
            desc_.motion.collision_planes.emplace_back(normal / length, plane.w);
        }
    }
}

auto particle_emitter_component::get_collision_planes() const -> const std::vector<math::vec4>&
{
    return desc_.motion.collision_planes;
}

void particle_emitter_component::set_sub_emitters(const std::vector<particle_sub_emitter>& sub_emitters)
{
    sub_emitters_ = sub_emitters;
    desc_.event_mask = 0;
    for(const auto& sub_emitter : sub_emitters_)
    {
        desc_.event_mask |= static_cast<uint32_t>(sub_emitter.trigger);
    }
}

auto particle_emitter_component::get_sub_emitters() const -> const std::vector<particle_sub_emitter>&
{
    return sub_emitters_;
}

void particle_emitter_component::fire_sub_emitters()
{
    if(sub_emitters_.empty() || !ps_soa::is_valid(emitter_handle_))
    {
        return;
    }
    sub_emitter_events_.clear();
    ps_soa::take_particle_events(emitter_handle_, sub_emitter_events_);
    if(!is_sub_emitter_rng_seeded_)
    {
        // Each emitter draws its own sequence, so emitters with the same probabilities do not fire in step.
        const auto seed = uint32_t(reinterpret_cast<uintptr_t>(this) >> 4u);
        sub_emitter_rng_.reset(seed | 1u, (seed * 2654435761u) | 1u);
        is_sub_emitter_rng_seeded_ = true;
    }
    for(const auto& sub_emitter : sub_emitters_)
    {
        auto* target = sub_emitter.target ? sub_emitter.target.try_get<particle_emitter_component>() : nullptr;
        // An inactive target is not updated: its bursts would wait and all fire when it comes back.
        if(target == nullptr || target == this || !sub_emitter.target.all_of<active_component>())
        {
            continue;
        }
        // The target's particles live in its simulation space.
        math::mat4 world_to_target(1.0f);
        if(target->get_simulation_space() == ps_soa::simulation_space::local)
        {
            if(const auto* transform = sub_emitter.target.try_get<transform_component>())
            {
                world_to_target = glm::inverse(math::mat4(transform->get_transform_global().get_matrix()));
            }
        }
        for(const auto& event : sub_emitter_events_)
        {
            if(event.type != sub_emitter.trigger ||
               (sub_emitter.probability < 1.0f && bx::frnd(&sub_emitter_rng_) >= sub_emitter.probability))
            {
                continue;
            }
            const float low = math::max(math::min(sub_emitter.count.min, sub_emitter.count.max), 0.0f);
            const float high = math::max(sub_emitter.count.min, sub_emitter.count.max);
            const uint32_t count = uint32_t(math::floor(low + bx::frnd(&sub_emitter_rng_) * (high - low + 1.0f)));
            if(count == 0)
            {
                continue;
            }
            ps_soa::emit_params params;
            params.position = math::vec3(world_to_target * math::vec4(event.position, 1.0f));
            params.mask = ps_soa::emit_params::position_flag;
            if(sub_emitter.inherit_velocity != 0.0f)
            {
                params.added_velocity = math::vec3(world_to_target * math::vec4(event.velocity * sub_emitter.inherit_velocity, 0.0f));
                params.mask |= ps_soa::emit_params::added_velocity_flag;
            }
            if(sub_emitter.inherit_color)
            {
                const math::vec4 color = math::clamp(event.color, math::vec4(0.0f), math::vec4(1.0f));
                params.color = math::color(color.r, color.g, color.b, color.a).to_srgb();
                params.mask |= ps_soa::emit_params::color_flag;
            }
            if(sub_emitter.inherit_size)
            {
                params.size = event.size;
                params.mask |= ps_soa::emit_params::size_flag;
            }
            target->emit(count, params);
        }
    }
}

void particle_emitter_component::set_light_count(uint32_t value)
{
    desc_.render.light_count = value;
}

auto particle_emitter_component::get_light_count() const -> uint32_t
{
    return desc_.render.light_count;
}

void particle_emitter_component::set_light_intensity(float value)
{
    desc_.render.light_intensity = math::max(value, 0.0f);
}

auto particle_emitter_component::get_light_intensity() const -> float
{
    return desc_.render.light_intensity;
}

void particle_emitter_component::set_light_range(float value)
{
    desc_.render.light_range = math::max(value, 0.0f);
}

auto particle_emitter_component::get_light_range() const -> float
{
    return desc_.render.light_range;
}

void particle_emitter_component::set_light_color_from_particle(bool value)
{
    desc_.render.light_color_from_particle = value;
}

auto particle_emitter_component::get_light_color_from_particle() const -> bool
{
    return desc_.render.light_color_from_particle;
}

void particle_emitter_component::set_lighting_wrap(float value)
{
    desc_.render.lighting_wrap = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_lighting_wrap() const -> float
{
    return desc_.render.lighting_wrap;
}

void particle_emitter_component::set_lighting(ps_soa::particle_lighting lighting)
{
    desc_.render.lighting = lighting;
}

auto particle_emitter_component::get_lighting() const -> ps_soa::particle_lighting
{
    return desc_.render.lighting;
}

void particle_emitter_component::set_receive_shadows(bool value)
{
    desc_.render.receive_shadows = value;
}

auto particle_emitter_component::get_receive_shadows() const -> bool
{
    return desc_.render.receive_shadows;
}

void particle_emitter_component::set_max_draw_distance(float value)
{
    desc_.render.max_draw_distance = math::max(value, 0.0f);
}

auto particle_emitter_component::get_max_draw_distance() const -> float
{
    return desc_.render.max_draw_distance;
}

void particle_emitter_component::set_draw_distance_fade(float value)
{
    desc_.render.draw_distance_fade = math::max(value, 0.0f);
}

auto particle_emitter_component::get_draw_distance_fade() const -> float
{
    return desc_.render.draw_distance_fade;
}

void particle_emitter_component::set_turbulence_strength(float value)
{
    desc_.motion.turbulence_strength = math::max(value, 0.0f);
}

auto particle_emitter_component::get_turbulence_strength() const -> float
{
    return desc_.motion.turbulence_strength;
}

void particle_emitter_component::set_turbulence_frequency(float value)
{
    desc_.motion.turbulence_frequency = math::max(value, 0.0f);
}

auto particle_emitter_component::get_turbulence_frequency() const -> float
{
    return desc_.motion.turbulence_frequency;
}

void particle_emitter_component::set_turbulence_speed(float value)
{
    desc_.motion.turbulence_speed = value;
}

auto particle_emitter_component::get_turbulence_speed() const -> float
{
    return desc_.motion.turbulence_speed;
}

void particle_emitter_component::set_turbulence_over_life(const math::gradient<float>& value)
{
    desc_.motion.turbulence_over_life = value;
    desc_.motion.turbulence_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_turbulence_over_life() const -> const math::gradient<float>&
{
    return desc_.motion.turbulence_over_life;
}

void particle_emitter_component::set_cone_angle(float value)
{
    desc_.emission.cone_angle = math::clamp(value, 0.0f, 90.0f);
}

auto particle_emitter_component::get_cone_angle() const -> float
{
    return desc_.emission.cone_angle;
}

void particle_emitter_component::set_torus_thickness(float value)
{
    desc_.emission.torus_thickness = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_torus_thickness() const -> float
{
    return desc_.emission.torus_thickness;
}

void particle_emitter_component::set_shape_mesh(const asset_handle<mesh>& value)
{
    shape_mesh_ = value;
    emission_mesh_source_ = nullptr;
    desc_.emission.mesh.reset();
}

auto particle_emitter_component::get_shape_mesh() const -> const asset_handle<mesh>&
{
    return shape_mesh_;
}

void particle_emitter_component::set_shape_mesh_mode(ps_soa::mesh_spawn_mode mode)
{
    desc_.emission.mesh_mode = mode;
}

auto particle_emitter_component::get_shape_mesh_mode() const -> ps_soa::mesh_spawn_mode
{
    return desc_.emission.mesh_mode;
}

void particle_emitter_component::refresh_emission_mesh()
{
    if(desc_.emission.shape != ps_soa::emitter_shape::mesh || !shape_mesh_.is_valid() ||
       !shape_mesh_.is_ready())
    {
        return;
    }
    const auto source = shape_mesh_.get(false);
    if(!source || source.get() == emission_mesh_source_)
    {
        return;
    }
    desc_.emission.mesh = build_emission_mesh(*source);
    emission_mesh_source_ = source.get();
}

void particle_emitter_component::set_rate_over_distance(float value)
{
    desc_.emission.particles_per_meter = math::max(value, 0.0f);
}

auto particle_emitter_component::get_rate_over_distance() const -> float
{
    return desc_.emission.particles_per_meter;
}

void particle_emitter_component::set_bursts(const std::vector<ps_soa::emitter_burst>& bursts)
{
    desc_.emission.bursts = bursts;
    for(auto& burst : desc_.emission.bursts)
    {
        burst.time = math::max(burst.time, 0.0f);
        burst.count = frange_t(math::max(burst.count.min, 0.0f), math::max(burst.count.max, 0.0f));
        burst.interval = math::max(burst.interval, k_min_burst_interval);
        burst.probability = math::clamp(burst.probability, 0.0f, 1.0f);
    }
}

auto particle_emitter_component::get_bursts() const -> const std::vector<ps_soa::emitter_burst>&
{
    return desc_.emission.bursts;
}

void particle_emitter_component::set_size_axes_over_life(const math::gradient<math::vec3>& value)
{
    desc_.appearance.size_axes_over_life = value;
    desc_.appearance.size_axes_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_size_axes_over_life() const -> const math::gradient<math::vec3>&
{
    return desc_.appearance.size_axes_over_life;
}

void particle_emitter_component::set_random_between_gradients(bool value)
{
    desc_.appearance.random_between_gradients = value;
}

auto particle_emitter_component::get_random_between_gradients() const -> bool
{
    return desc_.appearance.random_between_gradients;
}

void particle_emitter_component::set_color_gradient_b(const math::gradient<math::color>& value)
{
    desc_.appearance.color_gradient_b = value;
    desc_.appearance.color_gradient_b.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_color_gradient_b() const -> const math::gradient<math::color>&
{
    return desc_.appearance.color_gradient_b;
}

void particle_emitter_component::set_intensity_over_life(const math::gradient<float>& value)
{
    desc_.appearance.intensity_over_life = value;
    desc_.appearance.intensity_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_intensity_over_life() const -> const math::gradient<float>&
{
    return desc_.appearance.intensity_over_life;
}

void particle_emitter_component::set_spin_over_life(const math::gradient<float>& value)
{
    desc_.render.spin_over_life = value;
    desc_.render.spin_over_life.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_spin_over_life() const -> const math::gradient<float>&
{
    return desc_.render.spin_over_life;
}

void particle_emitter_component::set_texture_sheet_use_frame_curve(bool value)
{
    desc_.render.tex_sheet_use_frame_curve = value;
}

auto particle_emitter_component::get_texture_sheet_use_frame_curve() const -> bool
{
    return desc_.render.tex_sheet_use_frame_curve;
}

void particle_emitter_component::set_texture_sheet_frame_curve(const math::gradient<float>& value)
{
    desc_.render.tex_sheet_frame_curve = value;
    desc_.render.tex_sheet_frame_curve.bake(ps_soa::curve_bake_resolution);
}

auto particle_emitter_component::get_texture_sheet_frame_curve() const -> const math::gradient<float>&
{
    return desc_.render.tex_sheet_frame_curve;
}

void particle_emitter_component::set_texture_sheet_random_row(bool value)
{
    desc_.render.tex_sheet_random_row = value;
}

auto particle_emitter_component::get_texture_sheet_random_row() const -> bool
{
    return desc_.render.tex_sheet_random_row;
}

void particle_emitter_component::set_bounds_mode(ps_soa::bounds_mode mode)
{
    desc_.render.bounds_mode = mode;
}

auto particle_emitter_component::get_bounds_mode() const -> ps_soa::bounds_mode
{
    return desc_.render.bounds_mode;
}

void particle_emitter_component::set_fixed_bounds(const math::bbox& bounds)
{
    desc_.render.fixed_bounds = bounds;
}

auto particle_emitter_component::get_fixed_bounds() const -> const math::bbox&
{
    return desc_.render.fixed_bounds;
}

void particle_emitter_component::set_texture_sheet_randomize(bool randomize)
{
    desc_.render.tex_sheet_randomize = randomize;
}

auto particle_emitter_component::get_texture_sheet_randomize() const -> bool
{
    return desc_.render.tex_sheet_randomize;
}

void particle_emitter_component::set_culling_mode(culling_mode mode)
{
    culling_mode_ = mode;
}

auto particle_emitter_component::get_culling_mode() const -> culling_mode
{
    return culling_mode_;
}

void particle_emitter_component::set_last_render_frame(uint64_t frame)
{
    last_render_frame_ = frame;
}

auto particle_emitter_component::get_last_render_frame() const noexcept -> uint64_t
{
    return last_render_frame_;
}

auto particle_emitter_component::is_newly_created() const noexcept -> bool
{
    return last_render_frame_ == 0;
}

auto particle_emitter_component::was_used_last_frame() const noexcept -> bool
{
    const auto current_frame = uint64_t(gfx::get_render_frame());
    const bool is_new = is_newly_created();
    const bool was_used_recently = current_frame - last_render_frame_ <= 1;
    return is_new || was_used_recently;
}

void particle_emitter_component::set_render_mesh(const asset_handle<mesh>& value)
{
    render_mesh_ = value;
    is_render_mesh_dirty_ = true;
}

auto particle_emitter_component::get_render_mesh() const -> const asset_handle<mesh>&
{
    return render_mesh_;
}

void particle_emitter_component::set_render_material(const asset_handle<material>& value)
{
    render_material_ = value;
    is_render_mesh_dirty_ = true;
}

auto particle_emitter_component::get_render_material() const -> const asset_handle<material>&
{
    return render_material_;
}

void particle_emitter_component::set_mesh_cast_shadows(bool value)
{
    mesh_cast_shadows_ = value;
}

auto particle_emitter_component::get_mesh_cast_shadows() const -> bool
{
    return mesh_cast_shadows_;
}

auto particle_emitter_component::get_mesh_transforms() const -> const std::vector<math::mat4>&
{
    return mesh_transforms_;
}

auto particle_emitter_component::get_mesh_instance_stride() const -> uint32_t
{
    return mesh_node_transforms_.empty() ? 1u : uint32_t(mesh_node_transforms_.size());
}

auto particle_emitter_component::get_mesh_bounds() const -> const math::bbox&
{
    return mesh_bounds_;
}

auto particle_emitter_component::is_mesh_render() const -> bool
{
    return desc_.render.render_mode == ps_soa::render_mode::mesh && mesh_model_.is_valid();
}

namespace
{
/// Below this opacity a mesh particle is not drawn at all.
constexpr float k_min_mesh_opacity = 1.0f / 255.0f;
/// Below this opacity a mesh particle stops casting a shadow (shadows do not dither).
constexpr float k_min_mesh_shadow_opacity = 0.5f;

/// The instanced geometry's per-instance fade (its LOD crossfade term): 0 draws the whole mesh, a value in (0, 1]
/// keeps that share of its pixels by dithering.
auto get_mesh_fade(float opacity) -> float
{
    return opacity >= 1.0f ? 0.0f : opacity;
}

/// Calls @p visit(mesh, material, submesh index) for every non-skinned submesh of the model's mesh that has a
/// material.
template<typename Visitor>
void for_each_mesh_submesh(const model& mesh_model, Visitor&& visit)
{
    const auto mesh_asset = mesh_model.get_lod(0);
    const auto mesh_ptr = mesh_asset ? mesh_asset.get(false) : nullptr;
    if(!mesh_ptr)
    {
        return;
    }
    for(uint32_t group = 0; group < mesh_ptr->get_data_groups_count(); ++group)
    {
        const auto material_ptr = mesh_model.get_material_instance(group);
        if(!material_ptr)
        {
            continue;
        }
        for(const size_t submesh : mesh_ptr->get_non_skinned_submeshes_indices(group, 0))
        {
            visit(mesh_ptr, material_ptr, uint32_t(submesh));
        }
    }
}
} // namespace

void particle_emitter_component::collect_mesh_instances(batch_collector& collector, bool with_previous) const
{
    if(!is_mesh_render() || mesh_transforms_.empty())
    {
        return;
    }
    const uint32_t stride = get_mesh_instance_stride();
    const size_t particle_count = mesh_transforms_.size() / stride;
    for_each_mesh_submesh(mesh_model_,
                          [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
                          {
                              const batch_key key(mesh_ptr, material_ptr, 0, submesh);
                              if(!key.is_valid())
                              {
                                  return;
                              }
                              const size_t offset = stride == 1 ? 0 : submesh;
                              for(size_t particle = 0; particle < particle_count; ++particle)
                              {
                                  const float opacity = particle < mesh_opacity_.size() ? mesh_opacity_[particle] : 1.0f;
                                  if(opacity < k_min_mesh_opacity)
                                  {
                                      continue;
                                  }
                                  const size_t index = particle * stride + offset;
                                  batch_instance instance(&mesh_transforms_[index]);
                                  instance.lod_params.x = get_mesh_fade(opacity);
                                  if(with_previous)
                                  {
                                      instance.prev_world_transform_ptr = &prev_mesh_transforms_[index];
                                  }
                                  collector.collect_renderable(key, instance);
                              }
                          });
}

auto particle_emitter_component::collect_mesh_shadow_instances(std::vector<shadow_batch_collector>& collectors,
                                                               uint8_t view_mask) const -> bool
{
    if(!mesh_cast_shadows_ || !is_mesh_render() || mesh_transforms_.empty())
    {
        return false;
    }
    const uint32_t stride = get_mesh_instance_stride();
    const size_t particle_count = mesh_transforms_.size() / stride;
    bool is_collected = false;
    for_each_mesh_submesh(mesh_model_,
                          [&](const std::shared_ptr<mesh>& mesh_ptr, const material::sptr& material_ptr, uint32_t submesh)
                          {
                              const shadow_batch_key key = make_shadow_batch_key(mesh_ptr, 0, submesh, material_ptr);
                              if(!key.is_valid())
                              {
                                  return;
                              }
                              const size_t offset = stride == 1 ? 0 : submesh;
                              for(size_t view = 0; view < collectors.size(); ++view)
                              {
                                  if((view_mask & (1u << view)) == 0u)
                                  {
                                      continue;
                                  }
                                  for(size_t particle = 0; particle < particle_count; ++particle)
                                  {
                                      if(particle < mesh_opacity_.size() && mesh_opacity_[particle] < k_min_mesh_shadow_opacity)
                                      {
                                          continue;
                                      }
                                      collectors[view].collect_renderable(key,
                                                                          batch_instance(&mesh_transforms_[particle * stride + offset]));
                                  }
                                  is_collected = true;
                              }
                          });
    return is_collected;
}

namespace
{
/// The cutout polygons of the texture of asset @p key: decoded from its compiled file (what a game ships), or its
/// source while it is not compiled. Whole frames when neither decodes. Worker thread.
auto bake_texture_cutouts(const std::string& key, const ps_soa::cutout_sheet& sheet) -> std::vector<ps_soa::cutout_polygon>
{
    fs::error_code error;
    fs::path path = asset_reader::resolve_compiled_path<gfx::texture>(key);
    if(!fs::exists(path, error))
    {
        path = fs::absolute(fs::resolve_protocol(key));
    }
    bimg::ImageContainer* image = imageLoad(bx::FilePath(path.string().c_str()), bgfx::TextureFormat::RGBA8);
    if(image == nullptr)
    {
        APPLOG_WARNING("Particles: cannot read {} for its cutouts; drawing whole frames", key);
        return ps_soa::bake_cutouts(nullptr, 0, 0, sheet);
    }
    bimg::ImageMip mip{};
    std::vector<ps_soa::cutout_polygon> cutouts;
    if(bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip) && mip.m_format == bimg::TextureFormat::RGBA8)
    {
        cutouts = ps_soa::bake_cutouts(mip.m_data, mip.m_width, mip.m_height, sheet);
    }
    else
    {
        cutouts = ps_soa::bake_cutouts(nullptr, 0, 0, sheet);
    }
    bimg::imageFree(image);
    return cutouts;
}

auto combine_hash(uint64_t seed, uint64_t value) -> uint64_t
{
    constexpr uint64_t golden_ratio = 0x9e3779b97f4a7c15ull;
    return seed ^ (value + golden_ratio + (seed << 6) + (seed >> 2));
}
} // namespace

void particle_emitter_component::set_cutout_alpha_threshold(float value)
{
    desc_.render.cutout_alpha_threshold = math::clamp(value, 0.0f, 1.0f);
}

auto particle_emitter_component::get_cutout_alpha_threshold() const -> float
{
    return desc_.render.cutout_alpha_threshold;
}

void particle_emitter_component::refresh_cutout()
{
    if(!ps_soa::is_valid(emitter_handle_))
    {
        return;
    }
    const bool wants_cutout = desc_.render.primitive == ps_soa::particle_primitive::cutout &&
                              desc_.render.render_mode != ps_soa::render_mode::trail &&
                              desc_.render.render_mode != ps_soa::render_mode::ribbon && texture_.is_valid() &&
                              texture_.is_ready();
    uint64_t key = 0;
    ps_soa::cutout_sheet sheet;
    if(wants_cutout)
    {
        // The frames as the shaders play them (particle_sheet).
        const ps_soa::emitter_feature features = desc_.bake_features();
        const bool has_sheet = ps_soa::has_feature(features, ps_soa::emitter_feature::texsheet);
        sheet.tiles_x = has_sheet ? uint32_t(desc_.render.tex_sheet_tiles.x) : 1u;
        sheet.tiles_y = has_sheet ? uint32_t(desc_.render.tex_sheet_tiles.y) : 1u;
        sheet.alpha_threshold = desc_.render.cutout_alpha_threshold;
        sheet.is_blended = ps_soa::has_feature(features, ps_soa::emitter_feature::frame_blending) &&
                           (desc_.render.tex_sheet_cycles > 0.0f ||
                            ps_soa::has_feature(features, ps_soa::emitter_feature::texsheet_frame_curve));
        sheet.is_random_row = ps_soa::has_feature(features, ps_soa::emitter_feature::texsheet_random_row);
        sheet.is_single_pass = ps_soa::has_feature(features, ps_soa::emitter_feature::texsheet_single_pass);
        // The loaded texture object too: a reimport bakes again.
        uint64_t threshold_bits = 0;
        std::memcpy(&threshold_bits, &sheet.alpha_threshold, sizeof(sheet.alpha_threshold));
        key = std::hash<std::string>{}(texture_.id());
        key = combine_hash(key, uint64_t(reinterpret_cast<uintptr_t>(texture_.get(false).get())));
        key = combine_hash(key, uint64_t(sheet.tiles_x) | (uint64_t(sheet.tiles_y) << 32));
        key = combine_hash(key, threshold_bits);
        key = combine_hash(key, uint64_t(sheet.is_blended) | (uint64_t(sheet.is_random_row) << 1) |
                                    (uint64_t(sheet.is_single_pass) << 2));
        key = key == 0 ? 1 : key;
    }
    if(ps_soa::get_emitter_cutout(emitter_handle_) == key)
    {
        return;
    }
    ps_soa::set_emitter_cutout(emitter_handle_,
                               key,
                               [texture_key = texture_.id(), sheet]()
                               {
                                   return bake_texture_cutouts(texture_key, sheet);
                               });
}

void particle_emitter_component::refresh_render_mesh()
{
    if(desc_.render.render_mode != ps_soa::render_mode::mesh || !render_mesh_.is_valid())
    {
        if(mesh_model_.is_valid())
        {
            mesh_model_ = model{};
            render_mesh_source_ = nullptr;
        }
        return;
    }
    if(!render_mesh_.is_ready())
    {
        return;
    }
    const auto source = render_mesh_.get(false);
    if(!source || (source.get() == render_mesh_source_ && !is_render_mesh_dirty_))
    {
        return;
    }
    model built;
    built.set_lod(render_mesh_, 0);
    if(render_material_.is_valid())
    {
        for(uint32_t group = 0; group < source->get_data_groups_count(); ++group)
        {
            built.set_material(render_material_, group);
        }
    }
    else
    {
        built.apply_default_materials(engine::context().get_cached<asset_manager>(), false);
    }
    mesh_model_ = std::move(built);
    mesh_node_transforms_.clear();
    bool has_node_transforms = false;
    for(const auto& node : source->get_submesh_node_transforms(0))
    {
        mesh_node_transforms_.push_back(node.get_matrix());
        has_node_transforms = has_node_transforms || mesh_node_transforms_.back() != math::mat4(1.0f);
    }
    if(!has_node_transforms)
    {
        mesh_node_transforms_.clear();
    }
    const auto& bounds = source->get_bounds();
    mesh_radius_ = math::max(math::length(bounds.min), math::length(bounds.max));
    render_mesh_source_ = source.get();
    is_render_mesh_dirty_ = false;
    // This frame's instances were laid out for the previous mesh's submeshes.
    update_mesh_instances();
}

void particle_emitter_component::update_mesh_instances()
{
    mesh_bounds_.reset();
    if(!is_mesh_render())
    {
        mesh_transforms_.clear();
        prev_mesh_transforms_.clear();
        mesh_opacity_.clear();
        return;
    }
    ps_soa::collect_particle_mesh_transforms(emitter_handle_, particle_transforms_, prev_particle_transforms_, mesh_opacity_);
    for(const auto& transform : particle_transforms_)
    {
        const float scale = math::max(math::length(math::vec3(transform[0])),
                                      math::max(math::length(math::vec3(transform[1])), math::length(math::vec3(transform[2]))));
        const math::vec3 reach(mesh_radius_ * scale);
        mesh_bounds_.add_point(math::vec3(transform[3]) - reach);
        mesh_bounds_.add_point(math::vec3(transform[3]) + reach);
    }
    if(mesh_node_transforms_.empty())
    {
        mesh_transforms_.swap(particle_transforms_);
        prev_mesh_transforms_.swap(prev_particle_transforms_);
        return;
    }
    // Every submesh of every particle: the particle's matrix on top of the submesh's node transform.
    mesh_transforms_.clear();
    prev_mesh_transforms_.clear();
    for(size_t particle = 0; particle < particle_transforms_.size(); ++particle)
    {
        for(const auto& node : mesh_node_transforms_)
        {
            mesh_transforms_.push_back(particle_transforms_[particle] * node);
            prev_mesh_transforms_.push_back(prev_particle_transforms_[particle] * node);
        }
    }
}

void particle_emitter_component::update_emitter(const math::transform& world_transform, delta_t dt)
{
    is_simulated_ = false;
    if(!ps_soa::is_valid(emitter_handle_) || !enabled_)
    {
        return;
    }
    transform_state_.current = world_transform;
    const auto frame = uint64_t(gfx::get_render_frame());
    // Updates were skipped (the entity was inactive or the emitter disabled): where it was then is not
    // where it moved from, and a path from there would fill with particles.
    if(last_update_frame_ != 0 && frame - last_update_frame_ > 1)
    {
        transform_state_.previous = world_transform;
    }
    last_update_frame_ = frame;
    bool should_simulate = true;
    if(culling_mode_ != culling_mode::always_simulate && !was_used_last_frame() &&
       !ps_soa::has_pending_bursts(emitter_handle_))
    {
        should_simulate = false;
    }
    is_simulated_ = should_simulate;
    if(should_simulate)
    {
        ps_soa::update_emitter(emitter_handle_, dt.count(), desc_, transform_state_, desc_.playback);
    }
    else
    {
        const bool catch_up = culling_mode_ == culling_mode::renderer_based;
        ps_soa::update_emitter_frozen(emitter_handle_, dt.count(), desc_, transform_state_, catch_up);
    }
    update_mesh_instances();
}

void particle_emitter_component::emit(uint32_t count, const ps_soa::emit_params& params)
{
    if(!enabled_ || !ps_soa::is_valid(emitter_handle_))
    {
        return;
    }
    ps_soa::emit(emitter_handle_, count, params);
}

auto particle_emitter_component::is_simulated() const noexcept -> bool
{
    return is_simulated_;
}

auto particle_emitter_component::get_desc() const -> const ps_soa::emitter_desc&
{
    return desc_;
}

void particle_emitter_component::recreate_emitter()
{
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::destroy_emitter(emitter_handle_);
        emitter_handle_ = {};
    }
    emitter_handle_ = ps_soa::create_emitter(max_particles_);
    if(!ps_soa::is_valid(emitter_handle_))
    {
        APPLOG_ERROR("Failed to create particle emitter");
        return;
    }
    ps_soa::set_emitter_sort_backend(emitter_handle_, sort_backend_);
}

void particle_emitter_component::reset_emitter()
{
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::reset_emitter(emitter_handle_);
    }
}

void particle_emitter_component::set_simulation_space(ps_soa::simulation_space space)
{
    // The live particles were placed in the other space: they go when it changes.
    if(desc_.motion.space == space)
    {
        return;
    }
    desc_.motion.space = space;
    reset_emitter();
}

auto particle_emitter_component::get_simulation_space() const -> ps_soa::simulation_space
{
    return desc_.motion.space;
}

void particle_emitter_component::set_sort_backend(ps_soa::particle_sort_backend backend)
{
    sort_backend_ = backend;
    if(ps_soa::is_valid(emitter_handle_))
    {
        ps_soa::set_emitter_sort_backend(emitter_handle_, sort_backend_);
    }
}

auto particle_emitter_component::get_sort_backend() const -> ps_soa::particle_sort_backend
{
    return sort_backend_;
}

} // namespace unravel
