#include "version_manager.h"
#include "imgui/imgui.h"
#include "threadpp/future.hpp"
#include <context/context.hpp>
#include <editor/imgui/integration/imgui_notify.h>

#include <version/version.h>


#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>
#include <hpp/string_view.hpp>
#include <hpp/optional.hpp>

#include <logging/logging.h>
#include <ser20/external/simdjson/simdjson.h>
// #define CPPHTTPLIB_OPENSSL_SUPPORT 1
// #include <httplib.h>
#include <fstream>
#include <filesystem/filesystem.h>
#include <engine/assets/impl/asset_writer.h>

namespace unravel
{

namespace
{
fs::path github_rate_limit_cfg = fs::persistent_path() / "unravel" / "github_rate_limit.cfg";

/// Minimum interval between GitHub API checks.
constexpr auto github_check_interval = std::chrono::hours(6);

/**
 * @brief Reads the last GitHub check timestamp from the cfg file.
 * @return Seconds since epoch of the last check, or 0 if the file doesn't exist or is invalid.
 */
auto read_last_check_time() -> std::int64_t
{
    fs::error_code ec;
    if(!fs::exists(github_rate_limit_cfg, ec) || ec)
    {
        return 0;
    }
    std::ifstream file(github_rate_limit_cfg);
    if(!file.is_open())
    {
        return 0;
    }
    std::int64_t timestamp = 0;
    file >> timestamp;
    if(file.fail())
    {
        return 0;
    }
    return timestamp;
}

/**
 * @brief Writes the current time as the last GitHub check timestamp to the cfg file.
 */
void write_last_check_time()
{
    fs::error_code ec;
    auto parent = github_rate_limit_cfg.parent_path();
    if(!fs::exists(parent, ec))
    {
        fs::create_directories(parent, ec);
    }

    asset_writer::atomic_write_file(github_rate_limit_cfg, [](const fs::path& path)
    {
        std::ofstream file(path, std::ios::trunc);
        if(file.is_open())
        {
            auto now = std::chrono::system_clock::now();
            auto epoch_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            file << epoch_seconds;
        }
    }, ec);
}

/**
 * @brief Checks whether enough time has passed since the last GitHub API check.
 * @return True if a fresh check should be performed.
 */
auto should_check_github() -> bool
{
    auto last_check = read_last_check_time();
    if(last_check == 0)
    {
        return true;
    }
    auto now = std::chrono::system_clock::now();
    auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto elapsed = std::chrono::seconds(now_seconds - last_check);
    return elapsed >= github_check_interval;
}

// github_releases.cpp
//
// Requirements:
//   - cpp-httplib (with OpenSSL enabled)  https://github.com/yhirose/cpp-httplib
//   - simdjson                            https://github.com/simdjson/simdjson
//
// Build idea (example):
//   g++ -std=c++20 github_releases.cpp -lssl -lcrypto -o github_releases
//
// What this does:
//   1) Fetches GitHub releases (optionally only newest N) via REST API
//   2) Lists releases + assets (artifacts)
//   3) Checks if an update is available compared to a "current version"
//      Version pattern: major.minor.patch-commit_count_since_tag[-gSHA]
//      e.g. 1.0.0-66-gc83fa23; 1.0.0-61; 1.0.1-0; etc.

// #include <httplib.h>

// ----------------------------- Data model -----------------------------

struct GitHubAsset
{
    std::string name;
    std::string browser_download_url;
    std::uint64_t size = 0;
    std::string sha256; // Optional: only if you encode it somewhere (GitHub doesn't provide sha256 by default)
};

struct GitHubRelease
{
    std::string tag_name; // Often used as "version"
    std::string name;     // Human title
    bool draft = false;
    bool prerelease = false;
    std::string published_at; // ISO string
    std::vector<GitHubAsset> assets;
};

// ----------------------------- Version parsing & compare -----------------------------

struct EngineVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
    int commit_count = 0; // commit_count_since_tag
    std::string sha;      // optional (without leading 'g' or with, we keep as-is)

