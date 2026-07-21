<a id="physicscomponent"></a>

# PhysicsComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Provides physics functionality for an entity.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `LayerMask` | [`includeLayers`](#includelayers-1)  | Layers that this body should collide with (include filter). |
| `LayerMask` | [`excludeLayers`](#excludelayers-1)  | Layers that this body should ignore (exclude filter). |
| `LayerMask` | [`collisionLayers`](#collisionlayers-1)  | Effective collision layer mask after applying include and exclude filters. |
| `bool` | [`isSensor`](#issensor)  | Whether this physics component is a sensor (triggers collision events but doesn't generate physical responses). |
| `float` | [`mass`](#mass)  | The mass of the rigidbody. Mass determines how much force is needed to move the object. |
| `bool` | [`isKinematic`](#iskinematic)  | Whether this physics component is kinematic. Kinematic objects are moved by script and don't respond to forces. |
| `bool` | [`useGravity`](#usegravity)  | Whether this physics component uses gravity. |
| `Vector3` | [`velocity`](#velocity-1)  | The velocity vector of the rigidbody. It represents the rate of change of Rigidbody position. In most cases you should not modify the velocity directly, as this can result in unrealistic behaviour - use AddForce instead Do not set the velocity of an object every physics step, this will lead to unrealistic physics simulation. A typical usage is where you would change the velocity is when jumping in a first person shooter, because you want an immediate change in velocity. position. |
| `Vector3` | [`angularVelocity`](#angularvelocity)  | The angular velocity vector of the rigidbody measured in radians per second. In most cases you should not modify it directly, as this can result in unrealistic behaviour. Note that if the Rigidbody has rotational constraints set, the corresponding angular velocity components are set to zero in the mass space (ie relative to the inertia tensor rotation) at the time of the call. Additionally, setting the angular velocity of a kinematic rigidbody is not allowed and will have no effect. |

---

<a id="includelayers-1"></a>

### includeLayers

```java
LayerMask includeLayers
```

Layers that this body should collide with (include filter).

---

<a id="excludelayers-1"></a>

### excludeLayers

```java
LayerMask excludeLayers
```

Layers that this body should ignore (exclude filter).

---

<a id="collisionlayers-1"></a>

### collisionLayers

```java
LayerMask collisionLayers
```

Effective collision layer mask after applying include and exclude filters.

---

<a id="issensor"></a>

### isSensor

```java
bool isSensor
```

Whether this physics component is a sensor (triggers collision events but doesn't generate physical responses).

---

<a id="mass"></a>

### mass

```java
float mass
```

The mass of the rigidbody. Mass determines how much force is needed to move the object.

---

<a id="iskinematic"></a>

### isKinematic

```java
bool isKinematic
```

Whether this physics component is kinematic. Kinematic objects are moved by script and don't respond to forces.

---

<a id="usegravity"></a>

### useGravity

```java
bool useGravity
```

Whether this physics component uses gravity.

---

<a id="velocity-1"></a>

### velocity

```java
Vector3 velocity
```

The velocity vector of the rigidbody. It represents the rate of change of Rigidbody position. In most cases you should not modify the velocity directly, as this can result in unrealistic behaviour - use AddForce instead Do not set the velocity of an object every physics step, this will lead to unrealistic physics simulation. A typical usage is where you would change the velocity is when jumping in a first person shooter, because you want an immediate change in velocity. position.

---

<a id="angularvelocity"></a>

### angularVelocity

```java
Vector3 angularVelocity
```

The angular velocity vector of the rigidbody measured in radians per second. In most cases you should not modify it directly, as this can result in unrealistic behaviour. Note that if the Rigidbody has rotational constraints set, the corresponding angular velocity components are set to zero in the mass space (ie relative to the inertia tensor rotation) at the time of the call. Additionally, setting the angular velocity of a kinematic rigidbody is not allowed and will have no effect.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`ApplyExplosionForce`](#applyexplosionforce) `inline` | Applies an explosion force to the entity. |
| `void` | [`ApplyForce`](#applyforce) `inline` | Applies a force to the entity. |
| `void` | [`ApplyTorque`](#applytorque) `inline` | Applies a torque to the entity. |

---

<a id="applyexplosionforce"></a>

### ApplyExplosionForce

`inline`

```java
inline void ApplyExplosionForce(float explosionForce, Vector3 explosionPosition, float explosionRadius, float upwardsModifier = 0.0f, ForceMode mode = ForceMode.Force)
```

Applies an explosion force to the entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `explosionForce` | `float` | The force of the explosion. |
| `explosionPosition` | `[Vector3](Vector3.md#vector3)` | The center of the explosion. |
| `explosionRadius` | `float` | The radius of the explosion. |
| `upwardsModifier` | `float` | Adjusts the upward direction of the explosion force. |
| `mode` | `[ForceMode](Unravel-Core.md#forcemode)` | The force mode to apply. |

---

<a id="applyforce"></a>

### ApplyForce

`inline`

```java
inline void ApplyForce(Vector3 force, ForceMode mode = ForceMode.Force)
```

Applies a force to the entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `force` | `[Vector3](Vector3.md#vector3)` | The force to apply. |
| `mode` | `[ForceMode](Unravel-Core.md#forcemode)` | The force mode to apply. |

---

<a id="applytorque"></a>

### ApplyTorque

`inline`

```java
inline void ApplyTorque(Vector3 torque, ForceMode mode = ForceMode.Force)
```

Applies a torque to the entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `torque` | `[Vector3](Vector3.md#vector3)` | The torque to apply. |
| `mode` | `[ForceMode](Unravel-Core.md#forcemode)` | The force mode to apply. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_physics_apply_explosion_force`](#internal_m2n_physics_apply_explosion_force)  |  |
| `void` | [`internal_m2n_physics_apply_force`](#internal_m2n_physics_apply_force)  |  |
| `void` | [`internal_m2n_physics_apply_torque`](#internal_m2n_physics_apply_torque)  |  |
| `Vector3` | [`internal_m2n_physics_get_velocity`](#internal_m2n_physics_get_velocity)  |  |
| `void` | [`internal_m2n_physics_set_velocity`](#internal_m2n_physics_set_velocity)  |  |
| `Vector3` | [`internal_m2n_physics_get_angular_velocity`](#internal_m2n_physics_get_angular_velocity)  |  |
| `void` | [`internal_m2n_physics_set_angular_velocity`](#internal_m2n_physics_set_angular_velocity)  |  |
| `LayerMask` | [`internal_m2n_physics_get_include_layers`](#internal_m2n_physics_get_include_layers)  |  |
| `void` | [`internal_m2n_physics_set_include_layers`](#internal_m2n_physics_set_include_layers)  |  |
| `LayerMask` | [`internal_m2n_physics_get_exclude_layers`](#internal_m2n_physics_get_exclude_layers)  |  |
| `void` | [`internal_m2n_physics_set_exclude_layers`](#internal_m2n_physics_set_exclude_layers)  |  |
| `LayerMask` | [`internal_m2n_physics_get_collision_layers`](#internal_m2n_physics_get_collision_layers)  |  |
| `bool` | [`internal_m2n_physics_get_is_sensor`](#internal_m2n_physics_get_is_sensor)  |  |
| `void` | [`internal_m2n_physics_set_is_sensor`](#internal_m2n_physics_set_is_sensor)  |  |
| `float` | [`internal_m2n_physics_get_mass`](#internal_m2n_physics_get_mass)  |  |
| `void` | [`internal_m2n_physics_set_mass`](#internal_m2n_physics_set_mass)  |  |
| `bool` | [`internal_m2n_physics_get_is_kinematic`](#internal_m2n_physics_get_is_kinematic)  |  |
| `void` | [`internal_m2n_physics_set_is_kinematic`](#internal_m2n_physics_set_is_kinematic)  |  |
| `bool` | [`internal_m2n_physics_get_use_gravity`](#internal_m2n_physics_get_use_gravity)  |  |
| `void` | [`internal_m2n_physics_set_use_gravity`](#internal_m2n_physics_set_use_gravity)  |  |

---

<a id="internal_m2n_physics_apply_explosion_force"></a>

### internal_m2n_physics_apply_explosion_force

```java
void internal_m2n_physics_apply_explosion_force(Entity eid, float explosionForce, Vector3 explosionPosition, float explosionRadius, float upwardsModifier, ForceMode mode)
```

---

<a id="internal_m2n_physics_apply_force"></a>

### internal_m2n_physics_apply_force

```java
void internal_m2n_physics_apply_force(Entity eid, Vector3 force, ForceMode mode)
```

---

<a id="internal_m2n_physics_apply_torque"></a>

### internal_m2n_physics_apply_torque

```java
void internal_m2n_physics_apply_torque(Entity eid, Vector3 torque, ForceMode mode)
```

---

<a id="internal_m2n_physics_get_velocity"></a>

### internal_m2n_physics_get_velocity

```java
Vector3 internal_m2n_physics_get_velocity(Entity eid)
```

---

<a id="internal_m2n_physics_set_velocity"></a>

### internal_m2n_physics_set_velocity

```java
void internal_m2n_physics_set_velocity(Entity eid, Vector3 velocity)
```

---

<a id="internal_m2n_physics_get_angular_velocity"></a>

### internal_m2n_physics_get_angular_velocity

```java
Vector3 internal_m2n_physics_get_angular_velocity(Entity eid)
```

---

<a id="internal_m2n_physics_set_angular_velocity"></a>

### internal_m2n_physics_set_angular_velocity

```java
void internal_m2n_physics_set_angular_velocity(Entity eid, Vector3 velocity)
```

---

<a id="internal_m2n_physics_get_include_layers"></a>

### internal_m2n_physics_get_include_layers

```java
LayerMask internal_m2n_physics_get_include_layers(Entity eid)
```

---

<a id="internal_m2n_physics_set_include_layers"></a>

### internal_m2n_physics_set_include_layers

```java
void internal_m2n_physics_set_include_layers(Entity eid, LayerMask mask)
```

---

<a id="internal_m2n_physics_get_exclude_layers"></a>

### internal_m2n_physics_get_exclude_layers

```java
LayerMask internal_m2n_physics_get_exclude_layers(Entity eid)
```

---

<a id="internal_m2n_physics_set_exclude_layers"></a>

### internal_m2n_physics_set_exclude_layers

```java
void internal_m2n_physics_set_exclude_layers(Entity eid, LayerMask mask)
```

---

<a id="internal_m2n_physics_get_collision_layers"></a>

### internal_m2n_physics_get_collision_layers

```java
LayerMask internal_m2n_physics_get_collision_layers(Entity eid)
```

---

<a id="internal_m2n_physics_get_is_sensor"></a>

### internal_m2n_physics_get_is_sensor

```java
bool internal_m2n_physics_get_is_sensor(Entity eid)
```

---

<a id="internal_m2n_physics_set_is_sensor"></a>

### internal_m2n_physics_set_is_sensor

```java
void internal_m2n_physics_set_is_sensor(Entity eid, bool sensor)
```

---

<a id="internal_m2n_physics_get_mass"></a>

### internal_m2n_physics_get_mass

```java
float internal_m2n_physics_get_mass(Entity eid)
```

---

<a id="internal_m2n_physics_set_mass"></a>

### internal_m2n_physics_set_mass

```java
void internal_m2n_physics_set_mass(Entity eid, float mass)
```

---

<a id="internal_m2n_physics_get_is_kinematic"></a>

### internal_m2n_physics_get_is_kinematic

```java
bool internal_m2n_physics_get_is_kinematic(Entity eid)
```

---

<a id="internal_m2n_physics_set_is_kinematic"></a>

### internal_m2n_physics_set_is_kinematic

```java
void internal_m2n_physics_set_is_kinematic(Entity eid, bool kinematic)
```

---

<a id="internal_m2n_physics_get_use_gravity"></a>

### internal_m2n_physics_get_use_gravity

```java
bool internal_m2n_physics_get_use_gravity(Entity eid)
```

---

<a id="internal_m2n_physics_set_use_gravity"></a>

### internal_m2n_physics_set_use_gravity

```java
void internal_m2n_physics_set_use_gravity(Entity eid, bool useGravity)
```

