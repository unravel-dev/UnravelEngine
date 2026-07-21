<a id="ray"></a>

# Ray

> **Extends:** `IFormattable`

Represents a ray with an origin and a direction in 3D space.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`origin`](#origin)  | The origin point of the ray in 3D space. |
| `Vector3` | [`direction`](#direction-1)  | The direction of the ray in 3D space. |

---

<a id="origin"></a>

### origin

```java
Vector3 origin
```

The origin point of the ray in 3D space.

---

<a id="direction-1"></a>

### direction

```java
Vector3 direction
```

The direction of the ray in 3D space.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override string` | [`ToString`](#tostring-24) `inline` | Returns a string representation of the ray. |
| `string` | [`ToString`](#tostring-25) `inline` | Returns a string representation of the ray using a specified format. |
| `string` | [`ToString`](#tostring-26) `inline` | Returns a string representation of the ray using a specified format and format provider. |

---

<a id="tostring-24"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a string representation of the ray.

#### Returns
A string that represents the ray.

---

<a id="tostring-25"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Returns a string representation of the ray using a specified format.

#### Returns
A string that represents the ray.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string to use. |

---

<a id="tostring-26"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Returns a string representation of the ray using a specified format and format provider.

#### Returns
A string that represents the ray.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string to use. |
| `formatProvider` | `IFormatProvider` | An object that provides culture-specific formatting information. |

