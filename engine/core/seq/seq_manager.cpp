#include <stdexcept>

#include "seq_manager.h"
#include "seq_private.h"

namespace seq
{

auto seq_manager::start(seq_action action, const seq_scope_policy& scope_policy) -> seq_id_t
{
    const auto id = action.get_id();

    if(is_running(action))
    {
        return id;
    }

    if(is_paused(action))
    {
        resume(action);
        return id;
    }

    auto fill_info = [this, &scope_policy](seq_info& info, seq_action&& action)
    {
        info.action = std::move(action);
        if(scope_policy.scope.empty())
        {
            info.scopes.insert(info.scopes.end(), scopes_.begin(), scopes_.end());
        }
        else
        {
            if(scope_policy.policy == seq_scope_policy::policy_t::independent)
            {
                info.scopes.emplace_back(scope_policy.scope);
            }
            else if(scope_policy.policy == seq_scope_policy::policy_t::stacked)
            {
                info.scopes.insert(info.scopes.end(), scopes_.begin(), scopes_.end());
                info.scopes.emplace_back(scope_policy.scope);
            }
        }
    };

    auto iter = actions_.find(id);
    if(iter == actions_.end())
    {
        auto& info = actions_[id];
        fill_info(info, std::move(action));

        start_action(info);
    }
    else
    {
        // Occurs when start yourself from your own callback or start yourself from previous action process
        auto& info = pending_actions_[id];
        fill_info(info, std::move(action));
    }

    return id;
}

void seq_manager::stop(seq_id_t id)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        seq_private::stop(iter->second.action);
    }

    auto pending_iter = pending_actions_.find(id);
    if(pending_iter != std::end(pending_actions_))
    {
        pending_actions_.erase(pending_iter);
    }
}

void seq_manager::stop_all(const std::string& scope)
{
    for(auto& info : actions_)
    {
        if(std::find(info.second.scopes.begin(), info.second.scopes.end(), scope) != info.second.scopes.end())
        {
            seq_private::stop(info.second.action);
        }
    }
}

void seq_manager::pause(const seq_id_t id)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        seq_private::pause(iter->second.action);
    }
}

void seq_manager::pause_all(const std::string& scope, const std::string& key)
{
    paused_scopes_.insert(std::make_pair(scope, key));

    for(auto& info : actions_)
    {
        if(std::find(info.second.scopes.begin(), info.second.scopes.end(), scope) != info.second.scopes.end())
        {
            seq_private::pause(info.second.action, key);
        }
    }
}

void seq_manager::resume(const seq_id_t action)
{
    auto iter = actions_.find(action);
    if(iter != std::end(actions_))
    {
        seq_private::resume(iter->second.action);
    }
}

void seq_manager::resume_all(const std::string& scope, const std::string& key)
{
    paused_scopes_.erase(std::make_pair(scope, key));

    for(auto& info : actions_)
    {
        if(std::find(info.second.scopes.begin(), info.second.scopes.end(), scope) != info.second.scopes.end())
        {
            seq_private::resume(info.second.action, key);
        }
    }
}

void seq_manager::stop_when_finished(seq_id_t id)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        seq_private::stop_when_finished(iter->second.action);
    }
}

void seq_manager::stop_when_finished_all(const std::string& scope)
{
    for(auto& info : actions_)
    {
        if(std::find(info.second.scopes.begin(), info.second.scopes.end(), scope) != info.second.scopes.end())
        {
            seq_private::stop_when_finished(info.second.action);
        }
    }
}

void seq_manager::stop_and_finish(seq_id_t id, duration_t /*finish_after*/)
{
    auto iter = actions_.find(id);
    if(iter == std::end(actions_))
    {
        return;
    }
    auto& action = iter->second.action;

    if(seq_private::get_state(action) == state_t::finished)
    {
        return;
    }
    auto& depth = iter->second.depth;

    if(depth > 0)
    {
        stop(action);
        throw std::runtime_error("Cannot call stop_and_finish from callbacks.");
    }

    seq_private::stop_and_finished(action);
    seq_private::stop_when_finished(action);
    while(true)
    {
        const bool force = true;
        seq_private::resume(action, force);

        ++depth;
        bool finished = state_t::finished == seq_private::update(action, 99h);
        --depth;

        if(finished)
        {
            break;
        }
    }
}

void seq_manager::stop_and_finish_all(const std::string& scope)
{
    for(auto& info : actions_)
    {
        if(std::find(info.second.scopes.begin(), info.second.scopes.end(), scope) != info.second.scopes.end())
        {
            stop_and_finish(info.second.action);
        }
    }
}

auto seq_manager::is_stopping(seq_id_t id) const -> bool
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::is_stop_when_finished_requested(iter->second.action);
    }
    return false;
}

auto seq_manager::is_running(seq_id_t id) const -> bool
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::is_running(iter->second.action);
    }
    return false;
}

auto seq_manager::is_paused(seq_id_t id) const -> bool
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::is_paused(iter->second.action);
    }
    return false;
}

