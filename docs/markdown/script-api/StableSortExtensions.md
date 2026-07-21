<a id="stablesortextensions"></a>

# StableSortExtensions

Extension methods for stable in-place sorting of lists.

Pooled arrays are keyed by (and contain) script types, which would pin an unloading script domain. Fields are non-readonly, so the default cleanup (reset to null) applies; pools repopulate lazily on next use.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`StableSort< T, TKey >`](#stablesortttkey) `static` `inline` | Stable-sorts the list in place according to a key selector (like OrderBy, but in-place). Items with equal keys keep their original relative ordering. |

---

<a id="stablesortttkey"></a>

### StableSort< T, TKey >

`static` `inline`

```java
static inline void StableSort< T, TKey >(this IList< T > list, Func< T, TKey > keySelector, IComparer< TKey > keyComparer = null, bool usePooling = false)
```

Stable-sorts the list in place according to a key selector (like OrderBy, but in-place). Items with equal keys keep their original relative ordering.

This is an in-place mergesort variant. For large n, it's O(n log n).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `usePooling` | `bool` | If true, uses thread-local pooled arrays to minimize GC allocations. Recommended for frequent sorting operations. Default is false for safety. |

## Private Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `Dictionary< Type, object >` | [`keyArrayPool`](#keyarraypool) `static` |  |
| `Dictionary< Type, object >` | [`elementArrayPool`](#elementarraypool) `static` |  |

---

<a id="keyarraypool"></a>

### keyArrayPool

`static`

```java
Dictionary< Type, object > keyArrayPool
```

---

<a id="elementarraypool"></a>

### elementArrayPool

`static`

```java
Dictionary< Type, object > elementArrayPool
```

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `TKey[]` | [`GetOrCreateKeyArray< TKey >`](#getorcreatekeyarraytkey) `static` `inline` |  |
| `T[]` | [`GetOrCreateElementArray< T >`](#getorcreateelementarrayt) `static` `inline` |  |
| `void` | [`MergesortInPlace< T, TKey >`](#mergesortinplacettkey) `static` `inline` |  |
| `void` | [`Merge< T, TKey >`](#mergettkey) `static` `inline` |  |

---

<a id="getorcreatekeyarraytkey"></a>

### GetOrCreateKeyArray< TKey >

`static` `inline`

```java
static inline TKey[] GetOrCreateKeyArray< TKey >(int minSize)
```

---

<a id="getorcreateelementarrayt"></a>

### GetOrCreateElementArray< T >

`static` `inline`

```java
static inline T[] GetOrCreateElementArray< T >(int minSize)
```

---

<a id="mergesortinplacettkey"></a>

### MergesortInPlace< T, TKey >

`static` `inline`

```java
static inline void MergesortInPlace< T, TKey >(IList< T > list, TKey[] keys, int start, int end, IComparer< TKey > comparer, bool usePooling)
```

---

<a id="mergettkey"></a>

### Merge< T, TKey >

`static` `inline`

```java
static inline void Merge< T, TKey >(IList< T > list, TKey[] keys, int start, int mid, int end, IComparer< TKey > comparer, bool usePooling)
```

