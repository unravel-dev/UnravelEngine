#include "material_graph_scene_preview.h"

#include <engine/assets/asset_manager.h>
#include <engine/defaults/defaults.h>
#include <engine/defaults/engine_asset_keys.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/components/decal_component.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/post_process_material_component.h>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/ecs/systems/rendering_system.h>
#include <editor/imgui/integration/imgui.h>
#include <engine/rendering/material.h>

#include <imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
/// The size the scene is made at; the first draw gives it its own.
constexpr uint32_t INITIAL_PREVIEW_EDGE = 256;
/// The camera's distance from the shape, in radii of the sphere around its bounding box, at zoom 1.
constexpr float FIT_DISTANCE_IN_RADII = 2.2f;
constexpr float MIN_PREVIEW_ZOOM = 0.35f;
constexpr float MAX_PREVIEW_ZOOM = 4.0f;
/// Each step of the wheel moves the camera this much closer or further.
constexpr float WHEEL_ZOOM_STEP = 0.9f;
/// Degrees the camera turns per pixel dragged.
constexpr float ORBIT_DEGREES_PER_PIXEL = 0.4f;
/// The preview decal's face and depth, in radii of the sphere around the shape: it covers the shape's front.
constexpr float PREVIEW_DECAL_SIZE_IN_RADII = 1.6f;
constexpr float PREVIEW_DECAL_DEPTH_IN_RADII = 2.2f;

/// The camera never looks straight down or up, where the orbit's up turns over.
constexpr float MAX_PREVIEW_PITCH = 85.0f;

auto get_shape_mesh_name(material_graph_scene_preview::shape value) -> const char*
{
    switch(value)
    {
        case material_graph_scene_preview::shape::cube:
            return "Cube";
        case material_graph_scene_preview::shape::cylinder:
            return "Cylinder";
        case material_graph_scene_preview::shape::plane:
            return "Plane";
        case material_graph_scene_preview::shape::sphere:
        default:
            return "Sphere";
    }
}
} // namespace

void material_graph_scene_preview::deinit(rtti::context& ctx)
{
    unload_scene(ctx);
    output_.reset();
    reset_view();
}

void material_graph_scene_preview::render(rtti::context& ctx, const frame_times& times, const asset_handle<material>& mat)
{
    const bool was_drawn = is_drawn_;
    is_drawn_ = false;
    if(!was_drawn || size_.width == 0 || size_.height == 0)
    {
        return;
    }
    // A script recompile and the end of play reload every scene from a checkpoint into new entities: the handles kept
    // here then name none, and the scene is made again.
    if(has_scene_ && !camera_)
    {
        unload_scene(ctx);
    }
    if(!has_scene_)
    {
        create_scene(ctx, mat);
    }
    if(!object_ || object_shape_ != shape_)
    {
        create_object(ctx);
    }
    set_material(ctx, mat);
    if(!camera_ || !object_)
    {
        return;
    }
    update_camera();
    auto& camera_comp = camera_.get<camera_component>();
    camera_comp.set_viewport_size(size_);
    auto& rpath = ctx.get_cached<rendering_system>();
    rpath.on_frame_update(scene_, times);
    rpath.on_frame_before_render(scene_, times);
    view_render_params view;
    view.render_screen_space = false;
    view.settings_use = graphics_settings_use::none;
    output_ = rpath.render_scene(camera_, camera_comp, scene_, times, view);
}

void material_graph_scene_preview::draw(const ImVec2& size)
{
    const ImVec2 min = ImGui::GetCursorScreenPos();
    // The shape picker drawn over the preview takes the pointer from it.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##material_graph_scene_preview", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)));
    handle_input();
    is_drawn_ = true;
    size_ = usize32_t(uint32_t(std::max(size.x, 1.0f)), uint32_t(std::max(size.y, 1.0f)));
    auto* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 max = min + size;
    draw_list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
    // The output follows the size a frame late: drawn over the whole area meanwhile.
    if(output_ && output_->get_attachment_count() > 0)
    {
        draw_list->AddImage(ImGui::ToId(output_->get_texture(0)), min, max);
    }
}

void material_graph_scene_preview::set_shape(shape value)
{
    shape_ = value;
}

auto material_graph_scene_preview::get_shape() const -> shape
{
    return shape_;
}

void material_graph_scene_preview::create_scene(rtti::context& ctx, const asset_handle<material>& mat)
{
    const usize32_t size(INITIAL_PREVIEW_EDGE, INITIAL_PREVIEW_EDGE);
    const auto preview = defaults::create_default_3d_scene_for_asset_preview(ctx, scene_, mat, size, false);
    camera_ = preview.camera;
    object_ = preview.object;
    object_shape_ = shape::sphere;
    material_ = mat;
    has_scene_ = true;
    frame_object();
}

