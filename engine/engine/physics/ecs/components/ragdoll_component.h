#pragma once
#include <engine/engine_export.h>

#include <engine/ecs/components/basic_component.h>
#include <engine/layers/layer_mask.h>
#include <engine/physics/ecs/components/physics_component.h>
#include <math/math.h>

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @enum ragdoll_state
 * @brief Who moves a ragdoll's bones. Persisted by value: append only.
 */
enum class ragdoll_state : uint8_t
{
    /// The animation moves the bones. The bodies follow it as kinematic hitboxes: they push props and rays hit them.
    animated,
    /// Physics moves the bones: the bodies fall under gravity, held together by the joints.
    simulated,
    /// Physics moves the bones while joint drives pull them towards the animation: hits and collisions knock the
    /// body about and it finds its way back to the animated pose.
    powered,
};

/**
 * @enum ragdoll_joint_type
 * @brief How a ragdoll body turns against its parent body. Persisted by value: append only.
 */
enum class ragdoll_joint_type : uint8_t
{
    /// Swings in a cone about the joint axis and twists about it (shoulders, hips, spine, neck).
    ball,
    /// Turns about the joint axis only (elbows, knees).
    hinge,
    /// Does not turn.
    fixed,
};

/**
 * @struct ragdoll_body
 * @brief One body of a ragdoll, bound to a bone by name like a body of an Unreal physics asset.
 *
 * The shape is in the bone's space. The joint ties the body to the nearest ancestor bone that has a body, at this
 * bone's origin. Its frame is the bone's frame turned by the joint rotation; its X axis is the twist (ball) or hinge
 * axis, as in Unreal's constraints.
 */
struct ragdoll_body
{
    friend auto operator==(const ragdoll_body& lhs, const ragdoll_body& rhs) -> bool = default;

    /// Name of the bone the body moves.
    std::string bone;
    /// Collision shape in the bone's space. Mesh shapes are not used.
    physics_compound_shape shape{physics_capsule_shape{}};
    /// Mass in kilograms.
    float mass{1.0f};

    ragdoll_joint_type joint{ragdoll_joint_type::ball};
    /// Turn of the joint frame against the bone, Euler angles in degrees.
    math::vec3 joint_rotation{0.0f};
    /// Ball: half angle of the swing cone, degrees.
    float swing_limit{30.0f};
    /// Ball: twist range, hinge: angle range, degrees.
    float twist_lower{-20.0f};
    float twist_upper{20.0f};
    /// Multiplies the ragdoll's joint friction for this joint.
    float friction_scale{1.0f};
    /// Multiplies the ragdoll's drive strength for this joint.
    float drive_scale{1.0f};
};

/**
 * @struct ragdoll_bone_pair
 * @brief Two bodies that never collide with each other (bones that overlap in the rest pose).
 */
struct ragdoll_bone_pair
{
    friend auto operator==(const ragdoll_bone_pair& lhs, const ragdoll_bone_pair& rhs) -> bool = default;

    std::string bone_a;
    std::string bone_b;
};

/**
 * @struct ragdoll_drive
 * @brief How hard a powered ragdoll (or a simulated part of an animated one) follows the animation, as frequency and
 *        damping ratio like Unreal's physics control.
 */
struct ragdoll_drive
{
    friend auto operator==(const ragdoll_drive& lhs, const ragdoll_drive& rhs) -> bool = default;

    /// Joint springs towards the animated rotation against the parent body (Unreal's local physical animation).
    /// Oscillations per second; 0 turns them off.
    float local_frequency{5.0f};
    float local_damping_ratio{1.0f};
    /// Pull of every body towards its animated pose in the world (Unreal's world physical animation): keeps a powered
    /// ragdoll on its feet. Oscillations per second; 0 turns it off.
    float world_frequency{0.0f};
    float world_damping_ratio{1.0f};
    /// Newtons and newton meters the world pull uses at most, per body.
    float world_max_force{800.0f};
    float world_max_torque{150.0f};
};

/**
 * @struct ragdoll_body_runtime
 * @brief What play keeps per body. Not serialized.
 */
struct ragdoll_body_runtime
{
    /// The bone entity the body moves.
    entt::handle bone{};
    /// Index of the parent body, -1 for the root body.
    int parent{-1};
    /// This frame's animated pose: the bone in the world, and its local transform.
    math::transform anim_world{};
    math::transform anim_local{};
    /// Velocity of the animated pose, which a body keeps when it starts simulating.
    math::vec3 anim_linear_velocity{0.0f};
    math::vec3 anim_angular_velocity{0.0f};
    bool has_anim_pose{false};
    /// The bone in the model's space in the rest pose.
    math::transform bind_model{};
    /// The local transform last written to the bone. A bone holding anything else was posed by the animation (or a
    /// script) since, and that pose is the animated one.
    math::transform written_local{};
    bool has_written{false};
    /// The physics pose after the last two fixed steps.
    math::transform world_previous{};
    math::transform world{};
    bool has_physics_pose{false};
    math::vec3 linear_velocity{0.0f};
    math::vec3 angular_velocity{0.0f};
    /// Where a body that stopped simulating was, while the bone blends from there back to the animation.
    math::transform snapshot_world{};
    /// Physics share of the shown pose, 0 (animation) to 1 (physics).
    float weight{0.0f};
    /// Seconds the blend back to the animation takes, from 1 to 0.
    float blend_time{0.3f};
    /// The body is dynamic in the simulation; otherwise it is a kinematic target of the animation.
    bool simulated{false};
};

