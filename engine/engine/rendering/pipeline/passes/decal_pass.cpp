#include "decal_pass.h"
#include "mask_pass.h"

#include "depth_copy_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/ecs/components/basic_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/scene.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/pipeline/pipeline.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_compiler.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/vertex_decl.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace unravel
{

namespace
{
/// The vec4s of one decal's instance data (decal/vs_decal.eshader): its box's local-to-world and world-to-local rows,
/// its fade, its angle fade and its first four parameter registers.
constexpr uint16_t DECAL_INSTANCE_VEC4S = 12;
constexpr uint16_t DECAL_INSTANCE_STRIDE = DECAL_INSTANCE_VEC4S * sizeof(math::vec4);
/// The TAA mip bias the G-buffer's materials read their textures at while the view jitters
/// (pipeline_geometry.cpp, make_geometry_clip_planes).
constexpr float DECAL_TAA_MIP_BIAS = -0.3f;
/// Stages of the view textures fs_material_graph_decal.sh reads (mg_decal.sh).
constexpr uint8_t DECAL_DEPTH_STAGE = 7;
constexpr uint8_t DECAL_GBUFFER0_STAGE = 13;
constexpr uint8_t DECAL_GBUFFER1_STAGE = 14;
constexpr uint8_t DECAL_LAYERS_STAGE = 15;
/// Every layer: a decal of these Receiver Layers needs no layer test.
constexpr uint32_t DECAL_ALL_LAYERS = 0xFFFFFFFFu;
constexpr uint32_t DECAL_POINT_SAMPLER = BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP;
/// How far past the box, in near-plane lengths, the camera counts as inside it: its near plane may cut the box.
constexpr float DECAL_INSIDE_NEAR_MARGIN = 2.0f;
/// Pixels added around the decals' rectangle: the projection the rectangle comes from jitters under TAA.
constexpr float DECAL_RECT_MARGIN = 2.0f;

/// Row @p row of @p m.
auto decal_matrix_row(const math::mat4& m, int row) -> math::vec4
{
    return {m[0][row], m[1][row], m[2][row], m[3][row]};
}

/// The corners of the unit box, -0.5 to 0.5: corner i has x from bit 0, y from bit 1, z from bit 2.
auto get_decal_box_corner(uint32_t index) -> math::vec3
{
    return {(index & 1u) != 0 ? 0.5f : -0.5f, (index & 2u) != 0 ? 0.5f : -0.5f, (index & 4u) != 0 ? 0.5f : -0.5f};
}

/// The box's triangles, two per face, each wound so its normal points out of the box (clockwise on the screen seen
/// from outside, as the materials' front faces are).
constexpr std::array<uint16_t, 36> DECAL_BOX_INDICES = {
    0, 2, 3, 0, 3, 1, // -Z
    4, 5, 7, 4, 7, 6, // +Z
    0, 4, 6, 0, 6, 2, // -X
    1, 3, 7, 1, 7, 5, // +X
    0, 1, 5, 0, 5, 4, // -Y
    2, 6, 7, 2, 7, 3, // +Y
};

/// The share of the view's height a sphere of @p radius at @p distance covers.
auto get_decal_screen_share(float radius, float distance, float projection_scale, bool is_perspective) -> float
{
    return is_perspective ? radius * projection_scale / std::max(distance, 0.0001f) : radius * projection_scale;
}

/// 0 below half of @p screen_size, 1 from it up, linear between: how much of a decal of @p share shows.
auto get_decal_screen_fade(float share, float screen_size) -> float
{
    if(screen_size <= 0.0f)
    {
        return 1.0f;
    }
    return std::clamp((share - screen_size * 0.5f) / (screen_size * 0.5f), 0.0f, 1.0f);
}

/// The material a decal's draw binds, and whether it is the shared one: a decal sharing its material's draw takes the
/// shared material (its first registers ride in the instance); one whose overrides cannot takes its own.
auto get_decal_program_material(decal_component& decal, const std::shared_ptr<material>& shared) -> std::shared_ptr<material>
{
    auto& instance = decal.get_material_instance();
    if(!instance.has_overrides())
    {
        return shared;
    }
    if(!instance.get_resolved())
    {
        instance.update_resolved();
    }
    const auto& resolved = instance.get_resolved();
    if(!resolved || instance.get_resolved_shares_draw())
    {
        return shared;
    }
    return resolved;
}
} // namespace

auto decal_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Before any program: OpenGL wires a program to the uniforms registered when it links.
    uniforms_.cache_uniforms();
    copy_program_.cache_uniforms();
    programs_.init(am.get_asset<gfx::shader>("engine:/data/shaders/decal/vs_decal.eshader"), material_domain::decal);

    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    auto fs_resolve = am.get_asset<gfx::shader>("engine:/data/shaders/decal/fs_decal_resolve.eshader");
    copy_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_resolve);
    color_program_.cache_uniforms();
    color_program_.program = std::make_unique<gpu_program>(vs_clip_quad, gfx::shader::DEFAULT_VARIANT, fs_resolve, "color");
    surface_program_.cache_uniforms();
    surface_program_.program = std::make_unique<gpu_program>(vs_clip_quad, gfx::shader::DEFAULT_VARIANT, fs_resolve, "surface");
    // Built only while the overdraw view is on.
    overdraw_program_ = std::make_unique<gpu_program>(am.get_asset<gfx::shader>("engine:/data/shaders/decal/vs_decal.eshader"),
                                                      gfx::shader::DEFAULT_VARIANT,
                                                      am.get_asset<gfx::shader>("engine:/data/shaders/decal/fs_decal_overdraw.eshader"),
                                                      gfx::shader::DEFAULT_VARIANT,
                                                      false);

    std::array<gfx::pos_vertex, 8> vertices{};
    for(uint32_t i = 0; i < vertices.size(); ++i)
    {
        const auto corner = get_decal_box_corner(i);
        vertices[i].x = corner.x;
        vertices[i].y = corner.y;
        vertices[i].z = corner.z;
    }
    box_vertices_ = bgfx::createVertexBuffer(bgfx::copy(vertices.data(), uint32_t(sizeof(vertices))), gfx::pos_vertex::get_layout());
    box_indices_ = bgfx::createIndexBuffer(bgfx::copy(DECAL_BOX_INDICES.data(), uint32_t(sizeof(DECAL_BOX_INDICES))));
    return copy_program_.program->is_valid() && color_program_.program->is_valid() && surface_program_.program->is_valid();
}

