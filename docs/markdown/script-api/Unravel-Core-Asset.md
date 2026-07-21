<a id="asset"></a>

# Asset

> **Extends:** `IEquatable< Asset< T > >`

Represents a generic asset with a unique identifier (UID).

#### Template Parameters
* `T` The type of the asset.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Guid` | [`uid`](#uid)  | Gets the unique identifier (UID) of the asset. |

---

<a id="uid"></a>

### uid

```java
Guid uid
```

Gets the unique identifier (UID) of the asset.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override bool` | [`Equals`](#equals-14) `inline` | Determines whether the specified object is equal to the current asset. |
| `bool` | [`Equals`](#equals-15) `inline` | Determines whether the specified Asset<T> is equal to the current asset. |
| `override int` | [`GetHashCode`](#gethashcode-8)  | Serves as the default hash function for the asset. |

---

<a id="equals-14"></a>

### Equals

`inline`

```java
inline override bool Equals(object obj)
```

Determines whether the specified object is equal to the current asset.

#### Returns
`true` if the specified object is an Asset<T> and is equal to the current asset; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `obj` | `object` | The object to compare with the current asset. |

---

<a id="equals-15"></a>

### Equals

`inline`

```java
inline bool Equals(Asset< T > other)
```

Determines whether the specified Asset<T> is equal to the current asset.

#### Returns
`true` if the specified asset is equal to the current asset; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Asset](#asset)< T >` | The asset to compare with the current asset. |

---

<a id="gethashcode-8"></a>

### GetHashCode

```java
override int GetHashCode()
```

Serves as the default hash function for the asset.

#### Returns
A hash code for the current asset.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`operator==`](#operator-54) `static` `inline` | Determines whether two Asset<T> instances are equal. |
| `bool` | [`operator!=`](#operator-55) `static` | Determines whether two Asset<T> instances are not equal. |

---

<a id="operator-54"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(Asset< T > lhs, Asset< T > rhs)
```

Determines whether two Asset<T> instances are equal.

#### Returns
`true` if the two assets are equal; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Asset](#asset)< T >` | The first asset to compare. |
| `rhs` | `[Asset](#asset)< T >` | The second asset to compare. |

---

<a id="operator-55"></a>

### operator!=

`static`

```java
static bool operator!=(Asset< T > lhs, Asset< T > rhs)
```

Determines whether two Asset<T> instances are not equal.

#### Returns
`true` if the two assets are not equal; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Asset](#asset)< T >` | The first asset to compare. |
| `rhs` | `[Asset](#asset)< T >` | The second asset to compare. |