    // Original string for debugging/logging
    std::string original;
};

// Trim helpers
static inline auto ltrim_sv(hpp::string_view s) -> hpp::string_view
{
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    return s;
}
static inline auto rtrim_sv(hpp::string_view s) -> hpp::string_view
{
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}
static inline auto trim_sv(hpp::string_view s) -> hpp::string_view
{
    return rtrim_sv(ltrim_sv(s));
}

// Parse int from [pos..] until non-digit
static auto parse_int_token(hpp::string_view s, size_t& pos, int& out) -> bool
{
    if(pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
        return false;
    long long val = 0;
    while(pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])))
    {
        val = val * 10 + (s[pos] - '0');
        if(val > 1'000'000'000LL)
            return false;
        ++pos;
    }
    out = static_cast<int>(val);
    return true;
}

// Expect a specific char at pos
static auto expect_char(hpp::string_view s, size_t& pos, char c) -> bool
{
    if(pos >= s.size() || s[pos] != c)
        return false;
    ++pos;
    return true;
}

// Accept optional prefix like 'v'
static auto strip_optional_v(hpp::string_view s) -> hpp::string_view
{
    s = trim_sv(s);
    if(!s.empty() && (s.front() == 'v' || s.front() == 'V'))
    {
        s.remove_prefix(1);
    }
    return s;
}

// Parse versions like:
//   "1.0.0-66-gc83fa23"
//   "1.0.0-66"
//   "1.0.0"
// Also tolerates leading "v": "v1.0.0-66-g..."
static auto parse_engine_version(hpp::string_view ver) -> std::optional<EngineVersion>
{
    EngineVersion v;
    v.original = std::string(trim_sv(ver));

    hpp::string_view s = strip_optional_v(ver);
    s = trim_sv(s);

    size_t pos = 0;

    if(!parse_int_token(s, pos, v.major))
        return std::nullopt;
    if(!expect_char(s, pos, '.'))
        return std::nullopt;
    if(!parse_int_token(s, pos, v.minor))
        return std::nullopt;
    if(!expect_char(s, pos, '.'))
        return std::nullopt;
    if(!parse_int_token(s, pos, v.patch))
        return std::nullopt;

    // Optional: -commit_count
    if(pos < s.size() && s[pos] == '-')
    {
        ++pos;
        if(!parse_int_token(s, pos, v.commit_count))
            return std::nullopt;

        // Optional: -gSHA or -SHA
        if(pos < s.size() && s[pos] == '-')
        {
            ++pos;
            hpp::string_view rest = s.substr(pos);
            rest = trim_sv(rest);
            if(!rest.empty())
            {
                v.sha = std::string(rest);
                // Advance pos to consume the entire SHA string
                pos = s.size();
            }
        }
    }

    // Must consume everything (except whitespace)
    if(trim_sv(s.substr(pos)).size() != 0)
    {
        return std::nullopt;
    }

    return v;
}

// Return -1 if a<b, 0 if equal, +1 if a>b
static auto compare_versions(const EngineVersion& a, const EngineVersion& b) -> int
{
    if(a.major != b.major)
        return (a.major < b.major) ? -1 : 1;
    if(a.minor != b.minor)
        return (a.minor < b.minor) ? -1 : 1;
    if(a.patch != b.patch)
        return (a.patch < b.patch) ? -1 : 1;

    // Your rule: "1.0.0-66 is newer than 1.0.0-61"
    if(a.commit_count != b.commit_count)
        return (a.commit_count < b.commit_count) ? -1 : 1;

    // SHA doesn't define ordering; ignore.
    return 0;
}

