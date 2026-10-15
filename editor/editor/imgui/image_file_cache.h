#pragma once

#include <filesystem/filesystem.h>
#include <graphics/texture.h>

#include <string>
#include <unordered_map>

namespace unravel
{

/**
 * @brief Images read from files on disk for the pages the editor draws itself: the thumbnails of
 * projects and samples, the logo of the loading screen.
 *
 * A file is read once, and again only when refresh() finds it changed on disk. Each image gets a
 * mip chain, so it stays smooth however small it is drawn. Few files are read per frame, so a
 * page full of them comes up without a stall: an image not read yet is null for a frame or two.
 */
class image_file_cache
{
public:
    /// The image of the file, null while it is not read yet, missing or unreadable.
    auto get(const fs::path& path) -> gfx::texture::ptr;
    /// Has the next get of every file look at its write time again.
    void refresh();
    void clear();

private:
    struct entry
    {
        gfx::texture::ptr texture{};
        fs::file_time_type write_time{};
        bool is_checked{};
    };

    /// Takes one read from the budget of the frame; false when it is spent.
    auto take_read_budget() -> bool;

    std::unordered_map<std::string, entry> entries_{};
    int budget_frame_{-1};
    int reads_in_frame_{};
};

} // namespace unravel
