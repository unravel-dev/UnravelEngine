#include "web_public_link.h"
#include "deploy_preflight.h"
#include "web_preview_server.h"

#include <deploy/web.h>
#include <editor/system/project_manager.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>

#include <array>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <vector>

namespace unravel::deployment
{
namespace
{
/// What cloudflared prints once its tunnel's first connection to Cloudflare is up.
constexpr const char* TUNNEL_CONNECTED = "Registered tunnel connection";
/// The level cloudflared tags its error lines with.
constexpr const char* ERROR_LEVEL = " ERR ";
/// The last error lines a stopped tunnel's message quotes.
constexpr std::size_t QUOTED_ERROR_LINES = 3;
/// The page a web deploy's folder holds once deployed.
constexpr const char* PAGE_FILE = "index.html";
constexpr const char* INSTALL_ADVICE =
    "Install it - winget install --id Cloudflare.cloudflared on Windows, brew install cloudflared on macOS, or a "
    "download from github.com/cloudflare/cloudflared/releases - then share again. Editor Settings > External Tools can "
    "name where it is.";
#if defined(_WIN32)
/// Where cloudflared's Windows installer, which winget runs too, puts it: under Program Files.
constexpr const char* CLOUDFLARED_FOLDER = "cloudflared";
constexpr const char* CLOUDFLARED_FILE = "cloudflared.exe";
constexpr std::array<const char*, 2> PROGRAM_FILES_VARIABLES = {"ProgramFiles(x86)", "ProgramFiles"};
#endif

/// The cloudflared to try, in order: the one Editor Settings names, else the one on PATH and, on Windows, the
/// installer's, which an editor started before the install finds although its PATH is older.
auto get_cloudflared_candidates(const fs::path& setting) -> std::vector<fs::path>
{
    if(!setting.empty())
    {
        return {setting};
    }
    std::vector<fs::path> candidates{fs::path(DEFAULT_CLOUDFLARED)};
#if defined(_WIN32)
    for(const char* variable : PROGRAM_FILES_VARIABLES)
    {
        if(const char* folder = std::getenv(variable); folder != nullptr)
        {
            candidates.push_back(fs::path(folder) / CLOUDFLARED_FOLDER / CLOUDFLARED_FILE);
        }
    }
#endif
    return candidates;
}

/// The page a public link shares: the profile's served path and the graphics API the page starts the game with
/// (get_web_run_graphics; empty: the page chooses).
struct shared_page
{
    std::string site_path;
    std::string graphics;
};

/// One cloudflared at a time, and what its output said about the tunnel.
class public_link
{
public:
    auto start(const std::vector<fs::path>& programs,
               const std::string& local_origin,
               const shared_page& page,
               const std::string& profile) -> bool
    {
        stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = {public_link_state::starting, profile, {}, {}};
            page_ = page;
            error_lines_.clear();
        }
        const auto on_line = [this](const std::string& line)
        {
            read_line(line);
        };
        for(const auto& program : programs)
        {
            if(process_.start(make_web_tunnel_arguments(program, local_origin), on_line))
            {
                APPLOG_INFO("Public link: {} opens a tunnel to {}.", fs::to_utf8_string(program), local_origin);
                return true;
            }
        }
        fail(profile, std::string("cloudflared did not start. ") + INSTALL_ADVICE);
        return false;
    }

    /// Shows why sharing @p profile did not start.
    void fail(const std::string& profile, const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {public_link_state::failed, profile, {}, error};
    }

    void stop()
    {
        process_.stop();
        get_web_preview_server().set_tunnel_origin({});
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
    }

    auto get_status() -> public_link_status
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool is_open = status_.state == public_link_state::starting || status_.state == public_link_state::running;
        if(is_open && !process_.is_running())
        {
            status_.state = public_link_state::failed;
            status_.error = describe_stop();
            get_web_preview_server().set_tunnel_origin({});
        }
        return status_;
    }

private:
    /// On the process's reader thread: the tunnel's address, its connection and its errors.
    void read_line(const std::string& line)
    {
        APPLOG_TRACE("[cloudflared] {}", line);
        std::lock_guard<std::mutex> lock(mutex_);
        if(status_.page_address.empty())
        {
            if(const auto origin = find_web_tunnel_origin(line); !origin.empty())
            {
                status_.page_address = make_web_page_address(origin, page_.site_path, {}, page_.graphics);
                get_web_preview_server().set_tunnel_origin(origin);
            }
        }
        const bool is_connected = line.find(TUNNEL_CONNECTED) != std::string::npos;
        if(is_connected && status_.state == public_link_state::starting && !status_.page_address.empty())
        {
            status_.state = public_link_state::running;
            APPLOG_INFO("Public link: {} plays {} until sharing stops or the editor closes.", status_.page_address, status_.profile);
        }
        if(line.find(ERROR_LEVEL) != std::string::npos)
        {
            error_lines_.push_back(line);
            if(error_lines_.size() > QUOTED_ERROR_LINES)
            {
                error_lines_.pop_front();
            }
        }
    }

    auto describe_stop() const -> std::string
    {
        if(error_lines_.empty())
        {
            return "cloudflared stopped before the tunnel connected.";
        }
        std::string message = "cloudflared stopped. Its last errors:";
        for(const auto& line : error_lines_)
        {
            message += "\n" + line;
        }
        return message;
    }

    subprocess::stream process_;
    std::mutex mutex_;
    public_link_status status_;
    /// The shared profile's page, which its public address names.
    shared_page page_;
    std::deque<std::string> error_lines_;
};

auto get_link() -> public_link&
{
    static public_link link;
    return link;
}
} // namespace

auto get_public_link_problem(const deploy_settings& params) -> std::string
{
    if(params.deploy_target != deploy_platform::web)
    {
        return "Public links are for web profiles.";
    }
    const auto folder = get_output_location(params);
    fs::error_code ec;
    if(!fs::exists(folder / PAGE_FILE, ec))
    {
        return fmt::format("Deploy {} first: {} holds no web build of it.", params.profile, folder.generic_string());
    }
    return {};
}

auto start_public_link(rtti::context& ctx, const deploy_settings& params) -> std::string
{
    if(auto problem = get_public_link_problem(params); !problem.empty())
    {
        return problem;
    }
    const auto site_path = get_project_site_path(params.profile);
    const auto origin = get_web_preview_server().serve(get_output_location(params), site_path);
    if(origin.empty())
    {
        get_link().stop();
        get_link().fail(params.profile, "No port on this computer could be opened to serve the build.");
        return get_link().get_status().error;
    }
    const auto& setting = ctx.get_cached<project_manager>().get_editor_settings().external_tools.cloudflared_executable;
    const shared_page page{site_path, get_web_run_graphics(params.web)};
    if(!get_link().start(get_cloudflared_candidates(setting), origin, page, params.profile))
    {
        return get_link().get_status().error;
    }
    return {};
}

void stop_public_link()
{
    get_link().stop();
}

auto get_public_link_status() -> public_link_status
{
    return get_link().get_status();
}

} // namespace unravel::deployment