void decal_pass::deinit()
{
    if(bgfx::isValid(box_vertices_))
    {
        bgfx::destroy(box_vertices_);
        box_vertices_ = BGFX_INVALID_HANDLE;
    }
    if(bgfx::isValid(box_indices_))
    {
        bgfx::destroy(box_indices_);
        box_indices_ = BGFX_INVALID_HANDLE;
    }
    items_.clear();
}

void decal_pass::collect(scene& scn, const camera& cam, const feature_limits& limits, layer_mask render_mask)
{
    items_.clear();
    wants_full_path_ = false;
    layer_bounds_.clear();
    stats_ = {};
    if(!limits.decals)
    {
        return;
    }
    const auto& frustum = cam.get_frustum();
    const math::vec3 camera_position = cam.get_position();
    const bool is_perspective = cam.get_projection_mode() == projection_mode::perspective;
    const float projection_scale = cam.get_projection().get_matrix()[1][1];
    const float near_clip = cam.get_near_clip();
    const auto view_proj = cam.get_view_projection().get_matrix();
    math::vec2 rect_min(1.0f);
    math::vec2 rect_max(0.0f);

    scn.registry->view<transform_component, decal_component, active_component>().each(
        [&](entt::entity entity, transform_component& transform_comp, decal_component& decal, active_component&)
        {
            const auto* layer = scn.registry->try_get<layer_component>(entity);
            if(layer != nullptr && (layer->layers.mask & render_mask.mask) == 0)
            {
                return;
            }
            const auto receiver_layers = uint32_t(decal.receiver_layers.mask);
            if(receiver_layers == 0)
            {
                return;
            }
            const auto shared = decal.get_material().try_get();
            const auto* graph = shared ? shared->safe_cast<graph_material>() : nullptr;
            if(graph == nullptr || graph->get_domain() != material_domain::decal || !graph->is_ready_to_draw())
            {
                return;
            }
            const math::mat4 box_to_world = decal.get_box_to_world(transform_comp.get_transform_global());
            math::vec3 world_min(std::numeric_limits<float>::max());
            math::vec3 world_max(std::numeric_limits<float>::lowest());
            bool is_behind_camera = false;
            math::vec2 corner_min(1.0f);
            math::vec2 corner_max(0.0f);
            for(uint32_t i = 0; i < 8; ++i)
            {
                const math::vec3 corner = math::vec3(box_to_world * math::vec4(get_decal_box_corner(i), 1.0f));
                world_min = math::min(world_min, corner);
                world_max = math::max(world_max, corner);
                const math::vec4 clip = view_proj * math::vec4(corner, 1.0f);
                if(clip.w <= 0.0001f)
                {
                    is_behind_camera = true;
                    continue;
                }
                // The screen's rectangle from the top-left corner, as scissors take it.
                const math::vec2 screen(clip.x / clip.w * 0.5f + 0.5f, 0.5f - clip.y / clip.w * 0.5f);
                corner_min = math::min(corner_min, screen);
                corner_max = math::max(corner_max, screen);
            }
            if(!frustum.test_aabb(math::bbox(world_min, world_max)))
            {
                return;
            }
            const math::vec3 center = math::vec3(box_to_world[3]);
            const math::vec3 axis_x = math::vec3(box_to_world[0]);
            const math::vec3 axis_y = math::vec3(box_to_world[1]);
            const math::vec3 axis_z = math::vec3(box_to_world[2]);
            const float radius = 0.5f * std::sqrt(math::dot(axis_x, axis_x) + math::dot(axis_y, axis_y) + math::dot(axis_z, axis_z));
            const float distance = math::length(center - camera_position);
            float fade = std::clamp(decal.opacity, 0.0f, 1.0f) * decal.get_play_fade();
            fade *= decal.get_distance_fade(distance);
            fade *= get_decal_screen_fade(get_decal_screen_share(radius, distance, projection_scale, is_perspective), decal.fade_screen_size);
            if(fade <= 0.0f)
            {
                return;
            }

            decal_item item;
            item.keep_alive = get_decal_program_material(decal, shared);
            item.program_material = item.keep_alive->safe_cast<graph_material>();
            if(item.program_material == nullptr || !item.program_material->is_ready_to_draw())
            {
                item.keep_alive = shared;
                item.program_material = graph;
            }
            // The decal's own first registers: its resolved material's, which the instanced draw reads per decal.
            const auto& resolved = decal.get_material_instance().get_resolved();
            const material& values = resolved ? *resolved : *shared;
            values.write_instance_constants(item.registers.data());
            item.box_to_world = box_to_world;
            item.world_to_box = glm::inverse(box_to_world);
            item.fade = math::vec4(fade, decal.age, decal.get_normalized_age(), decal.lifetime);
            const float start = std::clamp(std::min(decal.angle_fade.x, decal.angle_fade.y), 0.0f, 180.0f);
            const float end = std::clamp(std::max(decal.angle_fade.x, decal.angle_fade.y), 0.0f, 180.0f);
            item.angle = math::vec4(std::cos(math::radians(start)),
                                    std::cos(math::radians(std::max(end, start + 0.01f))),
                                    float(receiver_layers & 0xFFFFu),
                                    float(receiver_layers >> 16u));
            if(receiver_layers != DECAL_ALL_LAYERS)
            {
                layer_bounds_.emplace_back(world_min, world_max);
            }
            item.sort_order = decal.sort_order;
            item.entity = static_cast<uint32_t>(entt::to_integral(entity));
            const math::vec3 camera_in_box = math::vec3(item.world_to_box * math::vec4(camera_position, 1.0f));
            const math::vec3 scale(math::length(axis_x), math::length(axis_y), math::length(axis_z));
            const math::vec3 margin = math::vec3(near_clip * DECAL_INSIDE_NEAR_MARGIN) / math::max(scale, math::vec3(0.0001f));
            item.is_camera_inside = math::all(math::lessThanEqual(math::abs(camera_in_box), math::vec3(0.5f) + margin));
            if(is_behind_camera || item.is_camera_inside)
            {
                corner_min = math::vec2(0.0f);
                corner_max = math::vec2(1.0f);
            }
            rect_min = math::min(rect_min, corner_min);
            rect_max = math::max(rect_max, corner_max);
            const uint32_t surface_writes = material_decal_writes_normal | material_decal_writes_surface;
            wants_full_path_ = wants_full_path_ || (graph->get_decal_writes() & surface_writes) != 0 || graph->decal_reads_surface();
            items_.push_back(std::move(item));
        });

    std::sort(items_.begin(),
              items_.end(),
              [](const decal_item& a, const decal_item& b)
              {
                  return std::tie(a.sort_order, a.program_material, a.is_camera_inside, a.entity) <
                         std::tie(b.sort_order, b.program_material, b.is_camera_inside, b.entity);
              });
    rect_min = math::clamp(rect_min, math::vec2(0.0f), math::vec2(1.0f));
    rect_max = math::clamp(rect_max, math::vec2(0.0f), math::vec2(1.0f));
    screen_rect_ = {};
    if(!items_.empty())
    {
        // Normalized here; run() turns it into the G-buffer's pixels.
        screen_rect_ = irect32_t(int32_t(rect_min.x * 65536.0f), int32_t(rect_min.y * 65536.0f), int32_t(rect_max.x * 65536.0f), int32_t(rect_max.y * 65536.0f));
    }
    stats_.decals = uint32_t(items_.size());
}

