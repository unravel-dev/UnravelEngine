<a id="gizmos"></a>

# Gizmos

Draws debug primitives in the scene for visualization during development.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`AddSphere`](#addsphere) `static` `inline` | Draws a debug sphere at the specified world position. |
| `void` | [`AddRay`](#addray) `static` `inline` | Draws a debug ray starting at *position* in the given direction. |

---

<a id="addsphere"></a>

### AddSphere

`static` `inline`

```java
static inline void AddSphere(Color color, Vector3 position, float radius)
```

Draws a debug sphere at the specified world position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `color` | `[Color](Color.md#color)` | The sphere color. |
| `position` | `[Vector3](Vector3.md#vector3)` | World-space center of the sphere. |
| `radius` | `float` | Sphere radius. |

---

<a id="addray"></a>

### AddRay

`static` `inline`

```java
static inline void AddRay(Color color, Vector3 position, Vector3 direction, float maxDistance = 99999.0f)
```

Draws a debug ray starting at *position*  in the given direction.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `color` | `[Color](Color.md#color)` | The ray color. |
| `position` | `[Vector3](Vector3.md#vector3)` | World-space origin of the ray. |
| `direction` | `[Vector3](Vector3.md#vector3)` | [Ray](Unravel-Core-Ray.md#ray) direction (need not be normalized). |
| `maxDistance` | `float` | Maximum ray length. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_gizmos_add_sphere`](#internal_m2n_gizmos_add_sphere)  |  |
| `void` | [`internal_m2n_gizmos_add_ray`](#internal_m2n_gizmos_add_ray)  |  |

---

<a id="internal_m2n_gizmos_add_sphere"></a>

### internal_m2n_gizmos_add_sphere

```java
void internal_m2n_gizmos_add_sphere(Color color, Vector3 position, float radius)
```

---

<a id="internal_m2n_gizmos_add_ray"></a>

### internal_m2n_gizmos_add_ray

```java
void internal_m2n_gizmos_add_ray(Color color, Vector3 position, Vector3 direction, float maxDistance)
```

