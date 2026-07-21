<a id="contactpoint"></a>

# ContactPoint

> **Extends:** `IFormattable`

Represents a contact point where a collision occurs.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Vector3` | [`point`](#point)  | The point of contact in world space. |
| `Vector3` | [`normal`](#normal)  | The normal vector at the contact point. |
| `float` | [`distance`](#distance-3)  | The distance between the colliders at the contact point. |
| `float` | [`impulse`](#impulse)  | The impulse applied to resolve the collision at the contact point. |

---

<a id="point"></a>

### point

```java
Vector3 point
```

The point of contact in world space.

---

<a id="normal"></a>

### normal

```java
Vector3 normal
```

The normal vector at the contact point.

---

<a id="distance-3"></a>

### distance

```java
float distance
```

The distance between the colliders at the contact point.

---

<a id="impulse"></a>

### impulse

```java
float impulse
```

The impulse applied to resolve the collision at the contact point.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override string` | [`ToString`](#tostring-18) `inline` | Converts the contact point to its string representation. |
| `string` | [`ToString`](#tostring-19) `inline` | Converts the contact point to its string representation with a specified format. |
| `string` | [`ToString`](#tostring-20) `inline` | Converts the contact point to its string representation with a specified format and format provider. |

---

<a id="tostring-18"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Converts the contact point to its string representation.

#### Returns
A string that represents the contact point.

---

<a id="tostring-19"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Converts the contact point to its string representation with a specified format.

#### Returns
A string that represents the contact point.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |

---

<a id="tostring-20"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Converts the contact point to its string representation with a specified format and format provider.

#### Returns
A string that represents the contact point.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |
| `formatProvider` | `IFormatProvider` | An object that supplies culture-specific formatting information. |