/**
 * @struct ragdoll_impulse
 * @brief An impulse waiting for the next fixed step.
 */
struct ragdoll_impulse
{
    int body{-1};
    math::vec3 impulse{0.0f};
    math::vec3 point{0.0f};
    bool at_point{false};
    /// The impulse is a velocity change, the same for every mass.
    bool is_velocity_change{false};
    /// With a velocity change: the change in angular velocity, radians per second.
    math::vec3 angular_velocity_change{0.0f};
};

/**
 * @struct ragdoll_runtime
 * @brief Play state of a ragdoll. Not serialized.
 */
struct ragdoll_runtime
{
    std::vector<ragdoll_body_runtime> bodies;
    /// Body indices with every parent before its children: the order poses are written in.
    std::vector<int> write_order;
    /// The bone above the root body that moves along with it (the armature root), when there is one, so the
    /// model's bounds follow a ragdoll that rolls away.
    entt::handle root_follower{};
    math::transform root_follower_anim_local{};
    math::transform root_follower_written_local{};
    bool root_follower_has_written{false};
    /// The armature the bones were found on; another one finds them again.
    uint64_t armature_version{0};
    bool is_prepared{false};
    /// The root body's bone, in its rest pose, sees the character's forward and up along these local axes.
    math::vec3 root_forward_local{0.0f, 0.0f, 1.0f};
    math::vec3 root_up_local{0.0f, 1.0f, 0.0f};
    std::vector<ragdoll_impulse> impulses;
    /// Seconds the bodies have moved slower than the rest speed.
    float rest_time{0.0f};
    /// Multiplies every drive, 0..1, to fade a powered ragdoll into a limp one.
    float drive_strength{1.0f};
    /// The backend built the bodies for this definition.
    bool is_built{false};
    /// The bodies' simulated flags or the drives changed since the backend last saw them.
    bool bodies_changed{false};
    /// The next step puts every body on the animated pose, with no velocity.
    bool teleport{false};
};

/**
 * @class ragdoll_component
 * @brief Physics bodies on the bones of a skinned model, held together by joints.
 *
 * Sits on the entity with the skinned model component. The bodies are bound to bones by name and described in the
 * bones' space, so one setup serves every instance of the character (prefabs). While playing, an animated ragdoll's
 * bodies follow the animation as kinematic hitboxes; a simulated or powered one moves the bones. Poses are blended in
 * each bone's parent space, as Unreal does, so partly simulated characters never tear: a bone without a body keeps
 * its animated local transform and rides on its parent.
 */
class ragdoll_component : public component_crtp<ragdoll_component, owned_component>
{
public:
    static void on_create_component(entt::registry& r, entt::entity e);
    static void on_destroy_component(entt::registry& r, entt::entity e);

    void set_bodies(const std::vector<ragdoll_body>& bodies);
    auto get_bodies() const noexcept -> const std::vector<ragdoll_body>&;

    void set_disabled_collisions(const std::vector<ragdoll_bone_pair>& pairs);
    auto get_disabled_collisions() const noexcept -> const std::vector<ragdoll_bone_pair>&;

    /// The state play starts in.
    void set_start_state(ragdoll_state state);
    auto get_start_state() const noexcept -> ragdoll_state;

    /// Newton meters a joint resists turning with: low values flop, high values look stiff.
    void set_joint_friction(float friction);
    auto get_joint_friction() const noexcept -> float;

    void set_linear_damping(float damping);
    auto get_linear_damping() const noexcept -> float;

    void set_angular_damping(float damping);
    auto get_angular_damping() const noexcept -> float;

    /// Bodies that are not parent and child collide with each other, so arms do not pass through the chest.
    void set_self_collision(bool enabled);
    auto get_self_collision() const noexcept -> bool;

    /// Continuous collision for the bodies, so a fast fall does not pass through thin geometry.
    void set_is_continuous(bool continuous);
    auto is_continuous() const noexcept -> bool;

    void set_include_layers(layer_mask mask);
    auto get_include_layers() const noexcept -> layer_mask;

    void set_exclude_layers(layer_mask mask);
    auto get_exclude_layers() const noexcept -> layer_mask;

    auto get_collision_mask() const noexcept -> layer_mask;

    void set_drive(const ragdoll_drive& drive);
    auto get_drive() const noexcept -> const ragdoll_drive&;

    /// Seconds a blend back to the animation takes by default.
    void set_blend_time(float seconds);
    auto get_blend_time() const noexcept -> float;

