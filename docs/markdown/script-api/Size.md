<a id="size"></a>

# Size

Represents a size with width and height components.

#### Template Parameters
* `T` The type of the width and height components, must implement IComparable<T>.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `T` | [`Width`](#width)  | The width component. |
| `T` | [`Height`](#height)  | The height component. |

---

<a id="width"></a>

### Width

```java
T Width
```

The width component.

---

<a id="height"></a>

### Height

```java
T Height
```

The height component.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`Size`](#size-1) `inline` | Creates a new [Size](#size) with the specified width and height. |
| `bool` | [`IsValid`](#isvalid)  | Determines whether the size is valid (both width and height are not default values). |
| `override bool` | [`Equals`](#equals)  | Returns true if the given object is equal to this size. |
| `override int` | [`GetHashCode`](#gethashcode)  | Returns the hash code for this instance. |

---

<a id="size-1"></a>

### Size

`inline`

```java
inline Size(T width, T height)
```

Creates a new [Size](#size) with the specified width and height.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `width` | `T` | The width. |
| `height` | `T` | The height. |

---

<a id="isvalid"></a>

### IsValid

```java
bool IsValid()
```

Determines whether the size is valid (both width and height are not default values).

#### Returns
True if the size is valid; otherwise, false.

---

<a id="equals"></a>

### Equals

```java
override bool Equals(object obj)
```

Returns true if the given object is equal to this size.

#### Returns
True if the given object is equal to this size; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `object` | The object to compare with the current instance. |

---

<a id="gethashcode"></a>

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
| `bool` | [`operator==`](#operator) `static` | Determines whether two sizes are equal. |
| `bool` | [`operator!=`](#operator-1) `static` | Determines whether two sizes are not equal. |
| `bool` | [`operator<`](#operator-2) `static` `inline` | Determines whether the first size is less than the second size. |
| `bool` | [`operator>`](#operator-3) `static` `inline` | Determines whether the first size is greater than the second size. |

---

<a id="operator"></a>

### operator==

`static`

```java
static bool operator==(Size< T > a, Size< T > b)
```

Determines whether two sizes are equal.

#### Returns
True if the sizes are equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Size](#size)< T >` | The first size. |
| `b` | `[Size](#size)< T >` | The second size. |

---

<a id="operator-1"></a>

### operator!=

`static`

```java
static bool operator!=(Size< T > a, Size< T > b)
```

Determines whether two sizes are not equal.

#### Returns
True if the sizes are not equal; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Size](#size)< T >` | The first size. |
| `b` | `[Size](#size)< T >` | The second size. |

---

<a id="operator-2"></a>

### operator<

`static` `inline`

```java
static inline bool operator<(Size< T > a, Size< T > b)
```

Determines whether the first size is less than the second size.

#### Returns
True if a is less than b; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Size](#size)< T >` | The first size. |
| `b` | `[Size](#size)< T >` | The second size. |

---

<a id="operator-3"></a>

### operator>

`static` `inline`

```java
static inline bool operator>(Size< T > a, Size< T > b)
```

Determines whether the first size is greater than the second size.

#### Returns
True if a is greater than b; otherwise, false.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[Size](#size)< T >` | The first size. |
| `b` | `[Size](#size)< T >` | The second size. |

