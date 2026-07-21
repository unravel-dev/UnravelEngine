<a id="color"></a>

# Color

> **Extends:** `IEquatable< Color >`, `IFormattable`

Representation of RGBA colors in floating-point format (0-1 range).

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Color` | [`red`](#red) `static` | Solid red. RGBA is (1, 0, 0, 1). |
| `Color` | [`green`](#green) `static` | Solid green. RGBA is (0, 1, 0, 1). |
| `Color` | [`blue`](#blue) `static` | Solid blue. RGBA is (0, 0, 1, 1). |
| `Color` | [`white`](#white) `static` | Solid white. RGBA is (1, 1, 1, 1). |
| `Color` | [`black`](#black) `static` | Solid black. RGBA is (0, 0, 0, 1). |
| `Color` | [`yellow`](#yellow) `static` | Yellow. RGBA is (1, 0.92, 0.016, 1), but the color is nice to look at! |
| `Color` | [`cyan`](#cyan) `static` | Cyan. RGBA is (0, 1, 1, 1). |
| `Color` | [`magenta`](#magenta) `static` | Magenta. RGBA is (1, 0, 1, 1). |
| `Color` | [`gray`](#gray) `static` | Gray. RGBA is (0.5, 0.5, 0.5, 1). |
| `Color` | [`grey`](#grey) `static` | English spelling for gray. RGBA is the same (0.5, 0.5, 0.5, 1). |
| `Color` | [`clear`](#clear) `static` | Completely transparent. RGBA is (0, 0, 0, 0). |
| `float` | [`grayscale`](#grayscale)  | The grayscale value of the color. (Read Only) |
| `float` | [`maxColorComponent`](#maxcolorcomponent)  | Returns the maximum color component value: Max(r,g,b). |
| `float` | [`this[int index]`](#thisintindex)  | Gets or sets the component at the specified index. |

---

<a id="red"></a>

### red

`static`

```java
Color red
```

Solid red. RGBA is (1, 0, 0, 1).

---

<a id="green"></a>

### green

`static`

```java
Color green
```

Solid green. RGBA is (0, 1, 0, 1).

---

<a id="blue"></a>

### blue

`static`

```java
Color blue
```

Solid blue. RGBA is (0, 0, 1, 1).

---

<a id="white"></a>

### white

`static`

```java
Color white
```

Solid white. RGBA is (1, 1, 1, 1).

---

<a id="black"></a>

### black

`static`

```java
Color black
```

Solid black. RGBA is (0, 0, 0, 1).

---

<a id="yellow"></a>

### yellow

`static`

```java
Color yellow
```

Yellow. RGBA is (1, 0.92, 0.016, 1), but the color is nice to look at!

---

<a id="cyan"></a>

### cyan

`static`

```java
Color cyan
```

Cyan. RGBA is (0, 1, 1, 1).

---

<a id="magenta"></a>

### magenta

`static`

```java
Color magenta
```

Magenta. RGBA is (1, 0, 1, 1).

---

<a id="gray"></a>

### gray

`static`

```java
Color gray
```

Gray. RGBA is (0.5, 0.5, 0.5, 1).

---

<a id="grey"></a>

### grey

`static`

```java
Color grey
```

English spelling for gray. RGBA is the same (0.5, 0.5, 0.5, 1).

---

<a id="clear"></a>

### clear

`static`

```java
Color clear
```

Completely transparent. RGBA is (0, 0, 0, 0).

---

<a id="grayscale"></a>

### grayscale

```java
float grayscale
```

The grayscale value of the color. (Read Only)

---

<a id="maxcolorcomponent"></a>

### maxColorComponent

```java
float maxColorComponent
```

Returns the maximum color component value: Max(r,g,b).

---

<a id="thisintindex"></a>

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

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`r`](#r)  | Red component of the color. |
| `float` | [`g`](#g)  | Green component of the color. |
| `float` | [`b`](#b)  | Blue component of the color. |
| `float` | [`a`](#a)  | Alpha component of the color (0 is transparent, 1 is opaque). |

---

<a id="r"></a>

### r

```java
float r
```

Red component of the color.

---

<a id="g"></a>

### g

```java
float g
```

Green component of the color.

---

<a id="b"></a>

### b

```java
float b
```

Blue component of the color.

---

<a id="a"></a>

### a

```java
float a
```

Alpha component of the color (0 is transparent, 1 is opaque).

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Color`](#color-1) `inline` | Constructs a new [Color](#color) with given r,g,b,a components. |
|  | [`Color`](#color-2) `inline` | Constructs a new [Color](#color) with given r,g,b components and sets a to 1. |
|  | [`Color`](#color-3) `inline` | Constructs a new [Color](#color) with given r,g,b,a components as integers (0-255 range). |
|  | [`Color`](#color-4) `inline` | Constructs a new [Color](#color) with given r,g,b components as integers (0-255 range) and sets a to 255. |
| `override string` | [`ToString`](#tostring) `inline` | Returns a formatted string of this color. |
| `string` | [`ToString`](#tostring-1) `inline` | Returns a formatted string of this color. |
| `string` | [`ToString`](#tostring-2) `inline` | Returns a formatted string of this color. |
| `override int` | [`GetHashCode`](#gethashcode-1) `inline` | Returns the hash code for this instance. |
| `override bool` | [`Equals`](#equals-1) `inline` | Returns true if the given color is exactly equal to this color. |
| `bool` | [`Equals`](#equals-2) `inline` | Returns true if the given color is exactly equal to this color. |

---

<a id="color-1"></a>

### Color

`inline`

```java
inline Color(float r, float g, float b, float a)
```

Constructs a new [Color](#color) with given r,g,b,a components.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `float` | Red component. |
| `g` | `float` | Green component. |
| `b` | `float` | Blue component. |
| `a` | `float` | Alpha component. |

---

<a id="color-2"></a>

### Color

`inline`

```java
inline Color(float r, float g, float b)
```

Constructs a new [Color](#color) with given r,g,b components and sets a to 1.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `float` | Red component. |
| `g` | `float` | Green component. |
| `b` | `float` | Blue component. |

---

<a id="color-3"></a>

### Color

`inline`

```java
inline Color(int r, int g, int b, int a)
```

Constructs a new [Color](#color) with given r,g,b,a components as integers (0-255 range).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `int` | Red component (0-255). |
| `g` | `int` | Green component (0-255). |
| `b` | `int` | Blue component (0-255). |
| `a` | `int` | Alpha component (0-255). |

---

<a id="color-4"></a>

### Color

`inline`

```java
inline Color(int r, int g, int b)
```

Constructs a new [Color](#color) with given r,g,b components as integers (0-255 range) and sets a to 255.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `r` | `int` | Red component (0-255). |
| `g` | `int` | Green component (0-255). |
| `b` | `int` | Blue component (0-255). |

---

<a id="tostring"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a formatted string of this color.

#### Returns
A formatted string representation of the color.

Returns a formatted string of this color.

#### Returns
A formatted string representation of the color.

---

<a id="tostring-1"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Returns a formatted string of this color.

#### Returns
A formatted string representation of the color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |

---

<a id="tostring-2"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Returns a formatted string of this color.

#### Returns
A formatted string representation of the color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | A numeric format string. |
| `formatProvider` | `IFormatProvider` | An object that specifies culture-specific formatting. |

---

<a id="gethashcode-1"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

---

<a id="equals-1"></a>

### Equals

`inline`

```java
inline override bool Equals(object other)
```

Returns true if the given color is exactly equal to this color.

#### Returns
True if the given color is exactly equal to this color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `object` | The object to compare with the current instance. |

---

<a id="equals-2"></a>

### Equals

`inline`

```java
inline bool Equals(Color other)
```

Returns true if the given color is exactly equal to this color.

#### Returns
True if the given color is exactly equal to this color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Color](#color)` | The color to compare with the current instance. |

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `Color` | [`operator+`](#operator-4) `static` `inline` | Adds two colors component-wise. |
| `Color` | [`operator-`](#operator-5) `static` `inline` | Subtracts two colors component-wise. |
| `Color` | [`operator*`](#operator-6) `static` `inline` | Multiplies two colors component-wise. |
| `Color` | [`operator*`](#operator-7) `static` `inline` | Multiplies a color by a scalar. |
| `Color` | [`operator*`](#operator-8) `static` `inline` | Multiplies a scalar by a color. |
| `Color` | [`operator/`](#operator-9) `static` `inline` | Divides a color by a scalar. |
| `bool` | [`operator==`](#operator-10) `static` `inline` | Determines whether two colors are equal. |
| `bool` | [`operator!=`](#operator-11) `static` `inline` | Determines whether two colors are not equal. |
| `Color` | [`Lerp`](#lerp) `static` `inline` | Linearly interpolates between colors a and b by t. |
| `Color` | [`LerpUnclamped`](#lerpunclamped) `static` `inline` | Linearly interpolates between colors a and b by t without clamping the interpolation parameter. |
| `implicit` | [`operator Vector4`](#operatorvector4) `static` `inline` | Implicitly converts a [Color](#color) to a [Vector4](Vector4.md#vector4). |
| `implicit` | [`operator Color`](#operatorcolor) `static` `inline` | Implicitly converts a [Vector4](Vector4.md#vector4) to a [Color](#color). |
| `void` | [`RGBToHSV`](#rgbtohsv) `static` `inline` | Converts an RGB color to HSV color space. |
| `Color` | [`HSVToRGB`](#hsvtorgb) `static` `inline` | Creates an RGB colour from HSV input. |
| `Color` | [`HSVToRGB`](#hsvtorgb-1) `static` `inline` | Creates an RGB colour from HSV input. |

---

<a id="operator-4"></a>

### operator+

`static` `inline`

```java
static inline Color operator+(Color a, Color b)
```

Adds two colors component-wise.

#### Returns
The sum of the two colors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The first color. |
| `b` | `[Color](#color)` | The second color. |

---

<a id="operator-5"></a>

### operator-

`static` `inline`

```java
static inline Color operator-(Color a, Color b)
```

Subtracts two colors component-wise.

#### Returns
The difference of the two colors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The first color. |
| `b` | `[Color](#color)` | The second color. |

---

<a id="operator-6"></a>

### operator*

`static` `inline`

```java
static inline Color operator*(Color a, Color b)
```

Multiplies two colors component-wise.

#### Returns
The component-wise product of the two colors.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The first color. |
| `b` | `[Color](#color)` | The second color. |

---

<a id="operator-7"></a>

### operator*

`static` `inline`

```java
static inline Color operator*(Color a, float b)
```

Multiplies a color by a scalar.

#### Returns
The scaled color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The color. |
| `b` | `float` | The scalar value. |

---

<a id="operator-8"></a>

### operator*

`static` `inline`

```java
static inline Color operator*(float b, Color a)
```

Multiplies a scalar by a color.

#### Returns
The scaled color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `b` | `float` | The scalar value. |
| `a` | `[Color](#color)` | The color. |

---

<a id="operator-9"></a>

### operator/

`static` `inline`

```java
static inline Color operator/(Color a, float b)
```

Divides a color by a scalar.

#### Returns
The divided color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The color. |
| `b` | `float` | The scalar value. |

---

<a id="operator-10"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Color lhs, Color rhs)
```

Determines whether two colors are equal.

#### Returns
True if the colors are equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Color](#color)` | The left-hand side color. |
| `rhs` | `[Color](#color)` | The right-hand side color. |

---

<a id="operator-11"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(Color lhs, Color rhs)
```

Determines whether two colors are not equal.

#### Returns
True if the colors are not equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Color](#color)` | The left-hand side color. |
| `rhs` | `[Color](#color)` | The right-hand side color. |

---

<a id="lerp"></a>

### Lerp

`static` `inline`

```java
static inline Color Lerp(Color a, Color b, float t)
```

Linearly interpolates between colors a and b by t.

#### Returns
The interpolated color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | [Color](#color) a. |
| `b` | `[Color](#color)` | [Color](#color) b. |
| `t` | `float` | Float for combining a and b. |

---

<a id="lerpunclamped"></a>

### LerpUnclamped

`static` `inline`

```java
static inline Color LerpUnclamped(Color a, Color b, float t)
```

Linearly interpolates between colors a and b by t without clamping the interpolation parameter.

#### Returns
The interpolated color.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Color](#color)` | The first color. |
| `b` | `[Color](#color)` | The second color. |
| `t` | `float` | The interpolation parameter (not clamped). |

---

<a id="operatorvector4"></a>

### operator Vector4

`static` `inline`

```java
static inline implicit operator Vector4(Color c)
```

Implicitly converts a [Color](#color) to a [Vector4](Vector4.md#vector4).

#### Returns
A [Vector4](Vector4.md#vector4) with r, g, b, a components from the [Color](#color).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `[Color](#color)` | The [Color](#color) to convert. |

---

<a id="operatorcolor"></a>

### operator Color

`static` `inline`

```java
static inline implicit operator Color(Vector4 v)
```

Implicitly converts a [Vector4](Vector4.md#vector4) to a [Color](#color).

#### Returns
A [Color](#color) with r, g, b, a components from the [Vector4](Vector4.md#vector4).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `v` | `[Vector4](Vector4.md#vector4)` | The [Vector4](Vector4.md#vector4) to convert. |

---

<a id="rgbtohsv"></a>

### RGBToHSV

`static` `inline`

```java
static inline void RGBToHSV(Color rgbColor, out float H, out float S, out float V)
```

Converts an RGB color to HSV color space.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `rgbColor` | `[Color](#color)` | The RGB color to convert. |
| `H` | `out float` | The hue component (0-1). |
| `S` | `out float` | The saturation component (0-1). |
| `V` | `out float` | The value/brightness component (0-1). |

---

<a id="hsvtorgb"></a>

### HSVToRGB

`static` `inline`

```java
static inline Color HSVToRGB(float H, float S, float V)
```

Creates an RGB colour from HSV input.

#### Returns
An opaque colour with HSV matching the input.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `H` | `float` | Hue [0..1]. |
| `S` | `float` | Saturation [0..1]. |
| `V` | `float` | Brightness value [0..1]. |

---

<a id="hsvtorgb-1"></a>

### HSVToRGB

`static` `inline`

```java
static inline Color HSVToRGB(float H, float S, float V, bool hdr)
```

Creates an RGB colour from HSV input.

#### Returns
An opaque colour with HSV matching the input.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `H` | `float` | Hue [0..1]. |
| `S` | `float` | Saturation [0..1]. |
| `V` | `float` | Brightness value [0..1]. |
| `hdr` | `bool` | Output HDR colours. If true, the returned colour will not be clamped to [0..1]. |

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`RGBToHSVHelper`](#rgbtohsvhelper) `static` `inline` |  |

---

<a id="rgbtohsvhelper"></a>

### RGBToHSVHelper

`static` `inline`

```java
static inline void RGBToHSVHelper(float offset, float dominantcolor, float colorone, float colortwo, out float H, out float S, out float V)
```

