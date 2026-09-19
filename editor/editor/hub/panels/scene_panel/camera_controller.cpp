#include "camera_controller.h"

#include <cmath>

namespace unravel
{
namespace
{
// UE's MovementVelocityDampingAmount. The velocity closes 63% of the gap to its target every
// 1 / 10 s: full speed and a full stop both take about 0.3 s.
constexpr float VELOCITY_DAMPING = 10.0f;
// The rest of a coast at this speed is half a millimetre. Stop there, otherwise the view creeps
// for seconds and never lets the temporal histories settle.
constexpr float REST_SPEED = 0.005f;
const math::vec3 ZERO_VELOCITY{0.0f, 0.0f, 0.0f};
} // namespace

auto camera_controller::update(const math::vec3& move_direction, float fly_speed, float dt) -> math::vec3
{
    const math::vec3 target_velocity = move_direction * fly_speed;
    const math::vec3 excess_velocity = velocity_ - target_velocity;
    const float decay = std::exp(-VELOCITY_DAMPING * dt);
    const math::vec3 displacement = target_velocity * dt + excess_velocity * ((1.0f - decay) / VELOCITY_DAMPING);
    velocity_ = target_velocity + excess_velocity * decay;
    const bool is_coasting = math::dot(target_velocity, target_velocity) == 0.0f;
    if(is_coasting && math::dot(velocity_, velocity_) < REST_SPEED * REST_SPEED)
    {
        stop();
    }
    return displacement;
}

void camera_controller::add_travel(const math::vec3& offset)
{
    // A coast under the drag alone covers velocity / drag.
    velocity_ += offset * VELOCITY_DAMPING;
}

void camera_controller::stop()
{
    velocity_ = ZERO_VELOCITY;
}

auto camera_controller::is_moving() const -> bool
{
    return math::dot(velocity_, velocity_) > 0.0f;
}
} // namespace unravel