auto decal_pass::has_decals() const -> bool
{
    return !items_.empty();
}

void decal_pass::set_overdraw_view(bool is_on)
{
    is_overdraw_view_ = is_on;
    if(overdraw_program_)
    {
        overdraw_program_->set_wanted(is_on);
    }
}

auto decal_pass::get_stats() const -> const stats&
{
    return stats_;
}

void decal_pass::run(gfx::render_view& rview,
                     const camera& cam,
                     const feature_limits& limits,
                     depth_copy_pass& depth_copy,
                     mask_pass& masks,
                     hpp::span<const rendering::visibility_data> receivers)
{
    if(items_.empty())
    {
        return;
    }
    const auto& gbuffer = rview.fbo_get("GBUFFER");
    const auto& base_color = gbuffer->get_texture(0);
    const auto& normal = gbuffer->get_texture(1);
    const auto& emissive = gbuffer->get_texture(2);
    const auto& depth = gbuffer->get_texture(4);
    const auto depth_copy_texture = depth_copy.run(rview,
                                                   {.name = "DECAL_DEPTH",
                                                    .source = depth,
                                                    .sampler_flags = DECAL_POINT_SAMPLER,
                                                    .pass_name = "Decals/Depth Copy"});
    if(!depth_copy_texture)
    {
        return;
    }
    const auto layers = draw_layers(rview, cam, masks, receivers, depth_copy_texture);
    const bool is_full_path = wants_full_path_ && limits.decal_surface_channels;
    stats_.is_full_path = is_full_path;
    if(!is_full_path)
    {
        auto& fbo = rview.fbo_acquire("DECAL_COLOR");
        if(gfx::needs_recreate(fbo, {base_color, emissive, depth}))
        {
            fbo = std::make_shared<gfx::frame_buffer>();
            fbo->populate({base_color, emissive, depth});
        }
        draw_decals(rview, cam, fbo.get(), false, depth_copy_texture, nullptr, normal, layers);
        return;
    }
    const auto size = base_color->get_size();
    const auto& buffer_a = rview.tex_acquire("DBUFFER_A",
                                             {.size = size, .format = bgfx::TextureFormat::RGBA8, .flags = BGFX_TEXTURE_RT | BGFX_TEXTURE_SRGB});
    const auto& buffer_b = rview.tex_acquire("DBUFFER_B", {.size = size, .format = bgfx::TextureFormat::RGBA8, .flags = BGFX_TEXTURE_RT});
    const auto& buffer_c = rview.tex_acquire("DBUFFER_C", {.size = size, .format = bgfx::TextureFormat::RGBA8, .flags = BGFX_TEXTURE_RT});
    auto& clear_fbo = rview.fbo_acquire("DBUFFER_CLEAR");
    if(gfx::needs_recreate(clear_fbo, {buffer_a, buffer_b, buffer_c}))
    {
        clear_fbo = std::make_shared<gfx::frame_buffer>();
        clear_fbo->populate({buffer_a, buffer_b, buffer_c});
    }
    // Nothing yet: no colour, the whole surface left.
    gfx::render_pass clear_pass("Decals/Clear");
    clear_pass.bind(clear_fbo.get());
    clear_pass.clear(BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);

    auto& draw_fbo = rview.fbo_acquire("DBUFFER");
    if(gfx::needs_recreate(draw_fbo, {buffer_a, buffer_b, buffer_c, emissive, depth}))
    {
        draw_fbo = std::make_shared<gfx::frame_buffer>();
        draw_fbo->populate({buffer_a, buffer_b, buffer_c, emissive, depth});
    }
    draw_decals(rview, cam, draw_fbo.get(), true, depth_copy_texture, base_color, normal, layers);
    resolve(rview, gbuffer);
}

