#include "bullet_backend.h"
#include "graphics/graphics.h"

#include <engine/defaults/defaults.h>
#include <engine/events.h>
#include <math/transform.hpp>

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/physics/backend/contact_graph.h>
#include <engine/physics/ecs/components/character_controller_component.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/settings.h>

#include <engine/profiler/profiler.h>

#include <concurrency/threading_mode.h>
#define BT_USE_SSE_IN_API
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>

#include <btBulletCollisionCommon.h>
#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <LinearMath/btGeometryUtil.h>

#include <logging/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

// #ifdef NDEBUG
#define BULLET_MT 1
// #endif

#ifdef BULLET_MT
#include "LinearMath/btQuickprof.h"
#include "LinearMath/btThreads.h"
#include <thread>
#endif

namespace bullet
{
namespace
{
bool enable_logging = false;

/// Meters (or about 1 degree of turn, per quaternion component) a dynamic body must move before its transform is
/// written: float noise from the Bullet round trip stays out of the transform.
constexpr float pose_write_epsilon = 0.009f;
/// Continuous collision starts once a step moves a body further than this share of its smallest extent.
constexpr float ccd_threshold_share_of_extent = 0.5f;
/// Meters: the smallest continuous collision threshold, so a sliver of a body still gets one.
constexpr float min_ccd_motion_threshold = 0.01f;
/// The swept sphere is this share of the threshold, so it stays inside the body.
constexpr float ccd_swept_sphere_share = 0.4f;
/// An explosion pushes a body like a sphere of this radius per meter of its bounds' diagonal: a quarter of a cube's
/// diagonal is close to half its side.
constexpr float explosion_radius_per_diagonal = 0.25f;
/// Meters treated as nothing in an explosion: a falloff that would divide by zero, or a body at the center, which is
/// pushed up.
constexpr float explosion_length_epsilon = 1e-4f;
/// Box3D rolls a hull on a quarter of its inner radius.
constexpr btScalar hull_rolling_radius_share = btScalar(0.25);
/// Box3D's choice of the helper axis for the radial scale of a capsule: the body's up axis, unless the capsule's axis is
/// near it.
constexpr float radial_helper_axis_limit = 0.9f;
/// Sides of the hull a cylinder becomes when a scale turns it into something Bullet's cylinder cannot hold; Box3D
/// always uses this hull.
constexpr int cylinder_hull_side_count = 16;
/// A scale counts as uniform while its components differ by less than this share of the largest.
constexpr float uniform_scale_tolerance = 1e-4f;
/// Degrees below which a child shape counts as not turned.
constexpr float unturned_shape_tolerance_degrees = 1e-3f;
/// Fewer points than this make no solid hull.
constexpr int min_hull_point_count = 4;
constexpr int box_corner_count = 8;
/// Below this the entity scale counts as unchanged and the shapes are not rebuilt.
constexpr float scale_epsilon = 1e-4f;

enum class manifold_type
{
    collision,
    sensor
};

enum class event_type
{
    enter,
    exit,
    stay
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

const btVector3 gravity_sun(btScalar(0), btScalar(-274), btScalar(0));
const btVector3 gravity_mercury(btScalar(0), btScalar(-3.7), btScalar(0));
const btVector3 gravity_venus(btScalar(0), btScalar(-8.87), btScalar(0));
const btVector3 gravity_earth(btScalar(0), btScalar(-9.8), btScalar(0));
const btVector3 gravity_mars(btScalar(0), btScalar(-3.72), btScalar(0));
const btVector3 gravity_jupiter(btScalar(0), btScalar(-24.79), btScalar(0));
const btVector3 gravity_saturn(btScalar(0), btScalar(-10.44), btScalar(0));
const btVector3 gravity_uranus(btScalar(0), btScalar(-8.69), btScalar(0));
const btVector3 gravity_neptune(btScalar(0), btScalar(-11.15), btScalar(0));
const btVector3 gravity_pluto(btScalar(0), btScalar(-0.62), btScalar(0));
const btVector3 gravity_moon(btScalar(0), btScalar(-1.625), btScalar(0));

auto to_bullet(const math::vec3& v) -> btVector3
{
    return {v.x, v.y, v.z};
}

auto from_bullet(const btVector3& v) -> math::vec3
{
    return {v.getX(), v.getY(), v.getZ()};
}

auto to_bullet(const math::quat& q) -> btQuaternion
{
    return {q.x, q.y, q.z, q.w};
}

auto from_bullet(const btQuaternion& q) -> math::quat
{
    math::quat r;
    r.x = q.getX();
    r.y = q.getY();
    r.z = q.getZ();
    r.w = q.getW();
    return r;
}

auto to_bx(const btVector3& data) -> bx::Vec3
{
    return {data.getX(), data.getY(), data.getZ()};
}

auto to_bx_color(const btVector3& in) -> uint32_t
{
#define COL32_R_SHIFT 0
#define COL32_G_SHIFT 8
#define COL32_B_SHIFT 16
#define COL32_A_SHIFT 24
#define COL32_A_MASK  0xFF000000

    uint32_t out = ((uint32_t)(in.getX() * 255.0f)) << COL32_R_SHIFT;
    out |= ((uint32_t)(in.getY() * 255.0f)) << COL32_G_SHIFT;
    out |= ((uint32_t)(in.getZ() * 255.0f)) << COL32_B_SHIFT;
    out |= ((uint32_t)(1.0f * 255.0f)) << COL32_A_SHIFT;
    return out;
}

class debugdraw : public btIDebugDraw
{
    int debug_mode_ = /*btIDebugDraw::DBG_DrawWireframe | */ btIDebugDraw::DBG_DrawContactPoints;
    DefaultColors our_colors_;
    gfx::dd_raii& dd_;
    std::unique_ptr<DebugDrawEncoderScopePush> scope_;

public:
    debugdraw(gfx::dd_raii& dd) : dd_(dd)
    {
    }

    void startLines()
    {
        if(!scope_)
        {
            scope_ = std::make_unique<DebugDrawEncoderScopePush>(dd_.encoder);
        }
    }

    auto getDefaultColors() const -> DefaultColors override
    {
        return our_colors_;
    }
    /// the default implementation for setDefaultColors has no effect. A derived class can implement it and store the
    /// colors.
    void setDefaultColors(const DefaultColors& colors) override
    {
        our_colors_ = colors;
    }

    void drawLine(const btVector3& from1, const btVector3& to1, const btVector3& color1) override
    {
        startLines();

        dd_.encoder.setColor(to_bx_color(color1));
        dd_.encoder.moveTo(to_bx(from1));
        dd_.encoder.lineTo(to_bx(to1));
    }

    void drawContactPoint(const btVector3& point_on_b,
                          const btVector3& normal_on_b,
                          btScalar distance,
                          int life_time,
                          const btVector3& color) override
    {
        drawLine(point_on_b, point_on_b + normal_on_b * distance, color);
        btVector3 ncolor(0, 0, 0);
        drawLine(point_on_b, point_on_b + normal_on_b * 0.1, ncolor);
    }

    void setDebugMode(int debugMode) override
    {
        debug_mode_ = debugMode;
    }

    auto getDebugMode() const -> int override
    {
        return debug_mode_;
    }

    void flushLines() override
    {
        scope_.reset();
    }

    void reportErrorWarning(const char* warningString) override
    {
    }

