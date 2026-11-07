#include "box3d_backend.h"

#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/physics/backend/contact_graph.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/mesh.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/settings.h>

#include <graphics/graphics.h>
#include <logging/logging.h>
#include <math/transform.hpp>

#include <box3d/box3d.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace box3d
{
namespace
{
// --- tuning -----------------------------------------------------------------------

/// Solver sub-steps per b3World_Step. physics_system already advances the world one
/// fixed step at a time, so the backend never catches up on its own; Box3D sub-steps
/// only subdivide the solver inside that one step (collision runs once per call), the
/// role solver iterations play in Bullet. Box3D's own recommendation is 4.
constexpr int sub_step_count = 4;
/// Upper bound for the sub-step setting.
constexpr int max_sub_step_count = 16;
/// Tessellation of the cylinder hull. Box3D hulls are capped at 128 vertices.
constexpr int cylinder_hull_sides = 16;
/// Box3D asserts on degenerate half widths; a collapsed axis is clamped to this.
constexpr float min_half_extent = 0.005f;
/// A box has eight corners.
constexpr int box_corner_count = 8;
/// Below this the entity scale is considered unchanged and shapes are not rebuilt.
constexpr float scale_epsilon = 1e-4f;
/// Generous epsilon for float conversion noise when writing transforms back.
constexpr float transform_sync_epsilon = 0.009f;
/// Same clamp the Bullet backend applies to combined friction.
constexpr float max_combined_friction = 10.0f;
/// Inertia per unit mass for a body whose shapes produced no mass (mesh-only bodies):
/// a unit sphere of radius 0.5, 2/5 * r^2.
constexpr float fallback_unit_inertia = 0.1f;
/// Character mover solver.
constexpr int mover_plane_capacity = 16;
constexpr int mover_max_iterations = 4;
constexpr float mover_tolerance = 1e-3f;
/// Triangles sampled when deciding the winding of a collision mesh from its normals.
constexpr int winding_sample_triangles = 256;
/// Ragdoll limbs grip like skin and cloth, and roll to a stop instead of spinning on.
constexpr float ragdoll_friction = 0.7f;
constexpr float ragdoll_rolling_resistance = 0.05f;
/// Wheel steering is a spring towards the steering angle. Box3D's default of 1 Hz cannot turn a loaded tire
/// against the ground's friction, so the wheel stays near straight; at this stiffness it holds the angle and the
/// steering's max torque is what limits it. Critically damped, so it does not overshoot.
constexpr float wheel_steering_hertz = 10.0f;
constexpr float wheel_steering_damping_ratio = 1.0f;
/// Box3D steers a wheel through a term divided by the sine of the angle between its spin and suspension axes, so a
/// wheel turned towards its suspension axis (a chassis moved without its wheels) would make the solver's velocities
/// infinite. A wheel steers only while that angle's cosine stays below this one, of 45 degrees.
constexpr float wheel_steering_max_axis_cosine = 0.7071f;
/// A wheel steers less than a right angle: Box3D's steering term grows without bound towards it.
constexpr float max_steering_degrees = 89.0f;
/// An axis this close to Y (as a cosine) takes X as the helper axis for its perpendiculars instead.
constexpr float helper_axis_max_cosine = 0.9f;
/// Degrees in a whole turn: joint angles wrap at half of it either way.
constexpr float full_turn_degrees = 360.0f;
/// Debug draw sizes.
constexpr float debug_point_half_size = 0.02f;
constexpr float debug_axis_length = 0.25f;

enum class manifold_type
{
    collision,
    sensor
};

enum class event_type
{
    enter,
    exit
};

struct contact_manifold
{
    manifold_type type{};
    event_type event{};
    entt::handle a{};
    entt::handle b{};
    /// Only meaningful on exit. Always stated relative to `a`.
    unravel::contact_end_reason reason{unravel::contact_end_reason::separated};

    std::vector<unravel::manifold_point> contacts;
};

// --- conversions ------------------------------------------------------------------

auto to_b3(const math::vec3& v) -> b3Vec3
{
    return {v.x, v.y, v.z};
}

auto to_b3_pos(const math::vec3& v) -> b3Pos
{
    return b3ToPos(to_b3(v));
}

auto from_b3(const b3Vec3& v) -> math::vec3
{
    return {v.x, v.y, v.z};
}

auto from_b3_pos(const b3Pos& p) -> math::vec3
{
    return from_b3(b3ToVec3(p));
}

auto to_b3(const math::quat& q) -> b3Quat
{
    return {{q.x, q.y, q.z}, q.w};
}

auto from_b3(const b3Quat& q) -> math::quat
{
    math::quat r;
    r.x = q.v.x;
    r.y = q.v.y;
    r.z = q.v.z;
    r.w = q.s;
    return r;
}

auto identity_quat() -> b3Quat
{
    return {{0.0f, 0.0f, 0.0f}, 1.0f};
}

auto identity_transform() -> b3Transform
{
    return {b3Vec3_zero, identity_quat()};
}

auto to_bx(const math::vec3& v) -> bx::Vec3
{
    return {v.x, v.y, v.z};
}

auto to_bx_color(b3HexColor color) -> uint32_t
{
    const uint32_t rgb = static_cast<uint32_t>(color);
    const uint32_t r = (rgb >> 16) & 0xFFu;
    const uint32_t g = (rgb >> 8) & 0xFFu;
    const uint32_t b = rgb & 0xFFu;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

auto to_b3_type(unravel::rigidbody_type type) -> b3BodyType
{
    switch(type)
    {
        case unravel::rigidbody_type::kinematic:
            return b3_kinematicBody;
        case unravel::rigidbody_type::dynamic:
            return b3_dynamicBody;
        case unravel::rigidbody_type::static_body:
        default:
            return b3_staticBody;
    }
}

// --- entity <-> user data ---------------------------------------------------------

/// Entities ride in shape/body user data offset by one, so a null pointer never
/// aliases entity 0.
auto encode_entity(entt::entity e) -> void*
{
    const auto value = static_cast<uintptr_t>(entt::to_integral(e)) + 1u;
    return reinterpret_cast<void*>(value);
}

auto decode_entity(void* user_data) -> entt::entity
{
    const auto value = reinterpret_cast<uintptr_t>(user_data);
    if(value == 0)
    {
        return entt::null;
    }
    return static_cast<entt::entity>(static_cast<std::underlying_type_t<entt::entity>>(value - 1u));
}

auto entity_from_shape(b3ShapeId shape) -> entt::entity
{
    if(!b3Shape_IsValid(shape))
    {
        return entt::null;
    }
    return decode_entity(b3Shape_GetUserData(shape));
}

// --- filtering --------------------------------------------------------------------

/// Layer masks are 32-bit ints where -1 means everything. Widening through uint32_t
/// keeps the bit pattern instead of sign-extending it.
auto to_filter_bits(int mask) -> uint64_t
{
    return static_cast<uint64_t>(static_cast<uint32_t>(mask));
}

auto make_filter(uint64_t category, uint64_t mask) -> b3Filter
{
    b3Filter filter = b3DefaultFilter();
    filter.categoryBits = category;
    filter.maskBits = mask;
    filter.groupIndex = 0;
    return filter;
}

/// Box3D requires the shape to accept the query as well, so a query carries every
/// category bit and only restricts what it is willing to hit.
auto make_query_filter(int layer_mask) -> b3QueryFilter
{
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits = UINT64_MAX;
    filter.maskBits = to_filter_bits(layer_mask);
    return filter;
}

auto make_query_filter(uint64_t category, uint64_t mask) -> b3QueryFilter
{
    b3QueryFilter filter = b3DefaultQueryFilter();
    filter.categoryBits = category;
    filter.maskBits = mask;
    return filter;
}

// --- material combine -------------------------------------------------------------

constexpr uint64_t combine_bits = 2;
constexpr uint64_t combine_mask = (1u << combine_bits) - 1u;
constexpr uint64_t friction_shift = combine_bits;
constexpr uint64_t restitution_shift = 0;

auto encode_combine_modes(unravel::combine_mode friction, unravel::combine_mode restitution) -> uint64_t
{
    const uint64_t f = (static_cast<uint64_t>(friction) & combine_mask) << friction_shift;
    const uint64_t r = (static_cast<uint64_t>(restitution) & combine_mask) << restitution_shift;
    return f | r;
}

auto decode_friction_combine(uint64_t code) -> unravel::combine_mode
{
    return static_cast<unravel::combine_mode>((code >> friction_shift) & combine_mask);
}

auto decode_restitution_combine(uint64_t code) -> unravel::combine_mode
{
    return static_cast<unravel::combine_mode>((code >> restitution_shift) & combine_mask);
}

/// If both bodies asked for the same mode use it, otherwise fall back to averaging.
auto pick_combine_mode(unravel::combine_mode a, unravel::combine_mode b) -> unravel::combine_mode
{
    return a == b ? a : unravel::combine_mode::average;
}

auto combine_values(float a, float b, unravel::combine_mode mode) -> float
{
    switch(mode)
    {
        case unravel::combine_mode::multiply:
            return a * b;
        case unravel::combine_mode::minimum:
            return std::min(a, b);
        case unravel::combine_mode::maximum:
            return std::max(a, b);
        case unravel::combine_mode::average:
        default:
            return (a + b) * 0.5f;
    }
}

/// World level mixing callbacks. Called from worker threads; pure by design.
auto combined_friction_callback(float friction_a, uint64_t material_a, float friction_b, uint64_t material_b) -> float
{
    const auto mode = pick_combine_mode(decode_friction_combine(material_a), decode_friction_combine(material_b));
    const float friction = combine_values(friction_a, friction_b, mode);
    return std::clamp(friction, -max_combined_friction, max_combined_friction);
}

auto combined_restitution_callback(float restitution_a, uint64_t material_a, float restitution_b, uint64_t material_b)
    -> float
{
    const auto mode = pick_combine_mode(decode_restitution_combine(material_a), decode_restitution_combine(material_b));
    return combine_values(restitution_a, restitution_b, mode);
}

// --- box3d diagnostics --------------------------------------------------------------

void log_callback(const char* message)
{
    APPLOG_WARNING("box3d: {}", message ? message : "");
}

auto assert_callback(const char* condition, const char* file_name, int line_number) -> int
{
    APPLOG_ERROR("box3d: assertion failed: {} ({}:{})",
                 condition ? condition : "",
                 file_name ? file_name : "",
                 line_number);
    return 1;
}

// --- per entity backend state ---------------------------------------------------------

struct rigidbody
{
    b3BodyId body{};
    std::vector<b3ShapeId> shapes;
    /// Collision meshes referenced by mesh shapes. Box3D holds a pointer, so these live
    /// exactly as long as the shapes do.
    std::vector<b3MeshData*> meshes;
    /// Entity scale currently baked into the shapes.
    math::vec3 shape_scale{1.0f, 1.0f, 1.0f};
    /// Sensor flag currently baked into the shapes.
    bool shapes_are_sensor{false};
    uint64_t category_bits{0};
    uint64_t mask_bits{0};
    /// Last surface material pushed to the shapes, so unchanged materials cost a compare.
    float applied_friction{-1.0f};
    float applied_restitution{-1.0f};
    float applied_rolling_resistance{-1.0f};
    math::vec3 applied_surface_velocity{0.0f};
    uint64_t applied_material_id{UINT64_MAX};
    unravel::contact_links links{};
};

/// One surface the mover touched in its last step. Feeds collision events for the
/// character, which the proxy body cannot supply against static or kinematic geometry.
struct mover_contact
{
    entt::entity entity{entt::null};
    b3ShapeId shape{};
    math::vec3 point{};
    /// Outward normal of the touched surface (points at the character).
    math::vec3 normal{};
    float separation{};
};

struct character_controller
{
    /// Kinematic proxy so sensors detect the character and dynamic bodies collide with it.
    b3BodyId body{};
    b3ShapeId shape{};
    /// Capsule origin (entity position plus the component center).
    math::vec3 position{};
    math::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::vec3 velocity{};
    /// Accumulated move() displacement; consumed once per fixed step.
    math::vec3 pending_displacement{};
    float radius{0.5f};
    float half_segment{0.0f};
    uint64_t category_bits{0};
    uint64_t mask_bits{0};
    bool grounded{false};
    /// Set by jump, cleared on landing. Suppresses the step-down snap that would
    /// otherwise pull the character straight back onto the floor.
    bool jumping{false};
    /// The ragdoll on this character's model: the mover walks through its hitboxes.
    entt::entity own_ragdoll{entt::null};
    unravel::physics_vector<mover_contact, mover_plane_capacity> touching;
    unravel::contact_links links{};
};

/// The Box3D joint of a joint component, on the joint's entity.
struct joint
{
    b3JointId id{};
    b3BodyId body_a{};
    b3BodyId body_b{};
    /// The joint frames in each body's space, kept so a body made again gets the same joint back.
    b3Transform frame_a{};
    b3Transform frame_b{};
    bool has_frames{false};
    /// Distance joints: the distance between the anchors when the joint was made.
    float length{0.0f};
    float previous_angle{0.0f};
    bool has_previous_angle{false};
};

/// One body of a ragdoll and its joint to the parent body.
struct ragdoll_part
{
    rigidbody body;
    b3JointId joint{};
    b3Transform frame_a{};
    b3Transform frame_b{};
    unravel::ragdoll_joint_type joint_type{unravel::ragdoll_joint_type::ball};
    /// World drive: a kinematic body on the animated pose and the motor joint that pulls the part to it.
    b3BodyId anchor{};
    b3JointId anchor_joint{};
    bool is_dynamic{false};
};

/// The Box3D bodies of a ragdoll component, on the ragdoll's entity.
struct ragdoll
{
    std::vector<ragdoll_part> parts;
    std::vector<b3JointId> filters;
    entt::entity controller{entt::null};
    /// Negative collision group of the ragdoll's bodies when they do not collide with each other.
    int group{0};
    unravel::contact_links links{};
};

/**
 * @brief Resolves a participant's contact list head.
 *
 * Rigid bodies are checked first because they vastly outnumber controllers.
 */
inline auto find_contact_links(entt::handle entity) -> unravel::contact_links*
{
    if(!entity)
    {
        return nullptr;
    }
    if(auto* body = entity.try_get<rigidbody>())
    {
        return &body->links;
    }
    if(auto* cc = entity.try_get<character_controller>())
    {
        return &cc->links;
    }
    if(auto* parts = entity.try_get<ragdoll>())
    {
        return &parts->links;
    }
    return nullptr;
}

/**
 * @brief The entity a shape takes part in contacts as: its own, or for a ragdoll body the ragdoll's entity, so
 *        sensors and scripts see one character instead of fifteen bones. Queries still report the bone.
 */
auto contact_entity_from_shape(entt::registry& registry, b3ShapeId shape) -> entt::entity
{
    const entt::entity entity = entity_from_shape(shape);
    if(entity == entt::null)
    {
        return entity;
    }
    const entt::entity owner = decode_entity(b3Body_GetUserData(b3Shape_GetBody(shape)));
    if(owner != entity && registry.valid(owner) && registry.all_of<ragdoll>(owner))
    {
        return owner;
    }
    return entity;
}

// --- contact policy (identical rules to the Bullet backend) ---------------------------

auto resolve_contact_flags(entt::handle entity) -> unravel::contact_event_flags
{
    if(!entity)
    {
        return unravel::contact_event_flags::none;
    }
    if(const auto* comp = entity.try_get<unravel::physics_component>())
    {
        return comp->get_contact_event_flags();
    }
    return unravel::contact_event_flags_default;
}

auto effective_contact_flags(entt::handle a, entt::handle b, manifold_type type) -> unravel::contact_event_flags
{
    const auto flags_a = resolve_contact_flags(a);
    if(type == manifold_type::sensor)
    {
        return flags_a;
    }
    return flags_a | resolve_contact_flags(b);
}

auto can_observe_contacts(entt::handle entity) -> bool
{
    if(entity)
    {
        if(auto* script = entity.try_get<unravel::script_component>())
        {
            return script->has_script_components();
        }
    }
    return false;
}

auto has_contact_listener(entt::handle a, entt::handle b, manifold_type type) -> bool
{
    if(can_observe_contacts(a))
    {
        return true;
    }
    return type == manifold_type::collision && can_observe_contacts(b);
}

auto should_track_contact(unravel::contact_event_flags flags, manifold_type type) -> bool
{
    const auto mask = (type == manifold_type::sensor) ? unravel::contact_event_flags::sensor_events
                                                      : unravel::contact_event_flags::collision_events;
    return unravel::has_any(flags, mask);
}

auto should_flush_on_destroy(unravel::contact_event_flags flags, manifold_type type) -> bool
{
    const auto mask = (type == manifold_type::sensor) ? unravel::contact_event_flags::sensor_exit_on_destroy
                                                      : unravel::contact_event_flags::collision_exit_on_destroy;
    return unravel::has_any(flags, mask);
}

/**
 * @brief Converts one touching contact into manifold points for the listener side.
 *
 * Box3D anchors are relative to each body's center of mass and the manifold normal
 * points from A to B; the engine convention has normal_on_a pointing a -> b.
 */
void copy_contact_points(const b3ContactData& data, bool swap_sides, std::vector<unravel::manifold_point>& out)
{
    out.clear();
    const b3BodyId body_a = b3Shape_GetBody(data.shapeIdA);
    const b3BodyId body_b = b3Shape_GetBody(data.shapeIdB);
    const math::vec3 center_a = from_b3_pos(b3Body_GetWorldCenter(body_a));
    const math::vec3 center_b = from_b3_pos(b3Body_GetWorldCenter(body_b));
    for(int m = 0; m < data.manifoldCount; ++m)
    {
        const b3Manifold& manifold = data.manifolds[m];
        const math::vec3 normal = from_b3(manifold.normal);
        for(int i = 0; i < manifold.pointCount; ++i)
        {
            const b3ManifoldPoint& p = manifold.points[i];
            const math::vec3 point_a = center_a + from_b3(p.anchorA);
            const math::vec3 point_b = center_b + from_b3(p.anchorB);

            unravel::manifold_point mp;
            if(swap_sides)
            {
                mp.a = point_b;
                mp.b = point_a;
                mp.normal_on_a = -normal;
                mp.normal_on_b = normal;
            }
            else
            {
                mp.a = point_a;
                mp.b = point_b;
                mp.normal_on_a = normal;
                mp.normal_on_b = -normal;
            }
            mp.distance = p.separation;
            mp.impulse = p.normalImpulse;
            out.push_back(mp);
        }
    }
}

/**
 * @brief Synthesizes a single manifold point for a sensor overlap.
 *
 * Box3D reports sensor overlaps as shape pairs without geometry. The closest point on
 * the visitor to the sensor body is cheap and only computed when a pair enters.
 */
void make_sensor_points(b3ShapeId sensor, b3ShapeId visitor, std::vector<unravel::manifold_point>& out)
{
    out.clear();
    const math::vec3 sensor_center = from_b3_pos(b3Body_GetWorldCenter(b3Shape_GetBody(sensor)));
    const math::vec3 point = from_b3(b3Shape_GetClosestPoint(visitor, to_b3(sensor_center)));
    math::vec3 normal = sensor_center - point;
    const float len = math::length(normal);
    normal = len > 0.0f ? normal / len : math::vec3{0.0f, 1.0f, 0.0f};

    unravel::manifold_point mp;
    mp.a = point;
    mp.b = point;
    // Sensor is `a`; normal_on_a points from the sensor towards the visitor.
    mp.normal_on_a = -normal;
    mp.normal_on_b = normal;
    mp.distance = 0.0f;
    mp.impulse = 0.0f;
    out.push_back(mp);
}

void make_mover_points(const mover_contact& contact, std::vector<unravel::manifold_point>& out)
{
    out.clear();
    unravel::manifold_point mp;
    mp.a = contact.point;
    mp.b = contact.point;
    // Character is `a`; the plane normal points from the surface at the character.
    mp.normal_on_a = -contact.normal;
    mp.normal_on_b = contact.normal;
    mp.distance = contact.separation;
    mp.impulse = 0.0f;
    out.push_back(mp);
}

// --- world ------------------------------------------------------------------------------

struct world
{
    b3WorldId id{};
    /// Static body at the origin that joints without a body A hang on.
    b3BodyId ground{};

    struct broken_joint
    {
        entt::entity entity{entt::null};
        math::vec3 force{};
        math::vec3 torque{};
    };
    /// Joints that broke since the last dispatch, reported to scripts with the contacts.
    std::vector<broken_joint> broken_joints;
    /// Last negative collision group handed to a ragdoll.
    int ragdoll_groups{0};
    /// Length of the last step, for joint speeds.
    float last_step_seconds{0.0f};
    /// Bodies whose sleep is off for one step, so the step restarts their count towards sleep; restore_sleep turns it
    /// back on after the step.
    std::vector<b3BodyId> sleep_restores;

    using contact_store = unravel::contact_graph<contact_manifold>;
    using contact_slot = contact_store::slot;

    /// Identifies a slot across re-entrant script calls that may have freed it.
    struct slot_ref
    {
        uint32_t slot{unravel::contact_links::npos};
        uint32_t generation{0};
    };

    contact_store contacts{&find_contact_links};

    unravel::physics_vector<slot_ref> to_enter;
    unravel::physics_vector<contact_manifold> to_exit;

    std::vector<b3ContactData> contact_scratch;
    std::vector<b3ShapeId> visitor_scratch;

    bool in_simulate{};

    auto is_valid() const -> bool
    {
        return b3World_IsValid(id);
    }

    void process_manifold(unravel::script_system& scripting, const contact_manifold& manifold)
    {
        switch(manifold.type)
        {
            case manifold_type::sensor:
            {
                if(manifold.event == event_type::enter)
                {
                    scripting.on_sensor_enter(manifold.a, manifold.b, manifold.contacts);
                }
                else
                {
                    scripting.on_sensor_exit(manifold.a, manifold.b, manifold.contacts, manifold.reason);
                }
                break;
            }
            case manifold_type::collision:
            {
                if(manifold.event == event_type::enter)
                {
                    scripting.on_collision_enter(manifold.a, manifold.b, manifold.contacts);
                }
                else
                {
                    scripting.on_collision_exit(manifold.a, manifold.b, manifold.contacts, manifold.reason);
                }
                break;
            }
            default:
                break;
        }
    }

    // --- contact graph -----------------------------------------------------------

    /**
     * @brief Finds an existing slot for a pair by walking one participant's list.
     *
     * Sensor pairs are directional (the sensor is always `a`). Collision pairs are
     * matched unordered because the same pair can be discovered from either side.
     */
    auto find_slot_for_pair(entt::handle a, entt::handle b, manifold_type type) -> uint32_t
    {
        return contacts.visit(a,
                              [&](uint32_t, const contact_slot& slot)
                              {
                                  if(slot.payload.type != type)
                                  {
                                      return false;
                                  }
                                  if(type == manifold_type::sensor)
                                  {
                                      return slot.a == a && slot.b == b;
                                  }
                                  return (slot.a == a && slot.b == b) || (slot.a == b && slot.b == a);
                              });
    }

    /**
     * @brief Drops every pair involving an entity, reporting the ones that were owed.
     *
     * Detach-then-notify: the graph is made fully consistent BEFORE any script runs.
     *
     * @param reason Stated relative to @p entity, which is the side going away.
     * @param notify False during teardown, where the exits are noise or have already
     *               been reported by the pre-destroy phase.
     */
    void release_contacts_for(entt::handle entity, unravel::contact_end_reason reason, bool notify)
    {
        auto* links = find_contact_links(entity);
        if(links == nullptr || links->head == unravel::contact_links::npos)
        {
            return;
        }

        const bool wants_events = notify && links->flush_pending != 0;

        unravel::physics_vector<contact_manifold, 8> pending;
        if(wants_events)
        {
            pending.reserve(links->flush_pending);
        }

        contacts.visit(entity,
                       [&](uint32_t id, contact_slot& slot)
                       {
                           if(wants_events && slot.flush_on_destroy && slot.enter_dispatched)
                           {
                               pending.push_back(std::move(slot.payload));

                               auto& cm = pending.back();
                               cm.event = event_type::exit;
                               cm.reason = (slot.a == entity) ? reason : unravel::mirror_contact_end_reason(reason);
                           }

                           contacts.erase(id);
                           return false;
                       });

        if(pending.empty())
        {
            return;
        }

        auto& ctx = unravel::engine::context();
        auto& scripting = ctx.get_cached<unravel::script_system>();
        for(const auto& cm : pending)
        {
            process_manifold(scripting, cm);
        }
    }

    /**
     * @brief Re-applies the contact policy to a body's existing pairs.
     */
    void refresh_contact_policy_for(entt::handle entity)
    {
        contacts.visit(entity,
                       [&](uint32_t id, contact_slot& slot)
                       {
                           const auto flags = effective_contact_flags(slot.a, slot.b, slot.payload.type);

                           if(!should_track_contact(flags, slot.payload.type))
                           {
                               contacts.erase(id);
                           }
                           else
                           {
                               contacts.set_flush_on_destroy(id, should_flush_on_destroy(flags, slot.payload.type));
                           }

                           return false;
                       });
    }

    void clear_contacts()
    {
        contacts.clear();
        to_enter.clear();
        to_exit.clear();
    }

    /**
     * @brief Marks a pair as still touching, inserting and queueing it when it is new.
     *
     * Points are captured once, when the pair is created, through @p fill so pairs that
     * are already tracked never pay for geometry.
     */
    template<typename Fill>
    void touch_pair(entt::handle a, entt::handle b, manifold_type type, Fill&& fill)
    {
        if(!has_contact_listener(a, b, type))
        {
            return;
        }

        const uint32_t existing = find_slot_for_pair(a, b, type);
        if(existing != unravel::contact_links::npos)
        {
            contacts.get(existing).seen_stamp = contacts.stamp();
            return;
        }

        const auto flags = effective_contact_flags(a, b, type);
        if(!should_track_contact(flags, type))
        {
            return;
        }

        const uint32_t id = contacts.insert(a, b, should_flush_on_destroy(flags, type));
        if(id == unravel::contact_links::npos)
        {
            return;
        }

        auto& slot = contacts.get(id);
        slot.payload.type = type;
        slot.payload.event = event_type::enter;
        slot.payload.reason = unravel::contact_end_reason::separated;
        slot.payload.a = a;
        slot.payload.b = b;
        fill(slot.payload.contacts);

        to_enter.push_back(slot_ref{id, slot.generation});
    }

    /**
     * @brief Folds a rigid body's touching contacts and sensor overlaps into the graph.
     *
     * Only bodies that can listen need this: a collision pair is matched unordered so
     * it is found from whichever side is scripted, and sensor callbacks only ever reach
     * the sensor, which is the side enumerating its visitors here.
     */
    void fold_body_contacts(unravel::scene& scn, entt::handle self, rigidbody& body)
    {
        if(!b3Body_IsValid(body.body) || !b3Body_IsEnabled(body.body))
        {
            return;
        }

        const int capacity = b3Body_GetContactCapacity(body.body);
        if(capacity > 0)
        {
            contact_scratch.resize(static_cast<size_t>(capacity));
            const int count = b3Body_GetContactData(body.body, contact_scratch.data(), capacity);
            for(int i = 0; i < count; ++i)
            {
                const b3ContactData& data = contact_scratch[static_cast<size_t>(i)];
                const entt::entity entity_a = contact_entity_from_shape(*scn.registry, data.shapeIdA);
                const entt::entity entity_b = contact_entity_from_shape(*scn.registry, data.shapeIdB);
                if(entity_a == entt::null || entity_b == entt::null)
                {
                    continue;
                }
                const bool self_is_a = (entity_a == self.entity());
                auto other = scn.create_handle(self_is_a ? entity_b : entity_a);
                if(!other || other == self)
                {
                    continue;
                }
                touch_pair(self,
                           other,
                           manifold_type::collision,
                           [&](std::vector<unravel::manifold_point>& out)
                           {
                               copy_contact_points(data, !self_is_a, out);
                           });
            }
        }

        if(!body.shapes_are_sensor)
        {
            return;
        }

        for(const b3ShapeId sensor : body.shapes)
        {
            if(!b3Shape_IsValid(sensor) || !b3Shape_IsSensor(sensor))
            {
                continue;
            }
            const int visitor_capacity = b3Shape_GetSensorCapacity(sensor);
            if(visitor_capacity <= 0)
            {
                continue;
            }
            visitor_scratch.resize(static_cast<size_t>(visitor_capacity));
            const int visitor_count = b3Shape_GetSensorData(sensor, visitor_scratch.data(), visitor_capacity);
            for(int i = 0; i < visitor_count; ++i)
            {
                const b3ShapeId visitor = visitor_scratch[static_cast<size_t>(i)];
                const entt::entity entity_v = contact_entity_from_shape(*scn.registry, visitor);
                if(entity_v == entt::null)
                {
                    continue;
                }
                auto other = scn.create_handle(entity_v);
                if(!other || other == self)
                {
                    continue;
                }
                touch_pair(self,
                           other,
                           manifold_type::sensor,
                           [&](std::vector<unravel::manifold_point>& out)
                           {
                               make_sensor_points(sensor, visitor, out);
                           });
            }
        }
    }

    /**
     * @brief Folds what the mover touched during its last step.
     *
     * Always enumerated, scripted or not: a character against a scripted static wall is
     * visible only from the character's side, because kinematic proxies never form
     * contacts with static or kinematic shapes.
     */
    void fold_character_contacts(unravel::scene& scn, entt::handle self, character_controller& cc)
    {
        for(const auto& contact : cc.touching)
        {
            if(contact.entity == entt::null)
            {
                continue;
            }
            auto other = scn.create_handle(contact.entity);
            if(!other || other == self)
            {
                continue;
            }
            touch_pair(self,
                       other,
                       manifold_type::collision,
                       [&](std::vector<unravel::manifold_point>& out)
                       {
                           make_mover_points(contact, out);
                       });
        }
    }

    /**
     * @brief Folds what a scripted ragdoll's bodies touch, as one participant: the pair stays while any body touches.
     */
    void fold_ragdoll_contacts(unravel::scene& scn, entt::handle self, ragdoll& parts)
    {
        for(const auto& part : parts.parts)
        {
            const b3BodyId body = part.body.body;
            if(!b3Body_IsValid(body) || !b3Body_IsEnabled(body))
            {
                continue;
            }
            const int capacity = b3Body_GetContactCapacity(body);
            if(capacity <= 0)
            {
                continue;
            }
            contact_scratch.resize(static_cast<size_t>(capacity));
            const int count = b3Body_GetContactData(body, contact_scratch.data(), capacity);
            for(int i = 0; i < count; ++i)
            {
                const b3ContactData& data = contact_scratch[static_cast<size_t>(i)];
                const bool self_is_a = B3_ID_EQUALS(b3Shape_GetBody(data.shapeIdA), body);
                auto other = scn.create_handle(
                    contact_entity_from_shape(*scn.registry, self_is_a ? data.shapeIdB : data.shapeIdA));
                if(!other || other == self)
                {
                    continue;
                }
                touch_pair(self,
                           other,
                           manifold_type::collision,
                           [&](std::vector<unravel::manifold_point>& out)
                           {
                               copy_contact_points(data, !self_is_a, out);
                           });
            }
        }
    }

    void process_contacts()
    {
        APP_SCOPE_PERF("Physics/Box3D/Process Contacts");
        auto& ctx = unravel::engine::context();
        auto& scripting = ctx.get_cached<unravel::script_system>();
        auto& ec = ctx.get_cached<unravel::ecs>();
        auto& scn = ec.get_scene();
        auto& registry = *scn.registry;

        const uint32_t stamp = contacts.advance_stamp();

        to_enter.clear();
        to_exit.clear();

        // Nothing in the scene can receive a contact callback, so there is nothing to
        // fold in. Existing pairs still expire below, which drains the graph.
        const bool any_listeners = !registry.storage<unravel::script_component>().empty();

        // Phase 1: fold the current touching set into the graph.
        if(any_listeners)
        {
            registry.view<unravel::script_component, rigidbody>().each(
                [&](auto e, auto&& script, auto&& body)
                {
                    if(!script.has_script_components())
                    {
                        return;
                    }
                    fold_body_contacts(scn, scn.create_handle(e), body);
                });

            registry.view<character_controller>().each(
                [&](auto e, auto&& cc)
                {
                    fold_character_contacts(scn, scn.create_handle(e), cc);
                });

            registry.view<unravel::script_component, ragdoll>().each(
                [&](auto e, auto&& script, auto&& parts)
                {
                    if(script.has_script_components())
                    {
                        fold_ragdoll_contacts(scn, scn.create_handle(e), parts);
                    }
                });
        }

        // Phase 2: whatever was not seen has separated. Walking backwards keeps the
        // swap-and-pop inside contact_graph::erase from skipping an entry.
        const auto& live = contacts.live();
        for(size_t i = live.size(); i != 0; --i)
        {
            const uint32_t id = live[i - 1];
            auto& slot = contacts.get(id);
            if(slot.seen_stamp == stamp)
            {
                continue;
            }

            if(slot.enter_dispatched)
            {
                to_exit.push_back(std::move(slot.payload));
                to_exit.back().event = event_type::exit;
                to_exit.back().reason = unravel::contact_end_reason::separated;
            }

            contacts.erase(id);
        }

        // Phase 3: dispatch. Script code may destroy entities, which frees slots via
        // release_contacts_for, so every queued enter is revalidated first.
        for(const auto& ref : to_enter)
        {
            if(!contacts.is_live(ref.slot, ref.generation))
            {
                continue;
            }

            auto& slot = contacts.get(ref.slot);
            slot.enter_dispatched = true;
            process_manifold(scripting, slot.payload);
        }

        for(const auto& cm : to_exit)
        {
            process_manifold(scripting, cm);
        }
    }

    // --- queries ---------------------------------------------------------------------

    struct cast_hit
    {
        b3ShapeId shape{};
        math::vec3 point{};
        math::vec3 normal{};
        float fraction{1.0f};
    };

    struct closest_cast_context
    {
        bool query_sensors{};
        bool has_hit{};
        cast_hit hit{};
    };

    struct all_casts_context
    {
        bool query_sensors{};
        unravel::physics_vector<cast_hit> hits;
    };

    static auto closest_cast_callback(b3ShapeId shape,
                                      b3Pos point,
                                      b3Vec3 normal,
                                      float fraction,
                                      uint64_t /*material*/,
                                      int /*triangle*/,
                                      int /*child*/,
                                      void* context) -> float
    {
        auto& ctx = *static_cast<closest_cast_context*>(context);
        if(!ctx.query_sensors && b3Shape_IsSensor(shape))
        {
            return -1.0f;
        }
        ctx.has_hit = true;
        ctx.hit.shape = shape;
        ctx.hit.point = from_b3_pos(point);
        ctx.hit.normal = from_b3(normal);
        ctx.hit.fraction = fraction;
        return fraction;
    }

    static auto all_casts_callback(b3ShapeId shape,
                                   b3Pos point,
                                   b3Vec3 normal,
                                   float fraction,
                                   uint64_t /*material*/,
                                   int /*triangle*/,
                                   int /*child*/,
                                   void* context) -> float
    {
        auto& ctx = *static_cast<all_casts_context*>(context);
        if(!ctx.query_sensors && b3Shape_IsSensor(shape))
        {
            return -1.0f;
        }
        auto& hit = ctx.hits.emplace_back();
        hit.shape = shape;
        hit.point = from_b3_pos(point);
        hit.normal = from_b3(normal);
        hit.fraction = fraction;
        return 1.0f;
    }

    /**
     * @brief What every cast reports: the contact point, the surface normal, and how far the cast travelled along
     *        @p direction (unit length) before it touched. A shape that starts inside another gets no normal from
     *        Box3D; it reports the way back against the cast.
     */
    static auto make_hit(const cast_hit& cast, entt::entity entity, const math::vec3& direction, float max_distance)
        -> unravel::raycast_hit
    {
        unravel::raycast_hit hit;
        hit.entity = entity;
        hit.point = cast.point;
        const float normal_length = math::length(cast.normal);
        hit.normal = normal_length > 0.0f ? cast.normal / normal_length : -direction;
        hit.distance = cast.fraction * max_distance;
        return hit;
    }

    static auto make_closest_hit(const closest_cast_context& ctx, const math::vec3& direction, float max_distance)
        -> hpp::optional<unravel::raycast_hit>
    {
        const entt::entity entity = ctx.has_hit ? entity_from_shape(ctx.hit.shape) : entt::null;
        if(entity == entt::null)
        {
            return {};
        }
        return make_hit(ctx.hit, entity, direction, max_distance);
    }

    /// The hits of a cast that reports all of them, nearest first.
    static auto make_sorted_hits(all_casts_context& ctx, const math::vec3& direction, float max_distance)
        -> unravel::physics_vector<unravel::raycast_hit>
    {
        std::sort(ctx.hits.begin(),
                  ctx.hits.end(),
                  [](const cast_hit& a, const cast_hit& b)
                  {
                      return a.fraction < b.fraction;
                  });
        unravel::physics_vector<unravel::raycast_hit> hits;
        hits.reserve(ctx.hits.size());
        for(const auto& cast : ctx.hits)
        {
            const entt::entity entity = entity_from_shape(cast.shape);
            if(entity != entt::null)
            {
                hits.push_back(make_hit(cast, entity, direction, max_distance));
            }
        }
        return hits;
    }

    auto ray_cast_closest(const math::vec3& origin,
                          const math::vec3& direction,
                          float max_distance,
                          int layer_mask,
                          bool query_sensors) -> hpp::optional<unravel::raycast_hit>
    {
        if(!is_valid())
        {
            return {};
        }

        closest_cast_context ctx;
        ctx.query_sensors = query_sensors;
        const math::vec3 translation = direction * max_distance;
        b3World_CastRay(id,
                        to_b3_pos(origin),
                        to_b3(translation),
                        make_query_filter(layer_mask),
                        &closest_cast_callback,
                        &ctx);
        return make_closest_hit(ctx, direction, max_distance);
    }

    auto ray_cast_all(const math::vec3& origin,
                      const math::vec3& direction,
                      float max_distance,
                      int layer_mask,
                      bool query_sensors) -> unravel::physics_vector<unravel::raycast_hit>
    {
        if(!is_valid())
        {
            return {};
        }

        all_casts_context ctx;
        ctx.query_sensors = query_sensors;
        const math::vec3 translation = direction * max_distance;
        b3World_CastRay(id,
                        to_b3_pos(origin),
                        to_b3(translation),
                        make_query_filter(layer_mask),
                        &all_casts_callback,
                        &ctx);
        return make_sorted_hits(ctx, direction, max_distance);
    }

    auto sphere_cast_closest(const math::vec3& origin,
                             const math::vec3& direction,
                             float radius,
                             float max_distance,
                             int layer_mask,
                             bool query_sensors) -> hpp::optional<unravel::raycast_hit>
    {
        if(!is_valid())
        {
            return {};
        }

        closest_cast_context ctx;
        ctx.query_sensors = query_sensors;
        const b3Vec3 center = b3Vec3_zero;
        const b3ShapeProxy proxy{&center, 1, radius};
        const math::vec3 translation = direction * max_distance;
        b3World_CastShape(id,
                          to_b3_pos(origin),
                          &proxy,
                          to_b3(translation),
                          make_query_filter(layer_mask),
                          &closest_cast_callback,
                          &ctx);
        return make_closest_hit(ctx, direction, max_distance);
    }

    auto sphere_cast_all(const math::vec3& origin,
                         const math::vec3& direction,
                         float radius,
                         float max_distance,
                         int layer_mask,
                         bool query_sensors) -> unravel::physics_vector<unravel::raycast_hit>
    {
        if(!is_valid())
        {
            return {};
        }

        all_casts_context ctx;
        ctx.query_sensors = query_sensors;
        const b3Vec3 center = b3Vec3_zero;
        const b3ShapeProxy proxy{&center, 1, radius};
        const math::vec3 translation = direction * max_distance;
        b3World_CastShape(id,
                          to_b3_pos(origin),
                          &proxy,
                          to_b3(translation),
                          make_query_filter(layer_mask),
                          &all_casts_callback,
                          &ctx);
        return make_sorted_hits(ctx, direction, max_distance);
    }

    struct overlap_context
    {
        bool query_sensors{};
        unravel::physics_vector<entt::entity> hits;
    };

    static auto overlap_callback(b3ShapeId shape, void* context) -> bool
    {
        auto& ctx = *static_cast<overlap_context*>(context);
        if(!ctx.query_sensors && b3Shape_IsSensor(shape))
        {
            return true;
        }
        const entt::entity entity = entity_from_shape(shape);
        if(entity == entt::null)
        {
            return true;
        }
        // A body with several shapes overlaps several times; report the entity once.
        if(std::find(ctx.hits.begin(), ctx.hits.end(), entity) == ctx.hits.end())
        {
            ctx.hits.push_back(entity);
        }
        return true;
    }

    auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
        -> unravel::physics_vector<entt::entity>
    {
        if(!is_valid())
        {
            return {};
        }

        overlap_context ctx;
        ctx.query_sensors = query_sensors;
        const b3Vec3 center = b3Vec3_zero;
        const b3ShapeProxy proxy{&center, 1, radius};
        b3World_OverlapShape(id, to_b3_pos(origin), &proxy, make_query_filter(layer_mask), &overlap_callback, &ctx);
        return std::move(ctx.hits);
    }
};

auto create_world() -> world
{
    world result{};

    b3WorldDef def = b3DefaultWorldDef();
    def.gravity = box3d::to_b3(unravel::get_default_gravity());
    def.enableSleep = true;
    def.enableContinuous = true;
    def.frictionCallback = &combined_friction_callback;
    def.restitutionCallback = &combined_restitution_callback;

    // Box3D spins up its own scheduler when no task callbacks are provided. One worker is the calling thread
    // alone: the scheduler starts no threads, as the single-threaded mode needs.
    def.workerCount = static_cast<uint32_t>(std::clamp(unravel::get_physics_worker_count(), 1, B3_MAX_WORKERS));

    result.id = b3CreateWorld(&def);
    if(b3World_IsValid(result.id))
    {
        b3BodyDef ground = b3DefaultBodyDef();
        ground.type = b3_staticBody;
        result.ground = b3CreateBody(result.id, &ground);
    }
    return result;
}

void destroy_world(world& w)
{
    if(w.is_valid())
    {
        b3DestroyWorld(w.id);
    }
    w.id = {};
}

// --- debug draw ----------------------------------------------------------------------

void debug_draw_segment(b3Pos p1, b3Pos p2, b3HexColor color, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    dd.encoder.setColor(to_bx_color(color));
    dd.encoder.moveTo(to_bx(from_b3_pos(p1)));
    dd.encoder.lineTo(to_bx(from_b3_pos(p2)));
}

void debug_draw_point(b3Pos p, float /*size*/, b3HexColor color, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    const math::vec3 c = from_b3_pos(p);
    dd.encoder.setColor(to_bx_color(color));
    dd.encoder.moveTo(to_bx(c - math::vec3{debug_point_half_size, 0.0f, 0.0f}));
    dd.encoder.lineTo(to_bx(c + math::vec3{debug_point_half_size, 0.0f, 0.0f}));
    dd.encoder.moveTo(to_bx(c - math::vec3{0.0f, debug_point_half_size, 0.0f}));
    dd.encoder.lineTo(to_bx(c + math::vec3{0.0f, debug_point_half_size, 0.0f}));
    dd.encoder.moveTo(to_bx(c - math::vec3{0.0f, 0.0f, debug_point_half_size}));
    dd.encoder.lineTo(to_bx(c + math::vec3{0.0f, 0.0f, debug_point_half_size}));
}

void debug_draw_transform(b3WorldTransform transform, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    const math::vec3 p = from_b3_pos(transform.p);
    const math::quat q = from_b3(transform.q);
    const math::vec3 axes[3] = {q * math::vec3{1.0f, 0.0f, 0.0f},
                                q * math::vec3{0.0f, 1.0f, 0.0f},
                                q * math::vec3{0.0f, 0.0f, 1.0f}};
    const uint32_t colors[3] = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u};
    for(int i = 0; i < 3; ++i)
    {
        dd.encoder.setColor(colors[i]);
        dd.encoder.moveTo(to_bx(p));
        dd.encoder.lineTo(to_bx(p + axes[i] * debug_axis_length));
    }
}

void debug_draw_sphere(b3Pos p, float radius, b3HexColor color, float /*alpha*/, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    dd.encoder.setColor(to_bx_color(color));
    bx::Sphere sphere;
    sphere.center = to_bx(from_b3_pos(p));
    sphere.radius = radius;
    dd.encoder.draw(sphere);
}

void debug_draw_capsule(b3Pos p1, b3Pos p2, float radius, b3HexColor color, float /*alpha*/, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    dd.encoder.setColor(to_bx_color(color));
    dd.encoder.drawCapsule(to_bx(from_b3_pos(p1)), to_bx(from_b3_pos(p2)), radius);
}

void debug_draw_bounds(b3AABB aabb, b3HexColor color, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    dd.encoder.setColor(to_bx_color(color));
    dd.encoder.draw(bx::Aabb{to_bx(from_b3(aabb.lowerBound)), to_bx(from_b3(aabb.upperBound))});
}

void debug_draw_box(b3Vec3 extents, b3WorldTransform transform, b3HexColor color, void* context)
{
    auto& dd = *static_cast<gfx::dd_raii*>(context);
    dd.encoder.setColor(to_bx_color(color));
    math::transform local;
    local.set_position(from_b3_pos(transform.p));
    local.set_rotation(from_b3(transform.q));
    // The debug box spans -1..1: Box3D's extents are half widths already.
    local.set_scale(from_b3(extents));
    bx::Obb obb;
    std::copy_n(static_cast<const float*>(local), 16, obb.mtx);
    dd.encoder.draw(obb);
}

void debug_draw_string(b3Pos /*p*/, const char* /*s*/, b3HexColor /*color*/, void* /*context*/)
{
}

auto make_debug_draw(gfx::dd_raii& dd) -> b3DebugDraw
{
    b3DebugDraw draw = b3DefaultDebugDraw();
    draw.DrawSegmentFcn = &debug_draw_segment;
    draw.DrawPointFcn = &debug_draw_point;
    draw.DrawTransformFcn = &debug_draw_transform;
    draw.DrawSphereFcn = &debug_draw_sphere;
    draw.DrawCapsuleFcn = &debug_draw_capsule;
    draw.DrawBoundsFcn = &debug_draw_bounds;
    draw.DrawBoxFcn = &debug_draw_box;
    draw.DrawStringFcn = &debug_draw_string;
    // Shapes are drawn by the editor gizmos from the components; the world draw only
    // adds what the solver knows: the joints as they hold now.
    draw.drawShapes = false;
    draw.drawJoints = true;
    draw.drawContacts = false;
    draw.drawContactNormals = false;
    draw.drawSleep = false;
    draw.context = &dd;
    return draw;
}

} // namespace
} // namespace box3d