auto decal_pass::draw_layers(gfx::render_view& rview,
                             const camera& cam,
                             mask_pass& masks,
                             hpp::span<const rendering::visibility_data> receivers,
                             const gfx::texture::ptr& depth) -> gfx::texture::ptr
{
    if(layer_bounds_.empty())
    {
        return nullptr;
    }
    // Only the receivers a layer-restricted decal touches: the others are tested by no decal (an unrestricted one
    // takes every layer, mg_decal_receives_layers). A pixel no drawn receiver shows holds no layer.
    const auto fbo = masks.run(rview,
                               cam,
                               receivers,
                               [this](entt::handle entity) -> hpp::optional<math::vec4>
                               {
                                   const auto& bounds = entity.get<model_component>().get_world_bounds();
                                   const bool is_touched = std::any_of(layer_bounds_.begin(),
                                                                       layer_bounds_.end(),
                                                                       [&](const math::bbox& box)
                                                                       {
                                                                           return box.intersect(bounds);
                                                                       });
                                   if(!is_touched)
                                   {
                                       return hpp::nullopt;
                                   }
                                   const auto* layer = entity.try_get<layer_component>();
                                   const auto bits = uint32_t(layer != nullptr ? layer->layers.mask : layer_reserved::default_layer);
                                   return math::vec4(float(bits & 0xFFu), float((bits >> 8u) & 0xFFu), float((bits >> 16u) & 0xFFu), float(bits >> 24u)) /
                                          255.0f;
                               },
                               {.name = "DECAL_LAYERS",
                                .pass_name = "Decals/Receiver Layers",
                                .format = bgfx::TextureFormat::RGBA8,
                                .clear_rgba = 0,
                                .visible_depth = depth});
    return fbo ? fbo->get_texture(0) : nullptr;
}

