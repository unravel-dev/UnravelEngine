#pragma once

#include <context/context.hpp>
#include <filesystem/filesystem.h>

#include <sample_catalog/sample_fetch.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace unravel
{

/// A sample as the start page shows it.
struct sample_info
{
    std::string title;
    /// The name of the repository on GitHub, and of the folder a clone of it gets.
    std::string repository;
    /// One line over the title: the area of the engine the sample is about.
    std::string topic;
    std::string description;
    std::vector<std::string> tags;
    std::string repository_url;
    std::string download_url;
    /// The picture to show: the fetched copy, else the one the editor ships; empty when there is neither.
    fs::path thumbnail;
    /// The glyph of its stand-in picture while there is none.
    const char* icon{};
    /// The project_guid every copy of the sample keeps; empty when the list names none.
    std::string project_guid;
    /// The oldest editor the sample opens in, when this editor is older; empty otherwise.
    std::string needed_version;
    bool is_new{};
};

/**
 * @brief The sample projects the start page offers.
 *
 * At start it reads the samples.json the editor ships and the one it fetched last, and lists the newer one. When
 * Editor Settings allow it and the last attempt is old enough (sample_catalog::is_fetch_due), it fetches the latest
 * release's samples.json and the pictures of its samples on a pool thread, and lists that file once it arrived. The
 * fetch fails silently; the frame never waits for it.
 */
class sample_catalog_manager
{
public:
    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    auto get_samples() const -> const std::vector<sample_info>&;

    /// The recent project that is a copy of the sample: its folder has the name of the repository, or its
    /// project.cfg has the sample's project_guid. Empty when there is none on disk.
    auto find_installed(const sample_info& sample, const std::vector<fs::path>& recent_projects) -> fs::path;

private:
    void start_fetch();
    void apply_outcome(const sample_catalog::fetch_outcome& outcome);
    /// Lists the samples of `manifest`.
    void use_manifest(sample_catalog::sample_manifest manifest);
    /// The project_guid in the project.cfg of `project_dir`, read once; empty when it has none.
    auto get_project_guid(const fs::path& project_dir) -> const std::string&;

    sample_catalog::sample_manifest manifest_;
    std::vector<sample_info> samples_;
    std::unordered_map<std::string, std::string> project_guids_;
    /// Shared with the fetch in flight, which can outlive deinit.
    std::shared_ptr<std::atomic<bool>> is_stopping_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace unravel
