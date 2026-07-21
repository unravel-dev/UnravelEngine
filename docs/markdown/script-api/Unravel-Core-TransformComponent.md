<a id="transformcomponent"></a>

# TransformComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Represents a component that defines the position, rotation, scale, and other transformations of an entity in 3D space.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`parent`](#parent)  | Gets or sets the parent entity of the current entity. |
| `Entity[]` | [`children`](#children)  | Gets the child entities of the current entity. |
| `Vector3` | [`position`](#position)  | Gets or sets the world space position of the Transform. |
| `Vector3` | [`localPosition`](#localposition)  | Gets or sets the position of the Transform relative to the parent transform. |
| `Vector3` | [`eulerAngles`](#eulerangles-1)  | Gets or sets the rotation of the Transform in world space, expressed in Euler angles. |
| `Vector3` | [`localEulerAngles`](#localeulerangles)  | Gets or sets the rotation of the Transform relative to the parent, expressed in local Euler angles. |
| `Quaternion` | [`rotation`](#rotation)  | Gets or sets the rotation of the Transform in world space, expressed as a [Quaternion](Quaternion.md#quaternion). |
| `Quaternion` | [`localRotation`](#localrotation)  | Gets or sets the rotation of the Transform relative to the parent, expressed as a [Quaternion](Quaternion.md#quaternion). |
| `Vector3` | [`scale`](#scale-6)  | Gets or sets the global scale of the Transform. |
| `Vector3` | [`localScale`](#localscale)  | Gets or sets the scale of the Transform relative to the parent. |
| `Vector3` | [`skew`](#skew)  | Gets or sets the global skew of the Transform. |
| `Vector3` | [`localSkew`](#localskew)  | Gets or sets the skew of the Transform relative to the parent. |
| `Vector3` | [`right`](#right-2)  | Gets or sets the right vector of the Transform in world space. |
| `Vector3` | [`localRight`](#localright)  | Gets or sets the right vector of the Transform relative to the parent. |
| `Vector3` | [`up`](#up-2)  | Gets or sets the up vector of the Transform in world space. |
| `Vector3` | [`localUp`](#localup)  | Gets or sets the up vector of the Transform relative to the parent. |
| `Vector3` | [`forward`](#forward-1)  | Gets or sets the forward vector of the Transform in world space. |
| `Vector3` | [`localForward`](#localforward)  | Gets or sets the forward vector of the Transform relative to the parent. |

---

<a id="parent"></a>

### parent

```java
Entity parent
```

Gets or sets the parent entity of the current entity.

The [Entity](Unravel-Core-Entity.md#entity-1) object representing the parent entity.

<br/>

---

<a id="children"></a>

### children

```java
Entity[] children
```

Gets the child entities of the current entity.

An array of [Entity](Unravel-Core-Entity.md#entity-1) objects representing the child entities.

---

<a id="position"></a>

### position

```java
Vector3 position
```

Gets or sets the world space position of the Transform.

---

<a id="localposition"></a>

### localPosition

```java
Vector3 localPosition
```

Gets or sets the position of the Transform relative to the parent transform.

---

<a id="eulerangles-1"></a>

### eulerAngles

```java
Vector3 eulerAngles
```

Gets or sets the rotation of the Transform in world space, expressed in Euler angles.

---

<a id="localeulerangles"></a>

### localEulerAngles

```java
Vector3 localEulerAngles
```

Gets or sets the rotation of the Transform relative to the parent, expressed in local Euler angles.

---

<a id="rotation"></a>

### rotation

```java
Quaternion rotation
```

Gets or sets the rotation of the Transform in world space, expressed as a [Quaternion](Quaternion.md#quaternion).

---

<a id="localrotation"></a>

### localRotation

```java
Quaternion localRotation
```

Gets or sets the rotation of the Transform relative to the parent, expressed as a [Quaternion](Quaternion.md#quaternion).

---

<a id="scale-6"></a>

### scale

```java
Vector3 scale
```

Gets or sets the global scale of the Transform.

---

<a id="localscale"></a>

### localScale

```java
Vector3 localScale
```

Gets or sets the scale of the Transform relative to the parent.

---

<a id="skew"></a>

### skew

```java
Vector3 skew
```

Gets or sets the global skew of the Transform.

---

<a id="localskew"></a>

### localSkew

```java
Vector3 localSkew
```

Gets or sets the skew of the Transform relative to the parent.

---

<a id="right-2"></a>

### right

```java
Vector3 right
```

Gets or sets the right vector of the Transform in world space.

---

<a id="localright"></a>

### localRight

```java
Vector3 localRight
```

Gets or sets the right vector of the Transform relative to the parent.

---

<a id="up-2"></a>

### up

```java
Vector3 up
```

Gets or sets the up vector of the Transform in world space.

---

<a id="localup"></a>

### localUp

```java
Vector3 localUp
```

Gets or sets the up vector of the Transform relative to the parent.

---

<a id="forward-1"></a>

### forward

```java
Vector3 forward
```

Gets or sets the forward vector of the Transform in world space.

---

<a id="localforward"></a>

### localForward

```java
Vector3 localForward
```

Gets or sets the forward vector of the Transform relative to the parent.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`FindChild`](#findchild) `inline` | Finds a child by name/path and returns it. If no child with name n can be found, null is returned. If n contains a '/' character it will access the Transform in the hierarchy like a path name Note: Find does not work properly if you have '/' in the name of an [Entity](Unravel-Core-Entity.md#entity-1). Note: Find can find transform of an inactive/disabled [Entity](Unravel-Core-Entity.md#entity-1). |
| `void` | [`SetParent`](#setparent) `inline` | Sets the parent entity of the current entity. |
| `void` | [`MoveBy`](#moveby) `inline` | Moves the Transform in world space by a specified amount. |
| `void` | [`MoveByLocal`](#movebylocal) `inline` | Moves the Transform relative to the parent by a specified amount. |
| `void` | [`ScaleBy`](#scaleby) `inline` | Scales the Transform in world space by a specified amount. |
| `void` | [`ScaleByLocal`](#scalebylocal) `inline` | Scales the Transform relative to the parent by a specified amount. |
| `void` | [`RotateBy`](#rotateby) `inline` | Rotates the Transform in world space by a specified [Quaternion](Quaternion.md#quaternion). |
| `void` | [`RotateByLocal`](#rotatebylocal) `inline` | Rotates the Transform relative to the parent by a specified [Quaternion](Quaternion.md#quaternion). |
| `void` | [`RotateByEuler`](#rotatebyeuler) `inline` | Rotates the Transform in world space by specified Euler angles. |
| `void` | [`RotateByEulerLocal`](#rotatebyeulerlocal) `inline` | Rotates the Transform relative to the parent by specified Euler angles. |
| `void` | [`RotateAxis`](#rotateaxis) `inline` | Rotates the Transform in world space around a specified axis by a specified angle. |
| `void` | [`LookAt`](#lookat) `inline` | Rotates the Transform to look at the specified point in world space. |
| `void` | [`LookAt`](#lookat-1) `inline` | Rotates the Transform to look at the specified point in world space, using the specified up vector. |
| `void` | [`LookAt`](#lookat-2) `inline` | Rotates the Transform to look at the specified target entity in world space. |
| `void` | [`LookAt`](#lookat-3) `inline` | Rotates the Transform to look at the specified target entity in world space, using the specified up vector. |
| `void` | [`LookAt`](#lookat-4) `inline` | Rotates the Transform to look at the specified target [TransformComponent](#transformcomponent) in world space. |
| `void` | [`LookAt`](#lookat-5) `inline` | Rotates the Transform to look at the specified target [TransformComponent](#transformcomponent) in world space, using the specified up vector. |
| `Vector3` | [`TransformVector`](#transformvector) `inline` | Transforms vector from local space to world space. |
| `Vector3` | [`InverseTransformVector`](#inversetransformvector) `inline` | Transforms vector from world space to local space. |
| `Vector3` | [`TransformDirection`](#transformdirection) `inline` | Transforms direction from local space to world space. |
| `Vector3` | [`InverseTransformDirection`](#inversetransformdirection) `inline` | Transforms direction from world space to local space. |
| `void` | [`RotateAround`](#rotatearound) `inline` | Rotates the Transform around a specified point and axis by a specified angle. |
| `void` | [`RotateAround`](#rotatearound-1) `inline` | Rotates the Transform around a specified target entity and axis by a specified angle. |
| `void` | [`RotateAround`](#rotatearound-2) `inline` | Rotates the Transform around a specified target [TransformComponent](#transformcomponent) and axis by a specified angle. |
| `void` | [`MoveTowards`](#movetowards-4) `inline` | Moves the Transform towards the specified target entity by a maximum distance. |

---

<a id="findchild"></a>

### FindChild

`inline`

```java
inline Entity FindChild(string path, bool recursive = false)
```

Finds a child by name/path and returns it. If no child with name n can be found, null is returned. If n contains a '/' character it will access the Transform in the hierarchy like a path name Note: Find does not work properly if you have '/' in the name of an [Entity](Unravel-Core-Entity.md#entity-1). Note: Find can find transform of an inactive/disabled [Entity](Unravel-Core-Entity.md#entity-1).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `string` | The string name to find. |
| `recursive` | `bool` | A boolean value indicating whether to perform a recursive descend down a Transform hierarchy. |

---

<a id="setparent"></a>

### SetParent

`inline`

```java
inline void SetParent(Entity parent, bool globalPositionStays)
```

Sets the parent entity of the current entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The [Entity](Unravel-Core-Entity.md#entity-1) to set as the parent. |
| `globalPositionStays` | `bool` | A boolean value indicating whether the entity should retain its global position when reparented. If `true`, the entity's global position will not change. If `false`, the entity's global position will be adjusted relative to the new parent. |

---

<a id="moveby"></a>

### MoveBy

`inline`

```java
inline void MoveBy(Vector3 amount)
```

Moves the Transform in world space by a specified amount.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The amount to move the Transform. |

---

<a id="movebylocal"></a>

### MoveByLocal

`inline`

```java
inline void MoveByLocal(Vector3 amount)
```

Moves the Transform relative to the parent by a specified amount.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The amount to move the Transform. |

---

<a id="scaleby"></a>

### ScaleBy

`inline`

```java
inline void ScaleBy(Vector3 amount)
```

Scales the Transform in world space by a specified amount.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The scaling factors to apply. |

---

<a id="scalebylocal"></a>

### ScaleByLocal

`inline`

```java
inline void ScaleByLocal(Vector3 amount)
```

Scales the Transform relative to the parent by a specified amount.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The scaling factors to apply. |

---

<a id="rotateby"></a>

### RotateBy

`inline`

```java
inline void RotateBy(Quaternion amount)
```

Rotates the Transform in world space by a specified [Quaternion](Quaternion.md#quaternion).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Quaternion](Quaternion.md#quaternion)` | The rotation to apply. |

---

<a id="rotatebylocal"></a>

### RotateByLocal

`inline`

```java
inline void RotateByLocal(Quaternion amount)
```

Rotates the Transform relative to the parent by a specified [Quaternion](Quaternion.md#quaternion).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Quaternion](Quaternion.md#quaternion)` | The rotation to apply. |

---

<a id="rotatebyeuler"></a>

### RotateByEuler

`inline`

```java
inline void RotateByEuler(Vector3 amount)
```

Rotates the Transform in world space by specified Euler angles.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The Euler angles to rotate by. |

---

<a id="rotatebyeulerlocal"></a>

### RotateByEulerLocal

`inline`

```java
inline void RotateByEulerLocal(Vector3 amount)
```

Rotates the Transform relative to the parent by specified Euler angles.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The Euler angles to rotate by. |

---

<a id="rotateaxis"></a>

### RotateAxis

`inline`

```java
inline void RotateAxis(float degrees, Vector3 axis)
```

Rotates the Transform in world space around a specified axis by a specified angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `degrees` | `float` | The angle in degrees to rotate. |
| `axis` | `[Vector3](Vector3.md#vector3)` | The axis to rotate around. |

---

<a id="lookat"></a>

### LookAt

`inline`

```java
inline void LookAt(Vector3 point)
```

Rotates the Transform to look at the specified point in world space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `point` | `[Vector3](Vector3.md#vector3)` | The point to look at. |

---

<a id="lookat-1"></a>

### LookAt

`inline`

```java
inline void LookAt(Vector3 point, Vector3 up)
```

Rotates the Transform to look at the specified point in world space, using the specified up vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `point` | `[Vector3](Vector3.md#vector3)` | The point to look at. |
| `up` | `[Vector3](Vector3.md#vector3)` | The vector that defines in which direction up is. |

---

<a id="lookat-2"></a>

### LookAt

`inline`

```java
inline void LookAt(Entity target)
```

Rotates the Transform to look at the specified target entity in world space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The target entity to look at. |

---

<a id="lookat-3"></a>

### LookAt

`inline`

```java
inline void LookAt(Entity target, Vector3 up)
```

Rotates the Transform to look at the specified target entity in world space, using the specified up vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The target entity to look at. |
| `up` | `[Vector3](Vector3.md#vector3)` | The vector that defines in which direction up is. |

---

<a id="lookat-4"></a>

### LookAt

`inline`

```java
inline void LookAt(TransformComponent target)
```

Rotates the Transform to look at the specified target [TransformComponent](#transformcomponent) in world space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[TransformComponent](#transformcomponent)` | The target [TransformComponent](#transformcomponent) to look at. |

---

<a id="lookat-5"></a>

### LookAt

`inline`

```java
inline void LookAt(TransformComponent target, Vector3 up)
```

Rotates the Transform to look at the specified target [TransformComponent](#transformcomponent) in world space, using the specified up vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[TransformComponent](#transformcomponent)` | The target [TransformComponent](#transformcomponent) to look at. |
| `up` | `[Vector3](Vector3.md#vector3)` | The vector that defines in which direction up is. |

---

<a id="transformvector"></a>

### TransformVector

`inline`

```java
inline Vector3 TransformVector(Vector3 vector)
```

Transforms vector from local space to world space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](Vector3.md#vector3)` | The vector to transform. |

---

<a id="inversetransformvector"></a>

### InverseTransformVector

`inline`

```java
inline Vector3 InverseTransformVector(Vector3 vector)
```

Transforms vector from world space to local space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](Vector3.md#vector3)` | The vector to transform. |

---

<a id="transformdirection"></a>

### TransformDirection

`inline`

```java
inline Vector3 TransformDirection(Vector3 direction)
```

Transforms direction from local space to world space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction to transform. |

---

<a id="inversetransformdirection"></a>

### InverseTransformDirection

`inline`

```java
inline Vector3 InverseTransformDirection(Vector3 direction)
```

Transforms direction from world space to local space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction to transform. |

---

<a id="rotatearound"></a>

### RotateAround

`inline`

```java
inline void RotateAround(Vector3 point, Vector3 axis, float angle)
```

Rotates the Transform around a specified point and axis by a specified angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `point` | `[Vector3](Vector3.md#vector3)` | The point to rotate around. |
| `axis` | `[Vector3](Vector3.md#vector3)` | The axis to rotate around. |
| `angle` | `float` | The angle in degrees to rotate. |

---

<a id="rotatearound-1"></a>

### RotateAround

`inline`

```java
inline void RotateAround(Entity target, Vector3 axis, float angle)
```

Rotates the Transform around a specified target entity and axis by a specified angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The target entity to rotate around. |
| `axis` | `[Vector3](Vector3.md#vector3)` | The axis to rotate around. |
| `angle` | `float` | The angle in degrees to rotate. |

---

<a id="rotatearound-2"></a>

### RotateAround

`inline`

```java
inline void RotateAround(TransformComponent target, Vector3 axis, float angle)
```

Rotates the Transform around a specified target [TransformComponent](#transformcomponent) and axis by a specified angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[TransformComponent](#transformcomponent)` | The target [TransformComponent](#transformcomponent) to rotate around. |
| `axis` | `[Vector3](Vector3.md#vector3)` | The axis to rotate around. |
| `angle` | `float` | The angle in degrees to rotate. |

---

<a id="movetowards-4"></a>

### MoveTowards

`inline`

```java
inline void MoveTowards(Entity target, float maxDistanceDelta)
```

Moves the Transform towards the specified target entity by a maximum distance.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `target` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The target entity to move towards. |
| `maxDistanceDelta` | `float` | The maximum distance to move. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `byte[]` | [`internal_m2n_get_children`](#internal_m2n_get_children)  |  |
| `Entity` | [`internal_m2n_get_child`](#internal_m2n_get_child)  |  |
| `Entity` | [`internal_m2n_get_parent`](#internal_m2n_get_parent)  |  |
| `void` | [`internal_m2n_set_parent`](#internal_m2n_set_parent)  |  |
| `Vector3` | [`internal_m2n_get_position_global`](#internal_m2n_get_position_global)  |  |
| `void` | [`internal_m2n_set_position_global`](#internal_m2n_set_position_global)  |  |
| `void` | [`internal_m2n_move_by_global`](#internal_m2n_move_by_global)  |  |
| `Vector3` | [`internal_m2n_get_position_local`](#internal_m2n_get_position_local)  |  |
| `void` | [`internal_m2n_set_position_local`](#internal_m2n_set_position_local)  |  |
| `void` | [`internal_m2n_move_by_local`](#internal_m2n_move_by_local)  |  |
| `Vector3` | [`internal_m2n_get_rotation_euler_global`](#internal_m2n_get_rotation_euler_global)  |  |
| `void` | [`internal_m2n_set_rotation_euler_global`](#internal_m2n_set_rotation_euler_global)  |  |
| `void` | [`internal_m2n_rotate_by_euler_global`](#internal_m2n_rotate_by_euler_global)  |  |
| `void` | [`internal_m2n_rotate_axis_global`](#internal_m2n_rotate_axis_global)  |  |
| `void` | [`internal_m2n_look_at`](#internal_m2n_look_at)  |  |
| `Vector3` | [`internal_m2n_transform_vector_global`](#internal_m2n_transform_vector_global)  |  |
| `Vector3` | [`internal_m2n_inverse_transform_vector_global`](#internal_m2n_inverse_transform_vector_global)  |  |
| `Vector3` | [`internal_m2n_transform_direction_global`](#internal_m2n_transform_direction_global)  |  |
| `Vector3` | [`internal_m2n_inverse_transform_direction_global`](#internal_m2n_inverse_transform_direction_global)  |  |
| `Vector3` | [`internal_m2n_get_rotation_euler_local`](#internal_m2n_get_rotation_euler_local)  |  |
| `void` | [`internal_m2n_set_rotation_euler_local`](#internal_m2n_set_rotation_euler_local)  |  |
| `void` | [`internal_m2n_rotate_by_euler_local`](#internal_m2n_rotate_by_euler_local)  |  |
| `Quaternion` | [`internal_m2n_get_rotation_global`](#internal_m2n_get_rotation_global)  |  |
| `void` | [`internal_m2n_set_rotation_global`](#internal_m2n_set_rotation_global)  |  |
| `void` | [`internal_m2n_rotate_by_global`](#internal_m2n_rotate_by_global)  |  |
| `Quaternion` | [`internal_m2n_get_rotation_local`](#internal_m2n_get_rotation_local)  |  |
| `void` | [`internal_m2n_set_rotation_local`](#internal_m2n_set_rotation_local)  |  |
| `void` | [`internal_m2n_rotate_by_local`](#internal_m2n_rotate_by_local)  |  |
| `Vector3` | [`internal_m2n_get_scale_global`](#internal_m2n_get_scale_global)  |  |
| `void` | [`internal_m2n_set_scale_global`](#internal_m2n_set_scale_global)  |  |
| `void` | [`internal_m2n_scale_by_global`](#internal_m2n_scale_by_global)  |  |
| `Vector3` | [`internal_m2n_get_scale_local`](#internal_m2n_get_scale_local)  |  |
| `void` | [`internal_m2n_set_scale_local`](#internal_m2n_set_scale_local)  |  |
| `void` | [`internal_m2n_scale_by_local`](#internal_m2n_scale_by_local)  |  |
| `Vector3` | [`internal_m2n_get_skew_global`](#internal_m2n_get_skew_global)  |  |
| `void` | [`internal_m2n_set_skew_global`](#internal_m2n_set_skew_global)  |  |
| `Vector3` | [`internal_m2n_get_skew_local`](#internal_m2n_get_skew_local)  |  |
| `void` | [`internal_m2n_set_skew_local`](#internal_m2n_set_skew_local)  |  |

---

<a id="internal_m2n_get_children"></a>

### internal_m2n_get_children

```java
byte[] internal_m2n_get_children(Entity eid)
```

---

<a id="internal_m2n_get_child"></a>

### internal_m2n_get_child

```java
Entity internal_m2n_get_child(Entity eid, string path, bool recursive)
```

---

<a id="internal_m2n_get_parent"></a>

### internal_m2n_get_parent

```java
Entity internal_m2n_get_parent(Entity eid)
```

---

<a id="internal_m2n_set_parent"></a>

### internal_m2n_set_parent

```java
void internal_m2n_set_parent(Entity eid, Entity newParent, bool globalStays)
```

---

<a id="internal_m2n_get_position_global"></a>

### internal_m2n_get_position_global

```java
Vector3 internal_m2n_get_position_global(Entity eid)
```

---

<a id="internal_m2n_set_position_global"></a>

### internal_m2n_set_position_global

```java
void internal_m2n_set_position_global(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_move_by_global"></a>

### internal_m2n_move_by_global

```java
void internal_m2n_move_by_global(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_get_position_local"></a>

### internal_m2n_get_position_local

```java
Vector3 internal_m2n_get_position_local(Entity eid)
```

---

<a id="internal_m2n_set_position_local"></a>

### internal_m2n_set_position_local

```java
void internal_m2n_set_position_local(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_move_by_local"></a>

### internal_m2n_move_by_local

```java
void internal_m2n_move_by_local(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_get_rotation_euler_global"></a>

### internal_m2n_get_rotation_euler_global

```java
Vector3 internal_m2n_get_rotation_euler_global(Entity eid)
```

---

<a id="internal_m2n_set_rotation_euler_global"></a>

### internal_m2n_set_rotation_euler_global

```java
void internal_m2n_set_rotation_euler_global(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_rotate_by_euler_global"></a>

### internal_m2n_rotate_by_euler_global

```java
void internal_m2n_rotate_by_euler_global(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_rotate_axis_global"></a>

### internal_m2n_rotate_axis_global

```java
void internal_m2n_rotate_axis_global(Entity eid, float degrees, Vector3 axis)
```

---

<a id="internal_m2n_look_at"></a>

### internal_m2n_look_at

```java
void internal_m2n_look_at(Entity eid, Vector3 point, Vector3 up)
```

---

<a id="internal_m2n_transform_vector_global"></a>

### internal_m2n_transform_vector_global

```java
Vector3 internal_m2n_transform_vector_global(Entity eid, Vector3 vector)
```

---

<a id="internal_m2n_inverse_transform_vector_global"></a>

### internal_m2n_inverse_transform_vector_global

```java
Vector3 internal_m2n_inverse_transform_vector_global(Entity eid, Vector3 vector)
```

---

<a id="internal_m2n_transform_direction_global"></a>

### internal_m2n_transform_direction_global

```java
Vector3 internal_m2n_transform_direction_global(Entity eid, Vector3 direction)
```

---

<a id="internal_m2n_inverse_transform_direction_global"></a>

### internal_m2n_inverse_transform_direction_global

```java
Vector3 internal_m2n_inverse_transform_direction_global(Entity eid, Vector3 direction)
```

---

<a id="internal_m2n_get_rotation_euler_local"></a>

### internal_m2n_get_rotation_euler_local

```java
Vector3 internal_m2n_get_rotation_euler_local(Entity eid)
```

---

<a id="internal_m2n_set_rotation_euler_local"></a>

### internal_m2n_set_rotation_euler_local

```java
void internal_m2n_set_rotation_euler_local(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_rotate_by_euler_local"></a>

### internal_m2n_rotate_by_euler_local

```java
void internal_m2n_rotate_by_euler_local(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_get_rotation_global"></a>

### internal_m2n_get_rotation_global

```java
Quaternion internal_m2n_get_rotation_global(Entity eid)
```

---

<a id="internal_m2n_set_rotation_global"></a>

### internal_m2n_set_rotation_global

```java
void internal_m2n_set_rotation_global(Entity eid, Quaternion value)
```

---

<a id="internal_m2n_rotate_by_global"></a>

### internal_m2n_rotate_by_global

```java
void internal_m2n_rotate_by_global(Entity eid, Quaternion amount)
```

---

<a id="internal_m2n_get_rotation_local"></a>

### internal_m2n_get_rotation_local

```java
Quaternion internal_m2n_get_rotation_local(Entity eid)
```

---

<a id="internal_m2n_set_rotation_local"></a>

### internal_m2n_set_rotation_local

```java
void internal_m2n_set_rotation_local(Entity eid, Quaternion value)
```

---

<a id="internal_m2n_rotate_by_local"></a>

### internal_m2n_rotate_by_local

```java
void internal_m2n_rotate_by_local(Entity eid, Quaternion amount)
```

---

<a id="internal_m2n_get_scale_global"></a>

### internal_m2n_get_scale_global

```java
Vector3 internal_m2n_get_scale_global(Entity eid)
```

---

<a id="internal_m2n_set_scale_global"></a>

### internal_m2n_set_scale_global

```java
void internal_m2n_set_scale_global(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_scale_by_global"></a>

### internal_m2n_scale_by_global

```java
void internal_m2n_scale_by_global(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_get_scale_local"></a>

### internal_m2n_get_scale_local

```java
Vector3 internal_m2n_get_scale_local(Entity eid)
```

---

<a id="internal_m2n_set_scale_local"></a>

### internal_m2n_set_scale_local

```java
void internal_m2n_set_scale_local(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_scale_by_local"></a>

### internal_m2n_scale_by_local

```java
void internal_m2n_scale_by_local(Entity eid, Vector3 amount)
```

---

<a id="internal_m2n_get_skew_global"></a>

### internal_m2n_get_skew_global

```java
Vector3 internal_m2n_get_skew_global(Entity eid)
```

---

<a id="internal_m2n_set_skew_global"></a>

### internal_m2n_set_skew_global

```java
void internal_m2n_set_skew_global(Entity eid, Vector3 value)
```

---

<a id="internal_m2n_get_skew_local"></a>

### internal_m2n_get_skew_local

```java
Vector3 internal_m2n_get_skew_local(Entity eid)
```

---

<a id="internal_m2n_set_skew_local"></a>

### internal_m2n_set_skew_local

```java
void internal_m2n_set_skew_local(Entity eid, Vector3 value)
```

