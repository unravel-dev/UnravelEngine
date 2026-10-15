---
name: components
title: Component workflow + semantics
description: The list -> add -> set -> get -> remove workflow, how properties are shaped (nested objects, lists, enums, entity and asset references), and per-component knowledge - lights, particle emitters (semantics + recipes), physics bodies and shapes, character controllers, animation clips, world-space text, reflection probes, local volumes and post effects.
order: 40
---
# Component workflow and semantics

## Workflow

1. scene_list_components - the components you can add, with how many
   properties each has; scene_list_components {"component":"Light"} - that
   component's properties: names, types, enum values, ranges, read-only.
2. scene_add_components_batch to add the component. A C# script is component
   "Script" with its type_name (see the scripting skill).
3. scene_set_component_properties_batch with a properties object.
4. scene_get_component_properties_batch to read back (names: [...] reads only
   those, also the ones the current mode hides).
5. scene_remove_components_batch to take a component off again.

Every component's properties come from the reflection the Inspector uses:
names, ranges, enum values and descriptions match what a person sees there, and
writes go through the component's own setters.

## How values are shaped

- Nested settings are objects: {"light":{"intensity":3}}. A partial object
  merges into the current value - name only what you change.
- Lists are arrays and take the array's length; a list of objects merges each
  element by index (a shorter array drops the rest).
- Enums are strings: the value name ("res_512") or its label ("512"); case,
  spaces and underscores do not matter.
- Asset references are keys (or uids), null clears them; entity references are
  entity ids.
- Out-of-range values, unknown names and renamed names (reported with the new
  name) reject the whole call; nothing changes.
- Read-only properties (statistics, velocities) may be written back as read;
  a value that moved since the read is left as it is and reported in a warning.
- Undo restores the whole component as it was, hidden settings included.

## Light

Light has enabled and a nested light object: color, intensity,
indirect_intensity, type (directional | point | spot), casts_shadows,
source_radius / source_length, point_data {range}, spot_data {range,
inner_angle, outer_angle}, shadow_params {type, resolution, bias, normal_bias,
slope_bias, near_plane, far_plane, pcf / pcss / vsm / esm settings},
directional_shadow_params {splits, distribution, stabilize} and contact_shadow
{enabled, ray_length, thickness, max_distance, opacity}. Example:
{"light":{"type":"spot","intensity":15,"spot_data":{"range":10,"outer_angle":45,"inner_angle":22}}}.

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

Every key below is a reflected property, named as the inspector's tooltips
describe it; enum values are snake_case (older spellings such as "OutExpo"
are accepted). A get without names leaves out the settings of other modes
(hidden in the inspector too); name them to read them anyway. A renamed key is
rejected with its new name.

Emission:
- lifetime [min,max] = particle life in seconds; duration = seconds of one
  emission cycle (loop:true repeats it, a one-shot stops once its last particle
  is gone); emission_rate = particles per second; rate_over_distance =
  particles per metre the emitter moves (trails and footprints stay evenly
  spaced at any speed and frame rate).
- bursts [{time,count:[min,max],cycles,interval,probability}] fire on top of
  the rates in every cycle: an explosion's flash at 0 s and its debris at
  0.05 s; cycles 0 repeats every interval until the cycle ends. emission_rate 0
  with bursts is a one-shot; with neither, scripts fire it with Emit(count).
- shape sphere | hemisphere | circle | box | rect | cone | line | torus | mesh,
  sized by shape_scale and offset by shape_position (circle + [1.3,0.1,1.3] is
  a flat disc). cone_angle (degrees) slants a cone's rim, torus_thickness is the
  tube's radius as a share of the ring's, shape_mesh (a mesh asset) with
  shape_mesh_mode surface | vertices spawns over a model.
  direction up | outward | inward (outward follows the normals of cone, torus
  and mesh); direction_spread (degrees) widens it.
- prewarm true starts a looping emitter full (one particle lifetime of emission
  already alive) when it first plays after it is created or reset: ambient fire,
  smoke and mist do not build up at scene start.
- auto_play false keeps an emitter stopped when the scene starts or it is
  instantiated, until Play(); use it for one-shot effects scripts trigger.