// Extract version from asset filename.
// Pattern: "UnravelEngine-{version}-{platform-info}"
// Example: "UnravelEngine-1.0.0-69-g1ead714-Windows-AMD64.exe" -> "1.0.0-69-g1ead714"
static auto extract_version_from_asset_name(hpp::string_view asset_name) -> std::optional<EngineVersion>
{
    // Look for "UnravelEngine-" prefix
    constexpr hpp::string_view prefix = "UnravelEngine-";
    if(asset_name.size() < prefix.size() || asset_name.substr(0, prefix.size()) != prefix)
    {
        return std::nullopt;
    }
    
    // Get the part after "UnravelEngine-"
    hpp::string_view version_part = asset_name.substr(prefix.size());
    
    // Find where the platform part starts by looking for known platform prefixes
    // Platform names: Windows, Linux, macOS (case-insensitive check for common ones)
    constexpr const char* platform_prefixes[] = {
        "-Windows-", "-Linux-", "-macOS-", "-Darwin-", "-Android-"
    };
    
    size_t version_end = version_part.size();
    for(const char* platform : platform_prefixes)
    {
        size_t pos = version_part.find(platform);
        if(pos != hpp::string_view::npos)
        {
            // Found a platform prefix, try parsing up to this point
            hpp::string_view candidate = version_part.substr(0, pos);
            auto parsed = parse_engine_version(candidate);
            if(parsed)
            {
                version_end = pos;
                break;
            }
        }
    }
    
    // Also try case-insensitive search for platform names
    if(version_end == version_part.size())
    {
        // Look for patterns like "-Windows", "-Linux" (capital letter after dash, followed by more dashes/end)
        for(size_t i = 1; i < version_part.size(); ++i)
        {
            if(version_part[i - 1] == '-' && version_part[i] >= 'A' && version_part[i] <= 'Z')
            {
                // Found a dash followed by capital letter - might be platform
                // Check if we can parse a valid version before this point
                hpp::string_view candidate = version_part.substr(0, i - 1);
                auto parsed = parse_engine_version(candidate);
                if(parsed)
                {
                    version_end = i - 1;
                    break;
                }
            }
        }
    }
    
    // Try parsing the version part
    hpp::string_view version_str = version_part.substr(0, version_end);
    auto result = parse_engine_version(version_str);
    
    // If parsing failed, try the entire remaining string (maybe no platform info)
    if(!result && version_end == version_part.size())
    {
        // Already tried the full string, return nullopt
        return std::nullopt;
    }
    
    return result;
}

// Convenience: returns true if latest > current, nullopt if parsing fails
static auto is_newer_version_available(hpp::string_view current_version_str, hpp::string_view latest_version_str) -> hpp::optional<bool>
{
    auto cur = parse_engine_version(current_version_str);
    auto lat = parse_engine_version(latest_version_str);
    if(!cur || !lat)
    {
        return std::nullopt;
    }
    return compare_versions(*cur, *lat) < 0;
}

// ----------------------------- GitHub API -----------------------------

struct GitHubClientConfig
{
    std::string owner;
    std::string repo;

    // Optional. Strongly recommended for launchers to avoid rate limits.
    // If provided, use "Bearer <token>".
    std::string token;

    // Include drafts? Usually no for public updates.
    bool include_drafts = false;

    // Include prereleases? Up to you.
    bool include_prereleases = false;

    // GitHub REST API host
    std::string host = "api.github.com";

    // How many releases to request per page (max 100).
    int per_page = 50;
};