auto seq_manager::is_finished(seq_id_t id) const -> bool
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::is_finished(iter->second.action);
    }
    return true;
}

void seq_manager::set_speed_multiplier(seq_id_t id, float speed_multiplier)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        seq_private::set_speed_multiplier(iter->second.action, speed_multiplier);
    }
}

float seq_manager::get_speed_multiplier(seq_id_t id)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::get_speed_multiplier(iter->second.action);
    }
    return 1.0f;
}

auto seq_manager::get_elapsed(seq_id_t id) const -> duration_t
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::get_elapsed(iter->second.action);
    }
    return {};
}

auto seq_manager::get_duration(seq_id_t id) const -> duration_t
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::get_duration(iter->second.action);
    }
    return {};
}

auto seq_manager::get_overflow(seq_id_t id) const -> duration_t
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        return seq_private::get_overflow(iter->second.action);
    }
    return {};
}

void seq_manager::update(seq_id_t id, duration_t delta)
{
    auto iter = actions_.find(id);
    if(iter != std::end(actions_))
    {
        seq_private::update(iter->second.action, delta);
    }
}

void seq_manager::set_elapsed(seq_id_t id, duration_t elapsed)
{
    auto iter = actions_.find(id);
    if(iter == std::end(actions_))
    {
        return;
    }

    seq_private::set_elapsed(iter->second.action, elapsed);
}

void seq_manager::update(duration_t delta)
{
    if(delta < duration_t::zero())
    {
        delta = duration_t::zero();
    }

    std::vector<seq_id_t> actions_to_start;
    actions_to_start.reserve(pending_actions_.size());

    for(auto& kvp : pending_actions_)
    {
        actions_to_start.emplace_back(kvp.first);

        actions_[kvp.first] = std::move(kvp.second);
    }
    pending_actions_.clear();

    for(auto id : actions_to_start)
    {
        start_action(actions_.at(id));
    }

    for(auto id : get_ids())
    {
        auto& action = actions_[id];

        if(action.depth > 0)
        {
            continue;
        }

        action.depth++;

        auto state = seq_private::update(action.action, delta);

        action.depth--;

        if(state == state_t::finished)
        {
            actions_.erase(action.action.get_id());
        }
    }
}

void seq_manager::push_scope(const std::string& scope)
{
    if(scope.empty())
    {
        return;
    }

    auto iter = std::find(scopes_.begin(), scopes_.end(), scope);
    if(iter != scopes_.end())
    {
        throw std::logic_error("push_scope that is already pushed.");
    }

    scopes_.emplace_back(scope);
    current_scope_idx_ = static_cast<int32_t>(scopes_.size()) - 1;
}

void seq_manager::pop_scope()
{
    if(current_scope_idx_ < int32_t(scopes_.size()))
    {
        close_scope(scopes_.at(current_scope_idx_));
    }
}

void seq_manager::close_scope(const std::string& scope)
{
    auto iter = std::find(scopes_.begin(), scopes_.end(), scope);
    scopes_.erase(iter, scopes_.end());

    current_scope_idx_ = static_cast<int32_t>(scopes_.size()) - 1;
}

void seq_manager::clear_scopes()
{
    scopes_.clear();
    current_scope_idx_ = (-1);
}

void seq_manager::start_action(seq_info& info)
{
    auto& action = info.action;

    bool paused_by_scope = false;
    // First force pause the action before it was started
    for(const auto& scope : info.scopes)
    {
        auto it = std::find_if(paused_scopes_.begin(),
                               paused_scopes_.end(),
                               [&](const auto& sc)
                               {
                                   return sc.first == scope;
                               });
        if(it != paused_scopes_.end())
        {
            const auto& scope_pause_key = it->second;
            seq_private::pause_forced(action, scope_pause_key);
            paused_by_scope = true;
            break;
        }
    }

    seq_private::start(action);

    if(paused_by_scope)
    {
        seq_private::pause_forced(action);
    }
}

auto seq_manager::get_current_scope() const -> const std::string&
{
    static const std::string default_scope{};
    if(scopes_.empty())
    {
        return default_scope;
    }
    return scopes_.at(current_scope_idx_);
}

auto seq_manager::get_scopes() const -> const std::vector<std::string>&
{
    return scopes_;
}

auto seq_manager::get_actions() const -> const action_collection_t&
{
    return actions_;
}

auto seq_manager::get_ids() const -> std::vector<seq_id_t>
{
    std::vector<seq_id_t> result;
    result.reserve(actions_.size());
    for(const auto& kvp : actions_)
    {
        result.emplace_back(kvp.first);
    }
    return result;
}

auto seq_manager::has_action_with_scope(const std::string& scope_id) -> bool
{
    for(const auto& kvp : actions_)
    {
        if(std::find(kvp.second.scopes.begin(), kvp.second.scopes.end(), scope_id) != kvp.second.scopes.end())
        {
            return true;
        }
    };
    return false;
}

} // namespace seq
