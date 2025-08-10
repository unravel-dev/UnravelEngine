#include "bullet_backend.h"

#include <engine/defaults/defaults.h>
#include <engine/events.h>
#include <math/transform.hpp>

#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/layer_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/scripting/ecs/components/script_component.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/settings/settings.h>

#define BT_USE_SSE_IN_API
#include <BulletCollision/CollisionDispatch/btCollisionDispatcherMt.h>
#include <BulletCollision/NarrowPhaseCollision/btRaycastCallback.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolverMt.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorldMt.h>

#include <btBulletCollisionCommon.h>
#include <btBulletDynamicsCommon.h>

#include <hpp/flat_map.hpp>
#include <logging/logging.h>

#ifdef NDEBUG
#define BULLET_MT 1
#endif

#ifdef BULLET_MT
#include "LinearMath/btThreads.h"
#include <thread>
#endif

namespace
{
struct contact_key
{
    entt::handle a, b;
    bool operator<(contact_key const& o) const
    {
        return a < o.a || (a == o.a && b < o.b);
    }
    bool operator==(contact_key const& o) const
    {
        return a == o.a && b == o.b;
    }
};
} // namespace
namespace std
{
template<>
struct hash<contact_key>
{
    size_t operator()(contact_key const& k) const noexcept
    {
        // simple 64-bit combine
        return (uint64_t)k.a.entity() * 0x9e3779b97f4a7c15ULL ^ ((uint64_t)k.b.entity() << 1);
    }
};
} // namespace std

