<a id="assets"></a>

# Assets

Loads and resolves assets by key or unique identifier.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `T` | [`GetAsset< T >`](#getassett) `static` `inline` | Loads an asset of type *T* by asset key. |
| `T` | [`GetAsset< T >`](#getassett-1) `static` `inline` | Loads an asset of type *T* by unique identifier. |
| `Material` | [`GetAsset`](#getasset) `static` `inline` | Loads a [Material](Unravel-Core-Material.md#material) by asset key and populates its properties. |
| `Material` | [`GetAsset`](#getasset-1) `static` `inline` | Loads a [Material](Unravel-Core-Material.md#material) by unique identifier and populates its properties. |

---

<a id="getassett"></a>

### GetAsset< T >

`static` `inline`

```java
static inline T GetAsset< T >(string key)
```

Loads an asset of type *T*  by asset key.

#### Returns
A new asset handle for the resolved asset.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The asset key (protocol path or project key). |

---

<a id="getassett-1"></a>

### GetAsset< T >

`static` `inline`

```java
static inline T GetAsset< T >(Guid uid)
```

Loads an asset of type *T*  by unique identifier.

#### Returns
A new asset handle for the resolved asset.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `uid` | `Guid` | The asset unique identifier. |

---

<a id="getasset"></a>

### GetAsset

`static` `inline`

```java
static inline Material GetAsset(string key)
```

Loads a [Material](Unravel-Core-Material.md#material) by asset key and populates its properties.

#### Returns
The loaded material.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The material asset key. |

---

<a id="getasset-1"></a>

### GetAsset

`static` `inline`

```java
static inline Material GetAsset(Guid uid)
```

Loads a [Material](Unravel-Core-Material.md#material) by unique identifier and populates its properties.

#### Returns
The loaded material.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `uid` | `Guid` | The material unique identifier. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `Guid` | [`internal_m2n_get_asset_by_uuid`](#internal_m2n_get_asset_by_uuid)  |  |
| `Guid` | [`internal_m2n_get_asset_by_key`](#internal_m2n_get_asset_by_key)  |  |

---

<a id="internal_m2n_get_asset_by_uuid"></a>

### internal_m2n_get_asset_by_uuid

```java
Guid internal_m2n_get_asset_by_uuid(Guid uid, Type obj)
```

---

<a id="internal_m2n_get_asset_by_key"></a>

### internal_m2n_get_asset_by_key

```java
Guid internal_m2n_get_asset_by_key(string key, Type obj)
```

