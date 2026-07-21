<a id="physics"></a>

# Physics

Provides static methods for performing physics-related operations, such as raycasting.

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `const int` | [`DefaultRaycastLayers`](#defaultraycastlayers) `static` | The default layer mask used for raycasting. |

---

<a id="defaultraycastlayers"></a>

### DefaultRaycastLayers

`static`

```java
const int DefaultRaycastLayers = -1
```

The default layer mask used for raycasting.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `? RaycastHit` | [`Raycast`](#raycast) `static` `inline` | Casts a ray and returns the first object hit, if any. |
| `? RaycastHit` | [`Raycast`](#raycast-1) `static` `inline` | Casts a ray from a specific origin in a specific direction and returns the first object hit, if any. |
| `RaycastHit[]` | [`RaycastAll`](#raycastall) `static` `inline` | Casts a ray and returns all objects hit along the ray's path. |
| `RaycastHit[]` | [`RaycastAll`](#raycastall-1) `static` `inline` | Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path. |
| `? RaycastHit` | [`SphereCast`](#spherecast) `static` `inline` | Casts a sphere along a ray and returns the first object hit, if any. |
| `? RaycastHit` | [`SphereCast`](#spherecast-1) `static` `inline` | Casts a sphere along a ray from a specific origin in a specific direction and returns the first object hit, if any. |
| `RaycastHit[]` | [`SphereCastAll`](#spherecastall) `static` `inline` | Casts a sphere along a ray and returns all objects hit along the ray's path. |
| `RaycastHit[]` | [`SphereCastAll`](#spherecastall-1) `static` `inline` | Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path. |
| `Entity[]` | [`SphereOverlap`](#sphereoverlap) `static` `inline` | [Tests](Unravel-Core-Tests.md#tests) a sphere from a specific origin and returns all objects touching or inside it. |

---

<a id="raycast"></a>

### Raycast

`static` `inline`

```java
static inline ? RaycastHit Raycast(Ray ray, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a ray and returns the first object hit, if any.

#### Returns
A nullable [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) containing information about the object hit, or `null` if no object was hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `ray` | `[Ray](Unravel-Core-Ray.md#ray)` | The ray to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="raycast-1"></a>

### Raycast

`static` `inline`

```java
static inline ? RaycastHit Raycast(Vector3 origin, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a ray from a specific origin in a specific direction and returns the first object hit, if any.

#### Returns
A nullable [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) containing information about the object hit, or `null` if no object was hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the ray. |
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction in which to cast the ray. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="raycastall"></a>

### RaycastAll

`static` `inline`

```java
static inline RaycastHit[] RaycastAll(Ray ray, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a ray and returns all objects hit along the ray's path.

#### Returns
An array of [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) objects containing information about each object hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `ray` | `[Ray](Unravel-Core-Ray.md#ray)` | The ray to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="raycastall-1"></a>

### RaycastAll

`static` `inline`

```java
static inline RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path.

#### Returns
An array of [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) objects containing information about each object hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the ray. |
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction in which to cast the ray. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="spherecast"></a>

### SphereCast

`static` `inline`

```java
static inline ? RaycastHit SphereCast(Ray ray, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a sphere along a ray and returns the first object hit, if any.

#### Returns
A nullable [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) containing information about the object hit, or `null` if no object was hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `ray` | `[Ray](Unravel-Core-Ray.md#ray)` | The ray to cast. |
| `radius` | `float` | The radius of the sphere to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="spherecast-1"></a>

### SphereCast

`static` `inline`

```java
static inline ? RaycastHit SphereCast(Vector3 origin, Vector3 direction, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a sphere along a ray from a specific origin in a specific direction and returns the first object hit, if any.

#### Returns
A nullable [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) containing information about the object hit, or `null` if no object was hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the ray. |
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction in which to cast the ray. |
| `radius` | `float` | The radius of the sphere to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="spherecastall"></a>

### SphereCastAll

`static` `inline`

```java
static inline RaycastHit[] SphereCastAll(Ray ray, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a sphere along a ray and returns all objects hit along the ray's path.

#### Returns
An array of [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) objects containing information about each object hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `ray` | `[Ray](Unravel-Core-Ray.md#ray)` | The ray to cast. |
| `radius` | `float` | The radius of the sphere to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="spherecastall-1"></a>

### SphereCastAll

`static` `inline`

```java
static inline RaycastHit[] SphereCastAll(Vector3 origin, Vector3 direction, float radius, float maxDistance = Mathf.Infinity, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

Casts a ray from a specific origin in a specific direction and returns all objects hit along the ray's path.

#### Returns
An array of [RaycastHit](Unravel-Core-RaycastHit.md#raycasthit) objects containing information about each object hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the ray. |
| `direction` | `[Vector3](Vector3.md#vector3)` | The direction in which to cast the ray. |
| `radius` | `float` | The radius of the sphere to cast. |
| `maxDistance` | `float` | The maximum distance the ray should check for collisions. Defaults to [Mathf.Infinity](Mathf.md#infinity). |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the raycast will include sensors in its results. Defaults to `false`. |

---

<a id="sphereoverlap"></a>

### SphereOverlap

`static` `inline`

```java
static inline Entity[] SphereOverlap(Vector3 origin, float radius, int layerMask = DefaultRaycastLayers, bool querySensors = false)
```

[Tests](Unravel-Core-Tests.md#tests) a sphere from a specific origin and returns all objects touching or inside it.

#### Returns
An array of [Entity](Unravel-Core-Entity.md#entity-1) objects containing information about each object hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the sphere. |
| `radius` | `float` | The radius of the sphere to cast. |
| `layerMask` | `int` | A layer mask that defines which layers to include in the raycast. Defaults to [DefaultRaycastLayers](#defaultraycastlayers). |
| `querySensors` | `bool` | If `true`, the cast will include sensors in its results. Defaults to `false`. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_physics_ray_cast`](#internal_m2n_physics_ray_cast)  |  |
| `byte[]` | [`internal_m2n_physics_ray_cast_all`](#internal_m2n_physics_ray_cast_all)  |  |
| `bool` | [`internal_m2n_physics_sphere_cast`](#internal_m2n_physics_sphere_cast)  |  |
| `byte[]` | [`internal_m2n_physics_sphere_cast_all`](#internal_m2n_physics_sphere_cast_all)  |  |
| `byte[]` | [`internal_m2n_physics_sphere_overlap`](#internal_m2n_physics_sphere_overlap)  |  |

---

<a id="internal_m2n_physics_ray_cast"></a>

### internal_m2n_physics_ray_cast

```java
bool internal_m2n_physics_ray_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors)
```

---

<a id="internal_m2n_physics_ray_cast_all"></a>

### internal_m2n_physics_ray_cast_all

```java
byte[] internal_m2n_physics_ray_cast_all(Vector3 origin, Vector3 direction, float maxDistance, int layerMask, bool querySensors)
```

---

<a id="internal_m2n_physics_sphere_cast"></a>

### internal_m2n_physics_sphere_cast

```java
bool internal_m2n_physics_sphere_cast(out RaycastHit hit, Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors)
```

---

<a id="internal_m2n_physics_sphere_cast_all"></a>

### internal_m2n_physics_sphere_cast_all

```java
byte[] internal_m2n_physics_sphere_cast_all(Vector3 origin, Vector3 direction, float radius, float maxDistance, int layerMask, bool querySensors)
```

---

<a id="internal_m2n_physics_sphere_overlap"></a>

### internal_m2n_physics_sphere_overlap

```java
byte[] internal_m2n_physics_sphere_overlap(Vector3 origin, float radius, int layerMask, bool querySensors)
```

