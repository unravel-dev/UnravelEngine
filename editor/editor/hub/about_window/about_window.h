#pragma once

#include <context/context.hpp>
#include <engine/assets/asset_handle.h>
#include <graphics/texture.h>

namespace unravel
{

/**
 * @brief The About window: the engine, its version and license, and where to learn more.
 *
 * It only shows what stays true for a release. The exact build (commits since the release, the
 * revision, the configuration) is in the tooltip of the version and in what the copy button
 * copies, where a bug report needs it.
 */
class about_window
{
public:
    void open();
    void draw(rtti::context& ctx);

private:
    void draw_header(rtti::context& ctx);
    void draw_details();
    void draw_links();
    void draw_footer();
    void copy_version();

    asset_handle<gfx::texture> logo_;
    /// When the version was last copied, for the confirmation on the copy button.
    double copied_time_{-1.0};
    bool is_open_{};
};

} // namespace unravel
