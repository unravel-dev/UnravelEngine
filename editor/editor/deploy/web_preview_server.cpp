#include "web_preview_server.h"

#include <httplib.h>

#include <array>
#include <fstream>
#include <iterator>

namespace unravel::deployment
{
namespace
{
constexpr const char* LOCAL_HOST = "127.0.0.1";
constexpr const char* INDEX_FILE = "index.html";
constexpr const char* DEFAULT_CONTENT_TYPE = "application/octet-stream";

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

auto read_file(const fs::path& file, std::string& bytes) -> bool
{
    std::ifstream input(file, std::ios::binary);
    if(!input)
    {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

/// The file a request's path names in @p folder, its index for a folder; empty when the path leaves the folder.
auto resolve_request(const fs::path& folder, const std::string& request_path) -> fs::path
{
    const auto relative = fs::path(request_path).relative_path().lexically_normal();
    if(!relative.empty() && *relative.begin() == "..")
    {
        return {};
    }
    auto file = folder / relative;
    fs::error_code ec;
    if(relative.empty() || fs::is_directory(file, ec))
    {
        file /= INDEX_FILE;
    }
    return file;
}

auto accepts_encoding(const httplib::Request& request, const char* encoding) -> bool
{
    return request.get_header_value("Accept-Encoding").find(encoding) != std::string::npos;
}

void serve_file(const fs::path& file, const httplib::Request& request, httplib::Response& response)
{
    response.set_header("Cache-Control", "no-cache");
    std::string bytes;
    for(const auto& variant : COMPRESSED_VARIANTS)
    {
        auto compressed = file;
        compressed += variant.extension;
        if(accepts_encoding(request, variant.encoding) && read_file(compressed, bytes))
        {
            response.set_header("Content-Encoding", variant.encoding);
            response.set_content(std::move(bytes), get_content_type(file));
            return;
        }
    }
    if(!read_file(file, bytes))
    {
        response.status = httplib::StatusCode::NotFound_404;
        return;
    }
    response.set_content(std::move(bytes), get_content_type(file));
}
} // namespace

web_preview_server::web_preview_server() = default;

web_preview_server::~web_preview_server()
{
    stop();
}

auto web_preview_server::serve(const fs::path& folder) -> std::string
{
    {
        std::lock_guard<std::mutex> lock(folder_mutex_);
        folder_ = folder;
    }
    if(!server_)
    {
        server_ = std::make_unique<httplib::Server>();
        server_->Get(R"(/.*)",
                     [this](const httplib::Request& request, httplib::Response& response)
                     {
                         const auto file = resolve_request(get_folder(), request.path);
                         if(file.empty())
                         {
                             response.status = httplib::StatusCode::Forbidden_403;
                             return;
                         }
                         serve_file(file, request, response);
                     });
        port_ = server_->bind_to_any_port(LOCAL_HOST);
        if(port_ < 0)
        {
            server_.reset();
            return {};
        }
        thread_ = std::thread(
            [server = server_.get()]()
            {
                server->listen_after_bind();
            });
    }
    return "http://" + std::string(LOCAL_HOST) + ":" + std::to_string(port_) + "/";
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

auto web_preview_server::get_folder() -> fs::path
{
    std::lock_guard<std::mutex> lock(folder_mutex_);
    return folder_;
}

auto get_web_preview_server() -> web_preview_server&
{
    static web_preview_server server;
    return server;
}

} // namespace unravel::deployment
