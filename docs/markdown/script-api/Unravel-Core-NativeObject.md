<a id="nativeobject"></a>

# NativeObject

> **Extends:** `IEquatable< NativeObject >`
> **Subclassed by:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component), [`Unravel.Core.UIDocument`](Unravel-Core-UIDocument.md#uidocument), [`Unravel.Core.UIElement`](Unravel-Core-UIElement.md#uielement)

Base class for managed wrappers around native engine objects. Equality accounts for validity; invalid instances are never equal.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`IsValid`](#isvalid-3)  | Returns whether this object still refers to a valid native instance. |
| `bool` | [`Equals`](#equals-18) `inline` | Determines whether this instance is equal to another [NativeObject](#nativeobject). |
| `override bool` | [`Equals`](#equals-19) `inline` | Determines whether this instance is equal to the specified object. |
| `override int` | [`GetHashCode`](#gethashcode-10) `inline` | Returns a hash code for this instance. |

---

<a id="isvalid-3"></a>

### IsValid

```java
bool IsValid()
```

Returns whether this object still refers to a valid native instance.

---

<a id="equals-18"></a>

### Equals

`inline`

```java
inline bool Equals(NativeObject other)
```

Determines whether this instance is equal to another [NativeObject](#nativeobject).

---

<a id="equals-19"></a>

### Equals

`inline`

```java
inline override bool Equals(object obj)
```

Determines whether this instance is equal to the specified object.

---

<a id="gethashcode-10"></a>

### GetHashCode

`inline`

```java
inline override int GetHashCode()
```

Returns a hash code for this instance.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`operator==`](#operator-62) `static` `inline` | Compares two [NativeObject](#nativeobject) instances for equality. |
| `bool` | [`operator!=`](#operator-63) `static` `inline` | Compares two [NativeObject](#nativeobject) instances for inequality. |
| `implicit` | [`operator bool`](#operatorbool-1) `static` `inline` | Implicit conversion to bool for null checking. Returns true if the object is valid. |

---

<a id="operator-62"></a>

### operator==

`static` `inline`

```java
static inline bool operator==(NativeObject left, NativeObject right)
```

Compares two [NativeObject](#nativeobject) instances for equality.

Expensive: calls [IsValid](#isvalid-3), which may involve native calls. For performance-critical null checks, prefer object.ReferenceEquals.

---

<a id="operator-63"></a>

### operator!=

`static` `inline`

```java
static inline bool operator!=(NativeObject left, NativeObject right)
```

Compares two [NativeObject](#nativeobject) instances for inequality.

---

<a id="operatorbool-1"></a>

### operator bool

`static` `inline`

```java
static inline implicit operator bool(NativeObject obj)
```

Implicit conversion to bool for null checking. Returns true if the object is valid.

#### Returns
`true` if the entity is valid; otherwise, `false`.

