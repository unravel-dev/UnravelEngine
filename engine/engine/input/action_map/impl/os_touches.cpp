#include "os_touches.hpp"

#include <algorithm>

namespace input
{
namespace
{
auto is_over(touch_phase phase) -> bool
{
    return phase == touch_phase::ended || phase == touch_phase::canceled;
}
} // namespace

void os_touches::update()
{
    touches_.erase(std::remove_if(touches_.begin(),
                                  touches_.end(),
                                  [](const tracked_touch& touch)
                                  {
                                      return is_over(touch.point.phase) && !touch.is_end_pending;
                                  }),
                   touches_.end());
    for(auto& touch : touches_)
    {
        touch.point.delta = {};
        if(touch.is_end_pending)
        {
            touch.point.phase = touch.pending_phase;
            touch.is_end_pending = false;
        }
        else
        {
            touch.point.phase = touch_phase::stationary;
        }
    }
}

void os_touches::begin(uint64_t device_id, uint64_t finger_id, coord position, float pressure)
{
    // A finger id is unique only while it touches: a stale entry for it is a finger whose end was lost.
    touches_.erase(std::remove_if(touches_.begin(),
                                  touches_.end(),
                                  [&](const tracked_touch& touch)
                                  {
                                      return touch.device_id == device_id && touch.finger_id == finger_id;
                                  }),
                   touches_.end());
    tracked_touch touch;
    touch.device_id = device_id;
    touch.finger_id = finger_id;
    touch.point.id = get_free_id();
    touch.point.phase = touch_phase::began;
    touch.point.position = position;
    touch.point.pressure = pressure;
    touches_.push_back(touch);
}

void os_touches::move(uint64_t device_id, uint64_t finger_id, coord position, float pressure)
{
    tracked_touch* touch = find(device_id, finger_id);
    if(touch == nullptr || is_over(touch->point.phase) || touch->is_end_pending)
    {
        return;
    }
    touch->point.delta.x += position.x - touch->point.position.x;
    touch->point.delta.y += position.y - touch->point.position.y;
    touch->point.position = position;
    touch->point.pressure = pressure;
    if(touch->point.phase != touch_phase::began)
    {
        touch->point.phase = touch_phase::moved;
    }
}

void os_touches::end(uint64_t device_id, uint64_t finger_id, coord position, bool is_canceled)
{
    tracked_touch* touch = find(device_id, finger_id);
    if(touch == nullptr || is_over(touch->point.phase) || touch->is_end_pending)
    {
        return;
    }
    const touch_phase end_phase = is_canceled ? touch_phase::canceled : touch_phase::ended;
    touch->point.delta.x += position.x - touch->point.position.x;
    touch->point.delta.y += position.y - touch->point.position.y;
    touch->point.position = position;
    if(touch->point.phase == touch_phase::began)
    {
        touch->is_end_pending = true;
        touch->pending_phase = end_phase;
        return;
    }
    touch->point.phase = end_phase;
}

void os_touches::cancel_all()
{
    for(auto& touch : touches_)
    {
        touch.is_end_pending = false;
        touch.point.phase = touch_phase::canceled;
    }
}

auto os_touches::get_count() const -> size_t
{
    return touches_.size();
}

auto os_touches::get(size_t index) const -> const touch_point&
{
    return touches_.at(index).point;
}

auto os_touches::find(uint64_t device_id, uint64_t finger_id) -> tracked_touch*
{
    auto it = std::find_if(touches_.begin(),
                           touches_.end(),
                           [&](const tracked_touch& touch)
                           {
                               return touch.device_id == device_id && touch.finger_id == finger_id;
                           });
    return it == touches_.end() ? nullptr : &*it;
}

auto os_touches::get_free_id() const -> int32_t
{
    int32_t id = 0;
    while(std::any_of(touches_.begin(),
                      touches_.end(),
                      [id](const tracked_touch& touch)
                      {
                          return touch.point.id == id;
                      }))
    {
        ++id;
    }
    return id;
}
} // namespace input