// Free helpers live in the backend namespace. Outside Debug the engine is a unity build:
// anonymous namespaces merge across the concatenated sources, so an unravel-level helper
// here would collide with its twin in the other backend.
namespace box3d
{
namespace
{
using namespace unravel;

const uint8_t system_id = dirty_ids::physics;

auto find_box3d_world() -> box3d::world*
{
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    return registry.ctx().find<box3d::world>();
}

auto get_layer_bits(entt::handle entity) -> uint64_t
{
    if(const auto* layer = entity.try_get<layer_component>())
    {
        return box3d::to_filter_bits(layer->layers.mask);
    }
    return box3d::to_filter_bits(layer_reserved::default_layer);
}

void wake_up(box3d::rigidbody& body)
{
    if(b3Body_IsValid(body.body))
    {
        b3Body_SetAwake(body.body, true);
    }
}

auto max_abs_component(const math::vec3& v) -> float
{
    return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}

/// Box3D rejects mesh scale components that are very small; keep the sign, clamp the
/// magnitude.
auto safe_mesh_scale(const math::vec3& s) -> b3Vec3
{
    const auto fix = [](float v)
    {
        const float magnitude = std::max(std::abs(v), B3_MIN_SCALE);
        return v < 0.0f ? -magnitude : magnitude;
    };
    return {fix(s.x), fix(s.y), fix(s.z)};
}

// --- collision mesh extraction --------------------------------------------------------

struct submesh_geometry
{
    std::vector<b3Vec3> points;
    std::vector<int32_t> indices;
    bool clockwise{false};
};

/**
 * @brief Decides the triangle winding of a submesh from its vertex normals.
 *
 * Box3D meshes are one sided, so a wrongly wound mesh lets bodies fall through. The
 * geometric normal of a sampled triangle is compared against the authored vertex
 * normals; a mirroring node transform flips the answer.
 */
auto detect_clockwise_winding(const bgfx::VertexLayout& layout,
                              const uint8_t* vertex_data,
                              const uint32_t* index_data,
                              uint32_t face_begin,
                              uint32_t face_end,
                              bool mirrored) -> bool
{
    if(!layout.has(bgfx::Attrib::Normal))
    {
        return mirrored;
    }

    int clockwise_votes = 0;
    int counter_clockwise_votes = 0;
    const uint32_t face_count = face_end - face_begin;
    const uint32_t stride = std::max(1u, face_count / static_cast<uint32_t>(box3d::winding_sample_triangles));
    for(uint32_t f = face_begin; f < face_end; f += stride)
    {
        const uint32_t i0 = index_data[f * 3 + 0];
        const uint32_t i1 = index_data[f * 3 + 1];
        const uint32_t i2 = index_data[f * 3 + 2];

        float p0[4];
        float p1[4];
        float p2[4];
        bgfx::vertexUnpack(p0, bgfx::Attrib::Position, layout, vertex_data, i0);
        bgfx::vertexUnpack(p1, bgfx::Attrib::Position, layout, vertex_data, i1);
        bgfx::vertexUnpack(p2, bgfx::Attrib::Position, layout, vertex_data, i2);

        float n0[4];
        float n1[4];
        float n2[4];
        bgfx::vertexUnpack(n0, bgfx::Attrib::Normal, layout, vertex_data, i0);
        bgfx::vertexUnpack(n1, bgfx::Attrib::Normal, layout, vertex_data, i1);
        bgfx::vertexUnpack(n2, bgfx::Attrib::Normal, layout, vertex_data, i2);

        const math::vec3 v0{p0[0], p0[1], p0[2]};
        const math::vec3 v1{p1[0], p1[1], p1[2]};
        const math::vec3 v2{p2[0], p2[1], p2[2]};
        const math::vec3 geometric = math::cross(v1 - v0, v2 - v0);
        const math::vec3 authored{n0[0] + n1[0] + n2[0], n0[1] + n1[1] + n2[1], n0[2] + n1[2] + n2[2]};
        const float agreement = math::dot(geometric, authored);
        if(agreement < 0.0f)
        {
            ++clockwise_votes;
        }
        else if(agreement > 0.0f)
        {
            ++counter_clockwise_votes;
        }
    }

    const bool clockwise = clockwise_votes > counter_clockwise_votes;
    return mirrored ? !clockwise : clockwise;
}

/**
 * @brief Extracts one submesh as an indexed triangle list in body-local space.
 *
 * Vertices are stored in node-local space; the submesh node transform (relative to the
 * model root) and the shape center are baked in, mirroring what the renderer applies.
 */
template<typename Visitor>
void for_each_submesh_geometry(const physics_mesh_shape& shape, Visitor&& visitor)
{
    const auto mesh_ref = shape.mesh_asset.get();
    if(!mesh_ref)
    {
        return;
    }

    const uint8_t* vertex_data = mesh_ref->get_system_vb();
    const uint32_t* index_data = mesh_ref->get_system_ib();
    const uint32_t vertex_count = mesh_ref->get_vertex_count();
    const uint32_t face_count = mesh_ref->get_face_count();
    const auto& layout = mesh_ref->get_vertex_format();

    if(!vertex_data || !index_data || vertex_count == 0 || face_count == 0)
    {
        return;
    }
    if(layout.getOffset(bgfx::Attrib::Position) == UINT16_MAX)
    {
        return;
    }

    const auto& submeshes = mesh_ref->get_submeshes();
    const auto node_transforms = mesh_ref->get_submesh_node_transforms();

    std::vector<int32_t> remap;
    submesh_geometry geometry;

    for(size_t s = 0; s < submeshes.size(); ++s)
    {
        const auto* submesh = submeshes[s];
        if(!submesh || submesh->face_start < 0 || submesh->face_count == 0)
        {
            continue;
        }
        const auto face_begin = static_cast<uint32_t>(submesh->face_start);
        if(face_begin >= face_count)
        {
            continue;
        }
        const uint32_t face_end = std::min(face_begin + submesh->face_count, face_count);

        math::transform node;
        if(s < node_transforms.size())
        {
            node = node_transforms[s];
        }
        const math::vec3 node_scale = node.get_scale();
        const bool mirrored = (node_scale.x * node_scale.y * node_scale.z) < 0.0f;

        remap.assign(vertex_count, -1);
        geometry.points.clear();
        geometry.indices.clear();
        geometry.indices.reserve(static_cast<size_t>(face_end - face_begin) * 3);

        for(uint32_t f = face_begin; f < face_end; ++f)
        {
            for(uint32_t k = 0; k < 3; ++k)
            {
                const uint32_t global_index = index_data[f * 3 + k];
                if(global_index >= vertex_count)
                {
                    geometry.indices.push_back(0);
                    continue;
                }
                int32_t local_index = remap[global_index];
                if(local_index < 0)
                {
                    float position[4];
                    bgfx::vertexUnpack(position, bgfx::Attrib::Position, layout, vertex_data, global_index);
                    const math::vec3 local{position[0], position[1], position[2]};
                    const math::vec3 baked = node.transform_coord(local) + shape.center;
                    local_index = static_cast<int32_t>(geometry.points.size());
                    geometry.points.push_back(box3d::to_b3(baked));
                    remap[global_index] = local_index;
                }
                geometry.indices.push_back(local_index);
            }
        }

        if(geometry.points.size() < 3 || geometry.indices.size() < 3)
        {
            continue;
        }

        geometry.clockwise = detect_clockwise_winding(layout, vertex_data, index_data, face_begin, face_end, mirrored);
        visitor(geometry);
    }
}

// --- shape creation -----------------------------------------------------------------------

struct shape_build_context
{
    box3d::world& world;
    box3d::rigidbody& body;
    b3ShapeDef def;
    math::vec3 scale;
};

void add_shape(shape_build_context& ctx, b3ShapeId shape)
{
    if(B3_IS_NON_NULL(shape))
    {
        ctx.body.shapes.push_back(shape);
    }
}

/// Corner @p index (0..7) of a box with @p half widths: each bit of the index picks the far side of one axis.
auto get_box_corner(const math::vec3& half, int index) -> math::vec3
{
    return {(index & 1) ? half.x : -half.x, (index & 2) ? half.y : -half.y, (index & 4) ? half.z : -half.z};
}

/**
 * @brief Adds the hull of @p points (in the shape's frame), placed by @p local and then scaled in the body's space.
 *
 * Box3D scales a box and a transformed hull along the shape's own axes, before turning it, which keeps a box a box.
 * The entity's scale applies after the shape's turn, so a turned shape on a body scaled differently along its axes is
 * sheared; this hull is, too, and matches what the transform shows.
 */
void add_hull_in_body_space(shape_build_context& ctx, const b3Vec3* points, int count, const b3Transform& local)
{
    const math::quat rotation = box3d::from_b3(local.q);
    const math::vec3 offset = box3d::from_b3(local.p);
    std::vector<b3Vec3> placed(static_cast<size_t>(count));
    std::transform(points,
                   points + count,
                   placed.begin(),
                   [&](const b3Vec3& point)
                   {
                       return box3d::to_b3((rotation * box3d::from_b3(point) + offset) * ctx.scale);
                   });
    b3HullData* hull = b3CreateHull(placed.data(), count, B3_MAX_HULL_VERTICES);
    if(hull == nullptr)
    {
        return;
    }
    add_shape(ctx, b3CreateHullShape(ctx.body.body, &ctx.def, hull));
    b3DestroyHull(hull);
}

/// A turned shape on a body scaled differently along its axes is sheared in the body's space, as it is drawn.
auto is_sheared_by(const math::vec3& rotation_degrees, const math::vec3& scale) -> bool
{
    const math::vec3 size = math::abs(scale);
    const bool is_uniform = size.x == size.y && size.y == size.z;
    return rotation_degrees != math::vec3{0.0f} && !is_uniform;
}

void create_box_shape(shape_build_context& ctx, const physics_box_shape& shape)
{
    const math::vec3 half_widths = math::max(shape.extends * 0.5f, math::vec3{box3d::min_half_extent});
    b3Transform local = box3d::identity_transform();
    local.p = box3d::to_b3(shape.center);
    local.q = box3d::to_b3(unravel::make_shape_rotation(shape.rotation));
    if(!is_sheared_by(shape.rotation, ctx.scale))
    {
        const b3BoxHull hull = b3MakeScaledBoxHull(box3d::to_b3(half_widths), local, box3d::to_b3(ctx.scale));
        add_shape(ctx, b3CreateHullShape(ctx.body.body, &ctx.def, &hull.base));
        return;
    }
    std::array<b3Vec3, box_corner_count> corners{};
    for(int i = 0; i < box_corner_count; ++i)
    {
        corners[size_t(i)] = box3d::to_b3(get_box_corner(half_widths, i));
    }
    add_hull_in_body_space(ctx, corners.data(), box_corner_count, local);
}

void create_sphere_shape(shape_build_context& ctx, const physics_sphere_shape& shape)
{
    b3Sphere sphere;
    sphere.center = box3d::to_b3(shape.center * ctx.scale);
    sphere.radius = std::max(shape.radius * max_abs_component(ctx.scale), box3d::min_half_extent);
    add_shape(ctx, b3CreateSphereShape(ctx.body.body, &ctx.def, &sphere));
}

/// How much a scale stretches the radius of a capsule or cylinder around @p axis: the larger stretch across it.
auto radial_scale(const math::vec3& axis, const math::vec3& scale) -> float
{
    const math::vec3 helper = std::abs(axis.y) < box3d::helper_axis_max_cosine ? math::vec3{0.0f, 1.0f, 0.0f}
                                                                                 : math::vec3{1.0f, 0.0f, 0.0f};
    const math::vec3 u = math::normalize(math::cross(axis, helper));
    const math::vec3 v = math::cross(axis, u);
    return std::max(math::length(u * scale), math::length(v * scale));
}

void create_capsule_shape(shape_build_context& ctx, const physics_capsule_shape& shape)
{
    const math::vec3 axis = unravel::make_shape_rotation(shape.rotation) * math::vec3{0.0f, 1.0f, 0.0f};
    const math::vec3 c1 = (shape.center - axis * (shape.length * 0.5f)) * ctx.scale;
    const math::vec3 c2 = (shape.center + axis * (shape.length * 0.5f)) * ctx.scale;
    const float radius = std::max(shape.radius * radial_scale(axis, ctx.scale), box3d::min_half_extent);
    if(math::distance(c1, c2) < B3_MIN_CAPSULE_LENGTH)
    {
        // Very short capsules are numerically unstable; Box3D asks for a sphere instead.
        b3Sphere sphere;
        sphere.center = box3d::to_b3((c1 + c2) * 0.5f);
        sphere.radius = radius;
        add_shape(ctx, b3CreateSphereShape(ctx.body.body, &ctx.def, &sphere));
        return;
    }
    b3Capsule capsule;
    capsule.center1 = box3d::to_b3(c1);
    capsule.center2 = box3d::to_b3(c2);
    capsule.radius = radius;
    add_shape(ctx, b3CreateCapsuleShape(ctx.body.body, &ctx.def, &capsule));
}

void create_cylinder_shape(shape_build_context& ctx, const physics_cylinder_shape& shape)
{
    const float length = std::max(shape.length, 2.0f * box3d::min_half_extent);
    const float radius = std::max(shape.radius, box3d::min_half_extent);
    b3HullData* hull = b3CreateCylinder(length, radius, -length * 0.5f, box3d::cylinder_hull_sides);
    if(hull == nullptr)
    {
        return;
    }
    b3Transform local = box3d::identity_transform();
    local.p = box3d::to_b3(shape.center);
    local.q = box3d::to_b3(unravel::make_shape_rotation(shape.rotation));
    if(is_sheared_by(shape.rotation, ctx.scale))
    {
        add_hull_in_body_space(ctx, b3GetHullPoints(hull), hull->vertexCount, local);
    }
    else
    {
        // The transformed hull is cloned into the world hull database; the source is ours.
        add_shape(ctx, b3CreateTransformedHullShape(ctx.body.body, &ctx.def, hull, local, box3d::to_b3(ctx.scale)));
    }
    b3DestroyHull(hull);
}

void create_convex_mesh_shapes(shape_build_context& ctx, const physics_mesh_shape& shape)
{
    for_each_submesh_geometry(shape,
                              [&](const submesh_geometry& geometry)
                              {
                                  const int point_count = static_cast<int>(geometry.points.size());
                                  b3HullData* hull =
                                      b3CreateHull(geometry.points.data(), point_count, B3_MAX_HULL_VERTICES);
                                  if(hull == nullptr)
                                  {
                                      return;
                                  }
                                  add_shape(ctx,
                                            b3CreateTransformedHullShape(ctx.body.body,
                                                                         &ctx.def,
                                                                         hull,
                                                                         box3d::identity_transform(),
                                                                         box3d::to_b3(ctx.scale)));
                                  b3DestroyHull(hull);
                              });
}

void create_concave_mesh_shapes(shape_build_context& ctx, const physics_mesh_shape& shape)
{
    for_each_submesh_geometry(shape,
                              [&](submesh_geometry& geometry)
                              {
                                  b3MeshDef def{};
                                  def.vertices = geometry.points.data();
                                  def.stride = 0;
                                  def.indices = geometry.indices.data();
                                  def.vertexCount = static_cast<int>(geometry.points.size());
                                  def.triangleCount = static_cast<int>(geometry.indices.size() / 3);
                                  def.identifyEdges = true;
                                  def.clockWiseWinding = geometry.clockwise;
                                  b3MeshData* mesh = b3CreateMesh(&def, nullptr, 0);
                                  if(mesh == nullptr)
                                  {
                                      return;
                                  }
                                  const b3ShapeId id =
                                      b3CreateMeshShape(ctx.body.body, &ctx.def, mesh, safe_mesh_scale(ctx.scale));
                                  if(B3_IS_NULL(id))
                                  {
                                      b3DestroyMesh(mesh);
                                      return;
                                  }
                                  ctx.body.shapes.push_back(id);
                                  ctx.body.meshes.push_back(mesh);
                              });
}

void create_mesh_shapes(shape_build_context& ctx, const physics_mesh_shape& shape, rigidbody_type body_type)
{
    // The build waits for a mesh still loading: a body without its collider lets whatever rests on it fall through.
    if(!shape.mesh_asset)
    {
        return;
    }
    if(shape.collision_type == mesh_collision_type::convex)
    {
        create_convex_mesh_shapes(ctx, shape);
        return;
    }
    if(body_type != rigidbody_type::static_body)
    {
        // Same restriction as a BVH triangle mesh in Bullet, but Box3D is silent about it.
        APPLOG_WARNING("box3d: concave mesh collision only produces contacts on static bodies");
    }
    create_concave_mesh_shapes(ctx, shape);
}

auto resolve_material(physics_component& comp) -> physics_material
{
    if(const auto mat = comp.get_material().get())
    {
        return *mat;
    }
    return physics_material{};
}

auto make_surface_material(const physics_material& mat, const math::vec3& surface_velocity = math::vec3{0.0f})
    -> b3SurfaceMaterial
{
    b3SurfaceMaterial surface = b3DefaultSurfaceMaterial();
    surface.friction = mat.friction;
    surface.restitution = mat.restitution;
    surface.rollingResistance = mat.rolling_resistance;
    surface.tangentVelocity = box3d::to_b3(surface_velocity);
    surface.userMaterialId = box3d::encode_combine_modes(mat.friction_combine, mat.restitution_combine);
    return surface;
}

void destroy_shapes(box3d::rigidbody& body)
{
    for(const b3ShapeId shape : body.shapes)
    {
        if(b3Shape_IsValid(shape))
        {
            b3DestroyShape(shape, false);
        }
    }
    body.shapes.clear();
    for(b3MeshData* mesh : body.meshes)
    {
        b3DestroyMesh(mesh);
    }
    body.meshes.clear();
}

/**
 * @brief Gives a body the authored mass (1 kg when it is not positive).
 *
 * Box3D derives mass from shape density. The authored mass is kept by scaling the
 * computed inertia by the same ratio, so the body keeps its shape-derived inertia
 * distribution. A dynamic body must never end up massless: gravity and impulses would
 * silently become no-ops, so shapes that give no mass (meshes) get a unit sphere's inertia.
 */
void apply_body_mass(b3BodyId body, float mass)
{
    b3Body_ApplyMassFromShapes(body);
    mass = mass > 0.0f ? mass : 1.0f;
    b3MassData data = b3Body_GetMassData(body);
    if(data.mass > 0.0f)
    {
        const float ratio = mass / data.mass;
        data.inertia = b3MulSM(ratio, data.inertia);
    }
    else
    {
        data.center = b3Vec3_zero;
        const float inertia = box3d::fallback_unit_inertia * mass;
        data.inertia = {{inertia, 0.0f, 0.0f}, {0.0f, inertia, 0.0f}, {0.0f, 0.0f, inertia}};
    }
    data.mass = mass;
    b3Body_SetMassData(body, data);
}

void update_rigidbody_mass_and_inertia(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    if(comp.get_body_type() == rigidbody_type::dynamic)
    {
        apply_body_mass(body.body, comp.get_mass());
        return;
    }
    b3Body_ApplyMassFromShapes(body.body);
}

/**
 * @brief Rebuilds every shape from the component, baking scale and the sensor flag.
 *
 * Scale, sensor-ness and geometry are all creation-time properties in Box3D, so any of
 * them changing costs a rebuild. Mass is re-derived afterwards.
 */
/// Adds a box, sphere, capsule or cylinder; mesh shapes need the body type and are made by create_mesh_shapes.
void add_convex_shape(shape_build_context& ctx, const physics_compound_shape& shape)
{
    if(const auto* box = hpp::get_if<physics_box_shape>(&shape.shape))
    {
        create_box_shape(ctx, *box);
    }
    else if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        create_sphere_shape(ctx, *sphere);
    }
    else if(const auto* capsule = hpp::get_if<physics_capsule_shape>(&shape.shape))
    {
        create_capsule_shape(ctx, *capsule);
    }
    else if(const auto* cylinder = hpp::get_if<physics_cylinder_shape>(&shape.shape))
    {
        create_cylinder_shape(ctx, *cylinder);
    }
}

void rebuild_shapes(box3d::world& world, box3d::rigidbody& body, physics_component& comp)
{
    destroy_shapes(body);
    if(!b3Body_IsValid(body.body))
    {
        return;
    }

    auto owner = comp.get_owner();
    const physics_material mat = resolve_material(comp);
    const b3SurfaceMaterial surface = make_surface_material(mat, comp.get_surface_velocity());

    body.category_bits = get_layer_bits(owner);
    body.mask_bits = box3d::to_filter_bits(comp.get_collision_mask().mask);
    body.shapes_are_sensor = comp.is_sensor();
    body.applied_friction = surface.friction;
    body.applied_restitution = surface.restitution;
    body.applied_rolling_resistance = surface.rollingResistance;
    body.applied_surface_velocity = comp.get_surface_velocity();
    body.applied_material_id = surface.userMaterialId;

    shape_build_context ctx{world, body, b3DefaultShapeDef(), body.shape_scale};
    ctx.def.userData = box3d::encode_entity(owner.entity());
    ctx.def.baseMaterial = surface;
    ctx.def.filter = box3d::make_filter(body.category_bits, body.mask_bits);
    ctx.def.isSensor = comp.is_sensor();
    // Needed on sensors so they scan, and on everything else so sensors can see it.
    ctx.def.enableSensorEvents = true;
    // Contacts are folded from the touching set each step; the event buffers are unused.
    ctx.def.enableContactEvents = false;
    ctx.def.enableHitEvents = false;
    ctx.def.updateBodyMass = false;

    for(const auto& s : comp.get_shapes())
    {
        if(const auto* mesh = hpp::get_if<physics_mesh_shape>(&s.shape))
        {
            create_mesh_shapes(ctx, *mesh, comp.get_body_type());
            continue;
        }
        add_convex_shape(ctx, s);
    }

    update_rigidbody_mass_and_inertia(body, comp);
}

void update_rigidbody_kind(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    const b3BodyType type = box3d::to_b3_type(comp.get_body_type());
    if(b3Body_GetType(body.body) != type)
    {
        // Resets the mass properties; callers re-apply them afterwards.
        b3Body_SetType(body.body, type);
    }
    switch(comp.get_body_type())
    {
        case rigidbody_type::static_body:
            break;
        case rigidbody_type::kinematic:
            // A kinematic body is driven by target transforms every step; letting it
            // sleep would make the target rejection threshold visible.
            b3Body_EnableSleep(body.body, false);
            break;
        case rigidbody_type::dynamic:
            // update_rigidbody_motion sets whether it may sleep.
            b3Body_SetAwake(body.body, true);
            break;
    }
}

void update_rigidbody_constraints(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    const auto freeze_position = comp.get_freeze_position();
    const auto freeze_rotation = comp.get_freeze_rotation();
    b3MotionLocks locks{};
    locks.linearX = freeze_position.x;
    locks.linearY = freeze_position.y;
    locks.linearZ = freeze_position.z;
    locks.angularX = freeze_rotation.x;
    locks.angularY = freeze_rotation.y;
    locks.angularZ = freeze_rotation.z;
    b3Body_SetMotionLocks(body.body, locks);
    // Box3D recomputes the mass from the shape densities whenever the locks turn a fixed
    // rotation on or off, which drops the authored mass: a 1 kg capsule with its rotation
    // frozen would weigh two tons, and an impulse would barely move it.
    update_rigidbody_mass_and_inertia(body, comp);

    // Adjust velocities to respect the locks.
    b3Vec3 linear = b3Body_GetLinearVelocity(body.body);
    linear.x = freeze_position.x ? 0.0f : linear.x;
    linear.y = freeze_position.y ? 0.0f : linear.y;
    linear.z = freeze_position.z ? 0.0f : linear.z;
    b3Body_SetLinearVelocity(body.body, linear);

    b3Vec3 angular = b3Body_GetAngularVelocity(body.body);
    angular.x = freeze_rotation.x ? 0.0f : angular.x;
    angular.y = freeze_rotation.y ? 0.0f : angular.y;
    angular.z = freeze_rotation.z ? 0.0f : angular.z;
    b3Body_SetAngularVelocity(body.body, angular);

    wake_up(body);
}

void update_rigidbody_velocity(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    b3Body_SetLinearVelocity(body.body, box3d::to_b3(comp.get_velocity()));
    wake_up(body);
}

void update_rigidbody_angular_velocity(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    b3Body_SetAngularVelocity(body.body, box3d::to_b3(comp.get_angular_velocity()));
    wake_up(body);
}

void update_rigidbody_collision_layer(box3d::rigidbody& body, physics_component& comp)
{
    const uint64_t category = get_layer_bits(comp.get_owner());
    const uint64_t mask = box3d::to_filter_bits(comp.get_collision_mask().mask);
    if(category == body.category_bits && mask == body.mask_bits)
    {
        return;
    }
    body.category_bits = category;
    body.mask_bits = mask;
    const b3Filter filter = box3d::make_filter(category, mask);
    for(const b3ShapeId shape : body.shapes)
    {
        if(b3Shape_IsValid(shape))
        {
            b3Shape_SetFilter(shape, filter, true);
        }
    }
    wake_up(body);
}

auto get_gravity_scale(const physics_component& comp) -> float
{
    return comp.is_using_gravity() ? comp.get_gravity_scale() : 0.0f;
}

void update_rigidbody_gravity(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    // Do not clear linear velocity here - that would wipe same-frame impulses.
    b3Body_SetGravityScale(body.body, get_gravity_scale(comp));
}

void update_rigidbody_damping(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    b3Body_SetLinearDamping(body.body, comp.get_linear_damping());
    b3Body_SetAngularDamping(body.body, comp.get_angular_damping());
}

void update_rigidbody_motion(box3d::rigidbody& body, physics_component& comp)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    const bool dynamic = comp.get_body_type() == rigidbody_type::dynamic;
    b3Body_SetBullet(body.body, dynamic && comp.is_continuous());
    if(dynamic)
    {
        b3Body_EnableSleep(body.body, comp.can_sleep());
    }
}

