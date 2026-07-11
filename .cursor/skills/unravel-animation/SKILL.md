---
name: unravel-animation
description: >-
  Works on UnravelEngine animation: skeletal animation, clips, blend spaces,
  animation components, and animation panel integration. Use for skeletal
  animation, blend trees, clip playback, or skinned mesh animation bugs.
disable-model-invocation: true
---

# Unravel Animation

## Start here

| Purpose | Path |
|---------|------|
| Animation core | `engine/engine/animation/` |
| Animation component | `engine/engine/animation/ecs/components/animation_component.h` |
| Bone component | `engine/engine/ecs/components/` (bone hierarchy) |
| Animation system | `engine/engine/animation/ecs/systems/animation_system.h` |
| Clips | `engine/engine/animation/animation.h` |
| Meta | `engine/engine/meta/ecs/components/animation_component.hpp` |
| Editor panel | `editor/editor/hub/panels/animation_panel/` |
| Skinned rendering | `engine/engine/rendering/` (skinned submeshes in pipeline_stats) |

## Architecture

- Skeletal animation drives bone transforms
- `animation_component` references clips via `asset_handle<animation_clip>`
- `animation_system` updates bone poses each frame
- Skinned meshes submitted in deferred pipeline (separate draw path from static)

## Blend spaces

Blend space assets combine multiple clips by parameters (e.g. speed, direction). Defined in animation module — read existing blend space types before adding new ones.

## ECS integration

- `animation_component` on animated entities
- `bone_component` / `submesh_component` for hierarchy
- `model_component` links mesh to skeleton

## Pipeline stats

`pipeline_stats` tracks:

- `drawn_skinned_models`, `drawn_skinned_submeshes`
- Separate from static `drawn_models` counts

## Editor

`animation_panel/` — clip preview, blend editing. Changes may require asset reimport for clip data.

## Verification checklist

- [ ] Clip plays correctly in play mode
- [ ] Bone transforms propagate to skinned mesh
- [ ] Blend space parameters interpolate smoothly
- [ ] Animation assets import and resolve via `asset_handle`
- [ ] No T-pose flash on play begin
- [ ] Performance acceptable (skinned draw count in stats)

## Common mistakes

- Animating in edit mode when only play mode should run
- Missing bone hierarchy on skinned model
- Clip asset not loaded before playback start
- Forgetting meta registration for new animation fields
