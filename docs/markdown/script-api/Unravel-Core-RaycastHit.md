<a id="raycasthit"></a>

# RaycastHit

> **Extends:** `IFormattable`

Represents information about a single hit during a raycasting operation.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`entity`](#entity-2)  | The [Entity](Unravel-Core-Entity.md#entity-1) that was hit by the ray. |
| `Vector3` | [`point`](#point-1)  | The point in 3D space where the ray hit the entity. |
| `Vector3` | [`normal`](#normal-1)  | The normal vector at the point of contact on the entity's surface. |
| `float` | [`distance`](#distance-4)  | The distance from the ray's origin to the point of contact. |

---

<a id="entity-2"></a>

### entity

```java
Entity entity
```

The [Entity](Unravel-Core-Entity.md#entity-1) that was hit by the ray.

---

<a id="point-1"></a>

### point

```java
Vector3 point
```

The point in 3D space where the ray hit the entity.

---

<a id="normal-1"></a>

### normal

```java
Vector3 normal
```

The normal vector at the point of contact on the entity's surface.

---

<a id="distance-4"></a>

### distance

```java
float distance
```

The distance from the ray's origin to the point of contact.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override string` | [`ToString`](#tostring-27) `inline` | Returns a string representation of the raycast hit. |
| `string` | [`ToString`](#tostring-28) `inline` | Returns a string representation of the raycast hit using a specified format. |
| `string` | [`ToString`](#tostring-29) `inline` | Returns a string representation of the raycast hit using a specified format and format provider. |

---

<a id="tostring-27"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Returns a string representation of the raycast hit.

#### Returns
A string that represents the raycast hit.

---

<a id="tostring-28"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Returns a string representation of the raycast hit using a specified format.

#### Returns
A string that represents the raycast hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string to use. |

---

<a id="tostring-29"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Returns a string representation of the raycast hit using a specified format and format provider.

#### Returns
A string that represents the raycast hit.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string to use. |
| `formatProvider` | `IFormatProvider` | An object that provides culture-specific formatting information. |

