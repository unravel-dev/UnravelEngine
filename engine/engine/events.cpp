#include "events.h"
#include "seq/seq_core.h"
#include <seq/seq.h>

namespace unravel
{
void events::toggle_play_mode(rtti::context& ctx)
{
    auto action = seq::delay(0ms);
    action.on_end.connect([this, &ctx]()
    {
        set_play_mode(ctx, !is_playing);
    });
    seq::queue(action, "events");
}

void events::set_play_mode(rtti::context& ctx, bool play)
{
    if(is_playing == play)
    {
        return;
    }


    if(play)
    {
        on_play_before_begin(ctx);

        is_playing = play;

        on_play_begin(ctx);

        frames_playing = 0;
    }
    else
    {
        if(is_paused)
        {
            set_paused(ctx, false);
        }

        on_play_end(ctx);

        is_playing = play;

        on_play_after_end(ctx);

    }
}

void events::toggle_pause(rtti::context& ctx)
{
    auto action = seq::delay(0ms);
    action.on_begin.connect([this, &ctx]()
    {
        set_paused(ctx, !is_paused);
    });
    seq::queue(action, "events");
}

void events::set_paused(rtti::context& ctx, bool paused)
{
    if(paused && !is_playing)
    {
        return;
    }

    if(is_paused == paused)
    {
        return;
    }

    is_paused = paused;
    is_paused ? on_pause(ctx) : on_resume(ctx);
}

void events::skip_next_frame(rtti::context& ctx)
{
    if(!is_playing)
    {
        return;
    }

    if(!is_paused)
    {
        return;
    }

    on_skip_next_frame(ctx);
}

} // namespace unravel
