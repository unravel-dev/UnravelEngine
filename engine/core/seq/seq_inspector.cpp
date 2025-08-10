#include "seq_inspector.h"

namespace seq
{
namespace inspector
{
void add_sequence_info(seq_action& action, const std::vector<seq_action>& actions)
{
#ifdef SEQ_INSPECTOR_ENABLE
    seq_inspect_info::ptr info = std::make_shared<seq_inspect_info>();
    info->id = action.get_id();
    info->updater_type = "sequence";
    info->modified_type = "action";

    info->begin_value = "0";
    info->current_value = "0";
    info->end_value = to_str(actions.size());

    info->duration = seq_private::get_duration(action);

    for(const auto& act : actions)
    {
        if(act.info)
        {
            info->children.emplace_back(act.info);
        }
    }

    action.info = std::move(info);
#endif
}

void add_together_info(seq_action& action, const std::vector<seq_action>& actions)
{
#ifdef SEQ_INSPECTOR_ENABLE
    seq_inspect_info::ptr info = std::make_shared<seq_inspect_info>();
    info->id = action.get_id();
    info->updater_type = "together";
    info->modified_type = "action";

    info->begin_value = "n/a";
    info->current_value = "n/a";
    info->end_value = "n/a";

    info->duration = seq_private::get_duration(action);

    for(const auto& act : actions)
    {
        if(act.info)
        {
            info->children.emplace_back(act.info);
        }
    }

    action.info = std::move(info);
#endif
}

void add_delay_info(seq_action& action)
{
#ifdef SEQ_INSPECTOR_ENABLE
    seq_inspect_info::ptr info = std::make_shared<seq_inspect_info>();
    info->id = action.get_id();
    info->updater_type = "delay";
    info->modified_type = "n/a";

    info->begin_value = "n/a";
    info->current_value = "n/a";
    info->end_value = "n/a";

    info->duration = seq_private::get_duration(action);

    action.info = std::move(info);
#endif
}

void add_repeat_info(seq_action& repeat_action, const seq_action& action, size_t times)
{
#ifdef SEQ_INSPECTOR_ENABLE
    seq_inspect_info::ptr info = std::make_shared<seq_inspect_info>();
    info->id = repeat_action.get_id();
    info->updater_type = "repeat";

    if(times == 0)
    {
        info->begin_value = "infinity";
        info->current_value = "infinity";
        info->end_value = "infinity";
        info->duration = 0ms;
    }
    else
    {
        info->begin_value = "0";
        info->current_value = "0";
        info->end_value = std::to_string(times);
        info->duration = duration_t(seq_private::get_duration(repeat_action).count() * times);
    }

    if(action.info)
    {
        info->children.emplace_back(action.info);
    }
    repeat_action.info = std::move(info);
#endif
}

void update_action_state(seq_action& action, state_t state)
{
#ifdef SEQ_INSPECTOR_ENABLE
    if(action.info)
    {
        action.info->state = to_str(state);
    }
#endif
}

void update_action_status(seq_action& action)
{
#ifdef SEQ_INSPECTOR_ENABLE
    if(action.info)
    {
        update_action_status(action, 0);
        action.info->current_value = "n/a";
    }
#endif
}

void add_location(seq_action& action, const hpp::source_location& location)
{
#ifdef SEQ_INSPECTOR_ENABLE
    if(action.info)
    {
        action.info->file_name = location.file_name();
        action.info->function_name = location.function_name();
        action.info->line_number = location.line();
        action.info->column_offset = location.column();
    }
#endif
}

} // end of namespace inspector
} // namespace seq