    void draw3dText(const btVector3& location, const char* textString) override
    {
    }
};

static constexpr int COMBINE_BITS = 2;
static constexpr int COMBINE_MASK = (1 << COMBINE_BITS) - 1; // 0b11
static constexpr int FRICTION_SHIFT = COMBINE_BITS;          // friction in bits [3..2]
static constexpr int RESTITUTION_SHIFT = 0;                  // bounce in bits [1..0]

inline int encode_combine_modes(unravel::combine_mode friction, unravel::combine_mode bounce)
{
    int f = (static_cast<int>(friction) & COMBINE_MASK) << FRICTION_SHIFT;
    int b = (static_cast<int>(bounce) & COMBINE_MASK) << RESTITUTION_SHIFT;
    return f | b;
}

inline unravel::combine_mode decode_friction_combine(int code)
{
    return static_cast<unravel::combine_mode>((code >> FRICTION_SHIFT) & COMBINE_MASK);
}

inline unravel::combine_mode decode_restitution_combine(int code)
{
    return static_cast<unravel::combine_mode>((code >> RESTITUTION_SHIFT) & COMBINE_MASK);
}

//------------------------------------------------------------------------------
// 2) Helper to pick a single combine-mode when two bodies collide.
//    If both bodies requested the same mode, we use that. Otherwise, default to Average.
//    You can adjust this tie-breaking however you like.
//------------------------------------------------------------------------------
static unravel::combine_mode pick_combine_mode(unravel::combine_mode modeA, unravel::combine_mode modeB)
{
    if(modeA == modeB)
    {
        return modeA;
    }
    // If only one of them left at default 0 (Multiply) and you want to treat that
    // differently, you could check for that here. For simplicity we go to Average any time
    // they differ:
    return unravel::combine_mode::average;
}

//------------------------------------------------------------------------------
// 3) The global callback that Bullet will call for each new contact.
//    We read userIndex2 from each body to decide how to combine their restitutions.
//------------------------------------------------------------------------------
static btScalar per_body_combine(const btCollisionObject* body0,
                                 const btCollisionObject* body1,
                                 btScalar e0,
                                 btScalar e1,
                                 unravel::combine_mode mode0,
                                 unravel::combine_mode mode1)
{
    // 3.3) Pick final combine mode:
    auto mode = pick_combine_mode(mode0, mode1);

    // 3.5) Compute combined restitution according to chosenMode:
    btScalar combined;
    switch(mode)
    {
        case unravel::combine_mode::multiply:
            combined = e0 * e1;
            break;

        case unravel::combine_mode::average:
            combined = (e0 + e1) * btScalar(0.5);
            break;

        case unravel::combine_mode::minimum:
            combined = btMin(e0, e1);
            break;

        case unravel::combine_mode::maximum:
            combined = btMax(e0, e1);
            break;

        default:
            combined = e0 * e1; // fallback if somehow we get out-of-range
            break;
    }

    // 3.7) Return true to indicate “we handled it.”
    return combined;
}

//--------------------------------------------------------------------------------------
// 1) Define your own combine‐functions (matching the CalculateCombinedCallback signature)
//--------------------------------------------------------------------------------------

static btScalar combined_restitution_callback(const btCollisionObject* body0, const btCollisionObject* body1)
{
    int raw_mode0 = body0->getUserIndex2();
    int raw_mode1 = body1->getUserIndex2();
    auto mode0 = decode_restitution_combine(raw_mode0);
    auto mode1 = decode_restitution_combine(raw_mode1);

    return per_body_combine(body0, body1, body0->getRestitution(), body1->getRestitution(), mode0, mode1);
}

static btScalar combined_friction_callback(const btCollisionObject* body0,
                                           const btCollisionObject* body1,
                                           btScalar f0,
                                           btScalar f1)
{
    int raw_mode0 = body0->getUserIndex2();
    int raw_mode1 = body1->getUserIndex2();
    auto mode0 = decode_restitution_combine(raw_mode0);
    auto mode1 = decode_restitution_combine(raw_mode1);

    auto friction = per_body_combine(body0, body1, f0, f1, mode0, mode1);
    const btScalar MAX_FRICTION = btScalar(10.);
    if(friction < -MAX_FRICTION)
        friction = -MAX_FRICTION;
    if(friction > MAX_FRICTION)
        friction = MAX_FRICTION;
    return friction;
}

static btScalar combined_friction_callback(const btCollisionObject* body0, const btCollisionObject* body1)
{
    auto f0 = body0->getFriction();
    auto f1 = body1->getFriction();
    return combined_friction_callback(body0, body1, f0, f1);
}

static btScalar combined_rolling_friction_callback(const btCollisionObject* body0, const btCollisionObject* body1)
{
    auto f0 = body0->getFriction() * body0->getRollingFriction();
    auto f1 = body1->getFriction() * body1->getRollingFriction();
    return combined_friction_callback(body0, body1, f0, f1);
}

static btScalar combined_spinning_friction_callback(const btCollisionObject* body0, const btCollisionObject* body1)
{
    auto f0 = body0->getFriction() * body0->getSpinningFriction();
    auto f1 = body1->getFriction() * body1->getSpinningFriction();
    return combined_friction_callback(body0, body1, f0, f1);
}

/**
 * @class surface_rigid_body
 * @brief A rigid body with the contact surface Box3D keeps on its shapes: rolling resistance and surface velocity.
 *
 * Bullet calls add_surface_to_contact only for objects flagged CF_CUSTOM_MATERIAL_CALLBACK, and only
 * update_rigidbody_surface sets that flag, on this class, so the callback reads flagged objects as this class.
 */
ATTRIBUTE_ALIGNED16(class)
surface_rigid_body : public btRigidBody
{
public:
    BT_DECLARE_ALIGNED_ALLOCATOR();

    using btRigidBody::btRigidBody;

    /// Speed of the surface along its contacts, in the body's space: a conveyor belt.
    btVector3 surface_velocity{0, 0, 0};
    /// Resistance to rolling, 0..1; a contact takes the larger of its two bodies'.
    btScalar rolling_resistance{0};
};

/// The surface of a flagged body; null for every other object.
auto find_surface(const btCollisionObject& object) -> const surface_rigid_body*
{
    if((object.getCollisionFlags() & btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK) == 0)
    {
        return nullptr;
    }
    return static_cast<const surface_rigid_body*>(&object);
}

/**
 * @brief The radius Box3D rolls a shape on: a sphere's or a capsule's own, a quarter of a hull's inner radius (here
 *        the smallest half extent of its bounds), and none for a mesh triangle.
 */
auto get_rolling_radius(const btCollisionShape& shape) -> btScalar
{
    switch(shape.getShapeType())
    {
        case SPHERE_SHAPE_PROXYTYPE:
            return static_cast<const btSphereShape&>(shape).getRadius();
        case CAPSULE_SHAPE_PROXYTYPE:
            return static_cast<const btCapsuleShape&>(shape).getRadius();
        case TRIANGLE_SHAPE_PROXYTYPE:
            return btScalar(0);
        default:
            break;
    }
    if(!shape.isConvex())
    {
        return btScalar(0);
    }
    btVector3 aabb_min;
    btVector3 aabb_max;
    shape.getAabb(btTransform::getIdentity(), aabb_min, aabb_max);
    const btVector3 half_extents = (aabb_max - aabb_min) * btScalar(0.5);
    return hull_rolling_radius_share * half_extents[half_extents.minAxis()];
}

/**
 * @brief Gives a contact the larger rolling resistance of its bodies on the larger radius of its shapes, as Box3D does.
 *
 * Box3D resists every turn of one body against the other; Bullet splits that into two rolling rows and a spinning row,
 * so both get the value. Bullet also caps each row at the value itself, which lets heavy bodies roll further than on
 * Box3D.
 */
void apply_rolling_resistance(btManifoldPoint& point,
                              const btCollisionObjectWrapper& a,
                              const btCollisionObjectWrapper& b)
{
    const auto* surface_a = find_surface(*a.getCollisionObject());
    const auto* surface_b = find_surface(*b.getCollisionObject());
    const btScalar resistance = btMax(surface_a ? surface_a->rolling_resistance : btScalar(0),
                                      surface_b ? surface_b->rolling_resistance : btScalar(0));
    if(resistance <= btScalar(0))
    {
        return;
    }
    const btScalar radius =
        btMax(get_rolling_radius(*a.getCollisionShape()), get_rolling_radius(*b.getCollisionShape()));
    point.m_combinedRollingFriction = resistance * radius;
    point.m_combinedSpinningFriction = resistance * radius;
}

/// How fast a body's surface moves in the world, on top of the body's own motion.
auto get_world_surface_velocity(const btCollisionObject& object) -> btVector3
{
    const auto* surface = find_surface(object);
    return surface ? object.getWorldTransform().getBasis() * surface->surface_velocity : btVector3(0, 0, 0);
}

auto get_point_velocity(const btCollisionObject& object, const btVector3& point) -> btVector3
{
    const auto* body = btRigidBody::upcast(&object);
    return body ? body->getVelocityInLocalPoint(point - body->getCenterOfMassPosition()) : btVector3(0, 0, 0);
}

auto remove_normal_part(const btVector3& value, const btVector3& normal) -> btVector3
{
    return value - normal * normal.dot(value);
}

/**
 * @brief Makes the bodies' surface velocities carry what touches them, as Box3D's tangent velocity does.
 *
 * With BT_CONTACT_FLAG_LATERAL_FRICTION_INITIALIZED on a point (and friction direction caching in the solver), Bullet's
 * friction drives the sliding of body a against body b along m_lateralFrictionDir1 to m_contactMotion1. The direction
 * taken is the sliding still off the target, so one friction row removes all of it, as Bullet's own row along the
 * sliding does for a target of zero. A point Bullet keeps from an earlier step keeps the direction it got then.
 */
void apply_surface_velocity(btManifoldPoint& point, const btCollisionObject& a, const btCollisionObject& b)
{
    const btVector3& normal = point.m_normalWorldOnB;
    const btVector3 target = remove_normal_part(get_world_surface_velocity(b) - get_world_surface_velocity(a), normal);
    if(target.fuzzyZero())
    {
        return;
    }
    const btVector3 sliding = remove_normal_part(get_point_velocity(a, point.getPositionWorldOnA()) -
                                                     get_point_velocity(b, point.getPositionWorldOnB()),
                                                 normal);
    const btVector3 error = sliding - target;
    const btVector3 direction = error.fuzzyZero() ? target.normalized() : error.normalized();
    point.m_lateralFrictionDir1 = direction;
    point.m_lateralFrictionDir2 = direction.cross(normal);
    point.m_contactMotion1 = target.dot(point.m_lateralFrictionDir1);
    point.m_contactMotion2 = target.dot(point.m_lateralFrictionDir2);
    point.m_contactPointFlags |= BT_CONTACT_FLAG_LATERAL_FRICTION_INITIALIZED;
}

/**
 * @brief Bullet's contact-added callback: the rolling resistance and the surface velocity of a contact.
 *
 * Bullet calls it for every point it finds on a pair with a flagged body, each step, from the collision threads, so it
 * only reads the bodies and writes the point. The first wrapper is the manifold's body a. The result is ignored.
 */
auto add_surface_to_contact(btManifoldPoint& point,
                            const btCollisionObjectWrapper* wrapper_a,
                            int /*part_a*/,
                            int /*index_a*/,
                            const btCollisionObjectWrapper* wrapper_b,
                            int /*part_b*/,
                            int /*index_b*/) -> bool
{
    apply_rolling_resistance(point, *wrapper_a, *wrapper_b);
    apply_surface_velocity(point, *wrapper_a->getCollisionObject(), *wrapper_b->getCollisionObject());
    return true;
}

void override_combine_callbacks()
{
    // Restitution:
    gCalculateCombinedRestitutionCallback = combined_restitution_callback;

    // Friction:
    gCalculateCombinedFrictionCallback = combined_friction_callback;
    gCalculateCombinedRollingFrictionCallback = combined_rolling_friction_callback;
    gCalculateCombinedSpinningFrictionCallback = combined_spinning_friction_callback;

    // Rolling resistance and surface velocity, for bodies flagged CF_CUSTOM_MATERIAL_CALLBACK:
    gContactAddedCallback = add_surface_to_contact;
}

// Owned only when created via btCreateDefaultTaskScheduler(). Sequential/OpenMP/TBB/PPL
// getters return static instances and must never be deleted.
btITaskScheduler* owned_task_scheduler = nullptr;

void setup_task_scheduler()
{
#ifdef BULLET_MT
    if(btGetTaskScheduler())
    {
        return;
    }
    // The single-threaded mode starts no threads: the sequential scheduler runs every task on the caller.
    if(unravel::is_single_threaded())
    {
        btSetTaskScheduler(btGetSequentialTaskScheduler());
        return;
    }
    owned_task_scheduler = btCreateDefaultTaskScheduler();
    if(owned_task_scheduler)
    {
        btSetTaskScheduler(owned_task_scheduler);
        return;
    }
    // Always-available static fallback - not owned by us.
    btSetTaskScheduler(btGetSequentialTaskScheduler());
#endif
}

void cleanup_task_scheduler()
{
#ifdef BULLET_MT
    btSetTaskScheduler(nullptr);
    if(owned_task_scheduler)
    {
        delete owned_task_scheduler;
        owned_task_scheduler = nullptr;
    }
#endif
}

// --- Bullet internal profiling ----------------------------------------------------
//
// Every BT_PROFILE inside Bullet expands to a CProfileSample, whose constructor calls
// through btEnterProfileZone -> a function pointer that defaults to an empty function.
// That indirection is present even in this BT_NO_PROFILE build, so redirecting it
// surfaces stepSimulation's internal breakdown - predictUnconstraintMotion,
// dispatchAllCollisionPairs, solveConstraints, integrateTransforms - without rebuilding
// Bullet or defining BT_ENABLE_PROFILE.
//
// Cost when the profiler is not capturing is one relaxed atomic load per zone, because
// profile_begin gates on the process capture flag before touching thread-local state.

/// Bullet nests zones several levels deep, and the innermost ones fire per manifold and
/// per solver iteration. Recording those would swamp the timeline and cost more than it
/// measures, so only the top levels are kept - enough to attribute the step.
constexpr int max_profile_zone_depth = 4;

thread_local int t_profile_zone_depth = 0;
thread_local uint32_t t_profile_zone_stack[max_profile_zone_depth]{};

void enter_profile_zone(const char* name)
{
    const int depth = t_profile_zone_depth++;
    if(depth < max_profile_zone_depth)
    {
        // The lane name is only consumed the first time a given thread reports, so this
        // labels Bullet's worker threads and is ignored on the main thread, which
        // already has a lane.
        t_profile_zone_stack[depth] = unravel::profile_begin(name, "Physics Thread");
    }
}

void leave_profile_zone()
{
    if(t_profile_zone_depth <= 0)
    {
        return;
    }

    const int depth = --t_profile_zone_depth;
    if(depth < max_profile_zone_depth)
    {
        // profile_end ignores UINT32_MAX, which is what profile_begin returns while the
        // profiler is idle, so depth tracking stays balanced either way.
        unravel::profile_end(t_profile_zone_stack[depth]);
    }
}

void noop_enter_profile_zone(const char* /*name*/)
{
}

void noop_leave_profile_zone()
{
}

void install_profile_zone_hooks()
{
    btSetCustomEnterProfileZoneFunc(&enter_profile_zone);
    btSetCustomLeaveProfileZoneFunc(&leave_profile_zone);
}

void remove_profile_zone_hooks()
{
    btSetCustomEnterProfileZoneFunc(&noop_enter_profile_zone);
    btSetCustomLeaveProfileZoneFunc(&noop_leave_profile_zone);
}

auto get_entity_from_user_index(unravel::ecs& ec, int index) -> entt::handle
{
    auto id = static_cast<entt::entity>(index);

    return ec.get_scene().create_handle(id);
}

auto get_entity_id_from_user_index(int index) -> entt::entity
{
    auto& ctx = unravel::engine::context();
    auto& ec = ctx.get_cached<unravel::ecs>();
    auto id = static_cast<entt::entity>(index);

    return id;
}

auto resolve_entity_from_collision_object(const btCollisionObject* obj) -> entt::entity
{
    if(!obj)
    {
        return entt::null;
    }
    // Character controllers use ghost objects, not rigidbodies — always read userIndex.
    const int user_index = obj->getUserIndex();
    if(user_index < 0)
    {
        return entt::null;
    }
    return get_entity_id_from_user_index(user_index);
}

/**
 * @brief Contact policy for one participant.
 *
 * Character controllers carry no physics_component, so they take the default policy.
 * That keeps controller-vs-sensor working without authoring anything.
 */
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

/**
 * @brief The policy governing a pair, taken from whoever can actually hear about it.
 *
 * Sensor callbacks only ever reach the sensor, so only the sensor's policy decides
 * whether a sensor pair is worth tracking or reporting. Collision callbacks reach both
 * participants, so either one may ask for them - which is what makes an unscripted
 * projectile dying inside a scripted sensor still produce the sensor's exit.
 */
auto effective_contact_flags(entt::handle a, entt::handle b, manifold_type type) -> unravel::contact_event_flags
{
    const auto flags_a = resolve_contact_flags(a);
    if(type == manifold_type::sensor)
    {
        // `a` is always the sensor for sensor pairs.
        return flags_a;
    }
    return flags_a | resolve_contact_flags(b);
}

/**
 * @brief Whether an entity can receive a contact callback at all.
 *
 * process_manifold dispatches only into script_system, which needs a script_component
 * to hand the event to. Without one on either side, every bit of bookkeeping a tracked
 * pair costs - a pooled slot, a contact-point buffer, a per-step diff, an enter and an
 * exit - produces nothing observable.
 */
auto can_observe_contacts(entt::handle entity) -> bool
{
    if(entity)
    {
        auto* script = entity.try_get<unravel::script_component>();
        if(script)
        {
            return script->has_script_components();
        }
    }
    return false;
}

/**
 * @brief Whether anyone is listening for this pair.
 *
 * Sensor callbacks only ever reach the sensor, which is always the `a` side. Collision
 * callbacks reach both participants.
 *
 * A script attached after an overlap has already begun will not receive that overlap's
 * enter, and therefore never its exit either - consistent: enter and exit always pair up.
 * That is the price of not maintaining state for bodies nobody is watching.
 */
auto has_contact_listener(entt::handle a, entt::handle b, manifold_type type) -> bool
{
    if(can_observe_contacts(a))
    {
        return true;
    }
    return type == manifold_type::collision && can_observe_contacts(b);
}

/**
 * @brief Whether a pair should be tracked at all, given someone is listening.
 *
 * Gated on the flags alone, never on the script list: the defaults enable both event
 * kinds, so this only skips work somebody explicitly opted out of.
 */
auto should_track_contact(unravel::contact_event_flags flags, manifold_type type) -> bool
{
    const auto mask = (type == manifold_type::sensor) ? unravel::contact_event_flags::sensor_events
                                                      : unravel::contact_event_flags::collision_events;
    return unravel::has_any(flags, mask);
}

/**
 * @brief Whether removing a participant should synthesize an exit for this pair.
 */
auto should_flush_on_destroy(unravel::contact_event_flags flags, manifold_type type) -> bool
{
    const auto mask = (type == manifold_type::sensor)
                          ? unravel::contact_event_flags::sensor_exit_on_destroy
                          : unravel::contact_event_flags::collision_exit_on_destroy;
    return unravel::has_any(flags, mask);
}

template<typename Callback>
class filter_ray_callback : public Callback
{
public:
    int layer_mask;
    bool query_sensors;

    filter_ray_callback(const btVector3& from, const btVector3& to, int mask, bool sensors)
        : Callback(from, to)
        , layer_mask(mask)
        , query_sensors(sensors)
    {
    }

