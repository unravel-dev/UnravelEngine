#include "permission_requests.h"

#include <ospp/permission.h>

#include <mutex>
#include <unordered_map>

namespace unravel
{
namespace
{
constexpr const char* ANDROID_PERMISSION_PREFIX = "android.permission.";
} // namespace

auto get_android_permission_name(const std::string& name) -> std::string
{
    return name.find('.') == std::string::npos ? ANDROID_PERMISSION_PREFIX + name : name;
}

struct permission_requests::state_table
{
    /// Answers `id`, unless the request was forgotten.
    void answer(std::uint32_t id, bool is_granted)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = states.find(id);
        if(found != states.end())
        {
            found->second = is_granted ? permission_state::granted : permission_state::denied;
        }
    }

    std::mutex mutex;
    std::unordered_map<std::uint32_t, permission_state> states;
    std::uint32_t last_id{};
};

permission_requests::permission_requests() : permission_requests(&os::permission::request)
{
}

permission_requests::permission_requests(asker ask) : ask_(std::move(ask)), table_(std::make_shared<state_table>())
{
}

auto permission_requests::request(const std::string& name) -> std::uint32_t
{
    std::uint32_t id = 0;
    {
        const std::lock_guard<std::mutex> lock(table_->mutex);
        id = ++table_->last_id;
        table_->states[id] = permission_state::pending;
    }
    // The answer may arrive inside the call, so the request is pending in the table before it.
    const bool is_asked = ask_(get_android_permission_name(name),
                               [table = table_, id](bool is_granted)
                               {
                                   table->answer(id, is_granted);
                               });
    if(!is_asked)
    {
        table_->answer(id, false);
    }
    return id;
}

auto permission_requests::get_state(std::uint32_t id) const -> permission_state
{
    const std::lock_guard<std::mutex> lock(table_->mutex);
    const auto found = table_->states.find(id);
    return found == table_->states.end() ? permission_state::denied : found->second;
}

void permission_requests::forget(std::uint32_t id)
{
    const std::lock_guard<std::mutex> lock(table_->mutex);
    table_->states.erase(id);
}

} // namespace unravel
