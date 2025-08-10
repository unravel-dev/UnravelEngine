#pragma once
#include "seq_action.h"
#include "seq_common.h"
#include "seq_private.h"
#include <hpp/source_location.hpp>

namespace seq
{
namespace inspector
{

template<typename T>
inline void update_begin_value(seq_action& action, const T& begin)
{
#ifdef SEQ_INSPECTOR_ENABLE
    if(action.info)
    {
        action.info->begin_value = to_str(begin);
    }
#endif
}

template<typename T>
inline void update_action_status(seq_action& action, const T& current)
{
#ifdef SEQ_INSPECTOR_ENABLE
    if(action.info)
    {
        action.info->current_value = to_str(current);
        action.info->elapsed = seq_private::get_elapsed(action);
        action.info->progress = float(action.info->elapsed.count()) / float(action.info->duration.count());
        action.info->speed_multiplier = seq_private::get_speed_multiplier(action);
        action.info->stop_when_finished = seq_private::is_stop_when_finished_requested(action);
    }
#endif
}

template<typename Object, typename T>
inline void add_info(seq_action& action,
                     const std::string& updater_type,
                     const Object& object,
                     const T& end_value,
                     const ease_t& ease_func)
{
#ifdef SEQ_INSPECTOR_ENABLE
    action.info = std::make_shared<seq_inspect_info>();
    action.info->id = action.get_id();
    action.info->duration = seq_private::get_duration(action);
    action.info->updater_type = updater_type;
    action.info->modified_type = type_to_str(object);
    action.info->end_value = to_str(end_value);
    action.info->ease_func = ease_func;
#endif
}

void update_action_status(seq_action& action);
void update_action_state(seq_action& action, state_t state);

void add_sequence_info(seq_action& action, const std::vector<seq_action>& actions);
void add_together_info(seq_action& action, const std::vector<seq_action>& actions);
void add_delay_info(seq_action& action);
void add_repeat_info(seq_action& repeat_action, const seq_action& action, size_t times = 0);
void add_location(seq_action& action, const hpp::source_location& location);

} // end of namespace inspector
} // namespace seq