void decal_pass::draw_decals(gfx::render_view& rview,
                             const camera& cam,
                             const gfx::frame_buffer* target,
                             bool is_full_path,
                             const gfx::texture::ptr& depth,
                             const gfx::texture::ptr& gbuffer0,
                             const gfx::texture::ptr& gbuffer1,
                             const gfx::texture::ptr& layers)
{
    gfx::render_pass pass(is_full_path ? "Decals/Accumulate" : "Decals/Draw");
    pass.bind(target);
    // Offsets from the camera (camera::set_pass_view_proj), as the G-buffer was drawn.
    cam.set_pass_view_proj(pass, cam.get_projection());

    const bool is_jittered = cam.get_aa_data().y > 1.0f;
    const math::vec4 view_params(is_jittered ? DECAL_TAA_MIP_BIAS : 0.0f, gbuffer0 ? 1.0f : 0.0f, layers ? 1.0f : 0.0f, 0.0f);
    const auto& layer_texture = layers ? layers : default_textures::get().black_texture();
    const math::mat4 inverse_view_proj = cam.get_inverse_view_projection_relative();
    const auto& surface_color = gbuffer0 ? gbuffer0 : default_textures::get().black_texture();
    // Premultiplied over what is there; the colour path keeps the G-buffer's AO, the full path keeps the share left.
    const uint64_t blend = is_full_path
                               ? BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_INV_SRC_ALPHA)
                               : BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA, BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
    const char* fragment_variant = is_full_path ? "full" : gfx::shader::DEFAULT_VARIANT;

    // The overdraw view counts the boxes' fragments with the same depth test, into a target of its own.
    std::optional<gfx::render_pass> overdraw_pass;
    if(is_overdraw_view_ && overdraw_program_ && overdraw_program_->begin())
    {
        overdraw_program_->end();
        const auto& scene_depth = rview.fbo_get("GBUFFER")->get_texture(4);
        const auto& overdraw = rview.tex_acquire("DECAL_OVERDRAW",
                                                 {.size = scene_depth->get_size(), .format = bgfx::TextureFormat::R16F, .flags = BGFX_TEXTURE_RT});
        auto& overdraw_fbo = rview.fbo_acquire("DECAL_OVERDRAW");
        if(gfx::needs_recreate(overdraw_fbo, {overdraw, scene_depth}))
        {
            overdraw_fbo = std::make_shared<gfx::frame_buffer>();
            overdraw_fbo->populate({overdraw, scene_depth});
        }
        overdraw_pass.emplace("Decals/Overdraw");
        overdraw_pass->bind(overdraw_fbo.get());
        overdraw_pass->clear(BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
        cam.set_pass_view_proj(*overdraw_pass, cam.get_projection());
    }

    size_t first = 0;
    while(first < items_.size())
    {
        size_t last = first + 1;
        while(last < items_.size() && items_[last].program_material == items_[first].program_material &&
              items_[last].is_camera_inside == items_[first].is_camera_inside && items_[last].sort_order == items_[first].sort_order)
        {
            ++last;
        }
        const auto& head = items_[first];
        auto* program = programs_.begin(*head.program_material, gfx::shader::DEFAULT_VARIANT, fragment_variant);
        auto count = uint32_t(last - first);
        count = std::min(count, bgfx::getAvailInstanceDataBuffer(count, DECAL_INSTANCE_STRIDE));
        if(program != nullptr && count > 0)
        {
            bgfx::InstanceDataBuffer instances;
            bgfx::allocInstanceDataBuffer(&instances, count, DECAL_INSTANCE_STRIDE);
            auto* data = reinterpret_cast<math::vec4*>(instances.data);
            for(uint32_t i = 0; i < count; ++i)
            {
                const auto& item = items_[first + i];
                auto* out = data + size_t(i) * DECAL_INSTANCE_VEC4S;
                for(int row = 0; row < 3; ++row)
                {
                    out[row] = decal_matrix_row(item.box_to_world, row);
                    out[3 + row] = decal_matrix_row(item.world_to_box, row);
                }
                out[6] = item.fade;
                out[7] = item.angle;
                std::copy(item.registers.begin(), item.registers.end(), out + 8);
            }
            programs_.submit_values(*head.program_material);
            programs_.submit_camera_position(cam.get_position());
            gfx::set_uniform(uniforms_.u_decal_inv_view_proj, inverse_view_proj);
            gfx::set_uniform(uniforms_.u_decal_view, view_params);
            gfx::set_texture(uniforms_.s_decal_depth, DECAL_DEPTH_STAGE, depth, DECAL_POINT_SAMPLER);
            gfx::set_texture(uniforms_.s_decal_gbuffer0, DECAL_GBUFFER0_STAGE, surface_color, DECAL_POINT_SAMPLER);
            gfx::set_texture(uniforms_.s_decal_gbuffer1, DECAL_GBUFFER1_STAGE, gbuffer1, DECAL_POINT_SAMPLER);
            gfx::set_texture(uniforms_.s_decal_layers, DECAL_LAYERS_STAGE, layer_texture, DECAL_POINT_SAMPLER);
            bgfx::setVertexBuffer(0, box_vertices_);
            bgfx::setIndexBuffer(box_indices_);
            bgfx::setInstanceDataBuffer(&instances);
            // Outside the box, its front faces in front of the surface; inside it, its back faces behind the surface.
            const uint64_t faces = head.is_camera_inside ? (gfx::depth_test_farther_or_equal | BGFX_STATE_CULL_CW)
                                                         : (gfx::depth_test_nearer_or_equal | BGFX_STATE_CULL_CCW);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | blend | faces);
            bgfx::submit(pass.id, program->native_handle(), 0, BGFX_DISCARD_ALL);
            ++stats_.draws;
            if(overdraw_pass && overdraw_program_->begin())
            {
                bgfx::setVertexBuffer(0, box_vertices_);
                bgfx::setIndexBuffer(box_indices_);
                bgfx::setInstanceDataBuffer(&instances);
                bgfx::setState(BGFX_STATE_WRITE_R | BGFX_STATE_BLEND_ADD | faces);
                bgfx::submit(overdraw_pass->id, overdraw_program_->native_handle(), 0, BGFX_DISCARD_ALL);
                overdraw_program_->end();
            }
        }
        first = last;
    }
    bgfx::discard();
    programs_.release_unused();
}

