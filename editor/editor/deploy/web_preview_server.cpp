// First: httplib's winsock2.h cannot follow the winsock.h that windows.h brings in, which the filesystem header of
// this file's own header does.
#include <httplib.h>

#include "web_preview_server.h"
#include "deploy_preflight.h"

#include <deploy/web.h>
#include <editor/system/local_http.h>
#include <logging/logging.h>
#include <string_utils/utils.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>

namespace unravel::deployment
{
namespace
{
constexpr const char* LOCAL_HOST = "127.0.0.1";
/// The port the server takes when it is free: a page keeps its origin, and the saves and caches the browser keeps
/// per origin, from one editor session to the next. Another port is taken when it is busy.
constexpr int PREFERRED_PORT = 8742;
constexpr const char* INDEX_FILE = "index.html";
constexpr const char* DEFAULT_CONTENT_TYPE = "application/octet-stream";
constexpr const char* QUALITY_PARAMETER = "q=";

struct content_type_entry
{
    const char* extension;
    const char* type;
};

/// What a static host sends the player's files as; the wasm type lets a browser compile while it downloads.
constexpr std::array<content_type_entry, 9> CONTENT_TYPES{{{".html", "text/html"},
                                                           {".js", "text/javascript"},
                                                           {".mjs", "text/javascript"},
                                                           {".json", "application/json"},
                                                           {".wasm", "application/wasm"},
                                                           {".css", "text/css"},
                                                           {".png", "image/png"},
                                                           {".ico", "image/x-icon"},
                                                           {".txt", "text/plain"}}};

struct compressed_variant
{
    const char* encoding;
    const char* extension;
};

/// The compressed files a web deploy writes beside the player's, the smaller first.
constexpr std::array<compressed_variant, 2> COMPRESSED_VARIANTS{{{"br", ".br"}, {"gzip", ".gz"}}};

auto get_content_type(const fs::path& file) -> const char*
{
    const auto extension = file.extension().generic_string();
    for(const auto& entry : CONTENT_TYPES)
    {
        if(extension == entry.extension)
        {
            return entry.type;
        }
    }
    return DEFAULT_CONTENT_TYPE;
}

auto is_same_token(const std::string& lhs, const std::string& rhs) -> bool
{
    return lhs.size() == rhs.size() && std::equal(lhs.begin(),
                                                  lhs.end(),
                                                  rhs.begin(),
                                                  [](unsigned char a, unsigned char b)
                                                  {
                                                      return std::tolower(a) == std::tolower(b);
                                                  });
}

/// The q value of one Accept-Encoding item ("br;q=0.5"): 1 when it states none.
auto get_quality(const std::string& item) -> double
{
    const auto parameter = item.find(QUALITY_PARAMETER, item.find(';'));
    if(item.find(';') == std::string::npos || parameter == std::string::npos)
    {
        return 1.0;
    }
    return std::strtod(item.c_str() + parameter + std::char_traits<char>::length(QUALITY_PARAMETER), nullptr);
}

/// Whether the request's Accept-Encoding takes @p encoding: named or "*", with a q above 0.
auto accepts_encoding(const httplib::Request& request, const std::string& encoding) -> bool
{
    const auto header = request.get_header_value("Accept-Encoding");
    std::size_t start = 0;
    while(start <= header.size())
    {
        const auto end = std::min(header.find(',', start), header.size());
        const auto item = string_utils::trim(header.substr(start, end - start));
        const auto name = string_utils::trim(item.substr(0, item.find(';')));
        if(is_same_token(name, encoding) || name == "*")
        {
            return get_quality(item) > 0.0;
        }
        start = end + 1;
    }
    return false;
}

/**
 * @brief The file @p relative (UTF-8, the request's path below its site) names in @p folder, its index for a folder;
 * empty when it does not stay in the folder. A path with a root name or root directory ("C:/Windows") would replace
 * the folder when joined, and ".." leaves it: both are refused, and the joined path is checked to lie in the folder.
 */
auto resolve_request(const fs::path& folder, const std::string& relative) -> fs::path
{
    const auto path = fs::from_utf8_string(relative).lexically_normal();
    if(path.has_root_name() || path.has_root_directory() || (!path.empty() && *path.begin() == ".."))
    {
        return {};
    }
    auto file = (folder / path).lexically_normal();
    const auto inside = file.lexically_relative(folder.lexically_normal());
    if(inside.empty() || *inside.begin() == "..")
    {
        return {};
    }
    fs::error_code ec;
    if(fs::is_directory(file, ec))
    {
        file /= INDEX_FILE;
    }
    return file;
}

auto is_file(const fs::path& path) -> bool
{
    fs::error_code ec;
    return fs::is_regular_file(path, ec);
}

/// Whether @p compressed holds what @p file holds: it was written no earlier than the file, or the file is not there.
/// A file changed after its copy was made (a page edited in the site) is sent as it is, not as its old copy.
auto is_current_copy(const fs::path& compressed, const fs::path& file) -> bool
{
    fs::error_code ec;
    const auto copy_time = fs::last_write_time(compressed, ec);
    if(ec)
    {
        return false;
    }
    const auto file_time = fs::last_write_time(file, ec);
    return ec || copy_time >= file_time;
}

/// Sends @p file as it is, or its current compressed copy the browser accepts; the server streams it from the disk.
void serve_file(const fs::path& file, const httplib::Request& request, httplib::Response& response)
{
    response.set_header("Cache-Control", "no-cache");
    response.set_header("Vary", "Accept-Encoding");
    for(const auto& variant : COMPRESSED_VARIANTS)
    {
        auto compressed = file;
        compressed += variant.extension;
        if(accepts_encoding(request, variant.encoding) && is_file(compressed) && is_current_copy(compressed, file))
        {
            response.set_header("Content-Encoding", variant.encoding);
            response.set_file_content(fs::to_utf8_string(compressed), get_content_type(file));
            return;
        }
    }
    if(!is_file(file))
    {
        response.status = httplib::StatusCode::NotFound_404;
        return;
    }
    response.set_file_content(fs::to_utf8_string(file), get_content_type(file));
}
} // namespace

web_preview_server::web_preview_server() = default;

web_preview_server::~web_preview_server()
{
    stop();
}

auto web_preview_server::serve(const fs::path& folder, const std::string& site_path) -> std::string
{
    {
        std::lock_guard<std::mutex> lock(sites_mutex_);
        sites_[site_path] = folder;
    }
    if(!server_ && !start())
    {
        return {};
    }
    return "http://" + std::string(LOCAL_HOST) + ":" + std::to_string(port_);
}

auto web_preview_server::start() -> bool
{
    for(const bool is_preferred_port : {true, false})
    {
        auto server = std::make_unique<httplib::Server>();
        server->Get(R"(/.*)",
                    [this](const httplib::Request& request, httplib::Response& response)
                    {
                        handle(request, response);
                    });
        const int port = !is_preferred_port                                 ? server->bind_to_any_port(LOCAL_HOST)
                         : server->bind_to_port(LOCAL_HOST, PREFERRED_PORT) ? PREFERRED_PORT
                                                                            : -1;
        if(port < 0)
        {
            continue;
        }
        server_ = std::move(server);
        port_ = port;
        thread_ = std::thread(
            [server = server_.get()]()
            {
                server->listen_after_bind();
            });
        // Until it listens, a stop would not reach it; a server that cannot listen leaves nothing behind.
        server_->wait_until_ready();
        if(server_->is_running())
        {
            return true;
        }
        stop();
        return false;
    }
    return false;
}

void web_preview_server::set_tunnel_origin(const std::string& origin)
{
    std::lock_guard<std::mutex> lock(sites_mutex_);
    tunnel_origin_ = origin;
}

auto web_preview_server::is_tunnel_request(const httplib::Request& request) -> bool
{
    std::lock_guard<std::mutex> lock(sites_mutex_);
    return is_web_tunnel_request(request.get_header_value("Host"), request.get_header_value("Origin"), tunnel_origin_);
}

void web_preview_server::handle(const httplib::Request& request, httplib::Response& response)
{
    std::string refusal;
    if(!is_tunnel_request(request) && !is_loopback_request(request, refusal))
    {
        APPLOG_WARNING("Web deploy server: {}.", refusal);
        response.status = httplib::StatusCode::Forbidden_403;
        return;
    }
    std::string relative;
    const auto folder = find_site(request.path, relative);
    if(folder.empty())
    {
        response.status = httplib::StatusCode::NotFound_404;
        return;
    }
    const auto file = resolve_request(folder, relative);
    if(file.empty())
    {
        response.status = httplib::StatusCode::Forbidden_403;
        return;
    }
    serve_file(file, request, response);
}

auto web_preview_server::find_site(const std::string& request_path, std::string& relative) -> fs::path
{
    std::lock_guard<std::mutex> lock(sites_mutex_);
    for(const auto& [site_path, folder] : sites_)
    {
        if(request_path.compare(0, site_path.size(), site_path) == 0)
        {
            relative = request_path.substr(site_path.size());
            return folder;
        }
    }
    return {};
}

void web_preview_server::stop()
{
    if(server_)
    {
        server_->stop();
    }
    if(thread_.joinable())
    {
        thread_.join();
    }
    server_.reset();
    port_ = -1;
}

auto get_web_preview_server() -> web_preview_server&
{
    static web_preview_server server;
    return server;
}

auto get_project_site_path(const std::string& profile) -> std::string
{
    return make_web_site_path(fs::to_utf8_string(get_project_root().filename()), profile);
}

} // namespace unravel::deployment
