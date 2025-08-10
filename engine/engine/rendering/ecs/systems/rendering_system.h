#pragma once

#include <engine/ecs/ecs.h>
#include <engine/rendering/ecs/components/camera_component.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>
#include <graphics/debugdraw.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <hpp/span.hpp>

#include <map>
#include <memory>
#include <vector>

namespace unravel
{

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
    /**
     * @brief Renders the scene and returns the frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     * @return A shared pointer to the frame buffer containing the rendered scene.
     */
    auto render_scene(scene& scn, delta_t dt) -> gfx::frame_buffer::ptr;

    /**
     * @brief Renders the scene to the specified output.
     * @param output The output frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     */
    void render_scene(const gfx::frame_buffer::ptr& output, scene& scn, delta_t dt);

    /**
     * @brief Renders the scene and returns the frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     * @return A shared pointer to the frame buffer containing the rendered scene.
     */
    auto render_scene(entt::handle camera_ent, camera_component& comp, scene& scn, delta_t dt) -> gfx::frame_buffer::ptr;

    /**
     * @brief Renders the scene to the specified output.
     * @param output The output frame buffer.
     * @param scn The scene to render.
     * @param dt The delta time.
     */
    void render_scene(const gfx::frame_buffer::ptr& output, entt::handle camera_ent, camera_component& comp, scene& scn, delta_t dt);

    void add_debugdraw_call(const std::function<void(gfx::dd_raii& dd)>& callback);
private:
    void render_debug(entt::handle camera_entity);

    std::vector<std::function<void(gfx::dd_raii& dd)>> debug_draw_callbacks_;
    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);


};

} // namespace unravel