void material_graph_scene_preview::unload_scene(rtti::context& ctx)
{
    if(has_scene_)
    {
        ctx.get_cached<rendering_system>().release_pipeline_resources(scene_);
        scene_.unload();
    }
    has_scene_ = false;
    camera_ = {};
    object_ = {};
    decal_ = {};
    material_ = {};
}

void material_graph_scene_preview::create_object(rtti::context& ctx)
{
    if(object_)
    {
        scene::destroy_entity(object_);
    }
    object_ = defaults::create_built_in_mesh_entity(ctx, scene_, get_shape_mesh_name(shape_));
    object_shape_ = shape_;
    material_ = {};
    if(!object_)
    {
        return;
    }
    object_.get<model_component>().set_casts_shadow(false);
    frame_object();
}

void material_graph_scene_preview::frame_object()
{
    if(!object_)
    {
        return;
    }
    // The sphere around the box: a cube's corners stay in view, as a sphere's sides do.
    const auto bounds = defaults::calc_bounds_sphere_global(object_);
    target_ = bounds.position;
    radius_ = std::max(bounds.radius, math::epsilon<float>());
}

void material_graph_scene_preview::set_material(rtti::context& ctx, const asset_handle<material>& mat)
{
    const auto domain = find_loaded_material_domain(mat).value_or(material_domain::surface);
    if(!object_ || !camera_ || (material_ == mat && shown_domain_ == domain))
    {
        return;
    }
    material_ = mat;
    shown_domain_ = domain;
    const auto surface = domain == material_domain::surface
                             ? mat
                             : ctx.get_cached<asset_manager>().get_asset<material>(STANDARD_MATERIAL_KEY);
    object_.get<model_component>().set_shared_material(surface, 0);
    if(decal_)
    {
        scene::destroy_entity(decal_);
        decal_ = {};
    }
    auto& post_materials = camera_.get_or_emplace<post_process_material_component>();
    post_materials.entries.clear();
    if(domain == material_domain::decal)
    {
        create_decal(ctx, mat);
    }
    else if(domain == material_domain::post_process)
    {
        post_materials.add(mat);
    }
}

void material_graph_scene_preview::create_decal(rtti::context& ctx, const asset_handle<material>& mat)
{
    decal_ = defaults::create_decal_entity(ctx, scene_, "Preview Decal", mat);
    if(!decal_)
    {
        return;
    }
    // From the first view, where the camera starts; straight down onto the plane, which the first view grazes.
    const float yaw = math::radians(INITIAL_YAW);
    const float pitch = math::radians(INITIAL_PITCH);
    const math::vec3 toward_view =
        object_shape_ == shape::plane ? math::vec3(0.0f, 1.0f, 0.0f)
                                      : math::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    auto& decal = decal_.get<decal_component>();
    decal.size = math::vec2(radius_ * PREVIEW_DECAL_SIZE_IN_RADII);
    decal.depth = radius_ * PREVIEW_DECAL_DEPTH_IN_RADII;
    defaults::place_decal_on_surface(decal_, target_, toward_view, 0.0f);
}

void material_graph_scene_preview::update_camera()
{
    const float yaw = math::radians(yaw_);
    const float pitch = math::radians(pitch_);
    const math::vec3 direction(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    auto& transform = camera_.get<transform_component>();
    transform.set_position_local(target_ + direction * (radius_ * FIT_DISTANCE_IN_RADII * zoom_));
    transform.look_at(target_);
}

void material_graph_scene_preview::handle_input()
{
    auto& io = ImGui::GetIO();
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        reset_view();
        return;
    }
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        yaw_ -= io.MouseDelta.x * ORBIT_DEGREES_PER_PIXEL;
        pitch_ = std::clamp(pitch_ + io.MouseDelta.y * ORBIT_DEGREES_PER_PIXEL, -MAX_PREVIEW_PITCH, MAX_PREVIEW_PITCH);
    }
    if(ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    {
        zoom_ = std::clamp(zoom_ * std::pow(WHEEL_ZOOM_STEP, io.MouseWheel), MIN_PREVIEW_ZOOM, MAX_PREVIEW_ZOOM);
    }
}

void material_graph_scene_preview::reset_view()
{
    yaw_ = INITIAL_YAW;
    pitch_ = INITIAL_PITCH;
    zoom_ = 1.0f;
}

} // namespace unravel
