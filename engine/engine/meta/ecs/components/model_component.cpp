#include "model_component.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/mesh.hpp>
#include <engine/meta/rendering/model.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/set.hpp>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(model_component)
{
    entt::meta_factory<model_component>{}
        .type("model_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Model"},
        })
        .func<&component_meta<model_component>::exists>("component_exists"_hs)
        .func<&component_meta<model_component>::add>("component_add"_hs)
        .func<&component_meta<model_component>::remove>("component_remove"_hs)
        .func<&component_meta<model_component>::save>("component_save"_hs)
        .func<&component_meta<model_component>::load>("component_load"_hs)
        .data<&model_component::set_enabled, &model_component::is_enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Is the model visible?"},
        })
        .data<&model_component::set_static, &model_component::is_static>("static"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "static"},
            entt::attribute{"pretty_name", "Static"},
            entt::attribute{"tooltip", "Is the model static?"},
        })
        .data<&model_component::set_casts_shadow, &model_component::casts_shadow>("casts_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip", "Is the model casting shadows?"},
        })
        .data<nullptr, &model_component::get_world_bounds>("world_bounds"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_bounds"},
            entt::attribute{"pretty_name", "World Bounds"},
            entt::attribute{"tooltip", "The world bounds of the model."},
        })
        .data<&model_component::set_model, &model_component::get_model>("model"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "model"},
            entt::attribute{"pretty_name", "Model"},
        });
}

SAVE(model_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("static", obj.is_static()));
    try_save(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow()));
    try_save(ar, ser20::make_nvp("model", obj.get_model()));
}
SAVE_INSTANTIATE(model_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(model_component, ser20::oarchive_binary_t);

LOAD(model_component)
{
    bool is_enabled{true};
    if(try_load(ar, ser20::make_nvp("enabled", is_enabled)))
    {
        obj.set_enabled(is_enabled);
    }

    bool is_static{};
    if(try_load(ar, ser20::make_nvp("static", is_static)))
    {
        obj.set_static(is_static);
    }

    bool casts_shadow{};
    if(try_load(ar, ser20::make_nvp("casts_shadow", casts_shadow)))
    {
        obj.set_casts_shadow(casts_shadow);
    }

    auto mod = obj.get_model();
    if(try_load(ar, ser20::make_nvp("model", mod)))
    {
        obj.set_model(mod);
    }
}
LOAD_INSTANTIATE(model_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(model_component, ser20::iarchive_binary_t);

REFLECT(bone_component)
{
    entt::meta_factory<bone_component>{}
        .type("bone_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Bone"},
        })
        .func<&component_meta<bone_component>::exists>("component_exists"_hs)
        .func<&component_meta<bone_component>::add>("component_add"_hs)
        .func<&component_meta<bone_component>::save>("component_save"_hs)
        .func<&component_meta<bone_component>::load>("component_load"_hs)
        .func<&component_meta<bone_component>::remove>("component_remove"_hs)
        .data<nullptr, &bone_component::bone_index>("bone_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bone_index"},
            entt::attribute{"pretty_name", "Bone Index"},
            entt::attribute{"tooltip", "The bone index this object represents."},
        });
}

SAVE(bone_component)
{
    try_save(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
SAVE_INSTANTIATE(bone_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(bone_component, ser20::oarchive_binary_t);

LOAD(bone_component)
{
    try_load(ar, ser20::make_nvp("bone_index", obj.bone_index));
}
LOAD_INSTANTIATE(bone_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(bone_component, ser20::iarchive_binary_t);

REFLECT(submesh_entry)
{
    entt::meta_factory<submesh_entry>{}
        .type("submesh_entry"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_entry"},
            entt::attribute{"pretty_name", "Submesh Entry"},
        })
        .data<nullptr, &submesh_entry::submesh_index>("submesh_index"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_index"},
            entt::attribute{"pretty_name", "Submesh Index"},
            entt::attribute{"tooltip", "Index of the submesh in the mesh asset."},
        })
        .data<&submesh_entry::material_override>("material_override"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_override"},
            entt::attribute{"pretty_name", "Material Override"},
            entt::attribute{"tooltip", "Overrides the model material for this submesh. Leave empty to use the model material."},
        })
        .data<&submesh_entry::casts_shadow>("casts_shadow"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "casts_shadow"},
            entt::attribute{"pretty_name", "Casts Shadow"},
            entt::attribute{"tooltip", "Whether this submesh casts shadows."},
        })
        .data<&submesh_entry::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Whether this submesh is rendered."},
        });
}

REFLECT(submesh_component)
{
    entt::meta_factory<submesh_component>{}
        .type("submesh_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "submesh_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Submesh"},
        })
        .func<&component_meta<submesh_component>::exists>("component_exists"_hs)
        .func<&component_meta<submesh_component>::add>("component_add"_hs)
        .func<&component_meta<submesh_component>::remove>("component_remove"_hs)
        .func<&component_meta<submesh_component>::save>("component_save"_hs)
        .func<&component_meta<submesh_component>::load>("component_load"_hs)
        .data<&submesh_component::entries>("entries"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entries"},
            entt::attribute{"pretty_name", "Entries"},
            entt::attribute{"tooltip", "Per-submesh render settings (material override, shadows, visibility)."},
            // The mesh asset dictates which submeshes exist; users edit entry settings only.
            entt::attribute{"is_fixed_size_array", true},
        });
}

SAVE(submesh_entry)
{
    try_save(ar, ser20::make_nvp("submesh_index", obj.submesh_index));
    try_save(ar, ser20::make_nvp("stable_id", obj.stable_id));
    try_save(ar, ser20::make_nvp("material_override", obj.material_override));
    try_save(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow));
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
}
SAVE_INSTANTIATE(submesh_entry, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(submesh_entry, ser20::oarchive_binary_t);

LOAD(submesh_entry)
{
    try_load(ar, ser20::make_nvp("submesh_index", obj.submesh_index));
    try_load(ar, ser20::make_nvp("stable_id", obj.stable_id));
    try_load(ar, ser20::make_nvp("material_override", obj.material_override));
    try_load(ar, ser20::make_nvp("casts_shadow", obj.casts_shadow));
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
}
LOAD_INSTANTIATE(submesh_entry, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(submesh_entry, ser20::iarchive_binary_t);

SAVE(submesh_component)
{
    try_save(ar, ser20::make_nvp("entries", obj.entries));
}
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(submesh_component, ser20::oarchive_binary_t);

LOAD(submesh_component)
{
    try_load(ar, ser20::make_nvp("entries", obj.entries));

    // Legacy data only serialized the bare index vector - synthesize default entries.
    std::vector<uint32_t> legacy_indices;
    try_load(ar, ser20::make_nvp("submeshes", legacy_indices));
    obj.migrate_legacy_indices(legacy_indices);
}
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(submesh_component, ser20::iarchive_binary_t);

} // namespace unravel
