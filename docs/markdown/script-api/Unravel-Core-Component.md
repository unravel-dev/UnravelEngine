<a id="component"></a>

# Component

> **Extends:** [`Unravel.Core.NativeObject`](Unravel-Core-NativeObject.md#nativeobject)
> **Subclassed by:** [`Unravel.Core.AnimationComponent`](Unravel-Core-AnimationComponent.md#animationcomponent), [`Unravel.Core.AudioListenerComponent`](Unravel-Core-AudioListenerComponent.md#audiolistenercomponent), [`Unravel.Core.AudioSourceComponent`](Unravel-Core-AudioSourceComponent.md#audiosourcecomponent), [`Unravel.Core.BoneComponent`](Unravel-Core-BoneComponent.md#bonecomponent), [`Unravel.Core.CameraComponent`](Unravel-Core-CameraComponent.md#cameracomponent), [`Unravel.Core.CharacterControllerComponent`](Unravel-Core-CharacterControllerComponent.md#charactercontrollercomponent), [`Unravel.Core.IdComponent`](Unravel-Core-IdComponent.md#idcomponent), [`Unravel.Core.LightComponent`](Unravel-Core-LightComponent.md#lightcomponent), [`Unravel.Core.ModelComponent`](Unravel-Core-ModelComponent.md#modelcomponent), [`Unravel.Core.ParticleEmitterComponent`](Unravel-Core-ParticleEmitterComponent.md#particleemittercomponent), [`Unravel.Core.PhysicsComponent`](Unravel-Core-PhysicsComponent.md#physicscomponent), [`Unravel.Core.ReflectionProbeComponent`](Unravel-Core-ReflectionProbeComponent.md#reflectionprobecomponent), [`Unravel.Core.ScriptComponent`](Unravel-Core-ScriptComponent.md#scriptcomponent), [`Unravel.Core.TextComponent`](Unravel-Core-TextComponent.md#textcomponent), [`Unravel.Core.TransformComponent`](Unravel-Core-TransformComponent.md#transformcomponent), [`Unravel.Core.UIDocumentComponent`](Unravel-Core-UIDocumentComponent.md#uidocumentcomponent)

Represents a base class for all components in the entity-component system.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `Entity` | [`owner`](#owner)  | Gets the entity that owns this component. |
| `TransformComponent` | [`transform`](#transform)  | Gets the [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent) associated with this component's owner. |

---

<a id="owner"></a>

### owner

```java
Entity owner
```

Gets the entity that owns this component.

---

<a id="transform"></a>

### transform

```java
TransformComponent transform
```

Gets the [TransformComponent](Unravel-Core-TransformComponent.md#transformcomponent) associated with this component's owner.

The transform is lazily loaded and cached for performance. The cache is updated if the owner entity is reassigned.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `override bool` | [`IsValid`](#isvalid-1) `inline` | Determines whether this component is valid. |

---

<a id="isvalid-1"></a>

### IsValid

`inline`

```java
inline override bool IsValid()
```

Determines whether this component is valid.

#### Returns
`true` if the owner entity is valid and this component exists on the owner; otherwise, `false`.

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`cacheDirty`](#cachedirty)  |  |
| `TransformComponent` | [`cachedTransform`](#cachedtransform)  |  |

---

<a id="cachedirty"></a>

### cacheDirty

```java
bool cacheDirty = true
```

---

<a id="cachedtransform"></a>

### cachedTransform

```java
TransformComponent cachedTransform = null
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_n2m_set_entity`](#internal_n2m_set_entity) `inline` | Sets the owning entity of this component. |

---

<a id="internal_n2m_set_entity"></a>

### internal_n2m_set_entity

`inline`

```java
inline void internal_n2m_set_entity(uint id)
```

Sets the owning entity of this component.

This method is called internally and is not intended for direct use. It updates the owner entity and invalidates the cached transform.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `uint` | The unique identifier of the entity to associate with this component. |