/// Contact stiffness and damping are world-wide in Box3D, so only friction,
/// restitution and the combine modes reach the shapes.
void update_rigidbody_material(box3d::rigidbody& body, physics_component& comp)
{
    const physics_material mat = resolve_material(comp);
    const b3SurfaceMaterial surface = make_surface_material(mat, comp.get_surface_velocity());
    const bool unchanged =
        surface.userMaterialId == body.applied_material_id &&
        math::epsilonEqual(surface.friction, body.applied_friction, math::epsilon<float>()) &&
        math::epsilonEqual(surface.restitution, body.applied_restitution, math::epsilon<float>()) &&
        math::epsilonEqual(surface.rollingResistance, body.applied_rolling_resistance, math::epsilon<float>()) &&
        body.applied_surface_velocity == comp.get_surface_velocity();
    if(unchanged)
    {
        return;
    }
    body.applied_friction = surface.friction;
    body.applied_restitution = surface.restitution;
    body.applied_rolling_resistance = surface.rollingResistance;
    body.applied_surface_velocity = comp.get_surface_velocity();
    body.applied_material_id = surface.userMaterialId;
    for(const b3ShapeId shape : body.shapes)
    {
        if(b3Shape_IsValid(shape))
        {
            b3Shape_SetSurfaceMaterial(shape, surface);
        }
    }
}

