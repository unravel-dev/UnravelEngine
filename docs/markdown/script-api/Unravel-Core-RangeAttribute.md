<a id="rangeattribute"></a>

# RangeAttribute

> **Extends:** `Attribute`

Specifies a range for a numeric field in a class or struct.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly float` | [`min`](#min-10)  | The minimum value of the range. |
| `readonly float` | [`max`](#max-10)  | The maximum value of the range. |

---

<a id="min-10"></a>

### min

```java
readonly float min
```

The minimum value of the range.

---

<a id="max-10"></a>

### max

```java
readonly float max
```

The maximum value of the range.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`RangeAttribute`](#rangeattribute-1) `inline` | Initializes a new instance of the [RangeAttribute](#rangeattribute) class. |

---

<a id="rangeattribute-1"></a>

### RangeAttribute

`inline`

```java
inline RangeAttribute(float min, float max)
```

Initializes a new instance of the [RangeAttribute](#rangeattribute) class.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `min` | `float` | The minimum value of the range. |
| `max` | `float` | The maximum value of the range. |

