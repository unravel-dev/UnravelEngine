#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <engine/frame_times.h>
#include <filesystem/filesystem.h>

#include <chrono>
#include <memory>

namespace unravel
{

/**
 * @brief The picture of a project on the start page and on the screen that opens it.
 *
 * A project can carry a picture of its own, thumbnail.png in its folder, kept and shared with the
 * project. Without one, the editor shows the view it captured into the cache of the project: once
 * a while after a project without any thumbnail opens, and again whenever a scene is saved. The
 * view is what the game camera sees, rendered for a few frames first so that it has settled, or
 * the Scene view when the scene has no camera.
 */
class project_thumbnail
{
public:
    using clock = std::chrono::steady_clock;

    /// Where a project keeps the thumbnail it chose for itself.
    static auto get_custom_path(const fs::path& project_dir) -> fs::path;
    /// Where the editor keeps the thumbnail it captured.
    static auto get_captured_path(const fs::path& project_dir) -> fs::path;
    /// The thumbnail to show for the project, the custom one first; empty when there is none.
    static auto find(const fs::path& project_dir) -> fs::path;
    /// Copies a PNG image into the project as its custom thumbnail.
    static auto set_custom(const fs::path& project_dir, const fs::path& image_path) -> bool;

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /// Captures the view of the open project once it has an image to give.
    void request_capture();

private:
    enum class capture_state
    {
        idle,
        /// Requested, waiting for capture_time_.
        waiting,
        /// The game view renders, whether its panel shows or not, until it has settled.
        warming
    };

    void on_project_opened(rtti::context& ctx);
    void on_frame_update(rtti::context& ctx, const frame_times& times);
    void on_frame_end(rtti::context& ctx, const frame_times& times);
    /// Issues the readback of the view; false while there is no image to read.
    auto capture(rtti::context& ctx) -> bool;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>();
    capture_state state_{capture_state::idle};
    /// A first capture waits for the scene to settle (lighting, streaming) until then.
    clock::time_point capture_time_{};
    int warm_frames_left_{};
};

} // namespace unravel
