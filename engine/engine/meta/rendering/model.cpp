#include "model.hpp"
#include "material.hpp"
#include "material_instance.hpp"
#include "mesh.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/common/basetypes.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>
#include <serialization/types/chrono.hpp>

#include <algorithm>
#include <string>

namespace unravel
{
namespace
{
constexpr const char* MATERIAL_INSTANCES_NAME = "material_instances";

/// An older save: a whole material per slot. Its differences from the shared material become the instance's
/// overrides once the shared material is loaded (material_instance::set_legacy_copy).
template<typename Archive>
auto try_load_legacy_copies(Archive& ar, std::vector<material_instance>& instances) -> bool
{
    if constexpr(is_binary_archive<Archive>())
    {
        return false;
    }
    else
    {
        std::vector<material::sptr> copies;
        if(!try_load(ar, ser20::make_nvp(MATERIAL_INSTANCES_NAME, copies)))
        {
            return false;
        }
        instances.clear();
        instances.resize(copies.size());
        for(size_t i = 0; i < copies.size(); ++i)
        {
            if(copies[i])
            {
                instances[i].set_legacy_copy(copies[i]);
            }
        }
        return true;
    }
}

/// Whether the load leaves the slot's instance as it is: a prefab instance's override of that slot, or a load
/// limited to other paths. The path is the one the inspector records for the slot.
auto is_instance_slot_kept(size_t slot) -> bool
{
    const auto* path_ctx = serialization::get_path_context();
    if(path_ctx == nullptr || !path_ctx->is_recording())
    {
        return false;
    }
    serialization::path_segment_guard list_guard(MATERIAL_INSTANCES_NAME);
    serialization::path_segment_guard slot_guard("[" + std::to_string(slot) + "]");
    return !path_ctx->should_serialize_property(path_ctx->get_current_path());
}

/// The instances the document states, read over the live ones: a document without them, or a load that skips the
/// whole list, keeps the live list, and a slot kept on its own keeps its live instance whatever the document says.
template<typename Archive>
auto load_material_instances(Archive& ar, const std::vector<material_instance>& live) -> std::vector<material_instance>
{
    auto instances = live;
    if(!try_load(ar, ser20::make_nvp(MATERIAL_INSTANCES_NAME, instances)))
    {
        instances = live;
        if(!try_load_legacy_copies(ar, instances))
        {
            instances = live;
        }
    }
    for(size_t slot = 0; slot < live.size(); ++slot)
    {
        if(is_instance_slot_kept(slot))
        {
            instances.resize(std::max(instances.size(), slot + 1));
            instances[slot] = live[slot];
        }
    }
    return instances;
}
} // namespace

REFLECT(model)
{
    auto is_lod_override_enabled_predicate = entt::property_predicate<bool>(
        [](const entt::meta_any& i)    
        {
            return i.try_cast<model>()->get_lod_override_enabled();
        });


    entt::meta_factory<model>{}
        .type("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
        })
        .data<&model::set_shared_materials, &model::get_shared_materials>("materials"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "materials"},
            entt::attribute{"pretty_name", "Shared Materials"},
            entt::attribute{"tooltip", "The shared material of each slot: editing one changes every model that uses it."},
        })
        .data<&model::set_material_instances, &model::get_material_instances>("material_instances"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_instances"},
            entt::attribute{"pretty_name", "Material Instances"},
            entt::attribute{"tooltip", "This model's overrides of each slot's shared material. A value that is not overridden follows the shared material."},
            // One per slot: the shared materials decide how many there are.
            entt::attribute{"is_fixed_size_array", true},
            entt::attribute{"element_label", "Slot"},
        })
        .data<&model::set_lods, &model::get_lods>("lods"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lods"},
            entt::attribute{"pretty_name", "LOD"},
            entt::attribute{"tooltip", "Explicit Levels of Detail. If not set, the model will use\n"
                    "the automatically generated mesh LODs (if any) based on the screen size."},
        })
        .data<&model::set_lod_override_enabled, &model::get_lod_override_enabled>("lod_override_enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_override_enabled"},
            entt::attribute{"pretty_name", "LOD Override"},
            entt::attribute{"tooltip", "Enable to force a specific LOD level regardless of distance."},
        })
        .data<&model::set_lod_override_level, &model::get_lod_override_level>("lod_override_level"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_override_level"},
            entt::attribute{"pretty_name", "Override Level"},
            entt::attribute{"tooltip", "The LOD level to use when override is enabled."},
            entt::attribute{"predicate", is_lod_override_enabled_predicate},
            entt::attribute{"min", 0},
            entt::attribute{"max", mesh::get_max_generated_lod_count()},
        })
        .data<&model::set_lod_selection_bias, &model::get_lod_selection_bias>("lod_selection_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_selection_bias"},
            entt::attribute{"pretty_name", "LOD Selection Bias"},
            entt::attribute{"tooltip", "The value added to the calculated LOD index. Increasing this value results in selecting less detailed LODs, reducing the value - in more detailed LODs."},
            entt::attribute{"readonly_predicate", is_lod_override_enabled_predicate},
            entt::attribute{"min", -float(mesh::get_max_generated_lod_count())},
            entt::attribute{"max", float(mesh::get_max_generated_lod_count())},
        })
        .data<&model::set_lod_transition_time, &model::get_lod_transition_time>("lod_transition_time"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "lod_transition_time"},
            entt::attribute{"pretty_name", "LOD Transition Time"},
            entt::attribute{"tooltip", "Duration of LOD transitions in seconds (0 = instant switch, >0 = smooth dithered crossfade)."},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
        });
}

SAVE(model)
{
    try_save(ar, ser20::make_nvp("lods", obj.mesh_lods_));
    try_save(ar, ser20::make_nvp("materials", obj.materials_));
    // Up to the last slot with overrides: a model without any saves an empty list.
    auto last = obj.material_instances_.size();
    while(last > 0 && !obj.material_instances_[last - 1].has_overrides())
    {
        --last;
    }
    std::vector<material_instance> instances(obj.material_instances_.begin(),
                                             obj.material_instances_.begin() + std::ptrdiff_t(last));
    try_save(ar, ser20::make_nvp(MATERIAL_INSTANCES_NAME, instances));
    try_save(ar, ser20::make_nvp("lod_override_enabled", obj.lod_override_enabled_));
    try_save(ar, ser20::make_nvp("lod_override_level", obj.lod_override_level_));
    try_save(ar, ser20::make_nvp("lod_selection_bias", obj.lod_selection_bias_));
    try_save(ar, ser20::make_nvp("lod_transition_time", obj.lod_transition_time_));
}
SAVE_INSTANTIATE(model, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model, ser20::oarchive_binary_t);

LOAD(model)
{
    std::vector<asset_handle<mesh>> lods = obj.get_lods();
    try_load(ar, ser20::make_nvp("lods", lods));
    obj.set_lods(lods);
    try_load(ar, ser20::make_nvp("materials", obj.materials_));
    obj.material_instances_ = load_material_instances(ar, obj.material_instances_);
    obj.sync_material_instances();
    try_load(ar, ser20::make_nvp("lod_override_enabled", obj.lod_override_enabled_));
    try_load(ar, ser20::make_nvp("lod_override_level", obj.lod_override_level_));
    try_load(ar, ser20::make_nvp("lod_selection_bias", obj.lod_selection_bias_));
    try_load(ar, ser20::make_nvp("lod_transition_time", obj.lod_transition_time_));
}
LOAD_INSTANTIATE(model, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model, ser20::iarchive_binary_t);
} // namespace unravel
