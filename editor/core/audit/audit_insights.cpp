#include "audit_facts.h"
#include "audit_rules.h"

#include <logging/logging.h>

#include <algorithm>
#include <ctime>
#include <map>

namespace unravel::audit::detail
{
namespace
{

/// The most rows a table of assets lists, largest first.
constexpr std::size_t MAX_LISTED_ASSETS = 200;

constexpr const char* GROUP_ASSETS = "Assets";
constexpr const char* GROUP_BUILDS = "Builds";
constexpr const char* GROUP_SCRIPTS = "Scripts";

/// How a table is grouped, its columns read and its rows sorted at first.
struct table_layout
{
    const char* group;
    std::vector<column_kind> kinds;
    int sort_column{-1};
    bool sort_descending{false};
    const char* empty_text{""};
};

void set_layout(insight_table& table, table_layout layout)
{
    table.group = layout.group;
    table.kinds = std::move(layout.kinds);
    table.sort_column = layout.sort_column;
    table.sort_descending = layout.sort_descending;
    table.empty_text = layout.empty_text;
}

auto text_cell(std::string text, std::string link = {}) -> insight_cell
{
    insight_cell cell;
    cell.text = std::move(text);
    cell.link = std::move(link);
    return cell;
}

auto number_cell(double number, std::string text) -> insight_cell
{
    insight_cell cell;
    cell.text = std::move(text);
    cell.number = number;
    cell.is_number = true;
    return cell;
}

auto bytes_cell(std::uint64_t bytes) -> insight_cell
{
    return number_cell(static_cast<double>(bytes), format_bytes(bytes));
}

auto count_cell(std::uint64_t count) -> insight_cell
{
    return number_cell(static_cast<double>(count), std::to_string(count));
}

auto get_type_name(const asset_record& asset) -> std::string
{
    auto extension = fs::path(asset.key).extension().generic_string();
    if(!extension.empty() && extension.front() == '.')
    {
        extension.erase(extension.begin());
    }
    return extension;
}

auto list_shipping_profiles(const input& in, const std::string& key) -> std::string
{
    std::string names;
    for(const auto& profile : in.profiles)
    {
        if(is_shipped_by(profile, key))
        {
            names += names.empty() ? profile.name : ", " + profile.name;
        }
    }
    return names.empty() ? std::string("None") : names;
}

auto get_platform_title(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return "Android";
        case target_platform::web:
            return "Web";
        case target_platform::desktop:
        default:
            return "Desktop";
    }
}

auto make_profile_table(const input& in) -> insight_table
{
    insight_table table{"profiles",
                        "Profiles",
                        "What each profile's deploy ships of the project's assets, with the sizes of their compiled files "
                        "in the editor's cache. A build's own sizes are in its deploy report.",
                        {"Profile", "Platform", "Shipped Assets", "Compiled Size", "Left Out"},
                        {}};
    set_layout(table,
               {GROUP_BUILDS,
                {column_kind::text, column_kind::text, column_kind::number, column_kind::bytes, column_kind::number},
                -1,
                false,
                "The project has no settings profiles."});
    for(const auto& profile : in.profiles)
    {
        std::uint64_t shipped = 0;
        std::uint64_t bytes = 0;
        std::uint64_t left_out = 0;
        for(const auto& entry : profile.content.entries)
        {
            if(entry.key.rfind("app:", 0) != 0)
            {
                continue;
            }
            if(deployment::is_shipped(profile.deploy, entry))
            {
                ++shipped;
                bytes += entry.size;
            }
            else
            {
                ++left_out;
            }
        }
        table.rows.push_back({text_cell(profile.name),
                              text_cell(get_platform_title(profile.platform)),
                              count_cell(shipped),
                              bytes_cell(bytes),
                              count_cell(left_out)});
    }
    return table;
}

auto make_largest_table(const input& in) -> insight_table
{
    insight_table table{"largest_assets",
                        "Largest Assets",
                        "The project's assets by the size of their compiled files, largest first, and the profiles that "
                        "ship them.",
                        {"Asset", "Type", "Compiled Size", "Shipped By"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::path, column_kind::text, column_kind::bytes, column_kind::text},
                2,
                true,
                "No asset has a compiled file yet. Let the editor finish compiling and run the audit again."});
    std::vector<const asset_record*> sorted;
    for(const auto& asset : in.assets)
    {
        if(asset.compiled_size > 0)
        {
            sorted.push_back(&asset);
        }
    }
    std::sort(sorted.begin(),
              sorted.end(),
              [](const asset_record* lhs, const asset_record* rhs)
              {
                  return lhs->compiled_size > rhs->compiled_size;
              });
    for(std::size_t index = 0; index < sorted.size() && index < MAX_LISTED_ASSETS; ++index)
    {
        const auto& asset = *sorted[index];
        table.rows.push_back({text_cell(asset.key, asset.key),
                              text_cell(get_type_name(asset)),
                              bytes_cell(asset.compiled_size),
                              text_cell(list_shipping_profiles(in, asset.key))});
    }
    return table;
}

