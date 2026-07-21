<a id="quaternion"></a>

# Quaternion

> **Extends:** `IEquatable< Quaternion >`, `IFormattable`

Quaternions are used to represent rotations.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Quaternion` | [`identity`](#identity) `static` | The identity rotation (Read Only). |
| `Vector3` | [`eulerAngles`](#eulerangles)  | Returns or sets the euler angle representation of the rotation. |
| `Quaternion` | [`normalized`](#normalized-3)  | Returns this quaternion with a magnitude of 1 (Read Only). |

---

<a id="identity"></a>

### identity

`static`

```java
Quaternion identity
```

The identity rotation (Read Only).

---

<a id="eulerangles"></a>

### eulerAngles

```java
Vector3 eulerAngles
```

Returns or sets the euler angle representation of the rotation.

---

<a id="normalized-3"></a>

### normalized

```java
Quaternion normalized
```

Returns this quaternion with a magnitude of 1 (Read Only).

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`x`](#x-3)  | X component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out. |
| `float` | [`y`](#y-3)  | Y component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out. |
| `float` | [`z`](#z-2)  | Z component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out. |
| `float` | [`w`](#w-1)  | W component of the [Quaternion](#quaternion). Do not directly modify quaternions. |

---

<a id="x-3"></a>

### x

```java
float x
```

X component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out.

---

<a id="y-3"></a>

### y

```java
float y
```

Y component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out.

---

<a id="z-2"></a>

### z

```java
float z
```

Z component of the [Quaternion](#quaternion). Don't modify this directly unless you know quaternions inside out.

---

<a id="w-1"></a>

### w

```java
float w
```

W component of the [Quaternion](#quaternion). Do not directly modify quaternions.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Quaternion`](#quaternion-1) `inline` | Constructs new [Quaternion](#quaternion) with given x,y,z,w components. |
| `void` | [`Set`](#set-3) `inline` | Set x, y, z and w components of an existing [Quaternion](#quaternion). |
| `void` | [`SetLookRotation`](#setlookrotation) `inline` | Creates a rotation with the specified forward direction, using [Vector3.up](Vector3.md#up-1) as the up direction. |
| `void` | [`SetLookRotation`](#setlookrotation-1) `inline` | Creates a rotation with the specified forward and upwards directions. |
| `void` | [`SetFromToRotation`](#setfromtorotation) `inline` | Creates a rotation which rotates from fromDirection to toDirection. |
| `void` | [`Normalize`](#normalize-5) `inline` | Normalizes this quaternion to have a magnitude of 1. |
| `override int` | [`GetHashCode`](#gethashcode-7) `inline` | Returns the hash code for this instance. |
| `override bool` | [`Equals`](#equals-12) `inline` | Returns true if the given quaternion is exactly equal to this quaternion. |
| `bool` | [`Equals`](#equals-13) `inline` | Returns true if the given quaternion is exactly equal to this quaternion. |
| `override string` | [`ToString`](#tostring-12) `inline` | Returns a formatted string for this quaternion. |
| `string` | [`ToString`](#tostring-13) `inline` | Returns a formatted string for this quaternion. |
| `string` | [`ToString`](#tostring-14) `inline` | Returns a formatted string for this quaternion. |

---

<a id="quaternion-1"></a>

### Quaternion

`inline`

```java
inline Quaternion(float x, float y, float z, float w)
```

Constructs new [Quaternion](#quaternion) with given x,y,z,w components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | The x component. |
| `y` | `float` | The y component. |
| `z` | `float` | The z component. |
| `w` | `float` | The w component. |

---

<a id="set-3"></a>

### Set

`inline`

```java
inline void Set(float newX, float newY, float newZ, float newW)
```

Set x, y, z and w components of an existing [Quaternion](#quaternion).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `newX` | `float` | The new x component. |
| `newY` | `float` | The new y component. |
| `newZ` | `float` | The new z component. |
| `newW` | `float` | The new w component. |

---

<a id="setlookrotation"></a>

### SetLookRotation

`inline`

```java
inline void SetLookRotation(Vector3 view)
```

Creates a rotation with the specified forward direction, using [Vector3.up](Vector3.md#up-1) as the up direction.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `view` | `[Vector3](Vector3.md#vector3)` | The direction to look in. |

---

<a id="setlookrotation-1"></a>

### SetLookRotation

`inline`

```java
inline void SetLookRotation(Vector3 view, Vector3 up)
```

Creates a rotation with the specified forward and upwards directions.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `view` | `[Vector3](Vector3.md#vector3)` | The direction to look in. |
| `up` | `[Vector3](Vector3.md#vector3)` | The vector that defines in which direction up is. |

---

<a id="setfromtorotation"></a>

### SetFromToRotation

`inline`

```java
inline void SetFromToRotation(Vector3 fromDirection, Vector3 toDirection)
```

Creates a rotation which rotates from fromDirection to toDirection.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `fromDirection` | `[Vector3](Vector3.md#vector3)` | The starting direction. |
| `toDirection` | `[Vector3](Vector3.md#vector3)` | The target direction. |

---

<a id="normalize-5"></a>

### Normalize

`inline`

```java
inline void Normalize()
```

Normalizes this quaternion to have a magnitude of 1.

---

<a id="gethashcode-7"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-12"></a>

### Equals

`inline`

```java
inline override bool Equals(object other)
```

Returns true if the given quaternion is exactly equal to this quaternion.

#### Returns
True if the given quaternion is exactly equal to this quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `object` | The object to compare with the current instance. |

---

<a id="equals-13"></a>

### Equals

`inline`

```java
inline bool Equals(Quaternion other)
```

Returns true if the given quaternion is exactly equal to this quaternion.

#### Returns
True if the given quaternion is exactly equal to this quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Quaternion](#quaternion)` | The quaternion to compare with the current instance. |

---

<a id="tostring-12"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a formatted string for this quaternion.

#### Returns
A formatted string representation of the quaternion.

---

<a id="tostring-13"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Returns a formatted string for this quaternion.

#### Returns
A formatted string representation of the quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |

---

<a id="tostring-14"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Returns a formatted string for this quaternion.

#### Returns
A formatted string representation of the quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |
| `formatProvider` | `IFormatProvider` | An object that specifies culture-specific formatting. |

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Quaternion` | [`FromToRotation`](#fromtorotation) `static` `inline` | Creates a rotation which rotates from fromDirection to toDirection. |
| `Quaternion` | [`Conjugate`](#conjugate) `static` `inline` | Returns the conjugate of a quaternion. |
| `Quaternion` | [`Inverse`](#inverse) `static` `inline` | Returns the Inverse of rotation. |
| `Quaternion` | [`Slerp`](#slerp-1) `static` `inline` | Spherically interpolates between quaternions a and b by ratio t. The parameter t is clamped to the range [0, 1]. |
| `Quaternion` | [`SlerpUnclamped`](#slerpunclamped) `static` `inline` | Spherically interpolates between a and b by t. The parameter t is not clamped. |
| `Quaternion` | [`Lerp`](#lerp-5) `static` `inline` | Interpolates between a and b by t and normalizes the result afterwards. The parameter t is clamped to the range [0, 1]. |
| `Quaternion` | [`LerpUnclamped`](#lerpunclamped-5) `static` `inline` | Interpolates between a and b by t and normalizes the result afterwards. The parameter t is not clamped. |
| `Quaternion` | [`AngleAxis`](#angleaxis) `static` `inline` | Creates a rotation which rotates angle degrees around axis. |
| `Quaternion` | [`LookRotation`](#lookrotation) `static` `inline` | Creates a rotation with the specified forward and upwards directions. |
| `Quaternion` | [`LookRotation`](#lookrotation-1) `static` `inline` | Creates a rotation with the specified forward direction, using [Vector3.up](Vector3.md#up-1) as the up direction. |
| `Quaternion` | [`operator/`](#operator-45) `static` `inline` | Divides a quaternion by a scalar. |
| `Quaternion` | [`operator*`](#operator-46) `static` `inline` | Multiplies a quaternion by a scalar. |
| `Quaternion` | [`operator*`](#operator-47) `static` `inline` | Multiplies a scalar by a quaternion. |
| `Quaternion` | [`operator+`](#operator-48) `static` `inline` | Adds two quaternions component-wise. |
| `Quaternion` | [`operator*`](#operator-49) `static` `inline` | Multiplies two quaternions. |
| `Vector3` | [`operator*`](#operator-50) `static` `inline` | Rotates a point by a quaternion. |
| `Quaternion` | [`operator-`](#operator-51) `static` `inline` | Negates a quaternion. |
| `bool` | [`operator==`](#operator-52) `static` `inline` | Determines whether two quaternions are approximately equal. |
| `bool` | [`operator!=`](#operator-53) `static` `inline` | Determines whether two quaternions are not approximately equal. |
| `float` | [`Dot`](#dot-3) `static` `inline` | The dot product between two rotations. |
| `float` | [`Angle`](#angle-2) `static` `inline` | Returns the angle in degrees between two rotations a and b. |
| `Quaternion` | [`Euler`](#euler) `static` `inline` | Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis; applied in that order. |
| `Quaternion` | [`Euler`](#euler-1) `static` `inline` | Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis. |
| `Quaternion` | [`RotateTowards`](#rotatetowards) `static` `inline` | Rotates a rotation from towards to. |
| `Quaternion` | [`Normalize`](#normalize-6) `static` `inline` | Converts this quaternion to one with the same orientation but with a magnitude of 1. |

---

<a id="fromtorotation"></a>

### FromToRotation

`static` `inline`

```java
static inline Quaternion FromToRotation(Vector3 fromDirection, Vector3 toDirection)
```

Creates a rotation which rotates from fromDirection to toDirection.

#### Returns
A quaternion representing the rotation from fromDirection to toDirection.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `fromDirection` | `[Vector3](Vector3.md#vector3)` | The starting direction. |
| `toDirection` | `[Vector3](Vector3.md#vector3)` | The target direction. |

---

<a id="conjugate"></a>

### Conjugate

`static` `inline`

```java
static inline Quaternion Conjugate(Quaternion q)
```

Returns the conjugate of a quaternion.

#### Returns
The conjugate of the quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `q` | `[Quaternion](#quaternion)` | The quaternion. |

---

<a id="inverse"></a>

### Inverse

`static` `inline`

```java
static inline Quaternion Inverse(Quaternion q)
```

Returns the Inverse of rotation.

#### Returns
The inverse quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `q` | `[Quaternion](#quaternion)` | The quaternion. |

---

<a id="slerp-1"></a>

### Slerp

`static` `inline`

```java
static inline Quaternion Slerp(Quaternion a, Quaternion b, float t)
```

Spherically interpolates between quaternions a and b by ratio t. The parameter t is clamped to the range [0, 1].

#### Returns
A quaternion spherically interpolated between quaternions a and b.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | Start value, returned when t = 0. |
| `b` | `[Quaternion](#quaternion)` | End value, returned when t = 1. |
| `t` | `float` | Interpolation ratio. |

---

<a id="slerpunclamped"></a>

### SlerpUnclamped

`static` `inline`

```java
static inline Quaternion SlerpUnclamped(Quaternion x, Quaternion y, float a)
```

Spherically interpolates between a and b by t. The parameter t is not clamped.

#### Returns
A quaternion spherically interpolated between quaternions x and y.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `[Quaternion](#quaternion)` | Start value, returned when t = 0. |
| `y` | `[Quaternion](#quaternion)` | End value, returned when t = 1. |
| `a` | `float` | Interpolation ratio (not clamped). |

---

<a id="lerp-5"></a>

### Lerp

`static` `inline`

```java
static inline Quaternion Lerp(Quaternion a, Quaternion b, float t)
```

Interpolates between a and b by t and normalizes the result afterwards. The parameter t is clamped to the range [0, 1].

#### Returns
A quaternion interpolated between quaternions a and b.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | Start value, returned when t = 0. |
| `b` | `[Quaternion](#quaternion)` | End value, returned when t = 1. |
| `t` | `float` | Interpolation ratio. |

---

<a id="lerpunclamped-5"></a>

### LerpUnclamped

`static` `inline`

```java
static inline Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t)
```

Interpolates between a and b by t and normalizes the result afterwards. The parameter t is not clamped.

#### Returns
A quaternion interpolated between quaternions a and b.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | Start value, returned when t = 0. |
| `b` | `[Quaternion](#quaternion)` | End value, returned when t = 1. |
| `t` | `float` | Interpolation ratio (not clamped). |

---

<a id="angleaxis"></a>

### AngleAxis

`static` `inline`

```java
static inline Quaternion AngleAxis(float angle, Vector3 axis)
```

Creates a rotation which rotates angle degrees around axis.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `angle` | `float` | The angle in degrees. |
| `axis` | `[Vector3](Vector3.md#vector3)` | The axis of rotation. |

---

<a id="lookrotation"></a>

### LookRotation

`static` `inline`

```java
static inline Quaternion LookRotation(Vector3 forward, Vector3 upwards)
```

Creates a rotation with the specified forward and upwards directions.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `forward` | `[Vector3](Vector3.md#vector3)` | The direction to look in. |
| `upwards` | `[Vector3](Vector3.md#vector3)` | The vector that defines in which direction up is. |

---

<a id="lookrotation-1"></a>

### LookRotation

`static` `inline`

```java
static inline Quaternion LookRotation(Vector3 forward)
```

Creates a rotation with the specified forward direction, using [Vector3.up](Vector3.md#up-1) as the up direction.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `forward` | `[Vector3](Vector3.md#vector3)` | The direction to look in. |

---

<a id="operator-45"></a>

### operator/

`static` `inline`

```java
static inline Quaternion operator/(Quaternion lhs, float rhs)
```

Divides a quaternion by a scalar.

#### Returns
The divided quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The quaternion. |
| `rhs` | `float` | The scalar value. |

---

<a id="operator-46"></a>

### operator*

`static` `inline`

```java
static inline Quaternion operator*(Quaternion lhs, float rhs)
```

Multiplies a quaternion by a scalar.

#### Returns
The scaled quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The quaternion. |
| `rhs` | `float` | The scalar value. |

---

<a id="operator-47"></a>

### operator*

`static` `inline`

```java
static inline Quaternion operator*(float lhs, Quaternion rhs)
```

Multiplies a scalar by a quaternion.

#### Returns
The scaled quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `float` | The scalar value. |
| `rhs` | `[Quaternion](#quaternion)` | The quaternion. |

---

<a id="operator-48"></a>

### operator+

`static` `inline`

```java
static inline Quaternion operator+(Quaternion lhs, Quaternion rhs)
```

Adds two quaternions component-wise.

#### Returns
The sum of the two quaternions.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The left-hand side quaternion. |
| `rhs` | `[Quaternion](#quaternion)` | The right-hand side quaternion. |

---

<a id="operator-49"></a>

### operator*

`static` `inline`

```java
static inline Quaternion operator*(Quaternion lhs, Quaternion rhs)
```

Multiplies two quaternions.

#### Returns
The product of the two quaternions.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The left-hand side quaternion. |
| `rhs` | `[Quaternion](#quaternion)` | The right-hand side quaternion. |

---

<a id="operator-50"></a>

### operator*

`static` `inline`

```java
static inline Vector3 operator*(Quaternion rotation, Vector3 point)
```

Rotates a point by a quaternion.

#### Returns
The rotated point.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `rotation` | `[Quaternion](#quaternion)` | The rotation quaternion. |
| `point` | `[Vector3](Vector3.md#vector3)` | The point to rotate. |

---

<a id="operator-51"></a>

### operator-

`static` `inline`

```java
static inline Quaternion operator-(Quaternion a)
```

Negates a quaternion.

#### Returns
The negated quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | The quaternion to negate. |

---

<a id="operator-52"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Quaternion lhs, Quaternion rhs)
```

Determines whether two quaternions are approximately equal.

#### Returns
True if the quaternions are approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The left-hand side quaternion. |
| `rhs` | `[Quaternion](#quaternion)` | The right-hand side quaternion. |

---

<a id="operator-53"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(Quaternion lhs, Quaternion rhs)
```

Determines whether two quaternions are not approximately equal.

#### Returns
True if the quaternions are not approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Quaternion](#quaternion)` | The left-hand side quaternion. |
| `rhs` | `[Quaternion](#quaternion)` | The right-hand side quaternion. |

---

<a id="dot-3"></a>

### Dot

`static` `inline`

```java
static inline float Dot(Quaternion a, Quaternion b)
```

The dot product between two rotations.

#### Returns
The dot product of the two quaternions.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | The first quaternion. |
| `b` | `[Quaternion](#quaternion)` | The second quaternion. |

---

<a id="angle-2"></a>

### Angle

`static` `inline`

```java
static inline float Angle(Quaternion a, Quaternion b)
```

Returns the angle in degrees between two rotations a and b.

#### Returns
The angle in degrees between the two rotations.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Quaternion](#quaternion)` | The first quaternion. |
| `b` | `[Quaternion](#quaternion)` | The second quaternion. |

---

<a id="euler"></a>

### Euler

`static` `inline`

```java
static inline Quaternion Euler(float x, float y, float z)
```

Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis; applied in that order.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `x` | `float` | Rotation around the x axis in degrees. |
| `y` | `float` | Rotation around the y axis in degrees. |
| `z` | `float` | Rotation around the z axis in degrees. |

---

<a id="euler-1"></a>

### Euler

`static` `inline`

```java
static inline Quaternion Euler(Vector3 euler)
```

Returns a rotation that rotates z degrees around the z axis, x degrees around the x axis, and y degrees around the y axis.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `euler` | `[Vector3](Vector3.md#vector3)` | The Euler angles in degrees (x, y, z). |

---

<a id="rotatetowards"></a>

### RotateTowards

`static` `inline`

```java
static inline Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
```

Rotates a rotation from towards to.

#### Returns
The rotated quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `[Quaternion](#quaternion)` | The starting rotation. |
| `to` | `[Quaternion](#quaternion)` | The target rotation. |
| `maxDegreesDelta` | `float` | The maximum angle in degrees to rotate. |

---

<a id="normalize-6"></a>

### Normalize

`static` `inline`

```java
static inline Quaternion Normalize(Quaternion q)
```

Converts this quaternion to one with the same orientation but with a magnitude of 1.

#### Returns
The normalized quaternion.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `q` | `[Quaternion](#quaternion)` | The quaternion to normalize. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `Quaternion` | [`internal_m2n_from_to_rotation`](#internal_m2n_from_to_rotation)  |  |
| `Quaternion` | [`internal_m2n_from_euler_rad`](#internal_m2n_from_euler_rad)  |  |
| `Vector3` | [`internal_m2n_to_euler_rad`](#internal_m2n_to_euler_rad)  |  |
| `Quaternion` | [`internal_m2n_angle_axis`](#internal_m2n_angle_axis)  |  |
| `Quaternion` | [`internal_m2n_look_rotation`](#internal_m2n_look_rotation)  |  |

---

<a id="internal_m2n_from_to_rotation"></a>

### internal_m2n_from_to_rotation

```java
Quaternion internal_m2n_from_to_rotation(Vector3 fromDirection, Vector3 toDirection)
```

---

<a id="internal_m2n_from_euler_rad"></a>

### internal_m2n_from_euler_rad

```java
Quaternion internal_m2n_from_euler_rad(Vector3 euler)
```

---

<a id="internal_m2n_to_euler_rad"></a>

### internal_m2n_to_euler_rad

```java
Vector3 internal_m2n_to_euler_rad(Quaternion rotation)
```

---

<a id="internal_m2n_angle_axis"></a>

### internal_m2n_angle_axis

```java
Quaternion internal_m2n_angle_axis(float angle, Vector3 axis)
```

---

<a id="internal_m2n_look_rotation"></a>

### internal_m2n_look_rotation

```java
Quaternion internal_m2n_look_rotation(Vector3 forward, Vector3 upwards)
```

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Quaternion` | [`identityQuaternion`](#identityquaternion) `static` |  |

---

<a id="identityquaternion"></a>

### identityQuaternion

`static`

```java
readonly Quaternion identityQuaternion = new (0f, 0f, 0f, 1f)
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Quaternion` | [`Internal_FromEulerRad`](#internal_fromeulerrad) `static` `inline` | Creates a quaternion from Euler angles in radians. |
| `Vector3` | [`Internal_ToEulerRad`](#internal_toeulerrad) `static` `inline` | Converts a quaternion to Euler angles in radians. |
| `bool` | [`IsEqualUsingDot`](#isequalusingdot) `static` `inline` |  |
| `Vector3` | [`Internal_MakePositive`](#internal_makepositive) `static` `inline` |  |

---

<a id="internal_fromeulerrad"></a>

### Internal_FromEulerRad

`static` `inline`

```java
static inline Quaternion Internal_FromEulerRad(Vector3 euler)
```

Creates a quaternion from Euler angles in radians.

#### Returns
A quaternion representing the rotation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `euler` | `[Vector3](Vector3.md#vector3)` | The Euler angles in radians. |

---

<a id="internal_toeulerrad"></a>

### Internal_ToEulerRad

`static` `inline`

```java
static inline Vector3 Internal_ToEulerRad(Quaternion rotation)
```

Converts a quaternion to Euler angles in radians.

#### Returns
The Euler angles in radians.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `rotation` | `[Quaternion](#quaternion)` | The quaternion. |

---

<a id="isequalusingdot"></a>

### IsEqualUsingDot

`static` `inline`

```java
static inline bool IsEqualUsingDot(float dot)
```

---

<a id="internal_makepositive"></a>

### Internal_MakePositive

`static` `inline`

```java
static inline Vector3 Internal_MakePositive(Vector3 euler)
```

