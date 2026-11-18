#pragma once

#include <filesystem/filesystem.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/// What a finished deploy built: the size of every file it shipped and how long each step took, kept per profile so
/// the next deploy can say what grew.
namespace unravel::deployment
{

/// One file of a build's data, or one entry of a pack in it.
struct deploy_report_file
{
    /// Its path below the build's data root ("data/app/compiled/textures/a.png.asset"); a pack's entry is the pack's
    /// path, a colon and the entry's name ("data/app/content.pak:compiled/textures/a.png.asset").
    std::string path;
    /// The kind of content ("Textures", "Audio", "Scripts", "Runtime"), as the report groups it.
    std::string category;
    /// Its bytes on disk; a pack entry's stored bytes, compressed as the pack keeps it.
    std::uint64_t bytes{};
};

/// How long a step of the deploy took.
struct deploy_report_step
{
    std::string name;
    double seconds{};
};

/// A finished deploy, as the profile's cache keeps it.
struct deploy_report
{
    std::string profile;
    /// "desktop", "android" or "web".
    std::string platform;
    std::string engine_version;
    /// Seconds since the Unix epoch when the deploy finished.
    std::int64_t finished_at{};
    /// Every file of the profile's folder in the deploy folder: what players install (a package for Android, the
    /// site for the web).
    std::uint64_t build_bytes{};
    std::vector<deploy_report_step> steps;
    std::vector<deploy_report_file> files;

    /// Bytes of the data files by category, largest first.
    auto get_category_totals() const -> std::vector<std::pair<std::string, std::uint64_t>>;
};

/// What changed from one deploy report of a profile to the next.
struct deploy_report_change
{
    std::int64_t build_bytes{};
    /// Each category whose bytes changed, by how much, largest change first.
    std::vector<std::pair<std::string, std::int64_t>> categories;
    /// Files that are new, gone or changed size, by how much (a gone file by minus its bytes), largest change first.
    std::vector<std::pair<std::string, std::int64_t>> files;
};

/// The steps' times of a running deploy; any thread.
class deploy_step_times
{
public:
    void add(const std::string& name, double seconds);
    auto get() const -> std::vector<deploy_report_step>;

private:
    mutable std::mutex mutex_;
    std::vector<deploy_report_step> steps_;
};

/// The category a build's file falls in, by its name: an asset by its source's type, else what the file is.
auto get_deploy_file_category(const std::string& path) -> std::string;

/**
 * @brief The files below `data_root` with their sizes, the entries of every pack among them included (each entry its
 * stored bytes, the pack's own table of contents left out). Sorted by path. Any thread.
 */
auto list_deploy_files(const fs::path& data_root) -> std::vector<deploy_report_file>;

/// The total bytes of the files below `folder`, or of the file itself.
auto measure_build_bytes(const fs::path& folder) -> std::uint64_t;

auto compare_deploy_reports(const deploy_report& older, const deploy_report& newer) -> deploy_report_change;

/// The profile's last deploy report and the one before it, in the profile's cache.
auto get_deploy_report_path(const fs::path& project_root, const std::string& profile) -> fs::path;
auto get_previous_deploy_report_path(const fs::path& project_root, const std::string& profile) -> fs::path;

auto write_deploy_report(const deploy_report& report) -> std::string;
auto read_deploy_report(const std::string& text, deploy_report& report, std::string& error) -> bool;
auto load_deploy_report(const fs::path& file, deploy_report& report, std::string& error) -> bool;

/// Keeps `report` as the profile's last deploy report, the last one becoming the one before it.
auto save_deploy_report(const fs::path& project_root, const deploy_report& report) -> bool;

} // namespace unravel::deployment
