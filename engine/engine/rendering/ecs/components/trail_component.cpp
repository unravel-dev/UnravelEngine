#include "trail_component.h"

#include <algorithm>
#include <cmath>

namespace unravel
{

namespace
{
/// The least step, in metres, a trail takes a point for, whatever its Min Distance: an entity that holds still rests
/// on its last point.
constexpr float TRAIL_MIN_STEP = 1.0e-4f;
/// Once the oldest point's distance along the trail passes this many metres, the distances start over from a whole
/// number of texture repeats: a float keeps them fine enough for a tiled texture however far the trail runs.
constexpr float TRAIL_TRAVELLED_RESTART = 4096.0f;

/// The metres between two places of a trail.
auto get_trail_step(const math::transform::position_t& from, const math::transform::position_t& to) -> float
{
    return math::length(math::vec3(to - from));
}
} // namespace

void trail_component::on_create_component(entt::registry& r, entt::entity e)
{
    entt::handle entity(r, e);
    entity.get<trail_component>().set_material(default_strip_material());
}

void trail_component::clear()
{
    history_.clear();
    was_emitting_ = false;
}

auto trail_component::get_point_count() const -> size_t
{
    return history_.size();
}

auto trail_component::make_head(const math::transform& world) const -> trail_point
{
    trail_point head;
    head.base = edge ? world.transform_coord(math::transform::position_t(edge_base)) : world.get_position();
    head.tip = edge ? world.transform_coord(math::transform::position_t(edge_tip)) : head.base;
    return head;
}

void trail_component::update(const math::transform& world, float dt)
{
    age_points(dt);
    const bool takes_points = emitting && time > 0.0f;
    if(takes_points)
    {
        take_point(world);
    }
    was_emitting_ = takes_points;
}

void trail_component::age_points(float dt)
{
    if(time <= 0.0f)
    {
        history_.clear();
        return;
    }
    for(auto& point : history_)
    {
        point.age += dt;
    }
    // A point past the trail's time stays while a strip joins it to a younger one: the tail ends between the two,
    // so it shortens smoothly.
    size_t expired = 0;
    while(expired < history_.size() && history_[expired].age > time)
    {
        const bool ends_a_tail = expired + 1 < history_.size() && !history_[expired + 1].starts_run && history_[expired + 1].age <= time;
        if(ends_a_tail)
        {
            break;
        }
        ++expired;
    }
    history_.erase(history_.begin(), history_.begin() + ptrdiff_t(expired));
    if(!history_.empty())
    {
        // Nothing comes before the oldest point.
        history_.front().starts_run = false;
    }
}

void trail_component::take_point(const math::transform& world)
{
    trail_point head = make_head(world);
    // After a pause no strip joins the points before to the ones that follow.
    const bool starts_run = !was_emitting_ && !history_.empty();
    const float least_step = std::max(min_distance, TRAIL_MIN_STEP);
    const bool has_moved = !history_.empty() && (get_trail_step(history_.back().base, head.base) > least_step ||
                                                 get_trail_step(history_.back().tip, head.tip) > least_step);
    if(!history_.empty() && !starts_run && !has_moved)
    {
        // The entity rests on its last point: a trail that leaves it later starts there, not where its time ran out.
        history_.back().age = 0.0f;
        return;
    }
    if(history_.size() >= MAX_POINTS)
    {
        history_.erase(history_.begin());
    }
    if(!history_.empty() && !starts_run)
    {
        const auto& last = history_.back();
        head.travelled = last.travelled + get_trail_step((last.base + last.tip) * 0.5f, (head.base + head.tip) * 0.5f);
    }
    head.starts_run = starts_run;
    history_.push_back(head);
    restart_travelled();
}

void trail_component::restart_travelled()
{
    if(history_.front().travelled < TRAIL_TRAVELLED_RESTART)
    {
        return;
    }
    const float repeat = std::max(strip.tile_length, STRIP_MIN_TILE_LENGTH);
    const float whole_repeats = std::floor(history_.front().travelled / repeat) * repeat;
    for(auto& point : history_)
    {
        point.travelled -= whole_repeats;
    }
}

void trail_component::collect_points(const math::transform& world, std::vector<trail_point>& points) const
{
    points.clear();
    if(history_.empty() || time <= 0.0f)
    {
        return;
    }
    points.reserve(history_.size() + 1);
    // Whether a strip joins the point about to be added to the one added before it, which is the younger of the two.
    bool joins_younger = false;
    // The entity's place joins the trail while the trail takes points: one that was just told to emit starts its run
    // at the next update.
    if(emitting && was_emitting_)
    {
        const auto& last = history_.back();
        trail_point head = make_head(world);
        head.travelled = last.travelled + get_trail_step((last.base + last.tip) * 0.5f, (head.base + head.tip) * 0.5f);
        points.push_back(head);
        joins_younger = true;
    }
    for(size_t i = history_.size(); i-- > 0;)
    {
        const auto& point = history_[i];
        if(point.age <= time)
        {
            points.push_back(point);
            points.back().starts_run = !joins_younger && points.size() > 1;
            joins_younger = !point.starts_run;
            continue;
        }
        if(joins_younger)
        {
            // The tail ends where the trail's time runs out, between this point and the younger one.
            const auto& younger = points.back();
            const float span = point.age - younger.age;
            const float share = span > 0.0f ? std::clamp((time - younger.age) / span, 0.0f, 1.0f) : 0.0f;
            const auto mix_place = [share](const math::transform::position_t& from, const math::transform::position_t& to)
            {
                return from + (to - from) * math::transform::position_t::value_type(share);
            };
            trail_point end;
            end.base = mix_place(younger.base, point.base);
            end.tip = mix_place(younger.tip, point.tip);
            end.age = time;
            end.travelled = math::mix(younger.travelled, point.travelled, share);
            points.push_back(end);
        }
        break;
    }
}

void trail_component::build(const math::transform& world,
                            const math::vec3& camera_position,
                            const math::vec3& origin,
                            dynamic_geometry& out,
                            const strip_width_floor& width_floor) const
{
    // The scratch lists keep their memory between builds.
    thread_local std::vector<trail_point> points;
    collect_points(world, points);
    if(points.size() < 2)
    {
        return;
    }
    const math::transform::position_t origin_position(origin);
    auto options = strip.make_options(sampler_.get(strip), camera_position - origin, world.get_rotation());
    options.width_floor = width_floor;
    // A tiled texture stays on the points it was laid on, while the trail grows at its head and ends at its tail.
    options.has_point_distances = true;
    if(edge)
    {
        thread_local std::vector<sheet_point> swept;
        swept.clear();
        swept.reserve(points.size());
        for(const auto& point : points)
        {
            sheet_point section;
            section.base = math::vec3(point.base - origin_position);
            section.tip = math::vec3(point.tip - origin_position);
            section.along = point.age / time;
            section.distance = point.travelled;
            section.starts_run = point.starts_run;
            swept.push_back(section);
        }
        build_sheet(swept, options, out);
        return;
    }
    thread_local std::vector<strip_point> placed;
    placed.clear();
    placed.reserve(points.size());
    for(const auto& point : points)
    {
        strip_point section;
        section.position = math::vec3(point.base - origin_position);
        section.along = point.age / time;
        section.distance = point.travelled;
        section.starts_run = point.starts_run;
        placed.push_back(section);
    }
    build_strip(placed, options, out);
}

auto trail_component::get_world_bounds(const math::transform& world) const -> math::bbox
{
    math::bbox bounds;
    thread_local std::vector<trail_point> points;
    collect_points(world, points);
    if(points.size() < 2)
    {
        return bounds;
    }
    for(const auto& point : points)
    {
        bounds.add_point(math::vec3(point.base));
        bounds.add_point(math::vec3(point.tip));
    }
    const float width_reach = edge ? 0.0f : strip.get_width_reach(sampler_.get(strip));
    const math::vec3 margin(width_reach + strip.get_curve_reach(bounds));
    return math::bbox(bounds.min - margin, bounds.max + margin);
}

} // namespace unravel