    /**
     * @brief Hands the bones to physics or back to the animation.
     *
     * Going to simulated or powered, every body starts simulating with the velocity the animation gave it, so a
     * running character falls forward. Going back to animated, the bones blend from where they lie to the animation
     * over @p blend_time seconds (a negative value takes the component's blend time).
     */
    void set_state(ragdoll_state state, float blend_time = -1.0f);
    auto get_state() const noexcept -> ragdoll_state;

    /**
     * @brief Simulates the body of a bone, and the bodies below it with @p include_children, while the rest follows
     *        the animation (Unreal's SetAllBodiesBelowSimulatePhysics). Simulated parts of an animated ragdoll are
     *        pulled to the animation by the local drive; stopping blends them back.
     * @return False when no body belongs to the bone.
     */
    auto set_bone_simulated(const std::string& bone, bool simulated, bool include_children = true, float blend_time = -1.0f)
        -> bool;

    /// Whether the body at @p index simulates.
    auto is_body_simulated(int index) const -> bool;

    /// Index of the body of a bone given by name, -1 when it has none.
    auto find_body(const std::string& bone) const -> int;
    /// Index of the body a bone entity belongs to: its own, else the nearest ancestor's. -1 when none.
    auto find_body(entt::handle bone) const -> int;

    /// Pushes one body; at a world point it also turns it. Simulating bodies only.
    void add_impulse(int body, const math::vec3& impulse, const math::vec3& point);
    void add_impulse(int body, const math::vec3& impulse);
    /// Gives every simulating body the same change in velocity.
    void add_velocity(const math::vec3& velocity_change);
    /// Turns every simulating body as one about @p pivot: each gets the angular velocity (radians per second, the
    /// axis by the right-hand rule) and the velocity its place on the turning body has. Rolls a lying body over.
    void add_angular_velocity(const math::vec3& angular_velocity, const math::vec3& pivot);
    /// Pushes every simulating body away from a point, weaker with distance, by mass.
    void add_explosion(const math::vec3& center, float radius, float impulse);

    /// Multiplies every drive, 0..1. Lowering it over time turns a staggering ragdoll into a falling one.
    void set_drive_strength(float strength);
    auto get_drive_strength() const noexcept -> float;

    /// Puts every body back on the animated pose without velocity, at the next step.
    void reset_to_animation();

    /// The root body's world pose: where the ragdoll is.
    auto get_root_pose() const -> math::transform;
    /// Whether the root body's front faces the sky (the character lies on its back).
    auto is_face_up() const -> bool;
    /// Mean speed of the bodies in meters per second.
    auto get_average_speed() const -> float;
    /// Seconds every body has moved slower than @p speed (meters per second).
    auto get_rest_time() const noexcept -> float;

    /**
     * @brief The pose a character should stand in to get up from where the ragdoll lies: the point under the root
     *        body, turned so the lying direction of a get-up animation matches the body's.
     * @param head_direction The direction from the hips to the head in a get-up animation's first pose, in the
     *        character's space (for a face-up get-up that starts lying on the back, usually -Z).
     */
    auto get_recovery_pose(const math::vec3& head_direction) const -> math::transform;

    auto is_dirty(uint8_t id) const noexcept -> bool;
    void set_dirty(uint8_t id, bool dirty) noexcept;

    /// The definition changed: the backend rebuilds the bodies.
    auto needs_rebuild() const noexcept -> bool;
    void set_needs_rebuild(bool rebuild) noexcept;

    auto get_runtime() noexcept -> ragdoll_runtime&;
    auto get_runtime() const noexcept -> const ragdoll_runtime&;

    /// Settings changed that the backend applies to live bodies (damping, friction, drives, layers).
    auto are_settings_dirty() const noexcept -> bool;
    void set_settings_dirty(bool dirty) noexcept;

    /// Puts the state, the simulated flags and the weights back to the start state, for a new play.
    void reset_runtime_state();

private:
    void mark_definition_changed();
    void mark_settings_changed();
    void set_body_simulated(int index, bool simulated, float blend_time);
    void collect_bodies_below(int index, std::vector<int>& out) const;

    std::vector<ragdoll_body> bodies_;
    std::vector<ragdoll_bone_pair> disabled_collisions_;
    ragdoll_state start_state_{ragdoll_state::animated};
    float joint_friction_{1.5f};
    float linear_damping_{0.05f};
    float angular_damping_{0.4f};
    bool self_collision_{true};
    bool is_continuous_{false};
    layer_mask include_layers_{layer_reserved::everything_layer};
    layer_mask exclude_layers_{layer_reserved::nothing_layer};
    ragdoll_drive drive_{};
    float blend_time_{0.3f};

    ragdoll_state state_{ragdoll_state::animated};
    ragdoll_runtime runtime_{};
    bool needs_rebuild_{true};
    bool settings_dirty_{true};
    std::bitset<8> dirty_;
};

} // namespace unravel