Motion (exact at any frame rate; there is no per-frame integration):
- start_speed [min,max] m/s; speed_over_life [{progress,value}] multiplies that
  launch velocity; drag (per second) decays it so gravity and acceleration
  reach a terminal speed; gravity_scale 1 falls (fountains); acceleration
  [x,y,z] m/s^2 is wind or buoyancy; inherit_velocity carries the emitter's own
  motion.
- motion_easing (linear, out_expo, out_cubic, ...) remaps a particle's time:
  out_expo bursts out and settles - explosions, novas. spawn_along_path 1
  spreads a moving emitter's spawns along its path (smooth trails).
- turbulence_strength (metres, 0 = off) swirls particles through a curl-noise
  field: smoke billows, embers wander, magic churns. turbulence_frequency
  (swirls per metre), turbulence_speed (swirls per second, 0 freezes the field),
  turbulence_over_life multiplies the strength over life.
- collision kill | bounce with collision_planes [[nx,ny,nz,offset]] (world
  normal and offset from the emitter along it; [[0,1,0,0]] is the ground at the
  emitter's height); restitution, friction and max_bounces shape the bounce.
  World simulation space only. collision_radius_scale (default 1) makes a
  particle touch with its edge, half its size from the plane, so sprites and
  meshes rest on the floor; 0 = its centre.
- sub_emitters [{trigger:birth|death|collision,target:<entity id>,count,
  probability,inherit_velocity,inherit_color,inherit_size}] burst another
  entity's emitter at each event: sparks where embers land, smoke where a
  rocket dies. Give the target emission_rate 0 and auto_play false. A target
  of null is a slot not assigned yet.

Look:
- size_over_life [{progress,min,max}] is the size over the particle's life and
  every key counts: [0 at 0, full at 0.07, full at 1] pops in, [0, full at 0.5,
  0] grows then shrinks. Each particle picks its place in the min..max range at
  birth and at death and drifts between the two. size_axes [x,y,z] multiplies
  it per axis, and size_axes_over_life [{progress,value:[x,y,z]}] squashes and
  stretches it over life (raindrops, shockwaves, flattening smoke); z is the
  depth of mesh particles.
- color_gradient [{progress,color:[r,g,b,a]}] uses every key. Fade alpha in
  from 0 at progress 0 and out to 0 at 1 to avoid popping.
  random_between_gradients true mixes it with color_gradient_b by a random
  amount per particle (sparks from yellow to red). intensity_over_life
  multiplies color_intensity over life (embers that flare and cool).
- blend_mode: additive for fire, sparks, magic; normal for smoke and dust.
  color_intensity above 1 (6-10) makes additive effects glow through bloom.
- Flipbook textures: texture_sheet_tiles [8,8] with texture_sheet_cycles 1
  plays an 8x8 sheet once per particle life. texture_sheet_use_frame_curve
  with texture_sheet_frame_curve (0 = first frame, 1 = last) holds, speeds up
  or reverses it; texture_sheet_random_row plays one random row per particle
  (several animations in one sheet). frame_blending true crossfades frames of
  a sheet that plays slower than about 30 frames per second.
  erosion_over_life (0..1) dissolves texels below that alpha.
- align_to_direction true turns particles so the texture's top points along
  their motion, within the quad's plane in every render mode (camera-facing
  sparks, arcs crawling flat over the floor, upright streaks). Draw
  directional art pointing up and stretch it with size_axes y (sparks
  [1,3,1]); a start_rotation of [90,90] turns art drawn sideways.
