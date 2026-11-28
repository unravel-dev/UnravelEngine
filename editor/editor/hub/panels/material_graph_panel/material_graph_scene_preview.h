#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <engine/ecs/scene.h>
#include <engine/frame_times.h>
#include <engine/rendering/material_graph/material_graph.h>

#include <graphics/frame_buffer.h>
#include <imgui/imgui.h>
#include <math/math.h>

#include <cstdint>

namespace unravel
{
class material;

/**
 * @brief The Material Graph window's 3D preview: a shape wearing the graph's material in a scene of its own, lit by a
 * sun and a sky, under a camera the user turns around it. The Particles shape is a Particle Emitter drawing the
 * material on its sprites, over a checkered floor.
 *
 * A Decal material is laid onto the shape (wearing the Standard material) by a decal projecting from the first view,
 * or straight down onto the plane; a Post Process material runs on the preview camera at its Location over the shape
 * in the Standard material.
 *
 * The scene renders only for a frame whose window drew the preview the frame before (render() runs before the UI),
 * at the size it was drawn at, with the engine's default graphics settings rather than the project's.
 */
class material_graph_scene_preview
{
public:
    /// The shapes the preview shows the material on.
    enum class shape : uint8_t
    {
        sphere,
        cube,
        cylinder,
        plane,
        particles,
    };

    void deinit(rtti::context& ctx);

    /// Renders @p mat on the shape, when the preview was drawn the frame before.
    void render(rtti::context& ctx, const frame_times& times, const asset_handle<material>& mat);

    /**
     * @brief Draws the last render over @p size and turns the camera with the mouse: dragging orbits, the wheel zooms,
     * a double-click goes back to the first view.
     */
    void draw(const ImVec2& size);

    void set_shape(shape value);
    auto get_shape() const -> shape;
    /// Shows a material that draws particles on the Particles shape, and any other on the mesh shape last picked.
    void pick_shape_for(bool draws_particles);

private:
    /// The scene, its camera and lights, and a sphere wearing @p mat.
    void create_scene(rtti::context& ctx, const asset_handle<material>& mat);
    /// Empties the scene and forgets its entities; the last render and the camera's view stay.
    void unload_scene(rtti::context& ctx);
    void create_object(rtti::context& ctx);
    /// The Particles shape: an emitter of slow, large sprites that fills the view from its first frame, lit as
    /// surfaces where the material is lit.
    void create_emitter(rtti::context& ctx);
    /// The floor under the Particles shape, in two greys: what a material that bends or filters the scene behind it
    /// shows, and what soft particles meet. Its tiles belong to the emitter's entity and go with it.
    void create_floor(rtti::context& ctx);
    /// Aims the camera at the shape's bounds.
    void frame_object();
    /// Shows @p mat as its domain draws: on the shape, as a decal onto it, or over the view.
    void set_material(rtti::context& ctx, const asset_handle<material>& mat);
    /// The decal that lays a Decal material onto the shape, sized to its bounds.
    void create_decal(rtti::context& ctx, const asset_handle<material>& mat);
    void update_camera();
    void handle_input();
    void reset_view();

    /// The first view, in degrees: from the front right, a little above.
    static constexpr float INITIAL_YAW = 30.0f;
    static constexpr float INITIAL_PITCH = 20.0f;

    scene scene_{"material_graph_preview"};
    entt::handle camera_;
    entt::handle object_;
    /// The decal a Decal material is shown with; empty for the other domains.
    entt::handle decal_;
    gfx::frame_buffer::ptr output_;
    asset_handle<material> material_;
    /// The domain material_ was shown as: a recompile can change it under the same asset.
    material_domain shown_domain_{material_domain::surface};
    shape shape_{shape::sphere};
    shape object_shape_{shape::sphere};
    /// The mesh shape last picked, which a material that draws no particles goes back to.
    shape mesh_shape_{shape::sphere};
    bool has_scene_{};

    /// The size the preview was drawn at; zero while it was not drawn.
    usize32_t size_{};
    bool is_drawn_{};

    /// The camera, around the shape's bounds: degrees, and a distance scale of the fitting distance.
    math::vec3 target_{};
    float radius_{1.0f};
    float yaw_{INITIAL_YAW};
    float pitch_{INITIAL_PITCH};
    float zoom_{1.0f};
};

} // namespace unravel
