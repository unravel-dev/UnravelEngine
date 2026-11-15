# Render change tracking and shadow map caching

How caches learn what changed (`engine/engine/rendering/render_change.h`), and the shadow map cache that reads it
(`shadowmap_generator`, `engine/engine/rendering/shadow.cpp`). GI captures read the same feed ([gi.md](gi.md)).

## Render change tracking

- `render_change` aspects: object (placement, pose, geometry, membership) and material (coverage, sidedness,
  albedo, normal, specular, occlusion, emission). A consumer reads only its `render_interest` mask
  (`shadow_caster`, `gi_card_capture`, `gi_card_direct`, `gi_field`, `velocity`).
- Materials: `material_property_id` with `get_material_property_traits` (`material.h`) is the single table of each
  property's aspects and whether it is per instance; the reflection attributes "material_property" and
  "per_instance" come from it (`make_material_property_attributes`). Setters compare first and call
  `mark_changed`; `get_revision(mask)`, `get_state_revision(mask)` and `get_instance_values_revision(mask)` sum
  only the asked aspects. Texture content (`asset_handle::version`) enters through `get_content_version(mask)` /
  `add_change_keys`. Graph parameters carry the aspects their Surface Output channels feed (skill
  `unravel-materials`).
- Models: `model_system` writes `model_component::get_change_stamps()` once per frame (the last frame of each
  aspect). Inputs: the transform's `dirty_ids::render_change` slot (placement), `update_armature` (pose, only when
  matrices or mapping differ), `touch(reasons)` from the component setters, the LOD mesh identities and the
  material change keys. `set_model` diffs the swap (`model::get_changes_from`), so a whole-model override stamps
  only the aspects that moved.
- Transform and pose setters compare exactly before dirtying: a write of the same value marks nothing.
- Adding a material property: the enum value and its traits row, its reflection attributes wrapped, `mark_changed`
  from the setter. Suite "render change" fails a reflected property without traits.
- Adding a consumer: define its `render_interest` mask and read `get_last_change(mask)` / `has_changed_since` /
  `has_changed_within`, or key a per-material cache on `get_revision(mask)`.
- An input that reaches no setter (a new input outside the component) must `touch` the model component; a global
  change (settings, a whole-scene event) invalidates the consumers directly.

## Shadow map caching

`graphics.shadow_map_caching`: a spot or point light's map, or a cascade of the sun, renders again only when its
key changes.

- Every input of a map reaches a key: `compute_view_settings_key` (epoch, map size, light type and implementation,
  view and projection matrices, atlas placement serial) or `compute_caster_facts` (entity, LOD, LOD mesh, the
  model's last change in `render_interest::shadow_caster`). A new per-object input goes through render change
  tracking; a global one calls `shadow::invalidate_cached_maps()` (the renderer does at play start, play end and
  return to foreground).
- An atlas tile placed again, even on the same square, has a new placement serial: another light may have drawn
  there in between.
- The sun's reach test follows the camera per submesh, so its cascade keys also take the collected batches'
  content hash; local lights key on caster facts alone.
- Static copy: a local light in the atlas with moving casters in reach restores the casters that hold still from a
  depth-only copy (`shadow_atlas::get_static_frame_buffer`; `fs_shadowmaps_restore.eshader` rebuilds the R32F
  colour from depth, inverse-z only) and draws only the movers. A caster is a mover while
  `has_changed_within(render_interest::shadow_caster, MOVER_SETTLE_FRAMES, frame)`.
- Shadow caster culling: `compute_shadow_view_mask` (`model.h`) is the one owner of which views a caster reaches,
  at model and submesh level; the mask ends at the first cascade fully containing the caster (the nested-cascade
  rule the receiver's cascade selection depends on), and the sun's `shadow_reach` gate rejects casters whose
  shadow cannot reach the view.

## Verify

- Suites "render change", "shadow caching", "shadow atlas", "render culling".
- Caching on vs off on a paused frame: the captures match. In a parked scene the cache renders nothing new: a
  detector that fires every frame there is a broken key (padding bytes in a hash, a write of an equal value).
