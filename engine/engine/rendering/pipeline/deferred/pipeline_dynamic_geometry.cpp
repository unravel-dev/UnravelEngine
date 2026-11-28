#include "pipeline_internal.h"

#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/dynamic_geometry/dynamic_geometry_draw.h>
#include <engine/rendering/ecs/components/light_component.h>
#include <engine/rendering/ecs/components/line_component.h>
#include <engine/rendering/ecs/components/trail_component.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/render_change.h>

#define XXH_INLINE_ALL
#include <filesystem/xxhash.h>
#include <logging/logging.h>

#include <cmath>
#include <cstring>

namespace unravel
{
namespace rendering
{

namespace
{
/// The casters' geometry is placed relative to a corner of the cell of this size, in metres, the camera stands in:
/// while the camera stays in the cell, geometry that holds still packs to the same bytes, and its shadow maps are
/// reused.
constexpr float DYNAMIC_SHADOW_ORIGIN_CELL = 64.0f;
/// The casters of one material are split by the cell of this size, in metres, they stand in: a light draws the ones
/// near it, and one that moves leaves the shadow maps of the others alone.
constexpr float DYNAMIC_SHADOW_CASTER_CELL = 32.0f;

/// @p box grown by @p margin on every side.
auto grow_dynamic_bounds(const math::bbox& box, float margin) -> math::bbox
{
    return margin > 0.0f ? math::bbox(box.min - math::vec3(margin), box.max + math::vec3(margin)) : box;
}

/// Whether @p first and @p second draw the same: one material, or two whose shader and values are the same.
auto shares_dynamic_draw(const graph_material* first, uint64_t first_hash, const graph_material* second, uint64_t second_hash) -> bool
{
    return first == second || (first_hash == second_hash && first->binds_same_draw(*second));
}

/// The corner of the DYNAMIC_SHADOW_ORIGIN_CELL cell @p position stands in.
auto get_dynamic_shadow_origin(const math::vec3& position) -> math::vec3
{
    return math::floor(position / DYNAMIC_SHADOW_ORIGIN_CELL) * DYNAMIC_SHADOW_ORIGIN_CELL;
}

/// A key of @p draw_hash in the DYNAMIC_SHADOW_CASTER_CELL cell @p position stands in.
auto get_dynamic_caster_key(uint64_t draw_hash, const math::vec3& position) -> uint64_t
{
    const math::vec3 cell = math::floor(position / DYNAMIC_SHADOW_CASTER_CELL);
    const int32_t place[3] = {int32_t(cell.x), int32_t(cell.y), int32_t(cell.z)};
    return XXH3_64bits_withSeed(place, sizeof(place), draw_hash);
}

/// A key of what a caster draws into a shadow map: its vertices and triangles, where they are placed, and what a
/// shadow reads of its material (its mask's values and textures, its sides). A mask that animates changes it every
/// frame.
auto compute_dynamic_shadow_key(const dynamic_geometry& geometry, const math::vec3& origin, const graph_material& graph) -> uint64_t
{
    uint64_t key = XXH3_64bits(geometry.get_vertex_data(), geometry.get_vertex_data_size());
    key = XXH3_64bits_withSeed(geometry.get_indices().data(), geometry.get_indices().size() * sizeof(uint32_t), key);
    const float placed[3] = {origin.x, origin.y, origin.z};
    key = XXH3_64bits_withSeed(placed, sizeof(placed), key);
    render_change_values material_keys{};
    graph.add_change_keys(material_keys);
    for_each_aspect(render_interest::shadow_caster & render_change::material_aspects,
                    [&](uint32_t aspect)
                    {
                        key = XXH3_64bits_withSeed(&material_keys[aspect], sizeof(material_keys[aspect]), key);
                    });
    if(graph.is_shadow_animated())
    {
        const uint64_t frame = gfx::get_render_frame();
        key = XXH3_64bits_withSeed(&frame, sizeof(frame), key);
    }
    return key;
}

/// Where the shadows of a frame's lights are drawn from: around each local light that casts them, and for a
/// directional light everything whose shadow, which runs along the light, comes within its shadow distance of the
/// camera. A caster outside all of it is not built.
struct dynamic_shadow_reach
{
    struct sun
    {
        /// The way the light shines, and how far from the camera its shadows are drawn, in metres.
        math::vec3 direction{0.0f, -1.0f, 0.0f};
        float distance = 0.0f;
    };

    math::vec3 camera_position{0.0f};
    std::vector<sun> suns;
    std::vector<math::bsphere> local_lights;

