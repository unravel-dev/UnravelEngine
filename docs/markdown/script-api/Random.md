<a id="random"></a>

# Random

Pseudo-random number generator utilities for gameplay and sampling.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Color` | [`color`](#color-5) `static` | Returns a random color with RGB components in the range [0.0, 1.0). |
| `Vector3` | [`onUnitSphere`](#onunitsphere) `static` | Returns a random point on the surface of a unit sphere. |
| `Vector3` | [`insideUnitSphere`](#insideunitsphere) `static` | Returns a random point inside a unit sphere. |
| `Vector2` | [`insideUnitCircle`](#insideunitcircle) `static` | Returns a random point inside a unit circle. |

---

<a id="color-5"></a>

### color

`static`

```java
Color color
```

Returns a random color with RGB components in the range [0.0, 1.0).

---

<a id="onunitsphere"></a>

### onUnitSphere

`static`

```java
Vector3 onUnitSphere
```

Returns a random point on the surface of a unit sphere.

---

<a id="insideunitsphere"></a>

### insideUnitSphere

`static`

```java
Vector3 insideUnitSphere
```

Returns a random point inside a unit sphere.

---

<a id="insideunitcircle"></a>

### insideUnitCircle

`static`

```java
Vector2 insideUnitCircle
```

Returns a random point inside a unit circle.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Seed`](#seed) `static` `inline` | Re-seeds the generator with a specific 64-bit seed. |
| `void` | [`SeedWithTime`](#seedwithtime) `static` `inline` | Seeds the generator using the current system time (ticks + process ID). |
| `ulong` | [`UInt64`](#uint64) `static` `inline` | Returns a random 64-bit unsigned integer. |
| `float` | [`Float`](#float) `static` `inline` | Returns a random float value between 0.0 and 1.0. |
| `Vector3` | [`Vec3`](#vec3) `static` `inline` | Returns a random [Vector3](Vector3.md#vector3) with components in the range [0.0, 1.0). |
| `double` | [`Double`](#double) `static` `inline` | Returns a random double value between 0.0 and 1.0. |
| `float` | [`SignF`](#signf) `static` `inline` | Returns a random sign value: either 1.0f or -1.0f. |
| `float` | [`Range`](#range) `static` `inline` | Returns a random float value between minValue (inclusive) and maxValue (exclusive). |
| `int` | [`Range`](#range-1) `static` `inline` | Returns a random integer value between minValue (inclusive) and maxValue (exclusive). |

---

<a id="seed"></a>

### Seed

`static` `inline`

```java
static inline void Seed(ulong seed)
```

Re-seeds the generator with a specific 64-bit seed.

---

<a id="seedwithtime"></a>

### SeedWithTime

`static` `inline`

```java
static inline void SeedWithTime()
```

Seeds the generator using the current system time (ticks + process ID).

---

<a id="uint64"></a>

### UInt64

`static` `inline`

```java
static inline ulong UInt64()
```

Returns a random 64-bit unsigned integer.

#### Returns
A random 64-bit unsigned integer.

---

<a id="float"></a>

### Float

`static` `inline`

```java
static inline float Float()
```

Returns a random float value between 0.0 and 1.0.

#### Returns
A random float value in the range [0.0, 1.0).

---

<a id="vec3"></a>

### Vec3

`static` `inline`

```java
static inline Vector3 Vec3()
```

Returns a random [Vector3](Vector3.md#vector3) with components in the range [0.0, 1.0).

#### Returns
A random [Vector3](Vector3.md#vector3).

---

<a id="double"></a>

### Double

`static` `inline`

```java
static inline double Double()
```

Returns a random double value between 0.0 and 1.0.

#### Returns
A random double value in the range [0.0, 1.0).

---

<a id="signf"></a>

### SignF

`static` `inline`

```java
static inline float SignF()
```

Returns a random sign value: either 1.0f or -1.0f.

#### Returns
1.0f or -1.0f.

---

<a id="range"></a>

### Range

`static` `inline`

```java
static inline float Range(float minValue, float maxValue)
```

Returns a random float value between minValue (inclusive) and maxValue (exclusive).

#### Returns
A random float value in the range [minValue, maxValue).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `minValue` | `float` | The minimum value (inclusive). |
| `maxValue` | `float` | The maximum value (exclusive). |

---

<a id="range-1"></a>

### Range

`static` `inline`

```java
static inline int Range(int minValue, int maxValue)
```

Returns a random integer value between minValue (inclusive) and maxValue (exclusive).

#### Returns
A random integer value in the range [minValue, maxValue).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `minValue` | `int` | The minimum value (inclusive). |
| `maxValue` | `int` | The maximum value (exclusive). |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `ulong[]` | [`s_State`](#s_state) `static` |  |
| `ulong` | [`s_Seed`](#s_seed) `static` |  |
| `const double` | [`INCR_DOUBLE`](#incr_double) `static` |  |
| `const float` | [`INCR_FLOAT`](#incr_float) `static` |  |

---

<a id="s_state"></a>

### s_State

`static`

```java
ulong[] s_State
```

---

<a id="s_seed"></a>

### s_Seed

`static`

```java
ulong s_Seed
```

---

<a id="incr_double"></a>

### INCR_DOUBLE

`static`

```java
const double INCR_DOUBLE = 1.0 / (1UL << 53)
```

---

<a id="incr_float"></a>

### INCR_FLOAT

`static`

```java
const float INCR_FLOAT = 1f / (1U << 24)
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `static` | [`Random`](#random-1) `static` `inline` |  |
| `ulong` | [`splitmix64`](#splitmix64) `static` `inline` |  |
| `ulong[]` | [`xorshift256_init`](#xorshift256_init) `static` `inline` |  |
| `ulong` | [`rol64`](#rol64) `static` `inline` |  |
| `ulong` | [`xoshiro256p`](#xoshiro256p) `static` `inline` |  |

---

<a id="random-1"></a>

### Random

`static` `inline`

```java
static inline static Random()
```

---

<a id="splitmix64"></a>

### splitmix64

`static` `inline`

```java
static inline ulong splitmix64(ulong state)
```

---

<a id="xorshift256_init"></a>

### xorshift256_init

`static` `inline`

```java
static inline ulong[] xorshift256_init(ulong seed)
```

---

<a id="rol64"></a>

### rol64

`static` `inline`

```java
static inline ulong rol64(ulong x, int k)
```

---

<a id="xoshiro256p"></a>

### xoshiro256p

`static` `inline`

```java
static inline ulong xoshiro256p()
```

