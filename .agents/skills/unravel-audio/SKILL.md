---
name: unravel-audio
description: >-
  UnravelEngine audio: the audio system and mixer, the voice pool, audio source and listener
  components, one-shots, 3D falloff, device loss, and the audiopp library with its miniaudio
  backend. Use when sounds do not play, cut out, start together, pan or fade wrong, when the
  device is lost, or when editing deps/audiopp.
---

# Unravel audio

Covers `engine/engine/audio/` (system, mixer, voice pool, device watch, components) and the
first-party library `deps/audiopp/` the engine plays through.

## Gotchas

- The engine plays through miniaudio alone: `deps/audiopp/CMakeLists.txt` forces
  `AUDIOPP_WITH_OPENAL` off and `AUDIOPP_DEFAULT_BACKEND` to `miniaudio` for the editor and
  every player (the Android player's sub-build runs the same file). OpenAL Soft is under the
  LGPL and stays out of engine builds; the backend is a build choice, never a project setting.
- `--audio-backend` names a backend; one the build lacks falls back to the default with a
  warning.
- audiopp's public classes never name a backend; backend code lives in one folder per backend
  under `deps/audiopp/audiopp/audiopp/impl/`. miniaudio's `voice_node` pan law, distance
  gain and filters are measured to match OpenAL Soft's, and audiopp's parity tests say when a
  change moves the mix. HRTF exists on OpenAL only.
- `audio_voice_pool` (`audio_system::get_voices()`) owns every live source: one-shots (`play`,
  addressed by slot + generation ids, so a finished voice's id is harmless) and component
  voices (`acquire`, held through a move-only `audio_voice_handle` whose copies are empty and
  which goes inert once the pool is gone). Nothing else creates an `audio::source`.
- `audio_source_component` holds settings and a voice handle, never a source. It checks
  `clip->is_valid()` before acquiring (`asset_handle::get` returns the shared empty asset,
  never null), so an empty clip takes no voice.
- The voice cap (`audio_system::max_concurrent_voices`) frees by `is_freed_before`:
  non-looping first, then lower priority, then one-shots before component voices, then the
  older voice. A one-shot refused by its clip limit frees nothing.
- Gains pass `clamp_audio_gain` (0..1, NaN becomes 0) and ranges `clamp_audio_range` before
  they reach a source.
- Distance falloff is linear (`compute_distance_gain` in the miniaudio backend's `spatial.h`):
  full gain up to min, `1 - rolloff` at max, then on down to 0. One-shots use rolloff 1 (silent
  at max) and 1..20 m (`audio_default_min_distance`, `audio_default_max_distance`), changed by
  `set_distance` (C# `AudioVoice.SetDistance`); a component uses its own range and rolloff.
- Scripts play one-shots with `Audio.PlayOneShot` (2D, or at a position), never through a
  temporary entity per sound.
- A device has one listener: every active `audio_listener_component` writes it each frame, and
  the last one in view order wins. A scene keeps one listener active.
- A lost device (unplugged headphones; on Android any route change) waits through
  `audio_device_watch`, which paces `device::reconnect` (at once, then waits doubling from 1 s
  to 8 s). Sources hold their position (`device::holds_sources_on_disconnect`), and
  `release_unheard_voices` ends the non-looping voices that have not played a sample, which
  would otherwise all start together when the device returns.
- No backend can be told to disconnect: tests pause the device instead, and a real unplug or
  route change is a manual check.
- Listeners follow scaled time; bus and voice fades run on unscaled time (they go on under
  `Time.timeScale` 0 and hold while the simulation is paused); the device watch runs on real
  time.
- Audio CPU on a phone is measured from a RelWithDebInfo player: a Debug player is built -O0
  and reads near a whole core on its own.
- On the Android NDK `ma_uint64` is `unsigned long long` and `std::uint64_t` is
  `unsigned long`: `std::min` across the two compiles everywhere else and fails there.
- A miniaudio node with several output buses needs every bus read in equal chunks, so the
  graph runs with a fixed processing size.

## How it works

- `audio_system` owns the device, the mixer (master gain, bus gains for `sfx`, `music`, `ui`,
  bus fades) and the play-mode hooks. Its frame update, in the scene-system band, paces the
  device watch, places listeners and sources from their global transforms and advances
  fades.
- A source component plays only while its entity is active: the component created on an
  active entity, or its entity activated, calls `on_play_begin` (voice reset, autoplay armed);
  the component destroyed or its entity deactivated calls `on_play_end` (voice stopped).
- The pool applies the voice cap, pushes bus gains, streams, frees finished voices, and
  pauses, resumes or stops everything with play mode. An app going to the background pauses
  the device itself, so every source keeps its position.
- Clips are `audio_clip` assets (`.ogg`, `.wav`, `.flac`, `.mp3` sources); their binary
  layout is in `engine/engine/meta/audio/audio_clip.cpp`, and a layout change bumps
  `get_format_version<audio_clip>`.

## Procedures

- A change in audiopp: build audiopp's own tree (`deps/audiopp/audiopp`, its
  `CMakePresets.json`; `BUILD_AUDIOPP_TESTS` is on there and both backends build) and run
  `audiopp_test`: it renders pan, distance, filters, pitch and every effect offline on both
  backends and compares them. `--bench` measures the two side by side.
- A level check on an effect asserts the level first: an effect that is silent on one side
  reads 0 on both backends, and a dB comparison of two zeros passes.
- A new source setting: the component, its reflection
  (`engine/engine/meta/ecs/components/audio_source_component.cpp`) and the C# component
  (`engine_data/data/scripts/scene/components/AudioSourceComponent.cs`) together
  (cross-cutting list in AGENTS.md).

## Verify

- `unravel-tests --suite audio` runs "audio voice pool" (it opens each backend's null device
  itself; the runner has no audio device) and "audio clip encoding".
- `audiopp_test` after any change in `deps/audiopp/`.
- Play-mode smoke: a source plays and attenuates as the listener moves, one-shots play, ending
  play stops every voice. The profiler scope is `Audio/System Update`.

## Entry points

| Purpose | Path |
|---------|------|
| System and mixer | `engine/engine/audio/ecs/systems/audio_system.h` |
| Voice pool, cap, spatial helpers | `engine/engine/audio/audio_voice_pool.h` |
| Device loss pacing | `engine/engine/audio/audio_device_watch.h` |
| Components | `engine/engine/audio/ecs/components/audio_source_component.h`, `engine/engine/audio/ecs/components/audio_listener_component.h` |
| Clips | `engine/engine/audio/audio_clip.h` |
| audiopp device and backends | `deps/audiopp/audiopp/audiopp/device.h`, `deps/audiopp/audiopp/audiopp/impl/miniaudio/` |
| Engine build of audiopp | `deps/audiopp/CMakeLists.txt` |
| C# API | `engine_data/data/scripts/audio/Audio.cs` |