void set_rigidbody_active(box3d::rigidbody& body, bool enabled)
{
    if(!b3Body_IsValid(body.body))
    {
        return;
    }
    if(b3Body_IsEnabled(body.body) == enabled)
    {
        return;
    }
    if(enabled)
    {
        b3Body_Enable(body.body);
    }
    else
    {
        b3Body_Disable(body.body);
    }
}

void update_rigidbody_full(box3d::world& world, box3d::rigidbody& body, physics_component& comp)
{
    update_rigidbody_kind(body, comp);
    // Rebuilding the shapes applies material, layer and mass.
    rebuild_shapes(world, body, comp);
    update_rigidbody_constraints(body, comp);
    update_rigidbody_gravity(body, comp);
    update_rigidbody_damping(body, comp);
    update_rigidbody_motion(body, comp);
    // Velocity last so explicit velocities / impulses are not overwritten by earlier setup.
    update_rigidbody_velocity(body, comp);
    update_rigidbody_angular_velocity(body, comp);
}

void make_rigidbody(box3d::world& world, entt::handle entity, physics_component& comp)
{
    auto& body = entity.emplace<box3d::rigidbody>();

    b3BodyDef def = b3DefaultBodyDef();
    def.type = box3d::to_b3_type(comp.get_body_type());
    def.userData = box3d::encode_entity(entity.entity());
    def.gravityScale = get_gravity_scale(comp);
    def.linearDamping = comp.get_linear_damping();
    def.angularDamping = comp.get_angular_damping();
    def.isBullet = comp.get_body_type() == rigidbody_type::dynamic && comp.is_continuous();
    def.enableSleep = comp.get_body_type() != rigidbody_type::kinematic && comp.can_sleep();
    def.isAwake = true;
    def.isEnabled = entity.all_of<active_component>();
    def.position = b3ToPos(b3Vec3_zero);
    def.rotation = box3d::identity_quat();

    // Create at the ECS pose so same-frame ApplyForce / queries hit the correct transform.
    if(const auto* transform = entity.try_get<transform_component>())
    {
        def.position = box3d::to_b3_pos(transform->get_position_global());
        def.rotation = box3d::to_b3(transform->get_rotation_global());
        if(comp.is_autoscaled())
        {
            body.shape_scale = transform->get_scale_global();
        }
    }

    body.body = b3CreateBody(world.id, &def);
    if(B3_IS_NULL(body.body))
    {
        APPLOG_ERROR("box3d: failed to create body");
        return;
    }

    update_rigidbody_full(world, body, comp);
}

void release_rigidbody(box3d::rigidbody& body)
{
    if(b3Body_IsValid(body.body))
    {
        // Destroying the body destroys its shapes; the mesh data is ours to free after.
        b3DestroyBody(body.body);
    }
    body.body = {};
    body.shapes.clear();
    for(b3MeshData* mesh : body.meshes)
    {
        b3DestroyMesh(mesh);
    }
    body.meshes.clear();
}

void destroy_physics_body(box3d::world& world, entt::handle entity, bool from_physics_component)
{
    auto* body = entity.try_get<box3d::rigidbody>();

    if(body)
    {
        // See bullet_backend: suppression is raised only for the span of
        // entt::registry::destroy, where the pre-destroy phase already reported these.
        const bool notify = !scene::is_destroy_suppressed();

        world.release_contacts_for(entity, unravel::contact_end_reason::other_disabled, notify);

        release_rigidbody(*body);
    }

    if(from_physics_component)
    {
        entity.remove<box3d::rigidbody>();
    }
}

void sync_physics_body(box3d::world& world, physics_component& comp, bool force = false)
{
    auto owner = comp.get_owner();

    if(force)
    {
        destroy_physics_body(world, owner, true);
        make_rigidbody(world, owner, comp);
    }
    else
    {
        auto& body = owner.get<box3d::rigidbody>();

        if(comp.is_property_dirty(physics_property::kind))
        {
            update_rigidbody_full(world, body, comp);
        }
        else
        {
            bool rebuilt = false;
            const bool sensor_changed =
                comp.is_property_dirty(physics_property::sensor) && body.shapes_are_sensor != comp.is_sensor();
            if(comp.is_property_dirty(physics_property::shape) || sensor_changed)
            {
                rebuild_shapes(world, body, comp);
                rebuilt = true;
            }
            if(comp.is_property_dirty(physics_property::mass) && !rebuilt)
            {
                update_rigidbody_mass_and_inertia(body, comp);
            }

            if(comp.is_property_dirty(physics_property::contact_events))
            {
                world.refresh_contact_policy_for(owner);
            }

            if(comp.is_property_dirty(physics_property::constraints))
            {
                update_rigidbody_constraints(body, comp);
            }
            if(comp.is_property_dirty(physics_property::gravity))
            {
                update_rigidbody_gravity(body, comp);
            }
            if(comp.is_property_dirty(physics_property::damping))
            {
                update_rigidbody_damping(body, comp);
            }
            if(comp.is_property_dirty(physics_property::motion))
            {
                update_rigidbody_motion(body, comp);
            }
            if(comp.is_property_dirty(physics_property::velocity))
            {
                update_rigidbody_velocity(body, comp);
            }
            if(comp.is_property_dirty(physics_property::angular_velocity))
            {
                update_rigidbody_angular_velocity(body, comp);
            }

            // These check internally for a change.
            update_rigidbody_material(body, comp);
            update_rigidbody_collision_layer(body, comp);
        }

        if(comp.get_body_type() == rigidbody_type::dynamic)
        {
            if(comp.are_any_properties_dirty())
            {
                wake_up(body);
            }
        }
    }

    comp.set_dirty(system_id, false);
}

auto make_world_transform(const transform_component& transform) -> b3WorldTransform
{
    b3WorldTransform result;
    result.p = box3d::to_b3_pos(transform.get_position_global());
    result.q = box3d::to_b3(transform.get_rotation_global());
    return result;
}

/// Scale lives inside the shapes, so a scale change is a shape rebuild. Only paid when
/// the entity actually changed scale.
void apply_shape_scale_if_needed(box3d::world& world,
                                 box3d::rigidbody& body,
                                 physics_component& comp,
                                 const transform_component& transform)
{
    if(!comp.is_autoscaled())
    {
        return;
    }
    const math::vec3 scale = transform.get_scale_global();
    if(math::any(math::epsilonNotEqual(scale, body.shape_scale, box3d::scale_epsilon)))
    {
        body.shape_scale = scale;
        rebuild_shapes(world, body, comp);
    }
}

void set_static_transform(box3d::world& world,
                          box3d::rigidbody& body,
                          physics_component& comp,
                          const transform_component& transform)
{
    const b3WorldTransform target = make_world_transform(transform);
    b3Body_SetTransform(body.body, target.p, target.q);
    apply_shape_scale_if_needed(world, body, comp, transform);
}

/**
 * @brief Starts a dynamic body's count towards sleep again, and wakes it.
 *
 * An awake body keeps counting from where it was. A body that rested against a moving kinematic body (the kinematic
 * body kept the island awake) has counted long enough to sleep, so the step after the kinematic body lets it go would
 * put it to sleep where it hangs, before it has gathered speed. A body with sleep off has its count zeroed by the step;
 * restore_sleep turns sleep back on after it.
 */
void restart_sleep_count(world& w, b3BodyId body)
{
    if(!b3Body_IsSleepEnabled(body))
    {
        return;
    }
    b3Body_EnableSleep(body, false);
    w.sleep_restores.push_back(body);
}

/// Turns sleep back on for the bodies restart_sleep_count turned it off for, unless their component says not to sleep.
void restore_sleep(world& w, entt::registry& registry)
{
    for(const b3BodyId body : w.sleep_restores)
    {
        if(!b3Body_IsValid(body))
        {
            continue;
        }
        const entt::entity entity = decode_entity(b3Body_GetUserData(body));
        const auto* comp = registry.valid(entity) ? registry.try_get<physics_component>(entity) : nullptr;
        b3Body_EnableSleep(body, comp == nullptr || comp->can_sleep());
    }
    w.sleep_restores.clear();
}

/// Restarts the sleep count of the dynamic bodies a moving kinematic body touches, as PhysX does for kinematic targets.
void restart_sleep_of_touching_bodies(world& w, b3BodyId body)
{
    const int capacity = b3Body_GetContactCapacity(body);
    if(capacity <= 0)
    {
        return;
    }
    w.contact_scratch.resize(size_t(capacity));
    const int count = b3Body_GetContactData(body, w.contact_scratch.data(), capacity);
    for(int i = 0; i < count; ++i)
    {
        const auto& contact = w.contact_scratch[size_t(i)];
        const b3BodyId body_a = b3Shape_GetBody(contact.shapeIdA);
        const b3BodyId other = B3_ID_EQUALS(body_a, body) ? b3Shape_GetBody(contact.shapeIdB) : body_a;
        if(b3Body_GetType(other) == b3_dynamicBody)
        {
            restart_sleep_count(w, other);
        }
    }
}

/**
 * @brief The sleep restart for kinematic bodies the engine drives on its own (ragdoll hitboxes, character proxies):
 *        only while they move faster than their sleep threshold, so a character standing still beside a crate lets
 *        it sleep.
 */
void restart_sleep_while_moving(world& w, b3BodyId body)
{
    const float threshold = b3Body_GetSleepThreshold(body);
    const bool is_moving = b3Length(b3Body_GetLinearVelocity(body)) > threshold ||
                           b3Length(b3Body_GetAngularVelocity(body)) > threshold;
    if(is_moving)
    {
        restart_sleep_of_touching_bodies(w, body);
    }
}

/**
 * @brief Drives a kinematic body to the ECS pose over the step.
 *
 * Box3D moves kinematic bodies by velocity, so the target transform is what pushes
 * dynamic bodies out of the way. When the ECS pose did not change the velocity has to
 * be cleared explicitly, otherwise the body keeps travelling.
 */
void move_kinematic_transform(box3d::world& world,
                              box3d::rigidbody& body,
                              physics_component& comp,
                              const transform_component& transform,
                              float step_dt)
{
    const b3WorldTransform target = make_world_transform(transform);
    if(step_dt > 0.0f)
    {
        b3Body_SetTargetTransform(body.body, target, step_dt, true);
        restart_sleep_of_touching_bodies(world, body.body);
    }
    else
    {
        b3Body_SetTransform(body.body, target.p, target.q);
        b3Body_SetLinearVelocity(body.body, b3Vec3_zero);
        b3Body_SetAngularVelocity(body.body, b3Vec3_zero);
    }
    apply_shape_scale_if_needed(world, body, comp, transform);
}

void hold_kinematic_transform(box3d::rigidbody& body)
{
    b3Body_SetLinearVelocity(body.body, b3Vec3_zero);
    b3Body_SetAngularVelocity(body.body, b3Vec3_zero);
}

void teleport_dynamic_transform(box3d::world& world,
                                box3d::rigidbody& body,
                                physics_component& comp,
                                const transform_component& transform)
{
    const b3WorldTransform target = make_world_transform(transform);
    b3Body_SetTransform(body.body, target.p, target.q);
    apply_shape_scale_if_needed(world, body, comp, transform);
    wake_up(body);
}

auto sync_transforms_to_physics(box3d::world& world,
                                physics_component& comp,
                                const transform_component& transform,
                                float step_dt) -> bool
{
    auto owner = comp.get_owner();
    auto& body = owner.get<box3d::rigidbody>();

    if(!b3Body_IsValid(body.body))
    {
        return false;
    }

    switch(comp.get_body_type())
    {
        case rigidbody_type::static_body:
            set_static_transform(world, body, comp, transform);
            break;
        case rigidbody_type::kinematic:
            move_kinematic_transform(world, body, comp, transform, step_dt);
            break;
        case rigidbody_type::dynamic:
            teleport_dynamic_transform(world, body, comp, transform);
            break;
    }

    return true;
}

auto sync_transforms_from_physics(physics_component& comp, transform_component& transform) -> bool
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return false;
    }

    auto owner = comp.get_owner();
    auto* body = owner.try_get<box3d::rigidbody>();

    if(!body || !b3Body_IsValid(body->body))
    {
        return false;
    }

    const bool awake = b3Body_IsAwake(body->body);
    comp.set_sleeping_internal(!awake);
    if(!awake)
    {
        comp.set_velocity_internal(math::vec3{0.0f});
        comp.set_angular_velocity_internal(math::vec3{0.0f});
        return false;
    }

    comp.set_velocity_internal(box3d::from_b3(b3Body_GetLinearVelocity(body->body)));
    comp.set_angular_velocity_internal(box3d::from_b3(b3Body_GetAngularVelocity(body->body)));

    const b3WorldTransform pose = b3Body_GetTransform(body->body);
    const math::vec3 p = box3d::from_b3_pos(pose.p);
    const math::quat q = box3d::from_b3(pose.q);

    // An interpolated body is shown between the poses written here, so any rounding would show as a stall.
    const float epsilon = comp.get_interpolate() ? 0.0f : box3d::transform_sync_epsilon;
    return transform.set_position_and_rotation_global(p, q, epsilon);
}

auto to_physics(box3d::world& world, transform_component& transform, physics_component& comp, float step_dt) -> bool
{
    const bool transform_dirty = transform.is_dirty(system_id);
    const bool rigidbody_dirty = comp.is_dirty(system_id);

    sync_physics_body(world, comp);

    // Dynamic bodies are simulation-authored: only push ECS transform on teleport/dirty pose.
    if(comp.get_body_type() == rigidbody_type::dynamic)
    {
        if(transform_dirty)
        {
            return sync_transforms_to_physics(world, comp, transform, step_dt);
        }
        return false;
    }

    // Static / kinematic: ECS is authoritative when transform or body setup changed.
    if(transform_dirty || rigidbody_dirty)
    {
        return sync_transforms_to_physics(world, comp, transform, step_dt);
    }

    if(comp.get_body_type() == rigidbody_type::kinematic)
    {
        if(auto* body = comp.get_owner().try_get<box3d::rigidbody>(); body && b3Body_IsValid(body->body))
        {
            hold_kinematic_transform(*body);
        }
    }

    return false;
}

auto from_physics(transform_component& transform, physics_component& comp) -> bool
{
    const bool result = sync_transforms_from_physics(comp, transform);
    // Resolved before the bit is cleared: the global's dirty flag raises the consumer bits only when it turns on,
    // so a local-space write later in the frame would otherwise reach no consumer and never teleport the body.
    transform.resolve_transform_global();
    transform.set_dirty(system_id, false);
    comp.set_dirty(system_id, false);
    return result;
}

// --- character controller ---------------------------------------------------------------

auto make_mover_capsule(const box3d::character_controller& cc) -> b3Capsule
{
    b3Capsule capsule;
    capsule.center1 = {0.0f, -cc.half_segment, 0.0f};
    capsule.center2 = {0.0f, cc.half_segment, 0.0f};
    capsule.radius = cc.radius;
    return capsule;
}

/// Signed distance between a plane (relative to the mover origin) and the capsule.
auto plane_capsule_separation(const b3Plane& plane, const b3Capsule& capsule) -> float
{
    const float d1 = b3Dot(plane.normal, capsule.center1) - plane.offset;
    const float d2 = b3Dot(plane.normal, capsule.center2) - plane.offset;
    return std::min(d1, d2) - capsule.radius;
}

struct mover_query
{
    box3d::world* world{};
    box3d::character_controller* cc{};
    /// Current capsule origin in world space. The capsule and the planes are relative
    /// to it, which keeps the queries precise far from the world origin.
    math::vec3 origin{};
    b3Capsule capsule{};
    b3QueryFilter filter{};
    b3CollisionPlane planes[box3d::mover_plane_capacity]{};
    box3d::mover_contact contacts[box3d::mover_plane_capacity]{};
    int plane_count{0};
};

/// The mover ignores its own proxy body and sensors; sensors are observed through the
/// proxy shape instead, and must never block movement.
auto mover_accepts_shape(b3ShapeId shape, const mover_query& query) -> bool
{
    if(B3_ID_EQUALS(shape, query.cc->shape))
    {
        return false;
    }
    if(query.cc->own_ragdoll != entt::null &&
       box3d::decode_entity(b3Body_GetUserData(b3Shape_GetBody(shape))) == query.cc->own_ragdoll)
    {
        return false;
    }
    return !b3Shape_IsSensor(shape);
}

auto mover_filter_fcn(b3ShapeId shape, void* context) -> bool
{
    return mover_accepts_shape(shape, *static_cast<mover_query*>(context));
}

auto mover_plane_fcn(b3ShapeId shape, const b3PlaneResult* results, int count, void* context) -> bool
{
    auto& query = *static_cast<mover_query*>(context);
    if(!mover_accepts_shape(shape, query))
    {
        return true;
    }
    const entt::entity entity = box3d::entity_from_shape(shape);
    for(int i = 0; i < count && query.plane_count < box3d::mover_plane_capacity; ++i)
    {
        const b3PlaneResult& result = results[i];
        auto& plane = query.planes[query.plane_count];
        plane.plane = result.plane;
        plane.pushLimit = FLT_MAX;
        plane.push = 0.0f;
        plane.clipVelocity = true;

        auto& contact = query.contacts[query.plane_count];
        contact.entity = entity;
        contact.shape = shape;
        contact.point = query.origin + box3d::from_b3(result.point);
        contact.normal = box3d::from_b3(result.plane.normal);
        contact.separation = plane_capsule_separation(result.plane, query.capsule);
        ++query.plane_count;
    }
    return true;
}

void collect_mover_planes(mover_query& query)
{
    query.plane_count = 0;
    b3World_CollideMover(query.world->id,
                         box3d::to_b3_pos(query.origin),
                         &query.capsule,
                         query.filter,
                         &mover_plane_fcn,
                         &query);
}

/// @return The fraction of @p translation the mover can travel.
auto cast_mover(mover_query& query, const math::vec3& translation) -> float
{
    if(math::dot(translation, translation) <= 0.0f)
    {
        return 1.0f;
    }
    return b3World_CastMover(query.world->id,
                             box3d::to_b3_pos(query.origin),
                             &query.capsule,
                             box3d::to_b3(translation),
                             query.filter,
                             &mover_filter_fcn,
                             &query);
}

/**
 * @brief Moves the mover towards origin + translation, sliding along whatever it hits.
 *
 * The collide / solve / cast loop from the Box3D mover sample: gather the planes at the
 * current position, ask the plane solver for the closest admissible delta, then sweep
 * that delta so nothing is tunnelled.
 */
void solve_mover_translation(mover_query& query, const math::vec3& translation)
{
    const math::vec3 target = query.origin + translation;
    for(int iteration = 0; iteration < box3d::mover_max_iterations; ++iteration)
    {
        collect_mover_planes(query);
        const math::vec3 target_delta = target - query.origin;
        const b3PlaneSolverResult result = b3SolvePlanes(box3d::to_b3(target_delta), query.planes, query.plane_count);
        math::vec3 delta = box3d::from_b3(result.delta);
        const float fraction = cast_mover(query, delta);
        delta *= fraction;
        query.origin += delta;
        if(math::dot(delta, delta) < box3d::mover_tolerance * box3d::mover_tolerance)
        {
            break;
        }
    }
}

/**
 * @brief Lets the character shove dynamic bodies it is pressing against.
 *
 * The character has infinite mass, so this is the one-sided impulse from the Box3D
 * mover sample.
 */
