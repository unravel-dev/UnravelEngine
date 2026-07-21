<a id="particleemittercomponent"></a>

# ParticleEmitterComponent

> **Extends:** [`Unravel.Core.Component`](Unravel-Core-Component.md#component)

Represents a component that provides particle emission capabilities for an entity.

## Properties

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`enabled`](#enabled-1)  | Gets or sets whether the particle emitter is enabled. |
| `uint` | [`maxParticles`](#maxparticles)  | Gets or sets the maximum number of particles. |
| `ParticleSimBackend` | [`simulationBackend`](#simulationbackend)  | Gets or sets the particle simulation backend (CPU batched gather vs GPU compute pack). |
| `EmitterShape` | [`shape`](#shape)  | Gets or sets the emitter shape. |
| `EmitterDirection` | [`direction`](#direction)  | Gets or sets the emitter direction. |
| `float` | [`gravityScale`](#gravityscale-1)  | Gets or sets the gravity scale applied to particles. |
| `float` | [`emissionRate`](#emissionrate)  | Gets or sets the particle emission rate (particles per second). |
| `float` | [`temporalMotion`](#temporalmotion)  | Gets or sets the temporal motion interpolation factor. |
| `float` | [`velocityDamping`](#velocitydamping)  | Gets or sets the velocity damping factor. |
| `float` | [`opacity`](#opacity)  | Gets or sets the opacity for particle opacity. |
| `Vector3` | [`forceOverLifetime`](#forceoverlifetime)  | Gets or sets the force applied over particle lifetime. |
| `Vector3` | [`emissionShapeScale`](#emissionshapescale)  | Gets or sets the emission shape scale. |
| `float` | [`emissionLifetime`](#emissionlifetime)  | Gets or sets the emission lifetime in seconds. |
| `float` | [`lifetime`](#lifetime)  | Gets or sets the particle lifetime in seconds. |
| `int` | [`positionEasing`](#positioneasing)  | Gets or sets the position easing function. |
| `uint` | [`numParticles`](#numparticles)  | Gets the current number of active particles. |
| `bool` | [`isPlaying`](#isplaying-1)  | Gets whether the emitter is currently playing. |
| `bool` | [`isPaused`](#ispaused-1)  | Gets whether the emitter is currently paused. |
| `Guid` | [`texture`](#texture)  | Gets or sets the texture asset used for particles. |
| `bool` | [`loop`](#loop-1)  | Gets or sets whether the emitter loops continuously (true) or emits only once (false). |

---

<a id="enabled-1"></a>

### enabled

```java
bool enabled
```

Gets or sets whether the particle emitter is enabled.

---

<a id="maxparticles"></a>

### maxParticles

```java
uint maxParticles
```

Gets or sets the maximum number of particles.

---

<a id="simulationbackend"></a>

### simulationBackend

```java
ParticleSimBackend simulationBackend
```

Gets or sets the particle simulation backend (CPU batched gather vs GPU compute pack).

---

<a id="shape"></a>

### shape

```java
EmitterShape shape
```

Gets or sets the emitter shape.

---

<a id="direction"></a>

### direction

```java
EmitterDirection direction
```

Gets or sets the emitter direction.

---

<a id="gravityscale-1"></a>

### gravityScale

```java
float gravityScale
```

Gets or sets the gravity scale applied to particles.

---

<a id="emissionrate"></a>

### emissionRate

```java
float emissionRate
```

Gets or sets the particle emission rate (particles per second).

---

<a id="temporalmotion"></a>

### temporalMotion

```java
float temporalMotion
```

Gets or sets the temporal motion interpolation factor.

---

<a id="velocitydamping"></a>

### velocityDamping

```java
float velocityDamping
```

Gets or sets the velocity damping factor.

---

<a id="opacity"></a>

### opacity

```java
float opacity
```

Gets or sets the opacity for particle opacity.

---

<a id="forceoverlifetime"></a>

### forceOverLifetime

```java
Vector3 forceOverLifetime
```

Gets or sets the force applied over particle lifetime.

---

<a id="emissionshapescale"></a>

### emissionShapeScale

```java
Vector3 emissionShapeScale
```

Gets or sets the emission shape scale.

---

<a id="emissionlifetime"></a>

### emissionLifetime

```java
float emissionLifetime
```

Gets or sets the emission lifetime in seconds.

---

<a id="lifetime"></a>

### lifetime

```java
float lifetime
```

Gets or sets the particle lifetime in seconds.

---

<a id="positioneasing"></a>

### positionEasing

```java
int positionEasing
```

Gets or sets the position easing function.

---

<a id="numparticles"></a>

### numParticles

```java
uint numParticles
```

Gets the current number of active particles.

---

<a id="isplaying-1"></a>

### isPlaying

```java
bool isPlaying
```

Gets whether the emitter is currently playing.

---

<a id="ispaused-1"></a>

### isPaused

```java
bool isPaused
```

Gets whether the emitter is currently paused.

---

<a id="texture"></a>

### texture

```java
Guid texture
```

Gets or sets the texture asset used for particles.

---

<a id="loop-1"></a>

### loop

```java
bool loop
```

Gets or sets whether the emitter loops continuously (true) or emits only once (false).

## Public Methods

| Return | Name | Description |
|--------|------|-------------|
| `void` | [`Play`](#play-2) `inline` | Starts particle emission. |
| `void` | [`Stop`](#stop-2) `inline` | Stops particle emission. |
| `void` | [`StopAndReset`](#stopandreset) `inline` | Stops particle emission and resets the emitter state. |
| `void` | [`Pause`](#pause-2) `inline` | Pauses particle emission. |
| `void` | [`Resume`](#resume-2) `inline` | Resumes particle emission from a paused state. |
| `void` | [`ResetEmitter`](#resetemitter) `inline` | Resets the emitter, clearing all particles and resetting internal state. |

---

<a id="play-2"></a>

### Play

`inline`

```java
inline void Play()
```

Starts particle emission.

---

<a id="stop-2"></a>

### Stop

`inline`

```java
inline void Stop()
```

Stops particle emission.

---

<a id="stopandreset"></a>

### StopAndReset

`inline`

```java
inline void StopAndReset()
```

Stops particle emission and resets the emitter state.

---

<a id="pause-2"></a>

### Pause

`inline`

```java
inline void Pause()
```

Pauses particle emission.

---

<a id="resume-2"></a>

### Resume

`inline`

```java
inline void Resume()
```

Resumes particle emission from a paused state.

---

<a id="resetemitter"></a>

### ResetEmitter

`inline`

```java
inline void ResetEmitter()
```

Resets the emitter, clearing all particles and resetting internal state.

## Private Methods

| Return | Name | Description |
|--------|------|-------------|
| `bool` | [`internal_m2n_particle_emitter_get_enabled`](#internal_m2n_particle_emitter_get_enabled)  |  |
| `void` | [`internal_m2n_particle_emitter_set_enabled`](#internal_m2n_particle_emitter_set_enabled)  |  |
| `uint` | [`internal_m2n_particle_emitter_get_max_particles`](#internal_m2n_particle_emitter_get_max_particles)  |  |
| `void` | [`internal_m2n_particle_emitter_set_max_particles`](#internal_m2n_particle_emitter_set_max_particles)  |  |
| `int` | [`internal_m2n_particle_emitter_get_shape`](#internal_m2n_particle_emitter_get_shape)  |  |
| `void` | [`internal_m2n_particle_emitter_set_shape`](#internal_m2n_particle_emitter_set_shape)  |  |
| `int` | [`internal_m2n_particle_emitter_get_direction`](#internal_m2n_particle_emitter_get_direction)  |  |
| `void` | [`internal_m2n_particle_emitter_set_direction`](#internal_m2n_particle_emitter_set_direction)  |  |
| `float` | [`internal_m2n_particle_emitter_get_gravity_scale`](#internal_m2n_particle_emitter_get_gravity_scale)  |  |
| `void` | [`internal_m2n_particle_emitter_set_gravity_scale`](#internal_m2n_particle_emitter_set_gravity_scale)  |  |
| `float` | [`internal_m2n_particle_emitter_get_emission_rate`](#internal_m2n_particle_emitter_get_emission_rate)  |  |
| `void` | [`internal_m2n_particle_emitter_set_emission_rate`](#internal_m2n_particle_emitter_set_emission_rate)  |  |
| `float` | [`internal_m2n_particle_emitter_get_temporal_motion`](#internal_m2n_particle_emitter_get_temporal_motion)  |  |
| `void` | [`internal_m2n_particle_emitter_set_temporal_motion`](#internal_m2n_particle_emitter_set_temporal_motion)  |  |
| `float` | [`internal_m2n_particle_emitter_get_velocity_damping`](#internal_m2n_particle_emitter_get_velocity_damping)  |  |
| `void` | [`internal_m2n_particle_emitter_set_velocity_damping`](#internal_m2n_particle_emitter_set_velocity_damping)  |  |
| `float` | [`internal_m2n_particle_emitter_get_opacity`](#internal_m2n_particle_emitter_get_opacity)  |  |
| `void` | [`internal_m2n_particle_emitter_set_opacity`](#internal_m2n_particle_emitter_set_opacity)  |  |
| `Vector3` | [`internal_m2n_particle_emitter_get_force_over_lifetime`](#internal_m2n_particle_emitter_get_force_over_lifetime)  |  |
| `void` | [`internal_m2n_particle_emitter_set_force_over_lifetime`](#internal_m2n_particle_emitter_set_force_over_lifetime)  |  |
| `Vector3` | [`internal_m2n_particle_emitter_get_emission_shape_scale`](#internal_m2n_particle_emitter_get_emission_shape_scale)  |  |
| `void` | [`internal_m2n_particle_emitter_set_emission_shape_scale`](#internal_m2n_particle_emitter_set_emission_shape_scale)  |  |
| `float` | [`internal_m2n_particle_emitter_get_emission_lifetime`](#internal_m2n_particle_emitter_get_emission_lifetime)  |  |
| `void` | [`internal_m2n_particle_emitter_set_emission_lifetime`](#internal_m2n_particle_emitter_set_emission_lifetime)  |  |
| `float` | [`internal_m2n_particle_emitter_get_lifetime`](#internal_m2n_particle_emitter_get_lifetime)  |  |
| `void` | [`internal_m2n_particle_emitter_set_lifetime`](#internal_m2n_particle_emitter_set_lifetime)  |  |
| `int` | [`internal_m2n_particle_emitter_get_position_easing`](#internal_m2n_particle_emitter_get_position_easing)  |  |
| `void` | [`internal_m2n_particle_emitter_set_position_easing`](#internal_m2n_particle_emitter_set_position_easing)  |  |
| `uint` | [`internal_m2n_particle_emitter_get_num_particles`](#internal_m2n_particle_emitter_get_num_particles)  |  |
| `bool` | [`internal_m2n_particle_emitter_is_playing`](#internal_m2n_particle_emitter_is_playing)  |  |
| `bool` | [`internal_m2n_particle_emitter_is_paused`](#internal_m2n_particle_emitter_is_paused)  |  |
| `Guid` | [`internal_m2n_particle_emitter_get_texture`](#internal_m2n_particle_emitter_get_texture)  |  |
| `void` | [`internal_m2n_particle_emitter_set_texture`](#internal_m2n_particle_emitter_set_texture)  |  |
| `void` | [`internal_m2n_particle_emitter_play`](#internal_m2n_particle_emitter_play)  |  |
| `void` | [`internal_m2n_particle_emitter_stop`](#internal_m2n_particle_emitter_stop)  |  |
| `void` | [`internal_m2n_particle_emitter_stop_and_reset`](#internal_m2n_particle_emitter_stop_and_reset)  |  |
| `void` | [`internal_m2n_particle_emitter_pause`](#internal_m2n_particle_emitter_pause)  |  |
| `void` | [`internal_m2n_particle_emitter_resume`](#internal_m2n_particle_emitter_resume)  |  |
| `void` | [`internal_m2n_particle_emitter_reset_emitter`](#internal_m2n_particle_emitter_reset_emitter)  |  |
| `bool` | [`internal_m2n_particle_emitter_get_loop`](#internal_m2n_particle_emitter_get_loop)  |  |
| `void` | [`internal_m2n_particle_emitter_set_loop`](#internal_m2n_particle_emitter_set_loop)  |  |
| `int` | [`internal_m2n_particle_emitter_get_simulation_backend`](#internal_m2n_particle_emitter_get_simulation_backend)  |  |
| `void` | [`internal_m2n_particle_emitter_set_simulation_backend`](#internal_m2n_particle_emitter_set_simulation_backend)  |  |

---

<a id="internal_m2n_particle_emitter_get_enabled"></a>

### internal_m2n_particle_emitter_get_enabled

```java
bool internal_m2n_particle_emitter_get_enabled(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_enabled"></a>

### internal_m2n_particle_emitter_set_enabled

```java
void internal_m2n_particle_emitter_set_enabled(Entity eid, bool enabled)
```

---

<a id="internal_m2n_particle_emitter_get_max_particles"></a>

### internal_m2n_particle_emitter_get_max_particles

```java
uint internal_m2n_particle_emitter_get_max_particles(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_max_particles"></a>

### internal_m2n_particle_emitter_set_max_particles

```java
void internal_m2n_particle_emitter_set_max_particles(Entity eid, uint maxParticles)
```

---

<a id="internal_m2n_particle_emitter_get_shape"></a>

### internal_m2n_particle_emitter_get_shape

```java
int internal_m2n_particle_emitter_get_shape(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_shape"></a>

### internal_m2n_particle_emitter_set_shape

```java
void internal_m2n_particle_emitter_set_shape(Entity eid, int shape)
```

---

<a id="internal_m2n_particle_emitter_get_direction"></a>

### internal_m2n_particle_emitter_get_direction

```java
int internal_m2n_particle_emitter_get_direction(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_direction"></a>

### internal_m2n_particle_emitter_set_direction

```java
void internal_m2n_particle_emitter_set_direction(Entity eid, int direction)
```

---

<a id="internal_m2n_particle_emitter_get_gravity_scale"></a>

### internal_m2n_particle_emitter_get_gravity_scale

```java
float internal_m2n_particle_emitter_get_gravity_scale(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_gravity_scale"></a>

### internal_m2n_particle_emitter_set_gravity_scale

```java
void internal_m2n_particle_emitter_set_gravity_scale(Entity eid, float scale)
```

---

<a id="internal_m2n_particle_emitter_get_emission_rate"></a>

### internal_m2n_particle_emitter_get_emission_rate

```java
float internal_m2n_particle_emitter_get_emission_rate(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_emission_rate"></a>

### internal_m2n_particle_emitter_set_emission_rate

```java
void internal_m2n_particle_emitter_set_emission_rate(Entity eid, float rate)
```

---

<a id="internal_m2n_particle_emitter_get_temporal_motion"></a>

### internal_m2n_particle_emitter_get_temporal_motion

```java
float internal_m2n_particle_emitter_get_temporal_motion(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_temporal_motion"></a>

### internal_m2n_particle_emitter_set_temporal_motion

```java
void internal_m2n_particle_emitter_set_temporal_motion(Entity eid, float motion)
```

---

<a id="internal_m2n_particle_emitter_get_velocity_damping"></a>

### internal_m2n_particle_emitter_get_velocity_damping

```java
float internal_m2n_particle_emitter_get_velocity_damping(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_velocity_damping"></a>

### internal_m2n_particle_emitter_set_velocity_damping

```java
void internal_m2n_particle_emitter_set_velocity_damping(Entity eid, float damping)
```

---

<a id="internal_m2n_particle_emitter_get_opacity"></a>

### internal_m2n_particle_emitter_get_opacity

```java
float internal_m2n_particle_emitter_get_opacity(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_opacity"></a>

### internal_m2n_particle_emitter_set_opacity

```java
void internal_m2n_particle_emitter_set_opacity(Entity eid, float opacity)
```

---

<a id="internal_m2n_particle_emitter_get_force_over_lifetime"></a>

### internal_m2n_particle_emitter_get_force_over_lifetime

```java
Vector3 internal_m2n_particle_emitter_get_force_over_lifetime(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_force_over_lifetime"></a>

### internal_m2n_particle_emitter_set_force_over_lifetime

```java
void internal_m2n_particle_emitter_set_force_over_lifetime(Entity eid, Vector3 force)
```

---

<a id="internal_m2n_particle_emitter_get_emission_shape_scale"></a>

### internal_m2n_particle_emitter_get_emission_shape_scale

```java
Vector3 internal_m2n_particle_emitter_get_emission_shape_scale(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_emission_shape_scale"></a>

### internal_m2n_particle_emitter_set_emission_shape_scale

```java
void internal_m2n_particle_emitter_set_emission_shape_scale(Entity eid, Vector3 scale)
```

---

<a id="internal_m2n_particle_emitter_get_emission_lifetime"></a>

### internal_m2n_particle_emitter_get_emission_lifetime

```java
float internal_m2n_particle_emitter_get_emission_lifetime(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_emission_lifetime"></a>

### internal_m2n_particle_emitter_set_emission_lifetime

```java
void internal_m2n_particle_emitter_set_emission_lifetime(Entity eid, float lifetime)
```

---

<a id="internal_m2n_particle_emitter_get_lifetime"></a>

### internal_m2n_particle_emitter_get_lifetime

```java
float internal_m2n_particle_emitter_get_lifetime(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_lifetime"></a>

### internal_m2n_particle_emitter_set_lifetime

```java
void internal_m2n_particle_emitter_set_lifetime(Entity eid, float lifetime)
```

---

<a id="internal_m2n_particle_emitter_get_position_easing"></a>

### internal_m2n_particle_emitter_get_position_easing

```java
int internal_m2n_particle_emitter_get_position_easing(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_position_easing"></a>

### internal_m2n_particle_emitter_set_position_easing

```java
void internal_m2n_particle_emitter_set_position_easing(Entity eid, int easing)
```

---

<a id="internal_m2n_particle_emitter_get_num_particles"></a>

### internal_m2n_particle_emitter_get_num_particles

```java
uint internal_m2n_particle_emitter_get_num_particles(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_is_playing"></a>

### internal_m2n_particle_emitter_is_playing

```java
bool internal_m2n_particle_emitter_is_playing(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_is_paused"></a>

### internal_m2n_particle_emitter_is_paused

```java
bool internal_m2n_particle_emitter_is_paused(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_get_texture"></a>

### internal_m2n_particle_emitter_get_texture

```java
Guid internal_m2n_particle_emitter_get_texture(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_texture"></a>

### internal_m2n_particle_emitter_set_texture

```java
void internal_m2n_particle_emitter_set_texture(Entity eid, Guid texture)
```

---

<a id="internal_m2n_particle_emitter_play"></a>

### internal_m2n_particle_emitter_play

```java
void internal_m2n_particle_emitter_play(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_stop"></a>

### internal_m2n_particle_emitter_stop

```java
void internal_m2n_particle_emitter_stop(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_stop_and_reset"></a>

### internal_m2n_particle_emitter_stop_and_reset

```java
void internal_m2n_particle_emitter_stop_and_reset(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_pause"></a>

### internal_m2n_particle_emitter_pause

```java
void internal_m2n_particle_emitter_pause(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_resume"></a>

### internal_m2n_particle_emitter_resume

```java
void internal_m2n_particle_emitter_resume(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_reset_emitter"></a>

### internal_m2n_particle_emitter_reset_emitter

```java
void internal_m2n_particle_emitter_reset_emitter(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_get_loop"></a>

### internal_m2n_particle_emitter_get_loop

```java
bool internal_m2n_particle_emitter_get_loop(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_loop"></a>

### internal_m2n_particle_emitter_set_loop

```java
void internal_m2n_particle_emitter_set_loop(Entity eid, bool loop)
```

---

<a id="internal_m2n_particle_emitter_get_simulation_backend"></a>

### internal_m2n_particle_emitter_get_simulation_backend

```java
int internal_m2n_particle_emitter_get_simulation_backend(Entity eid)
```

---

<a id="internal_m2n_particle_emitter_set_simulation_backend"></a>

### internal_m2n_particle_emitter_set_simulation_backend

```java
void internal_m2n_particle_emitter_set_simulation_backend(Entity eid, int backend)
```

