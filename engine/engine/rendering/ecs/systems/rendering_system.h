#pragma once

#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <engine/rendering/ecs/systems/particle_system.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/instanced_draw_list.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>
#include <graphics/debugdraw.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <hpp/optional.hpp>
#include <hpp/span.hpp>

#include <map>
#include <memory>
#include <vector>

namespace unravel
{

/// Whether a view renders with the project's graphics settings.
enum class graphics_settings_use : std::uint8_t
{
    /// The feature limits and the render scale.
    project,
    /// The engine's defaults: an asset preview (a thumbnail) does not depend on the profile the editor previews.
    none,
};

/// How the host of a camera view renders it.
struct view_render_params
{
    /// The view shows the game: it draws the screen-space UI into its output. Otherwise only the world-space UI.
    bool render_screen_space = true;
    /// The user is editing the scene in this view (pipeline::run_params::is_being_edited).
    bool is_being_edited = false;
    /// Whether the view renders with the project's graphics settings.
    graphics_settings_use settings_use = graphics_settings_use::project;
    /// The view's own render scale, over the project's graphics.render_scale; empty: the project's, or 1 with
    /// the engine's defaults.
    hpp::optional<float> render_scale;
    /// The size the scene renders at before the render scale; empty: the view's size
    /// (pipeline::run_params::render_resolution).
    usize32_t render_resolution{};
    /// A view whose scene renders at another size than its output also gives the output a depth at its size,
    /// for overlays drawn over it with the scene's depth (pipeline::run_params::needs_output_depth).
    bool needs_output_depth = false;
};

/**
 * @class rendering_system
 * @brief Base class for different rendering paths in the ACE framework.
 */
class rendering_system
{
public:
    rendering_system() = default;
    ~rendering_system() = default;

    /**
     * @brief Initializes the rendering path with the given context.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the rendering path with the given context.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;


    void on_frame_end(rtti::context& ctx, delta_t);

    /**
     * @brief Prepares the scene for rendering.
     * @param scn The scene to prepare.
     * @param dt The delta time.
     */
    void on_frame_update(scene& scn, delta_t dt);
    void on_frame_before_render(scene& scn, delta_t dt);


    void on_play_begin(hpp::span<const entt::handle> entities, delta_t dt);

    auto release_pipeline_resources(scene& scn) -> bool;

    /**
     * @brief Renders every active camera of the scene and returns the last one's frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     * @param view How the host shows the cameras.
     * @return A shared pointer to the frame buffer containing the rendered scene.
     */
    auto render_scene(scene& scn, delta_t dt, const view_render_params& view = {}) -> gfx::frame_buffer::ptr;

    /**
     * @brief Renders the scene to the specified output.
     * @param output The output frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     */
    void render_scene(const gfx::frame_buffer::ptr& output, scene& scn, delta_t dt);

    /**
     * @brief Renders the scene and returns the frame buffer.
     * @param camera_ent Camera entity
     * @param comp Camera component
     * @param scn The scene to render.
     * @param dt The delta time.
     * @param view How the host shows the camera.
     * @return A shared pointer to the frame buffer containing the rendered scene.
     */
    auto render_scene(entt::handle camera_ent, camera_component& comp, scene& scn, delta_t dt, const view_render_params& view = {})
        -> gfx::frame_buffer::ptr;

    /**
     * @brief Renders the scene to the specified output.
     * @param output The output frame buffer.
     * @param camera_ent Camera entity
     * @param comp Camera component
     * @param scn The scene to render.
     * @param dt The delta time.
     * @param view How the host shows the camera.
     */
    void render_scene(const gfx::frame_buffer::ptr& output,
                      entt::handle camera_ent,
                      camera_component& comp,
                      scene& scn,
                      delta_t dt,
                      const view_render_params& view = {});

    void add_debugdraw_call(const std::function<void(gfx::dd_raii& dd)>& callback);

    /**
     * @brief Sets the size of the view the game is shown in: the main window in the player, the
     * Game panel's resolution in the editor.
     *
     * The host showing the game sets it whenever it sizes the game cameras; scripts read it as the
     * screen size.
     * @param size The size in pixels.
     */
    void set_game_view_size(const usize32_t& size);

    /**
     * @brief Sets the size of the view the game is shown in and sizes every camera of @p scn to it.
     *
     * The host calls it in the frame update ahead of the scripts, so a frame's scripts read the
     * size that frame renders at.
     * @param scn The scene whose cameras show the game.
     * @param size The size in pixels.
     */
    void apply_game_view_size(scene& scn, const usize32_t& size);

    /**
     * @brief Gets the size of the view the game is shown in.
     * @return The size in pixels, or zero until a host has shown the game.
     */
    auto get_game_view_size() const -> const usize32_t&;

    /**
     * @brief The meshes scripts draw as instances this frame (Graphics.DrawMeshInstanced); emptied when the
     * frame ends.
     */
    auto get_instanced_draws() -> instanced_draw_list&;

private:
    /**
     * @brief Renders particles to the specified framebuffer.
     * @param output The output framebuffer.
     * @param camera The camera for rendering.
     * @param scene The scene containing particles.
     * @param dt Delta time.
     */
    void render_debug(entt::handle camera_entity);

    std::vector<std::function<void(gfx::dd_raii& dd)>> debug_draw_callbacks_;
    instanced_draw_list instanced_draws_;
    /// Size of the view the game is shown in; zero until a host sets it.
    usize32_t game_view_size_{};
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);


};

} // namespace unravel
