#pragma once


#include "audio_listener_component.hpp"
#include "audio_source_component.hpp"
#include "camera_component.hpp"
#include "id_component.hpp"
#include "tag_component.hpp"
#include "layer_component.hpp"
#include "light_component.hpp"
#include "model_component.hpp"
#include "animation_component.hpp"
#include "physics_component.hpp"
#include "character_controller_component.hpp"
#include "prefab_component.hpp"
#include "reflection_probe_component.hpp"
#include "volume_component.hpp"
#include "test_component.hpp"
#include "transform_component.hpp"
#include "script_component.hpp"
#include "auto_exposure_component.hpp"
#include "bloom_component.hpp"
#include "tonemapping_component.hpp"
#include "fxaa_component.hpp"
#include "taa_component.hpp"
#include "assao_component.hpp"
#include "ssr_component.hpp"
#include "ssil_component.hpp"
#include "gtao_component.hpp"
#include "gi_component.hpp"
#include "text_component.hpp"
#include "particle_emitter_component.hpp"
#include "../../ui/ecs/components/ui_document_component.hpp"
#include <tuple>

namespace unravel
{

using all_serializeable_components = std::tuple<
    id_component,
    tag_component,
    layer_component,
    prefab_component,
    prefab_id_component,
    transform_component,
    test_component,
    model_component,
    animation_component,
    bone_component,
    submesh_component,
    camera_component,
    volume_component,
    auto_exposure_component,
    tonemapping_component,
    assao_component,
    bloom_component,
    fxaa_component,
    taa_component,
    ssr_component,
    ssil_component,
    gtao_component,
    gi_component,
    light_component,
    skylight_component,
    reflection_probe_component,
    physics_component,
    character_controller_component,
    audio_source_component,
    audio_listener_component,
    text_component,
    particle_emitter_component,
    ui_document_component,
    script_component
    >;

using all_inspectable_components = std::tuple<
    id_component,
    tag_component,
    layer_component,
    prefab_component,
    prefab_id_component,
    transform_component,
    test_component,
    model_component,
    animation_component,
    bone_component,
    submesh_component,
    camera_component,
    volume_component,
    auto_exposure_component,
    tonemapping_component,
    assao_component,
    bloom_component,
    fxaa_component,
    taa_component,
    ssr_component,
    ssil_component,
    gtao_component,
    gi_component,
    light_component,
    skylight_component,
    reflection_probe_component,
    physics_component,
    character_controller_component,
    audio_source_component,
    audio_listener_component,
    text_component,
    particle_emitter_component,
    ui_document_component
    >;

using all_addable_components = std::tuple<
    test_component,
    model_component,
    animation_component,
    camera_component,
    volume_component,
    auto_exposure_component,
    tonemapping_component,
    assao_component,
    bloom_component,
    fxaa_component,
    taa_component,
    ssr_component,
    ssil_component,
    gtao_component,
    gi_component,
    light_component,
    skylight_component,
    reflection_probe_component,
    physics_component,
    character_controller_component,
    audio_source_component,
    audio_listener_component,
    text_component,
    particle_emitter_component,
    ui_document_component
    >;


} // namespace unravel
