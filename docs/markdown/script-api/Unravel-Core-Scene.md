<a id="scene"></a>

# Scene

> **Extends:** [`Unravel.Core.Asset< Scene >`](Unravel-Core-Asset.md#asset)

Represents a scene in the application, providing methods to manage entities and load or destroy scenes.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`LoadScene`](#loadscene) `static` `inline` | Loads a scene by its unique key. |
| `void` | [`LoadScene`](#loadscene-1) `static` `inline` | Loads a scene using a [Scene](#scene) asset. |
| `void` | [`ReloadScene`](#reloadscene) `static` `inline` | Reloads the current scene from its source prefab asset. |
| `Entity` | [`Instantiate`](#instantiate) `static` `inline` | Instantiates an entity from a specified prefab. |
| `Entity` | [`Instantiate`](#instantiate-1) `static` `inline` | Instantiates an entity from a prefab identified by a key. |
| `Entity` | [`Instantiate`](#instantiate-2) `static` `inline` | Instantiates an entity from a specified prefab with a parent entity. |
| `Entity` | [`Instantiate`](#instantiate-3) `static` `inline` | Instantiates an entity from a prefab identified by a key with a parent entity. |
| `Entity` | [`Instantiate`](#instantiate-4) `static` `inline` | Instantiates an entity from a specified prefab at a specific position. |
| `Entity` | [`Instantiate`](#instantiate-5) `static` `inline` | Instantiates an entity from a prefab identified by a key at a specific position. |
| `Entity` | [`Instantiate`](#instantiate-6) `static` `inline` | Instantiates an entity from a specified prefab at a specific position with a parent entity. |
| `Entity` | [`Instantiate`](#instantiate-7) `static` `inline` | Instantiates an entity from a prefab identified by a key at a specific position with a parent entity. |
| `Entity` | [`Instantiate`](#instantiate-8) `static` `inline` | Instantiates an entity from a specified prefab at a specific position and rotation with a parent entity. |
| `Entity` | [`Instantiate`](#instantiate-9) `static` `inline` | Instantiates an entity from a prefab identified by a key at a specific position and rotation with a parent entity. |
| `Entity` | [`CloneEntity`](#cloneentity) `static` `inline` | Clones an existing entity. |
| `Entity` | [`CreateEntity`](#createentity) `static` `inline` | Creates a new entity with the specified name. |
| `void` | [`DestroyEntity`](#destroyentity) `static` `inline` | Destroys the specified entity after a delay. |
| `void` | [`DestroyEntityImmediate`](#destroyentityimmediate) `static` `inline` | Immediately destroys the specified entity. |
| `bool` | [`IsEntityValid`](#isentityvalid) `static` `inline` | Determines whether the specified entity is valid within the current scene. |
| `Entity` | [`FindEntityByTag`](#findentitybytag) `static` `inline` | Finds the first entity with the specified tag. |
| `Entity[]` | [`FindEntitiesByTag`](#findentitiesbytag) `static` `inline` | Finds all entities with the specified tag. |
| `Entity` | [`FindEntityByName`](#findentitybyname) `static` `inline` | Finds the first entity with the specified name. |
| `Entity[]` | [`FindEntitiesByName`](#findentitiesbyname) `static` `inline` | Finds all entities with the specified name. |
| `Entity[]` | [`FindEntitiesWithComponent< T >`](#findentitieswithcomponentt) `static` `inline` | Finds all entities that have the specified component type. |

---

<a id="loadscene"></a>

### LoadScene

`static` `inline`

```java
static inline void LoadScene(string key)
```

Loads a scene by its unique key.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the scene to load. |

---

<a id="loadscene-1"></a>

### LoadScene

`static` `inline`

```java
static inline void LoadScene(Scene scene)
```

Loads a scene using a [Scene](#scene) asset.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `scene` | `[Scene](#scene)` | The [Scene](#scene) asset to load. |

---

<a id="reloadscene"></a>

### ReloadScene

`static` `inline`

```java
static inline void ReloadScene()
```

Reloads the current scene from its source prefab asset.

---

<a id="instantiate"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(Prefab prefab)
```

Instantiates an entity from a specified prefab.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `prefab` | `[Prefab](Unravel-Core-Prefab.md#prefab)` | The prefab to instantiate. |

---

<a id="instantiate-1"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(string key)
```

Instantiates an entity from a prefab identified by a key.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the prefab to instantiate. |

---

<a id="instantiate-2"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(Prefab prefab, Entity parent)
```

Instantiates an entity from a specified prefab with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `prefab` | `[Prefab](Unravel-Core-Prefab.md#prefab)` | The prefab to instantiate. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="instantiate-3"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(string key, Entity parent)
```

Instantiates an entity from a prefab identified by a key with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the prefab to instantiate. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="instantiate-4"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(Prefab prefab, Vector3 position)
```

Instantiates an entity from a specified prefab at a specific position.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `prefab` | `[Prefab](Unravel-Core-Prefab.md#prefab)` | The prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |

---

<a id="instantiate-5"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(string key, Vector3 position)
```

Instantiates an entity from a prefab identified by a key at a specific position.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |

---

<a id="instantiate-6"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(Prefab prefab, Vector3 position, Entity parent)
```

Instantiates an entity from a specified prefab at a specific position with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `prefab` | `[Prefab](Unravel-Core-Prefab.md#prefab)` | The prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="instantiate-7"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(string key, Vector3 position, Entity parent)
```

Instantiates an entity from a prefab identified by a key at a specific position with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="instantiate-8"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(Prefab prefab, Vector3 position, Quaternion rotation, Entity parent)
```

Instantiates an entity from a specified prefab at a specific position and rotation with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `prefab` | `[Prefab](Unravel-Core-Prefab.md#prefab)` | The prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |
| `rotation` | `[Quaternion](Quaternion.md#quaternion)` | The world rotation of the entity. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="instantiate-9"></a>

### Instantiate

`static` `inline`

```java
static inline Entity Instantiate(string key, Vector3 position, Quaternion rotation, Entity parent)
```

Instantiates an entity from a prefab identified by a key at a specific position and rotation with a parent entity.

#### Returns
The instantiated entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `string` | The key identifying the prefab to instantiate. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the entity will be instantiated. |
| `rotation` | `[Quaternion](Quaternion.md#quaternion)` | The world rotation of the entity. |
| `parent` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The parent entity to attach the instantiated entity to. |

---

<a id="cloneentity"></a>

### CloneEntity

`static` `inline`

```java
static inline Entity CloneEntity(Entity e)
```

Clones an existing entity.

#### Returns
A new entity that is a clone of the specified entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `e` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity to clone. |

---

<a id="createentity"></a>

### CreateEntity

`static` `inline`

```java
static inline Entity CreateEntity(string name = "Unnamed")
```

Creates a new entity with the specified name.

#### Returns
The newly created entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | The name to assign to the new entity. Defaults to "Unnamed". |

---

<a id="destroyentity"></a>

### DestroyEntity

`static` `inline`

```java
static inline void DestroyEntity(Entity entity, float seconds = 0.0f)
```

Destroys the specified entity after a delay.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity to destroy. |
| `seconds` | `float` | The delay in seconds before destruction. Defaults to 0. |

---

<a id="destroyentityimmediate"></a>

### DestroyEntityImmediate

`static` `inline`

```java
static inline void DestroyEntityImmediate(Entity entity)
```

Immediately destroys the specified entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity to destroy. |

---

<a id="isentityvalid"></a>

### IsEntityValid

`static` `inline`

```java
static inline bool IsEntityValid(Entity entity)
```

Determines whether the specified entity is valid within the current scene.

#### Returns
`true` if the entity is valid; otherwise, `false`.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity to validate. |

---

<a id="findentitybytag"></a>

### FindEntityByTag

`static` `inline`

```java
static inline Entity FindEntityByTag(string tag)
```

Finds the first entity with the specified tag.

#### Returns
The entity with the specified tag, or `invalid` if no such entity exists.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `tag` | `string` | The tag to search for. |

---

<a id="findentitiesbytag"></a>

### FindEntitiesByTag

`static` `inline`

```java
static inline Entity[] FindEntitiesByTag(string tag)
```

Finds all entities with the specified tag.

#### Returns
The entities with the specified tag, or `empty` if no entities match.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `tag` | `string` | The tag to search for. |

---

<a id="findentitybyname"></a>

### FindEntityByName

`static` `inline`

```java
static inline Entity FindEntityByName(string name)
```

Finds the first entity with the specified name.

#### Returns
The entity with the specified name, or `invalid` if no such entity exists.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | The name to search for. |

---

<a id="findentitiesbyname"></a>

### FindEntitiesByName

`static` `inline`

```java
static inline Entity[] FindEntitiesByName(string name)
```

Finds all entities with the specified name.

#### Returns
The entities with the specified name, or `empty` if no entities match.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `string` | The name to search for. |

---

<a id="findentitieswithcomponentt"></a>

### FindEntitiesWithComponent< T >

`static` `inline`

```java
static inline Entity[] FindEntitiesWithComponent< T >()
```

Finds all entities that have the specified component type.

#### Returns
The entities that have the specified component, or empty if no entities match.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_load_scene`](#internal_m2n_load_scene)  |  |
| `void` | [`internal_m2n_load_scene_uid`](#internal_m2n_load_scene_uid)  |  |
| `void` | [`internal_m2n_reload_scene`](#internal_m2n_reload_scene)  |  |
| `void` | [`internal_m2n_create_scene`](#internal_m2n_create_scene)  |  |
| `void` | [`internal_m2n_destroy_scene`](#internal_m2n_destroy_scene)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_uid`](#internal_m2n_create_entity_from_prefab_uid)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_key`](#internal_m2n_create_entity_from_prefab_key)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_uid_with_parent`](#internal_m2n_create_entity_from_prefab_uid_with_parent)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_key_with_parent`](#internal_m2n_create_entity_from_prefab_key_with_parent)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_uid_with_position`](#internal_m2n_create_entity_from_prefab_uid_with_position)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_key_with_position`](#internal_m2n_create_entity_from_prefab_key_with_position)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_uid_with_position_parent`](#internal_m2n_create_entity_from_prefab_uid_with_position_parent)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_key_with_position_parent`](#internal_m2n_create_entity_from_prefab_key_with_position_parent)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent`](#internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent)  |  |
| `Entity` | [`internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent`](#internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent)  |  |
| `Entity` | [`internal_m2n_create_entity`](#internal_m2n_create_entity)  |  |
| `Entity` | [`internal_m2n_clone_entity`](#internal_m2n_clone_entity)  |  |
| `bool` | [`internal_m2n_destroy_entity`](#internal_m2n_destroy_entity)  |  |
| `bool` | [`internal_m2n_destroy_entity_immediate`](#internal_m2n_destroy_entity_immediate)  |  |
| `bool` | [`internal_m2n_is_entity_valid`](#internal_m2n_is_entity_valid)  |  |
| `Entity` | [`internal_m2n_find_entity_by_name`](#internal_m2n_find_entity_by_name)  |  |
| `byte[]` | [`internal_m2n_find_entities_by_name`](#internal_m2n_find_entities_by_name)  |  |
| `Entity` | [`internal_m2n_find_entity_by_tag`](#internal_m2n_find_entity_by_tag)  |  |
| `byte[]` | [`internal_m2n_find_entities_by_tag`](#internal_m2n_find_entities_by_tag)  |  |
| `byte[]` | [`internal_m2n_find_entities_with_component`](#internal_m2n_find_entities_with_component)  |  |
| `byte[]` | [`internal_m2n_find_entities_with_components`](#internal_m2n_find_entities_with_components)  |  |

---

<a id="internal_m2n_load_scene"></a>

### internal_m2n_load_scene

```java
void internal_m2n_load_scene(string key)
```

---

<a id="internal_m2n_load_scene_uid"></a>

### internal_m2n_load_scene_uid

```java
void internal_m2n_load_scene_uid(Guid uid)
```

---

<a id="internal_m2n_reload_scene"></a>

### internal_m2n_reload_scene

```java
void internal_m2n_reload_scene()
```

---

<a id="internal_m2n_create_scene"></a>

### internal_m2n_create_scene

```java
void internal_m2n_create_scene()
```

---

<a id="internal_m2n_destroy_scene"></a>

### internal_m2n_destroy_scene

```java
void internal_m2n_destroy_scene()
```

---

<a id="internal_m2n_create_entity_from_prefab_uid"></a>

### internal_m2n_create_entity_from_prefab_uid

```java
Entity internal_m2n_create_entity_from_prefab_uid(Guid uid)
```

---

<a id="internal_m2n_create_entity_from_prefab_key"></a>

### internal_m2n_create_entity_from_prefab_key

```java
Entity internal_m2n_create_entity_from_prefab_key(string key)
```

---

<a id="internal_m2n_create_entity_from_prefab_uid_with_parent"></a>

### internal_m2n_create_entity_from_prefab_uid_with_parent

```java
Entity internal_m2n_create_entity_from_prefab_uid_with_parent(Guid uid, Entity parent)
```

---

<a id="internal_m2n_create_entity_from_prefab_key_with_parent"></a>

### internal_m2n_create_entity_from_prefab_key_with_parent

```java
Entity internal_m2n_create_entity_from_prefab_key_with_parent(string key, Entity parent)
```

---

<a id="internal_m2n_create_entity_from_prefab_uid_with_position"></a>

### internal_m2n_create_entity_from_prefab_uid_with_position

```java
Entity internal_m2n_create_entity_from_prefab_uid_with_position(Guid uid, Vector3 position)
```

---

<a id="internal_m2n_create_entity_from_prefab_key_with_position"></a>

### internal_m2n_create_entity_from_prefab_key_with_position

```java
Entity internal_m2n_create_entity_from_prefab_key_with_position(string key, Vector3 position)
```

---

<a id="internal_m2n_create_entity_from_prefab_uid_with_position_parent"></a>

### internal_m2n_create_entity_from_prefab_uid_with_position_parent

```java
Entity internal_m2n_create_entity_from_prefab_uid_with_position_parent(Guid uid, Vector3 position, Entity parent)
```

---

<a id="internal_m2n_create_entity_from_prefab_key_with_position_parent"></a>

### internal_m2n_create_entity_from_prefab_key_with_position_parent

```java
Entity internal_m2n_create_entity_from_prefab_key_with_position_parent(string key, Vector3 position, Entity parent)
```

---

<a id="internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent"></a>

### internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent

```java
Entity internal_m2n_create_entity_from_prefab_uid_with_position_rotation_parent(Guid uid, Vector3 position, Quaternion rotation, Entity parent)
```

---

<a id="internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent"></a>

### internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent

```java
Entity internal_m2n_create_entity_from_prefab_key_with_position_rotation_parent(string key, Vector3 position, Quaternion rotation, Entity parent)
```

---

<a id="internal_m2n_create_entity"></a>

### internal_m2n_create_entity

```java
Entity internal_m2n_create_entity(string name)
```

---

<a id="internal_m2n_clone_entity"></a>

### internal_m2n_clone_entity

```java
Entity internal_m2n_clone_entity(Entity id)
```

---

<a id="internal_m2n_destroy_entity"></a>

### internal_m2n_destroy_entity

```java
bool internal_m2n_destroy_entity(Entity id, float seconds)
```

---

<a id="internal_m2n_destroy_entity_immediate"></a>

### internal_m2n_destroy_entity_immediate

```java
bool internal_m2n_destroy_entity_immediate(Entity id)
```

---

<a id="internal_m2n_is_entity_valid"></a>

### internal_m2n_is_entity_valid

```java
bool internal_m2n_is_entity_valid(Entity id)
```

---

<a id="internal_m2n_find_entity_by_name"></a>

### internal_m2n_find_entity_by_name

```java
Entity internal_m2n_find_entity_by_name(string name)
```

---

<a id="internal_m2n_find_entities_by_name"></a>

### internal_m2n_find_entities_by_name

```java
byte[] internal_m2n_find_entities_by_name(string name)
```

---

<a id="internal_m2n_find_entity_by_tag"></a>

### internal_m2n_find_entity_by_tag

```java
Entity internal_m2n_find_entity_by_tag(string tag)
```

---

<a id="internal_m2n_find_entities_by_tag"></a>

### internal_m2n_find_entities_by_tag

```java
byte[] internal_m2n_find_entities_by_tag(string tag)
```

---

<a id="internal_m2n_find_entities_with_component"></a>

### internal_m2n_find_entities_with_component

```java
byte[] internal_m2n_find_entities_with_component(Type componentType)
```

---

<a id="internal_m2n_find_entities_with_components"></a>

### internal_m2n_find_entities_with_components

```java
byte[] internal_m2n_find_entities_with_components(Type[] componentTypes)
```

