<a id="entity-1"></a>

# Entity

> **Extends:** `IEquatable< Entity >`, `IFormattable`

Represents an entity within a scene. Provides methods to manage components and query entity properties.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`active`](#active)  | Gets or sets the wether the entity is active. |
| `bool` | [`activeLocal`](#activelocal)  | Gets whether the entity is active locally (independent of parent hierarchy). |
| `string` | [`name`](#name-1)  | Gets or sets the name of the entity. |
| `string` | [`tag`](#tag)  | Gets or sets the tag of the entity. |
| `LayerMask` | [`layers`](#layers)  | Gets or sets the layer of the entity. |
| `TransformComponent` | [`transform`](#transform-1)  | Gets the transform component of the entity. |

---

<a id="active"></a>

### active

```java
bool active
```

Gets or sets the wether the entity is active.

The active state associated with the entity.

---

<a id="activelocal"></a>

### activeLocal

```java
bool activeLocal
```

Gets whether the entity is active locally (independent of parent hierarchy).

---

<a id="name-1"></a>

### name

```java
string name
```

Gets or sets the name of the entity.

The name associated with the entity.

---

<a id="tag"></a>

### tag

```java
string tag
```

Gets or sets the tag of the entity.

The tag associated with the entity.

---

<a id="layers"></a>

### layers

```java
LayerMask layers
```

Gets or sets the layer of the entity.

The layer associated with the entity.

---

<a id="transform-1"></a>

### transform

```java
TransformComponent transform
```

Gets the transform component of the entity.

The transform component of the entity.

## Public Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly uint` | [`Id`](#id)  | Gets the unique identifier of the entity. |

---

<a id="id"></a>

### Id

```java
readonly uint Id
```

Gets the unique identifier of the entity.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`SetActive`](#setactive) `inline` | Sets the local active state of the entity. |
| `override bool` | [`Equals`](#equals-16) `inline` |  |
| `bool` | [`Equals`](#equals-17) `inline` | Determines whether the specified entity is equal to the current entity. |
| `override int` | [`GetHashCode`](#gethashcode-9)  |  |
| `bool` | [`IsValid`](#isvalid-2) `inline` | Determines whether the entity is valid within the scene. |
| `Component` | [`AddComponent`](#addcomponent) `inline` | Adds a new component of the specified type to the entity. |
| `T` | [`AddComponent< T >`](#addcomponentt) `inline` | Adds a new component of the specified type to the entity. |
| `bool` | [`HasComponent< T >`](#hascomponentt) `inline` | Determines whether the entity has a component of the specified type. |
| `bool` | [`HasComponent`](#hascomponent) `inline` | Determines whether the entity has a component of the specified type. |
| `T` | [`GetComponent< T >`](#getcomponentt) `inline` | Gets the component of the specified type from the entity. |
| `Component` | [`GetComponent`](#getcomponent) `inline` | Gets the component of the specified type from the entity. |
| `Component[]` | [`GetComponents`](#getcomponents) `inline` | Gets all components of the specified type from the entity. |
| `T[]` | [`GetComponents< T >`](#getcomponentst) `inline` | Gets all components of the specified type from the entity. |
| `T` | [`GetComponentInChildren< T >`](#getcomponentinchildrent) `inline` | Gets the first component of the specified type from the entity or any of its children. |
| `T[]` | [`GetComponentsInChildren< T >`](#getcomponentsinchildrent) `inline` | Gets all components of the specified type from the entity and its children. |
| `Component` | [`GetComponentInChildren`](#getcomponentinchildren) `inline` | Gets the first component of the specified type from the entity or any of its children. |
| `Component[]` | [`GetComponentsInChildren`](#getcomponentsinchildren) `inline` | Gets all components of the specified type from the entity and its children. |
| `bool` | [`RemoveComponent`](#removecomponent) `inline` | Removes the specified component instance from this entity. |
| `bool` | [`RemoveComponent`](#removecomponent-1) `inline` | Removes the specified component instance from this entity after a delay. |
| `bool` | [`RemoveComponent< T >`](#removecomponentt) `inline` | Removes a component of the specified type from the entity. |
| `bool` | [`RemoveComponent< T >`](#removecomponentt-1) `inline` | Removes a component of the specified type from the entity. |
| `override string` | [`ToString`](#tostring-21) `inline` | Converts the collision to its string representation. |
| `string` | [`ToString`](#tostring-22) `inline` | Converts the collision to its string representation with a specified format. |
| `string` | [`ToString`](#tostring-23) `inline` | Converts the collision to its string representation with a specified format and format provider. |

---

<a id="setactive"></a>

### SetActive

`inline`

```java
inline void SetActive(bool active)
```

Sets the local active state of the entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `active` | `bool` | Whether the entity should be active locally. |

---

<a id="equals-16"></a>

### Equals

`inline`

```java
inline override bool Equals(object obj)
```

---

<a id="equals-17"></a>

### Equals

`inline`

```java
inline bool Equals(Entity other)
```

Determines whether the specified entity is equal to the current entity.

#### Returns
`true` if the specified entity is equal to the current entity; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `other` | `[Entity](#entity-1)` | The entity to compare with the current entity. |

---

<a id="gethashcode-9"></a>

### GetHashCode

```java
override int GetHashCode()
```

---

<a id="isvalid-2"></a>

### IsValid

`inline`

```java
inline bool IsValid()
```

Determines whether the entity is valid within the scene.

#### Returns
`true` if the entity is valid; otherwise, `false`.

---

<a id="addcomponent"></a>

### AddComponent

`inline`

```java
inline Component AddComponent(Type type)
```

Adds a new component of the specified type to the entity.

#### Returns
The newly added component.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The type of component to add. |

---

<a id="addcomponentt"></a>

### AddComponent< T >

`inline`

```java
inline T AddComponent< T >()
```

Adds a new component of the specified type to the entity.

#### Returns
The newly added component.

---

<a id="hascomponentt"></a>

### HasComponent< T >

`inline`

```java
inline bool HasComponent< T >()
```

Determines whether the entity has a component of the specified type.

#### Returns
`true` if the entity has the specified component; otherwise, `false`.

---

<a id="hascomponent"></a>

### HasComponent

`inline`

```java
inline bool HasComponent(Type type)
```

Determines whether the entity has a component of the specified type.

#### Returns
`true` if the entity has the specified component; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The type of component to check for. |

---

<a id="getcomponentt"></a>

### GetComponent< T >

`inline`

```java
inline T GetComponent< T >()
```

Gets the component of the specified type from the entity.

#### Returns
The component of the specified type.

---

<a id="getcomponent"></a>

### GetComponent

`inline`

```java
inline Component GetComponent(Type type)
```

Gets the component of the specified type from the entity.

#### Returns
The component of the specified type.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The type of component to retrieve. |

---

<a id="getcomponents"></a>

### GetComponents

`inline`

```java
inline Component[] GetComponents(Type type)
```

Gets all components of the specified type from the entity.

#### Returns
An array of components of the specified type.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The type of component to retrieve. |

---

<a id="getcomponentst"></a>

### GetComponents< T >

`inline`

```java
inline T[] GetComponents< T >()
```

Gets all components of the specified type from the entity.

#### Returns
An array of components of the specified type.

---

<a id="getcomponentinchildrent"></a>

### GetComponentInChildren< T >

`inline`

```java
inline T GetComponentInChildren< T >()
```

Gets the first component of the specified type from the entity or any of its children.

---

<a id="getcomponentsinchildrent"></a>

### GetComponentsInChildren< T >

`inline`

```java
inline T[] GetComponentsInChildren< T >()
```

Gets all components of the specified type from the entity and its children.

---

<a id="getcomponentinchildren"></a>

### GetComponentInChildren

`inline`

```java
inline Component GetComponentInChildren(Type type)
```

Gets the first component of the specified type from the entity or any of its children.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The component type to search for. |

---

<a id="getcomponentsinchildren"></a>

### GetComponentsInChildren

`inline`

```java
inline Component[] GetComponentsInChildren(Type type)
```

Gets all components of the specified type from the entity and its children.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | `Type` | The component type to search for. |

---

<a id="removecomponent"></a>

### RemoveComponent

`inline`

```java
inline bool RemoveComponent(Component component)
```

Removes the specified component instance from this entity.

#### Returns
`true` if the component was successfully removed; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `component` | `[Component](Unravel-Core-Component.md#component)` | The component instance to remove. |

---

<a id="removecomponent-1"></a>

### RemoveComponent

`inline`

```java
inline bool RemoveComponent(Component component, float secondsDelay)
```

Removes the specified component instance from this entity after a delay.

#### Returns
`true` if the delayed removal was scheduled; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `component` | `[Component](Unravel-Core-Component.md#component)` | The component instance to remove. |
| `secondsDelay` | `float` | Delay in seconds before the component is removed. |

---

<a id="removecomponentt"></a>

### RemoveComponent< T >

`inline`

```java
inline bool RemoveComponent< T >()
```

Removes a component of the specified type from the entity.

#### Returns
`true` if the component was successfully removed; otherwise, `false`.

---

<a id="removecomponentt-1"></a>

### RemoveComponent< T >

`inline`

```java
inline bool RemoveComponent< T >(float secondsDelay)
```

Removes a component of the specified type from the entity.

#### Returns
`true` if the component was successfully removed; otherwise, `false`.

---

<a id="tostring-21"></a>

### ToString

`inline`

```java
inline override string ToString()
```

Converts the collision to its string representation.

#### Returns
A string that represents the collision.

---

<a id="tostring-22"></a>

### ToString

`inline`

```java
inline string ToString(string format)
```

Converts the collision to its string representation with a specified format.

#### Returns
A string that represents the collision.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |

---

<a id="tostring-23"></a>

### ToString

`inline`

```java
inline string ToString(string format, IFormatProvider formatProvider)
```

Converts the collision to its string representation with a specified format and format provider.

#### Returns
A string that represents the collision.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `format` | `string` | The format string. |
| `formatProvider` | `IFormatProvider` | An object that supplies culture-specific formatting information. |

## Public Static Attributes

| Return | Name | Description |
|--------|------|-------------|
| `readonly Entity` | [`Invalid`](#invalid) `static` |  |

---

<a id="invalid"></a>

### Invalid

`static`

```java
readonly Entity Invalid = new (0)
```

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`operator==`](#operator-56) `static` | Compares two entities for equality. |
| `bool` | [`operator!=`](#operator-57) `static` | Compares two entities for inequality. |
| `implicit` | [`operator bool`](#operatorbool) `static` `inline` | Implicit conversion to bool for null checking. Returns true if the entity is valid. |
| `bool` | [`Remove`](#remove) `static` `inline` | Removes a specified component instance from its owner. |
| `bool` | [`Remove`](#remove-1) `static` `inline` | Removes a specified component instance from its owner. |

---

<a id="operator-56"></a>

### operator==

`static`

```java
static bool operator==(Entity lhs, Entity rhs)
```

Compares two entities for equality.

#### Returns
`true` if both entities are equal; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Entity](#entity-1)` | The first entity. |
| `rhs` | `[Entity](#entity-1)` | The second entity. |

---

<a id="operator-57"></a>

### operator!=

`static`

```java
static bool operator!=(Entity lhs, Entity rhs)
```

Compares two entities for inequality.

#### Returns
`true` if both entities are not equal; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `lhs` | `[Entity](#entity-1)` | The first entity. |
| `rhs` | `[Entity](#entity-1)` | The second entity. |

---

<a id="operatorbool"></a>

### operator bool

`static` `inline`

```java
static inline implicit operator bool(Entity entity)
```

Implicit conversion to bool for null checking. Returns true if the entity is valid.

#### Returns
`true` if the entity is valid; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](#entity-1)` | The entity to check. |

---

<a id="remove"></a>

### Remove

`static` `inline`

```java
static inline bool Remove(Component component)
```

Removes a specified component instance from its owner.

#### Returns
`true` if the component was successfully removed; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `component` | `[Component](Unravel-Core-Component.md#component)` | The component instance to remove. |

---

<a id="remove-1"></a>

### Remove

`static` `inline`

```java
static inline bool Remove(Component component, float secondsDelay)
```

Removes a specified component instance from its owner.

#### Returns
`true` if the component was successfully removed; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `component` | `[Component](Unravel-Core-Component.md#component)` | The component instance to remove. |

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_get_active_global`](#internal_m2n_get_active_global)  |  |
| `bool` | [`internal_m2n_get_active_local`](#internal_m2n_get_active_local)  |  |
| `void` | [`internal_m2n_set_active_local`](#internal_m2n_set_active_local)  |  |
| `string` | [`internal_m2n_get_name`](#internal_m2n_get_name)  |  |
| `void` | [`internal_m2n_set_name`](#internal_m2n_set_name)  |  |
| `string` | [`internal_m2n_get_tag`](#internal_m2n_get_tag)  |  |
| `void` | [`internal_m2n_set_tag`](#internal_m2n_set_tag)  |  |
| `LayerMask` | [`internal_m2n_get_layers`](#internal_m2n_get_layers)  |  |
| `void` | [`internal_m2n_set_layers`](#internal_m2n_set_layers)  |  |
| `Component` | [`internal_m2n_add_component`](#internal_m2n_add_component)  |  |
| `Component` | [`internal_m2n_get_component`](#internal_m2n_get_component)  |  |
| `Array` | [`internal_m2n_get_components`](#internal_m2n_get_components)  |  |
| `Component` | [`internal_m2n_get_component_in_children`](#internal_m2n_get_component_in_children)  |  |
| `Array` | [`internal_m2n_get_components_in_children`](#internal_m2n_get_components_in_children)  |  |
| `bool` | [`internal_m2n_has_component`](#internal_m2n_has_component)  |  |
| `bool` | [`internal_m2n_remove_component_instance`](#internal_m2n_remove_component_instance)  |  |
| `bool` | [`internal_m2n_remove_component_instance_delay`](#internal_m2n_remove_component_instance_delay)  |  |
| `bool` | [`internal_m2n_remove_component`](#internal_m2n_remove_component)  |  |
| `bool` | [`internal_m2n_remove_component_delay`](#internal_m2n_remove_component_delay)  |  |
| `Component` | [`internal_m2n_get_transform_component`](#internal_m2n_get_transform_component)  |  |

---

<a id="internal_m2n_get_active_global"></a>

### internal_m2n_get_active_global

```java
bool internal_m2n_get_active_global(Entity id)
```

---

<a id="internal_m2n_get_active_local"></a>

### internal_m2n_get_active_local

```java
bool internal_m2n_get_active_local(Entity id)
```

---

<a id="internal_m2n_set_active_local"></a>

### internal_m2n_set_active_local

```java
void internal_m2n_set_active_local(Entity id, bool active)
```

---

<a id="internal_m2n_get_name"></a>

### internal_m2n_get_name

```java
string internal_m2n_get_name(Entity id)
```

---

<a id="internal_m2n_set_name"></a>

### internal_m2n_set_name

```java
void internal_m2n_set_name(Entity id, string name)
```

---

<a id="internal_m2n_get_tag"></a>

### internal_m2n_get_tag

```java
string internal_m2n_get_tag(Entity id)
```

---

<a id="internal_m2n_set_tag"></a>

### internal_m2n_set_tag

```java
void internal_m2n_set_tag(Entity id, string tag)
```

---

<a id="internal_m2n_get_layers"></a>

### internal_m2n_get_layers

```java
LayerMask internal_m2n_get_layers(Entity id)
```

---

<a id="internal_m2n_set_layers"></a>

### internal_m2n_set_layers

```java
void internal_m2n_set_layers(Entity id, LayerMask layers)
```

---

<a id="internal_m2n_add_component"></a>

### internal_m2n_add_component

```java
Component internal_m2n_add_component(Entity id, Type obj)
```

---

<a id="internal_m2n_get_component"></a>

### internal_m2n_get_component

```java
Component internal_m2n_get_component(Entity id, Type obj)
```

---

<a id="internal_m2n_get_components"></a>

### internal_m2n_get_components

```java
Array internal_m2n_get_components(Entity id, Type obj)
```

---

<a id="internal_m2n_get_component_in_children"></a>

### internal_m2n_get_component_in_children

```java
Component internal_m2n_get_component_in_children(Entity id, Type obj)
```

---

<a id="internal_m2n_get_components_in_children"></a>

### internal_m2n_get_components_in_children

```java
Array internal_m2n_get_components_in_children(Entity id, Type obj)
```

---

<a id="internal_m2n_has_component"></a>

### internal_m2n_has_component

```java
bool internal_m2n_has_component(Entity id, Type obj)
```

---

<a id="internal_m2n_remove_component_instance"></a>

### internal_m2n_remove_component_instance

```java
bool internal_m2n_remove_component_instance(Entity id, Component obj)
```

---

<a id="internal_m2n_remove_component_instance_delay"></a>

### internal_m2n_remove_component_instance_delay

```java
bool internal_m2n_remove_component_instance_delay(Entity id, Component obj, float secondsDelay)
```

---

<a id="internal_m2n_remove_component"></a>

### internal_m2n_remove_component

```java
bool internal_m2n_remove_component(Entity id, Type obj)
```

---

<a id="internal_m2n_remove_component_delay"></a>

### internal_m2n_remove_component_delay

```java
bool internal_m2n_remove_component_delay(Entity id, Type obj, float secondsDelay)
```

---

<a id="internal_m2n_get_transform_component"></a>

### internal_m2n_get_transform_component

```java
Component internal_m2n_get_transform_component(Entity id, Type obj)
```