void push_dynamic_bodies(mover_query& query, const math::vec3& character_velocity)
{
    for(int i = 0; i < query.plane_count; ++i)
    {
        const b3ShapeId shape = query.contacts[i].shape;
        if(!b3Shape_IsValid(shape))
        {
            continue;
        }
        const b3BodyId body = b3Shape_GetBody(shape);
        if(b3Body_GetType(body) != b3_dynamicBody)
        {
            continue;
        }

        const b3Vec3 point = box3d::to_b3(query.contacts[i].point);
        const b3Vec3 normal = b3Neg(query.planes[i].plane.normal);

        const float inv_mass = b3Body_GetInverseMass(body);
        const b3Matrix3 inv_inertia = b3Body_GetWorldInverseRotationalInertia(body);
        const b3Vec3 center = b3ToVec3(b3Body_GetWorldCenter(body));
        const b3Vec3 r = b3Sub(point, center);
        const b3Vec3 rn = b3Cross(r, normal);
        const float k = inv_mass + b3Dot(rn, b3MulMV(inv_inertia, rn));
        const float normal_mass = k > 0.0f ? 1.0f / k : 0.0f;

        const b3Vec3 v = b3Body_GetLinearVelocity(body);
        const b3Vec3 omega = b3Body_GetAngularVelocity(body);
        const b3Vec3 vr = b3Add(v, b3Cross(omega, r));
        const float vn = b3Dot(b3Sub(vr, box3d::to_b3(character_velocity)), normal);
        const float impulse = std::max(-normal_mass * vn, 0.0f);
        if(impulse <= 0.0f)
        {
            continue;
        }
        b3Body_ApplyLinearImpulse(body, b3MulSV(impulse, normal), b3ToPos(point), true);
    }
}

/**
 * @brief One fixed step of the geometric character mover.
 *
 * A step-up / slide / step-down mover built on Box3D's mover queries:
 *   1. free velocity integration (gravity, terminal speed, damping)
 *   2. sweep up by the step allowance plus any upward motion
 *   3. slide the horizontal move along the collected planes
 *   4. sweep back down, snapping onto stairs and slopes when previously grounded
 *   5. resolve residual overlap, decide grounded from supporting planes, clip velocity
 * Runs before the world step; the kinematic proxy is then driven to the new pose so it
 * pushes dynamic bodies and is seen by sensors at the end of the step.
 */
void step_character(box3d::world& world,
                    character_controller_component& comp,
                    box3d::character_controller& cc,
                    float dt)
{
    if(!b3Body_IsValid(cc.body) || !b3Body_IsEnabled(cc.body) || dt <= 0.0f)
    {
        cc.pending_displacement = math::vec3{0.0f};
        cc.touching.clear();
        return;
    }

    const math::vec3 up{0.0f, 1.0f, 0.0f};
    const bool was_grounded = cc.grounded;

    // 1) integrate the free velocity
    const math::vec3 gravity = box3d::from_b3(b3World_GetGravity(world.id)) * comp.get_gravity_scale();
    cc.velocity += gravity * dt;
    const float terminal = std::abs(comp.get_terminal_velocity());
    if(cc.velocity.y < -terminal)
    {
        cc.velocity.y = -terminal;
    }
    const float damping = std::clamp(comp.get_linear_damping(), 0.0f, 1.0f);
    if(damping > 0.0f)
    {
        cc.velocity *= std::pow(1.0f - damping, dt);
    }

    const float vertical_offset = cc.velocity.y * dt;
    const math::vec3 walk = cc.pending_displacement + math::vec3{cc.velocity.x, 0.0f, cc.velocity.z} * dt;
    cc.pending_displacement = math::vec3{0.0f};

    mover_query query;
    query.world = &world;
    query.cc = &cc;
    query.origin = cc.position;
    query.capsule = make_mover_capsule(cc);
    query.filter = box3d::make_query_filter(cc.category_bits, cc.mask_bits);

    const math::vec3 start = cc.position;
    const float step_height = std::max(comp.get_step_height(), 0.0f);

    // 2) up: the step allowance is a temporary lift that is always undone below, on
    //    top of whatever upward motion the velocity asks for.
    const float requested_up = std::max(0.0f, vertical_offset);
    const float lift = step_height + requested_up;
    float climbed = 0.0f;
    if(lift > 0.0f)
    {
        const float fraction = cast_mover(query, up * lift);
        climbed = lift * fraction;
        query.origin += up * climbed;
        if(fraction < 1.0f && vertical_offset > 0.0f)
        {
            // Hit a ceiling while moving up.
            cc.velocity.y = 0.0f;
        }
    }

    // 3) slide
    solve_mover_translation(query, walk);

    // 4) down: give back the allowance that was actually climbed, plus the fall.
    const float allowance_climbed = std::max(0.0f, climbed - requested_up);
    const float down_distance = allowance_climbed + std::max(0.0f, -vertical_offset);
    bool landed = false;
    if(down_distance > 0.0f)
    {
        const float fraction = cast_mover(query, -up * down_distance);
        query.origin -= up * (down_distance * fraction);
        landed = fraction < 1.0f;
    }
    if(!landed && cc.velocity.y <= 0.0f && was_grounded && !cc.jumping && step_height > 0.0f)
    {
        // Walking down stairs or a slope: stay attached if the ground is within one
        // step below.
        const float fraction = cast_mover(query, -up * step_height);
        if(fraction < 1.0f)
        {
            query.origin -= up * (step_height * fraction);
            landed = true;
        }
    }

    // 5) resolve residual overlap and read the supporting planes
    collect_mover_planes(query);
    const b3PlaneSolverResult resolve = b3SolvePlanes(b3Vec3_zero, query.planes, query.plane_count);
    query.origin += box3d::from_b3(resolve.delta);

    const float slope_cosine = std::cos(math::radians(comp.get_slope_limit()));
    bool supported = false;
    for(int i = 0; i < query.plane_count; ++i)
    {
        if(box3d::from_b3(query.planes[i].plane.normal).y >= slope_cosine)
        {
            supported = true;
            break;
        }
    }
    // A landing that registered no planes stopped within the contact slop; treat it as
    // ground. A landing on nothing but steep planes is a slide, not ground.
    cc.grounded = supported || (landed && query.plane_count == 0);
    if(cc.grounded)
    {
        cc.jumping = false;
        if(cc.velocity.y < 0.0f)
        {
            cc.velocity.y = 0.0f;
        }
    }

    cc.velocity = box3d::from_b3(b3ClipVector(box3d::to_b3(cc.velocity), query.planes, query.plane_count));

    const math::vec3 effective_velocity = (query.origin - start) / dt;
    push_dynamic_bodies(query, effective_velocity);

    cc.touching.clear();
    for(int i = 0; i < query.plane_count; ++i)
    {
        cc.touching.push_back(query.contacts[i]);
    }

    cc.position = query.origin;

    if(cc.grounded)
    {
        // Ground friction: horizontal impulses last one step, like the previous backend,
        // and move() is the only sustained horizontal input.
        cc.velocity.x = 0.0f;
        cc.velocity.z = 0.0f;
    }

    b3WorldTransform target;
    target.p = box3d::to_b3_pos(cc.position);
    target.q = box3d::identity_quat();
    b3Body_SetTargetTransform(cc.body, target, dt, true);
    restart_sleep_while_moving(world, cc.body);
}

void make_character_controller_body(box3d::world& world, entt::handle entity, character_controller_component& comp)
{
    auto& cc = entity.emplace<box3d::character_controller>();
    cc.radius = std::max(comp.get_radius(), box3d::min_half_extent);
    cc.half_segment = std::max((comp.get_height() - 2.0f * comp.get_radius()) * 0.5f, 0.0f);
    cc.category_bits = get_layer_bits(entity);
    cc.mask_bits = box3d::to_filter_bits(comp.get_collision_mask().mask);
    cc.velocity = math::vec3{0.0f};
    cc.pending_displacement = math::vec3{0.0f};
    cc.grounded = false;
    cc.jumping = false;

    if(const auto* transform = entity.try_get<transform_component>())
    {
        cc.position = transform->get_position_global() + comp.get_center();
        cc.rotation = transform->get_rotation_global();
    }

    b3BodyDef def = b3DefaultBodyDef();
    def.type = b3_kinematicBody;
    def.position = box3d::to_b3_pos(cc.position);
    def.rotation = box3d::identity_quat();
    def.userData = box3d::encode_entity(entity.entity());
    def.enableSleep = false;
    def.isEnabled = entity.all_of<active_component>();
    // Recycled manifolds cause ghost collisions on characters.
    def.enableContactRecycling = false;
    cc.body = b3CreateBody(world.id, &def);
    if(B3_IS_NULL(cc.body))
    {
        APPLOG_ERROR("box3d: failed to create character proxy body");
        return;
    }

    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.userData = box3d::encode_entity(entity.entity());
    shape_def.filter = box3d::make_filter(cc.category_bits, cc.mask_bits);
    shape_def.enableSensorEvents = true;
    shape_def.enableContactEvents = false;
    shape_def.enableHitEvents = false;

    if(2.0f * cc.half_segment < B3_MIN_CAPSULE_LENGTH)
    {
        b3Sphere sphere;
        sphere.center = b3Vec3_zero;
        sphere.radius = cc.radius;
        cc.shape = b3CreateSphereShape(cc.body, &shape_def, &sphere);
    }
    else
    {
        const b3Capsule capsule = make_mover_capsule(cc);
        cc.shape = b3CreateCapsuleShape(cc.body, &shape_def, &capsule);
    }
}

void release_character_controller(box3d::character_controller& cc)
{
    if(b3Body_IsValid(cc.body))
    {
        b3DestroyBody(cc.body);
    }
    cc.body = {};
    cc.shape = {};
    cc.touching.clear();
    cc.pending_displacement = math::vec3{0.0f};
}

void destroy_character_controller_body(box3d::world& world, entt::handle entity, bool from_cc_component)
{
    auto* cc = entity.try_get<box3d::character_controller>();
    if(cc)
    {
        // Same contract as rigid bodies: the tracked pairs leave the graph with it, and
        // are reported unless we are inside an entity teardown that already reported them.
        const bool notify = !scene::is_destroy_suppressed();

        world.release_contacts_for(entity, unravel::contact_end_reason::other_disabled, notify);

        release_character_controller(*cc);
    }
    if(from_cc_component)
    {
        entity.remove<box3d::character_controller>();
    }
}

void connect_ragdoll_controller(box3d::world& world, entt::handle owner, box3d::ragdoll& ragdoll);

/// A proxy body made again lost the filter joints that kept it apart from the character's own ragdoll.
void reconnect_own_ragdoll(box3d::world& world, entt::handle controller, entt::entity ragdoll_entity)
{
    auto& registry = *controller.registry();
    auto* ragdoll = registry.valid(ragdoll_entity) ? registry.try_get<box3d::ragdoll>(ragdoll_entity) : nullptr;
    if(ragdoll == nullptr)
    {
        return;
    }
    const auto is_gone = [](b3JointId id)
    {
        return !b3Joint_IsValid(id);
    };
    ragdoll->filters.erase(std::remove_if(ragdoll->filters.begin(), ragdoll->filters.end(), is_gone),
                           ragdoll->filters.end());
    connect_ragdoll_controller(world, entt::handle(registry, ragdoll_entity), *ragdoll);
}

void sync_character_controller_body(box3d::world& world, character_controller_component& comp, bool force = false)
{
    auto owner = comp.get_owner();
    if(force)
    {
        destroy_character_controller_body(world, owner, true);
        make_character_controller_body(world, owner, comp);
    }
    else
    {
        auto* cc = owner.try_get<box3d::character_controller>();
        if(!cc || !b3Body_IsValid(cc->body))
        {
            return;
        }
        // Geometry and filtering are baked into the proxy shape; everything else is read
        // from the component when the mover steps.
        if(comp.is_property_dirty(character_controller_property::shape) ||
           comp.is_property_dirty(character_controller_property::skin_width) ||
           comp.is_property_dirty(character_controller_property::layer))
        {
            const math::vec3 velocity = cc->velocity;
            const math::vec3 pending = cc->pending_displacement;
            const bool grounded = cc->grounded;
            const entt::entity own_ragdoll = cc->own_ragdoll;
            destroy_character_controller_body(world, owner, true);
            make_character_controller_body(world, owner, comp);
            if(auto* rebuilt = owner.try_get<box3d::character_controller>())
            {
                rebuilt->velocity = velocity;
                rebuilt->pending_displacement = pending;
                rebuilt->grounded = grounded;
                reconnect_own_ragdoll(world, owner, own_ragdoll);
            }
        }
    }
    comp.set_dirty(system_id, false);
}

void set_character_pose(box3d::character_controller& cc, const math::vec3& position, const math::quat& rotation)
{
    cc.position = position;
    cc.rotation = rotation;
    if(b3Body_IsValid(cc.body))
    {
        b3Body_SetTransform(cc.body, box3d::to_b3_pos(position), box3d::identity_quat());
    }
}

auto to_physics_cc(box3d::world& world, transform_component& transform, character_controller_component& comp) -> bool
{
    const bool transform_dirty = transform.is_dirty(system_id);
    const bool cc_dirty = comp.is_dirty(system_id);
    sync_character_controller_body(world, comp);
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc || !b3Body_IsValid(cc->body))
    {
        return false;
    }
    if(transform_dirty || cc_dirty)
    {
        set_character_pose(*cc, transform.get_position_global() + comp.get_center(), transform.get_rotation_global());
        return true;
    }
    return false;
}

auto from_physics_cc(box3d::world& /*world*/, transform_component& transform, character_controller_component& comp)
    -> bool
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc || !b3Body_IsValid(cc->body))
    {
        return false;
    }
    const math::vec3 p = cc->position - comp.get_center();
    // The mover never rotates; the ECS rotation stays authoritative.
    const bool changed =
        transform.set_position_and_rotation_global(p, transform.get_rotation_global(), box3d::transform_sync_epsilon);

    comp.set_grounded(cc->grounded);
    comp.set_velocity_internal(cc->velocity);
    // Resolved before the bit is cleared, as for bodies: a later local-space write must still raise it.
    transform.resolve_transform_global();
    transform.set_dirty(system_id, false);
    comp.set_dirty(system_id, false);
    return changed;
}

// --- forces ------------------------------------------------------------------------------

auto add_force(b3BodyId body, const b3Vec3& force, force_mode mode) -> bool
{
    if(b3LengthSquared(force) <= 0.0f)
    {
        return false;
    }
    switch(mode)
    {
        case force_mode::force:
            b3Body_ApplyForceToCenter(body, force, true);
            break;
        case force_mode::acceleration:
            b3Body_ApplyForceToCenter(body, b3MulSV(b3Body_GetMass(body), force), true);
            break;
        case force_mode::impulse:
            b3Body_ApplyLinearImpulseToCenter(body, force, true);
            break;
        case force_mode::velocity_change:
            b3Body_SetLinearVelocity(body, b3Add(b3Body_GetLinearVelocity(body), force));
            b3Body_SetAwake(body, true);
            break;
    }
    return true;
}

auto add_torque(b3BodyId body, const b3Vec3& torque, force_mode mode) -> bool
{
    if(b3LengthSquared(torque) <= 0.0f)
    {
        return false;
    }
    switch(mode)
    {
        case force_mode::force:
            b3Body_ApplyTorque(body, torque, true);
            break;
        case force_mode::acceleration:
        {
            // Angular acceleration: torque = I * alpha, with I in world space.
            const b3Matrix3 inv_inertia = b3Body_GetWorldInverseRotationalInertia(body);
            const b3Matrix3 inertia = b3InvertMatrix(inv_inertia);
            b3Body_ApplyTorque(body, b3MulMV(inertia, torque), true);
            break;
        }
        case force_mode::impulse:
            b3Body_ApplyAngularImpulse(body, torque, true);
            break;
        case force_mode::velocity_change:
            b3Body_SetAngularVelocity(body, b3Add(b3Body_GetAngularVelocity(body), torque));
            b3Body_SetAwake(body, true);
            break;
    }
    return true;
}

/// Flushes pending prefab/spawn dirty state so an impulse hits a correct dynamic body.
auto prepare_dynamic_body(box3d::world& world, physics_component& comp) -> box3d::rigidbody*
{
    auto owner = comp.get_owner();
    const bool force_recreate = !owner.all_of<box3d::rigidbody>();
    sync_physics_body(world, comp, force_recreate);

    auto* body = owner.try_get<box3d::rigidbody>();
    if(!body || !b3Body_IsValid(body->body) || b3Body_GetInverseMass(body->body) <= 0.0f)
    {
        return nullptr;
    }
    return body;
}

// --- joints ------------------------------------------------------------------------------

/// Box3D turns revolute and spherical joints about the frame's z axis; the engine's joint axis is X.
auto get_z_to_x_rotation() -> math::quat
{
    return math::angleAxis(math::half_pi<float>(), math::vec3{0.0f, 1.0f, 0.0f});
}

/// Box3D wheels spin about z and travel along x; the engine spins about X and travels along Y.
auto get_wheel_frame_rotation() -> math::quat
{
    const math::mat3 axes(math::vec3{0.0f, 1.0f, 0.0f}, math::vec3{0.0f, 0.0f, 1.0f}, math::vec3{1.0f, 0.0f, 0.0f});
    return math::normalize(math::quat_cast(axes));
}

/// Turns an engine joint frame into the Box3D frame of a joint type.
auto get_joint_axis_rotation(joint_type type) -> math::quat
{
    switch(type)
    {
        case joint_type::hinge:
        case joint_type::ball:
            return get_z_to_x_rotation();
        case joint_type::wheel:
            return get_wheel_frame_rotation();
        case joint_type::fixed:
        case joint_type::slider:
        case joint_type::distance:
        default:
            return math::identity<math::quat>();
    }
}

auto find_body_id(entt::handle entity) -> b3BodyId
{
    if(!entity)
    {
        return b3_nullBodyId;
    }
    if(const auto* body = entity.try_get<box3d::rigidbody>(); body && b3Body_IsValid(body->body))
    {
        return body->body;
    }
    return b3_nullBodyId;
}

/// A world frame in a body's space.
auto make_local_frame(b3BodyId body, const math::vec3& position, const math::quat& rotation) -> b3Transform
{
    b3Transform frame;
    const b3WorldTransform pose = b3Body_GetTransform(body);
    frame.p = b3Body_GetLocalPoint(body, box3d::to_b3_pos(position));
    frame.q = box3d::to_b3(math::normalize(glm::inverse(box3d::from_b3(pose.q)) * rotation));
    return frame;
}

auto to_threshold(float value) -> float
{
    return value > 0.0f ? value : FLT_MAX;
}

auto clamp_cone_angle(float degrees) -> float
{
    return math::radians(math::clamp(degrees, 0.0f, MAX_CONE_ANGLE));
}

auto clamp_joint_angle(float degrees) -> float
{
    // Box3D keeps revolute and twist limits inside +-0.99 pi.
    const float max_radians = 0.99f * math::pi<float>();
    return math::clamp(math::radians(degrees), -max_radians, max_radians);
}

void destroy_joint_id(b3JointId& id)
{
    if(B3_IS_NON_NULL(id) && b3Joint_IsValid(id))
    {
        b3DestroyJoint(id, true);
    }
    id = b3_nullJointId;
}

void fill_joint_base(b3JointDef& base, const box3d::joint& joint, const joint_component& comp)
{
    base.bodyIdA = joint.body_a;
    base.bodyIdB = joint.body_b;
    base.localFrameA = joint.frame_a;
    base.localFrameB = joint.frame_b;
    base.collideConnected = comp.get_collide_connected();
    base.forceThreshold = to_threshold(comp.get_break_force());
    base.torqueThreshold = to_threshold(comp.get_break_torque());
    base.userData = box3d::encode_entity(comp.get_owner().entity());
}

/// Motor settings for a joint whose motor is off: a motor at rest with the friction as its torque.
auto get_motor_speed(const joint_component& comp) -> float
{
    return comp.get_motor().enabled ? comp.get_motor().speed : 0.0f;
}

auto get_motor_force(const joint_component& comp) -> float
{
    return comp.get_motor().enabled ? comp.get_motor().max_force : comp.get_friction();
}

auto is_motor_on(const joint_component& comp) -> bool
{
    return comp.get_motor().enabled || comp.get_friction() > 0.0f;
}

void tune_hinge(b3JointId id, const joint_component& comp)
{
    const auto& limits = comp.get_limits();
    const auto& spring = comp.get_spring();
    b3RevoluteJoint_EnableLimit(id, limits.enabled);
    b3RevoluteJoint_SetLimits(id,
                              clamp_joint_angle(std::min(limits.lower, limits.upper)),
                              clamp_joint_angle(std::max(limits.lower, limits.upper)));
    b3RevoluteJoint_EnableSpring(id, spring.enabled);
    b3RevoluteJoint_SetSpringHertz(id, spring.frequency);
    b3RevoluteJoint_SetSpringDampingRatio(id, spring.damping_ratio);
    b3RevoluteJoint_SetTargetAngle(id, clamp_joint_angle(spring.target));
    b3RevoluteJoint_EnableMotor(id, is_motor_on(comp));
    b3RevoluteJoint_SetMotorSpeed(id, math::radians(get_motor_speed(comp)));
    b3RevoluteJoint_SetMaxMotorTorque(id, get_motor_force(comp));
}

void tune_slider(b3JointId id, const joint_component& comp)
{
    const auto& limits = comp.get_limits();
    const auto& spring = comp.get_spring();
    b3PrismaticJoint_EnableLimit(id, limits.enabled);
    b3PrismaticJoint_SetLimits(id, std::min(limits.lower, limits.upper), std::max(limits.lower, limits.upper));
    b3PrismaticJoint_EnableSpring(id, spring.enabled);
    b3PrismaticJoint_SetSpringHertz(id, spring.frequency);
    b3PrismaticJoint_SetSpringDampingRatio(id, spring.damping_ratio);
    b3PrismaticJoint_SetTargetTranslation(id, spring.target);
    b3PrismaticJoint_EnableMotor(id, is_motor_on(comp));
    b3PrismaticJoint_SetMotorSpeed(id, get_motor_speed(comp));
    b3PrismaticJoint_SetMaxMotorForce(id, get_motor_force(comp));
}

void tune_ball(b3JointId id, const joint_component& comp)
{
    const auto& limits = comp.get_limits();
    const auto& spring = comp.get_spring();
    b3SphericalJoint_EnableConeLimit(id, limits.enabled);
    b3SphericalJoint_SetConeLimit(id, clamp_cone_angle(limits.cone_angle));
    b3SphericalJoint_EnableTwistLimit(id, limits.enabled);
    b3SphericalJoint_SetTwistLimits(id,
                                    clamp_joint_angle(std::min(limits.lower, limits.upper)),
                                    clamp_joint_angle(std::max(limits.lower, limits.upper)));
    b3SphericalJoint_EnableSpring(id, spring.enabled);
    b3SphericalJoint_SetSpringHertz(id, spring.frequency);
    b3SphericalJoint_SetSpringDampingRatio(id, spring.damping_ratio);
    // The target is in the engine's joint frame, where X twists; Box3D's frame is that one turned so Z twists.
    const math::quat to_box3d_frame = get_z_to_x_rotation();
    const math::quat target = math::quat(math::radians(spring.target_rotation));
    b3SphericalJoint_SetTargetRotation(id,
                                       box3d::to_b3(math::normalize(glm::inverse(to_box3d_frame) * target *
                                                                    to_box3d_frame)));
    // A ball joint has no motor speed to drive: its motor is its friction.
    b3SphericalJoint_EnableMotor(id, comp.get_friction() > 0.0f);
    b3SphericalJoint_SetMotorVelocity(id, b3Vec3_zero);
    b3SphericalJoint_SetMaxMotorTorque(id, comp.get_friction());
}

