#include "bindings/script_bindings.h"
#include "script_system.h"

#include <logging/logging.h>
#include <hpp/type_name.hpp>

namespace unravel
{

auto script_system::bind_internal_calls(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    register_core_runtime_script_bindings();
    register_scene_script_bindings();
    register_entity_script_bindings();
    register_transform_component_script_bindings();
    register_physics_component_script_bindings();
    register_character_controller_component_script_bindings();
    register_animation_component_script_bindings();
    register_camera_component_script_bindings();
    register_model_component_script_bindings();
    register_particle_emitter_component_script_bindings();
    register_text_component_script_bindings();
    register_light_component_script_bindings();
    register_skylight_component_script_bindings();
    register_reflection_probe_component_script_bindings();
    register_submesh_component_script_bindings();
    register_bone_component_script_bindings();
    register_volume_component_script_bindings();
    register_assets_script_bindings();
    register_math_script_bindings();
    register_gizmos_script_bindings();
    register_input_script_bindings();
    register_physics_script_bindings();
    register_ik_script_bindings();
    register_audio_source_component_script_bindings();
    register_ui_document_component_script_bindings();
    register_ui_document_script_bindings();
    register_ui_element_script_bindings();
    register_ui_event_script_bindings();

    return true;
}

} // namespace unravel
