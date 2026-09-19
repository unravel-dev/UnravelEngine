#pragma once

#include <math/math.h>

namespace unravel
{
/**
 * @brief Velocity based fly movement of the editor camera.
 *
 * Follows UE's FEditorCameraController: the input accelerates a world-space velocity and a linear
 * drag pulls it back to rest, so the camera eases in, eases out and carries its momentum through
 * a turn. UE integrates that per frame (v += a * dt, v -= v * drag * dt), which settles at a
 * frame-rate dependent speed; the step here is the closed-form solution of
 * dv/dt = drag * (target - v), identical at any frame rate.
 */
class camera_controller
{
public:
    /**
     * @brief Advances the movement by one time step.
     * @param move_direction World-space input direction, length <= 1. Zero lets the camera coast to rest.
     * @param fly_speed Speed the camera settles at under a full input, in metres per second.
     * @param dt Time step in seconds.
     * @return World-space displacement of this step.
     */
    auto update(const math::vec3& move_direction, float fly_speed, float dt) -> math::vec3;

    /**
     * @brief Adds a velocity impulse whose coast covers exactly the given world-space offset.
     */
    void add_travel(const math::vec3& offset);

    /**
     * @brief Drops the momentum.
     */
    void stop();

    auto is_moving() const -> bool;

private:
    /// World-space velocity in metres per second.
    math::vec3 velocity_{0.0f, 0.0f, 0.0f};
};
} // namespace unravel
