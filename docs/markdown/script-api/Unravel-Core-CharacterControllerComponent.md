<a id="charactercontrollercomponent"></a>

# CharacterControllerComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Provides character controller physics functionality for an entity. Uses a capsule shape internally with sweep-based movement.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`isGrounded`](#isgrounded)  | Whether the character is currently touching the ground. |
| `bool` | [`canJump`](#canjump)  | Whether the character can currently jump (equivalent to isGrounded). |
| `Vector3` | [`velocity`](#velocity)  | The current velocity of the character controller (read-only, synced from physics). |
| `Vector3` | [`linearVelocity`](#linearvelocity)  | The linear velocity of the character controller. Setting this directly overrides the physics velocity. |
| `float` | [`radius`](#radius)  | The radius of the character capsule. |
| `float` | [`height`](#height-1)  | The total height of the character capsule. |
| `Vector3` | [`center`](#center)  | The center offset of the capsule relative to the entity transform. |
| `float` | [`stepHeight`](#stepheight)  | Maximum height of obstacles the character can step over. |
| `float` | [`slopeLimit`](#slopelimit)  | Maximum slope angle in degrees the character can walk up. |
| `float` | [`skinWidth`](#skinwidth)  | [Collision](Unravel-Core-Collision.md#collision) skin width around the character capsule. |
| `float` | [`gravityScale`](#gravityscale)  | Multiplier for world gravity applied to this controller. |
| `float` | [`terminalVelocity`](#terminalvelocity)  | Maximum downward fall speed in m/s. Downward vertical velocity is clamped to this magnitude each simulation step. Default is 55 (skydiver terminal velocity). |
| `float` | [`linearDamping`](#lineardamping)  | Damping applied to linear velocity each step. 0 = no damping, 1 = full damping. |
| `LayerMask` | [`includeLayers`](#includelayers)  | Layers that this character should collide with (include filter). |
| `LayerMask` | [`excludeLayers`](#excludelayers)  | Layers that this character should ignore (exclude filter). |
| `LayerMask` | [`collisionLayers`](#collisionlayers)  | Effective collision layer mask after applying include and exclude filters. |

---

<a id="isgrounded"></a>

### isGrounded

```java
bool isGrounded
```

Whether the character is currently touching the ground.

---

<a id="canjump"></a>

### canJump

```java
bool canJump
```

Whether the character can currently jump (equivalent to isGrounded).

---

<a id="velocity"></a>

### velocity

```java
Vector3 velocity
```

The current velocity of the character controller (read-only, synced from physics).

---

<a id="linearvelocity"></a>

### linearVelocity

```java
Vector3 linearVelocity
```

The linear velocity of the character controller. Setting this directly overrides the physics velocity.

---

<a id="radius"></a>

### radius

```java
float radius
```

The radius of the character capsule.

---

<a id="height-1"></a>

### height

```java
float height
```

The total height of the character capsule.

---

<a id="center"></a>

### center

```java
Vector3 center
```

The center offset of the capsule relative to the entity transform.

---

<a id="stepheight"></a>

### stepHeight

```java
float stepHeight
```

Maximum height of obstacles the character can step over.

---

<a id="slopelimit"></a>

### slopeLimit

```java
float slopeLimit
```

Maximum slope angle in degrees the character can walk up.

---

<a id="skinwidth"></a>

### skinWidth

```java
float skinWidth
```

[Collision](Unravel-Core-Collision.md#collision) skin width around the character capsule.

---

<a id="gravityscale"></a>

### gravityScale

```java
float gravityScale
```

Multiplier for world gravity applied to this controller.

---

<a id="terminalvelocity"></a>

### terminalVelocity

```java
float terminalVelocity
```

Maximum downward fall speed in m/s. Downward vertical velocity is clamped to this magnitude each simulation step. Default is 55 (skydiver terminal velocity).

---

<a id="lineardamping"></a>

### linearDamping

```java
float linearDamping
```

Damping applied to linear velocity each step. 0 = no damping, 1 = full damping.

---

<a id="includelayers"></a>

### includeLayers

```java
LayerMask includeLayers
```

Layers that this character should collide with (include filter).

---

<a id="excludelayers"></a>

### excludeLayers

```java
LayerMask excludeLayers
```

Layers that this character should ignore (exclude filter).

---

<a id="collisionlayers"></a>

### collisionLayers

```java
LayerMask collisionLayers
```

Effective collision layer mask after applying include and exclude filters.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Move`](#move) `inline` | Moves the character by a displacement vector. The movement is constrained by collisions. |
| `void` | [`Jump`](#jump) `inline` | Makes the character jump straight up with the given initial speed (m/s). |
| `void` | [`Jump`](#jump-1) `inline` | Makes the character jump with an arbitrary world-space velocity. The vector's magnitude is the jump speed and its direction is the jump axis. |
| `void` | [`ApplyImpulse`](#applyimpulse) `inline` | Applies an impulse to the character controller. |
| `void` | [`Warp`](#warp) `inline` | Teleports the character to a new position instantly, bypassing collision detection. |

---

<a id="move"></a>

### Move

`inline`

```java
inline void Move(Vector3 displacement)
```

Moves the character by a displacement vector. The movement is constrained by collisions.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `displacement` | `[Vector3](Vector3.md#vector3)` | The world-space displacement to apply. |

---

<a id="jump"></a>

### Jump

`inline`

```java
inline void Jump(float speed)
```

Makes the character jump straight up with the given initial speed (m/s).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `speed` | `float` | Initial upward speed in m/s. Must be greater than 0. |

---

<a id="jump-1"></a>

### Jump

`inline`

```java
inline void Jump(Vector3 velocity)
```

Makes the character jump with an arbitrary world-space velocity. The vector's magnitude is the jump speed and its direction is the jump axis.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `velocity` | `[Vector3](Vector3.md#vector3)` | World-space jump velocity. Must be non-zero. |

---

<a id="applyimpulse"></a>

### ApplyImpulse

`inline`

```java
inline void ApplyImpulse(Vector3 impulse)
```

Applies an impulse to the character controller.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `impulse` | `[Vector3](Vector3.md#vector3)` | The impulse vector to apply. |

---

<a id="warp"></a>

### Warp

`inline`

```java
inline void Warp(Vector3 position)
```

Teleports the character to a new position instantly, bypassing collision detection.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `position` | `[Vector3](Vector3.md#vector3)` | The target world position. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_cc_move`](#internal_m2n_cc_move)  |  |
| `void` | [`internal_m2n_cc_jump`](#internal_m2n_cc_jump)  |  |
| `void` | [`internal_m2n_cc_apply_impulse`](#internal_m2n_cc_apply_impulse)  |  |
| `void` | [`internal_m2n_cc_warp`](#internal_m2n_cc_warp)  |  |
| `bool` | [`internal_m2n_cc_get_is_grounded`](#internal_m2n_cc_get_is_grounded)  |  |
| `bool` | [`internal_m2n_cc_get_can_jump`](#internal_m2n_cc_get_can_jump)  |  |
| `Vector3` | [`internal_m2n_cc_get_velocity`](#internal_m2n_cc_get_velocity)  |  |
| `Vector3` | [`internal_m2n_cc_get_linear_velocity`](#internal_m2n_cc_get_linear_velocity)  |  |
| `void` | [`internal_m2n_cc_set_linear_velocity`](#internal_m2n_cc_set_linear_velocity)  |  |
| `float` | [`internal_m2n_cc_get_radius`](#internal_m2n_cc_get_radius)  |  |
| `void` | [`internal_m2n_cc_set_radius`](#internal_m2n_cc_set_radius)  |  |
| `float` | [`internal_m2n_cc_get_height`](#internal_m2n_cc_get_height)  |  |
| `void` | [`internal_m2n_cc_set_height`](#internal_m2n_cc_set_height)  |  |
| `Vector3` | [`internal_m2n_cc_get_center`](#internal_m2n_cc_get_center)  |  |
| `void` | [`internal_m2n_cc_set_center`](#internal_m2n_cc_set_center)  |  |
| `float` | [`internal_m2n_cc_get_step_height`](#internal_m2n_cc_get_step_height)  |  |
| `void` | [`internal_m2n_cc_set_step_height`](#internal_m2n_cc_set_step_height)  |  |
| `float` | [`internal_m2n_cc_get_slope_limit`](#internal_m2n_cc_get_slope_limit)  |  |
| `void` | [`internal_m2n_cc_set_slope_limit`](#internal_m2n_cc_set_slope_limit)  |  |
| `float` | [`internal_m2n_cc_get_skin_width`](#internal_m2n_cc_get_skin_width)  |  |
| `void` | [`internal_m2n_cc_set_skin_width`](#internal_m2n_cc_set_skin_width)  |  |
| `float` | [`internal_m2n_cc_get_gravity_scale`](#internal_m2n_cc_get_gravity_scale)  |  |
| `void` | [`internal_m2n_cc_set_gravity_scale`](#internal_m2n_cc_set_gravity_scale)  |  |
| `float` | [`internal_m2n_cc_get_terminal_velocity`](#internal_m2n_cc_get_terminal_velocity)  |  |
| `void` | [`internal_m2n_cc_set_terminal_velocity`](#internal_m2n_cc_set_terminal_velocity)  |  |
| `float` | [`internal_m2n_cc_get_linear_damping`](#internal_m2n_cc_get_linear_damping)  |  |
| `void` | [`internal_m2n_cc_set_linear_damping`](#internal_m2n_cc_set_linear_damping)  |  |
| `LayerMask` | [`internal_m2n_cc_get_include_layers`](#internal_m2n_cc_get_include_layers)  |  |
| `void` | [`internal_m2n_cc_set_include_layers`](#internal_m2n_cc_set_include_layers)  |  |
| `LayerMask` | [`internal_m2n_cc_get_exclude_layers`](#internal_m2n_cc_get_exclude_layers)  |  |
| `void` | [`internal_m2n_cc_set_exclude_layers`](#internal_m2n_cc_set_exclude_layers)  |  |
| `LayerMask` | [`internal_m2n_cc_get_collision_layers`](#internal_m2n_cc_get_collision_layers)  |  |

---

<a id="internal_m2n_cc_move"></a>

### internal_m2n_cc_move

```java
void internal_m2n_cc_move(Entity eid, Vector3 displacement)
```

---

<a id="internal_m2n_cc_jump"></a>

### internal_m2n_cc_jump

```java
void internal_m2n_cc_jump(Entity eid, Vector3 velocity)
```

---

<a id="internal_m2n_cc_apply_impulse"></a>

### internal_m2n_cc_apply_impulse

```java
void internal_m2n_cc_apply_impulse(Entity eid, Vector3 impulse)
```

---

<a id="internal_m2n_cc_warp"></a>

### internal_m2n_cc_warp

```java
void internal_m2n_cc_warp(Entity eid, Vector3 position)
```

---

<a id="internal_m2n_cc_get_is_grounded"></a>

### internal_m2n_cc_get_is_grounded

```java
bool internal_m2n_cc_get_is_grounded(Entity eid)
```

---

<a id="internal_m2n_cc_get_can_jump"></a>

### internal_m2n_cc_get_can_jump

```java
bool internal_m2n_cc_get_can_jump(Entity eid)
```

---

<a id="internal_m2n_cc_get_velocity"></a>

### internal_m2n_cc_get_velocity

```java
Vector3 internal_m2n_cc_get_velocity(Entity eid)
```

---

<a id="internal_m2n_cc_get_linear_velocity"></a>

### internal_m2n_cc_get_linear_velocity

```java
Vector3 internal_m2n_cc_get_linear_velocity(Entity eid)
```

---

<a id="internal_m2n_cc_set_linear_velocity"></a>

### internal_m2n_cc_set_linear_velocity

```java
void internal_m2n_cc_set_linear_velocity(Entity eid, Vector3 velocity)
```

---

<a id="internal_m2n_cc_get_radius"></a>

### internal_m2n_cc_get_radius

```java
float internal_m2n_cc_get_radius(Entity eid)
```

---

<a id="internal_m2n_cc_set_radius"></a>

### internal_m2n_cc_set_radius

```java
void internal_m2n_cc_set_radius(Entity eid, float radius)
```

---

<a id="internal_m2n_cc_get_height"></a>

### internal_m2n_cc_get_height

```java
float internal_m2n_cc_get_height(Entity eid)
```

---

<a id="internal_m2n_cc_set_height"></a>

### internal_m2n_cc_set_height

```java
void internal_m2n_cc_set_height(Entity eid, float height)
```

---

<a id="internal_m2n_cc_get_center"></a>

### internal_m2n_cc_get_center

```java
Vector3 internal_m2n_cc_get_center(Entity eid)
```

---

<a id="internal_m2n_cc_set_center"></a>

### internal_m2n_cc_set_center

```java
void internal_m2n_cc_set_center(Entity eid, Vector3 center)
```

---

<a id="internal_m2n_cc_get_step_height"></a>

### internal_m2n_cc_get_step_height

```java
float internal_m2n_cc_get_step_height(Entity eid)
```

---

<a id="internal_m2n_cc_set_step_height"></a>

### internal_m2n_cc_set_step_height

```java
void internal_m2n_cc_set_step_height(Entity eid, float stepHeight)
```

---

<a id="internal_m2n_cc_get_slope_limit"></a>

### internal_m2n_cc_get_slope_limit

```java
float internal_m2n_cc_get_slope_limit(Entity eid)
```

---

<a id="internal_m2n_cc_set_slope_limit"></a>

### internal_m2n_cc_set_slope_limit

```java
void internal_m2n_cc_set_slope_limit(Entity eid, float slopeLimit)
```

---

<a id="internal_m2n_cc_get_skin_width"></a>

### internal_m2n_cc_get_skin_width

```java
float internal_m2n_cc_get_skin_width(Entity eid)
```

---

<a id="internal_m2n_cc_set_skin_width"></a>

### internal_m2n_cc_set_skin_width

```java
void internal_m2n_cc_set_skin_width(Entity eid, float skinWidth)
```

---

<a id="internal_m2n_cc_get_gravity_scale"></a>

### internal_m2n_cc_get_gravity_scale

```java
float internal_m2n_cc_get_gravity_scale(Entity eid)
```

---

<a id="internal_m2n_cc_set_gravity_scale"></a>

### internal_m2n_cc_set_gravity_scale

```java
void internal_m2n_cc_set_gravity_scale(Entity eid, float scale)
```

---

<a id="internal_m2n_cc_get_terminal_velocity"></a>

### internal_m2n_cc_get_terminal_velocity

```java
float internal_m2n_cc_get_terminal_velocity(Entity eid)
```

---

<a id="internal_m2n_cc_set_terminal_velocity"></a>

### internal_m2n_cc_set_terminal_velocity

```java
void internal_m2n_cc_set_terminal_velocity(Entity eid, float speed)
```

---

<a id="internal_m2n_cc_get_linear_damping"></a>

### internal_m2n_cc_get_linear_damping

```java
float internal_m2n_cc_get_linear_damping(Entity eid)
```

---

<a id="internal_m2n_cc_set_linear_damping"></a>

### internal_m2n_cc_set_linear_damping

```java
void internal_m2n_cc_set_linear_damping(Entity eid, float damping)
```

---

<a id="internal_m2n_cc_get_include_layers"></a>

### internal_m2n_cc_get_include_layers

```java
LayerMask internal_m2n_cc_get_include_layers(Entity eid)
```

---

<a id="internal_m2n_cc_set_include_layers"></a>

### internal_m2n_cc_set_include_layers

```java
void internal_m2n_cc_set_include_layers(Entity eid, LayerMask mask)
```

---

<a id="internal_m2n_cc_get_exclude_layers"></a>

### internal_m2n_cc_get_exclude_layers

```java
LayerMask internal_m2n_cc_get_exclude_layers(Entity eid)
```

---

<a id="internal_m2n_cc_set_exclude_layers"></a>

### internal_m2n_cc_set_exclude_layers

```java
void internal_m2n_cc_set_exclude_layers(Entity eid, LayerMask mask)
```

---

<a id="internal_m2n_cc_get_collision_layers"></a>

### internal_m2n_cc_get_collision_layers

```java
LayerMask internal_m2n_cc_get_collision_layers(Entity eid)
```

