<a id="audiosourcecomponent"></a>

# AudioSourceComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Represents an audio source component that can play, pause, and control audio playback in a scene.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `AudioClip` | [`clip`](#clip)  | Gets or sets the audio clip currently assigned to the audio source. |
| `bool` | [`loop`](#loop)  | Gets or sets a value indicating whether the audio source loops playback. |
| `float` | [`volume`](#volume)  | Gets or sets the volume of the audio source. |
| `float` | [`pitch`](#pitch)  | Gets or sets the pitch of the audio source. |
| `float` | [`volumeRolloff`](#volumerolloff)  | Gets or sets the volume rolloff factor of the audio source. |
| `float` | [`minDistance`](#mindistance)  | Gets or sets the minimum distance for the audio source. |
| `float` | [`maxDistance`](#maxdistance)  | Gets or sets the maximum distance for the audio source. |
| `bool` | [`mute`](#mute)  | Gets or sets a value indicating whether the audio source is muted. |
| `bool` | [`isPlaying`](#isplaying)  | Gets a value indicating whether the audio source is currently playing. |
| `bool` | [`isPaused`](#ispaused)  | Gets a value indicating whether the audio source is currently paused. |
| `float` | [`time`](#time)  | Gets or sets the current playback time of the audio source. |

---

<a id="clip"></a>

### clip

```java
AudioClip clip
```

Gets or sets the audio clip currently assigned to the audio source.

The currently assigned audio clip.

---

<a id="loop"></a>

### loop

```java
bool loop
```

Gets or sets a value indicating whether the audio source loops playback.

`true` if the audio source is set to loop; otherwise, `false`.

---

<a id="volume"></a>

### volume

```java
float volume
```

Gets or sets the volume of the audio source.

A float value representing the audio source volume, where 1.0 is the default level.

---

<a id="pitch"></a>

### pitch

```java
float pitch
```

Gets or sets the pitch of the audio source.

A float value representing the pitch, where 1.0 is normal pitch.

---

<a id="volumerolloff"></a>

### volumeRolloff

```java
float volumeRolloff
```

Gets or sets the volume rolloff factor of the audio source.

A float value determining how the volume decreases with distance from the source.

---

<a id="mindistance"></a>

### minDistance

```java
float minDistance
```

Gets or sets the minimum distance for the audio source.

The minimum distance within which the audio source plays at full volume.

---

<a id="maxdistance"></a>

### maxDistance

```java
float maxDistance
```

Gets or sets the maximum distance for the audio source.

The maximum distance beyond which the audio source volume is effectively zero.

---

<a id="mute"></a>

### mute

```java
bool mute
```

Gets or sets a value indicating whether the audio source is muted.

`true` if the audio source is muted; otherwise, `false`.

---

<a id="isplaying"></a>

### isPlaying

```java
bool isPlaying
```

Gets a value indicating whether the audio source is currently playing.

`true` if the audio source is playing; otherwise, `false`.

---

<a id="ispaused"></a>

### isPaused

```java
bool isPaused
```

Gets a value indicating whether the audio source is currently paused.

`true` if the audio source is paused; otherwise, `false`.

---

<a id="time"></a>

### time

```java
float time
```

Gets or sets the current playback time of the audio source.

The current playback time in seconds.

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Play`](#play-1) `inline` | Starts playing the audio source from the beginning. |
| `void` | [`Stop`](#stop-1) `inline` | Stops playback of the audio source. |
| `void` | [`Pause`](#pause-1) `inline` | Pauses playback of the audio source. |
| `void` | [`Resume`](#resume-1) `inline` | Resumes playback of the audio source. |

---

<a id="play-1"></a>

### Play

`inline`

```java
inline void Play()
```

Starts playing the audio source from the beginning.

---

<a id="stop-1"></a>

### Stop

`inline`

```java
inline void Stop()
```

Stops playback of the audio source.

---

<a id="pause-1"></a>

### Pause

`inline`

```java
inline void Pause()
```

Pauses playback of the audio source.

---

<a id="resume-1"></a>

### Resume

`inline`

```java
inline void Resume()
```

Resumes playback of the audio source.

## Public Static Methods

| Return | Name | Description |
|--------|------|-------------|
| `AudioSourceComponent` | [`PlayClipAtPoint`](#playclipatpoint) `static` `inline` | Plays a one-shot audio clip at the specified position with a specified volume. |

---

<a id="playclipatpoint"></a>

### PlayClipAtPoint

`static` `inline`

```java
static inline AudioSourceComponent PlayClipAtPoint(AudioClip clip, Vector3 position, float volume = 1.0f)
```

Plays a one-shot audio clip at the specified position with a specified volume.

#### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `clip` | `[AudioClip](Unravel-Core-AudioClip.md#audioclip)` | The audio clip to play. |
| `position` | `[Vector3](Vector3.md#vector3)` | The world position where the audio will be played. |
| `volume` | `float` | The volume at which the audio will be played. Default is 1.0. |

## Private Attributes

| Return | Name | Description |
|--------|------|-------------|
| `AudioClip` | [`clip_`](#clip_)  |  |

---

<a id="clip_"></a>

### clip_

```java
AudioClip clip_
```

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_audio_source_get_loop`](#internal_m2n_audio_source_get_loop)  |  |
| `void` | [`internal_m2n_audio_source_set_loop`](#internal_m2n_audio_source_set_loop)  |  |
| `float` | [`internal_m2n_audio_source_get_volume`](#internal_m2n_audio_source_get_volume)  |  |
| `void` | [`internal_m2n_audio_source_set_volume`](#internal_m2n_audio_source_set_volume)  |  |
| `float` | [`internal_m2n_audio_source_get_pitch`](#internal_m2n_audio_source_get_pitch)  |  |
| `void` | [`internal_m2n_audio_source_set_pitch`](#internal_m2n_audio_source_set_pitch)  |  |
| `float` | [`internal_m2n_audio_source_get_volume_rolloff`](#internal_m2n_audio_source_get_volume_rolloff)  |  |
| `void` | [`internal_m2n_audio_source_set_volume_rolloff`](#internal_m2n_audio_source_set_volume_rolloff)  |  |
| `float` | [`internal_m2n_audio_source_get_min_distance`](#internal_m2n_audio_source_get_min_distance)  |  |
| `void` | [`internal_m2n_audio_source_set_min_distance`](#internal_m2n_audio_source_set_min_distance)  |  |
| `float` | [`internal_m2n_audio_source_get_max_distance`](#internal_m2n_audio_source_get_max_distance)  |  |
| `void` | [`internal_m2n_audio_source_set_max_distance`](#internal_m2n_audio_source_set_max_distance)  |  |
| `float` | [`internal_m2n_audio_source_get_time`](#internal_m2n_audio_source_get_time)  |  |
| `void` | [`internal_m2n_audio_source_set_time`](#internal_m2n_audio_source_set_time)  |  |
| `bool` | [`internal_m2n_audio_source_get_mute`](#internal_m2n_audio_source_get_mute)  |  |
| `void` | [`internal_m2n_audio_source_set_mute`](#internal_m2n_audio_source_set_mute)  |  |
| `bool` | [`internal_m2n_audio_source_is_playing`](#internal_m2n_audio_source_is_playing)  |  |
| `bool` | [`internal_m2n_audio_source_is_paused`](#internal_m2n_audio_source_is_paused)  |  |
| `void` | [`internal_m2n_audio_source_play`](#internal_m2n_audio_source_play)  |  |
| `void` | [`internal_m2n_audio_source_stop`](#internal_m2n_audio_source_stop)  |  |
| `void` | [`internal_m2n_audio_source_pause`](#internal_m2n_audio_source_pause)  |  |
| `void` | [`internal_m2n_audio_source_resume`](#internal_m2n_audio_source_resume)  |  |
| `Guid` | [`internal_m2n_audio_source_get_audio_clip`](#internal_m2n_audio_source_get_audio_clip)  |  |
| `void` | [`internal_m2n_audio_source_set_audio_clip`](#internal_m2n_audio_source_set_audio_clip)  |  |

---

<a id="internal_m2n_audio_source_get_loop"></a>

### internal_m2n_audio_source_get_loop

```java
bool internal_m2n_audio_source_get_loop(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_loop"></a>

### internal_m2n_audio_source_set_loop

```java
void internal_m2n_audio_source_set_loop(Entity eid, bool loop)
```

---

<a id="internal_m2n_audio_source_get_volume"></a>

### internal_m2n_audio_source_get_volume

```java
float internal_m2n_audio_source_get_volume(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_volume"></a>

### internal_m2n_audio_source_set_volume

```java
void internal_m2n_audio_source_set_volume(Entity eid, float volume)
```

---

<a id="internal_m2n_audio_source_get_pitch"></a>

### internal_m2n_audio_source_get_pitch

```java
float internal_m2n_audio_source_get_pitch(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_pitch"></a>

### internal_m2n_audio_source_set_pitch

```java
void internal_m2n_audio_source_set_pitch(Entity eid, float pitch)
```

---

<a id="internal_m2n_audio_source_get_volume_rolloff"></a>

### internal_m2n_audio_source_get_volume_rolloff

```java
float internal_m2n_audio_source_get_volume_rolloff(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_volume_rolloff"></a>

### internal_m2n_audio_source_set_volume_rolloff

```java
void internal_m2n_audio_source_set_volume_rolloff(Entity eid, float rolloff)
```

---

<a id="internal_m2n_audio_source_get_min_distance"></a>

### internal_m2n_audio_source_get_min_distance

```java
float internal_m2n_audio_source_get_min_distance(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_min_distance"></a>

### internal_m2n_audio_source_set_min_distance

```java
void internal_m2n_audio_source_set_min_distance(Entity eid, float distance)
```

---

<a id="internal_m2n_audio_source_get_max_distance"></a>

### internal_m2n_audio_source_get_max_distance

```java
float internal_m2n_audio_source_get_max_distance(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_max_distance"></a>

### internal_m2n_audio_source_set_max_distance

```java
void internal_m2n_audio_source_set_max_distance(Entity eid, float distance)
```

---

<a id="internal_m2n_audio_source_get_time"></a>

### internal_m2n_audio_source_get_time

```java
float internal_m2n_audio_source_get_time(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_time"></a>

### internal_m2n_audio_source_set_time

```java
void internal_m2n_audio_source_set_time(Entity eid, float seconds)
```

---

<a id="internal_m2n_audio_source_get_mute"></a>

### internal_m2n_audio_source_get_mute

```java
bool internal_m2n_audio_source_get_mute(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_mute"></a>

### internal_m2n_audio_source_set_mute

```java
void internal_m2n_audio_source_set_mute(Entity eid, bool mute)
```

---

<a id="internal_m2n_audio_source_is_playing"></a>

### internal_m2n_audio_source_is_playing

```java
bool internal_m2n_audio_source_is_playing(Entity eid)
```

---

<a id="internal_m2n_audio_source_is_paused"></a>

### internal_m2n_audio_source_is_paused

```java
bool internal_m2n_audio_source_is_paused(Entity eid)
```

---

<a id="internal_m2n_audio_source_play"></a>

### internal_m2n_audio_source_play

```java
void internal_m2n_audio_source_play(Entity eid)
```

---

<a id="internal_m2n_audio_source_stop"></a>

### internal_m2n_audio_source_stop

```java
void internal_m2n_audio_source_stop(Entity eid)
```

---

<a id="internal_m2n_audio_source_pause"></a>

### internal_m2n_audio_source_pause

```java
void internal_m2n_audio_source_pause(Entity eid)
```

---

<a id="internal_m2n_audio_source_resume"></a>

### internal_m2n_audio_source_resume

```java
void internal_m2n_audio_source_resume(Entity eid)
```

---

<a id="internal_m2n_audio_source_get_audio_clip"></a>

### internal_m2n_audio_source_get_audio_clip

```java
Guid internal_m2n_audio_source_get_audio_clip(Entity eid)
```

---

<a id="internal_m2n_audio_source_set_audio_clip"></a>

### internal_m2n_audio_source_set_audio_clip

```java
void internal_m2n_audio_source_set_audio_clip(Entity eid, Guid uid)
```