// Parse JSON into our structs
static auto parse_release_json(const simdjson::dom::element& r) -> GitHubRelease
{
    GitHubRelease rel;
    
    auto tag_name = r["tag_name"];
    if(!tag_name.error())
    {
        std::string_view tag_sv;
        if(tag_name.get(tag_sv) == simdjson::SUCCESS)
            rel.tag_name = std::string(tag_sv);
    }
    
    auto name = r["name"];
    if(!name.error())
    {
        std::string_view name_sv;
        if(name.get(name_sv) == simdjson::SUCCESS)
            rel.name = std::string(name_sv);
    }
    
    auto draft = r["draft"];
    if(!draft.error())
    {
        bool draft_val;
        if(draft.get(draft_val) == simdjson::SUCCESS)
            rel.draft = draft_val;
    }
    
    auto prerelease = r["prerelease"];
    if(!prerelease.error())
    {
        bool prerelease_val;
        if(prerelease.get(prerelease_val) == simdjson::SUCCESS)
            rel.prerelease = prerelease_val;
    }
    
    auto published_at = r["published_at"];
    if(!published_at.error())
    {
        std::string_view published_sv;
        if(published_at.get(published_sv) == simdjson::SUCCESS)
            rel.published_at = std::string(published_sv);
    }

    auto assets = r["assets"];
    if(!assets.error() && assets.type() == simdjson::dom::element_type::ARRAY)
    {
        for(const auto& a : assets)
        {
            GitHubAsset asset;
            
            auto asset_name = a["name"];
            if(!asset_name.error())
            {
                std::string_view name_sv;
                if(asset_name.get(name_sv) == simdjson::SUCCESS)
                    asset.name = std::string(name_sv);
            }
            
            auto browser_url = a["browser_download_url"];
            if(!browser_url.error())
            {
                std::string_view url_sv;
                if(browser_url.get(url_sv) == simdjson::SUCCESS)
                    asset.browser_download_url = std::string(url_sv);
            }
            
            auto size = a["size"];
            if(!size.error())
            {
                uint64_t size_val;
                if(size.get(size_val) == simdjson::SUCCESS)
                    asset.size = size_val;
            }

            // NOTE: GitHub releases API does NOT provide sha256 by default.
            // If you want sha256, you can:
            //   - upload a .sha256 file as a separate asset and parse it
            //   - or store hashes in release notes and parse body
            rel.assets.push_back(std::move(asset));
        }
    }
    return rel;
}

// Result type for fetch operations
struct FetchResult
{
    std::vector<GitHubRelease> releases;
    std::string error_message;
    
    bool has_error() const { return !error_message.empty(); }
};

// Fetch all releases (paginated), filtered by config flags.
// Returns newest-first as GitHub provides.
static auto fetch_github_releases(const GitHubClientConfig& cfg) -> FetchResult
{
    // httplib::SSLClient cli(cfg.host);
    // cli.set_follow_location(true);

    // auto timeout = std::chrono::seconds(10);
    // cli.set_max_timeout(std::chrono::milliseconds(timeout).count());
    // httplib::Headers headers = {{"User-Agent", "UnravelLauncher"}, {"Accept", "application/vnd.github+json"}};
    // if(!cfg.token.empty())
    // {
    //     headers.emplace("Authorization", "Bearer " + cfg.token);
    // }

    // FetchResult result;
    // int page = 1;
    // simdjson::dom::parser parser;

    // while(true)
    // {
    //     std::string path = "/repos/" + cfg.owner + "/" + cfg.repo +
    //                        "/releases?per_page=" + std::to_string(cfg.per_page) + "&page=" + std::to_string(page);

    //     auto res = cli.Get(path.c_str(), headers);
    //     if(!res)
    //     {
    //         result.error_message = "HTTP request failed (no response) for " + path;
    //         return result;
    //     }
    //     if(res->status != 200)
    //     {
    //         result.error_message = "GitHub API error " + std::to_string(res->status) + " for " + path +
    //                                " body: " + res->body;
    //         return result;
    //     }

    //     auto remaining = res->get_header_value("X-RateLimit-Remaining");
    //     auto reset     = res->get_header_value("X-RateLimit-Reset");

    //     APPLOG_TRACE("GitHub Rate Limit: Remaining: {}", remaining);
    //     APPLOG_TRACE("GitHub Rate Limit: Reset: {}", reset);

    //     simdjson::dom::element j;
    //     auto error = parser.parse(res->body).get(j);
    //     if(error != simdjson::SUCCESS)
    //     {
    //         result.error_message = "Failed to parse JSON for " + path + ": " + simdjson::error_message(error);
    //         return result;
    //     }
        
    //     if(j.type() != simdjson::dom::element_type::ARRAY)
    //     {
    //         result.error_message = "Unexpected JSON (expected array) for " + path;
    //         return result;
    //     }

    //     size_t array_size = 0;
    //     for(const auto& r : j)
    //     {
    //         array_size++;
    //         GitHubRelease rel = parse_release_json(r);

    //         if(!cfg.include_drafts && rel.draft)
    //             continue;
    //         if(!cfg.include_prereleases && rel.prerelease)
    //             continue;

    //         result.releases.push_back(std::move(rel));
    //     }

    //     if(array_size == 0)
    //         break;

    //     // If less than per_page, no more pages.
    //     if(static_cast<int>(array_size) < cfg.per_page)
    //         break;

    //     ++page;
    //     // (Optional) hard cap to prevent runaway
    //     if(page > 50)
    //         break;
    // } 
    // return result;
    return {};
}