auto is_used_by_any_profile(const input& in, const std::string& key) -> bool
{
    return std::any_of(in.profiles.begin(),
                       in.profiles.end(),
                       [&key](const profile_record& profile)
                       {
                           const auto* entry = profile.content.find(key);
                           return entry != nullptr && entry->is_used;
                       });
}

auto make_unused_table(const input& in) -> insight_table
{
    insight_table table{"unused_assets",
                        "Unused Assets",
                        "Assets no profile's scenes reach, through any scene, prefab, material, setting or script path. "
                        "A script that builds a path at run time can still load them.",
                        {"Asset", "Type", "Compiled Size"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::path, column_kind::text, column_kind::bytes},
                2,
                true,
                "Every asset a deploy can ship is reached from some profile's scenes."});
    std::vector<const asset_record*> unused;
    for(const auto& asset : in.assets)
    {
        if(asset.is_generated || in.profiles.empty() || is_used_by_any_profile(in, asset.key))
        {
            continue;
        }
        const auto* entry = in.profiles.front().content.find(asset.key);
        if(entry == nullptr)
        {
            // Not an asset a deploy can ship (a script).
            continue;
        }
        unused.push_back(&asset);
    }
    std::sort(unused.begin(),
              unused.end(),
              [](const asset_record* lhs, const asset_record* rhs)
              {
                  return lhs->compiled_size > rhs->compiled_size;
              });
    for(const auto* asset : unused)
    {
        table.rows.push_back({text_cell(asset->key, asset->key), text_cell(get_type_name(*asset)), bytes_cell(asset->compiled_size)});
    }
    return table;
}

auto make_texture_table(const input& in) -> insight_table
{
    insight_table table{"textures",
                        "Textures",
                        "Every texture as the editor compiled it: its size, mipmaps and format.",
                        {"Texture", "Width", "Height", "Mipmaps", "Format", "Compiled Size"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::path, column_kind::number, column_kind::number, column_kind::number, column_kind::text, column_kind::bytes},
                5,
                true,
                "The project has no texture with a compiled file."});
    for(const auto& asset : in.assets)
    {
        if(!asset.facts.texture)
        {
            continue;
        }
        const auto& facts = *asset.facts.texture;
        table.rows.push_back({text_cell(asset.key, asset.key),
                              count_cell(facts.width),
                              count_cell(facts.height),
                              count_cell(facts.mips),
                              text_cell(facts.format),
                              bytes_cell(asset.compiled_size)});
    }
    return table;
}

