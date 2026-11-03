---
name: unravel-audio
description: >-
  Works on UnravelEngine audio: audiopp and its backends, audio sources, listeners,
  3D spatial audio, and audio components. Use for sound playback, 3D audio,
  listener setup, or audio system lifecycle.
---

# Unravel Audio

## Start here

| Purpose | Path |
|---------|------|
| Audio system | `engine/engine/audio/ecs/systems/audio_system.h` |
| Voice pool | `engine/engine/audio/audio_voice_pool.h` |
| Source component | `engine/engine/audio/ecs/components/audio_source_component.h` |
| Listener component | `engine/engine/audio/ecs/components/audio_listener_component.h` |
| Meta | `engine/engine/meta/ecs/components/audio_source_component.hpp`, `audio_listener_component.hpp` |
| audiopp wrapper | `deps/audiopp/` |

## Architecture

- `audiopp` (ours; extend it only when the engine cannot do without) plays through a backend chosen when the
  device opens: `--audio-backend openal|miniaudio`, else the `AUDIOPP_DEFAULT_BACKEND` CMake option, which
  `deps/audiopp/CMakeLists.txt` sets to miniaudio. The `AUDIOPP_WITH_OPENAL` and `AUDIOPP_WITH_MINIAUDIO` options leave one out of the build. Each backend's code is in
  `audiopp/impl/<backend>/`, and the public classes never name one.
- **OpenAL Soft**: linked statically; on Android it plays through Oboe (AAudio), and the build fails without it.
- **miniaudio**: the single header in `3rdparty/miniaudio`. `ma_sound` reads and resamples, and `voice_node` pans
  and sends: its stereo law, distance gain and filters are measured to match OpenAL Soft's, so a backend change
  does not move the mix. Change one and the parity tests in audiopp's own suite say so.
- `audio_system` owns the device, the mixer (master + bus gains and fades) and the play-mode hooks
- A lost device (unplugged headphones, on Android any route change): `audio_device_watch` paces
  `device::reconnect`. Sources wait where they were (`device::holds_sources_on_disconnect`), and
  `release_unheard_voices` ends the non-looping voices that have not played a sample, which would otherwise
  all start together when a device returns. No backend can be told to disconnect, so tests pause the device
  instead; a real unplug is a manual check.
- `audio_voice_pool` (`audio_system::get_voices()`) owns EVERY live source: one-shots
  (`play`, addressed by slot + generation ids) and component voices (`acquire`, held through a
  move-only `audio_voice_handle`; copies are empty). It applies the voice cap
  (`audio_system::max_concurrent_voices`), pushes bus gains, streams, frees finished voices and
  pauses / resumes / stops everything.
- Voice cap rank (`is_freed_before`): non-looping first, then lower priority, then one-shots before
  component voices, then the older voice. A one-shot refused by its clip limit frees nothing.
- Spatial falloff is linear: full volume up to the range's min, silent at its
  max. One-shots start at 1..20 m (`set_distance`, C# `AudioVoice.SetDistance`); a component uses
  its own range. Scripts play one-shots with `Audio.PlayOneShot`, never through a temporary entity.
- `audio_source_component` - settings (serialized) + a voice handle; it never owns a source itself
- `audio_listener_component` - typically one per active camera

## Lifecycle

- `audio_source_component::on_play_begin()` - reset/play on play mode entry
- System hooks: `audio_system::on_play_begin`
- Sources tied to entity transforms for 3D spatialization

## Assets

Audio clips loaded through `asset_manager` as typed assets. Follow asset pipeline for import.

## Verification checklist

- [ ] Sound plays in play mode
- [ ] 3D attenuation follows entity/camera movement
- [ ] Only one active listener (or correct priority)
- [ ] Play/end stops or resets sources appropriately
- [ ] No audio device errors on init, on either backend
- [ ] Volume/pitch properties serialize correctly
- [ ] `unravel-tests --suite "audio voice pool"` passes (opens each backend's null device itself)
- [ ] `audiopp_test` passes after any change in audiopp: it holds the backends to each other

## Common mistakes

- Multiple listeners without explicit selection logic
- Playing audio in edit mode unintentionally
- Missing transform sync for 3D sources
- Forgetting meta registration for new audio fields
