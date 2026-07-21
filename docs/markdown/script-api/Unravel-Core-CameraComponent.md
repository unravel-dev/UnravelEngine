<a id="cameracomponent"></a>

# CameraComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Represents a camera component that allows interaction with the camera, such as converting screen space positions to rays in 3D space.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`ScreenPointToRay`](#screenpointtoray) `inline` | Converts a position in screen space to a ray in 3D space. |
| `Vector3` | [`ScreenPointToWorld`](#screenpointtoworld) `inline` | Converts a 2D screen-space position to a world-space point on the camera's near plane. |
| `Vector3` | [`ScreenPointToWorld`](#screenpointtoworld-1) `inline` | Converts a screen-space position with depth (z) to a world-space point. |

---

<a id="screenpointtoray"></a>

### ScreenPointToRay

`inline`

```java
inline bool ScreenPointToRay(Vector2 pos, out Ray ray)
```

Converts a position in screen space to a ray in 3D space.

#### Returns
`true` if the ray was successfully calculated; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pos` | `[Vector2](Vector2.md#vector2)` | The position in screen space, typically in pixel coordinates. |
| `ray` | `out [Ray](Unravel-Core-Ray.md#ray)` | When this method returns, contains the ray in 3D space corresponding to the screen space position. |

---

<a id="screenpointtoworld"></a>

### ScreenPointToWorld

`inline`

```java
inline Vector3 ScreenPointToWorld(Vector2 pos)
```

Converts a 2D screen-space position to a world-space point on the camera's near plane.

#### Returns
The corresponding world-space position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pos` | `[Vector2](Vector2.md#vector2)` | The position in screen space, typically in pixel coordinates. |

---

<a id="screenpointtoworld-1"></a>

### ScreenPointToWorld

`inline`

```java
inline Vector3 ScreenPointToWorld(Vector3 pos)
```

Converts a screen-space position with depth (z) to a world-space point.

#### Returns
The corresponding world-space position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `pos` | `[Vector3](Vector3.md#vector3)` | The position in screen space; z is depth. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_camera_screen_point_to_ray`](#internal_m2n_camera_screen_point_to_ray)  |  |
| `Vector3` | [`internal_m2n_camera_screen_point_to_world_2d`](#internal_m2n_camera_screen_point_to_world_2d)  |  |
| `Vector3` | [`internal_m2n_camera_screen_point_to_world`](#internal_m2n_camera_screen_point_to_world)  |  |

---

<a id="internal_m2n_camera_screen_point_to_ray"></a>

### internal_m2n_camera_screen_point_to_ray

```java
bool internal_m2n_camera_screen_point_to_ray(Entity eid, Vector2 pos, out Ray ray)
```

---

<a id="internal_m2n_camera_screen_point_to_world_2d"></a>

### internal_m2n_camera_screen_point_to_world_2d

```java
Vector3 internal_m2n_camera_screen_point_to_world_2d(Entity eid, Vector2 pos)
```

---

<a id="internal_m2n_camera_screen_point_to_world"></a>

### internal_m2n_camera_screen_point_to_world

```java
Vector3 internal_m2n_camera_screen_point_to_world(Entity eid, Vector3 pos)
```