void decal_pass::resolve(gfx::render_view& rview, const gfx::frame_buffer::ptr& gbuffer)
{
    const auto& base_color = gbuffer->get_texture(0);
    const auto& normal = gbuffer->get_texture(1);
    const auto size = base_color->get_size();
    const auto to_pixels = [](int32_t share, uint32_t extent)
    {
        return int32_t(float(share) / 65536.0f * float(extent));
    };
    const auto margin = int32_t(DECAL_RECT_MARGIN);
    const int32_t left = std::max(0, to_pixels(screen_rect_.left, size.width) - margin);
    const int32_t top = std::max(0, to_pixels(screen_rect_.top, size.height) - margin);
    const int32_t right = std::min(int32_t(size.width), to_pixels(screen_rect_.right, size.width) + margin + 1);
    const int32_t bottom = std::min(int32_t(size.height), to_pixels(screen_rect_.bottom, size.height) + margin + 1);
    if(right <= left || bottom <= top)
    {
        return;
    }
    const auto scissor = [&]()
    {
        bgfx::setScissor(uint16_t(left), uint16_t(top), uint16_t(right - left), uint16_t(bottom - top));
    };

    // The G-buffer's normal, metallic and roughness, which the surface resolve reads while it writes them.
    const auto& surface_copy = rview.tex_acquire("DECAL_SURFACE_COPY",
                                                 {.size = size, .format = bgfx::TextureFormat::Enum(normal->info.format), .flags = BGFX_TEXTURE_RT});
    auto& copy_fbo = rview.fbo_acquire("DECAL_SURFACE_COPY");
    if(gfx::needs_recreate(copy_fbo, {surface_copy}))
    {
        copy_fbo = std::make_shared<gfx::frame_buffer>();
        copy_fbo->populate({surface_copy});
    }
    if(copy_program_.program->begin())
    {
        gfx::render_pass pass("Decals/Copy Surface");
        pass.bind(copy_fbo.get());
        gfx::set_texture(copy_program_.s_decal_surface, 3, normal, DECAL_POINT_SAMPLER);
        scissor();
        bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, copy_program_.program->native_handle());
        copy_program_.program->end();
    }

    auto& color_fbo = rview.fbo_acquire("DECAL_RESOLVE_COLOR");
    if(gfx::needs_recreate(color_fbo, {base_color}))
    {
        color_fbo = std::make_shared<gfx::frame_buffer>();
        color_fbo->populate({base_color});
    }
    if(color_program_.program->begin())
    {
        gfx::render_pass pass("Decals/Resolve Color");
        pass.bind(color_fbo.get());
        gfx::set_texture(color_program_.s_decal_a, 0, rview.tex_get("DBUFFER_A"), DECAL_POINT_SAMPLER);
        scissor();
        // The decals' colour plus the surface's times the share they leave; its AO (alpha) stays.
        bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_SRC_ALPHA));
        bgfx::submit(pass.id, color_program_.program->native_handle());
        color_program_.program->end();
    }

    auto& surface_fbo = rview.fbo_acquire("DECAL_RESOLVE_SURFACE");
    if(gfx::needs_recreate(surface_fbo, {normal}))
    {
        surface_fbo = std::make_shared<gfx::frame_buffer>();
        surface_fbo->populate({normal});
    }
    if(surface_program_.program->begin())
    {
        gfx::render_pass pass("Decals/Resolve Surface");
        pass.bind(surface_fbo.get());
        gfx::set_texture(surface_program_.s_decal_b, 1, rview.tex_get("DBUFFER_B"), DECAL_POINT_SAMPLER);
        gfx::set_texture(surface_program_.s_decal_c, 2, rview.tex_get("DBUFFER_C"), DECAL_POINT_SAMPLER);
        gfx::set_texture(surface_program_.s_decal_surface, 3, surface_copy, DECAL_POINT_SAMPLER);
        scissor();
        bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, surface_program_.program->native_handle());
        surface_program_.program->end();
    }
}

} // namespace unravel