    // Override needsCollision to apply custom filtering
    auto needsCollision(btBroadphaseProxy* proxy0) const -> bool override
    {
        if(!Callback::needsCollision(proxy0))
        {
            return false;
        }

        // Apply layer mask filtering
        if((proxy0->m_collisionFilterGroup & layer_mask) == 0)
        {
            return false;
        }

        const auto* co = static_cast<const btCollisionObject*>(proxy0->m_clientObject);

        if(!query_sensors && (co->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
        {
            // Ignore sensors if querySensors is false
            return false;
        }

        return true;
    }
};

using filter_closest_ray_callback = filter_ray_callback<btCollisionWorld::ClosestRayResultCallback>;
using filter_all_hits_ray_callback = filter_ray_callback<btCollisionWorld::AllHitsRayResultCallback>;

// A custom callback that checks layer_mask and optionally ignores sensors.
class sphere_closest_convex_result_callback : public btCollisionWorld::ClosestConvexResultCallback
{
public:
    int layer_mask;
    bool query_sensors;

    sphere_closest_convex_result_callback(const btVector3& from, const btVector3& to, int layerMask, bool sensors)
        : btCollisionWorld::ClosestConvexResultCallback(from, to)
        , layer_mask(layerMask)
        , query_sensors(sensors)
    {
    }

    // If you’re using a filter callback approach, override needsCollision:
    bool needsCollision(btBroadphaseProxy* proxy0) const override
    {
        // First call base
        if(!btCollisionWorld::ClosestConvexResultCallback::needsCollision(proxy0))
            return false;

        if((proxy0->m_collisionFilterGroup & layer_mask) == 0)
        {
            return false;
        }

        // Then check layer mask
        const btCollisionObject* co = static_cast<const btCollisionObject*>(proxy0->m_clientObject);

        // Check for sensors if needed
        if(!query_sensors && (co->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
        {
            // Ignore sensors if querySensors is false
            return false;
        }

        return true;
    }
};

class sphere_all_convex_result_callback : public btCollisionWorld::ConvexResultCallback
{
public:
    int layer_mask;
    bool query_sensors;
    // We store all hits here
    struct hit_info
    {
        const btCollisionObject* object = nullptr;
        btVector3 point;
        btVector3 normal;
        btScalar fraction;
    };
    unravel::physics_vector<hit_info> hits;

    sphere_all_convex_result_callback(int layerMask, bool sensors) : layer_mask(layerMask), query_sensors(sensors)
    {
        m_closestHitFraction = btScalar(1.f);
    }

    // Called with each contact
    btScalar addSingleResult(btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace) override
    {
        // Store the fraction, normal, object, etc.
        hit_info hi;
        hi.object = convexResult.m_hitCollisionObject;
        // Despite its name, Bullet fills m_hitPointLocal with the contact point in the world.
        hi.point = convexResult.m_hitPointLocal;
        hi.fraction = convexResult.m_hitFraction;

        if(normalInWorldSpace)
            hi.normal = convexResult.m_hitNormalLocal;
        else
        {
            // transform normal
            hi.normal =
                convexResult.m_hitCollisionObject->getWorldTransform().getBasis() * convexResult.m_hitNormalLocal;
        }
        hits.push_back(hi);

        // Return fraction so bullet can continue
        // If we wanted to limit to the first or closest, we might do something else
        return m_closestHitFraction;
    }

    bool needsCollision(btBroadphaseProxy* proxy0) const override
    {
        if(!ConvexResultCallback::needsCollision(proxy0))
            return false;

        // Layer mask
        if((proxy0->m_collisionFilterGroup & layer_mask) == 0)
        {
            return false;
        }

        const btCollisionObject* co = static_cast<const btCollisionObject*>(proxy0->m_clientObject);
        // Sensors
        if(!query_sensors && (co->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
        {
            return false;
        }

        return true;
    }
};

struct sphere_overlap_callback : btCollisionWorld::ContactResultCallback
{
    btCollisionObject* me{};

    int layer_mask;
    bool query_sensors;

    unravel::physics_vector<btCollisionObject*> hits;

    sphere_overlap_callback(btCollisionObject* obj, int layerMask, bool sensors)
        : me(obj)
        , layer_mask(layerMask)
        , query_sensors(sensors)
    {
        m_closestDistanceThreshold = btScalar(1.f);
    }

    bool needsCollision(btBroadphaseProxy* proxy0) const override
    {
        if(!btCollisionWorld::ContactResultCallback::needsCollision(proxy0))
            return false;

        // Layer mask
        if((proxy0->m_collisionFilterGroup & layer_mask) == 0)
        {
            return false;
        }

        const btCollisionObject* co = static_cast<const btCollisionObject*>(proxy0->m_clientObject);
        // Sensors
        if(!query_sensors && (co->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE))
        {
            return false;
        }

        return true;
    }

    btScalar addSingleResult(btManifoldPoint&,
                             const btCollisionObjectWrapper* w0,
                             int,
                             int,
                             const btCollisionObjectWrapper* w1,
                             int,
                             int) override
    {
        const btCollisionObject* other =
            (w0->getCollisionObject() == me ? w1->getCollisionObject() : w0->getCollisionObject());
        hits.push_back(const_cast<btCollisionObject*>(other));
        return 0;
    }
};

struct rigidbody
{
    std::shared_ptr<surface_rigid_body> internal{};
    std::shared_ptr<btCollisionShape> internal_shape{};
    /// Entity scale baked into the shapes.
    math::vec3 shape_scale{1.0f, 1.0f, 1.0f};
    int collision_filter_group{};
    int collision_filter_mask{};
    unravel::contact_links links{};
};

struct character_controller
{
    std::shared_ptr<btPairCachingGhostObject> ghost{};
    std::shared_ptr<btCapsuleShape> shape{};
    std::shared_ptr<btKinematicCharacterController> controller{};
    int collision_filter_group{};
    int collision_filter_mask{};
    // Accumulated Move() displacement; flushed once per simulate then cleared.
    math::vec3 pending_displacement{};
    unravel::contact_links links{};
};

/**
 * @brief Resolves a participant's contact list head.
 *
 * A contact side is either a rigid body or a character controller ghost, and both
 * appear in the dispatcher's manifolds. Rigid bodies are checked first because they
 * vastly outnumber controllers.
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
    return nullptr;
}

struct world
{
    // Declared before dynamics_world so it outlives the world (pair cache holds a raw ptr).
    std::shared_ptr<btGhostPairCallback> ghost_pair_callback;
    std::shared_ptr<btBroadphaseInterface> broadphase;
    std::shared_ptr<btCollisionDispatcher> dispatcher;
    std::shared_ptr<btConstraintSolver> solver;
    std::shared_ptr<btConstraintSolverPoolMt> solver_pool;
    std::shared_ptr<btDefaultCollisionConfiguration> collision_config;
    std::shared_ptr<btDiscreteDynamicsWorld> dynamics_world;

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

    bool in_simulate{};
    float elapsed{};
    /// Milliseconds the last step took.
    float last_step_ms{};

    void add_rigidbody(const rigidbody& body)
    {
        if(body.internal->isInWorld())
        {
            return;
        }

        btAssert(in_simulate == false);

        dynamics_world->addRigidBody(body.internal.get(), body.collision_filter_group, body.collision_filter_mask);
    }

    void remove_rigidbody(const rigidbody& body)
    {
        if(!body.internal->isInWorld())
        {
            return;
        }
        btAssert(in_simulate == false);
        dynamics_world->removeRigidBody(body.internal.get());
    }

    void add_character_controller(const character_controller& cc)
    {
        btAssert(in_simulate == false);
        dynamics_world->addCollisionObject(cc.ghost.get(),
                                           cc.collision_filter_group,
                                           cc.collision_filter_mask);
        dynamics_world->addAction(cc.controller.get());
    }

    void remove_character_controller(const character_controller& cc)
    {
        btAssert(in_simulate == false);
        dynamics_world->removeAction(cc.controller.get());
        dynamics_world->removeCollisionObject(cc.ghost.get());
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
            {
                break;
            }
        }
    }

    // --- contact graph -----------------------------------------------------------

    /**
     * @brief Finds an existing slot for a pair by walking one participant's list.
     *
     * Sensor pairs are directional (the sensor is always `a`, and two overlapping
     * sensors report each other independently). Collision pairs are matched unordered,
     * because Bullet is free to present body0/body1 either way across manifold rebuilds.
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
     * Detach-then-notify: the graph is made fully consistent BEFORE any script runs, so
     * a callback that destroys further entities re-enters with `flush_pending == 0` on
     * this body and can neither observe nor re-emit what is being reported here.
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
                           // An enter that never reached script code must not be
                           // answered with an exit; as far as gameplay is concerned
                           // that pair never happened.
                           if(wants_events && slot.flush_on_destroy && slot.enter_dispatched)
                           {
                               // Moved, not copied: the slot is erased two lines down.
                               pending.push_back(std::move(slot.payload));

                               auto& cm = pending.back();
                               cm.event = event_type::exit;
                               // process_manifold states the reason relative to `a`.
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
     *
     * The policy is latched per pair at insert so the hot paths never read a component.
     * Changing the flags at runtime therefore has to walk that one body's list - O(k),
     * and only when the dirty flag says the flags actually moved. Pairs that are no
     * longer tracked at all are dropped silently; pairs that become trackable are
     * picked up by touch_pair on the next step, which re-tests the gate anyway.
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

    static void copy_manifold_points(btPersistentManifold* m,
                                     std::vector<unravel::manifold_point>& out,
                                     bool swap_sides)
    {
        const int count = m->getNumContacts();
        out.clear();
        out.reserve(static_cast<size_t>(count));

        for(int j = 0; j < count; ++j)
        {
            const auto& p = m->getContactPoint(j);

            unravel::manifold_point mp;
            if(swap_sides)
            {
                mp.a = from_bullet(p.getPositionWorldOnB());
                mp.b = from_bullet(p.getPositionWorldOnA());
                mp.normal_on_a = from_bullet(p.m_normalWorldOnB);
                mp.normal_on_b = -mp.normal_on_a;
            }
            else
            {
                mp.a = from_bullet(p.getPositionWorldOnA());
                mp.b = from_bullet(p.getPositionWorldOnB());
                mp.normal_on_b = from_bullet(p.m_normalWorldOnB);
                mp.normal_on_a = -mp.normal_on_b;
            }
            mp.impulse = p.getAppliedImpulse();
            mp.distance = p.getDistance();

            out.push_back(mp);
        }
    }

    /**
     * @brief Marks a pair as still touching, inserting and queueing it when it is new.
     *
     * Points are captured once, when the pair is created. Both natural and synthesized
     * exits therefore report the geometry of the moment the overlap began; refreshing
     * every pair every step would add a copy to the hot path for data almost nobody
     * reads.
     */
    void touch_pair(btPersistentManifold* m, entt::handle a, entt::handle b, manifold_type type, bool swap_sides)
    {
        // Cheapest gate first: one or two sparse-set probes and out. A pile of
        // unscripted debris never reaches the graph at all, so it costs no slots, no
        // contact-point buffers and no per-step diffing. If a listener disappears from
        // an already-tracked pair, skipping the refresh here lets the next sweep expire
        // the slot.
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
        copy_manifold_points(m, slot.payload.contacts, swap_sides);

        to_enter.push_back(slot_ref{id, slot.generation});
    }

    void process_manifolds()
    {
        APP_SCOPE_PERF("Physics/Bullet/Process Manifolds");
        auto& ctx = unravel::engine::context();
        auto& scripting = ctx.get_cached<unravel::script_system>();
        auto& ec = ctx.get_cached<unravel::ecs>();

        auto* dispatcher = dynamics_world->getDispatcher();
        const int nm = dispatcher->getNumManifolds();

        // Opening a step is a counter bump. Pairs seen during it record the stamp;
        // anything still carrying an older one has separated. Replaces a full pass over
        // every live contact just to clear a flag.
        const uint32_t stamp = contacts.advance_stamp();

        to_enter.clear();
        to_exit.clear();

        // Nothing in the scene can receive a contact callback, so there is nothing to
        // fold in. Existing pairs still expire below, which drains the graph.
        auto& registry = *ec.get_scene().registry;
        const bool any_listeners = !registry.storage<unravel::script_component>().empty();

        // Phase 1: fold every current manifold into the graph.
        for(int i = 0; any_listeners && i < nm; ++i)
        {
            auto* m = dispatcher->getManifoldByIndexInternal(i);
            if(m->getNumContacts() == 0)
            {
                continue;
            }

            const auto* obj_a = m->getBody0();
            const auto* obj_b = m->getBody1();
            const bool is_sensor_a = (obj_a->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) != 0;
            const bool is_sensor_b = (obj_b->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE) != 0;

            auto entity_a = get_entity_from_user_index(ec, obj_a->getUserIndex());
            auto entity_b = get_entity_from_user_index(ec, obj_b->getUserIndex());
            if(!entity_a || !entity_b)
            {
                continue;
            }

            if(is_sensor_a || is_sensor_b)
            {
                // Sensor pairs are directional - the sensor is always the `a` side,
                // and two overlapping sensors each report the other.
                if(is_sensor_a)
                {
                    touch_pair(m, entity_a, entity_b, manifold_type::sensor, false);
                }
                if(is_sensor_b)
                {
                    touch_pair(m, entity_b, entity_a, manifold_type::sensor, true);
                }
                continue;
            }

            touch_pair(m, entity_a, entity_b, manifold_type::collision, false);
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
                // Moved, not copied: the slot is freed on the next line, so its contact
                // buffer can be handed over instead of duplicated.
                to_exit.push_back(std::move(slot.payload));
                to_exit.back().event = event_type::exit;
                to_exit.back().reason = unravel::contact_end_reason::separated;
            }

            contacts.erase(id);
        }

        // Phase 3: dispatch. Script code called here may destroy entities, which frees
        // slots via release_contacts_for, so every queued enter is revalidated against
        // the slot generation before use. Exits carry their own copy and cannot dangle.
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

    void simulate(btScalar dt, btScalar fixed_time_step = 1.0 / 60.0, int max_subs_steps = 10)
    {
        APP_SCOPE_PERF("Physics/Bullet/Simulate Step");
        in_simulate = true;
        const auto start = std::chrono::steady_clock::now();
        dynamics_world->stepSimulation(dt, max_subs_steps, fixed_time_step);
        last_step_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
        in_simulate = false;
    }

    /**
     * @brief What every cast reports: the contact point, the surface normal, and how far the cast travelled along
     *        @p direction (unit length) before it touched. A cast that starts inside a shape can get no normal from
     *        Bullet; it reports the way back against the cast.
     */
    static auto make_hit(const btCollisionObject* object,
                         const btVector3& point,
                         const btVector3& normal,
                         btScalar fraction,
                         const math::vec3& direction,
                         float max_distance) -> hpp::optional<unravel::raycast_hit>
    {
        const entt::entity entity = resolve_entity_from_collision_object(object);
        if(entity == entt::null)
        {
            return {};
        }
        unravel::raycast_hit hit;
        hit.entity = entity;
        hit.point = from_bullet(point);
        hit.normal = normal.fuzzyZero() ? -direction : from_bullet(normal.normalized());
        hit.distance = float(fraction) * max_distance;
        return hit;
    }

    auto ray_cast_closest(const math::vec3& origin,
                          const math::vec3& direction,
                          float max_distance,
                          int layer_mask,
                          bool query_sensors) -> hpp::optional<unravel::raycast_hit>
    {
        if(!dynamics_world)
        {
            return {};
        }

        auto ray_origin = to_bullet(origin);
        auto ray_end = to_bullet(origin + direction * max_distance);

        filter_closest_ray_callback ray_callback(ray_origin, ray_end, layer_mask, query_sensors);

        ray_callback.m_flags |= btTriangleRaycastCallback::kF_UseGjkConvexCastRaytest;
        dynamics_world->rayTest(ray_origin, ray_end, ray_callback);
        if(!ray_callback.hasHit())
        {
            return {};
        }
        return make_hit(ray_callback.m_collisionObject,
                        ray_callback.m_hitPointWorld,
                        ray_callback.m_hitNormalWorld,
                        ray_callback.m_closestHitFraction,
                        direction,
                        max_distance);
    }

    auto ray_cast_all(const math::vec3& origin,
                      const math::vec3& direction,
                      float max_distance,
                      int layer_mask,
                      bool query_sensors) -> unravel::physics_vector<unravel::raycast_hit>
    {
        if(!dynamics_world)
        {
            return {};
        }

        auto ray_origin = to_bullet(origin);
        auto ray_end = to_bullet(origin + direction * max_distance);

        filter_all_hits_ray_callback ray_callback(ray_origin, ray_end, layer_mask, query_sensors);

        ray_callback.m_flags |= btTriangleRaycastCallback::kF_UseGjkConvexCastRaytest;
        dynamics_world->rayTest(ray_origin, ray_end, ray_callback);

        if(!ray_callback.hasHit())
        {
            return {};
        }

        unravel::physics_vector<unravel::raycast_hit> hits;

        // Collect all hits
        hits.reserve(ray_callback.m_hitPointWorld.size());
        for(int i = 0; i < ray_callback.m_hitPointWorld.size(); ++i)
        {
            if(auto hit = make_hit(ray_callback.m_collisionObjects[i],
                                   ray_callback.m_hitPointWorld[i],
                                   ray_callback.m_hitNormalWorld[i],
                                   ray_callback.m_hitFractions[i],
                                   direction,
                                   max_distance))
            {
                hits.push_back(*hit);
            }
        }
        return hits;
    }

    // Then the function itself
    auto sphere_cast_closest(const math::vec3& origin,
                             const math::vec3& direction,
                             float radius,
                             float max_distance,
                             int layer_mask,
                             bool query_sensors) -> hpp::optional<unravel::raycast_hit>
    {
        if(!dynamics_world)
        {
            return {};
        }

        // Convert origin, direction to bullet
        btVector3 btOrigin = to_bullet(origin);
        btVector3 btEnd = to_bullet(origin + direction * max_distance);

        // Create a temporary sphere shape
        // (We do *not* add this shape to the world, just use it for sweeping)
        btSphereShape shape(radius);
        // shape.setMargin(0.f); // optionally set margin=0

        // Build transform from=to
        btTransform start, end;
        start.setIdentity();
        end.setIdentity();
        start.setOrigin(btOrigin);
        end.setOrigin(btEnd);

        // Setup our custom callback
        bullet::sphere_closest_convex_result_callback cb(btOrigin, btEnd, layer_mask, query_sensors);

        // Perform the sweep
        dynamics_world->convexSweepTest(&shape, start, end, cb);

        // Check if we got a hit
        if(!cb.hasHit())
            return {}; // no hit

        return make_hit(cb.m_hitCollisionObject,
                        cb.m_hitPointWorld,
                        cb.m_hitNormalWorld,
                        cb.m_closestHitFraction,
                        direction,
                        max_distance);
    }

    auto sphere_cast_all(const math::vec3& origin,
                         const math::vec3& direction,
                         float radius,
                         float max_distance,
                         int layer_mask,
                         bool query_sensors) -> unravel::physics_vector<unravel::raycast_hit>
    {
        if(!dynamics_world)
        {
            return {};
        }
        // bullet transforms
        btVector3 btOrigin = to_bullet(origin);
        btVector3 btEnd = to_bullet(origin + direction * max_distance);

        btTransform start, end;
        start.setIdentity();
        end.setIdentity();
        start.setOrigin(btOrigin);
        end.setOrigin(btEnd);

        // shape
        btSphereShape shape(radius);

        // custom callback
        sphere_all_convex_result_callback cb(layer_mask, query_sensors);

        dynamics_world->convexSweepTest(&shape, start, end, cb);

        // Now cb.hits has all hits in the order they were encountered
        // Typically not sorted by fraction, so let's sort them:
        std::sort(cb.hits.begin(),
                  cb.hits.end(),
                  [](auto& a, auto& b)
                  {
                      return a.fraction < b.fraction;
                  });

        // Build the final results
        unravel::physics_vector<unravel::raycast_hit> hits;
        hits.reserve(cb.hits.size());

        for(const auto& hi : cb.hits)
        {
            if(auto hit = make_hit(hi.object, hi.point, hi.normal, hi.fraction, direction, max_distance))
            {
                hits.push_back(*hit);
            }
        }

        return hits;
    }

    auto sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
        -> unravel::physics_vector<entt::entity>
    {
        btSphereShape sphere(radius);
        btCollisionObject tempObj;
        tempObj.setCollisionShape(&sphere);
        tempObj.setWorldTransform(btTransform(btQuaternion::getIdentity(), to_bullet(origin)));

        sphere_overlap_callback cb(&tempObj, layer_mask, query_sensors);
        dynamics_world->contactTest(&tempObj, cb);

        // Build the final results
        unravel::physics_vector<entt::entity> hits;
        hits.reserve(cb.hits.size());

        for(const auto& hi : cb.hits)
        {
            const entt::entity entity = resolve_entity_from_collision_object(hi);
            if(entity == entt::null)
            {
                continue;
            }
            hits.push_back(entity);
        }

        return hits;
    }
};

auto get_world_from_user_pointer(void* pointer) -> world&
{
    auto world = reinterpret_cast<bullet::world*>(pointer);
    return *world;
}

auto create_dynamics_world() -> bullet::world
{
    bullet::world world{};
    /// collision configuration contains default setup for memory, collision setup
    auto collision_config = std::make_shared<btDefaultCollisionConfiguration>();
    // collision_config->setConvexConvexMultipointIterations();

    auto broadphase = std::make_shared<btDbvtBroadphase>();

#ifdef BULLET_MT
    auto dispatcher = std::make_shared<btCollisionDispatcherMt>(collision_config.get());
    auto solver_pool = std::make_shared<btConstraintSolverPoolMt>(unravel::get_physics_worker_count());
    auto solver = std::make_shared<btSequentialImpulseConstraintSolverMt>();
    world.dynamics_world = std::make_shared<btDiscreteDynamicsWorldMt>(dispatcher.get(),
                                                                       broadphase.get(),
                                                                       solver_pool.get(),
                                                                       solver.get(),
                                                                       collision_config.get());
    world.solver_pool = solver_pool;
#else

    auto dispatcher = std::make_shared<btCollisionDispatcher>(collision_config.get());
    auto solver = std::make_shared<btSequentialImpulseConstraintSolver>();
    world.dynamics_world = std::make_shared<btDiscreteDynamicsWorld>(dispatcher.get(),
                                                                     broadphase.get(),
                                                                     solver.get(),
                                                                     collision_config.get());
#endif
    world.collision_config = collision_config;
    world.dispatcher = dispatcher;
    world.broadphase = broadphase;
    world.solver = solver;
    world.dynamics_world->setGravity(to_bullet(unravel::get_default_gravity()));
    world.dynamics_world->setForceUpdateAllAabbs(false);
    // Points add_surface_to_contact flags keep the friction directions and targets it gave them. Every other point
    // still gets Bullet's own each step: the solver flags points itself only with two friction rows and
    // velocity-independent directions, both off here.
    world.dynamics_world->getSolverInfo().m_solverMode |= SOLVER_ENABLE_FRICTION_DIRECTION_CACHING;
    world.ghost_pair_callback = std::make_shared<btGhostPairCallback>();
    world.dynamics_world->getPairCache()->setInternalGhostPairCallback(world.ghost_pair_callback.get());
    return world;
}

ATTRIBUTE_ALIGNED16(class)
btCompoundShapeOwning : public btCompoundShape
{
public:
    BT_DECLARE_ALIGNED_ALLOCATOR();

    ~btCompoundShapeOwning() override
    {
        /*delete all the btBU_Simplex1to4 ChildShapes*/
        for(int i = 0; i < m_children.size(); i++)
        {
            delete m_children[i].m_childShape;
        }
    }

    /// Keeps a mesh a child shape reads its triangles from; Bullet's mesh shapes do not own theirs.
    void own_mesh(std::unique_ptr<btTriangleMesh> mesh)
    {
        meshes_.push_back(std::move(mesh));
    }

private:
    /// Destroyed after the child shapes, which read them.
    std::vector<std::unique_ptr<btTriangleMesh>> meshes_;
};
} // namespace
} // namespace bullet

// Free helpers live in the backend namespace. Outside Debug the engine is a unity build:
// anonymous namespaces merge across the concatenated sources, so an unravel-level helper
// here would collide with its twin in the other backend.
namespace bullet
{
namespace
{
using namespace unravel;

const uint8_t system_id = dirty_ids::physics;

void wake_up(bullet::rigidbody& body)
{
    if(body.internal)
    {
        body.internal->activate(true);
    }
}

/**
 * @brief Gives a body that cannot sleep its DISABLE_DEACTIVATION back once it is awake.
 *
 * Bullet keeps "cannot sleep" in the activation state itself, which put_island_to_sleep and a forced ACTIVE_TAG
 * overwrite.
 */
void keep_sleepless_body_awake(btRigidBody& body, const physics_component& comp)
{
    if(!comp.can_sleep() && body.getActivationState() != DISABLE_DEACTIVATION)
    {
        body.forceActivationState(DISABLE_DEACTIVATION);
    }
}

void put_to_sleep(btCollisionObject& object)
{
    if(auto* body = btRigidBody::upcast(&object))
    {
        body->setLinearVelocity(btVector3(0, 0, 0));
        body->setAngularVelocity(btVector3(0, 0, 0));
    }
    object.forceActivationState(ISLAND_SLEEPING);
}

/**
 * @brief Puts a body to sleep with every body of its island, as Box3D's b3Body_SetAwake(false) does.
 *
 * Bullet wakes a sleeping body in the next step while anything else in its island is awake, so the whole island
 * sleeps, bodies that cannot sleep included, as on Box3D; they wake with no velocity, as there. Bullet's islands come
 * from the last step and join bodies whose bounds overlap, where Box3D's join bodies that touch, so a few more
 * neighbors can fall asleep with it.
 */
void put_island_to_sleep(bullet::world& world, btRigidBody& body)
{
    const int island = body.getIslandTag();
    if(island < 0)
    {
        put_to_sleep(body);
        return;
    }
    const auto& objects = world.dynamics_world->getCollisionObjectArray();
    for(int i = 0; i < objects.size(); ++i)
    {
        if(objects[i]->getIslandTag() == island && !objects[i]->isStaticOrKinematicObject())
        {
            put_to_sleep(*objects[i]);
        }
    }
}

/**
 * @brief Drops what a ragdoll asks of the backend, since Bullet builds no ragdoll bodies.
 *
 * A prepared definition counts as built, so the full prepare does not run again every frame, and queued impulses are
 * cleared every step instead of piling up.
 */
void drop_ragdoll_requests(ragdoll_component& comp)
{
    auto& runtime = comp.get_runtime();
    if(runtime.is_prepared)
    {
        comp.set_needs_rebuild(false);
    }
    runtime.impulses.clear();
}

/// Islands of the last step: the distinct island tags of the bodies that simulate (the others have none).
auto count_islands(const btCollisionObjectArray& objects) -> int
{
    std::vector<int> tags;
    tags.reserve(size_t(objects.size()));
    for(int i = 0; i < objects.size(); ++i)
    {
        if(objects[i]->getIslandTag() >= 0)
        {
            tags.push_back(objects[i]->getIslandTag());
        }
    }
    std::sort(tags.begin(), tags.end());
    return int(std::unique(tags.begin(), tags.end()) - tags.begin());
}

/// Shapes a collision object holds, with a compound counted by its children, as Box3D counts shapes.
auto count_leaf_shapes(const btCollisionShape* shape) -> int
{
    if(shape == nullptr)
    {
        return 0;
    }
    if(!shape->isCompound())
    {
        return 1;
    }
    const auto* compound = static_cast<const btCompoundShape*>(shape);
    int count = 0;
    for(int i = 0; i < compound->getNumChildShapes(); ++i)
    {
        count += count_leaf_shapes(compound->getChildShape(i));
    }
    return count;
}

auto get_max_abs_component(const math::vec3& value) -> float
{
    const math::vec3 magnitude = math::abs(value);
    return std::max({magnitude.x, magnitude.y, magnitude.z});
}

/// Whether two scale components are the same, within uniform_scale_tolerance of the largest component.
auto is_same_scale(float a, float b, const math::vec3& scale) -> bool
{
    return std::abs(a - b) <= uniform_scale_tolerance * get_max_abs_component(scale);
}

/// Whether a scale stretches every axis alike, sign included.
auto is_uniform_scale(const math::vec3& scale) -> bool
{
    return is_same_scale(scale.x, scale.y, scale) && is_same_scale(scale.y, scale.z, scale);
}

/// Whether a child shape is not turned against the body, so a scale stretches it along its own axes.
auto is_unturned(const math::vec3& rotation_degrees) -> bool
{
    return math::all(math::lessThanEqual(math::abs(rotation_degrees), math::vec3{unturned_shape_tolerance_degrees}));
}

/**
 * @brief Adds a convex hull around points in the body's space.
 *
 * Bullet rounds a hull by its margin outside the points, while btBoxShape keeps its margin inside. The faces move in by
 * the margin first, so a hull keeps the size of the shape it stands for; a hull too thin for that keeps its points.
 */
void add_hull_shape(btCompoundShape& compound, btAlignedObjectArray<btVector3>& points)
{
    auto* hull = new btConvexHullShape();
    btAlignedObjectArray<btVector3> planes;
    btGeometryUtil::getPlaneEquationsFromVertices(points, planes);
    for(int i = 0; i < planes.size(); ++i)
    {
        planes[i][3] += hull->getMargin();
    }
    btAlignedObjectArray<btVector3> core;
    btGeometryUtil::getVerticesFromPlaneEquations(planes, core);
    const btAlignedObjectArray<btVector3>& kept = core.size() >= min_hull_point_count ? core : points;
    for(int i = 0; i < kept.size(); ++i)
    {
        hull->addPoint(kept[i], false);
    }
    hull->recalcLocalAabb();
    compound.addChildShape(btTransform::getIdentity(), hull);
}

/// A box stays a box under a uniform scale, or unturned; otherwise the scale slants it into a hull, as on Box3D.
void add_box_shape(btCompoundShape& compound, const physics_box_shape& shape, const math::vec3& scale)
{
    const math::quat rotation = unravel::make_shape_rotation(shape.rotation);
    const math::vec3 half_extents = shape.extends * 0.5f;
    if(is_uniform_scale(scale) || is_unturned(shape.rotation))
    {
        const btTransform local(bullet::to_bullet(rotation), bullet::to_bullet(shape.center * scale));
        compound.addChildShape(local, new btBoxShape(bullet::to_bullet(half_extents * math::abs(scale))));
        return;
    }
    btAlignedObjectArray<btVector3> corners;
    for(int corner = 0; corner < box_corner_count; ++corner)
    {
        const math::vec3 side{(corner & 1) != 0 ? 1.0f : -1.0f,
                              (corner & 2) != 0 ? 1.0f : -1.0f,
                              (corner & 4) != 0 ? 1.0f : -1.0f};
        corners.push_back(bullet::to_bullet((shape.center + rotation * (half_extents * side)) * scale));
    }
    add_hull_shape(compound, corners);
}

/// A sphere takes the largest stretch of the scale, as on Box3D.
void add_sphere_shape(btCompoundShape& compound, const physics_sphere_shape& shape, const math::vec3& scale)
{
    btTransform local = btTransform::getIdentity();
    local.setOrigin(bullet::to_bullet(shape.center * scale));
    compound.addChildShape(local, new btSphereShape(shape.radius * get_max_abs_component(scale)));
}

/// How much a scale stretches the radius of a capsule around @p axis: the larger stretch across it, as Box3D takes it.
auto get_radial_scale(const math::vec3& axis, const math::vec3& scale) -> float
{
    const math::vec3 helper = std::abs(axis.y) < radial_helper_axis_limit ? math::vec3{0.0f, 1.0f, 0.0f}
                                                                          : math::vec3{1.0f, 0.0f, 0.0f};
    const math::vec3 across = math::normalize(math::cross(axis, helper));
    const math::vec3 other_across = math::cross(axis, across);
    return std::max(math::length(across * scale), math::length(other_across * scale));
}

/// A capsule keeps its kind: the scale moves its two end centers, and its radius takes the stretch across it.
void add_capsule_shape(btCompoundShape& compound, const physics_capsule_shape& shape, const math::vec3& scale)
{
    const math::vec3 axis = unravel::make_shape_rotation(shape.rotation) * math::vec3{0.0f, 1.0f, 0.0f};
    const math::vec3 bottom = (shape.center - axis * (shape.length * 0.5f)) * scale;
    const math::vec3 top = (shape.center + axis * (shape.length * 0.5f)) * scale;
    const float length = math::distance(bottom, top);
    const math::vec3 direction = length > 0.0f ? (top - bottom) / length : axis;
    const btTransform local(shortestArcQuat(btVector3(0, 1, 0), bullet::to_bullet(direction)),
                            bullet::to_bullet((bottom + top) * 0.5f));
    compound.addChildShape(local, new btCapsuleShape(shape.radius * get_radial_scale(axis, scale), length));
}

/// A cylinder stays round under a uniform scale, or unturned with the same stretch across it; otherwise it becomes
/// the hull Box3D always uses.
void add_cylinder_shape(btCompoundShape& compound, const physics_cylinder_shape& shape, const math::vec3& scale)
{
    const math::quat rotation = unravel::make_shape_rotation(shape.rotation);
    const math::vec3 magnitude = math::abs(scale);
    const bool stays_round =
        is_uniform_scale(scale) || (is_unturned(shape.rotation) && is_same_scale(magnitude.x, magnitude.z, scale));
    if(stays_round)
    {
        const btTransform local(bullet::to_bullet(rotation), bullet::to_bullet(shape.center * scale));
        const btVector3 half_extents(shape.radius * magnitude.x,
                                     shape.length * 0.5f * magnitude.y,
                                     shape.radius * magnitude.x);
        compound.addChildShape(local, new btCylinderShape(half_extents));
        return;
    }
    btAlignedObjectArray<btVector3> rims;
    for(int side = 0; side < cylinder_hull_side_count; ++side)
    {
        const float angle = math::two_pi<float>() * float(side) / float(cylinder_hull_side_count);
        const math::vec3 top{shape.radius * std::cos(angle), shape.length * 0.5f, shape.radius * std::sin(angle)};
        const math::vec3 bottom{top.x, -top.y, top.z};
        rims.push_back(bullet::to_bullet((shape.center + rotation * top) * scale));
        rims.push_back(bullet::to_bullet((shape.center + rotation * bottom) * scale));
    }
    add_hull_shape(compound, rims);
}

/**
 * @brief Adds one Bullet collision shape per submesh, with the submesh's node transform, the shape center and the
 *        scale baked into its vertices, as Box3D bakes them. The vertex buffer stores positions in node-local space.
 */
void add_mesh_shapes(bullet::btCompoundShapeOwning& compound, const physics_mesh_shape& shape, const math::vec3& scale)
{
    const auto& mesh_ref = shape.mesh_asset.get();
    auto* vertex_data = mesh_ref->get_system_vb();
    auto* index_data = mesh_ref->get_system_ib();
    auto vertex_count = mesh_ref->get_vertex_count();
    auto face_count = mesh_ref->get_face_count();
    const auto& vertex_format = mesh_ref->get_vertex_format();
    if(!vertex_data || !index_data || vertex_count == 0 || face_count == 0)
    {
        return;
    }
    if(vertex_format.getOffset(bgfx::Attrib::Position) == UINT16_MAX)
    {
        return;
    }
    const auto& submeshes = mesh_ref->get_submeshes();
    const auto node_transforms = mesh_ref->get_submesh_node_transforms();
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
        math::transform node;
        if(s < node_transforms.size())
        {
            node = node_transforms[s];
        }
        // Build a triangle mesh from this submesh's faces only: 32-bit indices, 3-component vertices.
        auto triangle_mesh = std::make_unique<btTriangleMesh>(true, false);
        const auto face_end = std::min(face_begin + submesh->face_count, face_count);
        for(uint32_t f = face_begin; f < face_end; ++f)
        {
            btVector3 corners[3];
            for(uint32_t k = 0; k < 3; ++k)
            {
                float position[4];
                bgfx::vertexUnpack(position, bgfx::Attrib::Position, vertex_format, vertex_data, index_data[f * 3 + k]);
                const math::vec3 local{position[0], position[1], position[2]};
                corners[k] = bullet::to_bullet((node.transform_coord(local) + shape.center) * scale);
            }
            triangle_mesh->addTriangle(corners[0], corners[1], corners[2]);
        }
        // A convex hull can be dynamic; the quantized BVH mesh is accurate but static only.
        btCollisionShape* collision_shape = nullptr;
        if(shape.collision_type == mesh_collision_type::convex)
        {
            collision_shape = new btConvexTriangleMeshShape(triangle_mesh.get());
        }
        else
        {
            collision_shape = new btBvhTriangleMeshShape(triangle_mesh.get(), true);
        }
        compound.addChildShape(btTransform::getIdentity(), collision_shape);
        compound.own_mesh(std::move(triangle_mesh));
    }
}

/**
 * @brief Builds the body's shape with @p scale baked into every child along the body's axes, as Box3D bakes it: a
 *        turned box under a non-uniform scale slants, where btCompoundShape::setLocalScaling would stretch it along
 *        its own axes.
 */
auto make_rigidbody_shape(const physics_component& comp, const math::vec3& scale) -> std::shared_ptr<btCompoundShape>
{
    // use an ownning compound shape. When sharing is implemented we can go back to non owning
    auto cp = std::make_shared<bullet::btCompoundShapeOwning>();
    for(const auto& s : comp.get_shapes())
    {
        if(const auto* box = hpp::get_if<physics_box_shape>(&s.shape))
        {
            add_box_shape(*cp, *box, scale);
        }
        else if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&s.shape))
        {
            add_sphere_shape(*cp, *sphere, scale);
        }
        else if(const auto* capsule = hpp::get_if<physics_capsule_shape>(&s.shape))
        {
            add_capsule_shape(*cp, *capsule, scale);
        }
        else if(const auto* cylinder = hpp::get_if<physics_cylinder_shape>(&s.shape))
        {
            add_cylinder_shape(*cp, *cylinder, scale);
        }
        else if(const auto* mesh = hpp::get_if<physics_mesh_shape>(&s.shape))
        {
            // The build waits for a mesh still loading: a body without its collider lets whatever rests on it fall
            // through.
            if(mesh->mesh_asset)
            {
                add_mesh_shapes(*cp, *mesh, scale);
            }
        }
    }
    return cp;
}

void update_rigidbody_shape(bullet::world& world, bullet::rigidbody& body, physics_component& comp)
{
    auto shape = make_rigidbody_shape(comp, body.shape_scale);
    // The collision algorithms of the body's pairs were made for the old children, and a new compound with as many
    // children has the same revision, so they would not be remade: a box turned hull would be read as a box.
    if(auto* proxy = body.internal->getBroadphaseHandle())
    {
        world.dynamics_world->getBroadphase()->getOverlappingPairCache()->cleanProxyFromPairs(
            proxy,
            world.dynamics_world->getDispatcher());
    }
    body.internal->setCollisionShape(shape.get());
    body.internal_shape = shape;
}

void update_rigidbody_kind(bullet::rigidbody& body, physics_component& comp)
{
    auto flags = body.internal->getCollisionFlags();
    flags &= ~(btCollisionObject::CF_KINEMATIC_OBJECT | btCollisionObject::CF_STATIC_OBJECT);

    switch(comp.get_body_type())
    {
        case rigidbody_type::static_body:
            flags |= btCollisionObject::CF_STATIC_OBJECT;
            body.internal->setCollisionFlags(flags);
            body.internal->forceActivationState(ISLAND_SLEEPING);
            break;
        case rigidbody_type::kinematic:
            flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
            body.internal->setCollisionFlags(flags);
            body.internal->forceActivationState(DISABLE_DEACTIVATION);
            break;
        case rigidbody_type::dynamic:
            body.internal->setCollisionFlags(flags);
            body.internal->forceActivationState(comp.can_sleep() ? ACTIVE_TAG : DISABLE_DEACTIVATION);
            body.internal->setDeactivationTime(0.0f);
            break;
    }
}

void update_rigidbody_constraints(bullet::rigidbody& body, physics_component& comp)
{
    // Get freeze constraints for position and apply them
    auto freeze_position = comp.get_freeze_position();
    btVector3 linear_factor(float(!freeze_position.x), float(!freeze_position.y), float(!freeze_position.z));
    body.internal->setLinearFactor(linear_factor);

    // Adjust velocity to respect linear constraints
    auto velocity = body.internal->getLinearVelocity();
    velocity *= linear_factor;
    body.internal->setLinearVelocity(velocity);

    // Get freeze constraints for rotation and apply them
    auto freeze_rotation = comp.get_freeze_rotation();
    btVector3 angular_factor(float(!freeze_rotation.x), float(!freeze_rotation.y), float(!freeze_rotation.z));
    body.internal->setAngularFactor(angular_factor);

    // Adjust angular velocity to respect angular constraints
    auto angular_velocity = body.internal->getAngularVelocity();
    angular_velocity *= angular_factor;
    body.internal->setAngularVelocity(angular_velocity);

    // Ensure the body is active
    wake_up(body);
}

void update_rigidbody_velocity(bullet::rigidbody& body, physics_component& comp)
{
    body.internal->setLinearVelocity(bullet::to_bullet(comp.get_velocity()));

    wake_up(body);
}

void update_rigidbody_angular_velocity(bullet::rigidbody& body, physics_component& comp)
{
    body.internal->setAngularVelocity(bullet::to_bullet(comp.get_angular_velocity()));

    wake_up(body);
}

void update_rigidbody_collision_layer(bullet::world& world, bullet::rigidbody& body, physics_component& comp)
{
    int filter_group = comp.get_owner().get<layer_component>().layers.mask;
    int filter_mask = comp.get_collision_mask().mask;
    body.collision_filter_group = filter_group;
    body.collision_filter_mask = filter_mask;

    // bool is_dynamic = !(body.internal->isStaticObject() || body.internal->isKinematicObject());
    // body.collision_filter_group = is_dynamic ? body.collision_filter_group : int(unravel::layer_reserved::static_filter);
    // body.collision_filter_mask =
    //     is_dynamic ? body.collision_filter_mask : body.collision_filter_mask ^
    //     int(unravel::layer_reserved::static_filter);
    // 1) Get the body’s broadphase proxy
    btBroadphaseProxy* proxy = body.internal->getBroadphaseHandle();
    if(!proxy)
    {
        return; // or handle error
    }

    if(body.collision_filter_group != proxy->m_collisionFilterGroup ||
       body.collision_filter_mask != proxy->m_collisionFilterMask)
    {
        // 2) Clean up any old pair cache usage
        world.dynamics_world->getBroadphase()->getOverlappingPairCache()->cleanProxyFromPairs(
            proxy,
            world.dynamics_world->getDispatcher());

        // 3) Update filter group / mask
        proxy->m_collisionFilterGroup = body.collision_filter_group;
        proxy->m_collisionFilterMask = body.collision_filter_mask;

        // 4) Re-insert it into the broadphase
        world.dynamics_world->refreshBroadphaseProxy(body.internal.get());
        wake_up(body);
    }
}

void update_rigidbody_mass_and_inertia(bullet::rigidbody& body, physics_component& comp)
{
    btScalar mass(0);
    btVector3 local_inertia(0, 0, 0);
    if(comp.get_body_type() == rigidbody_type::dynamic)
    {
        // Bullet treats mass 0 as static (invMass 0). Dynamic bodies must keep a positive mass
        // even if the collision shape is not ready yet, otherwise impulses/gravity become no-ops.
        mass = comp.get_mass();
        if(mass <= 0.0f)
        {
            mass = 1.0f;
        }
        auto shape = body.internal->getCollisionShape();
        if(shape)
        {
            shape->calculateLocalInertia(mass, local_inertia);
        }
    }
    body.internal->setMassProps(mass, local_inertia);
    // setMassProps toggles CF_STATIC_OBJECT from mass; re-apply kind flags afterward.
    update_rigidbody_kind(body, comp);
}

void update_rigidbody_gravity(bullet::world& world, bullet::rigidbody& body, physics_component& comp)
{
    if(comp.is_using_gravity())
    {
        body.internal->setGravity(world.dynamics_world->getGravity() * comp.get_gravity_scale());
    }
    else
    {
        // Do not clear linear velocity here - that would wipe same-frame impulses.
        body.internal->setGravity(btVector3{0, 0, 0});
    }
}

/**
 * @brief Bullet's damping for the engine's damping rate.
 *
 * The engine's damping is Box3D's: a rate d >= 0 in dv/dt = -d v, which Box3D steps as v / (1 + h d) per sub-step.
 * Bullet steps v (1 - b)^h, the exact solution for b = 1 - e^-d, so the two agree up to Box3D's step error.
 */
auto to_bullet_damping(float rate) -> btScalar
{
    return btScalar(1) - std::exp(-std::max(rate, 0.0f));
}

void update_rigidbody_damping(bullet::rigidbody& body, physics_component& comp)
{
    body.internal->setDamping(to_bullet_damping(comp.get_linear_damping()),
                              to_bullet_damping(comp.get_angular_damping()));
}

void update_rigidbody_motion(bullet::rigidbody& body, physics_component& comp)
{
    const bool continuous = comp.get_body_type() == rigidbody_type::dynamic && comp.is_continuous();
    btVector3 aabb_min;
    btVector3 aabb_max;
    body.internal->getAabb(aabb_min, aabb_max);
    const btVector3 size = aabb_max - aabb_min;
    const float smallest = std::max(min_ccd_motion_threshold,
                                    float(std::min({size.x(), size.y(), size.z()})) * ccd_threshold_share_of_extent);
    body.internal->setCcdMotionThreshold(continuous ? smallest : 0.0f);
    body.internal->setCcdSweptSphereRadius(continuous ? smallest * ccd_swept_sphere_share : 0.0f);
    if(comp.get_body_type() == rigidbody_type::dynamic)
    {
        body.internal->forceActivationState(comp.can_sleep() ? ACTIVE_TAG : DISABLE_DEACTIVATION);
    }
}

/// Rolling resistance and surface velocity act through add_surface_to_contact, which Bullet calls for flagged bodies
/// only.
void update_rigidbody_surface(bullet::rigidbody& body,
                              const physics_material& mat,
                              const math::vec3& surface_velocity)
{
    auto& internal = *body.internal;
    internal.rolling_resistance = std::max(mat.rolling_resistance, 0.0f);
    internal.surface_velocity = bullet::to_bullet(surface_velocity);
    const bool has_surface = internal.rolling_resistance > 0.0f || !internal.surface_velocity.fuzzyZero();
    const int flags = internal.getCollisionFlags();
    internal.setCollisionFlags(has_surface ? flags | btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK
                                           : flags & ~btCollisionObject::CF_CUSTOM_MATERIAL_CALLBACK);
}

void update_rigidbody_material(bullet::rigidbody& body, physics_component& comp)
{
    auto mat = comp.get_material().get();

    int packed = bullet::encode_combine_modes(mat->friction_combine, mat->restitution_combine);
    if(body.internal->getUserIndex2() != packed)
    {
        body.internal->setUserIndex2(packed);
    }

    if(math::epsilonNotEqual(body.internal->getRestitution(), mat->restitution, math::epsilon<float>()))
    {
        body.internal->setRestitution(mat->restitution);
    }
    if(math::epsilonNotEqual(body.internal->getFriction(), mat->friction, math::epsilon<float>()))
    {
        body.internal->setFriction(mat->friction);
    }

    auto stiffness = mat->get_stiffness();
    if(math::epsilonNotEqual(body.internal->getContactStiffness(), stiffness, math::epsilon<float>()) ||
       math::epsilonNotEqual(body.internal->getContactDamping(), mat->damping, math::epsilon<float>()))
    {
        body.internal->setContactStiffnessAndDamping(stiffness, mat->damping);
    }
    update_rigidbody_surface(body, *mat, comp.get_surface_velocity());
}

void update_rigidbody_sensor(bullet::rigidbody& body, physics_component& comp)
{
    auto flags = body.internal->getCollisionFlags();
    if(comp.is_sensor())
    {
        body.internal->setCollisionFlags(flags | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
    else
    {
        body.internal->setCollisionFlags(flags & ~btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }
}

void set_rigidbody_active(bullet::world& world, bullet::rigidbody& body, bool enabled)
{
    if(enabled)
    {
        world.add_rigidbody(body);
    }
    else
    {
        world.remove_rigidbody(body);
    }
}

void update_rigidbody_full(bullet::world& world, bullet::rigidbody& body, physics_component& comp)
{
    // Kind is applied inside update_rigidbody_mass_and_inertia (after setMassProps).
    update_rigidbody_shape(world, body, comp);
    update_rigidbody_mass_and_inertia(body, comp);
    update_rigidbody_material(body, comp);
    update_rigidbody_sensor(body, comp);
    update_rigidbody_constraints(body, comp);
    update_rigidbody_gravity(world, body, comp);
    update_rigidbody_damping(body, comp);
    update_rigidbody_motion(body, comp);
    // Velocity last so explicit velocities / impulses are not overwritten by earlier setup.
    update_rigidbody_velocity(body, comp);
    update_rigidbody_angular_velocity(body, comp);
    update_rigidbody_collision_layer(world, body, comp);
}

auto find_bullet_world() -> bullet::world*
{
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    return registry.ctx().find<bullet::world>();
}

void make_rigidbody(bullet::world& world, entt::handle entity, physics_component& comp)
{
    auto& body = entity.emplace<bullet::rigidbody>();

    body.internal = std::make_shared<bullet::surface_rigid_body>(comp.get_mass(), nullptr, nullptr);
    body.internal->setUserIndex(int(entity.entity()));
    body.internal->setUserPointer(&world);
    body.internal->setFlags(BT_DISABLE_WORLD_GRAVITY);

    auto* transform = entity.try_get<transform_component>();
    if(transform && comp.is_autoscaled())
    {
        // The shapes are built with the scale in them, before the body enters the world and gets its bounds.
        body.shape_scale = transform->get_scale_global();
    }
    update_rigidbody_full(world, body, comp);

    // Apply ECS pose immediately so same-frame ApplyForce / queries hit the correct transform.
    if(transform)
    {
        const btTransform bt_trans(bullet::to_bullet(transform->get_rotation_global()),
                                   bullet::to_bullet(transform->get_position_global()));
        body.internal->setWorldTransform(bt_trans);
        body.internal->setInterpolationWorldTransform(bt_trans);
    }

    if(entity.all_of<active_component>())
    {
        world.add_rigidbody(body);
    }
}

void destroy_phyisics_body(bullet::world& world, entt::handle entity, bool from_physics_component)
{
    auto body = entity.try_get<bullet::rigidbody>();

    if(body)
    {
        // Anything this body was still touching leaves the graph with it either way: a
        // slot outliving its participant would later report an exit against a dead
        // handle, which is the failure this whole path exists to remove.
        //
        // Whether that is worth telling anyone about depends on why we are here.
        // Suppression is raised only for the span of entt::registry::destroy, so:
        //
        //   suppressed  -> entity teardown. The pre-destroy phase already reported
        //                  these while the whole subtree was intact; saying it twice
        //                  would be wrong and running script code from inside
        //                  registry::destroy would be worse.
        //   not         -> the component is being taken off an entity that lives on
        //                  (inspector, RemoveComponent, a body swap). The overlap has
        //                  genuinely ended and nobody else will ever say so.
        //
        // A raw handle.destroy() that skipped scene::destroy_entity also lands in the
        // second case. The physics layer cannot tell it apart from a plain component
        // removal from in here, and reporting from a slightly riskier context beats
        // dropping the event, so it takes the same path.
        const bool notify = !scene::is_destroy_suppressed();

        world.release_contacts_for(entity, unravel::contact_end_reason::other_disabled, notify);

        if(body->internal)
        {
            world.remove_rigidbody(*body);
        }
    }

    if(from_physics_component)
    {
        entity.remove<bullet::rigidbody>();
    }
}

void sync_physics_body(bullet::world& world, physics_component& comp, bool force = false)
{
    auto owner = comp.get_owner();

    if(force)
    {
        destroy_phyisics_body(world, comp.get_owner(), true);
        make_rigidbody(world, owner, comp);
    }
    else
    {
        auto& body = owner.get<bullet::rigidbody>();

        if(comp.is_property_dirty(physics_property::kind))
        {
            set_rigidbody_active(world, body, false);
            update_rigidbody_full(world, body, comp);
            set_rigidbody_active(world, body, true);
        }
        else
        {
            if(comp.is_property_dirty(physics_property::shape))
            {
                comp.set_property_dirty(physics_property::mass, true);
                update_rigidbody_shape(world, body, comp);
                world.dynamics_world->updateSingleAabb(body.internal.get());
            }
            if(comp.is_property_dirty(physics_property::mass))
            {
                update_rigidbody_mass_and_inertia(body, comp);
            }

            if(comp.is_property_dirty(physics_property::sensor))
            {
                update_rigidbody_sensor(body, comp);
            }

            if(comp.is_property_dirty(physics_property::contact_events))
            {
                world.refresh_contact_policy_for(owner);
            }

            if(comp.is_property_dirty(physics_property::constraints))
            {
                update_rigidbody_constraints(body, comp);
                comp.set_property_dirty(physics_property::gravity, true);
            }
            if(comp.is_property_dirty(physics_property::gravity))
            {
                update_rigidbody_gravity(world, body, comp);
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

            // here we check internally for a change
            update_rigidbody_material(body, comp);
            update_rigidbody_collision_layer(world, body, comp);
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

auto make_bt_transform(const transform_component& transform) -> btTransform
{
    const auto& p = transform.get_position_global();
    const auto& q = transform.get_rotation_global();
    return btTransform(bullet::to_bullet(q), bullet::to_bullet(p));
}

/// Scale lives inside the shapes, so a scale change is a shape rebuild, with the mass that follows from it. Only paid
/// when the entity changed scale.
void apply_shape_scale_if_needed(bullet::world& world,
                                 bullet::rigidbody& body,
                                 physics_component& comp,
                                 const transform_component& transform)
{
    if(!body.internal_shape || !comp.is_autoscaled())
    {
        return;
    }
    const math::vec3 scale = transform.get_scale_global();
    if(!math::any(math::epsilonNotEqual(scale, body.shape_scale, bullet::scale_epsilon)))
    {
        return;
    }
    body.shape_scale = scale;
    update_rigidbody_shape(world, body, comp);
    update_rigidbody_mass_and_inertia(body, comp);
    if(body.internal->getBroadphaseHandle() != nullptr)
    {
        world.dynamics_world->updateSingleAabb(body.internal.get());
    }
}

auto compute_angular_velocity(const btQuaternion& from, const btQuaternion& to, float dt) -> btVector3
{
    if(dt <= 0.0f)
    {
        return btVector3(0, 0, 0);
    }
    btQuaternion delta = to * from.inverse();
    btScalar angle = delta.getAngle();
    if(angle > SIMD_PI)
    {
        angle -= SIMD_2_PI;
    }
    if(btFuzzyZero(angle))
    {
        return btVector3(0, 0, 0);
    }
    btVector3 axis = delta.getAxis();
    return axis * (angle / dt);
}

void set_static_transform(bullet::world& world,
                          bullet::rigidbody& body,
                          physics_component& comp,
                          const transform_component& transform)
{
    const btTransform bt_trans = make_bt_transform(transform);
    body.internal->setWorldTransform(bt_trans);
    body.internal->setInterpolationWorldTransform(bt_trans);
    body.internal->setLinearVelocity(btVector3(0, 0, 0));
    body.internal->setAngularVelocity(btVector3(0, 0, 0));
    apply_shape_scale_if_needed(world, body, comp, transform);
    world.dynamics_world->updateSingleAabb(body.internal.get());
}

void move_kinematic_transform(bullet::world& world,
                              bullet::rigidbody& body,
                              physics_component& comp,
                              const transform_component& transform,
                              float step_dt)
{
    const btTransform bt_trans = make_bt_transform(transform);
    const btTransform& current = body.internal->getWorldTransform();
    if(step_dt > 0.0f)
    {
        const btVector3 lin_vel = (bt_trans.getOrigin() - current.getOrigin()) / step_dt;
        const btVector3 ang_vel = compute_angular_velocity(current.getRotation(), bt_trans.getRotation(), step_dt);
        body.internal->setLinearVelocity(lin_vel);
        body.internal->setAngularVelocity(ang_vel);
    }
    else
    {
        body.internal->setLinearVelocity(btVector3(0, 0, 0));
        body.internal->setAngularVelocity(btVector3(0, 0, 0));
    }
    body.internal->setWorldTransform(bt_trans);
    body.internal->setInterpolationWorldTransform(bt_trans);
    apply_shape_scale_if_needed(world, body, comp, transform);
    world.dynamics_world->updateSingleAabb(body.internal.get());
}

void teleport_dynamic_transform(bullet::world& world,
                                bullet::rigidbody& body,
                                physics_component& comp,
                                const transform_component& transform)
{
    const btTransform bt_trans = make_bt_transform(transform);
    body.internal->setWorldTransform(bt_trans);
    body.internal->setInterpolationWorldTransform(bt_trans);
    apply_shape_scale_if_needed(world, body, comp, transform);
    wake_up(body);
}

auto sync_transforms_to_physics(bullet::world& world,
                                physics_component& comp,
                                const transform_component& transform,
                                float step_dt) -> bool
{
    auto owner = comp.get_owner();
    auto& body = owner.get<bullet::rigidbody>();

    if(!body.internal)
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
    auto body = owner.try_get<bullet::rigidbody>();

    if(!body || !body->internal)
    {
        return false;
    }

    const bool awake = body->internal->isActive();
    comp.set_sleeping_internal(!awake);
    if(!awake)
    {
        comp.set_velocity_internal(math::vec3{0.0f});
        comp.set_angular_velocity_internal(math::vec3{0.0f});
        return false;
    }
    keep_sleepless_body_awake(*body->internal, comp);

    comp.set_velocity_internal(bullet::from_bullet(body->internal->getLinearVelocity()));
    comp.set_angular_velocity_internal(bullet::from_bullet(body->internal->getAngularVelocity()));

    const auto& bt_trans = body->internal->getWorldTransform();
    auto p = bullet::from_bullet(bt_trans.getOrigin());
    auto q = bullet::from_bullet(bt_trans.getRotation());

    // An interpolated body reads its step poses from the transform, so every step's pose is written: under the epsilon
    // a slow body would stand still for steps, then jump.
    const float epsilon = comp.get_interpolate() ? 0.0f : bullet::pose_write_epsilon;
    return transform.set_position_and_rotation_global(p, q, epsilon);
}

auto to_physics(bullet::world& world,
                transform_component& transform,
                physics_component& comp,
                float step_dt) -> bool
{
    bool transform_dirty = transform.is_dirty(system_id);
    bool rigidbody_dirty = comp.is_dirty(system_id);

    sync_physics_body(world, comp);

    // Dynamic bodies are simulation-authored: only push ECS transform on teleport/dirty pose.
    // Property dirty (e.g. ApplyForce syncing velocity) must not teleport and fight physics.
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

    return false;
}

/**
 * @brief Clears the physics bit of a transform the backend has just written.
 *
 * The global is resolved first. The lazy global raises the consumer bits only on its rising edge, so one left
 * unresolved would let a later local-space write pass without a physics bit, and the next step would undo it.
 */
void clear_physics_bit(transform_component& transform)
{
    (void)transform.get_transform_global();
    transform.set_dirty(system_id, false);
}

auto from_physics(transform_component& transform, physics_component& comp) -> bool
{
    bool result = sync_transforms_from_physics(comp, transform);

    clear_physics_bit(transform);
    comp.set_dirty(system_id, false);

    return result;
}

void make_character_controller_body(bullet::world& world,
                                    entt::handle entity,
                                    character_controller_component& comp)
{
    auto& cc = entity.emplace<bullet::character_controller>();
    float capsule_half_height = (comp.get_height() - 2.0f * comp.get_radius()) * 0.5f;
    if(capsule_half_height < 0.0f)
    {
        capsule_half_height = 0.0f;
    }
    cc.shape = std::make_shared<btCapsuleShape>(comp.get_radius(), capsule_half_height * 2.0f);
    cc.ghost = std::make_shared<btPairCachingGhostObject>();
    cc.ghost->setCollisionShape(cc.shape.get());
    cc.ghost->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT | btCollisionObject::CF_KINEMATIC_OBJECT);
    cc.ghost->setUserIndex(int(entity.entity()));
    cc.ghost->setUserPointer(&world);
    cc.collision_filter_group = entity.get<layer_component>().layers.mask;
    cc.collision_filter_mask = comp.get_collision_mask().mask;
    cc.controller = std::make_shared<btKinematicCharacterController>(
        cc.ghost.get(), cc.shape.get(), comp.get_step_height());
    cc.controller->setMaxSlope(math::radians(comp.get_slope_limit()));
    cc.controller->setGravity(world.dynamics_world->getGravity() * comp.get_gravity_scale());
    cc.controller->setFallSpeed(comp.get_terminal_velocity());
    cc.controller->setLinearDamping(comp.get_linear_damping());
    auto& transform = entity.get<transform_component>();
    const auto& p = transform.get_position_global();
    const auto& q = transform.get_rotation_global();
    btTransform bt_trans(bullet::to_bullet(q), bullet::to_bullet(p + comp.get_center()));
    cc.ghost->setWorldTransform(bt_trans);
    if(entity.all_of<active_component>())
    {
        world.add_character_controller(cc);
    }
}

void destroy_character_controller_body(bullet::world& world,
                                       entt::handle entity,
                                       bool from_cc_component)
{
    auto cc = entity.try_get<bullet::character_controller>();
    if(cc)
    {
        // Same contract as rigid bodies: the ghost's tracked pairs leave the graph with
        // it, and are reported unless we are inside an entity teardown that already
        // reported them. See destroy_phyisics_body.
        const bool notify = !scene::is_destroy_suppressed();

        world.release_contacts_for(entity, unravel::contact_end_reason::other_disabled, notify);

        cc->pending_displacement = math::vec3{0.0f};
        if(cc->controller)
        {
            cc->controller->setWalkDirection(btVector3(0, 0, 0));
            world.remove_character_controller(*cc);
        }
    }
    if(from_cc_component)
    {
        entity.remove<bullet::character_controller>();
    }
}

void sync_character_controller_body(bullet::world& world,
                                    character_controller_component& comp,
                                    bool force = false)
{
    auto owner = comp.get_owner();
    if(force)
    {
        destroy_character_controller_body(world, owner, true);
        make_character_controller_body(world, owner, comp);
    }
    else
    {
        auto* cc = owner.try_get<bullet::character_controller>();
        if(!cc || !cc->controller)
        {
            return;
        }
        if(comp.is_property_dirty(character_controller_property::shape) ||
           comp.is_property_dirty(character_controller_property::skin_width))
        {
            destroy_character_controller_body(world, owner, true);
            make_character_controller_body(world, owner, comp);
            comp.set_dirty(system_id, false);
            return;
        }
        if(comp.is_property_dirty(character_controller_property::step_height))
        {
            cc->controller->setStepHeight(comp.get_step_height());
        }
        if(comp.is_property_dirty(character_controller_property::slope_limit))
        {
            cc->controller->setMaxSlope(math::radians(comp.get_slope_limit()));
        }
        if(comp.is_property_dirty(character_controller_property::gravity_scale))
        {
            cc->controller->setGravity(world.dynamics_world->getGravity() * comp.get_gravity_scale());
        }
        if(comp.is_property_dirty(character_controller_property::layer))
        {
            destroy_character_controller_body(world, owner, true);
            make_character_controller_body(world, owner, comp);
            comp.set_dirty(system_id, false);
            return;
        }
        if(comp.is_property_dirty(character_controller_property::movement_params))
        {
            cc->controller->setFallSpeed(comp.get_terminal_velocity());
            cc->controller->setLinearDamping(comp.get_linear_damping());
        }
    }
    comp.set_dirty(system_id, false);
}

auto to_physics_cc(bullet::world& world,
                   transform_component& transform,
                   character_controller_component& comp) -> bool
{
    bool transform_dirty = transform.is_dirty(system_id);
    bool cc_dirty = comp.is_dirty(system_id);
    sync_character_controller_body(world, comp);
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return false;
    }
    // Flush pending Move() once for this fixed step (before simulate).
    cc->controller->setWalkDirection(bullet::to_bullet(cc->pending_displacement));
    cc->pending_displacement = math::vec3{0.0f};
    if(transform_dirty || cc_dirty)
    {
        if(!cc->ghost)
        {
            return false;
        }
        const auto& p = transform.get_position_global();
        const auto& q = transform.get_rotation_global();
        btTransform bt_trans(bullet::to_bullet(q), bullet::to_bullet(p + comp.get_center()));
        cc->ghost->setWorldTransform(bt_trans);
        return true;
    }
    return false;
}

auto from_physics_cc(bullet::world& world,
                     transform_component& transform,
                     character_controller_component& comp) -> bool
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->ghost || !cc->controller)
    {
        return false;
    }
    // Clear walk direction so the next catch-up step does not re-apply Move.
    cc->controller->setVelocityForTimeInterval(btVector3(0, 0, 0), 0.0f);
    const auto& bt_trans = cc->ghost->getWorldTransform();
    auto p = bullet::from_bullet(bt_trans.getOrigin()) - comp.get_center();
    auto q = bullet::from_bullet(bt_trans.getRotation());
    bool changed = transform.set_position_and_rotation_global(p, q, bullet::pose_write_epsilon);

    comp.set_grounded(cc->controller->onGround());
    auto bt_vel = cc->controller->getLinearVelocity();
    comp.set_velocity_internal(bullet::from_bullet(bt_vel));

    clear_physics_bit(transform);
    comp.set_dirty(system_id, false);
    return changed;
}

auto add_force(btRigidBody* body, const btVector3& force, force_mode mode) -> bool
{
    if(force.fuzzyZero())
    {
        return false;
    }
    // Apply force based on ForceMode
    switch(mode)
    {
        case force_mode::force: // Continuous force
            body->applyCentralForce(force);
            break;

        case force_mode::acceleration:
        { // Force independent of mass
            btVector3 acceleration_force = force * body->getMass();
            body->applyCentralForce(acceleration_force);
            break;
        }

        case force_mode::impulse: // Instantaneous impulse
            body->applyCentralImpulse(force);
            break;

        case force_mode::velocity_change: // Direct velocity change
        {
            btVector3 new_velocity = body->getLinearVelocity() + force; // Accumulate velocity
            body->setLinearVelocity(new_velocity);
            break;
        }
    }
    return true;
}

auto add_torque(btRigidBody* body, const btVector3& torque, force_mode mode) -> bool
{
    if(torque.fuzzyZero())
    {
        return false;
    }
    // Apply force based on ForceMode
    switch(mode)
    {
        case force_mode::force: // Continuous torque
            body->applyTorque(torque);
            break;

        case force_mode::acceleration: // Angular acceleration
        {
            btVector3 inertia_tensor = body->getInvInertiaDiagLocal();
            btVector3 angular_acceleration(
                inertia_tensor.getX() != 0 ? torque.getX() * (1.0f / inertia_tensor.getX()) : 0.0f,
                inertia_tensor.getY() != 0 ? torque.getY() * (1.0f / inertia_tensor.getY()) : 0.0f,
                inertia_tensor.getZ() != 0 ? torque.getZ() * (1.0f / inertia_tensor.getZ()) : 0.0f);
            body->applyTorque(angular_acceleration);
        }
        break;

        case force_mode::impulse: // Angular impulse
            body->applyTorqueImpulse(torque);
            break;

        case force_mode::velocity_change: // Direct angular velocity change
        {
            btVector3 new_velocity = body->getLinearVelocity() + torque; // Accumulate velocity
            body->setAngularVelocity(new_velocity);
            break;
        }
    }

    return true;
}

} // namespace
} // namespace bullet

namespace unravel
{

void bullet_backend::init()
{
    bullet::setup_task_scheduler();
    bullet::override_combine_callbacks();
    bullet::install_profile_zone_hooks();

    if(auto* scheduler = btGetTaskScheduler())
    {
        // Worth stating once. btCreateDefaultTaskScheduler returns null unless Bullet
        // was built thread-safe, and the fallback under it is sequential - a solver
        // silently running on one thread is otherwise indistinguishable from a slow one.
        APPLOG_INFO("bullet: task scheduler '{}', {} threads",
                    scheduler->getName(),
                    scheduler->getNumThreads());
    }
    else
    {
        APPLOG_WARNING("bullet: no task scheduler - simulation runs single threaded");
    }
}

void bullet_backend::deinit()
{
    bullet::remove_profile_zone_hooks();
    bullet::cleanup_task_scheduler();
}

void bullet_backend::on_create_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto& phisics = entity.get<physics_component>();
        bullet::sync_physics_body(*world, phisics, true);
    }
}

void bullet_backend::on_destroy_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        bullet::destroy_phyisics_body(*world, entity, true);
    }
}

void bullet_backend::on_destroy_bullet_rigidbody_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        bullet::destroy_phyisics_body(*world, entity, false);
    }
}

void bullet_backend::on_create_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto& comp = entity.get<character_controller_component>();
        bullet::sync_character_controller_body(*world, comp, true);
    }
}