- render_mode billboard | horizontal_billboard | vertical_billboard |
  camera_facing | stretched | trail | ribbon | mesh | fixed_axis. trail draws
  each particle's own past path over trail_length seconds (it curves with
  gravity, drag and turbulence), narrowing to trail_width_end (share of the
  size) and fading toward its tail. ribbon joins the emitter's live particles,
  oldest to youngest, into one strip: a moving emitter with a high rate (or
  rate_over_distance) leaves a ribbon behind it; ribbon_texture_mode age (the
  texture flows with the particles' age) | stretch (once head to tail) | tile
  (repeated every ribbon_tile_time seconds of age). fixed_axis stands each
  quad along facing_axis (emitter space, [0,1,0] = vertical billboard) turning
  about it toward the camera: beams, grass, streaks along a slope.
- stretched: stretch_camera_velocity_scale 1 stretches particles by their
  motion as the camera sees it (rain and snow streak when the camera moves
  through them); 0 = their own motion only.
- render_mode mesh draws render_mesh (a mesh asset key, e.g.
  "engine:/embedded/cube" or "engine:/embedded/sphere") per particle with
  render_material (unset = the mesh's own materials), drawn, lit, shadowed
  (mesh_cast_shadows) and with motion vectors like the scene's models: debris,
  rocks, shells, leaves. Size 1 is the mesh's own size, times size_axes (and
  size_axes_over_life, z included); start_rotation / spin_speed tumble each
  mesh about an axis of its own, align_to_direction points its +Y along the
  motion. The color
  gradient's alpha (and opacity) fades meshes out by dithering, as LOD
  transitions do (they stop casting shadows below half); its color, the blend
  mode and the texture do not apply to meshes.
- Horizontal billboards follow the entity's rotation: spin the entity to spin a
  ground rune; local simulation_space plus a spinning entity swirls particles.
- start_rotation [min,max] is each particle's start angle in degrees (0-360
  keeps smoke and fire from repeating one orientation); spin_speed [min,max]
  spins it in degrees per second, spin_over_life multiplies the spin over life
  (debris that settles), and random_spin_direction true spins half the other
  way. A [35,35] spin_speed turns a ground rune without turning its entity.
- size_by_speed_multiplier / color_by_speed_gradient scale and tint by speed
  over size_by_speed_speed_range / color_by_speed_speed_range ([min, max] m/s);
  lifetime_by_emitter_speed_curve scales the lifetime over
  lifetime_by_emitter_speed_range (the emitter's own speed, m/s).
- soft_fade_distance (metres, 0 = off) fades particles where they meet the scene
  behind them: smoke, mist and glows near the floor stop cutting hard lines.
- lighting unlit (default: the colour is emitted light - fire, magic, sparks) |
  volumetric (lit once per particle by the sky, the sun with its shadow and up
  to 4 nearby lights, the same from every side - smoke, dust, steam) | surface
  (lit like a small sphere; lighting_wrap 0..1 softens the terminator;
  normal_texture, a tangent-space normal map laid out like the texture with
  green up, replaces the sphere so smoke and debris show their shapes).
  receive_shadows false ignores the sun's shadow.
- light_count > 0 turns the emitter's brightest particles into point lights
  without shadows (light_intensity candela at full brightness, light_range
  metres, light_color_from_particle): embers and sparks light the floor and
  walls around them. A view draws at most 64 particle lights.
- sort_priority (integer, default 0) sets the draw order against other emitters,
  whatever their blend mode: lower draws first, higher composites on top; equal
  priorities draw back to front by distance. Typical layers: ground decals,
  scorch marks and floor rings -10, ground mist -5, fire 0, smoke over it 1,
  sparks and debris 2, flashes 3. sort_mode orders an emitter's own particles
  (automatic, oldest_first for trails of smoke, ...); sort_backend gpu | cpu
  picks where they are culled and sorted.

Cost:
- primitive octagon draws round textures (soft dots, smoke puffs) with about
  17% fewer pixels; primitive cutout draws an 8-sided polygon around each sheet
  frame's texels above cutout_alpha_threshold, baked from the texture (the
  same image, often half the fill for sparks, flames and debris);
  half_resolution draws large soft effects (smoke, fog, dust)
  at a quarter of the fill cost; write_velocity gives fast debris and sparks
  motion vectors so anti-aliasing keeps them crisp (one more draw).
- max_draw_distance (metres, 0 = no limit) culls a distant emitter, fading over
  draw_distance_fade; max_screen_size caps a particle's size next to the camera.
- culling_mode renderer_based (default) freezes an emitter no camera sees;
  always_simulate keeps it running.
- Project-wide quality: settings_set {"graphics.particles":{"spawn_scale":0.5,
  "sorting":true,"force_half_resolution":false,"min_screen_size":0,
  "max_drawn_particles":0,"max_particles_scale":1,"max_lighting":"surface"}}
  scales every rate and burst and every emitter's capacity, can skip the
  emitters smallest on screen, and caps the lighting (volumetric: surface
  lights as volumetric; unlit: lit particles draw unlit).

Preview and diagnose with particles_control_batch: emit a burst emitter's
count, play / stop a looping one (with_children for a whole effect), and read
num_particles and simulated per emitter.

