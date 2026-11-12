#pragma once
#include <engine/engine_export.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace unravel
{

/// How a request for a permission stands.
enum class permission_state : std::uint8_t
{
    /// The player has not answered yet.
    pending,
    granted,
    /// Refused by the player, or by the system: a permission the build does not declare is never granted.
    denied,
};

/// A permission's name as Android's manifest writes it: as given when it holds a '.', with "android.permission."
/// in front otherwise ("CAMERA" -> "android.permission.CAMERA"). Deploys and requests name permissions the same way.
ENGINE_EXPORT auto get_android_permission_name(const std::string& name) -> std::string;

/**
 * @brief The game's requests for permissions the player grants while it runs: on Android the camera, the
 * microphone, the location, notifications. The system answers a request on a thread of its own, possibly many
 * frames later, so a request has an id its state is read by.
 */
class ENGINE_EXPORT permission_requests
{
public:
    /// Receives the answer to a request, on any thread.
    using answer_callback = std::function<void(bool is_granted)>;
    /// Asks the platform for the permission `name`; false when the request could not be made, and then the
    /// callback is not called.
    using asker = std::function<bool(const std::string& name, answer_callback on_answer)>;

    /// Requests go to the operating system.
    permission_requests();
    /// Requests go to `ask`.
    explicit permission_requests(asker ask);

    /// Asks for the permission `name` (get_android_permission_name) and returns the request's id, never 0. A
    /// request that could not be made is denied at once.
    auto request(const std::string& name) -> std::uint32_t;
    /// The state of request `id`; denied for an id no request has.
    auto get_state(std::uint32_t id) const -> permission_state;
    /// Forgets request `id`, whose answer was read or is not wanted any more; an answer that arrives later is
    /// dropped.
    void forget(std::uint32_t id);

private:
    struct state_table;

    asker ask_;
    /// Shared with the answers on their way: one may arrive after these requests are gone.
    std::shared_ptr<state_table> table_;
};

} // namespace unravel
