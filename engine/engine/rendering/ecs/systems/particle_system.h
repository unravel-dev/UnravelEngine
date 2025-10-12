#pragma once

#include <engine/ecs/ecs.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/rendering/camera.h>

namespace unravel
{

/**
 * @class particle_system
 * @brief System that manages and updates particle emitters in the ECS.
 */
class particle_system
{
public:
    particle_system() = default;
    ~particle_system() = default;
    
    // Disable copy and move operations
    particle_system(const particle_system&) = delete;
    auto operator=(const particle_system&) -> particle_system& = delete;
    particle_system(particle_system&&) = delete;
    auto operator=(particle_system&&) -> particle_system& = delete;

    /**
     * @brief Initializes the particle system.
     * @param ctx The context to initialize with.
     * @return True if initialization was successful, false otherwise.
     */
    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Deinitializes the particle system.
     * @param ctx The context to deinitialize.
     * @return True if deinitialization was successful, false otherwise.
     */
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Updates all particle emitters in the scene.
     * @param scn The scene containing particle emitters.
     * @param dt The delta time for this frame.
     */
    void on_frame_update(scene& scn, delta_t dt);


private:
    /// Whether the particle system is initialized
    bool initialized_ = false;
};

} // namespace unravel
