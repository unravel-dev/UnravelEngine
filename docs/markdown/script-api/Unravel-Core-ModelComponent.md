<a id="modelcomponent"></a>

# ModelComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Represents a component that provides model rendering capabilities for an entity.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`enabled`](#enabled)  | Gets or sets a value indicating whether the model is enabled. |

---

<a id="enabled"></a>

### enabled

```java
bool enabled
```

Gets or sets a value indicating whether the model is enabled.

`true` if the model is enabled; otherwise, `false`.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `int` | [`GetSharedMaterialsCount`](#getsharedmaterialscount) `inline` | Gets the number of shared (asset-backed) materials on the model. |
| `Material` | [`GetSharedMaterial`](#getsharedmaterial) `inline` | Retrieves the material assigned to a specific index in the model. |
| `int` | [`GetMaterialsCount`](#getmaterialscount) `inline` | Gets the number of material instances on the model. |
| `Material` | [`GetMaterial`](#getmaterial) `inline` | Retrieves the material instance assigned to a specific index in the model. |
| `void` | [`SetSharedMaterial`](#setsharedmaterial) `inline` | Sets the material at the specified index for the model. |
| `void` | [`SetMaterial`](#setmaterial) `inline` | Sets the material instance at the specified index for the model. |
| `void` | [`ResetMaterials`](#resetmaterials) `inline` | Clears all material instances so the model uses its shared materials again. |
| `void` | [`SetColor`](#setcolor) `inline` | Sets the first material instance color for the model. |
| `void` | [`SetColor`](#setcolor-1) `inline` | Sets the material instance color at the specified index for the model. |
| `Color` | [`GetColor`](#getcolor) `inline` | Gets the first material instance color for the model. |
| `Color` | [`GetColor`](#getcolor-1) `inline` | Gets the material instance color for the model at the specified index. |

---

<a id="getsharedmaterialscount"></a>

### GetSharedMaterialsCount

`inline`

```java
inline int GetSharedMaterialsCount()
```

Gets the number of shared (asset-backed) materials on the model.

---

<a id="getsharedmaterial"></a>

### GetSharedMaterial

`inline`

```java
inline Material GetSharedMaterial(uint index = 0)
```

Retrieves the material assigned to a specific index in the model.

#### Returns
The [Material](Unravel-Core-Material.md#material) assigned to the specified index, or `null` if no material is assigned.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | `uint` | The index of the material to retrieve. |

---

<a id="getmaterialscount"></a>

### GetMaterialsCount

`inline`

```java
inline int GetMaterialsCount()
```

Gets the number of material instances on the model.

---

<a id="getmaterial"></a>

### GetMaterial

`inline`

```java
inline Material GetMaterial(uint index = 0)
```

Retrieves the material instance assigned to a specific index in the model.

#### Returns
The [Material](Unravel-Core-Material.md#material) assigned to the specified index, or `null` if no material is assigned.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `index` | `uint` | The index of the material instance to retrieve. |

---

<a id="setsharedmaterial"></a>

### SetSharedMaterial

`inline`

```java
inline void SetSharedMaterial(Material material, uint index = 0)
```

Sets the material at the specified index for the model.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `material` | `[Material](Unravel-Core-Material.md#material)` | The [Material](Unravel-Core-Material.md#material) to assign, or `null` to remove the material. |
| `index` | `uint` | The index of the material to set. |

---

<a id="setmaterial"></a>

### SetMaterial

`inline`

```java
inline void SetMaterial(Material material, uint index = 0)
```

Sets the material instance at the specified index for the model.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `material` | `[Material](Unravel-Core-Material.md#material)` | The [Material](Unravel-Core-Material.md#material) to assign, or `null` to remove the material. |
| `index` | `uint` | The index of the material to set. |

---

<a id="resetmaterials"></a>

### ResetMaterials

`inline`

```java
inline void ResetMaterials()
```

Clears all material instances so the model uses its shared materials again.

---

<a id="setcolor"></a>

### SetColor

`inline`

```java
inline void SetColor(Color color)
```

Sets the first material instance color for the model.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `color` | `[Color](Color.md#color)` | The [Color](Color.md#color) to assign. |

---

<a id="setcolor-1"></a>

### SetColor

`inline`

```java
inline void SetColor(Color color, uint index)
```

Sets the material instance color at the specified index for the model.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `color` | `[Color](Color.md#color)` | The [Color](Color.md#color) to assign. |
| `index` | `uint` | The index of the material to assign. |

---

<a id="getcolor"></a>

### GetColor

`inline`

```java
inline Color GetColor()
```

Gets the first material instance color for the model.

#### Returns
The [Color](Color.md#color) assigned to the specified index, or `white` if no material is assigned. 

<br/>

---

<a id="getcolor-1"></a>

### GetColor

`inline`

```java
inline Color GetColor(uint index)
```

Gets the material instance color for the model at the specified index.

#### Returns
The [Color](Color.md#color) assigned to the specified index, or `white` if no material is assigned. 

<br/>

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_model_get_enabled`](#internal_m2n_model_get_enabled)  |  |
| `void` | [`internal_m2n_model_set_enabled`](#internal_m2n_model_set_enabled)  |  |
| `Guid` | [`internal_m2n_model_get_shared_material`](#internal_m2n_model_get_shared_material)  |  |
| `int` | [`internal_m2n_model_get_shared_material_count`](#internal_m2n_model_get_shared_material_count)  |  |
| `void` | [`internal_m2n_model_set_shared_material`](#internal_m2n_model_set_shared_material)  |  |
| `void` | [`internal_m2n_model_set_material_instance`](#internal_m2n_model_set_material_instance)  |  |
| `MaterialProperties` | [`internal_m2n_model_get_material_instance`](#internal_m2n_model_get_material_instance)  |  |
| `int` | [`internal_m2n_model_get_material_instance_count`](#internal_m2n_model_get_material_instance_count)  |  |

---

<a id="internal_m2n_model_get_enabled"></a>

### internal_m2n_model_get_enabled

```java
bool internal_m2n_model_get_enabled(Entity eid)
```

---

<a id="internal_m2n_model_set_enabled"></a>

### internal_m2n_model_set_enabled

```java
void internal_m2n_model_set_enabled(Entity eid, bool enabled)
```

---

<a id="internal_m2n_model_get_shared_material"></a>

### internal_m2n_model_get_shared_material

```java
Guid internal_m2n_model_get_shared_material(Entity eid, uint index)
```

---

<a id="internal_m2n_model_get_shared_material_count"></a>

### internal_m2n_model_get_shared_material_count

```java
int internal_m2n_model_get_shared_material_count(Entity eid)
```

---

<a id="internal_m2n_model_set_shared_material"></a>

### internal_m2n_model_set_shared_material

```java
void internal_m2n_model_set_shared_material(Entity eid, Guid guid, uint index)
```

---

<a id="internal_m2n_model_set_material_instance"></a>

### internal_m2n_model_set_material_instance

```java
void internal_m2n_model_set_material_instance(Entity eid, MaterialProperties props, uint index)
```

---

<a id="internal_m2n_model_get_material_instance"></a>

### internal_m2n_model_get_material_instance

```java
MaterialProperties internal_m2n_model_get_material_instance(Entity eid, uint index)
```

---

<a id="internal_m2n_model_get_material_instance_count"></a>

### internal_m2n_model_get_material_instance_count

```java
int internal_m2n_model_get_material_instance_count(Entity eid)
```

