#pragma once

namespace unravel
{

void register_core_runtime_script_bindings();
void register_scene_script_bindings();
void register_entity_script_bindings();
void register_transform_component_script_bindings();
void register_physics_component_script_bindings();
void register_character_controller_component_script_bindings();
void register_animation_component_script_bindings();
void register_camera_component_script_bindings();
void register_model_component_script_bindings();
void register_particle_emitter_component_script_bindings();
void register_text_component_script_bindings();
void register_light_component_script_bindings();
void register_skylight_component_script_bindings();
void register_reflection_probe_component_script_bindings();
void register_submesh_component_script_bindings();
void register_bone_component_script_bindings();
void register_volume_component_script_bindings();
void register_audio_source_component_script_bindings();
void register_ui_document_component_script_bindings();
void register_ui_document_script_bindings();
void register_ui_element_script_bindings();
void register_ui_event_script_bindings();
void register_assets_script_bindings();
void register_input_script_bindings();
void register_physics_script_bindings();
void register_ik_script_bindings();
void register_gizmos_script_bindings();
void register_math_script_bindings();

} // namespace unravel