Starting recipes: fire (circle, up, rate 70, life 1.0, additive, 8x8 flipbook,
intensity 8, scale shrinking 1.2 -> 0.35, light_count 1 for its glow); smoke
above it (rate 16, life 3, normal blend, lighting volumetric, half_resolution,
turbulence_strength 0.3, scale growing 1 -> 2.6, gray fading alpha); sparks
(sphere, up, start_speed 5-7, gravity_scale 1, collision bounce on
[[0,1,0,0]], small shrinking quads, additive, intensity 10); magic trail
(rate_over_distance 20 or render_mode ribbon on a moving entity).

## Physics

- body_type: static (immovable), kinematic (transform driven), dynamic
  (simulated). Dynamic bodies need is_using_gravity true and a mass.
- A freshly added Physics component has NO shapes, and a body without a shape
  collides with nothing - things fall straight through it. Always set shapes.
- shapes is an array: {type:"box",center,extends} | {type:"sphere",center,
  radius} | {type:"capsule"|"cylinder",center,radius,length} |
  {type:"mesh",center,mesh_asset,collision_type:"convex"|"concave"} (convex
  for dynamic bodies). Omitted fields take defaults: box extends [1,1,1] (FULL
  size - a unit cube), sphere radius 0.5 (a real radius - the 1 m sphere
  primitive). With is_autoscaled true (the default) they are multiplied by the
  entity scale, so a scaled cube primitive needs only [{"type":"box"}] and a
  sphere primitive [{"type":"sphere"}]. A shape written with its own type
  keeps the fields you leave out.
- For meshes, set center/extends from the model AABB (assets_get_mesh_info), or
  use a mesh shape.
- freeze_position / freeze_rotation are [x,y,z] booleans; include_layers /
  exclude_layers take layer names, indices or "everything" / "nothing".
- Nothing simulates in edit mode; test with play mode (verification skill).
- Domino-style chains: spacing must be less than tilt reach - a 1.4m tall
  domino tilted 30 degrees reaches about 0.7m.
- Surfaces: a physics material sets bounce (restitution 0..1) and grip
  (friction, 0 is ice). assets_create_batch {"type":"physics_material"} makes
  one, assets_list_properties {"type":"physics_material"} shows the ranges;
  assign it with property material on the Physics component. Bodies read it
  when they are built, so it shows in play mode.
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
a signage label (font_size is read-only while auto_size is on; auto_size_range
[min,max] bounds it then); area [width,height] is the wrap box in meters.
alignment is {horizontal: left|center|right, vertical: top|middle|bottom|
capline|midline|baseline} - name one to change only it. style holds the colors,
outline, shadow and style_flags (["underline","overline","strike_through",
"background","foreground"]). The readable side of the glyphs faces -Z at yaw 0:
a viewer standing on -Z reads it; rotate the entity 180 in yaw for viewers on
+Z.

## Reflection Probe / Volume / post effects

- Reflection Probe: probe {type, method, intensity, box_data {extents,
  transition_distance}, sphere_data {range}}. Type box with extents as HALF
  extents fitted to the room interior plus transition_distance for the blend
  skirt; type sphere uses range. update_mode once is right for static
  showcases; resolution res_16 ... res_1024.
- Volume: mode local + extents (full size) + blend_distance scopes co-located
  post components (e.g. Bloom) to a region; higher priority wins over the
  global volume. Put Volume and Bloom on the same entity, then raise bloom
  settings.intensity (global default 0.15; a strong local glow zone is
  0.6-1.0).
- Post effects (Bloom, GTAO, SSR, Temporal AA, Auto Exposure, Global
  Illumination, ...) have enabled and a nested settings object, as the
  Inspector groups them: {"settings":{"intensity":0.8}}.
- Camera: field_of_view, near_clip_distance, far_clip_distance,
  projection_mode, orthographic_size, include_layers / exclude_layers.
- Audio Source: clip, volume, pitch, loop, range [min,max], bus, priority,
  spatial; is_playing (read-only) tells whether it holds a voice in play mode.

## Do

- Read the schema (scene_list_components {component}) before writing values;
  enums are strings, not ints.
- Batch all property writes for one feature into one call (one undo step).

## Do not

- Do not set properties on a component that does not exist yet - the tool
  errors instead of adding it for you.
- Do not guess semantic fields (units, enum names); check the schema or read
  an existing configured entity with detail "components".