void bullet_backend::on_destroy_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        bullet::destroy_character_controller_body(*world, entity, true);
    }
}

void bullet_backend::on_destroy_bullet_cc_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        bullet::destroy_character_controller_body(*world, entity, false);
    }
}

void bullet_backend::move_character(character_controller_component& comp, const math::vec3& displacement)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    // Accumulate for the next simulate flush; do not setWalkDirection here — Bullet
    // would re-apply that vector on every catch-up substep.
    cc->pending_displacement += displacement;
}

void bullet_backend::jump_character(character_controller_component& comp, const math::vec3& direction)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    cc->controller->jump(bullet::to_bullet(direction));
}

void bullet_backend::apply_impulse_character(character_controller_component& comp, const math::vec3& impulse)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    cc->controller->applyImpulse(bullet::to_bullet(impulse));
}

void bullet_backend::warp_character(character_controller_component& comp, const math::vec3& position)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    // The transform moves too: a script that also turns the character this frame dirties the transform, and the
    // next sync then places the controller where the transform is, which must already be the warp target.
    if(auto* transform = owner.try_get<transform_component>())
    {
        transform->set_position_global(position);
    }
    cc->controller->warp(bullet::to_bullet(position + comp.get_center()));
}

void bullet_backend::set_character_linear_velocity(character_controller_component& comp, const math::vec3& velocity)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    cc->controller->setLinearVelocity(bullet::to_bullet(velocity));
}

