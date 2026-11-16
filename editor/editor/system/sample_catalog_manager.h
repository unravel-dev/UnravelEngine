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
    /// Where its picture can be, in order: the fetched copy, then the one the editor ships. The first that is on
    /// disk is shown; another editor's fetch can remove a fetched copy.
    std::vector<fs::path> thumbnails;
    /// The glyph of its stand-in picture while there is none.
    const char* icon{};
    /// The project_guid every copy of the sample keeps; empty when the list names none.
    std::string project_guid;
    /// The oldest editor the sample opens in, when this editor is older; empty otherwise.
    std::string needed_version;
    /// The sample's version, as the list names it; a copy of an older one has an update.
    std::uint32_t version = sample_catalog::FIRST_SAMPLE_VERSION;
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
    /// Lists the newer of the shipped and the fetched samples.json, and starts a fetch when one is due and
    /// Editor Settings allow it.
    auto init(rtti::context& ctx) -> bool;
    /// Lets a fetch in flight end without an answer; its outcome is dropped.
    auto deinit(rtti::context& ctx) -> bool;

    /// The samples the start page shows, in their order.
    auto get_samples() const -> const std::vector<sample_info>&;

    /// The recent project that is a copy of the sample (sample_catalog::find_sample_copy); empty when there is
    /// none on disk.
    auto find_installed(const sample_info& sample, const std::vector<fs::path>& recent_projects) -> fs::path;

    /// The version of the copy of a sample in `copy_dir` (sample_catalog::load_copy_version), read again when its
    /// sample.json changes, as a git pull changes it; nothing when the copy has none.
    auto get_copy_version(const fs::path& copy_dir) -> hpp::optional<std::uint32_t>;

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

    /// A copy's version, and the write time of the sample.json it was read from.
    struct copy_version
    {
        fs::file_time_type write_time{};
        hpp::optional<std::uint32_t> version;
        bool is_read{};
    };
    std::unordered_map<std::string, copy_version> copy_versions_;
    /// Shared with the fetch in flight, which can outlive deinit.
    std::shared_ptr<std::atomic<bool>> is_stopping_ = std::make_shared<std::atomic<bool>>(false);
};

} // namespace unravel
