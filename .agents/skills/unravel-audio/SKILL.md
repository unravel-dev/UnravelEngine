---
name: unravel-audio
description: >-
  Works on UnravelEngine audio: OpenAL via audiopp, audio sources, listeners,
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

- **OpenAL Soft** via `audiopp` dependency (ours; extend it only when the engine cannot do without)
- `audio_system` owns the device, the mixer (master + bus gains and fades) and the play-mode hooks
- `audio_voice_pool` (`audio_system::get_voices()`) owns EVERY live OpenAL source: one-shots
  (`play`, addressed by slot + generation ids) and component voices (`acquire`, held through a
  move-only `audio_voice_handle`; copies are empty). It applies the voice cap
  (`audio_system::max_concurrent_voices`), pushes bus gains, streams, frees finished voices and
  pauses / resumes / stops everything.
- Voice cap rank (`is_freed_before`): non-looping first, then lower priority, then one-shots before
  component voices, then the older voice. A one-shot refused by its clip limit frees nothing.
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
- [ ] No OpenAL device errors on init
- [ ] Volume/pitch properties serialize correctly
- [ ] `unravel-tests --suite "audio voice pool"` passes (opens OpenAL's null backend itself)

## Common mistakes

- Multiple listeners without explicit selection logic
- Playing audio in edit mode unintentionally
- Missing transform sync for 3D sources
- Forgetting meta registration for new audio fields
