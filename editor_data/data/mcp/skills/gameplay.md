---
name: gameplay
title: Gameplay recipes - player, pickups, spawning, score, scenes, sound
description: The patterns a game is made of, with the exact engine API for each - a walking and jumping player, trigger pickups, spawning and destroying from prefabs, a score on the HUD, scene changes, sound - and how to prove each one works with synthesized input. Read with scripting-api.
order: 75
---
# Gameplay recipes

Each recipe names the components (set through scene_set_component_properties_batch)
and the C# that drives them. Check exact signatures with scripting-api before writing.

## A player that walks and jumps

- Entity with Character Controller (not Physics - they exclude each other). Fit
  radius / height to the mesh; center lifts the capsule so its base sits at the
  entity origin (center y = height / 2).
- Script, every frame:

      var cc = owner.GetComponent<CharacterControllerComponent>();
      var input = new Vector3(Input.GetAxis("Horizontal"), 0.0f, Input.GetAxis("Vertical"));
      cc.Move(input * speed * Time.deltaTime);
      if (Input.IsPressed("Jump") && cc.isGrounded) { cc.Jump(jumpSpeed); }

- Horizontal / Vertical / Jump are in the default input map (W A S D, arrows,
  space). Add actions with input_map_action.
- Start from engine:/data/scripts/samples/SampleCharacterController.cs for camera-
  relative movement and turning.

## Pickups and trigger zones

- The pickup gets Physics with is_sensor true and a shape (a body with no shape
  touches nothing). The player needs a body too - a Character Controller counts.
- Sensor events are on by default. In the pickup's script:

      public override void OnSensorEnter(Collision collision)
      {
          if (collision.entity.tag == "Player") { Scene.DestroyEntity(owner); }
      }

- OnCollisionEnter / OnCollisionExit are the solid-contact versions.

## Spawning and destroying

- Build the thing once, save it with prefabs_create_from_entities_batch, then
  Scene.Instantiate("app:/data/Prefabs/Coin.pfb", position) at runtime.
- Scene.DestroyEntity(entity) removes it (an optional delay in seconds).

## Score and HUD

- A UI Document overlay (ui skill) with <span id="score">0</span>; the script that
  owns the score sets GetElementById("score").InnerRml.

## Moving between scenes

- Scene.LoadScene("app:/data/Level2.spfb"); Scene.ReloadScene() restarts the
  current one (losing, retrying).
- The built game starts in standalone.startup_scene (settings_set).

## Sound

- Audio Source component: clip (asset key), auto_play, loop, volume.
- Something must listen: the scene presets put an Audio Listener on the Main
  Camera. A scene built by hand needs one added, or audio is silent.

## Proving it works

For each recipe, a control case and a measurement:

1. Enter play mode (play_set_active {"active":true}) and wait until
   editor_get_status reports play.is_simulation_running.
2. Act: input_send_keys {"keys":["w"],"frames":60} walks for 60 frames,
   input_send_keys {"keys":["space"]} taps jump; input_send_mouse clicks.
3. Measure: scene_get_bounds_batch on the player before and after (moved?), Log
   lines from the script via logs_get_recent (picked up? scored?).
4. Control: the same input with the action unbound, or the sensor off, must change
   nothing.

A screenshot alone does not prove gameplay; a measured change against a control
does.
