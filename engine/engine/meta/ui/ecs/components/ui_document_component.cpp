#include "ui_document_component.hpp"
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/common/basetypes.hpp>
#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

#include <type_traits>

namespace unravel
{
REFLECT(ui_document_component)
{
    auto world_space_predicate_entt = entt::property_predicate<bool>([](const entt::meta_any& obj) -> bool
    {
        auto* data = obj.try_cast<ui_document_component>();
        return data && data->render_mode == ui_render_mode::world_space;
    });

    entt::meta_factory<ui_render_mode>{}
        .type("ui_render_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ui_render_mode"},
            entt::attribute{"pretty_name", "UI Render Mode"},
        })
        .data<ui_render_mode::screen_space_overlay>("screen_space_overlay"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "screen_space_overlay"},
            entt::attribute{"pretty_name", "Screen Space Overlay"},
        })
        .data<ui_render_mode::world_space>("world_space"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "world_space"},
            entt::attribute{"pretty_name", "World Space"},
        });

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
        .data<&ui_document_component::set_enabled, &ui_document_component::is_enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
        })
        .data<&ui_document_component::asset>("asset"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "asset"},
            entt::attribute{"pretty_name", "Asset"},
        })
        .data<&ui_document_component::render_mode>("render_mode"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "render_mode"},
            entt::attribute{"pretty_name", "Render Mode"},
            entt::attribute{"tooltip", "Screen-space overlay or world-space 3D quad"},
        })
        .data<&ui_document_component::size>("size"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "size"},
            entt::attribute{"pretty_name", "Size"},
            entt::attribute{"group", "World Space Units"},
            entt::attribute{"tooltip", "Document resolution (pixels). For world-space only."},
            entt::attribute{"predicate", world_space_predicate_entt},
        })
        .data<&ui_document_component::pixels_per_world_unit>("pixels_per_world_unit"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "pixels_per_world_unit"},
            entt::attribute{"pretty_name", "Pixels per World Unit"},
            entt::attribute{"group", "World Space Units"},
            entt::attribute{"tooltip", "Pixels per world unit for world-space rendering."},
            entt::attribute{"predicate", world_space_predicate_entt},
        });
}

SAVE(ui_document_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.is_enabled()));
    try_save(ar, ser20::make_nvp("asset", obj.asset));
    try_save(ar, ser20::make_nvp("render_mode", obj.render_mode));
    try_save(ar, ser20::make_nvp("size", obj.size));
    try_save(ar, ser20::make_nvp("pixels_per_world_unit", obj.pixels_per_world_unit));
}
SAVE_INSTANTIATE(ui_document_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(ui_document_component, ser20::oarchive_binary_t);

LOAD(ui_document_component)
{
    bool enabled = true;
    if(try_load(ar, ser20::make_nvp("enabled", enabled)))
    {
        obj.set_enabled(enabled);
    }
    try_load(ar, ser20::make_nvp("asset", obj.asset));
    try_load(ar, ser20::make_nvp("render_mode", obj.render_mode));
    try_load(ar, ser20::make_nvp("size", obj.size));
    try_load(ar, ser20::make_nvp("pixels_per_world_unit", obj.pixels_per_world_unit));
}
LOAD_INSTANTIATE(ui_document_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(ui_document_component, ser20::iarchive_binary_t);

} // namespace unravel
