#include "seq.h"
#include "detail/seq_internal.h"
#include "seq_inspector.h"

namespace seq
{

auto start(seq_action action, const seq_scope_policy& scope_policy, hpp::source_location location) -> seq_id_t
{
    inspector::add_location(action, location);
    return detail::get_manager().start(std::move(action), scope_policy);
}

void stop(seq_id_t id)
{
    detail::get_manager().stop(id);
}

void pause(const seq_id_t id)
{
    detail::get_manager().pause(id);
}

void resume(const seq_id_t id)
{
    detail::get_manager().resume(id);
}

void stop_when_finished(seq_id_t id)
{
    detail::get_manager().stop_when_finished(id);
}

void stop_and_finish(seq_id_t id, duration_t finish_after)
{
    detail::get_manager().stop_and_finish(id, finish_after);
}

auto is_stopping(seq_id_t id) -> bool
{
    return detail::get_manager().is_stopping(id);
}

auto is_running(seq_id_t id) -> bool
{
    return detail::get_manager().is_running(id);
}

auto is_paused(seq_id_t id) -> bool
{
    return detail::get_manager().is_paused(id);
}

auto is_finished(seq_id_t id) -> bool
{
    return detail::get_manager().is_finished(id);
}

auto has_action_with_scope(const std::string& scope_id) -> bool
{
    return detail::get_manager().has_action_with_scope(scope_id);
}

void set_speed_multiplier(seq_id_t id, float speed_multiplier)
{
    detail::get_manager().set_speed_multiplier(id, speed_multiplier);
}

auto get_speed_multiplier(seq_id_t id) -> float
{
    return detail::get_manager().get_speed_multiplier(id);
}

auto get_elapsed(seq_id_t id) -> duration_t
{
    return detail::get_manager().get_elapsed(id);
}

void set_elapsed(seq_id_t id, duration_t duration)
{
    detail::get_manager().set_elapsed(id, duration);
}

auto get_duration(seq_id_t id) -> duration_t
{
    return detail::get_manager().get_duration(id);
}

auto get_overflow(seq_id_t id) -> duration_t
{
    return detail::get_manager().get_overflow(id);
}

void update(seq_id_t id, duration_t delta)
{
    detail::get_manager().update(id, delta);
}

auto get_percent(seq_id_t id) -> float
{
    auto dur = get_duration(id);
    if(dur == duration_t::zero())
    {
        return 0.0f;
    }
    float elapsed = float(get_elapsed(id).count());
    float total_dur = float(dur.count());
    return elapsed / total_dur;
}

void update(duration_t delta)
{
    detail::get_manager().update(delta);
}

void update(duraiton_secs_t delta)
{
    update(std::chrono::duration_cast<seq::duration_t>(delta));
}


void shutdown()
{
    detail::get_manager() = {};
}

namespace scope
{
void push(const std::string& scope)
{
    detail::get_manager().push_scope(scope);
}

void pop()
{
    detail::get_manager().pop_scope();
}

void close(const std::string& scope)
{
    detail::get_manager().close_scope(scope);
}

void clear()
{
    detail::get_manager().clear_scopes();
}

auto get_current() -> const std::string&
{
    return detail::get_manager().get_current_scope();
}

void stop_all(const std::string& scope)
{
    detail::get_manager().stop_all(scope);
}

void pause_all(const std::string& scope)
{
    detail::get_manager().pause_all(scope);
}

void resume_all(const std::string& scope)
{
    detail::get_manager().resume_all(scope);
}

void pause_all(const std::string& scope, const std::string& key)
{
    detail::get_manager().pause_all(scope, key);
}

void resume_all(const std::string& scope, const std::string& key)
{
    detail::get_manager().resume_all(scope, key);
}

void stop_and_finish_all(const std::string& scope)
{
    detail::get_manager().stop_and_finish_all(scope);
}

void stop_when_finished_all(const std::string& scope)
{
    detail::get_manager().stop_when_finished_all(scope);
}

} // namespace scope

namespace manager
{
void push(seq_manager& mgr)
{
    detail::push(mgr);
}
void pop()
{
    detail::pop();
}
} // namespace manager
} // namespace seq
