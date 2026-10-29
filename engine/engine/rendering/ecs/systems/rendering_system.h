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
#include <unordered_map>
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
    void on_play_end(rtti::context& ctx);
    /// Drops the instanced draws scripts made before a frame in which they update again.
    void reset_script_draws(rtti::context& ctx, delta_t dt);

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
     * @brief The handheld device a view stands for: the editor's Game panel showing a phone or a tablet preset.
     *
     * Scripts then see that device instead of the desktop the editor runs on: a touch screen, its pixel density
     * and the edges its cutout covers, all in the pixels of the game view. Unset in the player, where the window
     * and the platform answer.
     */
    struct game_view_device
    {
        /// Pixels of the game view per inch of the device's screen.
        float dpi{};
        /// Game view pixels covered on each edge (camera cutout, rounded corners).
        float inset_left{};
        float inset_top{};
        float inset_right{};
        float inset_bottom{};
    };

    /**
     * @brief Sets the device the game view stands for, or clears it with nothing.
     * @param device The device the scripts see, in the game view's pixels.
     */
    void set_game_view_device(const hpp::optional<game_view_device>& device);

    /**
     * @brief Gets the device the game view stands for.
     * @return The device, or nothing when the game view is the real screen.
     */
    auto get_game_view_device() const -> const hpp::optional<game_view_device>&;

    /**
     * @brief The meshes scripts draw as instances (Graphics.DrawMeshInstanced); emptied before each frame in which
     * scripts update, so a paused game keeps drawing them.
     */
    auto get_instanced_draws() -> instanced_draw_list&;

    /// The animations scripts baked (BakedAnimation.Bake); released when play mode ends.
    auto get_baked_animations() -> baked_animation_store&;

    /**
     * @brief Creates a material instance of @p shared that belongs to no renderer (new MaterialInstance(material) in
     * scripts), for instanced draws to draw in place of @p shared. Released by its script, or when play mode ends.
     * @return Its id; 0 when @p shared is not set.
     */
    auto create_material_instance(const asset_handle<material>& shared) -> uint32_t;
    /// The material instance @p id of create_material_instance(), or null.
    auto find_material_instance(uint32_t id) const -> std::shared_ptr<material_instance>;
    void release_material_instance(uint32_t id);

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
    baked_animation_store baked_animations_;
    /// The material instances of create_material_instance(), by id.
    std::unordered_map<uint32_t, std::shared_ptr<material_instance>> material_instances_;
    uint32_t next_material_instance_id_ = 1;
    /// Size of the view the game is shown in; zero until a host sets it.
    usize32_t game_view_size_{};
    /// The handheld device the game view stands for, set by the editor for phone and tablet presets.
    hpp::optional<game_view_device> game_view_device_;
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);


};

} // namespace unravel