auto make_audio_table(const input& in) -> insight_table
{
    insight_table table{"audio",
                        "Audio Clips",
                        "Every clip as it decodes when it loads: its length, channels, rate and memory.",
                        {"Clip", "Length", "Channels", "Sample Rate", "Decoded Size", "Compiled Size"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::path, column_kind::number, column_kind::number, column_kind::number, column_kind::bytes, column_kind::bytes},
                4,
                true,
                "The project has no audio clip with a compiled file."});
    for(const auto& asset : in.assets)
    {
        if(!asset.facts.audio)
        {
            continue;
        }
        const auto& facts = *asset.facts.audio;
        table.rows.push_back({text_cell(asset.key, asset.key),
                              number_cell(facts.seconds, fmt::format("{:.1f} s", facts.seconds)),
                              count_cell(facts.channels),
                              number_cell(facts.sample_rate, fmt::format("{} Hz", facts.sample_rate)),
                              bytes_cell(facts.decoded_bytes),
                              bytes_cell(asset.compiled_size)});
    }
    return table;
}

auto make_mesh_table(const input& in) -> insight_table
{
    insight_table table{"meshes",
                        "Meshes",
                        "Every mesh as the editor compiled it: its counts and generated LODs.",
                        {"Mesh", "Vertices", "Triangles", "Submeshes", "LODs", "Compiled Size"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::path, column_kind::number, column_kind::number, column_kind::number, column_kind::number, column_kind::bytes},
                2,
                true,
                "The project has no mesh with a compiled file."});
    for(const auto& asset : in.assets)
    {
        if(!asset.facts.mesh)
        {
            continue;
        }
        const auto& facts = *asset.facts.mesh;
        table.rows.push_back({text_cell(asset.key, asset.key),
                              count_cell(facts.vertices),
                              count_cell(facts.triangles),
                              count_cell(facts.submeshes),
                              count_cell(facts.lods),
                              bytes_cell(asset.compiled_size)});
    }
    return table;
}

auto format_change(std::int64_t bytes) -> std::string
{
    if(bytes == 0)
    {
        return "0";
    }
    const auto size = format_bytes(static_cast<std::uint64_t>(bytes < 0 ? -bytes : bytes));
    return bytes < 0 ? "-" + size : "+" + size;
}

auto change_cell(std::int64_t bytes) -> insight_cell
{
    return number_cell(static_cast<double>(bytes), format_change(bytes));
}

auto format_report_time(std::int64_t seconds) -> std::string
{
    const auto time = static_cast<std::time_t>(seconds);
    const std::tm* local = std::localtime(&time);
    if(local == nullptr)
    {
        return "unknown";
    }
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", local);
    return text;
}

auto make_deploy_table(const input& in) -> insight_table
{
    insight_table table{"deploys",
                        "Deploys",
                        "Each profile's last finished deploy, from the report its cache keeps: the size of the build "
                        "players install and its change since the deploy before it.",
                        {"Profile", "Deployed", "Engine", "Build Size", "Change", "Data Files", "Longest Step"},
                        {}};
    set_layout(table,
               {GROUP_BUILDS,
                {column_kind::text,
                 column_kind::time,
                 column_kind::text,
                 column_kind::bytes,
                 column_kind::change,
                 column_kind::bytes,
                 column_kind::text},
                -1,
                false,
                "No profile has a deploy report yet: deploy a profile to see the size of its build here."});
    for(const auto& profile : in.profiles)
    {
        if(!profile.last_deploy)
        {
            continue;
        }
        const auto& report = *profile.last_deploy;
        std::uint64_t data = 0;
        for(const auto& file : report.files)
        {
            data += file.bytes;
        }
        // Steps run side by side, so the longest one says where the time went.
        const deployment::deploy_report_step* longest = nullptr;
        for(const auto& step : report.steps)
        {
            if(longest == nullptr || step.seconds > longest->seconds)
            {
                longest = &step;
            }
        }
        const auto change = profile.previous_deploy ? deployment::compare_deploy_reports(*profile.previous_deploy, report).build_bytes
                                                    : std::int64_t{0};
        table.rows.push_back({text_cell(profile.name),
                              number_cell(static_cast<double>(report.finished_at), format_report_time(report.finished_at)),
                              text_cell(report.engine_version),
                              bytes_cell(report.build_bytes),
                              profile.previous_deploy ? change_cell(change) : text_cell("No earlier deploy"),
                              bytes_cell(data),
                              longest != nullptr
                                  ? number_cell(longest->seconds, fmt::format("{}, {:.1f} s", longest->name, longest->seconds))
                                  : text_cell("Not recorded")});
    }
    return table;
}

