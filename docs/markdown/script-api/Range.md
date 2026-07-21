<a id="range-2"></a>

# Range

Represents a range with minimum and maximum values.

#### Template Parameters
* `T` The type of the min and max values, must implement IComparable<T>.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `T` | [`Min`](#min-4)  | The minimum value of the range. |
| `T` | [`Max`](#max-4)  | The maximum value of the range. |

---

<a id="min-4"></a>

### Min

```java
T Min
```

The minimum value of the range.

---

<a id="max-4"></a>

### Max

```java
T Max
```

The maximum value of the range.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Range`](#range-3) `inline` | Creates a new [Range](#range-2) with the specified minimum and maximum values. |
| `bool` | [`Contains`](#contains)  | Determines whether the specified value is within the range [Min, Max]. |
| `override bool` | [`Equals`](#equals-3)  | Returns true if the given object is equal to this range. |
| `override int` | [`GetHashCode`](#gethashcode-2)  | Returns the hash code for this instance. |

---

<a id="range-3"></a>

### Range

`inline`

```java
inline Range(T min, T max)
```

Creates a new [Range](#range-2) with the specified minimum and maximum values.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `min` | `T` | The minimum value. |
| `max` | `T` | The maximum value. |

---

<a id="contains"></a>

### Contains

```java
bool Contains(T value)
```

Determines whether the specified value is within the range [Min, Max].

#### Returns
True if the value is within the range; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `value` | `T` | The value to test. |

---

<a id="equals-3"></a>

### Equals

```java
override bool Equals(object obj)
```

Returns true if the given object is equal to this range.

#### Returns
True if the given object is equal to this range; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `object` | The object to compare with the current instance. |

---

<a id="gethashcode-2"></a>

### GetHashCode

```java
override int GetHashCode()
```

Returns the hash code for this instance.

#### Returns
A 32-bit signed integer that is the hash code for this instance.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`operator==`](#operator-12) `static` | Determines whether two ranges are equal. |
| `bool` | [`operator!=`](#operator-13) `static` | Determines whether two ranges are not equal. |

---

<a id="operator-12"></a>

### operator==

`static`

```java
static bool operator==(Range< T > a, Range< T > b)
```

Determines whether two ranges are equal.

#### Returns
True if the ranges are equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Range](#range-2)< T >` | The first range. |
| `b` | `[Range](#range-2)< T >` | The second range. |

---

<a id="operator-13"></a>

### operator!=

`static`

```java
static bool operator!=(Range< T > a, Range< T > b)
```

Determines whether two ranges are not equal.

#### Returns
True if the ranges are not equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Range](#range-2)< T >` | The first range. |
| `b` | `[Range](#range-2)< T >` | The second range. |

