#include "ui_document_component.hpp"
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/assets/asset_handle.hpp>

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{
REFLECT(ui_document_component)
{
    // Register ui_document_component class with entt
    entt::meta_factory<ui_document_component>{}
        .type("ui_document_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ui_document_component"},
            entt::attribute{"category", "UI"},
            entt::attribute{"pretty_name", "UI Document"},
        })
        .func<&component_meta<ui_document_component>::exists>("component_exists"_hs)
        .func<&component_meta<ui_document_component>::add>("component_add"_hs)
        .func<&component_meta<ui_document_component>::remove>("component_remove"_hs)
        .func<&component_meta<ui_document_component>::save>("component_save"_hs)
        .func<&component_meta<ui_document_component>::load>("component_load"_hs)
        .data<&ui_document_component::asset>("asset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "asset"},
            entt::attribute{"pretty_name", "Asset"},
        })
        ;
}

SAVE(ui_document_component)
{
    try_save(ar, ser20::make_nvp("asset", obj.asset));

}
SAVE_INSTANTIATE(ui_document_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ui_document_component, ser20::oarchive_binary_t);

LOAD(ui_document_component)
{
    try_load(ar, ser20::make_nvp("asset", obj.asset));
}
LOAD_INSTANTIATE(ui_document_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ui_document_component, ser20::iarchive_binary_t);

} // namespace unravel
