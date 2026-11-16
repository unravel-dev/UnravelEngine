#include "sample_catalog_manager.h"

#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/meta/project/project_info.hpp>
#include <editor/system/project_manager.h>
#include <logging/logging.h>
#include <sample_catalog/sample_cache.h>
#include <sample_catalog/sample_state.h>
#include <threadpp/future.hpp>
#include <version/version.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>

namespace unravel
{
namespace
{

/// Where the editor ships its copy of samples.json and the pictures of those samples.
constexpr const char* SHIPPED_SAMPLES_DIR = "editor:/samples";
constexpr const char* SHIPPED_SAMPLES_FILE_NAME = "samples.json";
constexpr const char* SAMPLE_PROJECT_INFO_FILE_NAME = "project.cfg";
/// A sample shows as NEW for this many days after it was published.
constexpr std::int64_t SAMPLE_NEW_DAYS = 45;
constexpr std::int64_t SAMPLE_SECONDS_PER_DAY = 24 * 60 * 60;

/// An icon name a sample.json may give, and the glyph it stands for.
struct sample_icon
{
    const char* name;
    const char* glyph;
};

constexpr std::array<sample_icon, 17> SAMPLE_ICONS{{
    {"auto-fix", ICON_MDI_AUTO_FIX},
    {"bowling", ICON_MDI_BOWLING},
    {"camera-outline", ICON_MDI_CAMERA_OUTLINE},
    {"car-sports", ICON_MDI_CAR_SPORTS},
    {"cube-outline", ICON_MDI_CUBE_OUTLINE},
    {"gamepad-variant", ICON_MDI_GAMEPAD_VARIANT},
    {"lightbulb-on-outline", ICON_MDI_LIGHTBULB_ON_OUTLINE},
    {"music", ICON_MDI_MUSIC},
    {"numeric-2-box", ICON_MDI_NUMERIC_2_BOX},
    {"palette", ICON_MDI_PALETTE},
    {"puzzle", ICON_MDI_PUZZLE},
    {"run", ICON_MDI_RUN},
    {"shape-outline", ICON_MDI_SHAPE_OUTLINE},
    {"sword-cross", ICON_MDI_SWORD_CROSS},
    {"terrain", ICON_MDI_TERRAIN},
    {"view-dashboard-outline", ICON_MDI_VIEW_DASHBOARD_OUTLINE},
    {"white-balance-sunny", ICON_MDI_WHITE_BALANCE_SUNNY},
}};
/// The glyph of a sample whose icon is not one of SAMPLE_ICONS.
constexpr const char* SAMPLE_DEFAULT_ICON = ICON_MDI_GAMEPAD_VARIANT;

auto get_sample_glyph(const std::string& name) -> const char*
{
    const auto found = std::find_if(SAMPLE_ICONS.begin(),
                                    SAMPLE_ICONS.end(),
                                    [&](const sample_icon& icon)
                                    {
                                        return name == icon.name;
                                    });
    return found != SAMPLE_ICONS.end() ? found->glyph : SAMPLE_DEFAULT_ICON;
}

auto is_sample_fetch_enabled(rtti::context& ctx) -> bool
{
    return ctx.get_cached<project_manager>().get_editor_settings().updates.fetch_samples;
}

auto get_shipped_samples_dir() -> fs::path
{
    return fs::resolve_protocol(SHIPPED_SAMPLES_DIR);
}

/// The newer of the samples.json the editor ships and the one it fetched last.
auto load_newest_sample_manifest() -> sample_catalog::sample_manifest
{
    const fs::path shipped_path = get_shipped_samples_dir() / SHIPPED_SAMPLES_FILE_NAME;
    auto shipped = sample_catalog::load_manifest_file(shipped_path);
    if(!shipped)
    {
        APPLOG_WARNING("The list of samples the editor ships could not be read from {}.", shipped_path.string());
    }
    auto fetched = sample_catalog::load_manifest_file(sample_catalog::get_cached_manifest_path());
    if(fetched && (!shipped || sample_catalog::is_newer_manifest(*fetched, *shipped)))
    {
        return std::move(*fetched);
    }
    return shipped ? std::move(*shipped) : sample_catalog::sample_manifest{};
}

/// The fetched picture of the sample, else the one the editor ships; empty when there is neither.
auto find_sample_thumbnail(const sample_catalog::sample_entry& sample) -> fs::path
{
    for(const fs::path& path : {sample_catalog::get_thumbnails_dir() / sample_catalog::get_thumbnail_file_name(sample),
                                get_shipped_samples_dir() / sample_catalog::get_shipped_thumbnail_file_name(sample)})
    {
        fs::error_code error;
        if(fs::is_regular_file(path, error))
        {
            return path;
        }
    }
    return {};
}

/// Whether the sample was published within SAMPLE_NEW_DAYS of today.
auto is_recent_sample(const sample_catalog::sample_entry& sample) -> bool
{
    const auto added = sample_catalog::parse_date(sample.added);
    if(!added)
    {
        return false;
    }
    const std::int64_t today = sample_catalog::get_epoch_seconds(std::chrono::system_clock::now()) / SAMPLE_SECONDS_PER_DAY;
    return today - *added <= SAMPLE_NEW_DAYS;
}

/// The oldest editor the sample opens in, when this editor is older; empty otherwise and for a build without a
/// version.
auto get_needed_editor_version(const sample_catalog::sample_entry& sample) -> std::string
{
    const auto own = version::parse(version::get_full());
    if(!sample.min_engine || !own || version::compare(*own, *sample.min_engine) >= 0)
    {
        return {};
    }
    return sample.min_engine->to_string();
}

auto make_sample_info(const sample_catalog::sample_entry& sample) -> sample_info
{
    sample_info info;
    info.title = sample.title;
    info.repository = sample.repository;
    info.topic = sample.topic;
    info.description = sample.description;
    info.tags = sample.tags;
    info.repository_url = sample_catalog::get_repository_url(sample);
    info.download_url = sample_catalog::get_download_url(sample);
    info.thumbnail = find_sample_thumbnail(sample);
    info.icon = get_sample_glyph(sample.icon);
    info.project_guid = sample.project_guid;
    info.needed_version = get_needed_editor_version(sample);
    info.is_new = is_recent_sample(sample);
    return info;
}

auto is_same_folder_name(const std::string& lhs, const std::string& rhs) -> bool
{
    return std::equal(lhs.begin(),
                      lhs.end(),
                      rhs.begin(),
                      rhs.end(),
                      [](char lhs_character, char rhs_character)
                      {
                          return std::tolower(static_cast<unsigned char>(lhs_character)) ==
                                 std::tolower(static_cast<unsigned char>(rhs_character));
                      });
}

auto is_existing_folder(const fs::path& path) -> bool
{
    fs::error_code error;
    return fs::exists(path, error);
}

void log_sample_fetch_outcome(const sample_catalog::fetch_outcome& outcome)
{
    if(!outcome.manifest)
    {
        APPLOG_TRACE("Sample catalog: no answer. {}", outcome.error);
        return;
    }
    APPLOG_TRACE("Sample catalog: samples.json of {} lists {} samples.",
                 outcome.manifest->generated,
                 outcome.manifest->samples.size());
    for(const std::string& error : outcome.thumbnail_errors)
    {
        APPLOG_TRACE("Sample catalog: {}", error);
    }
}

} // namespace

auto sample_catalog_manager::init(rtti::context& ctx) -> bool
{
    use_manifest(load_newest_sample_manifest());
    if(!is_sample_fetch_enabled(ctx))
    {
        return true;
    }
    const auto state = sample_catalog::load_fetch_state(sample_catalog::get_fetch_state_path());
    if(sample_catalog::is_fetch_due(state, std::chrono::system_clock::now()))
    {
        start_fetch();
    }
    return true;
}

auto sample_catalog_manager::deinit(rtti::context& ctx) -> bool
{
    is_stopping_->store(true);
    return true;
}

auto sample_catalog_manager::get_samples() const -> const std::vector<sample_info>&
{
    return samples_;
}

auto sample_catalog_manager::find_installed(const sample_info& sample, const std::vector<fs::path>& recent_projects)
    -> fs::path
{
    for(const fs::path& project : recent_projects)
    {
        if(is_same_folder_name(project.filename().string(), sample.repository) && is_existing_folder(project))
        {
            return project;
        }
    }
    if(sample.project_guid.empty())
    {
        return {};
    }
    // A copy in a folder of another name, such as the one a ZIP download unpacks to.
    for(const fs::path& project : recent_projects)
    {
        if(get_project_guid(project) == sample.project_guid && is_existing_folder(project))
        {
            return project;
        }
    }
    return {};
}

void sample_catalog_manager::start_fetch()
{
    auto is_stopping = is_stopping_;
    tpp::async(
        [is_stopping]()
        {
            return sample_catalog::fetch_samples(
                [is_stopping]()
                {
                    return is_stopping->load();
                });
        })
        .then(tpp::this_thread::get_id(),
              [this, is_stopping](auto result)
              {
                  // Once the editor closes, the manager may be gone.
                  if(is_stopping->load())
                  {
                      return;
                  }
                  apply_outcome(result.get());
              });
}

void sample_catalog_manager::apply_outcome(const sample_catalog::fetch_outcome& outcome)
{
    log_sample_fetch_outcome(outcome);
    // Other editors share the file: the attempt goes onto what it holds now.
    const fs::path state_path = sample_catalog::get_fetch_state_path();
    auto state = sample_catalog::load_fetch_state(state_path);
    sample_catalog::record_fetch(state, outcome.manifest.has_value(), std::chrono::system_clock::now());
    if(!sample_catalog::save_fetch_state(state_path, state))
    {
        APPLOG_TRACE("Sample catalog: {} could not be written.", state_path.string());
    }
    // An editor newer than the latest release's copy ships a newer list.
    if(outcome.manifest && !sample_catalog::is_newer_manifest(manifest_, *outcome.manifest))
    {
        use_manifest(*outcome.manifest);
    }
}

void sample_catalog_manager::use_manifest(sample_catalog::sample_manifest manifest)
{
    manifest_ = std::move(manifest);
    samples_.clear();
    samples_.reserve(manifest_.samples.size());
    for(const sample_catalog::sample_entry& sample : manifest_.samples)
    {
        samples_.push_back(make_sample_info(sample));
    }
}

auto sample_catalog_manager::get_project_guid(const fs::path& project_dir) -> const std::string&
{
    const std::string key = project_dir.generic_string();
    auto found = project_guids_.find(key);
    if(found == project_guids_.end())
    {
        project_info info;
        load_from_file((project_dir / SAMPLE_PROJECT_INFO_FILE_NAME).string(), info);
        found = project_guids_.emplace(key, info.project_guid).first;
    }
    return found->second;
}

} // namespace unravel
