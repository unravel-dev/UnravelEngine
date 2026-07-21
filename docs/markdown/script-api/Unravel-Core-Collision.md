<a id="collision"></a>

# Collision

> **Extends:** `IFormattable`

Represents a collision that occurs between two entities.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`entity`](#entity)  | Gets the entity involved in the collision. |
| `ContactPoint[]` | [`contacts`](#contacts)  | Gets the array of contact points where the collision occurred. |

---

<a id="entity"></a>

### entity

```java
Entity entity
```

Gets the entity involved in the collision.

---

<a id="contacts"></a>

### contacts

```java
ContactPoint[] contacts
```

Gets the array of contact points where the collision occurred.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override string` | [`ToString`](#tostring-15) `inline` | Converts the collision to its string representation. |
| `string` | [`ToString`](#tostring-16) `inline` | Converts the collision to its string representation with a specified format. |
| `string` | [`ToString`](#tostring-17) `inline` | Converts the collision to its string representation with a specified format and format provider. |

---

<a id="tostring-15"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Converts the collision to its string representation.

#### Returns
A string that represents the collision.

---

<a id="tostring-16"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Converts the collision to its string representation with a specified format.

#### Returns
A string that represents the collision.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |

---

<a id="tostring-17"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Converts the collision to its string representation with a specified format and format provider.

#### Returns
A string that represents the collision.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |
| `formatProvider` | `IFormatProvider` | An object that supplies culture-specific formatting information. |