void bullet_backend::sync_character_runtime_state(character_controller_component& comp)
{
    auto owner = comp.get_owner();
    auto* cc = owner.try_get<bullet::character_controller>();
    if(!cc || !cc->controller)
    {
        return;
    }
    comp.set_grounded(cc->controller->onGround());
    comp.set_velocity_internal(bullet::from_bullet(cc->controller->getLinearVelocity()));
}

void bullet_backend::on_pre_destroy_entity(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world == nullptr)
    {
        return;
    }

    entt::handle entity(r, e);

    // The fast path the whole design exists to protect. A body with no tracked pairs,
    // or whose pairs all opted out of exit-on-destroy, leaves after one compare - no
    // graph walk, no component reads, no allocation.
    auto* links = bullet::find_contact_links(entity);
    if(links == nullptr || links->flush_pending == 0)
    {
        return;
    }

    world->release_contacts_for(entity, contact_end_reason::other_destroyed, true);
}

void bullet_backend::on_create_active_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto body = entity.try_get<bullet::rigidbody>();
        if(body)
        {
            bullet::set_rigidbody_active(*world, *body, true);
        }
        auto cc = entity.try_get<bullet::character_controller>();
        if(cc)
        {
            world->add_character_controller(*cc);
        }
    }
}

void bullet_backend::on_destroy_active_component(entt::registry& r, entt::entity e)
{
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);

        // Deactivating pulls the body out of the world exactly like destroying does,
        // so it owes the same exits. Reported here rather than inferred a step later,
        // which is what makes disable and destroy behave identically.
        //
        // Entity teardown also removes active_component, and there the exits were
        // already reported by the pre-destroy phase. Suppression tells the two apart:
        // it is raised only for the duration of entt::registry::destroy, and script
        // code must not run from in there.
        const bool notify = !scene::is_destroy_suppressed();
        world->release_contacts_for(entity, contact_end_reason::other_disabled, notify);

        auto body = entity.try_get<bullet::rigidbody>();
        if(body)
        {
            bullet::set_rigidbody_active(*world, *body, false);
        }
        auto cc = entity.try_get<bullet::character_controller>();
        if(cc)
        {
            world->remove_character_controller(*cc);
        }
    }
}

