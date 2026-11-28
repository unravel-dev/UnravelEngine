#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/rendering/dynamic_geometry/dynamic_geometry.h>
#include <engine/rendering/dynamic_geometry/dynamic_renderer.h>
#include <engine/rendering/dynamic_geometry/strip_style.h>
#include <math/math.h>

#include <vector>

namespace unravel
{

/**
 * @brief A trail: a strip along where the entity has been, drawn with a Surface material. Each point of it lives for
 * Time seconds; position 0 of the strip's style is the entity and 1 the oldest point.
 *
 * In Edge mode the trail is the sheet two points of the entity (a blade's base and tip) sweep through space, which
 * the strip's Subdivisions round into an arc between frames: a weapon's swing.
 *
 * The points are runtime state: never saved, and gone when play begins or ends. A trail follows its entity in edit
 * mode too. A trail that stops taking points and starts again leaves the two stretches apart: no strip joins them.
 */
class trail_component : public component_crtp<trail_component>, public dynamic_renderer
{
public:
    /// Called when the component is created: it starts on the engine's material for it, so its overrides have
    /// a material to be of.
    static void on_create_component(entt::registry& r, entt::entity e);

    /// Seconds a point of the trail lives.
    float time = 1.0f;
    /// How far the entity moves, in metres, before the trail takes another point.
    float min_distance = 0.05f;
    /// Whether the trail takes new points. Off, the ones it has age out.
    bool emitting = true;
    /// The trail is the sheet between Edge Base and Edge Tip as they sweep through space.
    bool edge = false;
    /// Edge mode: the two ends of the edge, in the entity's space.
    math::vec3 edge_base{0.0f, 0.0f, 0.0f};
    math::vec3 edge_tip{0.0f, 1.0f, 0.0f};
    /// How the strip is shaped, sized, coloured and textured along its length.
    strip_style strip;

    /// The most points a trail keeps: the oldest go first.
    static constexpr size_t MAX_POINTS = 1024;

    /// Forgets every point: the trail starts again where the entity is.
    void clear();
    /// Once a frame: ages the points by @p dt seconds, drops the ones past their time and takes where the entity is.
    void update(const math::transform& world, float dt);
    /// How many points the trail holds.
    auto get_point_count() const -> size_t;

    /**
     * @brief Adds the trail's triangles to @p out, placed relative to @p origin (a point near the camera).
     * @param world The entity's world transform: the trail's head.
     * @param camera_position Where the camera that draws the trail stands, in the world.
     * @param width_floor The least width the strip gets: the editor's picking widens a thin trail.
     */
    void build(const math::transform& world,
               const math::vec3& camera_position,
               const math::vec3& origin,
               dynamic_geometry& out,
               const strip_width_floor& width_floor = {}) const;

    /// The box around the trail in the world.
    auto get_world_bounds(const math::transform& world) const -> math::bbox;

private:
    /// Where the entity was: its position (the edge's base in Edge mode), the edge's tip, seconds since, and the
    /// metres the trail had run by then, which a tiled texture follows.
    struct trail_point
    {
        math::transform::position_t base{};
        math::transform::position_t tip{};
        float age = 0.0f;
        float travelled = 0.0f;
        /// The trail took no points between the one before and this one: no strip joins the two.
        bool starts_run = false;
    };

    /// The entity's place now, as a point of age 0.
    auto make_head(const math::transform& world) const -> trail_point;
    /// The first steps of update: every point is @p dt seconds older, and the ones past their time are gone.
    void age_points(float dt);
    /// The last step of update: where the entity is becomes a point once it is Min Distance from the last one, and
    /// until then the last one stays as young as the entity's stay on it.
    void take_point(const math::transform& world);
    /// Keeps the metres the points carry small on a trail that runs far (TRAIL_TRAVELLED_RESTART).
    void restart_travelled();
    /// The points to draw, from the entity back to where the trail's time runs out, into @p points. A point that
    /// starts a run is not joined to the one before it in that list.
    void collect_points(const math::transform& world, std::vector<trail_point>& points) const;

    /// Oldest first.
    std::vector<trail_point> history_;
    /// Whether the last update took points: the first point after a pause starts a run.
    bool was_emitting_ = false;
    /// The strip's curves as tables, read again when a curve changes.
    mutable strip_sampler sampler_;
};

} // namespace unravel
