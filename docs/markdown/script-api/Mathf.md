<a id="mathf"></a>

# Mathf

A collection of common math functions.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `int` | [`ClosestPowerOfTwo`](#closestpoweroftwo)  | Returns the closest power of two value. |
| `bool` | [`IsPowerOfTwo`](#ispoweroftwo)  | Returns true if the value is power of two. |
| `int` | [`NextPowerOfTwo`](#nextpoweroftwo)  | Returns the next power of two that is equal to, or greater than, the argument. |

---

<a id="closestpoweroftwo"></a>

### ClosestPowerOfTwo

```java
int ClosestPowerOfTwo(int value)
```

Returns the closest power of two value.

#### Returns
The closest power of two.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `int` | The input value. |

---

<a id="ispoweroftwo"></a>

### IsPowerOfTwo

```java
bool IsPowerOfTwo(int value)
```

Returns true if the value is power of two.

#### Returns
True if the value is a power of two; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `int` | The input value. |

---

<a id="nextpoweroftwo"></a>

### NextPowerOfTwo

```java
int NextPowerOfTwo(int value)
```

Returns the next power of two that is equal to, or greater than, the argument.

#### Returns
The next power of two.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `int` | The input value. |

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `const float` | [`PI`](#pi) `static` | The well-known 3.14159265358979... value (Read Only). |
| `const float` | [`Infinity`](#infinity) `static` | A representation of positive infinity (Read Only). |
| `const float` | [`NegativeInfinity`](#negativeinfinity) `static` | A representation of negative infinity (Read Only). |
| `const float` | [`Deg2Rad`](#deg2rad) `static` | Degrees-to-radians conversion constant (Read Only). |
| `const float` | [`Rad2Deg`](#rad2deg) `static` | Radians-to-degrees conversion constant (Read Only). |
| `const float` | [`kEpsilon`](#kepsilon) `static` | A small epsilon value used for floating point comparisons. |
| `const float` | [`kEpsilonNormalSqrt`](#kepsilonnormalsqrt) `static` | A small epsilon value used for normal vector comparisons. |
| `readonly float` | [`Epsilon`](#epsilon) `static` | A tiny floating point value (Read Only). |

---

<a id="pi"></a>

### PI

`static`

```java
const float PI = MathF.PI
```

The well-known 3.14159265358979... value (Read Only).

---

<a id="infinity"></a>

### Infinity

`static`

```java
const float Infinity = float.PositiveInfinity
```

A representation of positive infinity (Read Only).

---

<a id="negativeinfinity"></a>

### NegativeInfinity

`static`

```java
const float NegativeInfinity = float.NegativeInfinity
```

A representation of negative infinity (Read Only).

---

<a id="deg2rad"></a>

### Deg2Rad

`static`

```java
const float Deg2Rad = MathF.PI / 180f
```

Degrees-to-radians conversion constant (Read Only).

---

<a id="rad2deg"></a>

### Rad2Deg

`static`

```java
const float Rad2Deg = 57.29578f
```

Radians-to-degrees conversion constant (Read Only).

---

<a id="kepsilon"></a>

### kEpsilon

`static`

```java
const float kEpsilon = 1E-05f
```

A small epsilon value used for floating point comparisons.

---

<a id="kepsilonnormalsqrt"></a>

### kEpsilonNormalSqrt

`static`

```java
const float kEpsilonNormalSqrt = 1E-15f
```

A small epsilon value used for normal vector comparisons.

---

<a id="epsilon"></a>

### Epsilon

`static`

```java
readonly float Epsilon = (MathfInternal.IsFlushToZeroEnabled ? MathfInternal.FloatMinNormal : MathfInternal.FloatMinDenormal)
```

A tiny floating point value (Read Only).

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`Sin`](#sin) `static` `inline` | Returns the sine of angle f. |
| `float` | [`Cos`](#cos) `static` `inline` | Returns the cosine of angle f. |
| `float` | [`Tan`](#tan) `static` `inline` | Returns the tangent of angle f in radians. |
| `float` | [`Asin`](#asin) `static` `inline` | Returns the arc-sine of f - the angle in radians whose sine is f. |
| `float` | [`Acos`](#acos) `static` `inline` | Returns the arc-cosine of f - the angle in radians whose cosine is f. |
| `float` | [`Atan`](#atan) `static` `inline` | Returns the arc-tangent of f - the angle in radians whose tangent is f. |
| `float` | [`Atan2`](#atan2) `static` `inline` | Returns the angle in radians whose Tan is y/x. |
| `float` | [`Sqrt`](#sqrt) `static` `inline` | Returns square root of f. |
| `float` | [`Abs`](#abs) `static` `inline` | Returns the absolute value of f. |
| `int` | [`Abs`](#abs-1) `static` `inline` | Returns the absolute value of value. |
| `float` | [`Min`](#min) `static` `inline` | Returns the smallest of two values. |
| `float` | [`Min`](#min-1) `static` `inline` | Returns the smallest of two or more values. |
| `int` | [`Min`](#min-2) `static` `inline` | Returns the smallest of two values. |
| `int` | [`Min`](#min-3) `static` `inline` | Returns the smallest of two or more values. |
| `float` | [`Max`](#max) `static` `inline` | Returns largest of two values. |
| `float` | [`Max`](#max-1) `static` `inline` | Returns largest of two or more values. |
| `int` | [`Max`](#max-2) `static` `inline` | Returns the largest of two values. |
| `int` | [`Max`](#max-3) `static` `inline` | Returns the largest of two or more values. |
| `float` | [`Pow`](#pow) `static` `inline` | Returns f raised to power p. |
| `float` | [`Exp`](#exp) `static` `inline` | Returns e raised to the specified power. |
| `float` | [`Log`](#log) `static` `inline` | Returns the logarithm of a specified number in a specified base. |
| `float` | [`Log`](#log-1) `static` `inline` | Returns the natural (base e) logarithm of a specified number. |
| `float` | [`Log10`](#log10) `static` `inline` | Returns the base 10 logarithm of a specified number. |
| `float` | [`Ceil`](#ceil) `static` `inline` | Returns the smallest integer greater to or equal to f. |
| `float` | [`Floor`](#floor) `static` `inline` | Returns the largest integer smaller than or equal to f. |
| `float` | [`Round`](#round) `static` `inline` | Returns f rounded to the nearest integer. |
| `int` | [`CeilToInt`](#ceiltoint) `static` `inline` | Returns the smallest integer greater to or equal to f. |
| `int` | [`FloorToInt`](#floortoint) `static` `inline` | Returns the largest integer smaller to or equal to f. |
| `int` | [`RoundToInt`](#roundtoint) `static` `inline` | Returns f rounded to the nearest integer. |
| `float` | [`Sign`](#sign) `static` `inline` | Returns the sign of f. |
| `float` | [`Clamp`](#clamp) `static` `inline` | Clamps the given value between the given minimum float and maximum float values. Returns the given value if it is within the minimum and maximum range. |
| `int` | [`Clamp`](#clamp-1) `static` `inline` | Clamps the given value between a range defined by the given minimum integer and maximum integer values. Returns the given value if it is within min and max. |
| `float` | [`Clamp01`](#clamp01) `static` `inline` | Clamps value between 0 and 1 and returns value. |
| `float` | [`Lerp`](#lerp-1) `static` `inline` | Linearly interpolates between a and b by t. |
| `float` | [`LerpUnclamped`](#lerpunclamped-1) `static` `inline` | Linearly interpolates between a and b by t with no limit to t. |
| `float` | [`LerpAngle`](#lerpangle) `static` `inline` | Same as Lerp but makes sure the values interpolate correctly when they wrap around 360 degrees. |
| `float` | [`MoveTowards`](#movetowards) `static` `inline` | Moves a value current towards target. |
| `float` | [`MoveTowardsAngle`](#movetowardsangle) `static` `inline` | Same as MoveTowards but makes sure the values interpolate correctly when they wrap around 360 degrees. |
| `float` | [`SmoothStep`](#smoothstep) `static` `inline` | Interpolates between min and max with smoothing at the limits. |
| `bool` | [`Approximately`](#approximately) `static` `inline` | Compares two floating point values and returns true if they are similar. |
| `float` | [`SmoothDamp`](#smoothdamp) `static` `inline` | Gradually changes a value towards a desired goal over time. |
| `float` | [`SmoothDampAngle`](#smoothdampangle) `static` `inline` | Gradually changes an angle towards a desired goal over time. |
| `float` | [`Repeat`](#repeat) `static` `inline` | Loops the value t, so that it is never larger than length and never smaller than 0. |
| `float` | [`PingPong`](#pingpong) `static` `inline` | PingPong returns a value that will increment and decrement between the value 0 and length. |
| `float` | [`InverseLerp`](#inverselerp) `static` `inline` | Determines where a value lies between two points. |
| `float` | [`DeltaAngle`](#deltaangle) `static` `inline` | Calculates the shortest difference between two given angles given in degrees. |

---

<a id="sin"></a>

### Sin

`static` `inline`

```java
static inline float Sin(float f)
```

Returns the sine of angle f.

#### Returns
The return value between -1 and +1.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input angle, in radians. |

---

<a id="cos"></a>

### Cos

`static` `inline`

```java
static inline float Cos(float f)
```

Returns the cosine of angle f.

#### Returns
The return value between -1 and 1.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input angle, in radians. |

---

<a id="tan"></a>

### Tan

`static` `inline`

```java
static inline float Tan(float f)
```

Returns the tangent of angle f in radians.

#### Returns
The tangent of the angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input angle, in radians. |

---

<a id="asin"></a>

### Asin

`static` `inline`

```java
static inline float Asin(float f)
```

Returns the arc-sine of f - the angle in radians whose sine is f.

#### Returns
The angle in radians.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value, must be in the range [-1, 1]. |

---

<a id="acos"></a>

### Acos

`static` `inline`

```java
static inline float Acos(float f)
```

Returns the arc-cosine of f - the angle in radians whose cosine is f.

#### Returns
The angle in radians.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value, must be in the range [-1, 1]. |

---

<a id="atan"></a>

### Atan

`static` `inline`

```java
static inline float Atan(float f)
```

Returns the arc-tangent of f - the angle in radians whose tangent is f.

#### Returns
The angle in radians.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="atan2"></a>

### Atan2

`static` `inline`

```java
static inline float Atan2(float y, float x)
```

Returns the angle in radians whose Tan is y/x.

#### Returns
The angle in radians.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `y` | `float` | The y coordinate. |
| `x` | `float` | The x coordinate. |

---

<a id="sqrt"></a>

### Sqrt

`static` `inline`

```java
static inline float Sqrt(float f)
```

Returns square root of f.

#### Returns
The square root of f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="abs"></a>

### Abs

`static` `inline`

```java
static inline float Abs(float f)
```

Returns the absolute value of f.

#### Returns
The absolute value of f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="abs-1"></a>

### Abs

`static` `inline`

```java
static inline int Abs(int value)
```

Returns the absolute value of value.

#### Returns
The absolute value of value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `int` | The input value. |

---

<a id="min"></a>

### Min

`static` `inline`

```java
static inline float Min(float a, float b)
```

Returns the smallest of two values.

#### Returns
The smallest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The first value. |
| `b` | `float` | The second value. |

---

<a id="min-1"></a>

### Min

`static` `inline`

```java
static inline float Min(params float[] values)
```

Returns the smallest of two or more values.

#### Returns
The smallest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `values` | `params float[]` | The array of values. |

---

<a id="min-2"></a>

### Min

`static` `inline`

```java
static inline int Min(int a, int b)
```

Returns the smallest of two values.

#### Returns
The smallest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `int` | The first value. |
| `b` | `int` | The second value. |

---

<a id="min-3"></a>

### Min

`static` `inline`

```java
static inline int Min(params int[] values)
```

Returns the smallest of two or more values.

#### Returns
The smallest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `values` | `params int[]` | The array of values. |

---

<a id="max"></a>

### Max

`static` `inline`

```java
static inline float Max(float a, float b)
```

Returns largest of two values.

#### Returns
The largest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The first value. |
| `b` | `float` | The second value. |

---

<a id="max-1"></a>

### Max

`static` `inline`

```java
static inline float Max(params float[] values)
```

Returns largest of two or more values.

#### Returns
The largest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `values` | `params float[]` | The array of values. |

---

<a id="max-2"></a>

### Max

`static` `inline`

```java
static inline int Max(int a, int b)
```

Returns the largest of two values.

#### Returns
The largest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `int` | The first value. |
| `b` | `int` | The second value. |

---

<a id="max-3"></a>

### Max

`static` `inline`

```java
static inline int Max(params int[] values)
```

Returns the largest of two or more values.

#### Returns
The largest value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `values` | `params int[]` | The array of values. |

---

<a id="pow"></a>

### Pow

`static` `inline`

```java
static inline float Pow(float f, float p)
```

Returns f raised to power p.

#### Returns
f raised to power p.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The base value. |
| `p` | `float` | The power. |

---

<a id="exp"></a>

### Exp

`static` `inline`

```java
static inline float Exp(float power)
```

Returns e raised to the specified power.

#### Returns
e raised to the specified power.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `power` | `float` | The power. |

---

<a id="log"></a>

### Log

`static` `inline`

```java
static inline float Log(float f, float p)
```

Returns the logarithm of a specified number in a specified base.

#### Returns
The logarithm of f in base p.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The number. |
| `p` | `float` | The base. |

---

<a id="log-1"></a>

### Log

`static` `inline`

```java
static inline float Log(float f)
```

Returns the natural (base e) logarithm of a specified number.

#### Returns
The natural logarithm of f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The number. |

---

<a id="log10"></a>

### Log10

`static` `inline`

```java
static inline float Log10(float f)
```

Returns the base 10 logarithm of a specified number.

#### Returns
The base 10 logarithm of f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The number. |

---

<a id="ceil"></a>

### Ceil

`static` `inline`

```java
static inline float Ceil(float f)
```

Returns the smallest integer greater to or equal to f.

#### Returns
The smallest integer greater to or equal to f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="floor"></a>

### Floor

`static` `inline`

```java
static inline float Floor(float f)
```

Returns the largest integer smaller than or equal to f.

#### Returns
The largest integer smaller than or equal to f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="round"></a>

### Round

`static` `inline`

```java
static inline float Round(float f)
```

Returns f rounded to the nearest integer.

#### Returns
f rounded to the nearest integer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="ceiltoint"></a>

### CeilToInt

`static` `inline`

```java
static inline int CeilToInt(float f)
```

Returns the smallest integer greater to or equal to f.

#### Returns
The smallest integer greater to or equal to f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="floortoint"></a>

### FloorToInt

`static` `inline`

```java
static inline int FloorToInt(float f)
```

Returns the largest integer smaller to or equal to f.

#### Returns
The largest integer smaller to or equal to f.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="roundtoint"></a>

### RoundToInt

`static` `inline`

```java
static inline int RoundToInt(float f)
```

Returns f rounded to the nearest integer.

#### Returns
f rounded to the nearest integer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="sign"></a>

### Sign

`static` `inline`

```java
static inline float Sign(float f)
```

Returns the sign of f.

#### Returns
1 if f is positive, -1 if f is negative, 0 if f is zero.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `f` | `float` | The input value. |

---

<a id="clamp"></a>

### Clamp

`static` `inline`

```java
static inline float Clamp(float value, float min, float max)
```

Clamps the given value between the given minimum float and maximum float values. Returns the given value if it is within the minimum and maximum range.

#### Returns
The float result between the minimum and maximum values.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `float` | The floating point value to restrict inside the range defined by the minimum and maximum values. |
| `min` | `float` | The minimum floating point value to compare against. |
| `max` | `float` | The maximum floating point value to compare against. |

---

<a id="clamp-1"></a>

### Clamp

`static` `inline`

```java
static inline int Clamp(int value, int min, int max)
```

Clamps the given value between a range defined by the given minimum integer and maximum integer values. Returns the given value if it is within min and max.

#### Returns
The int result between min and max values.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `int` | The integer point value to restrict inside the min-to-max range. |
| `min` | `int` | The minimum integer point value to compare against. |
| `max` | `int` | The maximum integer point value to compare against. |

---

<a id="clamp01"></a>

### Clamp01

`static` `inline`

```java
static inline float Clamp01(float value)
```

Clamps value between 0 and 1 and returns value.

#### Returns
The clamped value between 0 and 1.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `float` | The input value. |

---

<a id="lerp-1"></a>

### Lerp

`static` `inline`

```java
static inline float Lerp(float a, float b, float t)
```

Linearly interpolates between a and b by t.

#### Returns
The interpolated float result between the two float values.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The start value. |
| `b` | `float` | The end value. |
| `t` | `float` | The interpolation value between the two floats. |

---

<a id="lerpunclamped-1"></a>

### LerpUnclamped

`static` `inline`

```java
static inline float LerpUnclamped(float a, float b, float t)
```

Linearly interpolates between a and b by t with no limit to t.

#### Returns
The float value as a result from the linear interpolation.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The start value. |
| `b` | `float` | The end value. |
| `t` | `float` | The interpolation between the two floats. |

---

<a id="lerpangle"></a>

### LerpAngle

`static` `inline`

```java
static inline float LerpAngle(float a, float b, float t)
```

Same as Lerp but makes sure the values interpolate correctly when they wrap around 360 degrees.

#### Returns
Returns the interpolated float result between angle a and angle b, based on the interpolation value t.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The start angle. A float expressed in degrees. |
| `b` | `float` | The end angle. A float expressed in degrees. |
| `t` | `float` | The interpolation value between the start and end angles. This value is clamped to the range [0, 1]. |

---

<a id="movetowards"></a>

### MoveTowards

`static` `inline`

```java
static inline float MoveTowards(float current, float target, float maxDelta)
```

Moves a value current towards target.

#### Returns
The new value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `float` | The current value. |
| `target` | `float` | The value to move towards. |
| `maxDelta` | `float` | The maximum change that should be applied to the value. |

---

<a id="movetowardsangle"></a>

### MoveTowardsAngle

`static` `inline`

```java
static inline float MoveTowardsAngle(float current, float target, float maxDelta)
```

Same as MoveTowards but makes sure the values interpolate correctly when they wrap around 360 degrees.

#### Returns
The new angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `float` | The current angle in degrees. |
| `target` | `float` | The target angle in degrees. |
| `maxDelta` | `float` | The maximum change in degrees. |

---

<a id="smoothstep"></a>

### SmoothStep

`static` `inline`

```java
static inline float SmoothStep(float from, float to, float t)
```

Interpolates between min and max with smoothing at the limits.

#### Returns
The smoothed interpolated value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `from` | `float` | The start value. |
| `to` | `float` | The end value. |
| `t` | `float` | The interpolation parameter. |

---

<a id="approximately"></a>

### Approximately

`static` `inline`

```java
static inline bool Approximately(float a, float b)
```

Compares two floating point values and returns true if they are similar.

#### Returns
True if the values are approximately equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The first value. |
| `b` | `float` | The second value. |

---

<a id="smoothdamp"></a>

### SmoothDamp

`static` `inline`

```java
static inline float SmoothDamp(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
```

Gradually changes a value towards a desired goal over time.

#### Returns
The smoothed value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `float` | The current position. |
| `target` | `float` | The position we are trying to reach. |
| `currentVelocity` | `ref float` | The current velocity, this value is modified by the function every time you call it. |
| `smoothTime` | `float` | Approximately the time it will take to reach the target. A smaller value will reach the target faster. |
| `maxSpeed` | `float` | Optionally allows you to clamp the maximum speed. |
| `deltaTime` | `float` | The time since the last call to this function. |

---

<a id="smoothdampangle"></a>

### SmoothDampAngle

`static` `inline`

```java
static inline float SmoothDampAngle(float current, float target, ref float currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
```

Gradually changes an angle towards a desired goal over time.

#### Returns
The smoothed angle.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `float` | The current angle in degrees. |
| `target` | `float` | The angle we are trying to reach in degrees. |
| `currentVelocity` | `ref float` | The current angular velocity, this value is modified by the function every time you call it. |
| `smoothTime` | `float` | Approximately the time it will take to reach the target. A smaller value will reach the target faster. |
| `maxSpeed` | `float` | Optionally allows you to clamp the maximum angular speed. |
| `deltaTime` | `float` | The time since the last call to this function. |

---

<a id="repeat"></a>

### Repeat

`static` `inline`

```java
static inline float Repeat(float t, float length)
```

Loops the value t, so that it is never larger than length and never smaller than 0.

#### Returns
The looped value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `t` | `float` | The input value. |
| `length` | `float` | The length of the loop. |

---

<a id="pingpong"></a>

### PingPong

`static` `inline`

```java
static inline float PingPong(float t, float length)
```

PingPong returns a value that will increment and decrement between the value 0 and length.

#### Returns
The ping-ponged value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `t` | `float` | The input value. |
| `length` | `float` | The length of the ping-pong range. |

---

<a id="inverselerp"></a>

### InverseLerp

`static` `inline`

```java
static inline float InverseLerp(float a, float b, float value)
```

Determines where a value lies between two points.

#### Returns
A value between zero and one, representing where the "value" parameter falls within the range defined by a and b.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `float` | The start of the range. |
| `b` | `float` | The end of the range. |
| `value` | `float` | The point within the range you want to calculate. |

---

<a id="deltaangle"></a>

### DeltaAngle

`static` `inline`

```java
static inline float DeltaAngle(float current, float target)
```

Calculates the shortest difference between two given angles given in degrees.

#### Returns
The shortest angular difference in degrees.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `current` | `float` | The current angle in degrees. |
| `target` | `float` | The target angle in degrees. |

