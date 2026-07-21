<a id="scriptcomponent"></a>

# ScriptComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)
> **Subclassed by:** [`SampleCharacterController`](SampleCharacterController.md#samplecharactercontroller), [`SampleMouseLookComponent`](SampleMouseLookComponent.md#samplemouselookcomponent), [`SampleOrbitComponent`](SampleOrbitComponent.md#sampleorbitcomponent), [`Unravel.Samples.SampleUIWrapperController`](Unravel-Samples-SampleUIWrapperController.md#sampleuiwrappercontroller)

Represents a script component that provides lifecycle hooks and event handling for an entity.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `string` | [`SourceFilePath`](#sourcefilepath)  |  |

---

<a id="sourcefilepath"></a>

### SourceFilePath

```java
string SourceFilePath
```

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`OnCreate`](#oncreate) `virtual` `inline` | Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate. |
| `void` | [`OnEnable`](#onenable) `virtual` `inline` | Called when the script or entity is enabled. Override this method to initialize resources or data. |
| `void` | [`OnDisable`](#ondisable) `virtual` `inline` | Called when the script or entity is disabled. Override this method to clean up resources or data. |
| `void` | [`OnStart`](#onstart) `virtual` `inline` | Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled. |
| `void` | [`OnDestroy`](#ondestroy) `virtual` `inline` | Called when the script is destroyed. Override this method to clean up resources or data. |
| `void` | [`OnSensorEnter`](#onsensorenter) `virtual` `inline` | Called when another entity enters a sensor attached to this entity. |
| `void` | [`OnSensorExit`](#onsensorexit) `virtual` `inline` | Called when another entity exits a sensor attached to this entity. |
| `void` | [`OnCollisionEnter`](#oncollisionenter) `virtual` `inline` | Called when this entity begins a collision with another entity. |
| `void` | [`OnCollisionExit`](#oncollisionexit) `virtual` `inline` | Called when this entity ends a collision with another entity. |
| `void` | [`OnUpdate`](#onupdate) `virtual` `inline` | Called on every frame update. Override this method to implement frame-based logic. |
| `void` | [`OnFixedUpdate`](#onfixedupdate) `virtual` `inline` | A framerate-idependent interval update when physics calculations are performed. Override this method to implement frame-based logic. |
| `void` | [`OnLateUpdate`](#onlateupdate) `virtual` `inline` | Called on every frame update after other updates are finished. Override this method to implement frame-based logic. |

---

<a id="oncreate"></a>

### OnCreate

`virtual` `inline`

```java
virtual inline void OnCreate()
```

Called when the script is created. Override this method to initialize resources or data. OnCreate is called when the script is first loaded, or when an object it is attached to is instantiated It only gets called once on each script, and only after other objects are initialised. This means that it is safe to create references to other game objects and components in OnCreate.

---

<a id="onenable"></a>

### OnEnable

`virtual` `inline`

```java
virtual inline void OnEnable()
```

Called when the script or entity is enabled. Override this method to initialize resources or data.

---

<a id="ondisable"></a>

### OnDisable

`virtual` `inline`

```java
virtual inline void OnDisable()
```

Called when the script or entity is disabled. Override this method to clean up resources or data.

---

<a id="onstart"></a>

### OnStart

`virtual` `inline`

```java
virtual inline void OnStart()
```

Called when the script starts execution. Override this method to set up logic at the start. Start is called once, before any Update methods and after OnCreate. It works in much the same way as OnCreate, with a few key differences. Unlike OnCreate, Start will not be called if the script is disabled.

---

<a id="ondestroy"></a>

### OnDestroy

`virtual` `inline`

```java
virtual inline void OnDestroy()
```

Called when the script is destroyed. Override this method to clean up resources or data.

---

<a id="onsensorenter"></a>

### OnSensorEnter

`virtual` `inline`

```java
virtual inline void OnSensorEnter(Collision collision)
```

Called when another entity enters a sensor attached to this entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `collision` | `[Collision](Unravel-Core-Collision.md#collision)` | Details of the collision, including the other entity and contact points. |

---

<a id="onsensorexit"></a>

### OnSensorExit

`virtual` `inline`

```java
virtual inline void OnSensorExit(Collision collision)
```

Called when another entity exits a sensor attached to this entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `collision` | `[Collision](Unravel-Core-Collision.md#collision)` | Details of the collision, including the other entity and contact points. |

---

<a id="oncollisionenter"></a>

### OnCollisionEnter

`virtual` `inline`

```java
virtual inline void OnCollisionEnter(Collision collision)
```

Called when this entity begins a collision with another entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `collision` | `[Collision](Unravel-Core-Collision.md#collision)` | Details of the collision, including the other entity and contact points. |

---

<a id="oncollisionexit"></a>

### OnCollisionExit

`virtual` `inline`

```java
virtual inline void OnCollisionExit(Collision collision)
```

Called when this entity ends a collision with another entity.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `collision` | `[Collision](Unravel-Core-Collision.md#collision)` | Details of the collision, including the other entity and contact points. |

---

<a id="onupdate"></a>

### OnUpdate

`virtual` `inline`

```java
virtual inline void OnUpdate()
```

Called on every frame update. Override this method to implement frame-based logic.

---

<a id="onfixedupdate"></a>

### OnFixedUpdate

`virtual` `inline`

```java
virtual inline void OnFixedUpdate()
```

A framerate-idependent interval update when physics calculations are performed. Override this method to implement frame-based logic.

---

<a id="onlateupdate"></a>

### OnLateUpdate

`virtual` `inline`

```java
virtual inline void OnLateUpdate()
```

Called on every frame update after other updates are finished. Override this method to implement frame-based logic.

## Protected Methods

| Return | Name | Description |
|--------|------|-------------|
|  | [`ScriptComponent`](#scriptcomponent-1) `inline` |  |

---

<a id="scriptcomponent-1"></a>

### ScriptComponent

`inline`

```java
inline ScriptComponent(string file = "")
```

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`m_started`](#m_started)  |  |

---

<a id="m_started"></a>

### m_started

```java
bool m_started = false
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_n2m_on_create`](#internal_n2m_on_create) `inline` | Internal method invoked when the script is created. Calls [OnCreate](#oncreate) and subscribes [OnUpdate](#onupdate) to the update system. |
| `void` | [`internal_n2m_on_enable`](#internal_n2m_on_enable) `inline` | Internal method invoked when the script is enabled. Calls [OnEnable](#onenable). |
| `void` | [`internal_n2m_on_disable`](#internal_n2m_on_disable) `inline` | Internal method invoked when the script is disabled. Calls [OnDisable](#ondisable). |
| `void` | [`internal_n2m_on_start`](#internal_n2m_on_start) `inline` | Internal method invoked when the script starts. Calls [OnStart](#onstart). |
| `void` | [`internal_n2m_on_destroy`](#internal_n2m_on_destroy) `inline` | Internal method invoked when the script is destroyed. Calls [OnDestroy](#ondestroy) and unsubscribes [OnUpdate](#onupdate) from the update system. |
| `void` | [`internal_n2m_on_sensor_enter`](#internal_n2m_on_sensor_enter) `inline` | Internal method invoked when another entity enters a sensor attached to this entity. Calls [OnSensorEnter](#onsensorenter). |
| `void` | [`internal_n2m_on_sensor_exit`](#internal_n2m_on_sensor_exit) `inline` | Internal method invoked when another entity exits a sensor attached to this entity. Calls [OnSensorExit](#onsensorexit). |
| `void` | [`internal_n2m_on_collision_enter`](#internal_n2m_on_collision_enter) `inline` | Internal method invoked when this entity begins a collision. Converts contact data and calls [OnCollisionEnter](#oncollisionenter). |
| `void` | [`internal_n2m_on_collision_exit`](#internal_n2m_on_collision_exit) `inline` | Internal method invoked when this entity ends a collision. Converts contact data and calls [OnCollisionExit](#oncollisionexit). |

---

<a id="internal_n2m_on_create"></a>

### internal_n2m_on_create

`inline`

```java
inline void internal_n2m_on_create()
```

Internal method invoked when the script is created. Calls [OnCreate](#oncreate) and subscribes [OnUpdate](#onupdate) to the update system.

---

<a id="internal_n2m_on_enable"></a>

### internal_n2m_on_enable

`inline`

```java
inline void internal_n2m_on_enable()
```

Internal method invoked when the script is enabled. Calls [OnEnable](#onenable).

---

<a id="internal_n2m_on_disable"></a>

### internal_n2m_on_disable

`inline`

```java
inline void internal_n2m_on_disable()
```

Internal method invoked when the script is disabled. Calls [OnDisable](#ondisable).

---

<a id="internal_n2m_on_start"></a>

### internal_n2m_on_start

`inline`

```java
inline void internal_n2m_on_start()
```

Internal method invoked when the script starts. Calls [OnStart](#onstart).

---

<a id="internal_n2m_on_destroy"></a>

### internal_n2m_on_destroy

`inline`

```java
inline void internal_n2m_on_destroy()
```

Internal method invoked when the script is destroyed. Calls [OnDestroy](#ondestroy) and unsubscribes [OnUpdate](#onupdate) from the update system.

---

<a id="internal_n2m_on_sensor_enter"></a>

### internal_n2m_on_sensor_enter

`inline`

```java
inline void internal_n2m_on_sensor_enter(Entity entity, byte[] contactData)
```

Internal method invoked when another entity enters a sensor attached to this entity. Calls [OnSensorEnter](#onsensorenter).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity that entered the sensor. |
| `contactData` | `byte[]` | The serialized contact data for the sensor. |

---

<a id="internal_n2m_on_sensor_exit"></a>

### internal_n2m_on_sensor_exit

`inline`

```java
inline void internal_n2m_on_sensor_exit(Entity entity, byte[] contactData)
```

Internal method invoked when another entity exits a sensor attached to this entity. Calls [OnSensorExit](#onsensorexit).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The entity that exited the sensor. |

---

<a id="internal_n2m_on_collision_enter"></a>

### internal_n2m_on_collision_enter

`inline`

```java
inline void internal_n2m_on_collision_enter(Entity entity, byte[] contactData)
```

Internal method invoked when this entity begins a collision. Converts contact data and calls [OnCollisionEnter](#oncollisionenter).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The other entity involved in the collision. |
| `contactData` | `byte[]` | The serialized contact data for the collision. |

---

<a id="internal_n2m_on_collision_exit"></a>

### internal_n2m_on_collision_exit

`inline`

```java
inline void internal_n2m_on_collision_exit(Entity entity, byte[] contactData)
```

Internal method invoked when this entity ends a collision. Converts contact data and calls [OnCollisionExit](#oncollisionexit).

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `[Entity](Unravel-Core-Entity.md#entity-1)` | The other entity involved in the collision. |
| `contactData` | `byte[]` | The serialized contact data for the collision. |

