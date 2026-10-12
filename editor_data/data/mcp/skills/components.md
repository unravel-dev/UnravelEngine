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

Which components are typed: scene_list_component_types marks each one with
typed true/false. Most come straight from the reflection the inspector uses, so
property names, ranges (minimum/maximum), enum values and descriptions in
scene_list_component_properties match what a person sees in the Inspector, and
writes go through the component's own setters.

## Light

enabled false switches a light off without deactivating its entity: no direct
light, no shadows, nothing in the global illumination. intensity 0 is not off -
the light still renders its shadow maps.

Spots aim along their forward axis: rotation_euler [90,0,0] points straight
down. Point and spot intensity is luminous intensity with an inverse-square
falloff: a surface at distance d receives intensity / d^2 (a directional
light's intensity is that illuminance directly), and range only fades the light
to zero at its edge. To light a surface d metres away as brightly as a
directional light of intensity E does, use intensity E * d^2 - e.g. a museum
key light 3 m above a plinth under a dim sun of 1: intensity ~9-20, range ~10,
outer_angle 30-60, inner_angle roughly half of outer, casts_shadows true.
source_radius (sphere) and source_length (tube along the light's local Y axis)
size the emitter in metres: wider, softer highlights at the same total light.

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
- A freshly added Physics component has NO shapes, and a body without a shape
  collides with nothing - things fall straight through it. Always set shapes.
- shapes is an array: {type:"box",center,extents} | {type:"sphere",center,
  radius} | {type:"capsule"|"cylinder",center,radius,length}. Omitted fields
  take defaults: box extents [1,1,1] (FULL size - a unit cube), sphere radius
  0.5 (a real radius - the 1 m Sphere primitive). With is_autoscaled true (the
  default) they are multiplied by the entity scale, so a scaled Cube primitive
  needs only [{"type":"box"}] and a Sphere primitive [{"type":"sphere"}].
- For meshes, set center/extents from the model AABB (assets_get_mesh_info).
- Nothing simulates in edit mode; test with play mode (verification skill).
- Domino-style chains: spacing must be less than tilt reach - a 1.4m tall
  domino tilted 30 degrees reaches about 0.7m.
- Surfaces: a physics material sets bounce (restitution 0..1) and grip
  (friction, 0 is ice). physics_materials_create_batch makes one,
  physics_materials_list_properties shows the ranges; assign it with property
  material on the Physics component. Bodies read it when they are built, so it
  shows in play mode.
- Contact events (sensor_events, collision_events, sensor_exit_on_destroy) are
  ON by default - triggers and collisions already reach scripts. Turn one off
  only to save cost; collision_exit_on_destroy is the one that starts off.
- velocity / angular_velocity are read-only: the simulation writes them, so read
  them in play mode to confirm something moves.

## Character Controller

The component for a walking player or NPC. It is mutually exclusive with
Physics - an entity has one or the other.

- It is a capsule: radius and height (both caps included) in metres, center
  offset from the entity origin. Fit it to the character's mesh bounds.
- step_height is the tallest ledge it climbs without jumping; slope_limit (0-90
  degrees) the steepest walkable slope.
- include_layers / exclude_layers take layer names (from the project's
  layer.layers setting), indices, or "everything" / "nothing".
- It does not move by itself and forces do not push it: a script drives it with
  CharacterControllerComponent.move / jump every frame. Start from
  engine:/data/scripts/samples/SampleCharacterController.cs (scripting-api).
- is_grounded and velocity are read-only and only meaningful in play mode -
  read them to check a jump or a fall.

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
