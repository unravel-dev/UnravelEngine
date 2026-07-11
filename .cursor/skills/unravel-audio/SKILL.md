---
name: unravel-audio
description: >-
  Works on UnravelEngine audio: OpenAL via audiopp, audio sources, listeners,
  3D spatial audio, and audio components. Use for sound playback, 3D audio,
  listener setup, or audio system lifecycle.
disable-model-invocation: true
---

# Unravel Audio

## Start here

| Purpose | Path |
|---------|------|
| Audio system | `engine/engine/audio/ecs/systems/audio_system.h` |
| Source component | `engine/engine/audio/ecs/components/audio_source_component.h` |
| Listener component | `engine/engine/audio/ecs/components/audio_listener_component.h` |
| Meta | `engine/engine/meta/ecs/components/audio_source_component.hpp`, `audio_listener_component.hpp` |
| audiopp wrapper | `deps/audiopp/` |

## Architecture

- **OpenAL Soft** via `audiopp` dependency
- `audio_system` manages device, listener, source updates
- `audio_source_component` — clip playback, 3D position, volume, loop
- `audio_listener_component` — typically one per active camera

## Lifecycle

- `audio_source_component::on_play_begin()` — reset/play on play mode entry
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

## Common mistakes

- Multiple listeners without explicit selection logic
- Playing audio in edit mode unintentionally
- Missing transform sync for 3D sources
- Forgetting meta registration for new audio fields
