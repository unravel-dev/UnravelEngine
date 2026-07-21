<a id="layermask"></a>

# LayerMask

Represents a layer mask that can be used to include or exclude layers.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `int` | [`value`](#value)  | The raw integer value representing the layer mask. |

---

<a id="value"></a>

### value

```java
int value
```

The raw integer value representing the layer mask.

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `const int` | [`Nothing`](#nothing) `static` |  |
| `const int` | [`Everything`](#everything) `static` |  |

---

<a id="nothing"></a>

### Nothing

`static`

```java
const int Nothing = 0
```

---

<a id="everything"></a>

### Everything

`static`

```java
const int Everything = -1
```

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `implicit` | [`operator int`](#operatorint) `static` `inline` | Converts the specified [LayerMask](#layermask) to its integer representation. |
| `implicit` | [`operator LayerMask`](#operatorlayermask) `static` `inline` | Converts the specified int to a [LayerMask](#layermask). |
| `LayerMask` | [`operator+`](#operator-58) `static` `inline` | Adds the specified layer index to this mask. |
| `LayerMask` | [`operator-`](#operator-59) `static` `inline` | Removes the specified layer index from this mask. |
| `LayerMask` | [`operator+`](#operator-60) `static` `inline` | Combines two layer masks (union). |
| `LayerMask` | [`operator-`](#operator-61) `static` `inline` | Subtracts one layer mask from another (difference). |
| `string` | [`LayerToName`](#layertoname) `static` `inline` | Given a layer index, returns the name of the layer as defined in either a built-in or a user-defined layer configuration. |
| `int` | [`NameToLayer`](#nametolayer) `static` `inline` | Given a layer name, returns the corresponding layer index as defined in either a built-in or a user-defined layer configuration. |
| `int` | [`GetMask`](#getmask) `static` `inline` | Given an array of layer names, returns a layer mask containing all the layers that match those names. |

---

<a id="operatorint"></a>

### operator int

`static` `inline`

```java
static inline implicit operator int(LayerMask mask)
```

Converts the specified [LayerMask](#layermask) to its integer representation.

#### Returns
An int that represents the layer mask.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `mask` | `[LayerMask](#layermask)` | The [LayerMask](#layermask) to convert. |

---

<a id="operatorlayermask"></a>

### operator LayerMask

`static` `inline`

```java
static inline implicit operator LayerMask(int intVal)
```

Converts the specified int to a [LayerMask](#layermask).

#### Returns
A [LayerMask](#layermask) constructed from the integer value.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `intVal` | `int` | The integer value that represents a layer mask. |

---

<a id="operator-58"></a>

### operator+

`static` `inline`

```java
static inline LayerMask operator+(LayerMask mask, int layerIndex)
```

Adds the specified layer index to this mask.

#### Returns
A new [LayerMask](#layermask) including the specified layer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `mask` | `[LayerMask](#layermask)` | The original layer mask. |
| `layerIndex` | `int` | The zero-based index of the layer to add. |

---

<a id="operator-59"></a>

### operator-

`static` `inline`

```java
static inline LayerMask operator-(LayerMask mask, int layerIndex)
```

Removes the specified layer index from this mask.

#### Returns
A new [LayerMask](#layermask) excluding the specified layer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `mask` | `[LayerMask](#layermask)` | The original layer mask. |
| `layerIndex` | `int` | The zero-based index of the layer to remove. |

---

<a id="operator-60"></a>

### operator+

`static` `inline`

```java
static inline LayerMask operator+(LayerMask a, LayerMask b)
```

Combines two layer masks (union).

#### Returns
A new [LayerMask](#layermask) that is the union of the two masks.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[LayerMask](#layermask)` | First layer mask. |
| `b` | `[LayerMask](#layermask)` | Second layer mask. |

---

<a id="operator-61"></a>

### operator-

`static` `inline`

```java
static inline LayerMask operator-(LayerMask a, LayerMask b)
```

Subtracts one layer mask from another (difference).

#### Returns
A new [LayerMask](#layermask) with the layers in *b*  removed.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `a` | `[LayerMask](#layermask)` | Original layer mask. |
| `b` | `[LayerMask](#layermask)` | Mask containing layers to remove from the original mask. |

---

<a id="layertoname"></a>

### LayerToName

`static` `inline`

```java
static inline string LayerToName(int layer)
```

Given a layer index, returns the name of the layer as defined in either a built-in or a user-defined layer configuration.

#### Returns
The name of the layer, or `null` if the layer is not defined.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `layer` | `int` | The zero-based index of the layer. |

---

<a id="nametolayer"></a>

### NameToLayer

`static` `inline`

```java
static inline int NameToLayer(string layerName)
```

Given a layer name, returns the corresponding layer index as defined in either a built-in or a user-defined layer configuration.

#### Returns
The zero-based index of the layer, or `-1` if no layer with that name exists.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `layerName` | `string` | The name of the layer. |

---

<a id="getmask"></a>

### GetMask

`static` `inline`

```java
static inline int GetMask(params string[] layerNames)
```

Given an array of layer names, returns a layer mask containing all the layers that match those names.

#### Returns
An integer representing the combined layer mask for all provided layers.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `layerNames` | `params string[]` | The names of the layers to include in the mask. |

#### Exceptions

| Exception | Description |
|-----------|-------------|
| `ArgumentNullException` | Thrown when *layerNames*  is `null`. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `string` | [`internal_m2n_layers_layer_to_name`](#internal_m2n_layers_layer_to_name)  |  |
| `int` | [`internal_m2n_layers_name_to_layer`](#internal_m2n_layers_name_to_layer)  |  |

---

<a id="internal_m2n_layers_layer_to_name"></a>

### internal_m2n_layers_layer_to_name

```java
string internal_m2n_layers_layer_to_name(int layer)
```

---

<a id="internal_m2n_layers_name_to_layer"></a>

### internal_m2n_layers_name_to_layer

```java
int internal_m2n_layers_name_to_layer(string layerName)
```