void tune_distance(b3JointId id, const box3d::joint& joint, const joint_component& comp)
{
    const auto& limits = comp.get_limits();
    const auto& spring = comp.get_spring();
    // A distance joint without a spring is a rigid rod; a rope is a spring of no stiffness inside its limits.
    const bool soft = spring.enabled || limits.enabled;
    b3DistanceJoint_SetLength(id, comp.get_distance() > 0.0f ? comp.get_distance() : joint.length);
    b3DistanceJoint_EnableSpring(id, soft);
    b3DistanceJoint_SetSpringHertz(id, spring.enabled ? spring.frequency : 0.0f);
    b3DistanceJoint_SetSpringDampingRatio(id, spring.damping_ratio);
    b3DistanceJoint_EnableLimit(id, limits.enabled);
    b3DistanceJoint_SetLengthRange(id,
                                   std::max(std::min(limits.lower, limits.upper), 0.0f),
                                   std::max(std::max(limits.lower, limits.upper), 0.0f));
    b3DistanceJoint_EnableMotor(id, is_motor_on(comp));
    b3DistanceJoint_SetMotorSpeed(id, get_motor_speed(comp));
    b3DistanceJoint_SetMaxMotorForce(id, get_motor_force(comp));
}

void tune_wheel(b3JointId id, const joint_component& comp)
{
    const auto& limits = comp.get_limits();
    const auto& spring = comp.get_spring();
    const auto& steering = comp.get_steering();
    b3WheelJoint_EnableSuspension(id, spring.enabled);
    b3WheelJoint_SetSuspensionHertz(id, spring.frequency);
    b3WheelJoint_SetSuspensionDampingRatio(id, spring.damping_ratio);
    b3WheelJoint_EnableSuspensionLimit(id, limits.enabled);
    b3WheelJoint_SetSuspensionLimits(id, std::min(limits.lower, limits.upper), std::max(limits.lower, limits.upper));
    b3WheelJoint_EnableSpinMotor(id, is_motor_on(comp));
    b3WheelJoint_SetSpinMotorSpeed(id, math::radians(get_motor_speed(comp)));
    b3WheelJoint_SetMaxSpinTorque(id, get_motor_force(comp));
    b3WheelJoint_SetSteeringHertz(id, wheel_steering_hertz);
    b3WheelJoint_SetSteeringDampingRatio(id, wheel_steering_damping_ratio);
    b3WheelJoint_SetMaxSteeringTorque(id, steering.max_torque);
    const float limit = math::radians(math::clamp(std::abs(steering.limit), 0.0f, box3d::max_steering_degrees));
    b3WheelJoint_SetSteeringLimits(id, -limit, limit);
    b3WheelJoint_SetTargetSteeringAngle(id, math::clamp(math::radians(steering.angle), -limit, limit));
}

auto is_wheel_square_to_suspension(const box3d::joint& joint) -> bool
{
    const b3Quat frame_a = b3MulQuat(b3Body_GetRotation(joint.body_a), joint.frame_a.q);
    const b3Quat frame_b = b3MulQuat(b3Body_GetRotation(joint.body_b), joint.frame_b.q);
    const b3Vec3 suspension_axis = b3RotateVector(frame_a, b3Vec3_axisX);
    const b3Vec3 spin_axis = b3RotateVector(frame_b, b3Vec3_axisZ);
    return std::abs(b3Dot(suspension_axis, spin_axis)) < wheel_steering_max_axis_cosine;
}

/// Checked before every step: a wheel knocked far off square stops steering until its joint has turned it back.
void apply_wheel_steering(const box3d::joint& joint, const joint_component& comp)
{
    const bool steers = comp.get_steering().enabled && is_wheel_square_to_suspension(joint);
    b3WheelJoint_EnableSteering(joint.id, steers);
    b3WheelJoint_EnableSteeringLimit(joint.id, steers);
}

void tune_fixed(b3JointId id, const joint_component& comp)
{
    const auto& spring = comp.get_spring();
    const float hertz = spring.enabled ? spring.frequency : 0.0f;
    b3WeldJoint_SetLinearHertz(id, hertz);
    b3WeldJoint_SetAngularHertz(id, hertz);
    b3WeldJoint_SetLinearDampingRatio(id, spring.damping_ratio);
    b3WeldJoint_SetAngularDampingRatio(id, spring.damping_ratio);
}

void tune_joint(box3d::joint& joint, const joint_component& comp)
{
    if(B3_IS_NULL(joint.id) || !b3Joint_IsValid(joint.id))
    {
        return;
    }
    b3Joint_SetCollideConnected(joint.id, comp.get_collide_connected());
    b3Joint_SetForceThreshold(joint.id, to_threshold(comp.get_break_force()));
    b3Joint_SetTorqueThreshold(joint.id, to_threshold(comp.get_break_torque()));
    switch(comp.get_type())
    {
        case joint_type::fixed:
            tune_fixed(joint.id, comp);
            break;
        case joint_type::hinge:
            tune_hinge(joint.id, comp);
            break;
        case joint_type::slider:
            tune_slider(joint.id, comp);
            break;
        case joint_type::ball:
            tune_ball(joint.id, comp);
            break;
        case joint_type::distance:
            tune_distance(joint.id, joint, comp);
            break;
        case joint_type::wheel:
            tune_wheel(joint.id, comp);
            break;
    }
    b3Joint_WakeBodies(joint.id);
}

auto create_joint_id(box3d::world& world, const box3d::joint& joint, const joint_component& comp) -> b3JointId
{
    switch(comp.get_type())
    {
        case joint_type::fixed:
        {
            b3WeldJointDef def = b3DefaultWeldJointDef();
            fill_joint_base(def.base, joint, comp);
            return b3CreateWeldJoint(world.id, &def);
        }
        case joint_type::hinge:
        {
            b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
            fill_joint_base(def.base, joint, comp);
            return b3CreateRevoluteJoint(world.id, &def);
        }
        case joint_type::slider:
        {
            b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
            fill_joint_base(def.base, joint, comp);
            return b3CreatePrismaticJoint(world.id, &def);
        }
        case joint_type::ball:
        {
            b3SphericalJointDef def = b3DefaultSphericalJointDef();
            fill_joint_base(def.base, joint, comp);
            return b3CreateSphericalJoint(world.id, &def);
        }
        case joint_type::distance:
        {
            b3DistanceJointDef def = b3DefaultDistanceJointDef();
            fill_joint_base(def.base, joint, comp);
            def.length = std::max(comp.get_distance() > 0.0f ? comp.get_distance() : joint.length, B3_LINEAR_SLOP);
            return b3CreateDistanceJoint(world.id, &def);
        }
        case joint_type::wheel:
        {
            b3WheelJointDef def = b3DefaultWheelJointDef();
            fill_joint_base(def.base, joint, comp);
            return b3CreateWheelJoint(world.id, &def);
        }
    }
    return b3_nullJointId;
}

/// Reads the frames from where the joint entity and both bodies stand now.
void place_joint_frames(box3d::joint& joint, const joint_component& comp)
{
    const math::transform frame = comp.get_frame_global();
    const math::quat rotation = math::normalize(frame.get_rotation() * get_joint_axis_rotation(comp.get_type()));
    joint.frame_a = make_local_frame(joint.body_a, frame.get_position(), rotation);
    if(comp.get_type() == joint_type::distance)
    {
        // The distance joint holds a point of body B: its origin moved by the connected anchor.
        const b3WorldTransform pose_b = b3Body_GetTransform(joint.body_b);
        const math::vec3 point_b = box3d::from_b3_pos(pose_b.p) + box3d::from_b3(pose_b.q) * comp.get_connected_anchor();
        joint.frame_b = make_local_frame(joint.body_b, point_b, rotation);
        joint.length = math::distance(frame.get_position(), point_b);
    }
    else
    {
        joint.frame_b = make_local_frame(joint.body_b, frame.get_position(), rotation);
    }
    joint.has_frames = true;
}

void break_joint(box3d::world& world, entt::handle owner, box3d::joint& joint, joint_component& comp)
{
    if(comp.get_state().is_broken)
    {
        return;
    }
    const math::vec3 force = B3_IS_NON_NULL(joint.id) && b3Joint_IsValid(joint.id)
                                 ? box3d::from_b3(b3Joint_GetConstraintForce(joint.id))
                                 : math::vec3{0.0f};
    const math::vec3 torque = B3_IS_NON_NULL(joint.id) && b3Joint_IsValid(joint.id)
                                  ? box3d::from_b3(b3Joint_GetConstraintTorque(joint.id))
                                  : math::vec3{0.0f};
    destroy_joint_id(joint.id);
    auto& state = comp.get_state();
    state.is_broken = true;
    state.is_active = false;
    state.force = force;
    state.torque = torque;
    world.broken_joints.push_back({owner.entity(), force, torque});
}

void sync_joint(box3d::world& world, joint_component& comp)
{
    auto owner = comp.get_owner();
    auto& joint = owner.get_or_emplace<box3d::joint>();
    auto& state = comp.get_state();
    if(comp.take_requested_repair() && state.is_broken)
    {
        state.is_broken = false;
        comp.set_needs_rebuild(true);
    }
    // A destroyed body lets the joint go: falling back to the world or to the joint's own entity would hold the
    // other body by frames measured on the body that is gone.
    if(comp.take_requested_break() || comp.is_body_missing())
    {
        break_joint(world, owner, joint, comp);
    }
    if(state.is_broken)
    {
        comp.set_dirty(system_id, false);
        return;
    }
    const b3BodyId body_a = comp.get_body_a() ? find_body_id(comp.get_body_a()) : world.ground;
    const b3BodyId body_b = find_body_id(comp.get_resolved_body_b());
    if(B3_IS_NULL(body_a) || B3_IS_NULL(body_b) || B3_ID_EQUALS(body_a, body_b))
    {
        // A body still loading or gone: the joint waits for it.
        destroy_joint_id(joint.id);
        state.is_active = false;
        return;
    }
    const bool is_alive = B3_IS_NON_NULL(joint.id) && b3Joint_IsValid(joint.id);
    const bool bodies_changed = !B3_ID_EQUALS(joint.body_a, body_a) || !B3_ID_EQUALS(joint.body_b, body_b);
    joint.body_a = body_a;
    joint.body_b = body_b;
    // A body made again (a reload, a type change) keeps its place on the entity, so the frames stay valid.
    if(comp.needs_rebuild() || !joint.has_frames)
    {
        place_joint_frames(joint, comp);
    }
    if(comp.needs_rebuild() || !is_alive || bodies_changed)
    {
        destroy_joint_id(joint.id);
        joint.id = create_joint_id(world, joint, comp);
        joint.has_previous_angle = false;
        comp.set_needs_rebuild(false);
        tune_joint(joint, comp);
    }
    else if(comp.is_dirty(system_id))
    {
        tune_joint(joint, comp);
    }
    comp.set_dirty(system_id, false);
    state.is_active = B3_IS_NON_NULL(joint.id);
    if(state.is_active && comp.get_type() == joint_type::wheel)
    {
        apply_wheel_steering(joint, comp);
    }
}

void read_joint_state(box3d::joint& joint, joint_component& comp, float dt)
{
    auto& state = comp.get_state();
    if(B3_IS_NULL(joint.id) || !b3Joint_IsValid(joint.id))
    {
        return;
    }
    state.force = box3d::from_b3(b3Joint_GetConstraintForce(joint.id));
    state.torque = box3d::from_b3(b3Joint_GetConstraintTorque(joint.id));
    float angle = state.angle;
    switch(comp.get_type())
    {
        case joint_type::hinge:
            angle = math::degrees(b3RevoluteJoint_GetAngle(joint.id));
            break;
        case joint_type::ball:
            angle = math::degrees(b3SphericalJoint_GetTwistAngle(joint.id));
            break;
        case joint_type::slider:
            state.position = b3PrismaticJoint_GetTranslation(joint.id);
            state.speed = b3PrismaticJoint_GetSpeed(joint.id);
            break;
        case joint_type::distance:
            state.position = b3DistanceJoint_GetCurrentLength(joint.id);
            break;
        case joint_type::wheel:
            angle = math::degrees(b3WheelJoint_GetSteeringAngle(joint.id));
            state.speed = math::degrees(b3WheelJoint_GetSpinSpeed(joint.id));
            break;
        case joint_type::fixed:
            break;
    }
    if(comp.get_type() == joint_type::hinge || comp.get_type() == joint_type::ball)
    {
        float delta = angle - joint.previous_angle;
        // The angle wraps at +-180 degrees.
        delta = std::remainder(delta, box3d::full_turn_degrees);
        state.speed = joint.has_previous_angle && dt > 0.0f ? delta / dt : 0.0f;
        joint.previous_angle = angle;
        joint.has_previous_angle = true;
    }
    state.angle = angle;
}

void destroy_joint(entt::handle entity)
{
    if(auto* joint = entity.try_get<box3d::joint>())
    {
        destroy_joint_id(joint->id);
        joint->has_frames = false;
    }
    if(auto* comp = entity.try_get<joint_component>())
    {
        comp->get_state().is_active = false;
        comp->set_needs_rebuild(true);
    }
}

/**
 * @brief Box3D tests the break thresholds only for the joints its constraint graph colours; a joint on a body with
 *        more constraints than the colours hold is solved in the overflow, which never reports. The same test
 *        (constraint force or torque at or above the threshold) runs here for every joint after the step.
 */
void break_overloaded_joints(box3d::world& world, entt::registry& registry)
{
    registry.view<joint_component, box3d::joint>().each(
        [&](auto /*entity*/, joint_component& comp, box3d::joint& joint)
        {
            if(comp.get_state().is_broken || B3_IS_NULL(joint.id) || !b3Joint_IsValid(joint.id))
            {
                return;
            }
            const float force = b3Length(b3Joint_GetConstraintForce(joint.id));
            const float torque = b3Length(b3Joint_GetConstraintTorque(joint.id));
            if(force >= to_threshold(comp.get_break_force()) || torque >= to_threshold(comp.get_break_torque()))
            {
                break_joint(world, comp.get_owner(), joint, comp);
            }
        });
}

/// Joints whose force passed their threshold in the last step break.
void collect_broken_joints(box3d::world& world, unravel::scene& scn)
{
    const b3JointEvents events = b3World_GetJointEvents(world.id);
    for(int i = 0; i < events.count; ++i)
    {
        const entt::entity entity = box3d::decode_entity(events.jointEvents[i].userData);
        auto owner = scn.create_handle(entity);
        if(!owner)
        {
            continue;
        }
        auto* comp = owner.try_get<joint_component>();
        auto* joint = owner.try_get<box3d::joint>();
        if(comp == nullptr || joint == nullptr || !B3_ID_EQUALS(joint->id, events.jointEvents[i].jointId))
        {
            continue;
        }
        break_joint(world, owner, *joint, *comp);
    }
    break_overloaded_joints(world, *scn.registry);
}

// --- ragdolls -----------------------------------------------------------------------------

auto rigid_pose(const math::transform& transform) -> b3WorldTransform
{
    b3WorldTransform pose;
    pose.p = box3d::to_b3_pos(transform.get_position());
    pose.q = box3d::to_b3(math::normalize(transform.get_rotation()));
    return pose;
}

auto make_pose(const b3WorldTransform& pose) -> math::transform
{
    math::transform result;
    result.set_position(box3d::from_b3_pos(pose.p));
    result.set_rotation(box3d::from_b3(pose.q));
    return result;
}

auto get_ragdoll_joint_rotation(ragdoll_joint_type type) -> math::quat
{
    return type == ragdoll_joint_type::fixed ? math::identity<math::quat>() : get_z_to_x_rotation();
}


auto make_ragdoll_filter(const ragdoll_component& comp, const box3d::ragdoll& ragdoll, entt::handle owner) -> b3Filter
{
    b3Filter filter = box3d::make_filter(get_layer_bits(owner), box3d::to_filter_bits(comp.get_collision_mask().mask));
    filter.groupIndex = comp.get_self_collision() ? 0 : -ragdoll.group;
    return filter;
}

/// Destroys a ragdoll's Box3D bodies and joints and lets its character go; the component stays on the entity.
void release_ragdoll(entt::handle owner, box3d::ragdoll& ragdoll)
{
    for(auto& filter : ragdoll.filters)
    {
        destroy_joint_id(filter);
    }
    ragdoll.filters.clear();
    for(auto& part : ragdoll.parts)
    {
        destroy_joint_id(part.joint);
        destroy_joint_id(part.anchor_joint);
        if(B3_IS_NON_NULL(part.anchor) && b3Body_IsValid(part.anchor))
        {
            b3DestroyBody(part.anchor);
        }
        part.anchor = b3_nullBodyId;
        release_rigidbody(part.body);
    }
    ragdoll.parts.clear();
    if(auto controller = owner.registry()->valid(ragdoll.controller) ? entt::handle(*owner.registry(), ragdoll.controller)
                                                                      : entt::handle{})
    {
        if(auto* cc = controller.try_get<box3d::character_controller>(); cc && cc->own_ragdoll == owner.entity())
        {
            cc->own_ragdoll = entt::null;
        }
    }
    if(auto* comp = owner.try_get<ragdoll_component>())
    {
        comp->get_runtime().is_built = false;
    }
}

void destroy_ragdoll(entt::handle owner)
{
    if(auto* ragdoll = owner.try_get<box3d::ragdoll>())
    {
        release_ragdoll(owner, *ragdoll);
        owner.remove<box3d::ragdoll>();
    }
}

/// The character the ragdoll belongs to, if it walks on a character controller: the entity or an ancestor.
auto find_ragdoll_controller(entt::handle owner) -> entt::handle
{
    for(entt::handle current = owner; current;)
    {
        if(current.all_of<box3d::character_controller>())
        {
            return current;
        }
        const auto* transform = current.try_get<transform_component>();
        current = transform ? transform->get_parent() : entt::handle{};
    }
    return {};
}

void create_ragdoll_part(box3d::world& world,
                         entt::handle owner,
                         ragdoll_component& comp,
                         box3d::ragdoll& ragdoll,
                         size_t index)
{
    const auto& definition = comp.get_bodies()[index];
    auto& runtime = comp.get_runtime().bodies[index];
    auto& part = ragdoll.parts[index];
    const auto& bone_transform = runtime.bone.get<transform_component>();
    // A ragdoll made again during play (its definition changed) carries on from where its simulated bodies are.
    const bool keeps_physics = runtime.simulated && runtime.has_physics_pose;
    const math::transform anim_pose = runtime.has_anim_pose ? runtime.anim_world : bone_transform.get_transform_global();
    const math::transform pose = keeps_physics ? runtime.world : anim_pose;
    const math::vec3 linear_velocity = keeps_physics ? runtime.linear_velocity : runtime.anim_linear_velocity;
    const math::vec3 angular_velocity = keeps_physics ? runtime.angular_velocity : runtime.anim_angular_velocity;

    b3BodyDef def = b3DefaultBodyDef();
    def.type = runtime.simulated ? b3_dynamicBody : b3_kinematicBody;
    def.position = box3d::to_b3_pos(pose.get_position());
    def.rotation = box3d::to_b3(math::normalize(pose.get_rotation()));
    def.userData = box3d::encode_entity(owner.entity());
    def.linearDamping = comp.get_linear_damping();
    def.angularDamping = comp.get_angular_damping();
    def.isBullet = comp.is_continuous();
    def.enableSleep = runtime.simulated;
    def.isAwake = true;
    part.body.body = b3CreateBody(world.id, &def);
    part.is_dynamic = runtime.simulated;
    if(B3_IS_NULL(part.body.body))
    {
        return;
    }
    part.body.shape_scale = math::abs(bone_transform.get_scale_global());
    shape_build_context ctx{world, part.body, b3DefaultShapeDef(), part.body.shape_scale};
    ctx.def.userData = box3d::encode_entity(runtime.bone.entity());
    ctx.def.filter = make_ragdoll_filter(comp, ragdoll, owner);
    ctx.def.enableSensorEvents = true;
    ctx.def.enableContactEvents = false;
    ctx.def.enableHitEvents = false;
    ctx.def.updateBodyMass = false;
    ctx.def.baseMaterial.friction = ragdoll_friction;
    ctx.def.baseMaterial.rollingResistance = ragdoll_rolling_resistance;
    add_convex_shape(ctx, definition.shape);
    apply_body_mass(part.body.body, definition.mass);
    if(runtime.simulated)
    {
        b3Body_SetLinearVelocity(part.body.body, box3d::to_b3(linear_velocity));
        b3Body_SetAngularVelocity(part.body.body, box3d::to_b3(angular_velocity));
    }
    runtime.world = pose;
    runtime.world_previous = pose;
    runtime.has_physics_pose = runtime.simulated;
}

/// The rest relation of a body to its parent body, in the world's units: what the joint frames hold.
auto get_rest_relative(const math::transform& owner_world,
                       const ragdoll_body_runtime& parent,
                       const ragdoll_body_runtime& child) -> std::pair<math::vec3, math::quat>
{
    const math::vec3 parent_position = owner_world.transform_coord(parent.bind_model.get_position());
    const math::vec3 child_position = owner_world.transform_coord(child.bind_model.get_position());
    const math::quat owner_rotation = math::normalize(owner_world.get_rotation());
    const math::quat parent_rotation = math::normalize(owner_rotation * parent.bind_model.get_rotation());
    const math::quat child_rotation = math::normalize(owner_rotation * child.bind_model.get_rotation());
    const math::quat inverse_parent = glm::inverse(parent_rotation);
    return {inverse_parent * (child_position - parent_position), math::normalize(inverse_parent * child_rotation)};
}

/// A joint's friction is a motor held at zero speed with at most this torque; it follows the setting during play.
void apply_part_friction(const ragdoll_component& comp, const ragdoll_body& definition, box3d::ragdoll_part& part)
{
    if(B3_IS_NULL(part.joint) || !b3Joint_IsValid(part.joint))
    {
        return;
    }
    const float friction = comp.get_joint_friction() * definition.friction_scale;
    switch(part.joint_type)
    {
        case ragdoll_joint_type::ball:
            b3SphericalJoint_EnableMotor(part.joint, friction > 0.0f);
            b3SphericalJoint_SetMotorVelocity(part.joint, b3Vec3_zero);
            b3SphericalJoint_SetMaxMotorTorque(part.joint, friction);
            break;
        case ragdoll_joint_type::hinge:
            b3RevoluteJoint_EnableMotor(part.joint, friction > 0.0f);
            b3RevoluteJoint_SetMotorSpeed(part.joint, 0.0f);
            b3RevoluteJoint_SetMaxMotorTorque(part.joint, friction);
            break;
        case ragdoll_joint_type::fixed:
            break;
    }
}

