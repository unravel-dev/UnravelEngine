#include "seq_core.h"
#include "seq_inspector.h"

namespace seq
{

auto sequence_impl(const std::vector<seq_action>& actions, const sentinel_t& sentinel, bool precise) -> seq_action
{
    if(actions.empty())
    {
        return {};
    }

    duration_t duration = 0ms;
    for(auto& action : actions)
    {
        duration += seq_private::get_duration(action);
    }

    auto updater = [actions = actions,
                    sentinel = sentinel,
                    current_action_idx = size_t(0),
                    prev_overflow = duration_t::zero(),
                    prev_elapsed = duration_t::zero(),
                    start_required = true,
                    finished = false,
                    precise](duration_t delta, seq_action& self) mutable
    {
        if(finished)
        {
            return state_t::finished;
        }

        if(sentinel.expired())
        {
            finished = true;
            return state_t::finished;
        }

        if(start_required)
        {
            seq_private::start(actions.at(current_action_idx));
            prev_elapsed = duration_t::zero();
            start_required = false;
        }

        if(seq_private::is_stop_when_finished_requested(self))
        {
            for(auto& action : actions)
            {
                seq_private::stop_when_finished(action);
            }
        }

        auto& current_action = actions.at(current_action_idx);

        if(precise)
        {
            delta += prev_overflow;
        }

        auto state = seq_private::update(current_action, delta);

        auto elapsed = seq_private::get_elapsed(current_action);
        auto elapsed_diff = elapsed - prev_elapsed;
        prev_elapsed = elapsed;

        seq_private::update_elapsed(self, elapsed_diff);
        inspector::update_action_status(self, current_action_idx);
        self.on_step.emit();

        prev_overflow = duration_t::zero();
        if(state == state_t::finished)
        {
            prev_overflow = seq_private::get_overflow(current_action);
            current_action_idx++;
            if(current_action_idx == actions.size())
            {
                seq_private::update_elapsed(self, prev_overflow);

                finished = true;
                return state_t::finished;
            }
            start_required = true;
        }
        return state_t::running;
    };

    auto creator = [updater = std::move(updater)]()
    {
        return updater;
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_sequence_info(action, actions);
    return action;
}

auto sequence(const std::vector<seq_action>& actions, const sentinel_t& sentinel) -> seq_action
{
    return sequence_impl(actions, sentinel, false);
}

auto sequence_precise(const std::vector<seq_action>& actions, const sentinel_t& sentinel) -> seq_action
{
    return sequence_impl(actions, sentinel, true);
}

auto together(const std::vector<seq_action>& actions, const sentinel_t& sentinel) -> seq_action
{
    if(actions.empty())
    {
        return {};
    }

    duration_t duration = 0ms;
    for(const auto& action : actions)
    {
        duration = std::max(seq_private::get_duration(action), duration);
    }

    auto updater =
        [actions = actions, sentinel = sentinel, start_required = true, finished = false](duration_t delta,
                                                                                          seq_action& self) mutable
    {
        if(finished)
        {
            return state_t::finished;
        }

        if(sentinel.expired())
        {
            finished = true;
            return state_t::finished;
        }

        if(start_required)
        {
            for(auto& action : actions)
            {
                seq_private::start(action);
            }
            start_required = false;
        }

        if(seq_private::is_stop_when_finished_requested(self))
        {
            for(auto& action : actions)
            {
                seq_private::stop_when_finished(action);
            }
        }

        finished = true;
        for(auto& action : actions)
        {
            const auto state = seq_private::update(action, delta);
            finished &= (state == state_t::finished);
        }

        seq_private::update_elapsed(self, delta);
        inspector::update_action_status(self);
        self.on_step.emit();

        if(finished)
        {
            return state_t::finished;
        }
        return state_t::running;
    };

    auto creator = [updater = std::move(updater)]()
    {
        return updater;
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_together_info(action, actions);
    return action;
}

auto delay(const duration_t& duration, const sentinel_t& sentinel) -> seq_action
{
    auto updater = [sentinel, finished = false](duration_t delta, seq_action& self) mutable
    {
        if(finished)
        {
            return state_t::finished;
        }

        if(sentinel.expired())
        {
            finished = true;
            return state_t::finished;
        }

        seq_private::update_elapsed(self, delta);
        inspector::update_action_status(self);

        self.on_step.emit();

        if(seq_private::get_elapsed(self) == seq_private::get_duration(self))
        {
            finished = true;
            return state_t::finished;
        }

        return state_t::running;
    };

    auto creator = [updater = std::move(updater)]()
    {
        return updater;
    };

    auto action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_delay_info(action);
    return action;
}

auto repeat_impl(const seq_action& action,
                 size_t times,
                 bool precise,
                 const sentinel_t& sentinel = hpp::eternal_sentinel()) -> seq_action
{
    auto updater = [action = action,
                    times = times,
                    sentinel = sentinel,
                    elapsed = size_t(0),
                    finished = false,
                    prev_overflow = duration_t::zero(),
                    precise](duration_t delta, seq_action& self) mutable
    {
        if(finished)
        {
            return state_t::finished;
        }

        if(sentinel.expired())
        {
            finished = true;
            return state_t::finished;
        }

        if(elapsed > 0)
        {
            if(seq_private::is_stop_when_finished_requested(self))
            {
                seq_private::stop_when_finished(action);
            }
        }

        if(precise)
        {
            delta += prev_overflow;
        }

        auto state = seq_private::update(action, delta);
        if(times > 0)
        {
            inspector::update_action_status(self, elapsed);
        }

        self.on_step.emit();

        prev_overflow = duration_t::zero();
        if(state == state_t::finished)
        {
            prev_overflow = seq_private::get_overflow(action);
            if(elapsed > 0)
            {
                if(seq_private::is_stop_when_finished_requested(self))
                {
                    finished = true;
                    return state_t::finished;
                }
            }

            if(times == 0)
            {
                seq_private::start(action);
                elapsed++;
            }
            else
            {
                if(elapsed >= times)
                {
                    finished = true;
                    return state_t::finished;
                }

                seq_private::start(action);

                elapsed++;
            }
        }
        return state_t::running;
    };

    auto creator = [updater = std::move(updater)]() mutable
    {
        return updater;
    };

    duration_t duration = 0ms;
    if(times > 0)
    {
        duration = times * seq_private::get_duration(action);
    }

    auto new_action = seq_action(std::move(creator), duration, sentinel);
    inspector::add_repeat_info(new_action, action, times);
    return new_action;
}

auto repeat(const seq_action& action, size_t times) -> seq_action
{
    return repeat_impl(action, times, false);
}

auto repeat(const seq_action& action, const sentinel_t& sentinel, size_t times) -> seq_action
{
    return repeat_impl(action, times, false, sentinel);
}

auto repeat_precise(const seq_action& action, size_t times) -> seq_action
{
    return repeat_impl(action, times, true);
}

auto repeat_precise(const seq_action& action, const sentinel_t& sentinel, size_t times) -> seq_action
{
    return repeat_impl(action, times, true, sentinel);
}

} // namespace seq
