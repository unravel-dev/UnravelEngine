<a id="animationcomponent"></a>

# AnimationComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Provides functionality to manage and blend animation clips on an entity.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `float` | [`Speed`](#speed)  | The speed of the animation. 1.0 is normal speed, 2.0 is double speed, 0.5 is half speed. |

---

<a id="speed"></a>

### Speed

```java
float Speed
```

The speed of the animation. 1.0 is normal speed, 2.0 is double speed, 0.5 is half speed.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Blend`](#blend) `inline` | Blends the specified animation clip into the default layer. |
| `void` | [`BlendLayer`](#blendlayer) `inline` | Blends the specified animation clip into the given layer. |
| `void` | [`Play`](#play) `inline` | Starts playing the currently blended animation on the entity. |
| `void` | [`Pause`](#pause) `inline` | Pauses the currently playing animation on the entity. |
| `void` | [`Resume`](#resume) `inline` | Resumes the currently paused animation on the entity. |
| `void` | [`Stop`](#stop) `inline` | Stops the currently playing animation on the entity. |

---

<a id="blend"></a>

### Blend

`inline`

```java
inline void Blend(AnimationClip clip, float seconds, bool loop, bool phaseSync)
```

Blends the specified animation clip into the default layer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `clip` | `[AnimationClip](Unravel-Core-AnimationClip.md#animationclip)` | The animation clip to blend in. |
| `seconds` | `float` | The duration of the blend transition, in seconds. |
| `loop` | `bool` | Whether the animation should loop after playing. |
| `phaseSync` | `bool` | Whether to synchronize the phase of the new clip with the current animation. |

---

<a id="blendlayer"></a>

### BlendLayer

`inline`

```java
inline void BlendLayer(int layer, AnimationClip clip, float seconds, bool loop, bool phaseSync)
```

Blends the specified animation clip into the given layer.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `layer` | `int` | The animation layer index to blend into. |
| `clip` | `[AnimationClip](Unravel-Core-AnimationClip.md#animationclip)` | The animation clip to blend in. |
| `seconds` | `float` | The duration of the blend transition, in seconds. |
| `loop` | `bool` | Whether the animation should loop after playing. |
| `phaseSync` | `bool` | Whether to synchronize the phase of the new clip with the current animation. |

---

<a id="play"></a>

### Play

`inline`

```java
inline void Play()
```

Starts playing the currently blended animation on the entity.

---

<a id="pause"></a>

### Pause

`inline`

```java
inline void Pause()
```

Pauses the currently playing animation on the entity.

---

<a id="resume"></a>

### Resume

`inline`

```java
inline void Resume()
```

Resumes the currently paused animation on the entity.

---

<a id="stop"></a>

### Stop

`inline`

```java
inline void Stop()
```

Stops the currently playing animation on the entity.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`internal_m2n_animation_blend`](#internal_m2n_animation_blend)  |  |
| `void` | [`internal_m2n_animation_play`](#internal_m2n_animation_play)  |  |
| `void` | [`internal_m2n_animation_pause`](#internal_m2n_animation_pause)  |  |
| `void` | [`internal_m2n_animation_resume`](#internal_m2n_animation_resume)  |  |
| `void` | [`internal_m2n_animation_stop`](#internal_m2n_animation_stop)  |  |
| `void` | [`internal_m2n_animation_set_speed`](#internal_m2n_animation_set_speed)  |  |
| `float` | [`internal_m2n_animation_get_speed`](#internal_m2n_animation_get_speed)  |  |

---

<a id="internal_m2n_animation_blend"></a>

### internal_m2n_animation_blend

```java
void internal_m2n_animation_blend(Entity eid, int layer, Guid guid, float seconds, bool loop, bool phaseSync)
```

---

<a id="internal_m2n_animation_play"></a>

### internal_m2n_animation_play

```java
void internal_m2n_animation_play(Entity eid)
```

---

<a id="internal_m2n_animation_pause"></a>

### internal_m2n_animation_pause

```java
void internal_m2n_animation_pause(Entity eid)
```

---

<a id="internal_m2n_animation_resume"></a>

### internal_m2n_animation_resume

```java
void internal_m2n_animation_resume(Entity eid)
```

---

<a id="internal_m2n_animation_stop"></a>

### internal_m2n_animation_stop

```java
void internal_m2n_animation_stop(Entity eid)
```

---

<a id="internal_m2n_animation_set_speed"></a>

### internal_m2n_animation_set_speed

```java
void internal_m2n_animation_set_speed(Entity eid, float speed)
```

---

<a id="internal_m2n_animation_get_speed"></a>

### internal_m2n_animation_get_speed

```java
float internal_m2n_animation_get_speed(Entity eid)
```

