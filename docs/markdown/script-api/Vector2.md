<a id="vector2"></a>

# Vector2

> **Extends:** `IEquatable< Vector2 >`, `IFormattable`

Representation of 2D vectors and points.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`this[int index]`](#thisintindex-1)  | Gets or sets the component at the specified index. |
| `Vector2` | [`normalized`](#normalized)  | Returns this vector with a magnitude of 1 (Read Only). |
| `float` | [`magnitude`](#magnitude)  | Returns the length of this vector (Read Only). |
| `float` | [`sqrMagnitude`](#sqrmagnitude)  | Returns the squared length of this vector (Read Only). |
| `Vector2` | [`zero`](#zero) `static` | Shorthand for writing [Vector2(0, 0)](#vector2). |
| `Vector2` | [`one`](#one) `static` | Shorthand for writing [Vector2(1, 1)](#vector2). |
| `Vector2` | [`up`](#up) `static` | Shorthand for writing [Vector2(0, 1)](#vector2). |
| `Vector2` | [`down`](#down) `static` | Shorthand for writing [Vector2](#vector2)(0, -1). |
| `Vector2` | [`left`](#left) `static` | Shorthand for writing [Vector2](#vector2)(-1, 0). |
| `Vector2` | [`right`](#right) `static` | Shorthand for writing [Vector2(1, 0)](#vector2). |
| `Vector2` | [`positiveInfinity`](#positiveinfinity) `static` | Shorthand for writing [Vector2](#vector2)(float.PositiveInfinity, float.PositiveInfinity). |
| `Vector2` | [`negativeInfinity`](#negativeinfinity-1) `static` | Shorthand for writing [Vector2](#vector2)(float.NegativeInfinity, float.NegativeInfinity). |

---

<a id="thisintindex-1"></a>

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
| `IndexOutOfRangeException` | Thrown when index is out of range [0, 1]. |

---

<a id="normalized"></a>

### normalized

```java
Vector2 normalized
```

Returns this vector with a magnitude of 1 (Read Only).

---

<a id="magnitude"></a>

### magnitude

```java
float magnitude
```

Returns the length of this vector (Read Only).

---

<a id="sqrmagnitude"></a>

### sqrMagnitude

```java
float sqrMagnitude
```

Returns the squared length of this vector (Read Only).

---

<a id="zero"></a>

### zero

`static`

```java
Vector2 zero
```

Shorthand for writing [Vector2(0, 0)](#vector2).

---

<a id="one"></a>

### one

`static`

```java
Vector2 one
```

Shorthand for writing [Vector2(1, 1)](#vector2).

---

<a id="up"></a>

### up

`static`

```java
Vector2 up
```

Shorthand for writing [Vector2(0, 1)](#vector2).

---

<a id="down"></a>

### down

`static`

```java
Vector2 down
```

Shorthand for writing [Vector2](#vector2)(0, -1).

---

<a id="left"></a>

### left

`static`

```java
Vector2 left
```

Shorthand for writing [Vector2](#vector2)(-1, 0).

---

<a id="right"></a>

### right

`static`

```java
Vector2 right
```

Shorthand for writing [Vector2(1, 0)](#vector2).

---

<a id="positiveinfinity"></a>

### positiveInfinity

`static`

```java
Vector2 positiveInfinity
```

Shorthand for writing [Vector2](#vector2)(float.PositiveInfinity, float.PositiveInfinity).

---

<a id="negativeinfinity-1"></a>

### negativeInfinity

`static`

```java
Vector2 negativeInfinity
```

Shorthand for writing [Vector2](#vector2)(float.NegativeInfinity, float.NegativeInfinity).

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`x`](#x)  | X component of the vector. |
| `float` | [`y`](#y)  | Y component of the vector. |

---

<a id="x"></a>

### x

```java
float x
```

X component of the vector.

---

<a id="y"></a>

### y

```java
float y
```

Y component of the vector.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Vector2`](#vector2-1) `inline` | Constructs a new vector with given x, y components. |
| `void` | [`Set`](#set) `inline` | Set x and y components of an existing [Vector2](#vector2). |
| `void` | [`Scale`](#scale) `inline` | Multiplies every component of this vector by the same component of scale. |
| `void` | [`Normalize`](#normalize) `inline` | Makes this vector have a magnitude of 1. |
| `override string` | [`ToString`](#tostring-3) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-4) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-5) `inline` | Returns a formatted string for this vector. |
| `override int` | [`GetHashCode`](#gethashcode-4) `inline` | Returns the hash code for this instance. |
| `override bool` | [`Equals`](#equals-6) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `bool` | [`Equals`](#equals-7) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `float` | [`SqrMagnitude`](#sqrmagnitude-1) `inline` | Returns the squared length of this vector. |

---

<a id="vector2-1"></a>

### Vector2

`inline`

```java
inline Vector2(float x, float y)
```

Constructs a new vector with given x, y components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |

---

<a id="set"></a>

### Set

`inline`

```java
inline void Set(float newX, float newY)
```

Set x and y components of an existing [Vector2](#vector2).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `newX` | `float` | The new x component. |
| `newY` | `float` | The new y component. |

---

<a id="scale"></a>

### Scale

`inline`

```java
inline void Scale(Vector2 scale)
```

Multiplies every component of this vector by the same component of scale.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `scale` | `[Vector2](#vector2)` | The scale vector. |

---

<a id="normalize"></a>

### Normalize

`inline`

```java
inline void Normalize()
```

Makes this vector have a magnitude of 1.

---

<a id="tostring-3"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a formatted string for this vector.

#### Returns
A formatted string representation of the vector.

---

<a id="tostring-4"></a>

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

<a id="tostring-5"></a>

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

---

<a id="gethashcode-4"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-6"></a>

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

<a id="equals-7"></a>

### Equals

`inline`

```java
inline bool Equals(Vector2 other)
```

Returns true if the given vector is exactly equal to this vector.

#### Returns
True if the given vector is exactly equal to this vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Vector2](#vector2)` | The vector to compare with the current instance. |

---

<a id="sqrmagnitude-1"></a>

### SqrMagnitude

`inline`

```java
inline float SqrMagnitude()
```

Returns the squared length of this vector.

#### Returns
The squared length of this vector.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Vector2` | [`Lerp`](#lerp-2) `static` `inline` | Linearly interpolates between vectors a and b by t. |
| `Vector2` | [`LerpUnclamped`](#lerpunclamped-2) `static` `inline` | Linearly interpolates between vectors a and b by t without clamping the interpolation parameter. |
| `Vector2` | [`MoveTowards`](#movetowards-1) `static` `inline` | Moves a point current towards target. |
| `Vector2` | [`Scale`](#scale-1) `static` `inline` | Multiplies two vectors component-wise. |
| `Vector2` | [`Reflect`](#reflect) `static` `inline` | Reflects a vector off the vector defined by a normal. |
| `Vector2` | [`Perpendicular`](#perpendicular) `static` `inline` | Returns the 2D vector perpendicular to this 2D vector. The result is always rotated 90-degrees in a counter-clockwise direction for a 2D coordinate system where the positive Y axis goes up. |
| `float` | [`Dot`](#dot) `static` `inline` | Dot Product of two vectors. |
| `float` | [`Angle`](#angle) `static` `inline` | Gets the unsigned angle in degrees between from and to. |
| `float` | [`SignedAngle`](#signedangle) `static` `inline` | Gets the signed angle in degrees between from and to. |
| `float` | [`Distance`](#distance) `static` `inline` | Returns the distance between a and b. |
| `Vector2` | [`ClampMagnitude`](#clampmagnitude) `static` `inline` | Returns a copy of vector with its magnitude clamped to maxLength. |
| `float` | [`SqrMagnitude`](#sqrmagnitude-2) `static` `inline` | Returns the squared length of the vector. |
| `Vector2` | [`Min`](#min-6) `static` `inline` | Returns a vector that is made from the smallest components of two vectors. |
| `Vector2` | [`Max`](#max-6) `static` `inline` | Returns a vector that is made from the largest components of two vectors. |
| `Vector2` | [`SmoothDamp`](#smoothdamp-1) `static` `inline` | Gradually changes a vector towards a desired goal over time. |
| `Vector2` | [`operator+`](#operator-19) `static` `inline` | Adds two vectors component-wise. |
| `Vector2` | [`operator-`](#operator-20) `static` `inline` | Subtracts two vectors component-wise. |
| `Vector2` | [`operator*`](#operator-21) `static` `inline` | Multiplies two vectors component-wise. |
| `Vector2` | [`operator/`](#operator-22) `static` `inline` | Divides two vectors component-wise. |
| `Vector2` | [`operator-`](#operator-23) `static` `inline` | Negates a vector. |
| `Vector2` | [`operator*`](#operator-24) `static` `inline` | Multiplies a vector by a scalar. |
| `Vector2` | [`operator*`](#operator-25) `static` `inline` | Multiplies a scalar by a vector. |
| `Vector2` | [`operator/`](#operator-26) `static` `inline` | Divides a vector by a scalar. |
| `bool` | [`operator==`](#operator-27) `static` `inline` | Determines whether two vectors are approximately equal. |
| `bool` | [`operator!=`](#operator-28) `static` `inline` | Determines whether two vectors are not approximately equal. |
| `implicit` | [`operator Vector2`](#operatorvector2) `static` `inline` | Implicitly converts a [Vector3](Vector3.md#vector3) to a [Vector2](#vector2) by taking the x and y components. |
| `implicit` | [`operator Vector3`](#operatorvector3) `static` `inline` | Implicitly converts a [Vector2](#vector2) to a [Vector3](Vector3.md#vector3) by adding a z component of 0. |

---

<a id="lerp-2"></a>

### Lerp

`static` `inline`

```java
static inline Vector2 Lerp(Vector2 a, Vector2 b, float t)
```

Linearly interpolates between vectors a and b by t.

#### Returns
The interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |
| `t` | `float` | The interpolation parameter. |

---

<a id="lerpunclamped-2"></a>

### LerpUnclamped

`static` `inline`

```java
static inline Vector2 LerpUnclamped(Vector2 a, Vector2 b, float t)
```

Linearly interpolates between vectors a and b by t without clamping the interpolation parameter.

#### Returns
The interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |
| `t` | `float` | The interpolation parameter (not clamped). |

---

<a id="movetowards-1"></a>

### MoveTowards

`static` `inline`

```java
static inline Vector2 MoveTowards(Vector2 current, Vector2 target, float maxDistanceDelta)
```

Moves a point current towards target.

#### Returns
The new position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `[Vector2](#vector2)` | The current position. |
| `target` | `[Vector2](#vector2)` | The target position. |
| `maxDistanceDelta` | `float` | The maximum distance to move. |

---

<a id="scale-1"></a>

### Scale

`static` `inline`

```java
static inline Vector2 Scale(Vector2 a, Vector2 b)
```

Multiplies two vectors component-wise.

#### Returns
A new vector with components multiplied.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="reflect"></a>

### Reflect

`static` `inline`

```java
static inline Vector2 Reflect(Vector2 inDirection, Vector2 inNormal)
```

Reflects a vector off the vector defined by a normal.

#### Returns
The reflected vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `inDirection` | `[Vector2](#vector2)` | The direction vector to reflect. |
| `inNormal` | `[Vector2](#vector2)` | The normal of the plane. |

---

<a id="perpendicular"></a>

### Perpendicular

`static` `inline`

```java
static inline Vector2 Perpendicular(Vector2 inDirection)
```

Returns the 2D vector perpendicular to this 2D vector. The result is always rotated 90-degrees in a counter-clockwise direction for a 2D coordinate system where the positive Y axis goes up.

#### Returns
The perpendicular direction.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `inDirection` | `[Vector2](#vector2)` | The input direction. |

---

<a id="dot"></a>

### Dot

`static` `inline`

```java
static inline float Dot(Vector2 lhs, Vector2 rhs)
```

Dot Product of two vectors.

#### Returns
The dot product of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector2](#vector2)` | The left-hand side vector. |
| `rhs` | `[Vector2](#vector2)` | The right-hand side vector. |

---

<a id="angle"></a>

### Angle

`static` `inline`

```java
static inline float Angle(Vector2 from, Vector2 to)
```

Gets the unsigned angle in degrees between from and to.

#### Returns
The unsigned angle in degrees between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `[Vector2](#vector2)` | The vector from which the angular difference is measured. |
| `to` | `[Vector2](#vector2)` | The vector to which the angular difference is measured. |

---

<a id="signedangle"></a>

### SignedAngle

`static` `inline`

```java
static inline float SignedAngle(Vector2 from, Vector2 to)
```

Gets the signed angle in degrees between from and to.

#### Returns
The signed angle in degrees between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `[Vector2](#vector2)` | The vector from which the angular difference is measured. |
| `to` | `[Vector2](#vector2)` | The vector to which the angular difference is measured. |

---

<a id="distance"></a>

### Distance

`static` `inline`

```java
static inline float Distance(Vector2 a, Vector2 b)
```

Returns the distance between a and b.

#### Returns
The distance between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="clampmagnitude"></a>

### ClampMagnitude

`static` `inline`

```java
static inline Vector2 ClampMagnitude(Vector2 vector, float maxLength)
```

Returns a copy of vector with its magnitude clamped to maxLength.

#### Returns
A copy of the vector with magnitude clamped to maxLength.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `vector` | `[Vector2](#vector2)` | The vector to clamp. |
| `maxLength` | `float` | The maximum length. |

---

<a id="sqrmagnitude-2"></a>

### SqrMagnitude

`static` `inline`

```java
static inline float SqrMagnitude(Vector2 a)
```

Returns the squared length of the vector.

#### Returns
The squared length of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The vector. |

---

<a id="min-6"></a>

### Min

`static` `inline`

```java
static inline Vector2 Min(Vector2 lhs, Vector2 rhs)
```

Returns a vector that is made from the smallest components of two vectors.

#### Returns
A vector with the minimum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector2](#vector2)` | The left-hand side vector. |
| `rhs` | `[Vector2](#vector2)` | The right-hand side vector. |

---

<a id="max-6"></a>

### Max

`static` `inline`

```java
static inline Vector2 Max(Vector2 lhs, Vector2 rhs)
```

Returns a vector that is made from the largest components of two vectors.

#### Returns
A vector with the maximum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector2](#vector2)` | The left-hand side vector. |
| `rhs` | `[Vector2](#vector2)` | The right-hand side vector. |

---

<a id="smoothdamp-1"></a>

### SmoothDamp

`static` `inline`

```java
static inline Vector2 SmoothDamp(Vector2 current, Vector2 target, ref Vector2 currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
```

Gradually changes a vector towards a desired goal over time.

#### Returns
The smoothed vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `[Vector2](#vector2)` | The current position. |
| `target` | `[Vector2](#vector2)` | The position we are trying to reach. |
| `currentVelocity` | `ref [Vector2](#vector2)` | The current velocity, this value is modified by the function every time you call it. |
| `smoothTime` | `float` | Approximately the time it will take to reach the target. A smaller value will reach the target faster. |
| `maxSpeed` | `float` | Optionally allows you to clamp the maximum speed. |
| `deltaTime` | `float` | The time since the last call to this function. |

---

<a id="operator-19"></a>

### operator+

`static` `inline`

```java
static inline Vector2 operator+(Vector2 a, Vector2 b)
```

Adds two vectors component-wise.

#### Returns
The sum of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="operator-20"></a>

### operator-

`static` `inline`

```java
static inline Vector2 operator-(Vector2 a, Vector2 b)
```

Subtracts two vectors component-wise.

#### Returns
The difference of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="operator-21"></a>

### operator*

`static` `inline`

```java
static inline Vector2 operator*(Vector2 a, Vector2 b)
```

Multiplies two vectors component-wise.

#### Returns
The component-wise product of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="operator-22"></a>

### operator/

`static` `inline`

```java
static inline Vector2 operator/(Vector2 a, Vector2 b)
```

Divides two vectors component-wise.

#### Returns
The component-wise quotient of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The first vector. |
| `b` | `[Vector2](#vector2)` | The second vector. |

---

<a id="operator-23"></a>

### operator-

`static` `inline`

```java
static inline Vector2 operator-(Vector2 a)
```

Negates a vector.

#### Returns
The negated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The vector to negate. |

---

<a id="operator-24"></a>

### operator*

`static` `inline`

```java
static inline Vector2 operator*(Vector2 a, float d)
```

Multiplies a vector by a scalar.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-25"></a>

### operator*

`static` `inline`

```java
static inline Vector2 operator*(float d, Vector2 a)
```

Multiplies a scalar by a vector.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `d` | `float` | The scalar value. |
| `a` | `[Vector2](#vector2)` | The vector. |

---

<a id="operator-26"></a>

### operator/

`static` `inline`

```java
static inline Vector2 operator/(Vector2 a, float d)
```

Divides a vector by a scalar.

#### Returns
The divided vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector2](#vector2)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-27"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Vector2 lhs, Vector2 rhs)
```

Determines whether two vectors are approximately equal.

#### Returns
True if the vectors are approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector2](#vector2)` | The left-hand side vector. |
| `rhs` | `[Vector2](#vector2)` | The right-hand side vector. |

---

<a id="operator-28"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(Vector2 lhs, Vector2 rhs)
```

Determines whether two vectors are not approximately equal.

#### Returns
True if the vectors are not approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector2](#vector2)` | The left-hand side vector. |
| `rhs` | `[Vector2](#vector2)` | The right-hand side vector. |

---

<a id="operatorvector2"></a>

### operator Vector2

`static` `inline`

```java
static inline implicit operator Vector2(Vector3 v)
```

Implicitly converts a [Vector3](Vector3.md#vector3) to a [Vector2](#vector2) by taking the x and y components.

#### Returns
A [Vector2](#vector2) with x and y components from the [Vector3](Vector3.md#vector3).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector3](Vector3.md#vector3)` | The [Vector3](Vector3.md#vector3) to convert. |

---

<a id="operatorvector3"></a>

### operator Vector3

`static` `inline`

```java
static inline implicit operator Vector3(Vector2 v)
```

Implicitly converts a [Vector2](#vector2) to a [Vector3](Vector3.md#vector3) by adding a z component of 0.

#### Returns
A [Vector3](Vector3.md#vector3) with x and y from [Vector2](#vector2) and z = 0.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector2](#vector2)` | The [Vector2](#vector2) to convert. |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Vector2` | [`zeroVector`](#zerovector) `static` |  |
| `readonly Vector2` | [`oneVector`](#onevector) `static` |  |
| `readonly Vector2` | [`upVector`](#upvector) `static` |  |
| `readonly Vector2` | [`downVector`](#downvector) `static` |  |
| `readonly Vector2` | [`leftVector`](#leftvector) `static` |  |
| `readonly Vector2` | [`rightVector`](#rightvector) `static` |  |
| `readonly Vector2` | [`positiveInfinityVector`](#positiveinfinityvector) `static` |  |
| `readonly Vector2` | [`negativeInfinityVector`](#negativeinfinityvector) `static` |  |

---

<a id="zerovector"></a>

### zeroVector

`static`

```java
readonly Vector2 zeroVector = new (0f, 0f)
```

---

<a id="onevector"></a>

### oneVector

`static`

```java
readonly Vector2 oneVector = new (1f, 1f)
```

---

<a id="upvector"></a>

### upVector

`static`

```java
readonly Vector2 upVector = new (0f, 1f)
```

---

<a id="downvector"></a>

### downVector

`static`

```java
readonly Vector2 downVector = new (0f, -1f)
```

---

<a id="leftvector"></a>

### leftVector

`static`

```java
readonly Vector2 leftVector = new (-1f, 0f)
```

---

<a id="rightvector"></a>

### rightVector

`static`

```java
readonly Vector2 rightVector = new (1f, 0f)
```

---

<a id="positiveinfinityvector"></a>

### positiveInfinityVector

`static`

```java
readonly Vector2 positiveInfinityVector = new (float.PositiveInfinity, float.PositiveInfinity)
```

---

<a id="negativeinfinityvector"></a>

### negativeInfinityVector

`static`

```java
readonly Vector2 negativeInfinityVector = new (float.NegativeInfinity, float.NegativeInfinity)
```

