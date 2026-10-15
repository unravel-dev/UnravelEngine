#pragma once

#include <filesystem/filesystem.h>

#include <array>
#include <string>
#include <vector>

/**
 * @brief The sample projects the start page offers: what each one shows, where it lives on GitHub,
 * and whether a copy of it is already among the recent projects.
 */
namespace unravel::sample_catalog
{
constexpr size_t MAX_TAGS = 3;

struct sample_info
{
    const char* title{};
    /// The name of the repository on GitHub, and of the folder a clone of it gets.
    const char* repository{};
    /// One line under the title: the area of the engine the sample is about.
    const char* topic{};
    const char* description{};
    std::array<const char*, MAX_TAGS> tags{};
    /// The picture the editor ships for it, an editor:/ path.
    const char* thumbnail{};
    /// The glyph of its stand-in picture while there is none.
    const char* icon{};
    bool is_new{};
};

auto get_samples() -> const std::vector<sample_info>&;

auto get_repository_url(const sample_info& sample) -> std::string;
auto get_download_url(const sample_info& sample) -> std::string;

/// The recent project that is a copy of the sample (its folder has the name of the repository),
/// empty when there is none on disk.
auto find_installed(const sample_info& sample, const std::vector<fs::path>& recent_projects) -> fs::path;
} // namespace unravel::sample_catalog