    auto is_empty() const -> bool
    {
        return suns.empty() && local_lights.empty();
    }

    /// Whether a caster within @p box can put a shadow where one is drawn.
    auto holds(const math::bbox& box) const -> bool
    {
        const math::vec3 center = box.get_center();
        const float radius = math::length(box.max - box.min) * 0.5f;
        for(const auto& light : local_lights)
        {
            if(math::distance(center, light.position) <= light.radius + radius)
            {
                return true;
            }
        }
        for(const auto& light : suns)
        {
            // The shadow leaves the caster along the light: where on that way it comes nearest the camera.
            const float along = std::max(math::dot(camera_position - center, light.direction), 0.0f);
            if(math::distance(center + light.direction * along, camera_position) <= light.distance + radius)
            {
                return true;
            }
        }
        return false;
    }
};

/// How far from @p cam the farthest point it sees at view depth @p depth is, in metres: a corner of its frustum there.
auto get_view_reach(const camera& cam, float depth) -> float
{
    const auto& projection = cam.get_projection();
    // Half the view's width and height: at depth 1 through a perspective, anywhere through an orthographic view.
    const float half_width = 1.0f / std::max(std::abs(projection[0][0]), 1.0e-6f);
    const float half_height = 1.0f / std::max(std::abs(projection[1][1]), 1.0e-6f);
    const bool is_orthographic = cam.get_projection_mode() == projection_mode::orthographic;
    const float spread = is_orthographic ? 1.0f : depth;
    return math::length(math::vec3(half_width * spread, half_height * spread, depth));
}

/// The reach of the lights of @p registry that shine and cast shadows, for the camera @p cam, into @p reach, which
/// keeps its memory between frames.
void find_dynamic_shadow_reach(entt::registry& registry, const camera& cam, dynamic_shadow_reach& reach)
{
    reach.camera_position = cam.get_position();
    reach.suns.clear();
    reach.local_lights.clear();
    registry.view<transform_component, light_component, active_component>().each(
        [&](auto /*entity*/, auto&& transform, auto&& light_comp, auto&& /*active*/)
        {
            const auto& light = light_comp.get_light();
            if(!light_comp.is_enabled() || !light.casts_shadows)
            {
                return;
            }
            const auto frame = light_component::get_light_frame(transform.get_transform_global());
            if(light.type == light_type::directional)
            {
                // Its shadows are drawn up to the shadow distance into the view.
                reach.suns.push_back({frame.z_unit_axis(), get_view_reach(cam, light.shadow_params.far_plane)});
                return;
            }
            reach.local_lights.push_back(light_comp.get_world_bounds_sphere(frame));
        });
}

/// Says once that a frame's transient buffers held no room for a draw of lines and trails.
void report_dropped_dynamic_draw()
{
    static bool is_reported = false;
    if(!is_reported)
    {
        is_reported = true;
        APPLOG_WARNING("Lines and trails were left out of a frame. Their triangles did not fit the frame's transient buffers.");
    }
}

/// Whether @p registry has a Line or a Trail at all.
auto has_dynamic_renderers(entt::registry& registry) -> bool
{
    return !registry.storage<line_component>().empty() || !registry.storage<trail_component>().empty();
}
} // namespace

auto deferred::dynamic_shadow_caster::bind_shadow_geometry() const -> bool
{
    const uint32_t frame = gfx::get_render_frame();
    if(uploaded_frame != frame)
    {
        if(!upload_dynamic_geometry(geometry, vertices, indices))
        {
            return false;
        }
        uploaded_frame = frame;
    }
    bgfx::setVertexBuffer(0, &vertices, 0, geometry.get_vertex_count());
    bgfx::setIndexBuffer(&indices, 0, geometry.get_index_count());
    const math::mat4 world = get_dynamic_geometry_world(origin);
    gfx::set_world_transform(math::value_ptr(world));
    return true;
}

auto deferred::dynamic_shadow_caster::get_shadow_material() const -> const material&
{
    return *drawn;
}

auto deferred::dynamic_shadow_caster::get_shadow_key() const -> uint64_t
{
    return key;
}

void deferred::release_dynamic_draws(std::vector<dynamic_draw>& draws, size_t& count)
{
    for(size_t i = 0; i < count; ++i)
    {
        draws[i].material.reset();
        draws[i].graph = nullptr;
    }
    count = 0;
}

auto deferred::acquire_dynamic_draw(std::vector<dynamic_draw>& draws, size_t& count, const dynamic_renderer& renderer) -> dynamic_draw&
{
    if(count == draws.size())
    {
        draws.emplace_back();
    }
    auto& draw = draws[count++];
    // The geometry keeps its memory from the frame before.
    draw.geometry.clear();
    draw.material = renderer.get_drawn_material();
    draw.graph = renderer.get_drawn_graph();
    draw.draw_hash = renderer.get_draw_hash();
    draw.world_bounds.reset();
    draw.sort_priority = renderer.sort_priority;
    return draw;
}

auto deferred::find_dynamic_opaque_draw(const dynamic_renderer& renderer) -> dynamic_draw&
{
    const auto* graph = renderer.get_drawn_graph();
    const uint64_t hash = renderer.get_draw_hash();
    // Neighbours in the component's storage mostly draw the same material.
    if(dynamic_opaque_count_ > 0)
    {
        auto& last = dynamic_opaque_[dynamic_opaque_count_ - 1];
        if(shares_dynamic_draw(last.graph, last.draw_hash, graph, hash))
        {
            return last;
        }
    }
    const auto found = dynamic_opaque_lookup_.find(hash);
    if(found != dynamic_opaque_lookup_.end() && shares_dynamic_draw(dynamic_opaque_[found->second].graph, hash, graph, hash))
    {
        return dynamic_opaque_[found->second];
    }
    dynamic_opaque_lookup_[hash] = dynamic_opaque_count_;
    return acquire_dynamic_draw(dynamic_opaque_, dynamic_opaque_count_, renderer);
}

auto deferred::begin_dynamic_draw(const run_context& run,
                                  entt::entity entity,
                                  dynamic_renderer& renderer,
                                  const asset_handle<material>& fallback,
                                  const math::bbox& world_bounds) -> dynamic_draw*
{
    if(!world_bounds.is_populated() || !pipeline::is_on_rendered_layer(*run.scn.registry, entity, run.render_mask))
    {
        return nullptr;
    }
    renderer.refresh_material(fallback);
    const auto* graph = renderer.get_drawn_graph();
    if(graph == nullptr)
    {
        return nullptr;
    }
    // A World Position Offset moves the vertices out of the box they are built in.
    if(!run.cam.test_aabb(grow_dynamic_bounds(world_bounds, graph->get_max_world_position_offset())))
    {
        return nullptr;
    }
    if(graph->is_forward())
    {
        // One entry each: the translucency pass sorts them among its other draws.
        return &acquire_dynamic_draw(dynamic_forward_, dynamic_forward_count_, renderer);
    }
    // Everything of a material that goes into the G-buffer is one draw.
    return &find_dynamic_opaque_draw(renderer);
}

void deferred::end_dynamic_draw(dynamic_draw& draw, uint32_t index_count_before, const math::bbox& world_bounds)
{
    const bool has_built = draw.geometry.get_index_count() > index_count_before;
    if(has_built)
    {
        draw.world_bounds.add_point(world_bounds.min);
        draw.world_bounds.add_point(world_bounds.max);
        stats_.drawn_dynamic_renderers++;
        return;
    }
    // A renderer with nothing to draw (a line of one point, a trail that has not moved) leaves no entry of its own.
    const bool is_last_forward = dynamic_forward_count_ > 0 && &draw == &dynamic_forward_[dynamic_forward_count_ - 1];
    if(is_last_forward)
    {
        draw.material.reset();
        draw.graph = nullptr;
        --dynamic_forward_count_;
    }
}

void deferred::collect_dynamic_geometry(const run_context& run)
{
    release_dynamic_draws(dynamic_opaque_, dynamic_opaque_count_);
    release_dynamic_draws(dynamic_forward_, dynamic_forward_count_);
    dynamic_opaque_lookup_.clear();
    auto& registry = *run.scn.registry;
    // A capture would keep one moment of what moves: a probe draws none of them.
    if(!run.is_camera_run() || !has_dynamic_renderers(registry))
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Dynamic Geometry/Build");
    const auto& strip_material = dynamic_renderer::default_strip_material();
    // The geometry is placed relative to the camera, so its numbers stay small far from the world's origin.
    const math::vec3 origin = run.cam.get_position();
    dynamic_origin_ = origin;
    registry.view<transform_component, line_component, active_component>().each(
        [&](auto entity, auto&& transform, auto&& line, auto&& /*active*/)
        {
            const auto& world = transform.get_transform_global();
            const math::bbox bounds = line.get_world_bounds(world);
            if(auto* draw = begin_dynamic_draw(run, entity, line, strip_material, bounds))
            {
                const uint32_t index_count = draw->geometry.get_index_count();
                line.build(world, origin, origin, draw->geometry);
                end_dynamic_draw(*draw, index_count, bounds);
            }
        });
    registry.view<transform_component, trail_component, active_component>().each(
        [&](auto entity, auto&& transform, auto&& trail, auto&& /*active*/)
        {
            const auto& world = transform.get_transform_global();
            const math::bbox bounds = trail.get_world_bounds(world);
            if(auto* draw = begin_dynamic_draw(run, entity, trail, strip_material, bounds))
            {
                const uint32_t index_count = draw->geometry.get_index_count();
                trail.build(world, origin, origin, draw->geometry);
                end_dynamic_draw(*draw, index_count, bounds);
            }
        });
}

auto deferred::has_dynamic_opaque_geometry() const -> bool
{
    for(size_t i = 0; i < dynamic_opaque_count_; ++i)
    {
        if(!dynamic_opaque_[i].geometry.is_empty())
        {
            return true;
        }
    }
    return false;
}

void deferred::submit_dynamic_geometry(gfx::render_pass& pass, const camera& camera)
{
    if(dynamic_opaque_count_ == 0)
    {
        return;
    }
    geom_program& prog = geom_program_;
    const auto camera_pos = camera.get_position();
    const auto clip_planes = pipeline::get_material_clip_planes(camera);
    // No LOD transition dithers it.
    const auto lod_params = math::vec3{0.0f, -1.0f, 1.0f};
    const math::mat4 world = get_dynamic_geometry_world(dynamic_origin_);
    for(size_t i = 0; i < dynamic_opaque_count_; ++i)
    {
        const auto& draw = dynamic_opaque_[i];
        // While its shader is on its way a material draws nothing here: the stand-in surface would show a masked strip
        // as a solid band.
        auto* graph_program = draw.geometry.is_empty() ? nullptr
                                                       : graph_material_programs_.begin(*draw.graph, prog.vertex_variant, prog.graph_fragment_variant);
        if(graph_program == nullptr)
        {
            continue;
        }
        if(!bind_dynamic_geometry(draw.geometry))
        {
            report_dropped_dynamic_draw();
            bgfx::discard();
            continue;
        }
        gfx::set_uniform(prog.u_camera_wpos, camera_pos);
        gfx::set_uniform(prog.u_camera_clip_planes, clip_planes);
        gfx::set_uniform(prog.u_lod_params, lod_params);
        gfx::set_world_transform(math::value_ptr(world));
        submit_graph_material(prog, *draw.graph);
        bgfx::submit(pass.id, graph_program->native_handle(), 0, BGFX_DISCARD_ALL);
        stats_.drawn_dynamic_batches++;
    }
}

void deferred::draw_translucent_dynamic(bgfx::ViewId view, size_t index, const run_context& run)
{
    const auto& draw = dynamic_forward_[index];
    if(draw.graph == nullptr)
    {
        return;
    }
    if(!bind_dynamic_geometry(draw.geometry))
    {
        report_dropped_dynamic_draw();
        return;
    }
    geom_program& prog = geom_program_;
    gfx::set_uniform(prog.u_camera_wpos, run.cam.get_position());
    gfx::set_uniform(prog.u_camera_clip_planes, pipeline::get_material_clip_planes(run.cam));
    gfx::set_uniform(prog.u_lod_params, math::vec3{0.0f, -1.0f, 1.0f});
    const math::mat4 world = get_dynamic_geometry_world(dynamic_origin_);
    gfx::set_world_transform(math::value_ptr(world));
    // The radiance cache keeps no probes around a strip: it takes the sky and its reflection probe. It
    // draws in the sorted list, whatever its material's Order Independent setting.
    stats_.drawn_dynamic_batches +=
        submit_forward_surface(view, *draw.graph, prog, draw.world_bounds, dynamic_forward_probes_[index], false, false, false, BGFX_DISCARD_ALL);
}

auto deferred::find_dynamic_shadow_caster(const dynamic_renderer& renderer, const math::bbox& world_bounds) -> dynamic_shadow_caster&
{
    const auto* graph = renderer.get_drawn_graph();
    const uint64_t hash = renderer.get_draw_hash();
    const uint64_t key = get_dynamic_caster_key(hash, world_bounds.get_center());
    const auto found = dynamic_shadow_lookup_.find(key);
    if(found != dynamic_shadow_lookup_.end())
    {
        auto& caster = *dynamic_shadow_casters_[found->second];
        if(shares_dynamic_draw(caster.graph, caster.draw_hash, graph, hash))
        {
            return caster;
        }
    }
    if(dynamic_shadow_caster_count_ == dynamic_shadow_casters_.size())
    {
        dynamic_shadow_casters_.push_back(std::make_unique<dynamic_shadow_caster>());
    }
    dynamic_shadow_lookup_[key] = dynamic_shadow_caster_count_;
    auto& caster = *dynamic_shadow_casters_[dynamic_shadow_caster_count_++];
    // The geometry keeps its memory from the frame before.
    caster.geometry.clear();
    caster.drawn = renderer.get_drawn_material();
    caster.graph = graph;
    caster.draw_hash = hash;
    caster.uploaded_frame = UINT32_MAX;
    return caster;
}

auto deferred::begin_dynamic_shadow_caster(const run_context& run,
                                           entt::entity entity,
                                           dynamic_renderer& renderer,
                                           const asset_handle<material>& fallback,
                                           const math::bbox& world_bounds) -> dynamic_shadow_caster*
{
    if(!renderer.casts_shadow || !world_bounds.is_populated() ||
       !pipeline::is_on_rendered_layer(*run.scn.registry, entity, run.render_mask))
    {
        return nullptr;
    }
    renderer.refresh_material(fallback);
    const auto* graph = renderer.get_drawn_graph();
    if(graph == nullptr || !graph->casts_shadow())
    {
        return nullptr;
    }
    return &find_dynamic_shadow_caster(renderer, world_bounds);
}

void deferred::gather_dynamic_shadow_casters(const run_context& run)
{
    // The materials of the casters before are released here, when this frame has none.
    for(size_t i = 0; i < dynamic_shadow_caster_count_; ++i)
    {
        dynamic_shadow_casters_[i]->drawn.reset();
        dynamic_shadow_casters_[i]->graph = nullptr;
    }
    dynamic_shadow_caster_count_ = 0;
    dynamic_shadow_lookup_.clear();
    auto& registry = *run.scn.registry;
    if(!run.runs_shadow_step() || !has_dynamic_renderers(registry))
    {
        return;
    }
    const math::vec3 camera_position = run.cam.get_position();
    // Keeps its memory between frames.
    thread_local dynamic_shadow_reach reach;
    find_dynamic_shadow_reach(registry, run.cam, reach);
    if(reach.is_empty())
    {
        return;
    }
    APP_SCOPE_PERF("Rendering/Dynamic Geometry/Shadow Build");
    const auto& strip_material = dynamic_renderer::default_strip_material();
    const math::vec3 origin = get_dynamic_shadow_origin(camera_position);
    // The caster a renderer within @p bounds builds into: none for one that casts nothing or stands where no light
    // draws a shadow from.
    const auto begin_caster = [&](entt::entity entity, dynamic_renderer& renderer, const math::bbox& bounds) -> dynamic_shadow_caster*
    {
        const bool is_in_reach = bounds.is_populated() && reach.holds(bounds);
        return is_in_reach ? begin_dynamic_shadow_caster(run, entity, renderer, strip_material, bounds) : nullptr;
    };
    registry.view<transform_component, line_component, active_component>().each(
        [&](auto entity, auto&& transform, auto&& line, auto&& /*active*/)
        {
            const auto& world = transform.get_transform_global();
            if(auto* caster = begin_caster(entity, line, line.get_world_bounds(world)))
            {
                line.build(world, camera_position, origin, caster->geometry);
            }
        });
    registry.view<transform_component, trail_component, active_component>().each(
        [&](auto entity, auto&& transform, auto&& trail, auto&& /*active*/)
        {
            const auto& world = transform.get_transform_global();
            if(auto* caster = begin_caster(entity, trail, trail.get_world_bounds(world)))
            {
                trail.build(world, camera_position, origin, caster->geometry);
            }
        });
    for(size_t i = 0; i < dynamic_shadow_caster_count_; ++i)
    {
        auto& caster = *dynamic_shadow_casters_[i];
        if(caster.geometry.is_empty())
        {
            continue;
        }
        caster.origin = origin;
        caster.key = compute_dynamic_shadow_key(caster.geometry, origin, *caster.graph);
        // The box of what was built, grown by what a World Position Offset moves it.
        const auto& built = caster.geometry.get_bounds();
        shadow::shadow_visibility_data entry;
        entry.world_bounds =
            grow_dynamic_bounds(math::bbox(built.min + origin, built.max + origin), caster.graph->get_max_world_position_offset());
        entry.geometry = &caster;
        instance_source_casters_.push_back(entry);
    }
}

} // namespace rendering
} // namespace unravel