void bullet_backend::apply_explosion_force(physics_component& comp,
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

    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return;
    }

    auto owner = comp.get_owner();
    // Flush pending prefab/spawn dirty state so the impulse hits a correct dynamic body.
    const bool force_recreate = !owner.all_of<bullet::rigidbody>();
    bullet::sync_physics_body(*world, comp, force_recreate);

    auto* bbody = owner.try_get<bullet::rigidbody>();
    if(!bbody || !bbody->internal || bbody->internal->getInvMass() <= 0.0f)
    {
        return;
    }

    const auto& body = bbody->internal;
    btVector3 body_position = body->getWorldTransform().getOrigin();
    btVector3 direction = body_position - bullet::to_bullet(explosion_position);
    float distance = direction.length();

    if(distance > explosion_radius && explosion_radius > 0.0f)
    {
        return;
    }

    if(distance > 0.0f)
    {
        direction /= distance;
    }
    else
    {
        direction.setZero();
    }

    if(upwards_modifier != 0.0f)
    {
        direction.setY(direction.getY() + upwards_modifier);
        direction.normalize();
    }

    float attenuation = explosion_radius > 0.0f ? (1.0f - (distance / explosion_radius)) : 1.0f;
    btVector3 force = direction * explosion_force * attenuation;

    if(bullet::add_force(body.get(), force, mode))
    {
        comp.set_velocity_internal(bullet::from_bullet(body->getLinearVelocity()));
        body->forceActivationState(ACTIVE_TAG);
        body->setDeactivationTime(0.0f);
    }
}