void create_ragdoll_joint(box3d::world& world,
                          entt::handle owner,
                          ragdoll_component& comp,
                          box3d::ragdoll& ragdoll,
                          size_t index)
{
    const auto& definition = comp.get_bodies()[index];
    const auto& runtime = comp.get_runtime().bodies[index];
    auto& part = ragdoll.parts[index];
    if(runtime.parent < 0 || B3_IS_NULL(part.body.body))
    {
        return;
    }
    auto& parent = ragdoll.parts[size_t(runtime.parent)];
    if(B3_IS_NULL(parent.body.body))
    {
        return;
    }
    const auto [relative_position, relative_rotation] =
        get_rest_relative(owner.get<transform_component>().get_transform_global(),
                          comp.get_runtime().bodies[size_t(runtime.parent)],
                          runtime);
    const math::quat joint_rotation = math::quat(math::radians(definition.joint_rotation));
    const math::quat to_box3d_frame = get_ragdoll_joint_rotation(definition.joint);
    // The parent's side carries the swing offset, turned in the engine's joint frame (X twists), so the cone's
    // centre turns away from the rest pose.
    const math::quat swing_offset = math::quat(math::radians(definition.swing_offset));
    part.frame_b.p = b3Vec3_zero;
    part.frame_b.q = box3d::to_b3(math::normalize(joint_rotation * to_box3d_frame));
    part.frame_a.p = box3d::to_b3(relative_position);
    part.frame_a.q = box3d::to_b3(math::normalize(relative_rotation * joint_rotation * swing_offset * to_box3d_frame));
    part.joint_type = definition.joint;
    const auto fill_base = [&](b3JointDef& base)
    {
        base.bodyIdA = parent.body.body;
        base.bodyIdB = part.body.body;
        base.localFrameA = part.frame_a;
        base.localFrameB = part.frame_b;
        base.collideConnected = false;
    };
    switch(definition.joint)
    {
        case ragdoll_joint_type::ball:
        {
            b3SphericalJointDef def = b3DefaultSphericalJointDef();
            fill_base(def.base);
            def.enableConeLimit = true;
            def.coneAngle = clamp_cone_angle(definition.swing_limit);
            def.enableTwistLimit = true;
            def.lowerTwistAngle = clamp_joint_angle(std::min(definition.twist_lower, definition.twist_upper));
            def.upperTwistAngle = clamp_joint_angle(std::max(definition.twist_lower, definition.twist_upper));
            part.joint = b3CreateSphericalJoint(world.id, &def);
            break;
        }
        case ragdoll_joint_type::hinge:
        {
            b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
            fill_base(def.base);
            def.enableLimit = true;
            def.lowerAngle = clamp_joint_angle(std::min(definition.twist_lower, definition.twist_upper));
            def.upperAngle = clamp_joint_angle(std::max(definition.twist_lower, definition.twist_upper));
            part.joint = b3CreateRevoluteJoint(world.id, &def);
            break;
        }
        case ragdoll_joint_type::fixed:
        {
            b3WeldJointDef def = b3DefaultWeldJointDef();
            fill_base(def.base);
            part.joint = b3CreateWeldJoint(world.id, &def);
            break;
        }
    }
    apply_part_friction(comp, definition, part);
}

void create_ragdoll_filters(box3d::world& world, ragdoll_component& comp, box3d::ragdoll& ragdoll)
{
    if(!comp.get_self_collision())
    {
        // The negative group keeps every pair apart already.
        return;
    }
    for(const auto& pair : comp.get_disabled_collisions())
    {
        const int a = comp.find_body(pair.bone_a);
        const int b = comp.find_body(pair.bone_b);
        if(a < 0 || b < 0 || a == b)
        {
            continue;
        }
        const b3BodyId body_a = ragdoll.parts[size_t(a)].body.body;
        const b3BodyId body_b = ragdoll.parts[size_t(b)].body.body;
        if(B3_IS_NULL(body_a) || B3_IS_NULL(body_b))
        {
            continue;
        }
        b3FilterJointDef def = b3DefaultFilterJointDef();
        def.base.bodyIdA = body_a;
        def.base.bodyIdB = body_b;
        ragdoll.filters.push_back(b3CreateFilterJoint(world.id, &def));
    }
}

/// The character's own controller and ragdoll ignore each other: the mover walks through its hitboxes and the proxy
/// never pushes the limbs.
void connect_ragdoll_controller(box3d::world& world, entt::handle owner, box3d::ragdoll& ragdoll)
{
    const auto controller = find_ragdoll_controller(owner);
    if(!controller)
    {
        return;
    }
    auto& cc = controller.get<box3d::character_controller>();
    cc.own_ragdoll = owner.entity();
    ragdoll.controller = controller.entity();
    if(B3_IS_NULL(cc.body) || !b3Body_IsValid(cc.body))
    {
        return;
    }
    for(const auto& part : ragdoll.parts)
    {
        if(B3_IS_NULL(part.body.body))
        {
            continue;
        }
        b3FilterJointDef def = b3DefaultFilterJointDef();
        def.base.bodyIdA = cc.body;
        def.base.bodyIdB = part.body.body;
        ragdoll.filters.push_back(b3CreateFilterJoint(world.id, &def));
    }
}

void build_ragdoll(box3d::world& world, entt::handle owner, ragdoll_component& comp)
{
    destroy_ragdoll(owner);
    auto& runtime = comp.get_runtime();
    if(!runtime.is_prepared || runtime.bodies.size() != comp.get_bodies().size())
    {
        return;
    }
    auto& ragdoll = owner.emplace<box3d::ragdoll>();
    ragdoll.group = ++world.ragdoll_groups;
    ragdoll.parts.resize(comp.get_bodies().size());
    for(const int index : runtime.write_order)
    {
        create_ragdoll_part(world, owner, comp, ragdoll, size_t(index));
    }
    for(const int index : runtime.write_order)
    {
        create_ragdoll_joint(world, owner, comp, ragdoll, size_t(index));
    }
    create_ragdoll_filters(world, comp, ragdoll);
    connect_ragdoll_controller(world, owner, ragdoll);
    runtime.is_built = true;
    runtime.bodies_changed = true;
    comp.set_needs_rebuild(false);
    comp.set_settings_dirty(false);
}

void apply_ragdoll_settings(ragdoll_component& comp, box3d::ragdoll& ragdoll, entt::handle owner)
{
    const b3Filter filter = make_ragdoll_filter(comp, ragdoll, owner);
    for(size_t i = 0; i < ragdoll.parts.size(); ++i)
    {
        auto& part = ragdoll.parts[i];
        if(B3_IS_NULL(part.body.body))
        {
            continue;
        }
        b3Body_SetLinearDamping(part.body.body, comp.get_linear_damping());
        b3Body_SetAngularDamping(part.body.body, comp.get_angular_damping());
        b3Body_SetBullet(part.body.body, comp.is_continuous());
        if(i < comp.get_bodies().size())
        {
            apply_part_friction(comp, comp.get_bodies()[i], part);
        }
        for(const b3ShapeId shape : part.body.shapes)
        {
            if(b3Shape_IsValid(shape))
            {
                b3Shape_SetFilter(shape, filter, true);
            }
        }
    }
    comp.set_settings_dirty(false);
    comp.get_runtime().bodies_changed = true;
}

/// Whether a simulating body follows the animation through its joint spring.
auto has_local_drive(const ragdoll_component& comp, const ragdoll_body_runtime& body) -> bool
{
    const auto& drive = comp.get_drive();
    const bool is_driven = comp.get_state() == ragdoll_state::powered || comp.get_state() == ragdoll_state::animated;
    return is_driven && body.simulated && body.parent >= 0 && drive.local_frequency > 0.0f &&
           comp.get_runtime().drive_strength > 0.0f;
}

auto has_world_drive(const ragdoll_component& comp, const ragdoll_body_runtime& body) -> bool
{
    const auto& drive = comp.get_drive();
    return comp.get_state() == ragdoll_state::powered && body.simulated && drive.world_frequency > 0.0f &&
           comp.get_runtime().drive_strength > 0.0f;
}

void set_part_dynamic(ragdoll_component& comp, box3d::ragdoll_part& part, ragdoll_body_runtime& body, size_t index)
{
    const bool dynamic = body.simulated;
    if(part.is_dynamic == dynamic || B3_IS_NULL(part.body.body))
    {
        return;
    }
    part.is_dynamic = dynamic;
    if(dynamic)
    {
        b3Body_SetType(part.body.body, b3_dynamicBody);
        // A type change recomputes the mass from the shape densities.
        apply_body_mass(part.body.body, comp.get_bodies()[index].mass);
        b3Body_EnableSleep(part.body.body, true);
        b3Body_SetLinearVelocity(part.body.body, box3d::to_b3(body.anim_linear_velocity));
        b3Body_SetAngularVelocity(part.body.body, box3d::to_b3(body.anim_angular_velocity));
        b3Body_SetAwake(part.body.body, true);
        const math::transform pose = make_pose(b3Body_GetTransform(part.body.body));
        body.world = pose;
        body.world_previous = pose;
        body.has_physics_pose = true;
    }
    else
    {
        b3Body_SetType(part.body.body, b3_kinematicBody);
        b3Body_EnableSleep(part.body.body, false);
        body.has_physics_pose = false;
    }
}

void set_part_local_drive(const ragdoll_component& comp, box3d::ragdoll_part& part, const ragdoll_body_runtime& body, size_t index)
{
    if(B3_IS_NULL(part.joint) || !b3Joint_IsValid(part.joint))
    {
        return;
    }
    const auto& drive = comp.get_drive();
    const bool enabled = has_local_drive(comp, body);
    // Stiffness grows with the square of the frequency, so the root of the strength scales it evenly.
    const float hertz = drive.local_frequency * comp.get_bodies()[index].drive_scale *
                        std::sqrt(comp.get_runtime().drive_strength);
    switch(part.joint_type)
    {
        case ragdoll_joint_type::ball:
            b3SphericalJoint_EnableSpring(part.joint, enabled);
            b3SphericalJoint_SetSpringHertz(part.joint, hertz);
            b3SphericalJoint_SetSpringDampingRatio(part.joint, drive.local_damping_ratio);
            break;
        case ragdoll_joint_type::hinge:
            b3RevoluteJoint_EnableSpring(part.joint, enabled);
            b3RevoluteJoint_SetSpringHertz(part.joint, hertz);
            b3RevoluteJoint_SetSpringDampingRatio(part.joint, drive.local_damping_ratio);
            break;
        case ragdoll_joint_type::fixed:
            break;
    }
}

void set_part_world_drive(box3d::world& world,
                          const ragdoll_component& comp,
                          box3d::ragdoll_part& part,
                          const ragdoll_body_runtime& body,
                          size_t index)
{
    const bool enabled = has_world_drive(comp, body) && B3_IS_NON_NULL(part.body.body);
    if(!enabled)
    {
        destroy_joint_id(part.anchor_joint);
        if(B3_IS_NON_NULL(part.anchor) && b3Body_IsValid(part.anchor))
        {
            b3DestroyBody(part.anchor);
        }
        part.anchor = b3_nullBodyId;
        return;
    }
    if(B3_IS_NULL(part.anchor))
    {
        b3BodyDef def = b3DefaultBodyDef();
        def.type = b3_kinematicBody;
        const b3WorldTransform pose = rigid_pose(body.anim_world);
        def.position = pose.p;
        def.rotation = pose.q;
        def.enableSleep = false;
        part.anchor = b3CreateBody(world.id, &def);
        b3MotorJointDef motor = b3DefaultMotorJointDef();
        motor.base.bodyIdA = part.anchor;
        motor.base.bodyIdB = part.body.body;
        motor.base.collideConnected = false;
        part.anchor_joint = b3CreateMotorJoint(world.id, &motor);
    }
    const auto& drive = comp.get_drive();
    const float strength = comp.get_runtime().drive_strength * comp.get_bodies()[index].drive_scale;
    const float hertz = drive.world_frequency * std::sqrt(strength);
    b3MotorJoint_SetLinearHertz(part.anchor_joint, hertz);
    b3MotorJoint_SetAngularHertz(part.anchor_joint, hertz);
    b3MotorJoint_SetLinearDampingRatio(part.anchor_joint, drive.world_damping_ratio);
    b3MotorJoint_SetAngularDampingRatio(part.anchor_joint, drive.world_damping_ratio);
    b3MotorJoint_SetMaxSpringForce(part.anchor_joint, drive.world_max_force * strength);
    b3MotorJoint_SetMaxSpringTorque(part.anchor_joint, drive.world_max_torque * strength);
}

void apply_ragdoll_bodies(box3d::world& world, ragdoll_component& comp, box3d::ragdoll& ragdoll)
{
    auto& runtime = comp.get_runtime();
    for(size_t i = 0; i < ragdoll.parts.size() && i < runtime.bodies.size(); ++i)
    {
        set_part_dynamic(comp, ragdoll.parts[i], runtime.bodies[i], i);
    }
    for(size_t i = 0; i < ragdoll.parts.size() && i < runtime.bodies.size(); ++i)
    {
        set_part_local_drive(comp, ragdoll.parts[i], runtime.bodies[i], i);
        set_part_world_drive(world, comp, ragdoll.parts[i], runtime.bodies[i], i);
    }
    runtime.bodies_changed = false;
}

/// The rotation of a body against its parent that the animation shows, in Box3D's joint terms.
auto get_drive_target(const box3d::ragdoll_part& part, const ragdoll_body_runtime& parent, const ragdoll_body_runtime& body)
    -> math::quat
{
    const math::quat frame_a = math::normalize(parent.anim_world.get_rotation()) * box3d::from_b3(part.frame_a.q);
    const math::quat frame_b = math::normalize(body.anim_world.get_rotation()) * box3d::from_b3(part.frame_b.q);
    return math::normalize(glm::inverse(frame_a) * frame_b);
}

/**
 * @brief Where a kinematic body goes: the animated pose, or, while its bone blends back from where the body stopped
 *        simulating, the blend that is shown, so the hitbox follows what is drawn instead of sweeping to the
 *        animation in one step (and knocking what it passes through).
 */
auto get_kinematic_target(const ragdoll_body_runtime& body) -> math::transform
{
    if(body.weight <= 0.0f)
    {
        return body.anim_world;
    }
    math::transform target = body.anim_world;
    target.set_position(math::mix(body.anim_world.get_position(), body.snapshot_world.get_position(), body.weight));
    target.set_rotation(math::normalize(
        math::slerp(body.anim_world.get_rotation(), body.snapshot_world.get_rotation(), body.weight)));
    return target;
}

void follow_ragdoll_animation(box3d::world& world, ragdoll_component& comp, box3d::ragdoll& ragdoll, float dt)
{
    auto& runtime = comp.get_runtime();
    for(size_t i = 0; i < ragdoll.parts.size() && i < runtime.bodies.size(); ++i)
    {
        auto& part = ragdoll.parts[i];
        const auto& body = runtime.bodies[i];
        if(B3_IS_NULL(part.body.body) || !body.has_anim_pose)
        {
            continue;
        }
        if(!part.is_dynamic)
        {
            b3Body_SetTargetTransform(part.body.body, rigid_pose(get_kinematic_target(body)), dt, true);
            restart_sleep_while_moving(world, part.body.body);
            continue;
        }
        if(B3_IS_NON_NULL(part.anchor))
        {
            b3Body_SetTargetTransform(part.anchor, rigid_pose(body.anim_world), dt, true);
        }
        if(body.parent < 0 || !has_local_drive(comp, body) || B3_IS_NULL(part.joint))
        {
            continue;
        }
        const math::quat target = get_drive_target(part, runtime.bodies[size_t(body.parent)], body);
        if(part.joint_type == ragdoll_joint_type::ball)
        {
            b3SphericalJoint_SetTargetRotation(part.joint, box3d::to_b3(target));
        }
        else if(part.joint_type == ragdoll_joint_type::hinge)
        {
            const float angle = 2.0f * std::atan2(target.z, target.w);
            const float wrapped = std::remainder(angle, math::two_pi<float>());
            b3RevoluteJoint_SetTargetAngle(part.joint, math::clamp(wrapped, b3RevoluteJoint_GetLowerLimit(part.joint),
                                                                   b3RevoluteJoint_GetUpperLimit(part.joint)));
        }
    }
}

void apply_ragdoll_impulses(ragdoll_component& comp, box3d::ragdoll& ragdoll)
{
    auto& runtime = comp.get_runtime();
    for(const auto& impulse : runtime.impulses)
    {
        if(impulse.body < 0 || size_t(impulse.body) >= ragdoll.parts.size())
        {
            continue;
        }
        const auto& part = ragdoll.parts[size_t(impulse.body)];
        if(!part.is_dynamic || B3_IS_NULL(part.body.body))
        {
            continue;
        }
        if(impulse.is_velocity_change)
        {
            b3Body_SetLinearVelocity(part.body.body,
                                     b3Add(b3Body_GetLinearVelocity(part.body.body), box3d::to_b3(impulse.impulse)));
            b3Body_SetAngularVelocity(part.body.body,
                                      b3Add(b3Body_GetAngularVelocity(part.body.body),
                                            box3d::to_b3(impulse.angular_velocity_change)));
            b3Body_SetAwake(part.body.body, true);
        }
        else if(impulse.at_point)
        {
            b3Body_ApplyLinearImpulse(part.body.body, box3d::to_b3(impulse.impulse), box3d::to_b3_pos(impulse.point), true);
        }
        else
        {
            b3Body_ApplyLinearImpulseToCenter(part.body.body, box3d::to_b3(impulse.impulse), true);
        }
    }
    runtime.impulses.clear();
}

void teleport_ragdoll(ragdoll_component& comp, box3d::ragdoll& ragdoll)
{
    auto& runtime = comp.get_runtime();
    for(size_t i = 0; i < ragdoll.parts.size() && i < runtime.bodies.size(); ++i)
    {
        auto& part = ragdoll.parts[i];
        auto& body = runtime.bodies[i];
        if(B3_IS_NULL(part.body.body) || !body.has_anim_pose)
        {
            continue;
        }
        const b3WorldTransform pose = rigid_pose(body.anim_world);
        b3Body_SetTransform(part.body.body, pose.p, pose.q);
        b3Body_SetLinearVelocity(part.body.body, b3Vec3_zero);
        b3Body_SetAngularVelocity(part.body.body, b3Vec3_zero);
        if(B3_IS_NON_NULL(part.anchor))
        {
            b3Body_SetTransform(part.anchor, pose.p, pose.q);
        }
        body.world = body.anim_world;
        body.world_previous = body.anim_world;
    }
    runtime.teleport = false;
}

void sync_ragdoll(box3d::world& world, entt::handle owner, ragdoll_component& comp, float dt)
{
    auto& runtime = comp.get_runtime();
    if(!runtime.is_prepared)
    {
        // The model or its armature went: bodies left in the world would drift on with nothing to show them.
        destroy_ragdoll(owner);
        runtime.impulses.clear();
        return;
    }
    if(comp.needs_rebuild() || !runtime.is_built || !owner.all_of<box3d::ragdoll>())
    {
        build_ragdoll(world, owner, comp);
    }
    auto* ragdoll = owner.try_get<box3d::ragdoll>();
    if(ragdoll == nullptr)
    {
        return;
    }
    if(comp.are_settings_dirty())
    {
        apply_ragdoll_settings(comp, *ragdoll, owner);
    }
    if(runtime.bodies_changed)
    {
        apply_ragdoll_bodies(world, comp, *ragdoll);
    }
    if(runtime.teleport)
    {
        teleport_ragdoll(comp, *ragdoll);
    }
    follow_ragdoll_animation(world, comp, *ragdoll, dt);
    apply_ragdoll_impulses(comp, *ragdoll);
    comp.set_dirty(system_id, false);
}

void read_ragdoll(ragdoll_component& comp, box3d::ragdoll& ragdoll)
{
    auto& runtime = comp.get_runtime();
    for(size_t i = 0; i < ragdoll.parts.size() && i < runtime.bodies.size(); ++i)
    {
        const auto& part = ragdoll.parts[i];
        auto& body = runtime.bodies[i];
        if(B3_IS_NULL(part.body.body))
        {
            continue;
        }
        const math::transform pose = make_pose(b3Body_GetTransform(part.body.body));
        if(!part.is_dynamic)
        {
            // Where the hitbox is, for tools; the shown pose comes from the animation or the snapshot.
            body.world = pose;
            body.world_previous = pose;
            body.has_physics_pose = false;
            body.linear_velocity = body.anim_linear_velocity;
            body.angular_velocity = body.anim_angular_velocity;
            continue;
        }
        body.world_previous = body.has_physics_pose ? body.world : pose;
        body.world = pose;
        body.has_physics_pose = true;
        body.linear_velocity = box3d::from_b3(b3Body_GetLinearVelocity(part.body.body));
        body.angular_velocity = box3d::from_b3(b3Body_GetAngularVelocity(part.body.body));
    }
}

// --- shape queries -------------------------------------------------------------------------

/// The points and radius of a box, capsule or sphere placed at a pose, relative to @p origin.
struct query_proxy
{
    std::array<b3Vec3, 8> points{};
    int count{0};
    float radius{0.0f};

    auto get() const -> b3ShapeProxy
    {
        return {points.data(), count, radius};
    }
};

auto make_query_proxy(const physics_compound_shape& shape, const math::quat& rotation, query_proxy& proxy) -> bool
{
    const auto place = [&](const math::vec3& center, const math::vec3& shape_rotation, const math::vec3& point)
    {
        return box3d::to_b3(rotation * (center + unravel::make_shape_rotation(shape_rotation) * point));
    };
    if(const auto* box = hpp::get_if<physics_box_shape>(&shape.shape))
    {
        const math::vec3 half = math::max(box->extends * 0.5f, math::vec3{box3d::min_half_extent});
        for(int i = 0; i < box3d::box_corner_count; ++i)
        {
            proxy.points[size_t(i)] = place(box->center, box->rotation, get_box_corner(half, i));
        }
        proxy.count = box3d::box_corner_count;
        return true;
    }
    if(const auto* capsule = hpp::get_if<physics_capsule_shape>(&shape.shape))
    {
        const math::vec3 half{0.0f, capsule->length * 0.5f, 0.0f};
        proxy.points[0] = place(capsule->center, capsule->rotation, -half);
        proxy.points[1] = place(capsule->center, capsule->rotation, half);
        proxy.count = 2;
        proxy.radius = std::max(capsule->radius, box3d::min_half_extent);
        return true;
    }
    if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        proxy.points[0] = box3d::to_b3(rotation * sphere->center);
        proxy.count = 1;
        proxy.radius = std::max(sphere->radius, box3d::min_half_extent);
        return true;
    }
    return false;
}

auto shape_cast_closest(box3d::world& world,
                        const physics_compound_shape& shape,
                        const math::vec3& origin,
                        const math::quat& rotation,
                        const math::vec3& direction,
                        float max_distance,
                        int layer_mask,
                        bool query_sensors) -> hpp::optional<raycast_hit>
{
    query_proxy proxy;
    if(!world.is_valid() || !make_query_proxy(shape, rotation, proxy))
    {
        return {};
    }
    box3d::world::closest_cast_context ctx;
    ctx.query_sensors = query_sensors;
    const b3ShapeProxy shape_proxy = proxy.get();
    const math::vec3 translation = direction * max_distance;
    b3World_CastShape(world.id,
                      box3d::to_b3_pos(origin),
                      &shape_proxy,
                      box3d::to_b3(translation),
                      box3d::make_query_filter(layer_mask),
                      &box3d::world::closest_cast_callback,
                      &ctx);
    return box3d::world::make_closest_hit(ctx, direction, max_distance);
}

auto shape_cast_all(box3d::world& world,
                    const physics_compound_shape& shape,
                    const math::vec3& origin,
                    const math::quat& rotation,
                    const math::vec3& direction,
                    float max_distance,
                    int layer_mask,
                    bool query_sensors) -> physics_vector<raycast_hit>
{
    query_proxy proxy;
    if(!world.is_valid() || !make_query_proxy(shape, rotation, proxy))
    {
        return {};
    }
    box3d::world::all_casts_context ctx;
    ctx.query_sensors = query_sensors;
    const b3ShapeProxy shape_proxy = proxy.get();
    const math::vec3 translation = direction * max_distance;
    b3World_CastShape(world.id,
                      box3d::to_b3_pos(origin),
                      &shape_proxy,
                      box3d::to_b3(translation),
                      box3d::make_query_filter(layer_mask),
                      &box3d::world::all_casts_callback,
                      &ctx);
    return box3d::world::make_sorted_hits(ctx, direction, max_distance);
}