// Extract the best version from a release (checks both tag_name and asset names).
static auto get_best_version_from_release(const GitHubRelease& release) -> std::optional<EngineVersion>
{
    // First, try tag_name
    auto tag_ver = parse_engine_version(release.tag_name);
    
    // Also check asset names for versions
    std::optional<EngineVersion> asset_ver;
    for(const auto& asset : release.assets)
    {
        auto extracted = extract_version_from_asset_name(asset.name);
        if(extracted)
        {
            // Use the highest version found in assets
            if(!asset_ver || compare_versions(*extracted, *asset_ver) > 0)
            {
                asset_ver = extracted;
            }
        }
    }
    
    // Use the highest version between tag and assets
    if(tag_ver && asset_ver)
    {
        return (compare_versions(*tag_ver, *asset_ver) > 0) ? tag_ver : asset_ver;
    }
    if(tag_ver)
    {
        return tag_ver;
    }
    return asset_ver;
}

// Find "latest" release by comparing tag_name versions and asset versions (your custom scheme).
// Checks both the release tag_name and versions extracted from asset filenames.
// Ignores releases whose tag_name doesn't parse (unless assets have parseable versions).
static auto pick_latest_by_version(const std::vector<GitHubRelease>& releases) -> std::optional<GitHubRelease>
{
    std::optional<GitHubRelease> best;
    std::optional<EngineVersion> best_ver;

    for(const auto& r : releases)
    {
        auto release_ver = get_best_version_from_release(r);
        if(!release_ver)
        {
            // Neither tag nor assets have a parseable version, skip this release
            continue;
        }

        if(!best || compare_versions(*release_ver, *best_ver) > 0)
        {
            best = r;
            best_ver = release_ver;
        }
    }
    return best;
}

// Checks if update exists compared to current_version.
// Returns the chosen latest release if newer, else nullopt.
// Uses your compare: major/minor/patch then commit_count.
// Checks both release tag_name and versions extracted from asset filenames.
static auto check_for_update(const std::vector<GitHubRelease>& releases,
                                                     hpp::string_view current_version) -> std::optional<GitHubRelease>
{
    auto cur = parse_engine_version(current_version);
    if(!cur)
    {
        return std::nullopt;
    }

    auto latest = pick_latest_by_version(releases);
    if(!latest)
        return std::nullopt;

    // Get the best version from the release (considering both tag and assets)
    auto latest_ver = get_best_version_from_release(*latest);
    if(!latest_ver)
        return std::nullopt;

    if(compare_versions(*cur, *latest_ver) < 0)
    {
        return latest;
    }
    return std::nullopt;
}

// ----------------------------- Example usage -----------------------------

static void print_releases_and_assets(const std::vector<GitHubRelease>& releases)
{
    for(const auto& r : releases)
    {
        std::string release_info = "Release: " + r.tag_name;
        if(!r.name.empty())
            release_info += "  (" + r.name + ")";
        if(!r.published_at.empty())
            release_info += "  published: " + r.published_at;
        if(r.prerelease)
            release_info += "  [prerelease]";
        if(r.draft)
            release_info += "  [draft]";
        APPLOG_TRACE("{}", release_info);

        for(const auto& a : r.assets)
        {
            APPLOG_TRACE("  - {}  ({:.2f} MB)", a.name, a.size / (1024.0 * 1024.0));
            APPLOG_TRACE("    {}", a.browser_download_url);
        }
        APPLOG_TRACE("");
    }
}

