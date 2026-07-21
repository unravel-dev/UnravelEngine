<a id="vector4"></a>

# Vector4

> **Extends:** `IEquatable< Vector4 >`, `IFormattable`

Representation of four-dimensional vectors.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`this[int index]`](#thisintindex-3)  | Gets or sets the component at the specified index. |
| `Vector4` | [`normalized`](#normalized-2)  | Returns this vector with a magnitude of 1 (Read Only). |
| `float` | [`magnitude`](#magnitude-3)  | Returns the length of this vector (Read Only). |
| `float` | [`sqrMagnitude`](#sqrmagnitude-5)  | Returns the squared length of this vector (Read Only). |
| `Vector4` | [`zero`](#zero-2) `static` | Shorthand for writing [Vector4(0,0,0,0)](#vector4). |
| `Vector4` | [`one`](#one-2) `static` | Shorthand for writing [Vector4(1,1,1,1)](#vector4). |
| `Vector4` | [`positiveInfinity`](#positiveinfinity-2) `static` | Shorthand for writing [Vector4](#vector4)(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity). |
| `Vector4` | [`negativeInfinity`](#negativeinfinity-3) `static` | Shorthand for writing [Vector4](#vector4)(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity). |

---

<a id="thisintindex-3"></a>

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
| `IndexOutOfRangeException` | Thrown when index is out of range [0, 3]. |

---

<a id="normalized-2"></a>

### normalized

```java
Vector4 normalized
```

Returns this vector with a magnitude of 1 (Read Only).

---

<a id="magnitude-3"></a>

### magnitude

```java
float magnitude
```

Returns the length of this vector (Read Only).

---

<a id="sqrmagnitude-5"></a>

### sqrMagnitude

```java
float sqrMagnitude
```

Returns the squared length of this vector (Read Only).

---

<a id="zero-2"></a>

### zero

`static`

```java
Vector4 zero
```

Shorthand for writing [Vector4(0,0,0,0)](#vector4).

---

<a id="one-2"></a>

### one

`static`

```java
Vector4 one
```

Shorthand for writing [Vector4(1,1,1,1)](#vector4).

---

<a id="positiveinfinity-2"></a>

### positiveInfinity

`static`

```java
Vector4 positiveInfinity
```

Shorthand for writing [Vector4](#vector4)(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity).

---

<a id="negativeinfinity-3"></a>

### negativeInfinity

`static`

```java
Vector4 negativeInfinity
```

Shorthand for writing [Vector4](#vector4)(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity).

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`x`](#x-2)  | X component of the vector. |
| `float` | [`y`](#y-2)  | Y component of the vector. |
| `float` | [`z`](#z-1)  | Z component of the vector. |
| `float` | [`w`](#w)  | W component of the vector. |

---

<a id="x-2"></a>

### x

```java
float x
```

X component of the vector.

---

<a id="y-2"></a>

### y

```java
float y
```

Y component of the vector.

---

<a id="z-1"></a>

### z

```java
float z
```

Z component of the vector.

---

<a id="w"></a>

### w

```java
float w
```

W component of the vector.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Vector4`](#vector4-1) `inline` | Creates a new vector with given x, y, z, w components. |
|  | [`Vector4`](#vector4-2) `inline` | Creates a new vector with given x, y, z components and sets w to zero. |
|  | [`Vector4`](#vector4-3) `inline` | Creates a new vector with given x, y components and sets z and w to zero. |
| `void` | [`Set`](#set-2) `inline` | Set x, y, z and w components of an existing [Vector4](#vector4). |
| `void` | [`Scale`](#scale-4) `inline` | Multiplies every component of this vector by the same component of scale. |
| `override int` | [`GetHashCode`](#gethashcode-6) `inline` | Returns the hash code for this instance. |
| `override bool` | [`Equals`](#equals-10) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `bool` | [`Equals`](#equals-11) `inline` | Returns true if the given vector is exactly equal to this vector. |
| `void` | [`Normalize`](#normalize-3) `inline` | Makes this vector have a magnitude of 1. |
| `override string` | [`ToString`](#tostring-9) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-10) `inline` | Returns a formatted string for this vector. |
| `string` | [`ToString`](#tostring-11) `inline` | Returns a formatted string for this vector. |
| `float` | [`SqrMagnitude`](#sqrmagnitude-6) `inline` | Returns the squared length of this vector. |

---

<a id="vector4-1"></a>

### Vector4

`inline`

```java
inline Vector4(float x, float y, float z, float w)
```

Creates a new vector with given x, y, z, w components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |
| `z` | `float` | The z component. |
| `w` | `float` | The w component. |

---

<a id="vector4-2"></a>

### Vector4

`inline`

```java
inline Vector4(float x, float y, float z)
```

Creates a new vector with given x, y, z components and sets w to zero.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |
| `z` | `float` | The z component. |

---

<a id="vector4-3"></a>

### Vector4

`inline`

```java
inline Vector4(float x, float y)
```

Creates a new vector with given x, y components and sets z and w to zero.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |

---

<a id="set-2"></a>

### Set

`inline`

```java
inline void Set(float newX, float newY, float newZ, float newW)
```

Set x, y, z and w components of an existing [Vector4](#vector4).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `newX` | `float` | The new x component. |
| `newY` | `float` | The new y component. |
| `newZ` | `float` | The new z component. |
| `newW` | `float` | The new w component. |

---

<a id="scale-4"></a>

### Scale

`inline`

```java
inline void Scale(Vector4 scale)
```

Multiplies every component of this vector by the same component of scale.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `scale` | `[Vector4](#vector4)` | The scale vector. |

---

<a id="gethashcode-6"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-10"></a>

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

<a id="equals-11"></a>

### Equals

`inline`

```java
inline bool Equals(Vector4 other)
```

Returns true if the given vector is exactly equal to this vector.

#### Returns
True if the given vector is exactly equal to this vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Vector4](#vector4)` | The vector to compare with the current instance. |

---

<a id="normalize-3"></a>

### Normalize

`inline`

```java
inline void Normalize()
```

Makes this vector have a magnitude of 1.

---

<a id="tostring-9"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a formatted string for this vector.

#### Returns
A formatted string representation of the vector.

---

<a id="tostring-10"></a>

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

<a id="tostring-11"></a>

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

<a id="sqrmagnitude-6"></a>

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
| `Vector4` | [`Lerp`](#lerp-4) `static` `inline` | Linearly interpolates between two vectors. |
| `Vector4` | [`LerpUnclamped`](#lerpunclamped-4) `static` `inline` | Linearly interpolates between two vectors without clamping the interpolation parameter. |
| `Vector4` | [`MoveTowards`](#movetowards-3) `static` `inline` | Moves a point current towards target. |
| `Vector4` | [`Scale`](#scale-5) `static` `inline` | Multiplies two vectors component-wise. |
| `Vector4` | [`Normalize`](#normalize-4) `static` `inline` | Makes this vector have a magnitude of 1. |
| `float` | [`Dot`](#dot-2) `static` `inline` | Dot Product of two vectors. |
| `Vector4` | [`Project`](#project-1) `static` `inline` | Projects a vector onto another vector. |
| `float` | [`Distance`](#distance-2) `static` `inline` | Returns the distance between a and b. |
| `float` | [`Magnitude`](#magnitude-4) `static` `inline` | Returns the length of the vector. |
| `Vector4` | [`Min`](#min-8) `static` `inline` | Returns a vector that is made from the smallest components of two vectors. |
| `Vector4` | [`Max`](#max-8) `static` `inline` | Returns a vector that is made from the largest components of two vectors. |
| `Vector4` | [`operator+`](#operator-37) `static` `inline` | Adds two vectors component-wise. |
| `Vector4` | [`operator-`](#operator-38) `static` `inline` | Subtracts two vectors component-wise. |
| `Vector4` | [`operator-`](#operator-39) `static` `inline` | Negates a vector. |
| `Vector4` | [`operator*`](#operator-40) `static` `inline` | Multiplies a vector by a scalar. |
| `Vector4` | [`operator*`](#operator-41) `static` `inline` | Multiplies a scalar by a vector. |
| `Vector4` | [`operator/`](#operator-42) `static` `inline` | Divides a vector by a scalar. |
| `bool` | [`operator==`](#operator-43) `static` `inline` | Determines whether two vectors are approximately equal. |
| `bool` | [`operator!=`](#operator-44) `static` `inline` | Determines whether two vectors are not approximately equal. |
| `implicit` | [`operator Vector4`](#operatorvector4-1) `static` `inline` | Implicitly converts a [Vector3](Vector3.md#vector3) to a [Vector4](#vector4) by adding a w component of 0. |
| `implicit` | [`operator Vector3`](#operatorvector3-1) `static` `inline` | Implicitly converts a [Vector4](#vector4) to a [Vector3](Vector3.md#vector3) by taking the x, y, z components. |
| `implicit` | [`operator Vector4`](#operatorvector4-2) `static` `inline` | Implicitly converts a [Vector2](Vector2.md#vector2) to a [Vector4](#vector4) by adding z and w components of 0. |
| `implicit` | [`operator Vector2`](#operatorvector2-1) `static` `inline` | Implicitly converts a [Vector4](#vector4) to a [Vector2](Vector2.md#vector2) by taking the x and y components. |
| `float` | [`SqrMagnitude`](#sqrmagnitude-7) `static` `inline` | Returns the squared length of the vector. |

---

<a id="lerp-4"></a>

### Lerp

`static` `inline`

```java
static inline Vector4 Lerp(Vector4 a, Vector4 b, float t)
```

Linearly interpolates between two vectors.

#### Returns
The interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |
| `t` | `float` | The interpolation parameter. |

---

<a id="lerpunclamped-4"></a>

### LerpUnclamped

`static` `inline`

```java
static inline Vector4 LerpUnclamped(Vector4 a, Vector4 b, float t)
```

Linearly interpolates between two vectors without clamping the interpolation parameter.

#### Returns
The interpolated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |
| `t` | `float` | The interpolation parameter (not clamped). |

---

<a id="movetowards-3"></a>

### MoveTowards

`static` `inline`

```java
static inline Vector4 MoveTowards(Vector4 current, Vector4 target, float maxDistanceDelta)
```

Moves a point current towards target.

#### Returns
The new position.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `[Vector4](#vector4)` | The current position. |
| `target` | `[Vector4](#vector4)` | The target position. |
| `maxDistanceDelta` | `float` | The maximum distance to move. |

---

<a id="scale-5"></a>

### Scale

`static` `inline`

```java
static inline Vector4 Scale(Vector4 a, Vector4 b)
```

Multiplies two vectors component-wise.

#### Returns
A new vector with components multiplied.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |

---

<a id="normalize-4"></a>

### Normalize

`static` `inline`

```java
static inline Vector4 Normalize(Vector4 a)
```

Makes this vector have a magnitude of 1.

#### Returns
The normalized vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector to normalize. |

---

<a id="dot-2"></a>

### Dot

`static` `inline`

```java
static inline float Dot(Vector4 a, Vector4 b)
```

Dot Product of two vectors.

#### Returns
The dot product of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |

---

<a id="project-1"></a>

### Project

`static` `inline`

```java
static inline Vector4 Project(Vector4 a, Vector4 b)
```

Projects a vector onto another vector.

#### Returns
The projected vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector to project. |
| `b` | `[Vector4](#vector4)` | The normal vector to project onto. |

---

<a id="distance-2"></a>

### Distance

`static` `inline`

```java
static inline float Distance(Vector4 a, Vector4 b)
```

Returns the distance between a and b.

#### Returns
The distance between the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |

---

<a id="magnitude-4"></a>

### Magnitude

`static` `inline`

```java
static inline float Magnitude(Vector4 a)
```

Returns the length of the vector.

#### Returns
The length of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector. |

---

<a id="min-8"></a>

### Min

`static` `inline`

```java
static inline Vector4 Min(Vector4 lhs, Vector4 rhs)
```

Returns a vector that is made from the smallest components of two vectors.

#### Returns
A vector with the minimum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector4](#vector4)` | The left-hand side vector. |
| `rhs` | `[Vector4](#vector4)` | The right-hand side vector. |

---

<a id="max-8"></a>

### Max

`static` `inline`

```java
static inline Vector4 Max(Vector4 lhs, Vector4 rhs)
```

Returns a vector that is made from the largest components of two vectors.

#### Returns
A vector with the maximum components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector4](#vector4)` | The left-hand side vector. |
| `rhs` | `[Vector4](#vector4)` | The right-hand side vector. |

---

<a id="operator-37"></a>

### operator+

`static` `inline`

```java
static inline Vector4 operator+(Vector4 a, Vector4 b)
```

Adds two vectors component-wise.

#### Returns
The sum of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |

---

<a id="operator-38"></a>

### operator-

`static` `inline`

```java
static inline Vector4 operator-(Vector4 a, Vector4 b)
```

Subtracts two vectors component-wise.

#### Returns
The difference of the two vectors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The first vector. |
| `b` | `[Vector4](#vector4)` | The second vector. |

---

<a id="operator-39"></a>

### operator-

`static` `inline`

```java
static inline Vector4 operator-(Vector4 a)
```

Negates a vector.

#### Returns
The negated vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector to negate. |

---

<a id="operator-40"></a>

### operator*

`static` `inline`

```java
static inline Vector4 operator*(Vector4 a, float d)
```

Multiplies a vector by a scalar.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-41"></a>

### operator*

`static` `inline`

```java
static inline Vector4 operator*(float d, Vector4 a)
```

Multiplies a scalar by a vector.

#### Returns
The scaled vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `d` | `float` | The scalar value. |
| `a` | `[Vector4](#vector4)` | The vector. |

---

<a id="operator-42"></a>

### operator/

`static` `inline`

```java
static inline Vector4 operator/(Vector4 a, float d)
```

Divides a vector by a scalar.

#### Returns
The divided vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector. |
| `d` | `float` | The scalar value. |

---

<a id="operator-43"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Vector4 lhs, Vector4 rhs)
```

Determines whether two vectors are approximately equal.

#### Returns
True if the vectors are approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector4](#vector4)` | The left-hand side vector. |
| `rhs` | `[Vector4](#vector4)` | The right-hand side vector. |

---

<a id="operator-44"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(Vector4 lhs, Vector4 rhs)
```

Determines whether two vectors are not approximately equal.

#### Returns
True if the vectors are not approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Vector4](#vector4)` | The left-hand side vector. |
| `rhs` | `[Vector4](#vector4)` | The right-hand side vector. |

---

<a id="operatorvector4-1"></a>

### operator Vector4

`static` `inline`

```java
static inline implicit operator Vector4(Vector3 v)
```

Implicitly converts a [Vector3](Vector3.md#vector3) to a [Vector4](#vector4) by adding a w component of 0.

#### Returns
A [Vector4](#vector4) with x, y, z from [Vector3](Vector3.md#vector3) and w = 0.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector3](Vector3.md#vector3)` | The [Vector3](Vector3.md#vector3) to convert. |

---

<a id="operatorvector3-1"></a>

### operator Vector3

`static` `inline`

```java
static inline implicit operator Vector3(Vector4 v)
```

Implicitly converts a [Vector4](#vector4) to a [Vector3](Vector3.md#vector3) by taking the x, y, z components.

#### Returns
A [Vector3](Vector3.md#vector3) with x, y, z components from the [Vector4](#vector4).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector4](#vector4)` | The [Vector4](#vector4) to convert. |

---

<a id="operatorvector4-2"></a>

### operator Vector4

`static` `inline`

```java
static inline implicit operator Vector4(Vector2 v)
```

Implicitly converts a [Vector2](Vector2.md#vector2) to a [Vector4](#vector4) by adding z and w components of 0.

#### Returns
A [Vector4](#vector4) with x, y from [Vector2](Vector2.md#vector2) and z = w = 0.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector2](Vector2.md#vector2)` | The [Vector2](Vector2.md#vector2) to convert. |

---

<a id="operatorvector2-1"></a>

### operator Vector2

`static` `inline`

```java
static inline implicit operator Vector2(Vector4 v)
```

Implicitly converts a [Vector4](#vector4) to a [Vector2](Vector2.md#vector2) by taking the x and y components.

#### Returns
A [Vector2](Vector2.md#vector2) with x and y components from the [Vector4](#vector4).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector4](#vector4)` | The [Vector4](#vector4) to convert. |

---

<a id="sqrmagnitude-7"></a>

### SqrMagnitude

`static` `inline`

```java
static inline float SqrMagnitude(Vector4 a)
```

Returns the squared length of the vector.

#### Returns
The squared length of the vector.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Vector4](#vector4)` | The vector. |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Vector4` | [`zeroVector`](#zerovector-2) `static` |  |
| `readonly Vector4` | [`oneVector`](#onevector-2) `static` |  |
| `readonly Vector4` | [`positiveInfinityVector`](#positiveinfinityvector-2) `static` |  |
| `readonly Vector4` | [`negativeInfinityVector`](#negativeinfinityvector-2) `static` |  |

---

<a id="zerovector-2"></a>

### zeroVector

`static`

```java
readonly Vector4 zeroVector = new (0f, 0f, 0f, 0f)
```

---

<a id="onevector-2"></a>

### oneVector

`static`

```java
readonly Vector4 oneVector = new (1f, 1f, 1f, 1f)
```

---

<a id="positiveinfinityvector-2"></a>

### positiveInfinityVector

`static`

```java
readonly Vector4 positiveInfinityVector = new (float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity)
```

---

<a id="negativeinfinityvector-2"></a>

### negativeInfinityVector

`static`

```java
readonly Vector4 negativeInfinityVector = new (float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity)
```