void bullet_backend::apply_force(physics_component& comp, const math::vec3& force, force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }

    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return;
    }

    auto owner = comp.get_owner();
    // Flush pending prefab/spawn dirty state so the impulse hits a correct dynamic body.
    const bool force_recreate = !owner.all_of<bullet::rigidbody>();
    bullet::sync_physics_body(*world, comp, force_recreate);

    auto* bbody = owner.try_get<bullet::rigidbody>();
    if(!bbody || !bbody->internal || bbody->internal->getInvMass() <= 0.0f)
    {
        return;
    }

    auto vector = bullet::to_bullet(force);
    if(!bullet::add_force(bbody->internal.get(), vector, mode))
    {
        return;
    }

    // Cache only - do not dirty, or the next sync can fight the just-applied impulse.
    comp.set_velocity_internal(bullet::from_bullet(bbody->internal->getLinearVelocity()));
    bbody->internal->forceActivationState(ACTIVE_TAG);
    bbody->internal->setDeactivationTime(0.0f);
}

void bullet_backend::apply_force_at_position(physics_component& comp,
                                             const math::vec3& force,
                                             const math::vec3& position,
                                             force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }
    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return;
    }
    auto owner = comp.get_owner();
    bullet::sync_physics_body(*world, comp, !owner.all_of<bullet::rigidbody>());
    auto* bbody = owner.try_get<bullet::rigidbody>();
    if(!bbody || !bbody->internal || bbody->internal->getInvMass() <= 0.0f)
    {
        return;
    }
    btRigidBody* body = bbody->internal.get();
    const btVector3 relative = bullet::to_bullet(position) - body->getCenterOfMassPosition();
    btVector3 value = bullet::to_bullet(force);
    switch(mode)
    {
        case force_mode::acceleration:
            value *= btScalar(1.0) / body->getInvMass();
            [[fallthrough]];
        case force_mode::force:
            body->applyForce(value, relative);
            break;
        case force_mode::velocity_change:
            value *= btScalar(1.0) / body->getInvMass();
            [[fallthrough]];
        case force_mode::impulse:
            body->applyImpulse(value, relative);
            break;
    }
    body->forceActivationState(ACTIVE_TAG);
    body->setDeactivationTime(0.0f);
    comp.set_velocity_internal(bullet::from_bullet(body->getLinearVelocity()));
    comp.set_angular_velocity_internal(bullet::from_bullet(body->getAngularVelocity()));
}

