#pragma once

#include <cstddef>
#include <string>

/**
 * @brief The screen the editor shows while it starts up and while it opens a project: the stage
 * being loaded with its progress, and what the process takes of the GPU and system memory.
 */
namespace unravel::loading_page
{
struct progress_info
{
    /// What is being loaded, e.g. "Loading [Engine Assets]".
    const std::string& stage;
    size_t completed{};
    /// 0 while the stage has no count to report.
    size_t total{};
    /// The item of the stage in work, may be empty.
    const std::string& current_job;
};

/// Draws the whole screen. Called between the begin and the end of an ImGui frame.
void draw(const progress_info& info);
} // namespace unravel::loading_page
