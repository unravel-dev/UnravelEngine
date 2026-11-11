#pragma once

#include <filesystem/filesystem.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib
{
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace unravel::deployment
{
/**
 * @brief Serves web deploys on this computer, as a static web host does, for Deploy and Run: a browser loads the game
 * only from a web server.
 *
 * Each deploy is served under a path of its own (make_web_site_path), beside the ones served before, so a page left
 * open keeps working and keeps its own caches in the browser. The server prefers one port, so the pages keep their
 * address - and with it their saves and caches - from one editor session to the next. A file's .br or .gz beside it
 * goes compressed to a browser that accepts that encoding, every file is checked again on each visit (the page keeps
 * the game's files in its own caches by content), and only this computer can connect - and, while a public link is
 * shared (web_public_link.h), that link's tunnel, which reaches the shared build only.
 */
class web_preview_server
{
public:
    web_preview_server();
    ~web_preview_server();
    web_preview_server(const web_preview_server&) = delete;
    auto operator=(const web_preview_server&) -> web_preview_server& = delete;

    /**
     * @brief Serves @p folder under @p site_path ("/<project>/<profile>/") from now on, starting the server the
     * first time.
     * @return The server's origin ("http://127.0.0.1:8742"), or an empty string when no port on this computer could
     * be opened.
     */
    auto serve(const fs::path& folder, const std::string& site_path) -> std::string;
    /// Stops serving every site; the next serve starts again.
    void stop();
    /// Lets requests through the quick tunnel at @p origin ("https://<name>.trycloudflare.com") in as well, besides
    /// this computer's, for the site at @p site_path only; an empty origin: this computer's only.
    void set_tunnel(const std::string& origin, const std::string& site_path);

private:
    auto start() -> bool;
    void handle(const httplib::Request& request, httplib::Response& response);
    auto is_tunnel_request(const httplib::Request& request) -> bool;
    /// Whether @p request_path lies in the site the tunnel shares.
    auto is_tunnel_site(const std::string& request_path) -> bool;
    /// The folder of the site @p request_path lies in, and the path below the site in @p relative; empty when none.
    auto find_site(const std::string& request_path, std::string& relative) -> fs::path;

    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::mutex sites_mutex_;
    /// The folder of each site path served.
    std::map<std::string, fs::path> sites_;
    /// The public link's tunnel, whose requests come in too; empty when none is shared. Guarded by sites_mutex_.
    std::string tunnel_origin_;
    /// The one site the tunnel's requests may read. Guarded by sites_mutex_.
    std::string tunnel_site_path_;
    int port_{-1};
};

/// The editor's server for web deploys.
auto get_web_preview_server() -> web_preview_server&;

/// The path the editor serves the open project's build of @p profile under (make_web_site_path).
auto get_project_site_path(const std::string& profile) -> std::string;

} // namespace unravel::deployment
