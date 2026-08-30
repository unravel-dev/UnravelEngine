---
name: components
title: Typed component workflow + semantics
description: The list -> add -> set -> get -> remove workflow and per-component knowledge - lights, particle emitters (semantics + recipes), physics bodies and shapes, animation clips, world-space text, reflection probes, local volumes and bloom.
order: 40
---
# Typed component workflow and semantics

## Workflow

1. scene_list_component_properties (optional component filter) - the editable
   schema, including enum values.
2. scene_add_components_batch to add the component. Valid names come from
   scene_list_component_types. C# scripts are different: scene_add_scripts_batch
   (see the scripting skill).
3. scene_set_component_properties_batch with a properties object.
4. Round-trip check with scene_get_component_properties_batch when unsure.
5. scene_remove_components_batch to take a component off again.

Typed get/set supported: Light, Skylight, Audio Source, Camera, Volume,
Script, Particle Emitter, Physics, Animation, Text, Reflection Probe, Bloom.

## Light

Spots aim along their forward axis: rotation_euler [90,0,0] points straight
down. Typical museum-style key light: intensity 5-6, range ~10, outer_angle
30-60, inner_angle roughly half of outer, casts_shadows true.

## Particle Emitter

Field semantics:
- lifetime = particle life in seconds; emission_lifetime = emitter run seconds
  per loop (loop:true restarts it); emission_rate = particles per second.
- shape + emission_shape_scale define the spawn region (circle + [1.3,0.1,1.3]
  is a flat disc); direction up | outward | inward gives initial direction.
- velocity_gradient / scale_gradient are arrays of {progress,min,max} over the
  particle's life; color_gradient is {progress,color:[r,g,b,a]}. Fade alpha in
  from 0 at progress 0 and out to 0 at 1 to avoid popping.
- Flipbook textures: texture_sheet_tiles [8,8] with texture_sheet_cycles 1
  plays an 8x8 sheet once per particle life.
- blend_mode: additive for fire, sparks, magic; normal for smoke and dust.
  color_intensity above 1 (6-10) makes additive effects glow through bloom.
- gravity_scale 1 makes particles fall (fountains); align_to_direction true
  stretches sparks along their motion.

Starting recipes: fire (circle, up, rate 70, life 1.0, additive, 8x8 flipbook,
intensity 8, scale shrinking 1.2 -> 0.35); smoke above it (rate 16, life 3,
normal blend, scale growing 1 -> 2.6, gray fading alpha); sparks (sphere, up,
velocity 5-7, gravity_scale 1, small shrinking quads, additive, intensity 10).

## Physics

- body_type: static (immovable), kinematic (transform driven), dynamic
  (simulated). Dynamic bodies need is_using_gravity true and a mass.
- shapes is an array: {type:"box",center,extents} | {type:"sphere",center,
  radius} | {type:"capsule"|"cylinder",center,radius,length}. extents and
  radius are FULL sizes in local units; with is_autoscaled true (default) they
  are multiplied by the entity scale - a unit cube primitive scaled to its
  world size just needs the default box shape.
- For meshes, set center/extents from the model AABB (assets_get_mesh_info).
- Nothing simulates in edit mode; test with play mode (verification skill).
- Domino-style chains: spacing must be less than tilt reach - a 1.4m tall
  domino tilted 30 degrees reaches about 0.7m.

## Animation

Set animation to a clip asset key (clips are generated next to imported
skinned meshes, e.g. "app:/data/Models/X/Run.anim"), auto_play true. The clip
must match the skeleton of the entity's model (clips generated from the same
source file always match). apply_root_motion moves the entity with the clip;
leave false for in-place showcase loops.

## Text (world-space labels)

Text renders in world units: with a ~1m scale entity, font_size 12-20 reads as
a signage label; area [width,height] is the wrap box in meters. alignment is
"middle_center" etc. Glyphs face +Z at yaw 0 - rotate the entity 180 in yaw to
face -Z viewers.

## Reflection Probe / Volume / Bloom

- Reflection Probe: type box with extents as HALF extents fitted to the room
  interior plus transition_distance for the blend skirt; type sphere uses
  range. update_mode once is right for static showcases.
- Volume: mode local + extents (full size) + blend_distance scopes co-located
  post components (e.g. Bloom) to a region; higher priority wins over the
  global volume. Put Volume and Bloom on the same entity, then raise bloom
  intensity (global default 0.15; a strong local glow zone is 0.6-1.0).

## Do

- Read the schema before writing values; enums are strings, not ints.
- Batch all property writes for one feature into one call (one undo step).

## Do not

- Do not set properties on a component that does not exist yet - the tool
  errors instead of adding it for you.
- Do not guess semantic fields (units, enum names); check the schema or read
  an existing configured entity with detail "components".