namespace bullet
{
namespace
{
bool enable_logging = false;

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

void override_combine_callbacks()
{
    // Restitution:
    gCalculateCombinedRestitutionCallback = combined_restitution_callback;

    // Friction:
    gCalculateCombinedFrictionCallback = combined_friction_callback;
    gCalculateCombinedRollingFrictionCallback = combined_rolling_friction_callback;
    gCalculateCombinedSpinningFrictionCallback = combined_spinning_friction_callback;
}

void setup_task_scheduler()
{
#ifdef BULLET_MT
    // Select and initialize a task scheduler
    btITaskScheduler* scheduler = btGetTaskScheduler();
    if(!scheduler)
        scheduler = btCreateDefaultTaskScheduler(); // Use Intel TBB if available

    if(!scheduler)
        scheduler = btGetSequentialTaskScheduler(); // Fallback to single-threaded

    // Set the chosen scheduler
    if(scheduler)
    {
        btSetTaskScheduler(scheduler);
    }
#endif
}

void cleanup_task_scheduler()
{
#ifdef BULLET_MT
    // Select and initialize a task scheduler
    btITaskScheduler* scheduler = btGetTaskScheduler();
    if(scheduler)
    {
        btSetTaskScheduler(nullptr);
        delete scheduler;
    }

#endif
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

auto has_scripting(entt::handle a) -> bool
{
    if(!a)
    {
        return false;
    }
    auto a_scirpt_comp = a.try_get<unravel::script_component>();
    bool a_has_scripting = a_scirpt_comp && a_scirpt_comp->has_script_components();
    return a_has_scripting;
}

auto should_record_collision_event(entt::handle a, entt::handle b) -> bool
{
    if(has_scripting(a))
    {
        return true;
    }
    if(has_scripting(b))
    {
        return true;
    }

    return false;
}

auto should_record_sensor_event(entt::handle a, entt::handle b) -> bool
{
    if(has_scripting(a))
    {
        return true;
    }

    return false;
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
    std::shared_ptr<btRigidBody> internal{};
    std::shared_ptr<btCollisionShape> internal_shape{};
    int collision_filter_group{};
    int collision_filter_mask{};
};

struct world
{
    std::shared_ptr<btBroadphaseInterface> broadphase;
    std::shared_ptr<btCollisionDispatcher> dispatcher;
    std::shared_ptr<btConstraintSolver> solver;
    std::shared_ptr<btConstraintSolverPoolMt> solver_pool;
    std::shared_ptr<btDefaultCollisionConfiguration> collision_config;
    std::shared_ptr<btDiscreteDynamicsWorld> dynamics_world;

    struct contact_record
    {
        contact_record()
        {
            // Reserve a small typical number of contacts to avoid per-frame reallocation
            cm.contacts.reserve(4);
        }

        contact_manifold cm;
        bool active_this_frame = false;
    };
    hpp::flat_map<contact_key, contact_record> contacts_cache;
    unravel::physics_vector<contact_manifold> to_enter;
    unravel::physics_vector<contact_manifold> to_exit;

    bool in_simulate{};
    float elapsed{};

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

    void process_manifold(unravel::script_system& scripting, const contact_manifold& manifold)
    {
        switch(manifold.type)
        {
            case manifold_type::sensor:
            {
                if(manifold.event == event_type::enter)
                {
                    scripting.on_sensor_enter(manifold.a, manifold.b);
                }
                else
                {
                    scripting.on_sensor_exit(manifold.a, manifold.b);
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
                    scripting.on_collision_exit(manifold.a, manifold.b, manifold.contacts);
                }
                break;
            }

            default:
            {
                break;
            }
        }
    }

    void process_manifolds()
    {
        auto& ctx = unravel::engine::context();
        auto& scripting = ctx.get_cached<unravel::script_system>();
        auto& ec = ctx.get_cached<unravel::ecs>();

        auto* dispatcher = dynamics_world->getDispatcher();
        int nm = dispatcher->getNumManifolds();

        // Phase 0: clear active flags
        for(auto& kv : contacts_cache)
            kv.second.active_this_frame = false;

        to_enter.clear();
        to_exit.clear();
        to_enter.reserve(nm);
        to_exit.reserve(contacts_cache.size());

        // Phase 1: scan all current manifolds
        for(int i = 0; i < nm; ++i)
        {
            auto* m = dispatcher->getManifoldByIndexInternal(i);
            if(m->getNumContacts() == 0)
                continue;

            // Identify entities and sensor flags
            auto* objA = m->getBody0();
            auto* objB = m->getBody1();
            bool isSensorA = objA->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE;
            bool isSensorB = objB->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE;
            auto eA = get_entity_from_user_index(ec, objA->getUserIndex());
            auto eB = get_entity_from_user_index(ec, objB->getUserIndex());

            // Handle trigger overlaps: A->B and B->A
            if(isSensorA || isSensorB)
            {
                // A->B if A is sensor
                {
                    contact_key key{eA, eB};
                    auto it = contacts_cache.find(key);
                    if(it != contacts_cache.end())
                    {
                        it->second.active_this_frame = true;
                    }
                    else
                    {
                        contact_manifold cm{manifold_type::sensor, event_type::enter, eA, eB, {}};
                        to_enter.push_back(cm);
                        auto& rec = contacts_cache.emplace(key, contact_record{}).first->second;
                        rec.cm = cm;
                        rec.active_this_frame = true;
                    }
                }
                // B->A if B is sensor
                {
                    contact_key key{eB, eA};
                    auto it = contacts_cache.find(key);
                    if(it != contacts_cache.end())
                    {
                        it->second.active_this_frame = true;
                    }
                    else
                    {
                        contact_manifold cm{manifold_type::sensor, event_type::enter, eB, eA, {}};
                        to_enter.push_back(cm);
                        auto& rec = contacts_cache.emplace(key, contact_record{}).first->second;
                        rec.cm = cm;
                        rec.active_this_frame = true;
                    }
                }
                continue;
            }

            // Handle collisions: only new ones cause ENTER
            contact_key key{eA, eB};
            auto it = contacts_cache.find(key);
            if(it != contacts_cache.end())
            {
                // existing: refresh
                it->second.active_this_frame = true;
            }
            else
            {
                // new collision
                contact_manifold cm;
                cm.type = manifold_type::collision;
                cm.event = event_type::enter;
                cm.a = eA;
                cm.b = eB;
                cm.contacts.reserve(m->getNumContacts());
                for(int j = 0; j < m->getNumContacts(); ++j)
                {
                    auto const& p = m->getContactPoint(j);
                    unravel::manifold_point mp;
                    mp.a = from_bullet(p.getPositionWorldOnA());
                    mp.b = from_bullet(p.getPositionWorldOnB());
                    mp.normal_on_b = from_bullet(p.m_normalWorldOnB);
                    mp.normal_on_a = -mp.normal_on_b;
                    mp.impulse = p.getAppliedImpulse();
                    mp.distance = p.getDistance();
                    cm.contacts.push_back(mp);
                }
                to_enter.push_back(cm);
                auto& rec = contacts_cache.emplace(key, contact_record{}).first->second;
                rec.cm = cm;
                rec.active_this_frame = true;
            }
        }

        // Phase 2: EXIT for stale entries
        for(auto it = contacts_cache.begin(); it != contacts_cache.end();)
        {
            if(!it->second.active_this_frame)
            {
                auto cm = it->second.cm;
                cm.event = event_type::exit;
                to_exit.push_back(cm);
                it = contacts_cache.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Phase 3: dispatch
        for(auto& cm : to_enter)
        {
            process_manifold(scripting, cm);
        }
        for(auto& cm : to_exit)
        {
            process_manifold(scripting, cm);
        }
    }

    void simulate(btScalar dt, btScalar fixed_time_step = 1.0 / 60.0, int max_subs_steps = 10)
    {
        in_simulate = true;

        dynamics_world->stepSimulation(dt, max_subs_steps, fixed_time_step);

        in_simulate = false;
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
        if(ray_callback.hasHit())
        {
            const btRigidBody* body = btRigidBody::upcast(ray_callback.m_collisionObject);
            if(body)
            {
                unravel::raycast_hit hit;
                hit.entity = get_entity_id_from_user_index(body->getUserIndex());
                hit.point = from_bullet(ray_callback.m_hitPointWorld);
                hit.normal = from_bullet(ray_callback.m_hitNormalWorld);
                hit.distance = math::distance(origin, hit.point);

                return hit;
            }
        }
        return {};
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
            const btCollisionObject* collision_object = ray_callback.m_collisionObjects[i];
            const btRigidBody* body = btRigidBody::upcast(collision_object);

            if(body)
            {
                auto& hit = hits.emplace_back();

                hit.entity = get_entity_id_from_user_index(body->getUserIndex());
                hit.point = from_bullet(ray_callback.m_hitPointWorld[i]);
                hit.normal = from_bullet(ray_callback.m_hitNormalWorld[i]);
                hit.distance = math::distance(origin, hit.point);
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

        // Build a raycast_hit
        unravel::raycast_hit hit;
        // The collision object
        const btCollisionObject* obj = cb.m_hitCollisionObject;
        // The fraction
        float fraction = cb.m_closestHitFraction;
        btVector3 hitPoint = btOrigin.lerp(btEnd, fraction);
        btVector3 normal = cb.m_hitNormalWorld;

        // If you store user index as entity, etc.:
        const btRigidBody* body = btRigidBody::upcast(obj);
        if(body)
        {
            // e.g. get entity id from bullet user pointer or user index
            hit.entity = get_entity_id_from_user_index(body->getUserIndex());
        }
        else
        {
            // fallback if needed
            hit.entity = entt::null;
        }

        hit.point = from_bullet(hitPoint);
        hit.normal = from_bullet(normal.normalized());
        hit.distance = fraction * max_distance; // approximate

        return hit;
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
            auto& hit = hits.emplace_back();

            const btRigidBody* body = btRigidBody::upcast(hi.object);
            if(body)
            {
                hit.entity = get_entity_id_from_user_index(body->getUserIndex());
            }
            else
            {
                hit.entity = entt::null;
            }

            btVector3 hitPoint = btOrigin.lerp(btEnd, hi.fraction);
            hit.point = from_bullet(hitPoint);
            hit.normal = from_bullet(hi.normal.normalized());
            hit.distance = hi.fraction * max_distance;
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
            auto& hit = hits.emplace_back();

            const btRigidBody* body = btRigidBody::upcast(hi);
            if(body)
            {
                hit = get_entity_id_from_user_index(body->getUserIndex());
            }
            else
            {
                hit = entt::null;
            }
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
    auto solver_pool = std::make_shared<btConstraintSolverPoolMt>(std::thread::hardware_concurrency() - 1);
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
    world.dynamics_world->setGravity(gravity_earth);
    world.dynamics_world->setForceUpdateAllAabbs(false);
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
};
} // namespace
} // namespace bullet

namespace unravel
{

namespace
{
const uint8_t system_id = 1;

void wake_up(bullet::rigidbody& body)
{
    if(body.internal)
    {
        body.internal->activate(true);
    }
}

auto make_rigidbody_shape(physics_component& comp) -> std::shared_ptr<btCompoundShape>
{
    // use an ownning compound shape. When sharing is implemented we can go back to non owning
    auto cp = std::make_shared<bullet::btCompoundShapeOwning>();

    auto compound_shapes = comp.get_shapes();
    if(compound_shapes.empty())
    {
        return cp;
    }

    for(const auto& s : compound_shapes)
    {
        if(hpp::holds_alternative<physics_box_shape>(s.shape))
        {
            const auto& shape = hpp::get<physics_box_shape>(s.shape);
            auto half_extends = shape.extends * 0.5f;

            btBoxShape* box_shape = new btBoxShape({half_extends.x, half_extends.y, half_extends.z});

            btTransform local_transform = btTransform::getIdentity();
            local_transform.setOrigin(bullet::to_bullet(shape.center));
            cp->addChildShape(local_transform, box_shape);
        }
        else if(hpp::holds_alternative<physics_sphere_shape>(s.shape))
        {
            const auto& shape = hpp::get<physics_sphere_shape>(s.shape);

            btSphereShape* sphere_shape = new btSphereShape(shape.radius);

            btTransform local_transform = btTransform::getIdentity();
            local_transform.setOrigin(bullet::to_bullet(shape.center));
            cp->addChildShape(local_transform, sphere_shape);
        }
        else if(hpp::holds_alternative<physics_capsule_shape>(s.shape))
        {
            const auto& shape = hpp::get<physics_capsule_shape>(s.shape);

            btCapsuleShape* capsule_shape = new btCapsuleShape(shape.radius, shape.length);

            btTransform local_transform = btTransform::getIdentity();
            local_transform.setOrigin(bullet::to_bullet(shape.center));
            cp->addChildShape(local_transform, capsule_shape);
        }
        else if(hpp::holds_alternative<physics_cylinder_shape>(s.shape))
        {
            const auto& shape = hpp::get<physics_cylinder_shape>(s.shape);

            btVector3 half_extends(shape.radius, shape.length * 0.5f, shape.radius);
            btCylinderShape* cylinder_shape = new btCylinderShape(half_extends);

            btTransform local_transform = btTransform::getIdentity();
            local_transform.setOrigin(bullet::to_bullet(shape.center));
            cp->addChildShape(local_transform, cylinder_shape);
        }
    }

    return cp;
}

void update_rigidbody_shape(bullet::rigidbody& body, physics_component& comp)
{
    auto shape = make_rigidbody_shape(comp);

    body.internal->setCollisionShape(shape.get());
    body.internal_shape = shape;
}

void update_rigidbody_shape_scale(bullet::world& world, bullet::rigidbody& body, const math::vec3& s)
{
    auto bt_scale = body.internal_shape->getLocalScaling();
    auto scale = bullet::from_bullet(bt_scale);

    if(math::any(math::epsilonNotEqual(scale, s, math::epsilon<float>())))
    {
        bt_scale = bullet::to_bullet(s);
        body.internal_shape->setLocalScaling(bt_scale);
        world.dynamics_world->updateSingleAabb(body.internal.get());
    }
}

// Updated to preserve existing collision flags when switching kinematic/dynamic
void update_rigidbody_kind(bullet::rigidbody& body, physics_component& comp)
{
    // Read current flags
    auto flags = body.internal->getCollisionFlags();
    auto rbFlags = body.internal->getFlags();

    if(comp.is_kinematic())
    {
        // Set kinematic bit, clear static if previously set
        flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
        flags &= ~btCollisionObject::CF_DYNAMIC_OBJECT;

        body.internal->setCollisionFlags(flags);
    }
    else
    {
        // Clear kinematic bit, optionally set dynamic bit
        flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
        flags |= btCollisionObject::CF_DYNAMIC_OBJECT; // ensure dynamic flag
        body.internal->setCollisionFlags(flags);
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
    if(!comp.is_kinematic())
    {
        auto shape = body.internal->getCollisionShape();
        if(shape)
        {
            mass = comp.get_mass();
            shape->calculateLocalInertia(mass, local_inertia);
        }
    }
    body.internal->setMassProps(mass, local_inertia);
}

void update_rigidbody_gravity(bullet::world& world, bullet::rigidbody& body, physics_component& comp)
{
    if(comp.is_using_gravity())
    {
        body.internal->setGravity(world.dynamics_world->getGravity());
    }
    else
    {
        body.internal->setGravity(btVector3{0, 0, 0});
        body.internal->setLinearVelocity(btVector3(0, 0, 0));
    }
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
    update_rigidbody_kind(body, comp);
    update_rigidbody_shape(body, comp);
    update_rigidbody_mass_and_inertia(body, comp);
    update_rigidbody_material(body, comp);
    update_rigidbody_sensor(body, comp);
    update_rigidbody_constraints(body, comp);
    update_rigidbody_velocity(body, comp);
    update_rigidbody_angular_velocity(body, comp);
    update_rigidbody_gravity(world, body, comp);
    update_rigidbody_collision_layer(world, body, comp);
}

void make_rigidbody(bullet::world& world, entt::handle entity, physics_component& comp)
{
    auto& body = entity.emplace<bullet::rigidbody>();

    body.internal = std::make_shared<btRigidBody>(comp.get_mass(), nullptr, nullptr);
    body.internal->setUserIndex(int(entity.entity()));
    body.internal->setUserPointer(&world);
    body.internal->setFlags(BT_DISABLE_WORLD_GRAVITY);

    update_rigidbody_full(world, body, comp);

    if(entity.all_of<active_component>())
    {
        world.add_rigidbody(body);
    }
}

void destroy_phyisics_body(bullet::world& world, entt::handle entity, bool from_physics_component)
{
    auto body = entity.try_get<bullet::rigidbody>();

    if(body && body->internal)
    {
        world.remove_rigidbody(*body);
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
                update_rigidbody_shape(body, comp);
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

            if(comp.is_property_dirty(physics_property::constraints))
            {
                update_rigidbody_constraints(body, comp);
                comp.set_property_dirty(physics_property::gravity, true);
            }
            if(comp.is_property_dirty(physics_property::velocity))
            {
                update_rigidbody_velocity(body, comp);
            }
            if(comp.is_property_dirty(physics_property::angular_velocity))
            {
                update_rigidbody_angular_velocity(body, comp);
            }

            if(comp.is_property_dirty(physics_property::gravity))
            {
                update_rigidbody_gravity(world, body, comp);
            }

            // here we check internally for a change
            update_rigidbody_material(body, comp);
            update_rigidbody_collision_layer(world, body, comp);
        }

        if(!comp.is_kinematic())
        {
            if(comp.are_any_properties_dirty())
            {
                wake_up(body);
            }
        }
    }

    comp.set_dirty(system_id, false);
}

auto sync_transforms(bullet::world& world, physics_component& comp, const transform_component& transform) -> bool
{
    auto owner = comp.get_owner();
    auto& body = owner.get<bullet::rigidbody>();

    if(!body.internal)
    {
        return false;
    }

    const auto& p = transform.get_position_global();
    const auto& q = transform.get_rotation_global();
    const auto& s = transform.get_scale_global();

    auto bt_pos = bullet::to_bullet(p);
    auto bt_rot = bullet::to_bullet(q);
    btTransform bt_trans(bt_rot, bt_pos);
    body.internal->setWorldTransform(bt_trans);

    if(body.internal_shape && comp.is_autoscaled())
    {
        update_rigidbody_shape_scale(world, body, s);
    }

    wake_up(body);

    return true;
}

auto sync_state(physics_component& comp) -> bool
{
    auto owner = comp.get_owner();
    auto body = owner.try_get<bullet::rigidbody>();

    if(!body || !body->internal)
    {
        return false;
    }

    if(!body->internal->isActive())
    {
        return false;
    }

    comp.set_velocity(bullet::from_bullet(body->internal->getLinearVelocity()));
    comp.set_angular_velocity(bullet::from_bullet(body->internal->getAngularVelocity()));

    return true;
}

auto sync_transforms(physics_component& comp, transform_component& transform) -> bool
{
    auto owner = comp.get_owner();
    auto body = owner.try_get<bullet::rigidbody>();

    if(!body || !body->internal)
    {
        return false;
    }

    if(!body->internal->isActive())
    {
        return false;
    }

    const auto& bt_trans = body->internal->getWorldTransform();
    auto p = bullet::from_bullet(bt_trans.getOrigin());
    auto q = bullet::from_bullet(bt_trans.getRotation());

    // Here we are using a more generous epsilon to
    // take into account any conversion errors between us and bullet
    float epsilon = 0.009f;
    return transform.set_position_and_rotation_global(p, q, epsilon);
}

auto to_physics(bullet::world& world, transform_component& transform, physics_component& comp) -> bool
{
    bool transform_dirty = transform.is_dirty(system_id);
    bool rigidbody_dirty = comp.is_dirty(system_id);

    // if(rigidbody_dirty)
    {
        sync_physics_body(world, comp);
    }

    if(transform_dirty || rigidbody_dirty)
    {
        return sync_transforms(world, comp, transform);
    }

    return false;
}

auto from_physics(bullet::world& world, transform_component& transform, physics_component& comp) -> bool
{
    sync_state(comp);

    bool result = sync_transforms(comp, transform);

    transform.set_dirty(system_id, false);
    comp.set_dirty(system_id, false);

    return result;
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

void bullet_backend::init()
{
    bullet::setup_task_scheduler();
    bullet::override_combine_callbacks();
}

void bullet_backend::deinit()
{
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
        sync_physics_body(*world, phisics, true);
    }
}

void bullet_backend::on_destroy_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        destroy_phyisics_body(*world, entity, true);
    }
}

void bullet_backend::on_destroy_bullet_rigidbody_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        destroy_phyisics_body(*world, entity, false);
    }
}

void bullet_backend::on_create_active_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto body = entity.try_get<bullet::rigidbody>();
        if(body)
        {
            set_rigidbody_active(*world, *body, true);
        }
    }
}

void bullet_backend::on_destroy_active_component(entt::registry& r, entt::entity e)
{
    // this function will be called for both physics_component and bullet::rigidbody
    auto world = r.ctx().find<bullet::world>();
    if(world)
    {
        entt::handle entity(r, e);
        auto body = entity.try_get<bullet::rigidbody>();
        if(body)
        {
            set_rigidbody_active(*world, *body, false);
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
    auto owner = comp.get_owner();

    if(auto bbody = owner.try_get<bullet::rigidbody>())
    {
        const auto& body = bbody->internal;

        // Ensure the object is a dynamic rigid body
        if(body && body->getInvMass() > 0)
        {
            // Get the position of the rigid body
            btVector3 body_position = body->getWorldTransform().getOrigin();

            // Calculate the vector from the explosion position to the body
            btVector3 direction = body_position - bullet::to_bullet(explosion_position);
            float distance = direction.length();

            // Skip objects outside the explosion radius
            if(distance > explosion_radius && explosion_radius > 0.0f)
            {
                return;
            }

            // Normalize the direction vector
            if(distance > 0.0f)
            {
                direction /= distance; // Normalize direction
            }
            else
            {
                direction.setZero(); // If explosion is at the same position as the body
            }

            // Apply upwards modifier
            if(upwards_modifier != 0.0f)
            {
                direction.setY(direction.getY() + upwards_modifier);
                direction.normalize();
            }

            // Calculate the explosion force magnitude based on distance
            float attenuation = 1.0f - (distance / explosion_radius);
            btVector3 force = direction * explosion_force * attenuation;

            if(add_force(body.get(), force, mode))
            {
                comp.set_velocity(bullet::from_bullet(body->getLinearVelocity()));

                wake_up(*bbody);
            }
        }
    }
}

void bullet_backend::apply_force(physics_component& comp, const math::vec3& force, force_mode mode)
{
    auto owner = comp.get_owner();

    if(auto bbody = owner.try_get<bullet::rigidbody>())
    {
        const auto& body = bbody->internal;
        auto vector = bullet::to_bullet(force);

        if(add_force(body.get(), vector, mode))
        {
            comp.set_velocity(bullet::from_bullet(body->getLinearVelocity()));
            wake_up(*bbody);
        }
    }
}

void bullet_backend::apply_torque(physics_component& comp, const math::vec3& torque, force_mode mode)
{
    auto owner = comp.get_owner();

    if(auto bbody = owner.try_get<bullet::rigidbody>())
    {
        auto vector = bullet::to_bullet(torque);
        const auto& body = bbody->internal;

        if(add_torque(body.get(), vector, mode))
        {
            comp.set_angular_velocity(bullet::from_bullet(body->getAngularVelocity()));
            wake_up(*bbody);
        }
    }
}

void bullet_backend::clear_kinematic_velocities(physics_component& comp)
{
    if(comp.is_kinematic())
    {
        auto owner = comp.get_owner();

        if(auto bbody = owner.try_get<bullet::rigidbody>())
        {
            bbody->internal->clearForces();

            comp.set_velocity(bullet::from_bullet(bbody->internal->getLinearVelocity()));
            comp.set_angular_velocity(bullet::from_bullet(bbody->internal->getAngularVelocity()));

            wake_up(*bbody);
        }
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
    registry.on_construct<active_component>().connect<&on_create_active_component>();
    registry.on_destroy<active_component>().connect<&on_destroy_active_component>();

    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            sync_physics_body(world, comp, true);
        });
}

void bullet_backend::on_play_end(rtti::context& ctx)
{
    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;

    auto& world = registry.ctx().get<bullet::world>();

    registry.view<physics_component>().each(
        [&](auto e, auto&& comp)
        {
            destroy_phyisics_body(world, comp.get_owner(), true);
        });

    registry.on_construct<active_component>().disconnect<&on_create_active_component>();
    registry.on_destroy<active_component>().disconnect<&on_destroy_active_component>();
    registry.on_destroy<bullet::rigidbody>().disconnect<&on_destroy_bullet_rigidbody_component>();

    registry.ctx().erase<bullet::world>();
}

void bullet_backend::on_pause(rtti::context& ctx)
{
}

void bullet_backend::on_resume(rtti::context& ctx)
{
}

void bullet_backend::on_skip_next_frame(rtti::context& ctx)
{
    delta_t step(1.0f / 60.0f);
    on_frame_update(ctx, step);
}

void bullet_backend::on_frame_update(rtti::context& ctx, delta_t dt)
{
    auto& ev = ctx.get_cached<events>();

    auto& ec = ctx.get_cached<ecs>();
    auto& registry = *ec.get_scene().registry;
    auto& world = registry.ctx().get<bullet::world>();

    if(dt > delta_t::zero())
    {
        float fixed_time_step = 1.0f / 50.0f;
        int max_subs_steps = 3;

        if(ctx.has<settings>())
        {
            auto& ss = ctx.get<settings>();
            fixed_time_step = ss.time.fixed_timestep;
            max_subs_steps = ss.time.max_fixed_steps;
        }

        // Accumulate time
        world.elapsed += dt.count();

        int steps = 0;
        while(world.elapsed >= fixed_time_step && steps < max_subs_steps)
        {
            delta_t step_dt(fixed_time_step);
            ev.on_frame_fixed_update(ctx, step_dt);

            // update phyiscs spatial properties from transform
            uint64_t physics_entities{};
            uint64_t physics_entities_synced{};

            registry.view<transform_component, physics_component, active_component>().each(
                [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
                {
                    physics_entities++;
                    if(to_physics(world, transform, rigidbody))
                    {
                        physics_entities_synced++;
                    }
                });

            // APPLOG_TRACE("Physics Update: entities {} -> synced to physics {}",
            //              physics_entities,
            //              physics_entities_synced);
            // update physics
            world.simulate(fixed_time_step, fixed_time_step, 1);

            physics_entities = {};
            physics_entities_synced = {};
            // update transform from phyiscs interpolated spatial properties
            registry.view<transform_component, physics_component, active_component>().each(
                [&](auto e, auto&& transform, auto&& rigidbody, auto&& active_comp)
                {
                    physics_entities++;
                    if(from_physics(world, transform, rigidbody))
                    {
                        physics_entities_synced++;
                    }
                });

            // APPLOG_TRACE("Physics Update: entities {} -> synced from physics {}",
            //              physics_entities,
            //              physics_entities_synced);

            world.process_manifolds();

            world.elapsed -= fixed_time_step;
            steps++;
        }
    }
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

} // namespace unravel
