#include "post_process_material_component.hpp"

#include <engine/rendering/material_graph/material_graph.h>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/material_instance.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
#include <serialization/types/vector.hpp>

namespace unravel
{

REFLECT(post_process_material_entry)
{
    entt::meta_factory<post_process_material_entry>{}
        .type("post_process_material_entry"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "post_process_material_entry"},
            entt::attribute{"pretty_name", "Post Process Material"},
        })
        .data<&post_process_material_entry::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Whether the material runs."},
        })
        .data<&post_process_material_entry::set_material, &post_process_material_entry::get_material>("material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material"},
            entt::attribute{"pretty_name", "Material"},
            entt::attribute{"material_domain", get_material_domain_key(material_domain::post_process)},
            entt::attribute{"tooltip", "A Post Process material: it runs at the Location its output names. Create one in the "
                                       "Content Browser (Create > Post Process Material)."},
        })
        .data<&post_process_material_entry::weight>("weight"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "weight"},
            entt::attribute{"pretty_name", "Weight"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"step", 0.01f},
            entt::attribute{"tooltip", "How strongly the material shows: it scales the material's Opacity, and a volume's "
                                       "contribution scales it again."},
        })
        .data<&post_process_material_entry::set_material_instance,
              static_cast<const material_instance& (post_process_material_entry::*)() const>(&post_process_material_entry::get_material_instance)>(
            "material_overrides"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "material_overrides"},
            entt::attribute{"pretty_name", "Material Overrides"},
            entt::attribute{"tooltip", "This entry's own values of the material's parameters and textures. Entries of one "
                                       "material in several volumes blend these values as the camera moves between them."},
        });
}

REFLECT(post_process_material_component)
{
    entt::meta_factory<post_process_material_component>{}
        .type("post_process_material_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "post_process_material_component"},
            entt::attribute{"category", "Rendering"},
            entt::attribute{"pretty_name", "Post Process Materials"},
        })
        .func<&component_meta<post_process_material_component>::exists>("component_exists"_hs)
        .func<&component_meta<post_process_material_component>::add>("component_add"_hs)
        .func<&component_meta<post_process_material_component>::remove>("component_remove"_hs)
        .func<&component_meta<post_process_material_component>::save>("component_save"_hs)
        .func<&component_meta<post_process_material_component>::load>("component_load"_hs)
        .data<&post_process_material_component::entries>("entries"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "entries"},
            entt::attribute{"pretty_name", "Materials"},
            entt::attribute{"tooltip", "The post-process materials this volume or camera runs, each a pass over the whole view "
                                       "at its material's Location. On a volume they fade with it; on a camera they always "
                                       "apply, after every volume's."},
            entt::attribute{"element_label", "Material"},
        });
}

SAVE(post_process_material_entry)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("material", obj.get_material()));
    try_save(ar, ser20::make_nvp("weight", obj.weight));
    try_save(ar, ser20::make_nvp("material_overrides", obj.get_material_instance()));
}
SAVE_INSTANTIATE(post_process_material_entry, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(post_process_material_entry, ser20::oarchive_binary_t);

LOAD(post_process_material_entry)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    asset_handle<material> material = obj.get_material();
    if(try_load(ar, ser20::make_nvp("material", material)))
    {
        obj.set_material(material);
    }
    try_load(ar, ser20::make_nvp("weight", obj.weight));
    // The overrides load onto an instance of the material just read, which tells their types.
    material_instance overrides = obj.get_material_instance();
    if(try_load(ar, ser20::make_nvp("material_overrides", overrides)))
    {
        obj.set_material_instance(overrides);
    }
}
LOAD_INSTANTIATE(post_process_material_entry, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(post_process_material_entry, ser20::iarchive_binary_t);

SAVE(post_process_material_component)
{
    try_save(ar, ser20::make_nvp("entries", obj.entries));
}
SAVE_INSTANTIATE(post_process_material_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(post_process_material_component, ser20::oarchive_binary_t);

LOAD(post_process_material_component)
{
    try_load(ar, ser20::make_nvp("entries", obj.entries));
}
LOAD_INSTANTIATE(post_process_material_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(post_process_material_component, ser20::iarchive_binary_t);

} // namespace unravel