auto make_deploy_contents_table(const input& in) -> insight_table
{
    insight_table table{"deploy_contents",
                        "Deploy Contents",
                        "What each profile's last deploy shipped, by kind of content, and the change since the deploy "
                        "before it. Packed files count at their compressed size.",
                        {"Profile", "Content", "Size", "Change"},
                        {}};
    set_layout(table,
               {GROUP_BUILDS,
                {column_kind::text, column_kind::text, column_kind::bytes, column_kind::change},
                -1,
                false,
                "No profile has a deploy report yet: deploy a profile to see what its build ships here."});
    for(const auto& profile : in.profiles)
    {
        if(!profile.last_deploy)
        {
            continue;
        }
        std::vector<std::pair<std::string, std::int64_t>> changes;
        if(profile.previous_deploy)
        {
            changes = deployment::compare_deploy_reports(*profile.previous_deploy, *profile.last_deploy).categories;
        }
        for(const auto& [category, bytes] : profile.last_deploy->get_category_totals())
        {
            const auto found = std::find_if(changes.begin(),
                                            changes.end(),
                                            [&category](const auto& entry)
                                            {
                                                return entry.first == category;
                                            });
            table.rows.push_back({text_cell(profile.name),
                                  text_cell(category),
                                  bytes_cell(bytes),
                                  change_cell(found != changes.end() ? found->second : 0)});
        }
    }
    return table;
}

auto make_entry_point_table(const input& in) -> insight_table
{
    insight_table table{"update_methods",
                        "Update Methods",
                        "The scripts' update methods the analysis started from, and how many of the project's methods "
                        "each one reaches.",
                        {"Method", "Script", "Line", "Reaches"},
                        {}};
    set_layout(table,
               {GROUP_SCRIPTS,
                {column_kind::text, column_kind::path, column_kind::number, column_kind::number},
                3,
                true,
                "No script overrides OnUpdate, OnFixedUpdate or OnLateUpdate, or starts a coroutine."});
    for(const auto& point : in.code.entry_points)
    {
        table.rows.push_back({text_cell(point.method),
                              text_cell(point.file, point.file),
                              count_cell(point.line),
                              count_cell(point.reachable)});
    }
    return table;
}

auto make_type_table(const input& in) -> insight_table
{
    insight_table table{"assets_by_type",
                        "Assets by Type",
                        "The project's assets by file type: how many there are and the size of their compiled files.",
                        {"Type", "Assets", "Compiled Size"},
                        {}};
    set_layout(table,
               {GROUP_ASSETS,
                {column_kind::text, column_kind::number, column_kind::bytes},
                2,
                true,
                "No asset has a compiled file yet. Let the editor finish compiling and run the audit again."});
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> by_type;
    for(const auto& asset : in.assets)
    {
        if(asset.compiled_size == 0)
        {
            continue;
        }
        auto& entry = by_type[get_type_name(asset)];
        ++entry.first;
        entry.second += asset.compiled_size;
    }
    for(const auto& [type, entry] : by_type)
    {
        table.rows.push_back({text_cell(type.empty() ? std::string("(none)") : type), count_cell(entry.first), bytes_cell(entry.second)});
    }
    return table;
}

} // namespace

auto make_insights(const input& in) -> std::vector<insight_table>
{
    std::vector<insight_table> tables;
    tables.push_back(make_largest_table(in));
    tables.push_back(make_type_table(in));
    tables.push_back(make_unused_table(in));
    tables.push_back(make_texture_table(in));
    tables.push_back(make_audio_table(in));
    tables.push_back(make_mesh_table(in));
    tables.push_back(make_profile_table(in));
    tables.push_back(make_deploy_table(in));
    tables.push_back(make_deploy_contents_table(in));
    if(in.code.ran)
    {
        tables.push_back(make_entry_point_table(in));
    }
    return tables;
}

} // namespace unravel::audit::detail