auto shape_overlap(box3d::world& world,
                   const physics_compound_shape& shape,
                   const math::vec3& origin,
                   const math::quat& rotation,
                   int layer_mask,
                   bool query_sensors) -> physics_vector<entt::entity>
{
    query_proxy proxy;
    if(!world.is_valid() || !make_query_proxy(shape, rotation, proxy))
    {
        return {};
    }
    box3d::world::overlap_context ctx;
    ctx.query_sensors = query_sensors;
    const b3ShapeProxy shape_proxy = proxy.get();
    b3World_OverlapShape(world.id,
                         box3d::to_b3_pos(origin),
                         &shape_proxy,
                         box3d::make_query_filter(layer_mask),
                         &box3d::world::overlap_callback,
                         &ctx);
    return std::move(ctx.hits);
}

/// Tells the scripts of each joint that broke since the last dispatch, and of its bodies.
void dispatch_broken_joints(box3d::world& world, unravel::scene& scn)
{
    if(world.broken_joints.empty())
    {
        return;
    }
    // Scripts may break or destroy more joints while this runs: hand them a list of their own.
    const auto broken = std::move(world.broken_joints);
    world.broken_joints.clear();
    auto& scripting = engine::context().get_cached<script_system>();
    for(const auto& item : broken)
    {
        auto joint = scn.create_handle(item.entity);
        if(!joint)
        {
            continue;
        }
        auto* comp = joint.try_get<joint_component>();
        if(comp == nullptr)
        {
            continue;
        }
        scripting.on_joint_break(joint, comp->get_body_a(), comp->get_resolved_body_b(), item.force, item.torque);
    }
}

} // namespace
} // namespace box3d

namespace unravel
{

void box3d_backend::init()
{
    b3SetLogFcn(&box3d::log_callback);
    b3SetAssertFcn(&box3d::assert_callback);

    const b3Version version = b3GetVersion();
    APPLOG_INFO("box3d: version {}.{}.{}, {} precision",
                version.major,
                version.minor,
                version.revision,
                b3IsDoublePrecision() ? "double" : "single");
}

void box3d_backend::deinit()
{
}

void box3d_backend::on_create_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto& comp = entity.get<physics_component>();
        box3d::sync_physics_body(*world, comp, true);
    }
}

void box3d_backend::on_destroy_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        box3d::destroy_physics_body(*world, entity, true);
    }
}

void box3d_backend::on_destroy_box3d_rigidbody_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        box3d::destroy_physics_body(*world, entity, false);
    }
}

void box3d_backend::on_create_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto& comp = entity.get<character_controller_component>();
        box3d::sync_character_controller_body(*world, comp, true);
    }
}

void box3d_backend::on_destroy_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        box3d::destroy_character_controller_body(*world, entity, true);
    }
}

void box3d_backend::on_destroy_box3d_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        box3d::destroy_character_controller_body(*world, entity, false);
    }
}

void box3d_backend::on_create_joint_component(entt::registry& r, entt::entity e)
{
    // Made by the next sync, once both bodies exist.
}

void box3d_backend::on_destroy_joint_component(entt::registry& r, entt::entity e)
{
    if(r.ctx().find<box3d::world>() == nullptr)
    {
        return;
    }
    entt::handle entity(r, e);
    box3d::destroy_joint(entity);
    entity.remove<box3d::joint>();
}

void box3d_backend::on_destroy_box3d_joint_component(entt::registry& r, entt::entity e)
{
    if(r.ctx().find<box3d::world>() == nullptr)
    {
        return;
    }
    if(auto* joint = r.try_get<box3d::joint>(e))
    {
        box3d::destroy_joint_id(joint->id);
    }
}

void box3d_backend::on_create_ragdoll_component(entt::registry& r, entt::entity e)
{
    // Built by the next sync from the animated pose the physics system captures.
}

void box3d_backend::on_destroy_ragdoll_component(entt::registry& r, entt::entity e)
{
    auto* world = r.ctx().find<box3d::world>();
    if(world == nullptr)
    {
        return;
    }
    entt::handle entity(r, e);
    world->release_contacts_for(entity, contact_end_reason::other_disabled, !scene::is_destroy_suppressed());
    box3d::destroy_ragdoll(entity);
}

void box3d_backend::on_destroy_box3d_ragdoll_component(entt::registry& r, entt::entity e)
{
    // A destroyed entity loses this component before its ragdoll_component (entt empties the newest pools
    // first), so the bodies are released here rather than by the ragdoll_component's hook.
    if(r.ctx().find<box3d::world>() == nullptr)
    {
        return;
    }
    if(auto* ragdoll = r.try_get<box3d::ragdoll>(e))
    {
        box3d::release_ragdoll(entt::handle(r, e), *ragdoll);
    }
}

void box3d_backend::move_character(character_controller_component& comp, const math::vec3& displacement)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc)
    {
        return;
    }
    // Accumulate for the next step; consumed exactly once so catch-up steps do not
    // re-apply it.
    cc->pending_displacement += displacement;
}

void box3d_backend::jump_character(character_controller_component& comp, const math::vec3& direction)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc || !cc->grounded)
    {
        return;
    }
    cc->velocity = direction;
    cc->grounded = false;
    cc->jumping = true;
}

void box3d_backend::apply_impulse_character(character_controller_component& comp, const math::vec3& impulse)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc)
    {
        return;
    }
    // The controller has no mass; an impulse is a velocity change.
    cc->velocity += impulse;
}

void box3d_backend::warp_character(character_controller_component& comp, const math::vec3& position)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc)
    {
        return;
    }
    // The transform moves too: a script that also turns the character this frame dirties the transform, and the
    // next sync then places the controller where the transform is, which must already be the warp target.
    if(auto* transform = owner.try_get<transform_component>())
    {
        transform->set_position_global(position);
    }
    box3d::set_character_pose(*cc, position + comp.get_center(), cc->rotation);
}

void box3d_backend::set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc)
    {
        return;
    }
    cc->velocity = velocity;
}

void box3d_backend::sync_character_runtime_state(character_controller_component& comp)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<box3d::character_controller>();
    if(!cc)
    {
        return;
    }
    comp.set_grounded(cc->grounded);
    comp.set_velocity_internal(cc->velocity);
}

void box3d_backend::on_pre_destroy_entity(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world == nullptr)
    {
        return;
    }

    entt::handle entity(r, e);

    // The fast path the whole design exists to protect: one compare and out for a body
    // that owes nothing.
    auto* links = box3d::find_contact_links(entity);
    if(links == nullptr || links->flush_pending == 0)
    {
        return;
    }

    world->release_contacts_for(entity, contact_end_reason::other_destroyed, true);
}

void box3d_backend::on_create_active_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);
        if(auto* body = entity.try_get<box3d::rigidbody>())
        {
            box3d::set_rigidbody_active(*body, true);
        }
        if(auto* cc = entity.try_get<box3d::character_controller>())
        {
            if(b3Body_IsValid(cc->body) && !b3Body_IsEnabled(cc->body))
            {
                b3Body_Enable(cc->body);
            }
        }
    }
}

void box3d_backend::on_destroy_active_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<box3d::world>();
    if(world)
    {
        entt::handle entity(r, e);

        // Deactivating pulls the body out of the world exactly like destroying does, so
        // it owes the same exits. See bullet_backend for why suppression tells the two
        // apart.
        const bool notify = !scene::is_destroy_suppressed();
        world->release_contacts_for(entity, contact_end_reason::other_disabled, notify);

        if(auto* body = entity.try_get<box3d::rigidbody>())
        {
            box3d::set_rigidbody_active(*body, false);
        }
        if(auto* cc = entity.try_get<box3d::character_controller>())
        {
            cc->touching.clear();
            if(b3Body_IsValid(cc->body) && b3Body_IsEnabled(cc->body))
            {
                b3Body_Disable(cc->body);
            }
        }
        // An inactive joint lets go; an active one is made again where its entity then stands.
        if(entity.all_of<box3d::joint>())
        {
            box3d::destroy_joint(entity);
        }
        if(entity.all_of<box3d::ragdoll>())
        {
            box3d::destroy_ragdoll(entity);
        }
    }
}

void box3d_backend::apply_explosion_force(physics_component& comp,
                                          float explosion_force,
                                          const math::vec3& explosion_position,
                                          float explosion_radius,
                                          float upwards_modifier,
                                          force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }

    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return;
    }

    auto* body = box3d::prepare_dynamic_body(*world, comp);
    if(!body)
    {
        return;
    }

    const math::vec3 body_position = box3d::from_b3_pos(b3Body_GetPosition(body->body));
    math::vec3 direction = body_position - explosion_position;
    const float distance = math::length(direction);

    if(distance > explosion_radius && explosion_radius > 0.0f)
    {
        return;
    }

    direction = distance > 0.0f ? direction / distance : math::vec3{0.0f};

    if(upwards_modifier != 0.0f)
    {
        direction.y += upwards_modifier;
        direction = math::normalize(direction);
    }

    const float attenuation = explosion_radius > 0.0f ? (1.0f - (distance / explosion_radius)) : 1.0f;
    const math::vec3 force = direction * explosion_force * attenuation;

    if(box3d::add_force(body->body, box3d::to_b3(force), mode))
    {
        comp.set_velocity_internal(box3d::from_b3(b3Body_GetLinearVelocity(body->body)));
    }
}

void box3d_backend::apply_force(physics_component& comp, const math::vec3& force, force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }

    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return;
    }

    auto* body = box3d::prepare_dynamic_body(*world, comp);
    if(!body)
    {
        return;
    }

    if(!box3d::add_force(body->body, box3d::to_b3(force), mode))
    {
        return;
    }

    // Cache only - do not dirty, or the next sync can fight the just-applied impulse.
    comp.set_velocity_internal(box3d::from_b3(b3Body_GetLinearVelocity(body->body)));
}

void box3d_backend::apply_force_at_position(physics_component& comp,
                                            const math::vec3& force,
                                            const math::vec3& position,
                                            force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic || math::dot(force, force) <= 0.0f)
    {
        return;
    }
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return;
    }
    auto* body = box3d::prepare_dynamic_body(*world, comp);
    if(!body)
    {
        return;
    }
    const b3Pos point = box3d::to_b3_pos(position);
    b3Vec3 value = box3d::to_b3(force);
    switch(mode)
    {
        case force_mode::acceleration:
            value = b3MulSV(b3Body_GetMass(body->body), value);
            [[fallthrough]];
        case force_mode::force:
            b3Body_ApplyForce(body->body, value, point, true);
            break;
        case force_mode::velocity_change:
            value = b3MulSV(b3Body_GetMass(body->body), value);
            [[fallthrough]];
        case force_mode::impulse:
            b3Body_ApplyLinearImpulse(body->body, value, point, true);
            break;
    }
    comp.set_velocity_internal(box3d::from_b3(b3Body_GetLinearVelocity(body->body)));
    comp.set_angular_velocity_internal(box3d::from_b3(b3Body_GetAngularVelocity(body->body)));
}

void box3d_backend::set_body_awake(physics_component& comp, bool awake)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }
    auto owner = comp.get_owner();
    if(auto* body = owner.try_get<box3d::rigidbody>(); body && b3Body_IsValid(body->body))
    {
        b3Body_SetAwake(body->body, awake);
        auto* world = box3d::find_box3d_world();
        if(awake && world != nullptr)
        {
            // A wake-up also restarts the count towards sleep, as in PhysX and Unity.
            box3d::restart_sleep_count(*world, body->body);
        }
        comp.set_sleeping_internal(!awake);
    }
}

void box3d_backend::set_gravity(const math::vec3& gravity)
{
    if(auto* world = box3d::find_box3d_world(); world && world->is_valid())
    {
        b3World_SetGravity(world->id, box3d::to_b3(gravity));
    }
}

auto box3d_backend::get_gravity() const -> math::vec3
{
    if(auto* world = box3d::find_box3d_world(); world && world->is_valid())
    {
        return box3d::from_b3(b3World_GetGravity(world->id));
    }
    return get_default_gravity();
}

void box3d_backend::explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask)
{
    auto* world = box3d::find_box3d_world();
    if(!world || !world->is_valid())
    {
        return;
    }
    b3ExplosionDef def = b3DefaultExplosionDef();
    def.maskBits = box3d::to_filter_bits(layer_mask);
    def.position = box3d::to_b3_pos(center);
    def.radius = std::max(radius, 0.0f);
    def.falloff = std::max(falloff, 0.0f);
    def.impulsePerArea = impulse_per_area;
    b3World_Explode(world->id, &def);
}

auto box3d_backend::shape_cast(const physics_compound_shape& shape,
                               const math::vec3& origin,
                               const math::quat& rotation,
                               const math::vec3& direction,
                               float max_distance,
                               int layer_mask,
                               bool query_sensors) -> hpp::optional<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return box3d::shape_cast_closest(*world, shape, origin, rotation, direction, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::shape_cast_all(const physics_compound_shape& shape,
                                   const math::vec3& origin,
                                   const math::quat& rotation,
                                   const math::vec3& direction,
                                   float max_distance,
                                   int layer_mask,
                                   bool query_sensors) -> physics_vector<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return box3d::shape_cast_all(*world, shape, origin, rotation, direction, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::shape_overlap(const physics_compound_shape& shape,
                                  const math::vec3& origin,
                                  const math::quat& rotation,
                                  int layer_mask,
                                  bool query_sensors) -> physics_vector<entt::entity>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return box3d::shape_overlap(*world, shape, origin, rotation, layer_mask, query_sensors);
}

auto box3d_backend::get_world_stats() const -> world_stats
{
    world_stats stats;
    auto* world = box3d::find_box3d_world();
    if(!world || !world->is_valid())
    {
        return stats;
    }
    const b3Counters counters = b3World_GetCounters(world->id);
    stats.body_count = counters.bodyCount;
    stats.awake_body_count = b3World_GetAwakeBodyCount(world->id);
    stats.shape_count = counters.shapeCount;
    stats.contact_count = counters.contactCount;
    stats.joint_count = counters.jointCount;
    stats.island_count = counters.islandCount;
    stats.step_ms = b3World_GetProfile(world->id).step;
    return stats;
}

void box3d_backend::apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }

    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return;
    }

    auto* body = box3d::prepare_dynamic_body(*world, comp);
    if(!body)
    {
        return;
    }

    if(!box3d::add_torque(body->body, box3d::to_b3(torque), mode))
    {
        return;
    }

    comp.set_angular_velocity_internal(box3d::from_b3(b3Body_GetAngularVelocity(body->body)));
}

void box3d_backend::clear_kinematic_velocities(physics_component& comp)
{
    if(comp.get_body_type() != rigidbody_type::kinematic)
    {
        return;
    }

    auto owner = comp.get_owner();
    if(auto* body = owner.try_get<box3d::rigidbody>(); body && b3Body_IsValid(body->body))
    {
        b3Body_SetLinearVelocity(body->body, b3Vec3_zero);
        b3Body_SetAngularVelocity(body->body, b3Vec3_zero);
        comp.set_velocity_internal(math::vec3{0.0f});
        comp.set_angular_velocity_internal(math::vec3{0.0f});
    }
}

auto box3d_backend::ray_cast(const math::vec3& origin,
                             const math::vec3& direction,
                             float max_distance,
                             int layer_mask,
                             bool query_sensors) -> hpp::optional<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return world->ray_cast_closest(origin, direction, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::ray_cast_all(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) -> physics_vector<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return world->ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::sphere_cast(const math::vec3& origin,
                                const math::vec3& direction,
                                float radius,
                                float max_distance,
                                int layer_mask,
                                bool query_sensors) -> hpp::optional<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return world->sphere_cast_closest(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::sphere_cast_all(const math::vec3& origin,
                                    const math::vec3& direction,
                                    float radius,
                                    float max_distance,
                                    int layer_mask,
                                    bool query_sensors) -> physics_vector<raycast_hit>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return world->sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto box3d_backend::sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
    -> physics_vector<entt::entity>
{
    auto* world = box3d::find_box3d_world();
    if(!world)
    {
        return {};
    }
    return world->sphere_overlap(origin, radius, layer_mask, query_sensors);
}

void box3d_backend::on_play_begin(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto& world = registry.ctx().emplace<box3d::world>(box3d::create_world());
    if(!world.is_valid())
    {
        APPLOG_ERROR("box3d: failed to create world");
    }

    registry.on_destroy<box3d::rigidbody>().connect<&on_destroy_box3d_rigidbody_component>();
    registry.on_destroy<box3d::character_controller>().connect<&on_destroy_box3d_cc_component>();
    registry.on_destroy<box3d::joint>().connect<&on_destroy_box3d_joint_component>();
    registry.on_destroy<box3d::ragdoll>().connect<&on_destroy_box3d_ragdoll_component>();
    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();

    // Connected only for the duration of play, so scene::destroy_entity finds an empty
    // signal in edit mode and skips its subtree walk entirely.
    on_pre_destroy(registry).connect<&on_pre_destroy_entity>();

    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::sync_physics_body(world, comp, true);
        });
    registry.view<character_controller_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::sync_character_controller_body(world, comp, true);
        });
    registry.view<joint_component>().each(
        [&](auto e, auto&& comp)
        {
            comp.get_state() = {};
            comp.set_needs_rebuild(true);
        });
    registry.view<ragdoll_component>().each(
        [&](auto e, auto&& comp)
        {
            comp.reset_runtime_state();
        });
}

void box3d_backend::on_play_end(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<box3d::world>();

    // Drop the whole graph before the bodies go. Play end is a bulk teardown, so no
    // exits are owed.
    world.clear_contacts();
    world.broken_joints.clear();

    registry.view<joint_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::destroy_joint(comp.get_owner());
            comp.get_owner().template remove<box3d::joint>();
            comp.get_state() = {};
        });
    registry.view<ragdoll_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::destroy_ragdoll(comp.get_owner());
            comp.reset_runtime_state();
        });

    registry.view<character_controller_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::destroy_character_controller_body(world, comp.get_owner(), true);
        });
    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            box3d::destroy_physics_body(world, comp.get_owner(), true);
        });

    on_pre_destroy(registry).disconnect<&on_pre_destroy_entity>();
    registry.on_destroy<box3d::joint>().disconnect<&on_destroy_box3d_joint_component>();
    registry.on_destroy<box3d::ragdoll>().disconnect<&on_destroy_box3d_ragdoll_component>();
    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();
    registry.on_destroy<box3d::character_controller>().disconnect<&on_destroy_box3d_cc_component>();
    registry.on_destroy<box3d::rigidbody>().disconnect<&on_destroy_box3d_rigidbody_component>();

    box3d::destroy_world(world);
    registry.ctx().erase<box3d::world>();
}

void box3d_backend::on_pause(rtti::context& ctx)
{
}

void box3d_backend::on_resume(rtti::context& ctx)
{
}

void box3d_backend::sync_to_physics(rtti::context& ctx, delta_t step_dt)
{
    APP_SCOPE_PERF("Physics/Box3D/Sync Transforms To Physics");
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<box3d::world>();
    const float dt = step_dt.count();

    registry.view<transform_component, physics_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
        {
            box3d::to_physics(world, transform, rigidbody, dt);
        });
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& cc_comp, auto&& active_comp)
        {
            box3d::to_physics_cc(world, transform, cc_comp);
        });
    // Joints and ragdolls after the bodies: a joint needs both of its bodies, a ragdoll its character's controller.
    registry.view<joint_component, active_component>().each(
        [&](auto e, auto&& joint, auto&& active_comp)
        {
            box3d::sync_joint(world, joint);
        });
    registry.view<ragdoll_component, active_component>().each(
        [&](auto e, auto&& ragdoll, auto&& active_comp)
        {
            box3d::sync_ragdoll(world, ragdoll.get_owner(), ragdoll, dt);
        });
}

void box3d_backend::simulate(delta_t step_dt)
{
    APP_SCOPE_PERF("Physics/Box3D/Simulate Step");
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<box3d::world>();
    if(!world.is_valid())
    {
        return;
    }

    const float dt = step_dt.count();

    // Movers step against the world as it stands, then hand their proxies a target so
    // the solver carries them (and anything they push) through the step.
    {
        APP_SCOPE_PERF("Physics/Box3D/Character Movers");
        registry.view<character_controller_component, box3d::character_controller, active_component>().each(
            [&](auto e, auto&& comp, auto&& cc, auto&& active_comp)
            {
                box3d::step_character(world, comp, cc, dt);
            });
    }

    int sub_steps = box3d::sub_step_count;
    if(ctx.has<settings>())
    {
        sub_steps = std::clamp(ctx.get<settings>().physics.sub_steps, 1, box3d::max_sub_step_count);
    }

    // Exactly one time step per call, like the Bullet backend.
    world.in_simulate = true;
    b3World_Step(world.id, dt, sub_steps);
    world.in_simulate = false;
    box3d::restore_sleep(world, registry);

    box3d::collect_broken_joints(world, ec.get_scene());
    world.last_step_seconds = dt;
}

void box3d_backend::sync_from_physics(rtti::context& ctx)
{
    APP_SCOPE_PERF("Physics/Box3D/Sync Transforms From Physics");
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<box3d::world>();

    registry.view<transform_component, physics_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
        {
            box3d::from_physics(transform, rigidbody);
        });
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& cc_comp, auto&& active_comp)
        {
            box3d::from_physics_cc(world, transform, cc_comp);
        });
    registry.view<joint_component, box3d::joint, active_component>().each(
        [&](auto e, auto&& comp, auto&& joint, auto&& active_comp)
        {
            box3d::read_joint_state(joint, comp, world.last_step_seconds);
        });
    registry.view<ragdoll_component, box3d::ragdoll, active_component>().each(
        [&](auto e, auto&& comp, auto&& ragdoll, auto&& active_comp)
        {
            box3d::read_ragdoll(comp, ragdoll);
        });
}

void box3d_backend::dispatch_contacts(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<box3d::world>();
    if(!world.is_valid())
    {
        return;
    }
    world.process_contacts();
    box3d::dispatch_broken_joints(world, ec.get_scene());
}

void box3d_backend::draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto* world = registry.ctx().find<box3d::world>();
    if(world && world->is_valid())
    {
        DebugDrawEncoderScopePush scope(dd.encoder);
        b3DebugDraw draw = box3d::make_debug_draw(dd);
        b3World_Draw(world->id, &draw, UINT64_MAX);
    }
}

void box3d_backend::draw_gizmo(rtti::context& ctx, physics_component& comp, const camera& cam, gfx::dd_raii& dd)
{
}

void box3d_backend::draw_gizmo(rtti::context& ctx,
                               character_controller_component& comp,
                               const camera& cam,
                               gfx::dd_raii& dd)
{
    auto owner = comp.get_owner();
    if(!owner || !owner.all_of<transform_component>())
    {
        return;
    }
    auto& transform = owner.get<transform_component>();
    const auto& p = transform.get_position_global();
    const auto& q = transform.get_rotation_global();
    float cylinder_half_height = (comp.get_height() - 2.0f * comp.get_radius()) * 0.5f;
    if(cylinder_half_height < 0.0f)
    {
        cylinder_half_height = 0.0f;
    }
    const auto center = p + comp.get_center();
    const math::vec3 up = q * math::vec3(0.0f, 1.0f, 0.0f);
    const auto top = center + up * cylinder_half_height;
    const auto bottom = center - up * cylinder_half_height;
    dd.encoder.setColor(0xff00ffff);
    dd.encoder.setWireframe(true);
    dd.encoder.drawCapsule({bottom.x, bottom.y, bottom.z}, {top.x, top.y, top.z}, comp.get_radius());
}

} // namespace unravel
