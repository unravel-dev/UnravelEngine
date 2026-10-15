#pragma once
#include <base/basetypes.hpp>
#include <context/context.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <string>

namespace unravel
{
class footer_panel
{
public:
    void on_frame_ui_render(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw = {});

private:
    /// How far one kind of job (Compiling Mesh, Compiling Scripts) is in the current batch.
    struct job_kind_progress
    {
        /// Jobs of the kind queued or running when last looked at.
        size_t pending{};
        /// Jobs of the kind the batch has seen so far.
        size_t total{};
    };

    /// Jobs from the moment the pool gets busy until it has been idle for a moment.
    struct job_batch
    {
        bool is_active{};
        std::chrono::steady_clock::time_point started_at{};
        std::chrono::steady_clock::time_point last_busy_at{};
        std::map<std::string, job_kind_progress> kinds;
    };

    void draw_footer_child(rtti::context& ctx, float footer_size, const std::function<void()>& on_draw);
    /// Keeps the job card up while the pool is busy, and shows a summary once it is done.
    void update_job_status(rtti::context& ctx);
    void track_jobs(const std::map<std::string, size_t>& counts);

    job_batch job_batch_;
};
} // namespace unravel