auto check_for_update(bool force_check = false) -> bool
{
    if(!should_check_github() && !force_check)
    {
        APPLOG_TRACE("Skipping GitHub update check (last check was less than 6 hours ago).");
        return false;
    }

    if(force_check)
    {
        write_last_check_time();
    }

    GitHubClientConfig cfg;
    cfg.owner = "unravel-dev";
    cfg.repo = "UnravelEngine";
    // cfg.token = "ghp_..."; // strongly recommended for a launcher
    cfg.include_drafts = false;
    cfg.include_prereleases = false;
    cfg.per_page = 50;

    // Example current version (your scheme)
    std::string current_version = version::get_full();

    auto fetch_result = fetch_github_releases(cfg);
    if(fetch_result.has_error())
    {
        APPLOG_ERROR("Error fetching releases: {}", fetch_result.error_message);
        return false;
    }


    // Print everything we got (versions + artifacts)
    print_releases_and_assets(fetch_result.releases);

    // Update check
    if(auto update = check_for_update(fetch_result.releases, current_version))
    {
        APPLOG_INFO("UPDATE AVAILABLE!");
        APPLOG_INFO("  Current: {}", current_version);
        APPLOG_INFO("  Latest:  {}", update->tag_name);
        APPLOG_INFO("  Assets:");
        for(const auto& a : update->assets)
        {
            APPLOG_INFO("    * {}", a.name);
        }
        return true;
    }
    else
    {
        APPLOG_TRACE("No update available. Current: {}", current_version);
    }

    return false;
}

} // namespace
auto version_manager::init(rtti::context& ctx) -> bool
{
    tpp::async(
        []()
        {
            return should_check_github();
        })
        .then(tpp::this_thread::get_id(),
              [](auto result)
              {
                  auto has_update = result.get();
                  if(has_update)
                  {
                    ImGuiToast toast(ImGuiToastType_Warning, 99999);
                    toast.set_title("New version available.");
                    toast.set_show_dismiss_button(true);
                    toast.set_on_dismiss(
                        []()
                        {
                            // Write the last check time to the cfg file
                            write_last_check_time();
                        }
                    );
                    toast.set_draw_callback(
                        [](const ImGuiToast& toast, float opacity, const ImVec4& text_color)
                        {
                            ImGui::Text("Download from ");
                            ImGui::SameLine();
                            ImGui::TextLinkOpenURL("Releases.", "https://github.com/unravel-dev/UnravelEngine/releases");
                        });
                    ImGui::PushNotification(toast);
                  }
              });
    return true;
}

auto version_manager::deinit(rtti::context& ctx) -> bool
{
    return true;
}

void version_manager::check_for_update_async()
{
    tpp::async(
        []()
        {
            return check_for_update(true);
        })
        .then(tpp::this_thread::get_id(),
              [](auto result)
              {
                  auto has_update = result.get();
                  if(has_update)
                  {
                    ImGuiToast toast(ImGuiToastType_Warning, 99999);
                    toast.set_title("New version available.");
                    toast.set_show_dismiss_button(true);
                    toast.set_on_dismiss(
                        []()
                        {
                            // Write the last check time to the cfg file
                            write_last_check_time();
                        }
                    );
                    toast.set_draw_callback(
                        [](const ImGuiToast& toast, float opacity, const ImVec4& text_color)
                        {
                            ImGui::Text("Download from ");
                            ImGui::SameLine();
                            ImGui::TextLinkOpenURL("Releases.", "https://github.com/unravel-dev/UnravelEngine/releases");
                        });
                    ImGui::PushNotification(toast);
                  }
                  else
                  {
                    ImGuiToast toast(ImGuiToastType_Warning, 2000);
                    toast.set_title("There are no updates available.");
                    toast.set_content("You are using the latest version.");
                    ImGui::PushNotification(toast);
                  }
              });
}
} // namespace unravel
