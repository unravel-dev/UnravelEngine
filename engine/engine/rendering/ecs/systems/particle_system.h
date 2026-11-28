#pragma once

#include <engine/ecs/ecs.h>
#include <graphics/frame_buffer.h>
#include <graphics/render_view.h>
#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/rendering/camera.h>
#include <engine/frame_times.h>

#include <cstdint>
#include <vector>

namespace unravel
{
class particle_emitter_component;
struct particle_decal_spawn;
struct particle_script_events;

/**
 * @brief What a frame spent on the particles that collide with physics colliders.
 */
struct particle_collision_stats
{
    /// Casts made, and how many of them touched a surface.
    uint32_t casts = 0;
    uint32_t hits = 0;
    /// Particles whose path waits for a later frame: the frame's cast limit was reached.
    uint32_t waiting = 0;
};

/**
 * @class particle_system
 * @brief System that manages and updates particle emitters in the ECS.
 */
class particle_system
{
public:
    // Defined where the types its lists hold are complete.
    particle_system();
    ~particle_system();
    
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
     * @param times The frame's times; emitters advance by the scaled time.
     */
    void on_frame_before_render(scene& scn, const frame_times& times);

    /**
     * @brief The casts the particles of the project's scene made against its physics world when it last updated:
     * zeros while there is no physics world or no emitter collides with it.
     */
    auto get_collision_stats() const -> const particle_collision_stats&;

    /**
     * @brief Moves the particle events the emitters of the played scene recorded for scripts to @p out_events, an
     * entry for each emitter and frame, oldest first. They wait here, not on their emitters, so the events of an
     * emitter that is destroyed before the scripts run still arrive.
     */
    void take_script_events(std::vector<particle_script_events>& out_events);

private:
    /// Sweeps the paths of the scene's particles that wait for it against its physics world, the frame's casts
    /// shared among their emitters. @return what it spent.
    auto resolve_collisions(scene& scn) -> particle_collision_stats;
    /// Hands out what happened to the scene's particles this frame: sub-emitters burst, and in the scene the game
    /// plays, the scripts' events are kept for them and the contacts' decals are left.
    void hand_out_events(scene& scn);
    /// Leaves the decals the frame's contacts asked for in the scene.
    void spawn_collision_decals(scene& scn);

    /// Whether the particle system is initialized
    bool initialized_ = false;
    /// The emitters with paths to sweep this frame, and which of them goes first.
    std::vector<particle_emitter_component*> sweeping_emitters_;
    std::vector<particle_emitter_component*> still_waiting_;
    uint32_t sweep_turn_ = 0;
    /// The decals the frame's contacts asked for.
    std::vector<particle_decal_spawn> collision_decals_;
    /// The events that wait for the scripts.
    std::vector<particle_script_events> script_events_;
    /// What the project's scene spent on sweeps when it last updated.
    particle_collision_stats collision_stats_;
};

} // namespace unravel
