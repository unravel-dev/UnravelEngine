#pragma once

#include <filesystem/filesystem.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib
{
class Server;
}

namespace unravel::deployment
{
/**
 * @brief Serves a web deploy's folder on this computer, as a static web host does, for Deploy and Run: a browser
 * loads the game only from a web server. A file's .br or .gz beside it goes compressed to a browser that accepts that
 * encoding, nothing is kept in the browser's cache, and only this computer can connect.
 */
class web_preview_server
{
public:
    web_preview_server();
    ~web_preview_server();
    web_preview_server(const web_preview_server&) = delete;
    auto operator=(const web_preview_server&) -> web_preview_server& = delete;

    /// Serves @p folder from now on, starting the server the first time. Returns the page's address, or an empty
    /// string when no port on this computer could be opened.
    auto serve(const fs::path& folder) -> std::string;
    /// Stops serving; the next serve starts again.
    void stop();

private:
    auto get_folder() -> fs::path;

    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::mutex folder_mutex_;
    fs::path folder_;
    int port_{-1};
};

/// The editor's server for web deploys.
auto get_web_preview_server() -> web_preview_server&;

} // namespace unravel::deployment
