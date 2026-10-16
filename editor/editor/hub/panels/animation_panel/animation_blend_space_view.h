#pragma once

#include "animation_controller_ui.h"

#include <editor/imgui/integration/imgui.h>
#include <engine/animation/animation_controller.h>

#include <vector>

namespace unravel
{

/**
 * @brief Plot of a blend state's points: a track for 1D blends, a plane for 2D ones.
 *
 * Points drag to new positions. When the state is playing live, every point shows its current
 * weight and the input parameters show as a marker; dragging on empty space moves that marker,
 * which the owner writes back into the running controller as a preview. The axes fit the points
 * and the marker, and hold still while something is dragged so it does not run away.
 */
class animation_blend_space_view
{
public:
    struct result : animation_edit_result
    {
        /// The user moved the parameter marker; preview_parameter holds the new value.
        bool preview_moved{};
        math::vec2 preview_parameter{};
        /// Index of the point the user clicked, or -1.
        int clicked_point{-1};
    };

    /**
     * @param weights Live weight per point, or empty when the state does not play.
     * @param parameter Live input parameters, or nullptr when the state does not play.
     */
    auto draw(animation_state_def& state,
              const std::vector<float>& weights,
              const math::vec2* parameter,
              int selected_point) -> result;

    /// Ends a point or marker drag in progress: the plot shows another state, or the definition
    /// changed under it (undo, a reload).
    void cancel_interaction();

private:
    struct plot_range
    {
        math::vec2 min{-1.0f, -1.0f};
        math::vec2 max{1.0f, 1.0f};
    };

    int dragging_point_{-1};
    bool dragged_{};
    bool dragging_marker_{};
    /// The axes while a point or the marker is dragged.
    plot_range frozen_range_;
};

} // namespace unravel