void bullet_backend::set_body_awake(physics_component& comp, bool awake)
{
    auto owner = comp.get_owner();
    auto* bbody = owner.try_get<bullet::rigidbody>();
    auto* world = bullet::find_bullet_world();
    if(comp.get_body_type() != rigidbody_type::dynamic || !bbody || !bbody->internal || !world)
    {
        return;
    }
    if(awake)
    {
        bbody->internal->activate(true);
        bullet::keep_sleepless_body_awake(*bbody->internal, comp);
    }
    else
    {
        bullet::put_island_to_sleep(*world, *bbody->internal);
    }
    comp.set_sleeping_internal(!awake);
}

void bullet_backend::set_gravity(const math::vec3& gravity)
{
    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return;
    }
    world->dynamics_world->setGravity(bullet::to_bullet(gravity));
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    registry.view<physics_component, bullet::rigidbody>().each(
        [&](auto e, auto&& comp, auto&& body)
        {
            bullet::update_rigidbody_gravity(*world, body, comp);
        });
}

auto bullet_backend::get_gravity() const -> math::vec3
{
    auto* world = bullet::find_bullet_world();
    return world ? bullet::from_bullet(world->dynamics_world->getGravity()) : get_default_gravity();
}

void bullet_backend::explode(const math::vec3& center, float radius, float falloff, float impulse_per_area, int layer_mask)
{
    auto* world = bullet::find_bullet_world();
    if(!world || radius + falloff <= 0.0f)
    {
        return;
    }
    // Bullet has no explosion query: each body takes the impulse a sphere of its own size would, facing the blast.
    auto& registry = *engine::context().get_cached<ecs>().get_scene().registry;
    registry.view<physics_component, bullet::rigidbody>().each(
        [&](auto e, auto&& comp, auto&& body)
        {
            if(comp.get_body_type() != rigidbody_type::dynamic || !body.internal ||
               (body.collision_filter_group & layer_mask) == 0)
            {
                return;
            }
            const btVector3 offset = body.internal->getCenterOfMassPosition() - bullet::to_bullet(center);
            const float distance = float(offset.length());
            if(distance > radius + falloff)
            {
                return;
            }
            btVector3 aabb_min;
            btVector3 aabb_max;
            body.internal->getAabb(aabb_min, aabb_max);
            const float half = float((aabb_max - aabb_min).length()) * bullet::explosion_radius_per_diagonal;
            const float area = math::pi<float>() * half * half;
            const float fade = distance <= radius
                                   ? 1.0f
                                   : 1.0f - (distance - radius) / std::max(falloff, bullet::explosion_length_epsilon);
            const btVector3 direction =
                distance > bullet::explosion_length_epsilon ? offset / distance : btVector3{0, 1, 0};
            body.internal->applyCentralImpulse(direction * (impulse_per_area * area * fade));
            body.internal->activate(true);
        });
}

void bullet_backend::warn_unsupported(const char* feature) const
{
    if(warned_.insert(feature).second)
    {
        APPLOG_WARNING("bullet: {} is not supported by the Bullet backend; it works with Box3D, the engine default",
                       feature);
    }
}

auto bullet_backend::shape_cast(const physics_compound_shape& shape,
                                const math::vec3& origin,
                                const math::quat& rotation,
                                const math::vec3& direction,
                                float max_distance,
                                int layer_mask,
                                bool query_sensors) -> hpp::optional<raycast_hit>
{
    if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        return sphere_cast(origin + rotation * sphere->center, direction, sphere->radius, max_distance, layer_mask, query_sensors);
    }
    warn_unsupported("A box or capsule cast");
    return {};
}

auto bullet_backend::shape_cast_all(const physics_compound_shape& shape,
                                    const math::vec3& origin,
                                    const math::quat& rotation,
                                    const math::vec3& direction,
                                    float max_distance,
                                    int layer_mask,
                                    bool query_sensors) -> physics_vector<raycast_hit>
{
    if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        return sphere_cast_all(origin + rotation * sphere->center,
                               direction,
                               sphere->radius,
                               max_distance,
                               layer_mask,
                               query_sensors);
    }
    warn_unsupported("A box or capsule cast");
    return {};
}

auto bullet_backend::shape_overlap(const physics_compound_shape& shape,
                                   const math::vec3& origin,
                                   const math::quat& rotation,
                                   int layer_mask,
                                   bool query_sensors) -> physics_vector<entt::entity>
{
    if(const auto* sphere = hpp::get_if<physics_sphere_shape>(&shape.shape))
    {
        return sphere_overlap(origin + rotation * sphere->center, sphere->radius, layer_mask, query_sensors);
    }
    warn_unsupported("A box or capsule overlap");
    return {};
}

void bullet_backend::on_create_joint_component(entt::registry& r, entt::entity e)
{
    if(r.ctx().find<bullet::world>() != nullptr)
    {
        warn_unsupported("The joint component");
    }
}

void bullet_backend::on_destroy_joint_component(entt::registry& r, entt::entity e)
{
}

void bullet_backend::on_create_ragdoll_component(entt::registry& r, entt::entity e)
{
    if(r.ctx().find<bullet::world>() != nullptr)
    {
        warn_unsupported("The ragdoll component");
    }
}

void bullet_backend::on_destroy_ragdoll_component(entt::registry& r, entt::entity e)
{
}

/**
 * Bullet's numbers next to Box3D's: bodies include the character controllers' ghost objects, as Box3D counts their
 * proxies; contacts are Bullet's manifolds, one per pair of bodies whose bounds overlap, where Box3D keeps one per
 * pair of shapes; islands are those of the last step, joining bodies whose bounds overlap, where Box3D joins bodies
 * that touch; joints are always 0, since the Bullet backend makes none.
 */
auto bullet_backend::get_world_stats() const -> world_stats
{
    world_stats stats;
    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return stats;
    }
    const auto& objects = world->dynamics_world->getCollisionObjectArray();
    stats.body_count = objects.size();
    for(int i = 0; i < objects.size(); ++i)
    {
        stats.awake_body_count += objects[i]->isActive() ? 1 : 0;
        stats.shape_count += bullet::count_leaf_shapes(objects[i]->getCollisionShape());
    }
    stats.contact_count = world->dynamics_world->getDispatcher()->getNumManifolds();
    stats.joint_count = world->dynamics_world->getNumConstraints();
    stats.island_count = bullet::count_islands(objects);
    stats.step_ms = world->last_step_ms;
    return stats;
}

void bullet_backend::apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode)
{
    if(comp.get_body_type() != rigidbody_type::dynamic)
    {
        return;
    }

    auto* world = bullet::find_bullet_world();
    if(!world)
    {
        return;
    }

    auto owner = comp.get_owner();
    const bool force_recreate = !owner.all_of<bullet::rigidbody>();
    bullet::sync_physics_body(*world, comp, force_recreate);

    auto* bbody = owner.try_get<bullet::rigidbody>();
    if(!bbody || !bbody->internal || bbody->internal->getInvMass() <= 0.0f)
    {
        return;
    }

    auto vector = bullet::to_bullet(torque);
    if(!bullet::add_torque(bbody->internal.get(), vector, mode))
    {
        return;
    }

    comp.set_angular_velocity_internal(bullet::from_bullet(bbody->internal->getAngularVelocity()));
    bbody->internal->forceActivationState(ACTIVE_TAG);
    bbody->internal->setDeactivationTime(0.0f);
}

void bullet_backend::clear_kinematic_velocities(physics_component& comp)
{
    if(comp.get_body_type() != rigidbody_type::kinematic)
    {
        return;
    }

    auto owner = comp.get_owner();
    if(auto bbody = owner.try_get<bullet::rigidbody>())
    {
        bbody->internal->clearForces();
        bbody->internal->setLinearVelocity(btVector3(0, 0, 0));
        bbody->internal->setAngularVelocity(btVector3(0, 0, 0));
        comp.set_velocity_internal(math::vec3{0.0f, 0.0f, 0.0f});
        comp.set_angular_velocity_internal(math::vec3{0.0f, 0.0f, 0.0f});
    }
}

auto bullet_backend::ray_cast(const math::vec3& origin,
                              const math::vec3& direction,
                              float max_distance,
                              int layer_mask,
                              bool query_sensors) -> hpp::optional<raycast_hit>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    return world.ray_cast_closest(origin, direction, max_distance, layer_mask, query_sensors);
}

auto bullet_backend::ray_cast_all(const math::vec3& origin,
                                  const math::vec3& direction,
                                  float max_distance,
                                  int layer_mask,
                                  bool query_sensors) -> physics_vector<raycast_hit>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    return world.ray_cast_all(origin, direction, max_distance, layer_mask, query_sensors);
}

auto bullet_backend::sphere_cast(const math::vec3& origin,
                                 const math::vec3& direction,
                                 float radius,
                                 float max_distance,
                                 int layer_mask,
                                 bool query_sensors) -> hpp::optional<raycast_hit>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    return world.sphere_cast_closest(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto bullet_backend::sphere_cast_all(const math::vec3& origin,
                                     const math::vec3& direction,
                                     float radius,
                                     float max_distance,
                                     int layer_mask,
                                     bool query_sensors) -> physics_vector<raycast_hit>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    return world.sphere_cast_all(origin, direction, radius, max_distance, layer_mask, query_sensors);
}

auto bullet_backend::sphere_overlap(const math::vec3& origin, float radius, int layer_mask, bool query_sensors)
    -> physics_vector<entt::entity>
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    return world.sphere_overlap(origin, radius, layer_mask, query_sensors);
}

void bullet_backend::on_play_begin(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scn = ec.get_scene();
    auto& registry = *scn.registry;

    auto& world = registry.ctx().emplace<bullet::world>(bullet::create_dynamics_world());

    registry.on_destroy<bullet::rigidbody>().connect<&on_destroy_bullet_rigidbody_component>();
    registry.on_destroy<bullet::character_controller>().connect<&on_destroy_bullet_cc_component>();
    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();

    // Connected only for the duration of play, so scene::destroy_entity finds an empty
    // signal in edit mode and skips its subtree walk entirely.
    on_pre_destroy(registry).connect<&on_pre_destroy_entity>();

    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            bullet::sync_physics_body(world, comp, true);
        });
    registry.view<character_controller_component>().each(
        [&](auto e, auto&& comp)
        {
            bullet::sync_character_controller_body(world, comp, true);
        });
    warn_unsupported_components(registry);
}

void bullet_backend::warn_unsupported_components(entt::registry& registry)
{
    // Every play warns once per feature, for a scene saved with joints or ragdolls as for one that adds them.
    warned_.clear();
    if(!registry.storage<joint_component>().empty())
    {
        warn_unsupported("The joint component");
    }
    if(!registry.storage<ragdoll_component>().empty())
    {
        warn_unsupported("The ragdoll component");
    }
}

void bullet_backend::on_play_end(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    // Drop the whole graph before the bodies go. Play end is a bulk teardown, so no
    // exits are owed, and clearing first keeps the per-body destroy assert honest.
    world.clear_contacts();

    registry.view<character_controller_component>().each(
        [&](auto e, auto&& comp)
        {
            bullet::destroy_character_controller_body(world, comp.get_owner(), true);
        });
    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            bullet::destroy_phyisics_body(world, comp.get_owner(), true);
        });

    on_pre_destroy(registry).disconnect<&on_pre_destroy_entity>();
    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();
    registry.on_destroy<bullet::character_controller>().disconnect<&on_destroy_bullet_cc_component>();
    registry.on_destroy<bullet::rigidbody>().disconnect<&on_destroy_bullet_rigidbody_component>();

    registry.ctx().erase<bullet::world>();
}

void bullet_backend::on_pause(rtti::context& ctx)
{
}

void bullet_backend::on_resume(rtti::context& ctx)
{
}

void bullet_backend::sync_to_physics(rtti::context& ctx, delta_t step_dt)
{
    APP_SCOPE_PERF("Physics/Bullet/Sync Transforms To Physics");
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<bullet::world>();
    const float dt = step_dt.count();

    registry.view<transform_component, physics_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
        {
            bullet::to_physics(world, transform, rigidbody, dt);
        });
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& cc_comp, auto&& active_comp)
        {
            bullet::to_physics_cc(world, transform, cc_comp);
        });
    registry.view<ragdoll_component>().each(
        [&](auto e, auto&& ragdoll)
        {
            bullet::drop_ragdoll_requests(ragdoll);
        });
}

void bullet_backend::simulate(delta_t step_dt)
{
    auto& ctx = engine::context();
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<bullet::world>();

    if(ctx.has<settings>())
    {
        // Cheap enough to re-apply per step, and doing so means the editor slider takes
        // effect without restarting play.
        auto& info = world.dynamics_world->getSolverInfo();
        const int iterations = std::max(1, ctx.get<settings>().physics.solver_iterations);
        if(info.m_numIterations != iterations)
        {
            info.m_numIterations = iterations;
        }
    }

    const float dt = step_dt.count();
    world.simulate(dt, dt, 1);
}

void bullet_backend::sync_from_physics(rtti::context& ctx)
{
    APP_SCOPE_PERF("Physics/Bullet/Sync Transforms From Physics");
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<bullet::world>();

    registry.view<transform_component, physics_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
        {
            bullet::from_physics(transform, rigidbody);
        });
    registry.view<transform_component, character_controller_component, active_component>().each(
        [&](auto e, auto&& transform, auto&& cc_comp, auto&& active_comp)
        {
            bullet::from_physics_cc(world, transform, cc_comp);
        });
}

void bullet_backend::dispatch_contacts(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<bullet::world>();
    world.process_manifolds();
}

void bullet_backend::draw_system_gizmos(rtti::context& ctx, const camera& cam, gfx::dd_raii& dd)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto world = registry.ctx().find<bullet::world>();
    if(world)
    {
        bullet::debugdraw drawer(dd);
        world->dynamics_world->setDebugDrawer(&drawer);

        world->dynamics_world->debugDrawWorld();

        world->dynamics_world->setDebugDrawer(nullptr);
    }
}

void bullet_backend::draw_gizmo(rtti::context& ctx, physics_component& comp, const camera& cam, gfx::dd_raii& dd)
{
}

void bullet_backend::draw_gizmo(rtti::context& ctx,
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
    auto center = p + comp.get_center();
    math::vec3 up = q * math::vec3(0.0f, 1.0f, 0.0f);
    auto top = center + up * cylinder_half_height;
    auto bottom = center - up * cylinder_half_height;
    dd.encoder.setColor(0xff00ffff);
    dd.encoder.setWireframe(true);
    dd.encoder.drawCapsule({bottom.x, bottom.y, bottom.z}, {top.x, top.y, top.z}, comp.get_radius());
}

} // namespace unravel
