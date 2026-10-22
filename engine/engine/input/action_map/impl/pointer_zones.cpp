#include "pointer_zones.hpp"

#include <algorithm>

namespace input
{
namespace
{
auto is_inside(const zone& area, coord position) -> bool
{
    return position.x >= area.x && position.x < area.x + area.w && position.y >= area.y &&
           position.y < area.y + area.h;
}
} // namespace

auto pointer_zones::create() -> zone_id
{
    pointer_zone created;
    created.id = next_id_++;
    zones_.push_back(created);
    return created.id;
}

void pointer_zones::destroy(zone_id id)
{
    zones_.erase(std::remove_if(zones_.begin(),
                                zones_.end(),
                                [id](const pointer_zone& candidate)
                                {
                                    return candidate.id == id;
                                }),
                 zones_.end());
}

void pointer_zones::clear()
{
    zones_.clear();
}

void pointer_zones::set_area(zone_id id, const zone& area)
{
    if(pointer_zone* found = find_zone(id))
    {
        found->area = area;
    }
}

void pointer_zones::update()
{
    for(auto& candidate : zones_)
    {
        candidate.pointer.update();
    }
}

auto pointer_zones::is_free_zone_at(coord position) const -> bool
{
    return std::any_of(zones_.begin(),
                       zones_.end(),
                       [&](const pointer_zone& candidate)
                       {
                           return is_inside(candidate.area, position) && !candidate.pointer.has_touching();
                       });
}

auto pointer_zones::take_finger(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool
{
    return take(device_id, finger_id, position, pressure);
}

auto pointer_zones::move_finger(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool
{
    pointer_zone* holder = find_holder(device_id, finger_id);
    if(holder == nullptr)
    {
        return false;
    }
    holder->pointer.move(device_id, finger_id, position, pressure);
    return true;
}

auto pointer_zones::end_finger(uint64_t device_id, uint64_t finger_id, coord position, bool is_canceled) -> bool
{
    pointer_zone* holder = find_holder(device_id, finger_id);
    if(holder == nullptr)
    {
        return false;
    }
    holder->pointer.end(device_id, finger_id, position, is_canceled);
    return true;
}

auto pointer_zones::take_mouse(coord position) -> bool
{
    constexpr float MOUSE_PRESSURE = 1.0f;
    return take(MOUSE_DEVICE, MOUSE_FINGER, position, MOUSE_PRESSURE);
}

void pointer_zones::move_mouse(coord position)
{
    pointer_zone* holder = find_holder(MOUSE_DEVICE, MOUSE_FINGER);
    // The mouse is polled every frame; only a change of position is motion.
    if(holder == nullptr || holder->pointer.get(holder->pointer.get_count() - 1).position == position)
    {
        return;
    }
    constexpr float MOUSE_PRESSURE = 1.0f;
    holder->pointer.move(MOUSE_DEVICE, MOUSE_FINGER, position, MOUSE_PRESSURE);
}

auto pointer_zones::end_mouse(coord position) -> bool
{
    return end_finger(MOUSE_DEVICE, MOUSE_FINGER, position, false);
}

auto pointer_zones::is_mouse_held() const -> bool
{
    return std::any_of(zones_.begin(),
                       zones_.end(),
                       [](const pointer_zone& candidate)
                       {
                           return candidate.pointer.is_touching(MOUSE_DEVICE, MOUSE_FINGER);
                       });
}

auto pointer_zones::get_pointer(zone_id id) const -> const touch_point*
{
    const pointer_zone* found = find_zone(id);
    if(found == nullptr || found->pointer.get_count() == 0)
    {
        return nullptr;
    }
    return &found->pointer.get(found->pointer.get_count() - 1);
}

auto pointer_zones::is_mouse_pointer(zone_id id) const -> bool
{
    const pointer_zone* found = find_zone(id);
    return found != nullptr && found->pointer.get_count() > 0 && found->is_mouse;
}

void pointer_zones::cancel_all()
{
    for(auto& candidate : zones_)
    {
        candidate.pointer.cancel_all();
    }
}

auto pointer_zones::find_zone(zone_id id) -> pointer_zone*
{
    auto it = std::find_if(zones_.begin(),
                           zones_.end(),
                           [id](const pointer_zone& candidate)
                           {
                               return candidate.id == id;
                           });
    return it == zones_.end() ? nullptr : &*it;
}

auto pointer_zones::find_zone(zone_id id) const -> const pointer_zone*
{
    auto it = std::find_if(zones_.begin(),
                           zones_.end(),
                           [id](const pointer_zone& candidate)
                           {
                               return candidate.id == id;
                           });
    return it == zones_.end() ? nullptr : &*it;
}

auto pointer_zones::find_free_zone_at(coord position) -> pointer_zone*
{
    auto it = std::find_if(zones_.rbegin(),
                           zones_.rend(),
                           [&](const pointer_zone& candidate)
                           {
                               return is_inside(candidate.area, position) && !candidate.pointer.has_touching();
                           });
    return it == zones_.rend() ? nullptr : &*it;
}

auto pointer_zones::find_holder(uint64_t device_id, uint64_t finger_id) -> pointer_zone*
{
    auto it = std::find_if(zones_.begin(),
                           zones_.end(),
                           [&](const pointer_zone& candidate)
                           {
                               return candidate.pointer.is_touching(device_id, finger_id);
                           });
    return it == zones_.end() ? nullptr : &*it;
}

auto pointer_zones::take(uint64_t device_id, uint64_t finger_id, coord position, float pressure) -> bool
{
    pointer_zone* free_zone = find_free_zone_at(position);
    if(free_zone == nullptr)
    {
        return false;
    }
    free_zone->pointer.begin(device_id, finger_id, position, pressure);
    free_zone->is_mouse = device_id == MOUSE_DEVICE;
    return true;
}

} // namespace input
