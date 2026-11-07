#pragma once

#include <context/context.hpp>
#include <deploy/deploy_settings.h>

#include <string>

/// A public HTTPS link to a web profile's deploy, through a Cloudflare quick tunnel to the editor's web server.
namespace unravel::deployment
{

/// Where a public link stands.
enum class public_link_state
{
    /// Nothing is shared.
    stopped,
    /// cloudflared runs, and its tunnel is not connected yet.
    starting,
    /// The tunnel is connected: the link works, once its new name reached the visitor's DNS (a few seconds).
    running,
    /// cloudflared did not start, or it stopped; the status says why.
    failed,
};

struct public_link_status
{
    public_link_state state{public_link_state::stopped};
    /// The profile whose deploy the link opens.
    std::string profile;
    /// The page's public address, once cloudflared announced the tunnel.
    std::string page_address;
    /// Why the link failed, in sentences.
    std::string error;
};

/// Why the profile's deploy cannot be shared now, as one sentence; empty when it can.
auto get_public_link_problem(const deploy_settings& params) -> std::string;

/**
 * @brief Shares the profile's last deploy at a public HTTPS address. The editor serves its folder, as Deploy and Run
 * does, and cloudflared (Editor Settings > External Tools, else the one on PATH or where its installer puts it) opens
 * a Cloudflare quick tunnel to that server: no account, and a new https://<name>.trycloudflare.com address each time.
 * The server lets that tunnel's requests in besides this computer's until stop_public_link or the editor closes.
 * One link at a time: sharing again replaces it. Main thread.
 * @return Why it did not start, in sentences, which the status shows too unless the profile cannot be shared
 * (get_public_link_problem); empty once cloudflared runs.
 */
auto start_public_link(rtti::context& ctx, const deploy_settings& params) -> std::string;

/// Ends the tunnel; the server serves this computer only again.
void stop_public_link();

/// What the link is doing; notices a cloudflared that stopped.
auto get_public_link_status() -> public_link_status;

} // namespace unravel::deployment
