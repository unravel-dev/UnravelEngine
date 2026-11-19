#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace unravel
{

/**
 * @brief The cubemap faces all reflection probes together may render in one frame.
 *
 * A face is a whole scene render with dozens of render passes, so a frame that bakes every probe at once
 * (a scene loading, play starting, Build Reflection Captures) would cost as much as dozens of frames and could
 * run out of the frame's render passes. The renderer refills the budget at the start of every frame from
 * graphics.reflection_probe_faces_per_frame, and the cameras of that frame spend it as they render faces.
 */
class probe_face_budget
{
public:
    /// The faces a frame may render when the project does not say: one probe's six.
    static constexpr std::uint32_t default_faces_per_frame = 6;

    /// A budget that never runs out, for a render that bakes its probes whole (an asset preview).
    static auto unlimited() -> probe_face_budget
    {
        probe_face_budget budget;
        budget.begin_frame(std::numeric_limits<std::uint32_t>::max());
        return budget;
    }

    /// Starts a frame with @p faces to spend, at least one so bakes always finish.
    void begin_frame(std::uint32_t faces)
    {
        faces_left_ = std::max<std::uint32_t>(faces, 1);
    }

    /// Whether this frame may still render a face.
    [[nodiscard]] auto has_faces_left() const -> bool
    {
        return faces_left_ > 0;
    }

    /// The faces this frame may still render.
    [[nodiscard]] auto get_faces_left() const -> std::uint32_t
    {
        return faces_left_;
    }

    /// Spends one face of this frame. False, spending nothing, once the frame has none left.
    auto try_take() -> bool
    {
        if(faces_left_ == 0)
        {
            return false;
        }
        --faces_left_;
        return true;
    }

private:
    std::uint32_t faces_left_{default_faces_per_frame};
};

} // namespace unravel
