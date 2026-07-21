<a id="referenceequalitycomparer"></a>

# ReferenceEqualityComparer

> **Extends:** `IEqualityComparer< ScriptComponent >`

Comparer that uses reference equality instead of overridden Equals/GetHashCode. This prevents issues when [NativeObject](Unravel-Core-NativeObject.md#nativeobject)'s IsValid() state changes after adding to collections.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`Equals`](#equals-20) `inline` |  |
| `int` | [`GetHashCode`](#gethashcode-11) `inline` |  |

---

<a id="equals-20"></a>

### Equals

`inline`

```java
inline bool Equals(ScriptComponent x, ScriptComponent y)
```

---

<a id="gethashcode-11"></a>

### GetHashCode

`inline`

```java
inline int GetHashCode(ScriptComponent obj)
```

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly ReferenceEqualityComparer` | [`Instance`](#instance) `static` |  |

---

<a id="instance"></a>

### Instance

`static`

```java
readonly ReferenceEqualityComparer Instance = new ReferenceEqualityComparer()
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`ReferenceEqualityComparer`](#referenceequalitycomparer-1) `inline` |  |

---

<a id="referenceequalitycomparer-1"></a>

### ReferenceEqualityComparer

`inline`

```java
inline ReferenceEqualityComparer()
```

