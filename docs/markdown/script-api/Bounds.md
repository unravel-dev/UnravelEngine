<a id="bounds"></a>

# Bounds

> **Extends:** `IEquatable< Bounds >`

Axis-aligned bounding box defined by minimum and maximum corners.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`Min`](#min-5)  | The minimum corner of the box. |
| `Vector3` | [`Max`](#max-5)  | The maximum corner of the box. |

---

<a id="min-5"></a>

### Min

```java
Vector3 Min
```

The minimum corner of the box.

---

<a id="max-5"></a>

### Max

```java
Vector3 Max
```

The maximum corner of the box.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Bounds`](#bounds-1) `inline` | Creates a box from two corners. |
|  | [`Bounds`](#bounds-2) `inline` | Creates a box by specifying each coordinate. |
| `void` | [`Reset`](#reset) `inline` | Resets to an "unpopulated" state—ready for Grow/Encapsulate. |
| `bool` | [`IsPopulated`](#ispopulated) `inline` | True if [Reset()](#reset) has been called (i.e. not both corners = zero). |
| `bool` | [`IsDegenerate`](#isdegenerate)  | True if the box has zero volume (within epsilon). |
| `void` | [`AddPoint`](#addpoint) `inline` | Expand to include this point. |
| `bool` | [`ContainsPoint`](#containspoint)  | True if the point is inside [Min,Max]. |
| `bool` | [`ContainsPoint`](#containspoint-1)  | Contains with per-axis tolerance. |
| `bool` | [`ContainsPoint`](#containspoint-2)  | Contains with uniform tolerance. |
| `Vector3` | [`GetDimensions`](#getdimensions)  | The size of the box = Max − Min. |
| `Vector3` | [`GetCenter`](#getcenter)  | Center = (Min + Max) / 2. |
| `Vector3` | [`GetExtents`](#getextents)  | Extents = half-dimensions. |
| `void` | [`Inflate`](#inflate) `inline` | Grow/shrink all sides by a uniform amount. |
| `void` | [`Inflate`](#inflate-1) `inline` | Grow/shrink all sides by per-axis amounts. |
| `Vector3[]` | [`GetCorners`](#getcorners)  | The eight corners in the same order as your C++ code. |
| `bool` | [`Intersect`](#intersect)  | AABB-AABB overlap test. |
| `bool` | [`Intersect`](#intersect-1) `inline` | Overlap + full containment test. Returns true if they overlap; sets contained=true if b is entirely inside this. |
| `bool` | [`Intersect`](#intersect-2) `inline` | Overlap + return the intersection box. |
| `bool` | [`Intersect`](#intersect-3) `inline` | Overlap with a tolerance vector. |
| `bool` | [`Slab`](#slab) `inline` |  |
| `bool` | [`Intersect`](#intersect-4) `inline` | Ray vs AABB (slab method). Returns true if hit; t = entry time in [0,1] if restrictRange. |
| `override bool` | [`Equals`](#equals-4)  | Returns true if the given object is equal to this bounds. |
| `override int` | [`GetHashCode`](#gethashcode-3)  | Returns the hash code for this instance. |
| `bool` | [`Equals`](#equals-5)  | Returns true if the given bounds is equal to this bounds. |

---

<a id="bounds-1"></a>

### Bounds

`inline`

```java
inline Bounds(Vector3 min, Vector3 max)
```

Creates a box from two corners.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `min` | `[Vector3](Vector3.md#vector3)` | The minimum corner. |
| `max` | `[Vector3](Vector3.md#vector3)` | The maximum corner. |

---

<a id="bounds-2"></a>

### Bounds

`inline`

```java
inline Bounds(float xMin, float yMin, float zMin, float xMax, float yMax, float zMax)
```

Creates a box by specifying each coordinate.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `xMin` | `float` | The minimum x coordinate. |
| `yMin` | `float` | The minimum y coordinate. |
| `zMin` | `float` | The minimum z coordinate. |
| `xMax` | `float` | The maximum x coordinate. |
| `yMax` | `float` | The maximum y coordinate. |
| `zMax` | `float` | The maximum z coordinate. |

---

<a id="reset"></a>

### Reset

`inline`

```java
inline void Reset()
```

Resets to an "unpopulated" state—ready for Grow/Encapsulate.

---

<a id="ispopulated"></a>

### IsPopulated

`inline`

```java
inline bool IsPopulated()
```

True if [Reset()](#reset) has been called (i.e. not both corners = zero).

#### Returns
True if the bounds have been populated; otherwise, false.

---

<a id="isdegenerate"></a>

### IsDegenerate

```java
bool IsDegenerate(float epsilon = float.Epsilon)
```

True if the box has zero volume (within epsilon).

#### Returns
True if the box has zero volume; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `epsilon` | `float` | The tolerance value. |

---

<a id="addpoint"></a>

### AddPoint

`inline`

```java
inline void AddPoint(Vector3 point)
```

Expand to include this point.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `point` | `[Vector3](Vector3.md#vector3)` | The point to include. |

---

<a id="containspoint"></a>

### ContainsPoint

```java
bool ContainsPoint(Vector3 p)
```

True if the point is inside [Min,Max].

#### Returns
True if the point is inside the bounds; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `p` | `[Vector3](Vector3.md#vector3)` | The point to test. |

---

<a id="containspoint-1"></a>

### ContainsPoint

```java
bool ContainsPoint(Vector3 p, Vector3 tol)
```

Contains with per-axis tolerance.

#### Returns
True if the point is inside the bounds with tolerance; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `p` | `[Vector3](Vector3.md#vector3)` | The point to test. |
| `tol` | `[Vector3](Vector3.md#vector3)` | The tolerance per axis. |

---

<a id="containspoint-2"></a>

### ContainsPoint

```java
bool ContainsPoint(Vector3 p, float tol)
```

Contains with uniform tolerance.

#### Returns
True if the point is inside the bounds with tolerance; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `p` | `[Vector3](Vector3.md#vector3)` | The point to test. |
| `tol` | `float` | The uniform tolerance value. |

---

<a id="getdimensions"></a>

### GetDimensions

```java
Vector3 GetDimensions()
```

The size of the box = Max − Min.

#### Returns
The dimensions of the box.

---

<a id="getcenter"></a>

### GetCenter

```java
Vector3 GetCenter()
```

Center = (Min + Max) / 2.

#### Returns
The center point of the box.

---

<a id="getextents"></a>

### GetExtents

```java
Vector3 GetExtents()
```

Extents = half-dimensions.

#### Returns
The extents (half-dimensions) of the box.

---

<a id="inflate"></a>

### Inflate

`inline`

```java
inline void Inflate(float amount)
```

Grow/shrink all sides by a uniform amount.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `float` | The amount to inflate (positive) or deflate (negative) by. |

---

<a id="inflate-1"></a>

### Inflate

`inline`

```java
inline void Inflate(Vector3 amount)
```

Grow/shrink all sides by per-axis amounts.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `amount` | `[Vector3](Vector3.md#vector3)` | The amount to inflate (positive) or deflate (negative) per axis. |

---

<a id="getcorners"></a>

### GetCorners

```java
Vector3[] GetCorners()
```

The eight corners in the same order as your C++ code.

#### Returns
An array of the eight corner points.

---

<a id="intersect"></a>

### Intersect

```java
bool Intersect(Bounds b)
```

AABB-AABB overlap test.

#### Returns
True if the bounds overlap; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The other bounds to test against. |

---

<a id="intersect-1"></a>

### Intersect

`inline`

```java
inline bool Intersect(Bounds b, out bool contained)
```

Overlap + full containment test. Returns true if they overlap; sets contained=true if b is entirely inside this.

#### Returns
True if the bounds overlap; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The other bounds to test against. |
| `contained` | `out bool` | Set to true if b is entirely inside this bounds; otherwise, false. |

---

<a id="intersect-2"></a>

### Intersect

`inline`

```java
inline bool Intersect(Bounds b, out Bounds intersection)
```

Overlap + return the intersection box.

#### Returns
True if the bounds overlap; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The other bounds to test against. |
| `intersection` | `out [Bounds](#bounds)` | The intersection bounds if they overlap; otherwise, default bounds. |

---

<a id="intersect-3"></a>

### Intersect

`inline`

```java
inline bool Intersect(Bounds b, Vector3 tol)
```

Overlap with a tolerance vector.

#### Returns
True if the bounds overlap with tolerance; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The other bounds to test against. |
| `tol` | `[Vector3](Vector3.md#vector3)` | The tolerance vector. |

---

<a id="slab"></a>

### Slab

`inline`

```java
inline bool Slab(float o, float d, float mn, float mx, ref float t0, ref float t1)
```

---

<a id="intersect-4"></a>

### Intersect

`inline`

```java
inline bool Intersect(Vector3 origin, Vector3 dir, out float t, bool restrictRange = true)
```

Ray vs AABB (slab method). Returns true if hit; t = entry time in [0,1] if restrictRange.

#### Returns
True if the ray intersects the bounds; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `origin` | `[Vector3](Vector3.md#vector3)` | The origin of the ray. |
| `dir` | `[Vector3](Vector3.md#vector3)` | The direction of the ray. |
| `t` | `out float` | The intersection parameter along the ray. |
| `restrictRange` | `bool` | If true, restricts t to [0,1]. |

---

<a id="equals-4"></a>

### Equals

```java
override bool Equals(object obj)
```

Returns true if the given object is equal to this bounds.

#### Returns
True if the given object is equal to this bounds; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `object` | The object to compare with the current instance. |

---

<a id="gethashcode-3"></a>

### GetHashCode

```java
override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-5"></a>

### Equals

```java
bool Equals(Bounds other)
```

Returns true if the given bounds is equal to this bounds.

#### Returns
True if the given bounds is equal to this bounds; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Bounds](#bounds)` | The bounds to compare with the current instance. |

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Bounds` | [`Empty`](#empty) `static` | An empty bounds box. |

---

<a id="empty"></a>

### Empty

`static`

```java
readonly Bounds Empty = new ( * float.MaxValue,  * float.MinValue)
```

An empty bounds box.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Bounds` | [`operator+`](#operator-14) `static` | Translates the bounds by a vector. |
| `Bounds` | [`operator-`](#operator-15) `static` | Translates the bounds by the negative of a vector. |
| `Bounds` | [`operator*`](#operator-16) `static` | Scales the bounds by a scalar. |
| `bool` | [`operator==`](#operator-17) `static` | Determines whether two bounds are equal. |
| `bool` | [`operator!=`](#operator-18) `static` | Determines whether two bounds are not equal. |

---

<a id="operator-14"></a>

### operator+

`static`

```java
static Bounds operator+(Bounds b, Vector3 shift)
```

Translates the bounds by a vector.

#### Returns
The translated bounds.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The bounds. |
| `shift` | `[Vector3](Vector3.md#vector3)` | The translation vector. |

---

<a id="operator-15"></a>

### operator-

`static`

```java
static Bounds operator-(Bounds b, Vector3 shift)
```

Translates the bounds by the negative of a vector.

#### Returns
The translated bounds.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The bounds. |
| `shift` | `[Vector3](Vector3.md#vector3)` | The translation vector. |

---

<a id="operator-16"></a>

### operator*

`static`

```java
static Bounds operator*(Bounds b, float s)
```

Scales the bounds by a scalar.

#### Returns
The scaled bounds.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `[Bounds](#bounds)` | The bounds. |
| `s` | `float` | The scalar value. |

---

<a id="operator-17"></a>

### operator==

`static`

```java
static bool operator==(Bounds a, Bounds b)
```

Determines whether two bounds are equal.

#### Returns
True if the bounds are equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Bounds](#bounds)` | The first bounds. |
| `b` | `[Bounds](#bounds)` | The second bounds. |

---

<a id="operator-18"></a>

### operator!=

`static`

```java
static bool operator!=(Bounds a, Bounds b)
```

Determines whether two bounds are not equal.

#### Returns
True if the bounds are not equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Bounds](#bounds)` | The first bounds. |
| `b` | `[Bounds](#bounds)` | The second bounds. |

