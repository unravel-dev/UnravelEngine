<a id="bytearrayextensions"></a>

# ByteArrayExtensions

Extension methods for converting byte arrays to blittable structs.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `T` | [`ToStruct< T >`](#tostructt) `static` `inline` | Converts a byte array to a single struct of type *T* . |
| `T[]` | [`ToStructArray< T >`](#tostructarrayt) `static` `inline` | Converts a byte array to an array of structs of type *T* . |

---

<a id="tostructt"></a>

### ToStruct< T >

`static` `inline`

```java
static inline T ToStruct< T >(this byte[] data)
```

Converts a byte array to a single struct of type *T* .

---

<a id="tostructarrayt"></a>

### ToStructArray< T >

`static` `inline`

```java
static inline T[] ToStructArray< T >(this byte[] data)
```

Converts a byte array to an array of structs of type *T* .

## Private Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `T` | [`ToStructImpl< T >`](#tostructimplt) `static` `inline` |  |

---

<a id="tostructimplt"></a>

### ToStructImpl< T >

`static` `inline`

```java
static inline T ToStructImpl< T >(byte[] data, int offset, int size)
```

