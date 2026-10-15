#pragma once

#include "image_file_cache.h"

#include <context/context.hpp>

namespace unravel
{
struct loading_screen;

/**
 * @brief The screen the editor shows while it starts and while it opens a project.
 *
 * What is loading (the editor, or a project with its thumbnail) and for how long, the module in
 * work with its step, item and count, what the workers of the thread pool run and what waits
 * behind them, the modules finished so far with their times, and the memory the process takes.
 */
class loading_page
{
public:
    /// Draws the whole screen. Called between the begin and the end of an ImGui frame.
    void draw(rtti::context& ctx, const loading_screen& screen);
    /// Lets go of the images; called before the renderer shuts down.
    void release();

private:
    image_file_cache images_{};
};
} // namespace unravel
