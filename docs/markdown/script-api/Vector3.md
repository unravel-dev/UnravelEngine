<a id="vector3"></a>

# Vector3

> **Extends:** `IEquatable< Vector3 >`, `IFormattable`

Representation of 3D vectors and points.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`this[int index]`](#thisintindex-2)  | Gets or sets the component at the specified index. |
| `Vector3` | [`normalized`](#normalized-1)  | Returns this vector with a magnitude of 1 (Read Only). |
| `float` | [`magnitude`](#magnitude-1)  | Returns the length of this vector (Read Only). |
| `float` | [`sqrMagnitude`](#sqrmagnitude-3)  | Returns the squared length of this vector (Read Only). |
| `Vector3` | [`zero`](#zero-1) `static` | Shorthand for writing [Vector3(0, 0, 0)](#vector3). |
| `Vector3` | [`one`](#one-1) `static` | Shorthand for writing [Vector3(1, 1, 1)](#vector3). |
| `Vector3` | [`forward`](#forward) `static` | Shorthand for writing [Vector3(0, 0, 1)](#vector3). |
| `Vector3` | [`back`](#back) `static` | Shorthand for writing [Vector3](#vector3)(0, 0, -1). |
| `Vector3` | [`up`](#up-1) `static` | Shorthand for writing [Vector3(0, 1, 0)](#vector3). |
| `Vector3` | [`down`](#down-1) `static` | Shorthand for writing [Vector3](#vector3)(0, -1, 0). |
| `Vector3` | [`left`](#left-1) `static` | Shorthand for writing [Vector3](#vector3)(-1, 0, 0). |
| `Vector3` | [`right`](#right-1) `static` | Shorthand for writing [Vector3(1, 0, 0)](#vector3). |
| `Vector3` | [`positiveInfinity`](#positiveinfinity-1) `static` | Shorthand for writing [Vector3](#vector3)(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity). |
| `Vector3` | [`negativeInfinity`](#negativeinfinity-2) `static` | Shorthand for writing [Vector3](#vector3)(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity). |

---

<a id="thisintindex-2"></a>

### this[int index]

```java
float this[int index]
```

Gets or sets the component at the specified index.

#### Returns
The component value at the specified index.

#### Exceptions

| Exception | Description |
|-----------|-------------|
| `IndexOutOfRangeException` | Thrown when index is out of range [0, 2]. |

---

<a id="normalized-1"></a>

### normalized

```java
Vector3 normalized
```

Returns this vector with a magnitude of 1 (Read Only).

---

<a id="magnitude-1"></a>

### magnitude

```java
float magnitude
```

Returns the length of this vector (Read Only).

---

<a id="sqrmagnitude-3"></a>

### sqrMagnitude

```java
float sqrMagnitude
```

Returns the squared length of this vector (Read Only).

---

<a id="zero-1"></a>

### zero

`static`

```java
Vector3 zero
```

Shorthand for writing [Vector3(0, 0, 0)](#vector3).

---

<a id="one-1"></a>

### one

`static`

```java
Vector3 one
```

Shorthand for writing [Vector3(1, 1, 1)](#vector3).

---

<a id="forward"></a>

### forward

`static`

```java
Vector3 forward
```

Shorthand for writing [Vector3(0, 0, 1)](#vector3).

---

<a id="back"></a>

### back

`static`

```java
Vector3 back
```

Shorthand for writing [Vector3](#vector3)(0, 0, -1).

---

<a id="up-1"></a>

### up

`static`

```java
Vector3 up
```

Shorthand for writing [Vector3(0, 1, 0)](#vector3).

---

<a id="down-1"></a>

### down

`static`

```java
Vector3 down
```

Shorthand for writing [Vector3](#vector3)(0, -1, 0).

---

<a id="left-1"></a>

### left

`static`

```java
Vector3 left
```

Shorthand for writing [Vector3](#vector3)(-1, 0, 0).

---

<a id="right-1"></a>

### right

`static`

```java
Vector3 right
```

Shorthand for writing [Vector3(1, 0, 0)](#vector3).

---

<a id="positiveinfinity-1"></a>

### positiveInfinity

`static`

```java
Vector3 positiveInfinity
```

Shorthand for writing [Vector3](#vector3)(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity).

---

<a id="negativeinfinity-2"></a>

### negativeInfinity

`static`

```java
Vector3 negativeInfinity
```

Shorthand for writing [Vector3](#vector3)(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity).

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`x`](#x-1)  | X component of the vector. |
| `float` | [`y`](#y-1)  | Y component of the vector. |
| `float` | [`z`](#z)  | Z component of the vector. |

---

<a id="x-1"></a>

### x

```java
float x
```

X component of the vector.

---

<a id="y-1"></a>

### y

```java
float y
```

Y component of the vector.

---

<a id="z"></a>

### z

```java
float z
```

Z component of the vector.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Vector3`](#vector3-1) `inline` | Creates a new vector with given x, y, z components. |
|  | [`Vector3`](#vector3-2) `inline` | Creates a new vector with given x, y components and sets z to zero. |
| `void` | [`Set`](#set-1) `inline` | Set x, y and z components of an existing [Vector3](#vector3). |
| `void` | [`Scale`](#scale-2) `inline` | Multiplies every component of this vector by the same component of scale. |
| `override int` | [`GetHashCode`](#gethashcode-5) `inline` | Returns the hash code for this instance. |
| `override bool` | [`Equals`](#equals-8) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `bool` | [`Equals`](#equals-9) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `void` | [`Normalize`](#normalize-1) `inline` | Makes this vector have a magnitude of 1. |
| `override string` | [`ToString`](#tostring-6) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-7) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-8) `inline` | Returns a formatted string for this vector. |

---

<a id="vector3-1"></a>

### Vector3

`inline`

```java
inline Vector3(float x, float y, float z)
```

Creates a new vector with given x, y, z components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |
| `z` | `float` | The z component. |

---

<a id="vector3-2"></a>

### Vector3

`inline`

```java
inline Vector3(float x, float y)
```

Creates a new vector with given x, y components and sets z to zero.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |

---

<a id="set-1"></a>

### Set

`inline`

```java
inline void Set(float newX, float newY, float newZ)
```

Set x, y and z components of an existing [Vector3](#vector3).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `newX` | `float` | The new x component. |
| `newY` | `float` | The new y component. |
| `newZ` | `float` | The new z component. |

---

<a id="scale-2"></a>

### Scale

`inline`

```java
inline void Scale(Vector3 scale)
```

Multiplies every component of this vector by the same component of scale.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `scale` | `[Vector3](#vector3)` | The scale vector. |

---

<a id="gethashcode-5"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-8"></a>

### Equals

`inline`

```java
inline override bool Equals(object other)
```

Returns true if the given vector is exactly equal to this vector.

#### Returns
True if the given vector is exactly equal to this vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `object` | The object to compare with the current instance. |

---

<a id="equals-9"></a>

### Equals

`inline`

```java
inline bool Equals(Vector3 other)
```

Returns true if the given vector is exactly equal to this vector.

#### Returns
True if the given vector is exactly equal to this vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Vector3](#vector3)` | The vector to compare with the current instance. |

---

<a id="normalize-1"></a>

### Normalize

`inline`

```java
inline void Normalize()
```

Makes this vector have a magnitude of 1.

---

<a id="tostring-6"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a formatted string for this vector.

#### Returns
A formatted string representation of the vector.

---

<a id="tostring-7"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Returns a formatted string for this vector.

#### Returns
A formatted string representation of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |

---

<a id="tostring-8"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Returns a formatted string for this vector.

#### Returns
A formatted string representation of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |
| `formatProvider` | `IFormatProvider` | An object that specifies culture-specific formatting. |

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`Slerp`](#slerp) `static` `inline` | Spherically interpolates between two vectors. |
| `Vector3` | [`SlerpClamped`](#slerpclamped) `static` `inline` | Spherically interpolates between two vectors, clamping the interpolation parameter to [0, 1]. |
| `Vector3` | [`Lerp`](#lerp-3) `static` `inline` | Linearly interpolates between two points. |
| `Vector3` | [`LerpUnclamped`](#lerpunclamped-3) `static` `inline` | Linearly interpolates between two vectors without clamping the interpolation parameter. |
| `Vector3` | [`MoveTowards`](#movetowards-2) `static` `inline` | Calculate a position between the points specified by current and target, moving no farther than the distance specified by maxDistanceDelta. |
| `Vector3` | [`SmoothDamp`](#smoothdamp-2) `static` `inline` | Gradually changes a vector towards a desired goal over time. |
| `Vector3` | [`Scale`](#scale-3) `static` `inline` | Multiplies two vectors component-wise. |
| `Vector3` | [`Cross`](#cross) `static` `inline` | Cross Product of two vectors. |
| `Vector3` | [`Reflect`](#reflect-1) `static` `inline` | Reflects a vector off the plane defined by a normal. |
| `Vector3` | [`Normalize`](#normalize-2) `static` `inline` | Makes this vector have a magnitude of 1. |
| `float` | [`Dot`](#dot-1) `static` `inline` | Dot Product of two vectors. |
| `Vector3` | [`Project`](#project) `static` `inline` | Projects a vector onto another vector. |
| `Vector3` | [`ProjectOnPlane`](#projectonplane) `static` `inline` | Projects a vector onto a plane defined by a normal orthogonal to the plane. |
| `float` | [`Angle`](#angle-1) `static` `inline` | Calculates the angle between vectors from and to. |
| `float` | [`SignedAngle`](#signedangle-1) `static` `inline` | Calculates the signed angle between vectors from and to in relation to axis. |
| `float` | [`Distance`](#distance-1) `static` `inline` | Returns the distance between a and b. |
| `Vector3` | [`ClampMagnitude`](#clampmagnitude-1) `static` `inline` | Returns a copy of vector with its magnitude clamped to maxLength. |
| `float` | [`Magnitude`](#magnitude-2) `static` `inline` | Returns the length of the vector. |
| `float` | [`SqrMagnitude`](#sqrmagnitude-4) `static` `inline` | Returns the squared length of the vector. |
| `Vector3` | [`Min`](#min-7) `static` `inline` | Returns a vector that is made from the smallest components of two vectors. |
| `Vector3` | [`Max`](#max-7) `static` `inline` | Returns a vector that is made from the largest components of two vectors. |
| `Vector3` | [`operator+`](#operator-29) `static` `inline` | Adds two vectors component-wise. |
| `Vector3` | [`operator-`](#operator-30) `static` `inline` | Subtracts two vectors component-wise. |
| `Vector3` | [`operator-`](#operator-31) `static` `inline` | Negates a vector. |
| `Vector3` | [`operator*`](#operator-32) `static` `inline` | Multiplies a vector by a scalar. |
| `Vector3` | [`operator*`](#operator-33) `static` `inline` | Multiplies a scalar by a vector. |
| `Vector3` | [`operator/`](#operator-34) `static` `inline` | Divides a vector by a scalar. |
| `bool` | [`operator==`](#operator-35) `static` `inline` | Determines whether two vectors are approximately equal. |
| `bool` | [`operator!=`](#operator-36) `static` `inline` | Determines whether two vectors are not approximately equal. |

---

<a id="slerp"></a>

### Slerp

`static` `inline`

```java
static inline Vector3 Slerp(Vector3 a, Vector3 b, float t)
```

Spherically interpolates between two vectors.

#### Returns
The spherically interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |
| `t` | `float` | The interpolation parameter. |

---

<a id="slerpclamped"></a>

### SlerpClamped

`static` `inline`

```java
static inline Vector3 SlerpClamped(Vector3 a, Vector3 b, float t)
```

Spherically interpolates between two vectors, clamping the interpolation parameter to [0, 1].

#### Returns
The spherically interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |
| `t` | `float` | The interpolation parameter, clamped to [0, 1]. |

---

<a id="lerp-3"></a>

### Lerp

`static` `inline`

```java
static inline Vector3 Lerp(Vector3 a, Vector3 b, float t)
```

Linearly interpolates between two points.

#### Returns
Interpolated value, equals to a + (b - a) * t.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | Start value, returned when t = 0. |
| `b` | `[Vector3](#vector3)` | End value, returned when t = 1. |
| `t` | `float` | Value used to interpolate between a and b. |

---

<a id="lerpunclamped-3"></a>

### LerpUnclamped

`static` `inline`

```java
static inline Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t)
```

Linearly interpolates between two vectors without clamping the interpolation parameter.

#### Returns
The linearly interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |
| `t` | `float` | The interpolation parameter (not clamped). |

---

<a id="movetowards-2"></a>

### MoveTowards

`static` `inline`

```java
static inline Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
```

Calculate a position between the points specified by current and target, moving no farther than the distance specified by maxDistanceDelta.

#### Returns
The new position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `[Vector3](#vector3)` | The position to move from. |
| `target` | `[Vector3](#vector3)` | The position to move towards. |
| `maxDistanceDelta` | `float` | Distance to move current per call. |

---

<a id="smoothdamp-2"></a>

### SmoothDamp

`static` `inline`

```java
static inline Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
```

Gradually changes a vector towards a desired goal over time.

#### Returns
The smoothed vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `[Vector3](#vector3)` | The current position. |
| `target` | `[Vector3](#vector3)` | The position we are trying to reach. |
| `currentVelocity` | `ref [Vector3](#vector3)` | The current velocity, this value is modified by the function every time you call it. |
| `smoothTime` | `float` | Approximately the time it will take to reach the target. A smaller value will reach the target faster. |
| `maxSpeed` | `float` | Optionally allows you to clamp the maximum speed. |
| `deltaTime` | `float` | The time since the last call to this function. |

---

<a id="scale-3"></a>

### Scale

`static` `inline`

```java
static inline Vector3 Scale(Vector3 a, Vector3 b)
```

Multiplies two vectors component-wise.

#### Returns
A new vector with components multiplied.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |

---

<a id="cross"></a>

### Cross

`static` `inline`

```java
static inline Vector3 Cross(Vector3 lhs, Vector3 rhs)
```

Cross Product of two vectors.

#### Returns
The cross product of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

---

<a id="reflect-1"></a>

### Reflect

`static` `inline`

```java
static inline Vector3 Reflect(Vector3 inDirection, Vector3 inNormal)
```

Reflects a vector off the plane defined by a normal.

#### Returns
The reflected vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `inDirection` | `[Vector3](#vector3)` | The direction vector to reflect. |
| `inNormal` | `[Vector3](#vector3)` | The normal of the plane. |

---

<a id="normalize-2"></a>

### Normalize

`static` `inline`

```java
static inline Vector3 Normalize(Vector3 value)
```

Makes this vector have a magnitude of 1.

#### Returns
The normalized vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `[Vector3](#vector3)` | The vector to normalize. |

---

<a id="dot-1"></a>

### Dot

`static` `inline`

```java
static inline float Dot(Vector3 lhs, Vector3 rhs)
```

Dot Product of two vectors.

#### Returns
The dot product of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

---

<a id="project"></a>

### Project

`static` `inline`

```java
static inline Vector3 Project(Vector3 vector, Vector3 onNormal)
```

Projects a vector onto another vector.

#### Returns
The projected vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](#vector3)` | The vector to project. |
| `onNormal` | `[Vector3](#vector3)` | The normal vector to project onto. |

---

<a id="projectonplane"></a>

### ProjectOnPlane

`static` `inline`

```java
static inline Vector3 ProjectOnPlane(Vector3 vector, Vector3 planeNormal)
```

Projects a vector onto a plane defined by a normal orthogonal to the plane.

#### Returns
The location of the vector on the plane.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](#vector3)` | The location of the vector above the plane. |
| `planeNormal` | `[Vector3](#vector3)` | The direction from the vector towards the plane. |

---

<a id="angle-1"></a>

### Angle

`static` `inline`

```java
static inline float Angle(Vector3 from, Vector3 to)
```

Calculates the angle between vectors from and to.

#### Returns
The angle in degrees between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `[Vector3](#vector3)` | The vector from which the angular difference is measured. |
| `to` | `[Vector3](#vector3)` | The vector to which the angular difference is measured. |

---

<a id="signedangle-1"></a>

### SignedAngle

`static` `inline`

```java
static inline float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
```

Calculates the signed angle between vectors from and to in relation to axis.

#### Returns
Returns the signed angle between from and to in degrees.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `[Vector3](#vector3)` | The vector from which the angular difference is measured. |
| `to` | `[Vector3](#vector3)` | The vector to which the angular difference is measured. |
| `axis` | `[Vector3](#vector3)` | A vector around which the other vectors are rotated. |

---

<a id="distance-1"></a>

### Distance

`static` `inline`

```java
static inline float Distance(Vector3 a, Vector3 b)
```

Returns the distance between a and b.

#### Returns
The distance between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |

---

<a id="clampmagnitude-1"></a>

### ClampMagnitude

`static` `inline`

```java
static inline Vector3 ClampMagnitude(Vector3 vector, float maxLength)
```

Returns a copy of vector with its magnitude clamped to maxLength.

#### Returns
A copy of the vector with magnitude clamped to maxLength.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](#vector3)` | The vector to clamp. |
| `maxLength` | `float` | The maximum length. |

---

<a id="magnitude-2"></a>

### Magnitude

`static` `inline`

```java
static inline float Magnitude(Vector3 vector)
```

Returns the length of the vector.

#### Returns
The length of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](#vector3)` | The vector. |

---

<a id="sqrmagnitude-4"></a>

### SqrMagnitude

`static` `inline`

```java
static inline float SqrMagnitude(Vector3 vector)
```

Returns the squared length of the vector.

#### Returns
The squared length of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector3](#vector3)` | The vector. |

---

<a id="min-7"></a>

### Min

`static` `inline`

```java
static inline Vector3 Min(Vector3 lhs, Vector3 rhs)
```

Returns a vector that is made from the smallest components of two vectors.

#### Returns
A vector with the minimum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

---

<a id="max-7"></a>

### Max

`static` `inline`

```java
static inline Vector3 Max(Vector3 lhs, Vector3 rhs)
```

Returns a vector that is made from the largest components of two vectors.

#### Returns
A vector with the maximum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

---

<a id="operator-29"></a>

### operator+

`static` `inline`

```java
static inline Vector3 operator+(Vector3 a, Vector3 b)
```

Adds two vectors component-wise.

#### Returns
The sum of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |

---

<a id="operator-30"></a>

### operator-

`static` `inline`

```java
static inline Vector3 operator-(Vector3 a, Vector3 b)
```

Subtracts two vectors component-wise.

#### Returns
The difference of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The first vector. |
| `b` | `[Vector3](#vector3)` | The second vector. |

---

<a id="operator-31"></a>

### operator-

`static` `inline`

```java
static inline Vector3 operator-(Vector3 a)
```

Negates a vector.

#### Returns
The negated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The vector to negate. |

---

<a id="operator-32"></a>

### operator*

`static` `inline`

```java
static inline Vector3 operator*(Vector3 a, float d)
```

Multiplies a vector by a scalar.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-33"></a>

### operator*

`static` `inline`

```java
static inline Vector3 operator*(float d, Vector3 a)
```

Multiplies a scalar by a vector.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `d` | `float` | The scalar value. |
| `a` | `[Vector3](#vector3)` | The vector. |

---

<a id="operator-34"></a>

### operator/

`static` `inline`

```java
static inline Vector3 operator/(Vector3 a, float d)
```

Divides a vector by a scalar.

#### Returns
The divided vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector3](#vector3)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-35"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Vector3 lhs, Vector3 rhs)
```

Determines whether two vectors are approximately equal.

#### Returns
True if the vectors are approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

---

<a id="operator-36"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(Vector3 lhs, Vector3 rhs)
```

Determines whether two vectors are not approximately equal.

#### Returns
True if the vectors are not approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector3](#vector3)` | The left-hand side vector. |
| `rhs` | `[Vector3](#vector3)` | The right-hand side vector. |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Vector3` | [`zeroVector`](#zerovector-1) `static` |  |
| `readonly Vector3` | [`oneVector`](#onevector-1) `static` |  |
| `readonly Vector3` | [`upVector`](#upvector-1) `static` |  |
| `readonly Vector3` | [`downVector`](#downvector-1) `static` |  |
| `readonly Vector3` | [`leftVector`](#leftvector-1) `static` |  |
| `readonly Vector3` | [`rightVector`](#rightvector-1) `static` |  |
| `readonly Vector3` | [`forwardVector`](#forwardvector) `static` |  |
| `readonly Vector3` | [`backVector`](#backvector) `static` |  |
| `readonly Vector3` | [`positiveInfinityVector`](#positiveinfinityvector-1) `static` |  |
| `readonly Vector3` | [`negativeInfinityVector`](#negativeinfinityvector-1) `static` |  |

---

<a id="zerovector-1"></a>

### zeroVector

`static`

```java
readonly Vector3 zeroVector = new (0f, 0f, 0f)
```

---

<a id="onevector-1"></a>

### oneVector

`static`

```java
readonly Vector3 oneVector = new (1f, 1f, 1f)
```

---

<a id="upvector-1"></a>

### upVector

`static`

```java
readonly Vector3 upVector = new (0f, 1f, 0f)
```

---

<a id="downvector-1"></a>

### downVector

`static`

```java
readonly Vector3 downVector = new (0f, -1f, 0f)
```

---

<a id="leftvector-1"></a>

### leftVector

`static`

```java
readonly Vector3 leftVector = new (-1f, 0f, 0f)
```

---

<a id="rightvector-1"></a>

### rightVector

`static`

```java
readonly Vector3 rightVector = new (1f, 0f, 0f)
```

---

<a id="forwardvector"></a>

### forwardVector

`static`

```java
readonly Vector3 forwardVector = new (0f, 0f, 1f)
```

---

<a id="backvector"></a>

### backVector

`static`

```java
readonly Vector3 backVector = new (0f, 0f, -1f)
```

---

<a id="positiveinfinityvector-1"></a>

### positiveInfinityVector

`static`

```java
readonly Vector3 positiveInfinityVector = new (float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity)
```

---

<a id="negativeinfinityvector-1"></a>

### negativeInfinityVector

`static`

```java
readonly Vector3 negativeInfinityVector = new (float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity)
```

