<a id="material"></a>

# Material

> **Extends:** [`Unravel.Core.Asset< Material >`](Unravel-Core-Asset.md#asset)

[Material](#material) asset with editable shading properties.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Color` | [`color`](#color-7)  | Base (albedo) color. |
| `Color` | [`emissiveColor`](#emissivecolor)  | Emissive color. |
| `Vector2` | [`tiling`](#tiling)  | UV tiling scale. |
| `float` | [`roughness`](#roughness)  | Surface roughness. |
| `float` | [`metalness`](#metalness)  | Surface metalness. |
| `float` | [`bumpiness`](#bumpiness)  | Normal-map bump intensity. |

---

<a id="color-7"></a>

### color

```java
Color color = 
```

Base (albedo) color.

---

<a id="emissivecolor"></a>

### emissiveColor

```java
Color emissiveColor = 
```

Emissive color.

---

<a id="tiling"></a>

### tiling

```java
Vector2 tiling = 
```

UV tiling scale.

---

<a id="roughness"></a>

### roughness

```java
float roughness = 0.0f
```

Surface roughness.

---

<a id="metalness"></a>

### metalness

```java
float metalness = 0.0f
```

Surface metalness.

---

<a id="bumpiness"></a>

### bumpiness

```java
float bumpiness = 1.0f
```

Normal-map bump intensity.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Material`](#material-1) `inline` | Creates an empty material handle. |
|  | [`Material`](#material-2) `inline` | Creates a material copy with the same properties as *rhs* . |

---

<a id="material-1"></a>

### Material

`inline`

```java
inline Material()
```

Creates an empty material handle.

---

<a id="material-2"></a>

### Material

`inline`

```java
inline Material(Material rhs)
```

Creates a material copy with the same properties as *rhs* .

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `rhs` | `[Material](#material)` | The material to copy properties from. |

