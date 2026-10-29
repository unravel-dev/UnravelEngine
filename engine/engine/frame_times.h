#pragma once

#include <base/basetypes.hpp>

namespace unravel
{

/**
 * @brief The time one frame advances, on each clock a system may follow.
 *
 * The engine loop fills it once per frame and passes it to every frame event, so each system
 * picks the clock it needs instead of rebuilding one clock from another.
 */
struct frame_times
{
    /// Game time: smoothed, scaled by the time scale, and zero on the first frame of play and
    /// while play is paused (a frame step advances it by one step). What gameplay integrates.
    delta_t scaled{};
    /// The same game time without the time scale: zero when scaled is zero for the first frame
    /// or a pause, but still advancing at time scale 0. What scripts read as unscaled time.
    delta_t unscaled{};
    /// Real time the frame took: not smoothed, scaled, clamped or paused. For the editor UI and
    /// frame statistics.
    delta_t real{};

    /// The same time on every clock, for frames the engine loop does not run: the forced updates
    /// after a scene load, a thumbnail render.
    static constexpr auto make_uniform(delta_t time) -> frame_times
    {
        return {time, time, time};
    }
};

} // namespace unravel
