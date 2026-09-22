#include "inspector_assets.h"
#include "inspector_asset_picker.h"
#include <editor/editing/authoring_root.h>
#include "inspectors.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>
#include <engine/ui/style_sheet.h>
#include <engine/ui/ui_tree.h>


#include <engine/ecs/components/prefab_component.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/texture.hpp>
#include <engine/meta/ui/style_sheet.hpp>
#include <engine/meta/ui/ui_tree.hpp>
#include <engine/rendering/font.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>


#include <editor/assets/asset_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/thumbnail_manager.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

#include <filesystem/filesystem.h>
#include <algorithm>
#include <type_traits>
#include <graphics/texture.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/sequencer/imgui_neo_sequencer.h>
#include <logging/logging.h>

namespace unravel
{
namespace
{
template<typename T>
auto process_drag_drop_target(asset_manager& am, asset_handle<T>& entry) -> bool
{
    const auto& formats = ex::get_suported_formats<T>();
    asset_picker::draw_drop_highlight(std::any_of(formats.begin(),
                                                  formats.end(),
                                                  [](const std::string& type)
                                                  {
                                                      return ImGui::IsDragDropPossibleTargetForType(type.c_str());
                                                  }));

    bool result = false;
    if(ImGui::BeginDragDropTarget())
    {
        if(ImGui::IsDragDropPayloadBeingAccepted())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        else
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        }

        for(const auto& type : ex::get_suported_formats<T>())
        {
            auto payload = ImGui::AcceptDragDropPayload(type.c_str());
            if(payload)
            {
                std::string absolute_path(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));

                std::string key = fs::convert_to_protocol(fs::path(absolute_path)).generic_string();
                const auto& entry_future = am.template find_asset<T>(key);
                if(entry_future.is_ready())
                {
                    entry = entry_future;
                }

                if(entry.is_valid())
                {
                    result = true;
                    break;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    return result;
}

/// The icon of an asset type, for a field or a picker tile without a thumbnail.
template<typename T>
auto get_asset_icon() -> const char*
{
    if constexpr(std::is_same_v<T, gfx::texture>)
    {
        return ICON_MDI_IMAGE_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, material>)
    {
        return ICON_MDI_CIRCLE_HALF_FULL;
    }
    else if constexpr(std::is_same_v<T, mesh>)
    {
        return ICON_MDI_CUBE_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, animation_clip>)
    {
        return ICON_MDI_ANIMATION_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, prefab>)
    {
        return ICON_MDI_PACKAGE_VARIANT_CLOSED;
    }
    else if constexpr(std::is_same_v<T, scene_prefab>)
    {
        return ICON_MDI_MAP_OUTLINE;
    }
    else if constexpr(std::is_same_v<T, physics_material>)
    {
        return ICON_MDI_ATOM;
    }
    else if constexpr(std::is_same_v<T, audio_clip>)
    {
        return ICON_MDI_MUSIC_NOTE;
    }
    else if constexpr(std::is_same_v<T, font>)
    {
        return ICON_MDI_FORMAT_FONT;
    }
    else if constexpr(std::is_same_v<T, style_sheet>)
    {
        return ICON_MDI_LANGUAGE_CSS3;
    }
    else
    {
        return ICON_MDI_FILE_DOCUMENT_OUTLINE;
    }
}

/// The thumbnail of an asset, the icon of its type when it has none, a clock while it loads.
template<typename T>
auto make_preview(thumbnail_manager& tm, const asset_handle<T>& asset) -> asset_picker::preview
{
    asset_picker::preview result{};
    result.icon = get_asset_icon<T>();
    if(!asset)
    {
        return result;
    }
    // Asking for the thumbnail is also what starts loading an asset nobody uses yet.
    const auto& thumbnail = tm.get_thumbnail(asset);
    if(!asset.is_ready())
    {
        result.is_loading = true;
        return result;
    }
    result.texture = ImGui::ToId(thumbnail);
    result.texture_size = ImGui::GetSize(thumbnail);
    return result;
}

/// What the picker offers: the empty handle first (get_assets_with_predicate always puts it
/// there), then the assets of the project that pass the search.
template<typename T>
auto find_pickable_assets(asset_manager& am, const ImGuiTextFilter& filter) -> std::vector<asset_handle<T>>
{
    return am.get_assets_with_predicate<T>(
        [&](const auto& asset)
        {
            const auto& id = asset.id();
            hpp::string_view id_view(id);
            return !id_view.starts_with("editor:/") && filter.PassFilter(asset.name().c_str());
        });
}

//-----------------------------------------------------------------------------
/// <summary>
/// The field of an asset property: thumbnail, name and the buttons under it, a drop target for
/// assets of the type, and the picker window it opens. The first tile of the picker, the empty
/// handle, clears the property.
/// </summary>
//-----------------------------------------------------------------------------
template<typename T>
auto pick_asset(ImGuiTextFilter& filter,
                editing_manager& em,
                thumbnail_manager& tm,
                asset_manager& am,
                asset_handle<T>& data,
                const std::string& type) -> inspect_result
{
    inspect_result result{};

    asset_picker::field_content content{};
    content.thumbnail = make_preview(tm, data);
    content.type = type;
    if(data)
    {
        content.name = data.name();
        content.path = data.id();
    }
    const asset_picker::field_request request = asset_picker::draw_field(content);

    const bool is_dropped = process_drag_drop_target(am, data);
    result.changed |= is_dropped;
    result.edit_finished |= is_dropped;

    if(request.is_locate_pressed)
    {
        em.foucs_asset(data);
    }
    if(request.is_clear_pressed && data)
    {
        data = asset_handle<T>::get_empty();
        result.changed = true;
        result.edit_finished = true;
    }

    const std::string popup_id = fmt::format("Pick {}", type);
    if(request.is_pick_pressed)
    {
        filter.Clear();
        ImGui::OpenPopup(popup_id.c_str());
    }
    if(!asset_picker::begin_window(popup_id.c_str(), get_asset_icon<T>(), fmt::format("Select {}", type), filter))
    {
        return result;
    }

    const auto assets = find_pickable_assets<T>(am, filter);
    const auto get_tile = [&](std::size_t index) -> asset_picker::tile_content
    {
        const auto& asset = assets[index];
        asset_picker::tile_content tile{};
        if(!asset)
        {
            tile.thumbnail.icon = ICON_MDI_CANCEL;
            tile.name = "None";
            tile.is_selected = !data;
            return tile;
        }
        tile.thumbnail = make_preview(tm, asset);
        tile.name = asset.name();
        tile.is_selected = data && asset.uid() == data.uid();
        return tile;
    };
    const asset_picker::grid_result grid = asset_picker::draw_grid(assets.size(), get_tile);
    if(grid.picked >= 0)
    {
        data = assets[static_cast<std::size_t>(grid.picked)];
        result.changed = true;
        result.edit_finished = true;
        ImGui::CloseCurrentPopup();
    }

    // Not counting the empty handle.
    const std::size_t asset_count = assets.empty() ? 0 : assets.size() - 1;
    std::string footer = fmt::format("{} {}", asset_count, asset_count == 1 ? "asset" : "assets");
    if(grid.hovered >= 0)
    {
        const auto& hovered = assets[static_cast<std::size_t>(grid.hovered)];
        footer = hovered ? hovered.id() : fmt::format("Clears the {}", type);
    }
    asset_picker::draw_footer(footer);
    asset_picker::end_window();
    return result;
}

template<typename T>
auto make_asset_instance_proxy(entt::meta_any& var, const meta_any_proxy& var_proxy) -> meta_any_proxy
{
    meta_any_proxy data_var_proxy;
    data_var_proxy.impl->parent = var_proxy.impl;
    data_var_proxy.impl->type_name = entt::get_pretty_name(var.type());
    data_var_proxy.impl->name = var_proxy.impl->name;
    data_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto data = var.cast<std::shared_ptr<T>>();
            result = entt::forward_as_meta(*data);
            return true;
        }
        return false;
    };
    data_var_proxy.impl->setter =
        [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            var.assign(value);
            return parent_proxy.impl->setter(parent_proxy, var, execution_count);
        }
        return false;
    };
    return data_var_proxy;
}

template<typename T>
auto make_asset_proxy(entt::meta_any& var, const meta_any_proxy& var_proxy) -> meta_any_proxy
{
    auto& data = var.cast<asset_handle<T>&>();
    meta_any_proxy data_var_proxy;
    data_var_proxy.impl->parent = var_proxy.impl;
    data_var_proxy.impl->type_name = entt::get_pretty_name(var.type());
    data_var_proxy.impl->name = var_proxy.impl->name;
    data_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto& data = var.cast<asset_handle<T>&>();
            if(data)
            {
                auto mat = data.get(false);
                if(mat)
                {
                    result = entt::forward_as_meta(*mat);
                    return true;
                }
            }
        }
        return false;
    };
    data_var_proxy.impl->setter =
        [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        // entt::meta_any var;
        // proxy.impl->getter(var);
        // if(var)
        // {
        //     var = value;
        //     parent_proxy.impl->setter(parent_proxy, var, execution_count);
        // }
        return false;
    };
    return data_var_proxy;
}

template<typename T>
auto make_mutable_asset_proxy(entt::meta_any& var, const meta_any_proxy& var_proxy) -> meta_any_proxy
{
    auto& data = var.cast<asset_handle<T>&>();
    meta_any_proxy data_var_proxy;
    data_var_proxy.impl->parent = var_proxy.impl;
    data_var_proxy.impl->type_name = entt::get_pretty_name(var.type());
    data_var_proxy.impl->name = var_proxy.impl->name;
    data_var_proxy.impl->getter = [parent_proxy = var_proxy](entt::meta_any& result)
    {
        entt::meta_any var;
        if(parent_proxy.impl->getter(var) && var)
        {
            auto& data = var.cast<asset_handle<T>&>();
            if(data)
            {
                auto mat = data.get(false);
                if(mat)
                {
                    result = entt::forward_as_meta(*mat);
                    return true;
                }
            }
        }
        return false;
    };

    data_var_proxy.impl->setter = [data, parent_proxy = var_proxy](meta_any_proxy& proxy,
                                                                   const entt::meta_any& value,
                                                                   uint64_t execution_count) mutable
    {
        entt::meta_any var;
        if(proxy.impl->getter(var) && var)
        {
            var.assign(value);
            parent_proxy.impl->setter(parent_proxy, var, execution_count);

            // Get the asset and mutate it
            auto data_asset = data.get(true);
            if(data_asset)
            {
                *data_asset = var.cast<T&>();
            }

            if(execution_count > 1)
            {
                // Do this after the setter is called to ensure the asset is mutable
                auto& ctx = engine::context();
                auto& tm = ctx.get_cached<thumbnail_manager>();
                tm.regenerate_thumbnail(data.uid());
                asset_writer::atomic_save_to_file(data.id(), data);
            }

            return true;
        }
        return false;
    };

    return data_var_proxy;
}

} // namespace

void inspector_asset_handle_texture::draw_image(const asset_handle<gfx::texture>& data, ImVec2 size)
{
    if(data.is_ready())
    {
        auto sz = ImGui::GetSize(data, size);
        ImGui::ImageWithAspect(ImGui::ToId(data, inspected_mip_), sz, size);

        const auto tex = data.get(false);
        if(tex)
        {
            if(tex->info.numMips > 1)
            {
                ImGui::KnobSliderScalarT("Mip", &inspected_mip_, 0, tex->info.numMips - 1);
            }
        }
        return;
    }

    ImGui::Dummy(size);
    ImGui::RenderFrameBorder(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

auto inspector_asset_handle_texture::inspect_as_property(rtti::context& ctx, asset_handle<gfx::texture>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<gfx::texture>());

    return result;
}

auto inspector_asset_handle_texture::inspect(rtti::context& ctx,
                                             entt::meta_any& var,
                                             const meta_any_proxy& var_proxy,
                                             const var_info& info,
                                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<gfx::texture>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    bool changed = false;
    if(inspected_asset_ != data || inspected_version_ != data.version())
    {
        inspected_asset_ = data;
        inspected_version_ = data.version();
        importer_ = nullptr;
        inspected_mip_ = 0;
    }

    auto& am = ctx.get_cached<unravel::asset_manager>();
    inspect_result result{};

    auto available = ImGui::GetContentRegionAvail();

    if(ImGui::BeginTabBar("asset_handle_texture",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::BeginChild(ex::get_type(data.extension()).c_str());

            draw_image(data, available);

            if(data.is_ready())
            {
                var_info tex_var_info;
                tex_var_info.read_only = true;
                tex_var_info.is_copyable = false;

                auto tex_var_proxy = make_asset_proxy<gfx::texture>(var, var_proxy);

                entt::meta_any tex_var;
                if(tex_var_proxy.impl->getter(tex_var))
                {
                    result |= ::unravel::inspect_var(ctx, tex_var, tex_var_proxy, tex_var_info);
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Import"))
        {
            auto meta = am.get_metadata(data.uid());

            auto base_importer = meta.meta.importer;

            auto importer = std::static_pointer_cast<texture_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<texture_importer_meta>(*importer);
                }

                result |= ::unravel::inspect(ctx, *importer_);
            }

            if(ImGui::Button("Revert"))
            {
                importer_ = {};
            }
            ImGui::SameLine();
            if(ImGui::Button("Apply"))
            {
                if(importer_)
                {
                    *importer = *importer_;
                }

                auto meta_absolute_path = asset_writer::resolve_meta_file(data);
                asset_writer::atomic_save_to_file(meta_absolute_path.string(), meta.meta);
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    return result;
}

auto inspector_asset_handle_material::inspect_as_property(rtti::context& ctx, asset_handle<material>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};

    result |= pick_asset(filter, em, tm, am, data, ex::get_type<material>());

    return result;
}

auto inspector_asset_handle_material::inspect(rtti::context& ctx,
                                              entt::meta_any& var,
                                              const meta_any_proxy& var_proxy,
                                              const var_info& info,
                                              const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<material>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    if(data.is_ready())
    {
        auto data_var_proxy = make_mutable_asset_proxy<material>(var, var_proxy);

        entt::meta_any data_var;
        if(data_var_proxy.impl->getter(data_var))
        {
            result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy);
        }

        if(result.changed)
        {
            auto& tm = ctx.get_cached<thumbnail_manager>();
            tm.regenerate_thumbnail(data.uid());
        }
    }
    if(result.edit_finished)
    {
        asset_writer::atomic_save_to_file(data.id(), data);
    }

    return result;
}

auto inspector_shared_material::inspect(rtti::context& ctx,
                                        entt::meta_any& var,
                                        const meta_any_proxy& var_proxy,
                                        const var_info& info,
                                        const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<std::shared_ptr<material>&>();

    inspect_result result{};
    {
        if(data)
        {
            if(ImGui::Button(ICON_MDI_DELETE))
            {
                data.reset();
                result.changed = true;
                result.edit_finished = true;
            }
            ImGui::SameLine();
            if(ImGui::TreeNodeEx("Material Instance", ImGuiTreeNodeFlags_AllowOverlap))
            {
                auto data_var_proxy = make_asset_instance_proxy<material>(var, var_proxy);
                auto data_var = entt::forward_as_meta(*data);
                result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy);

                ImGui::TreePop();
            }
        }
        else
        {
            if(ImGui::Button("Create Instance"))
            {
                data = std::make_shared<pbr_material>();
                result.changed = true;
                result.edit_finished = true;
            }
        }
    }

    return result;
}

auto inspector_asset_handle_mesh::inspect_as_property(rtti::context& ctx, asset_handle<mesh>& data) -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};

    result |= pick_asset(filter, em, tm, am, data, ex::get_type<mesh>());
    return result;
}

auto inspector_asset_handle_mesh::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<mesh>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_version_ != data.version())
    {
        inspected_asset_ = data;
        inspected_version_ = data.version();
        importer_ = nullptr;
    }

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_mesh",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::BeginChild(ex::get_type(data.extension()).c_str());

            if(data)
            {
                var_info mesh_var_info;
                mesh_var_info.read_only = true;
                mesh_var_info.is_copyable = false;

                auto mesh_var_proxy = make_asset_proxy<mesh>(var, var_proxy);

                entt::meta_any mesh_var;
                if(mesh_var_proxy.impl->getter(mesh_var))
                {
                    result |= ::unravel::inspect_var(ctx, mesh_var, mesh_var_proxy, mesh_var_info);
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if(ImGui::BeginTabItem("Import"))
        {
            auto meta = am.get_metadata(data.uid());

            auto base_importer = meta.meta.importer;

            auto importer = std::static_pointer_cast<mesh_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<mesh_importer_meta>(*importer);
                }

                if(ImGui::BeginTabBar("asset_handle_mesh_import",
                                      ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                          ImGuiTabBarFlags_FittingPolicyScroll))
                {
                    if(ImGui::BeginTabItem("Model"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->model);

                        ImGui::EndTabItem();
                    }

                    if(ImGui::BeginTabItem("Sdf"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->sdf);

                        ImGui::EndTabItem();
                    }

                    if(ImGui::BeginTabItem("Rig"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->rig);

                        ImGui::EndTabItem();
                    }

                    if(ImGui::BeginTabItem("Animations"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->animations);

                        ImGui::EndTabItem();
                    }

                    if(ImGui::BeginTabItem("Materials"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->materials);

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }

            if(ImGui::Button("Revert"))
            {
                importer_ = {};
            }
            ImGui::SameLine();
            if(ImGui::Button("Apply"))
            {
                if(importer_)
                {
                    *importer = *importer_;
                }

                auto meta_absolute_path = asset_writer::resolve_meta_file(data);
                asset_writer::atomic_save_to_file(meta_absolute_path.string(), meta.meta);
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return result;
}

auto inspector_asset_handle_animation::inspect_as_property(rtti::context& ctx, asset_handle<animation_clip>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<animation_clip>());

    return result;
}

auto inspector_asset_handle_animation::inspect(rtti::context& ctx,
                                               entt::meta_any& var,
                                               const meta_any_proxy& var_proxy,
                                               const var_info& info,
                                               const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<animation_clip>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_version_ != data.version())
    {
        inspected_asset_ = data;
        inspected_version_ = data.version();
        importer_ = nullptr;
    }

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_animation",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            if(data)
            {
                var_info clip_var_info;
                clip_var_info.read_only = true;

                auto clip_var_proxy = make_asset_proxy<animation_clip>(var, var_proxy);

                entt::meta_any clip_var;
                if(clip_var_proxy.impl->getter(clip_var))
                {
                    result |= ::unravel::inspect_var(ctx, clip_var, clip_var_proxy, clip_var_info);
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Import"))
        {
            auto meta = am.get_metadata(data.uid());
            auto base_importer = meta.meta.importer;

            auto importer = std::static_pointer_cast<animation_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<animation_importer_meta>(*importer);
                }

                if(ImGui::BeginTabBar("asset_handle_mesh_import",
                                      ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                          ImGuiTabBarFlags_FittingPolicyScroll))
                {
                    if(ImGui::BeginTabItem("Root Motion"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->root_motion);

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }

            if(ImGui::Button("Revert"))
            {
                importer_ = {};
            }
            ImGui::SameLine();
            if(ImGui::Button("Apply"))
            {
                if(importer_)
                {
                    *importer = *importer_;
                }

                auto meta_absolute_path = asset_writer::resolve_meta_file(data);
                asset_writer::atomic_save_to_file(meta_absolute_path.string(), meta.meta);
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return result;
}

auto inspector_asset_handle_prefab::get_prefab_entity(rtti::context& ctx, const asset_handle<prefab>& prefab)
    -> entt::handle
{
    const bool cached = inspected_root_ && inspected_root_.valid() && inspected_uid_ == prefab.uid() &&
                        inspected_version_ == prefab.version();
    if(cached)
    {
        return inspected_root_;
    }

    inspected_scene_.unload();
    inspected_root_ = inspected_scene_.instantiate(prefab, false);
    inspected_uid_ = prefab.uid();
    inspected_version_ = prefab.version();

    // An instance of the prefab that is upstream of it - its edits are saved straight back
    // into the file. The tag keeps it from being synced against that file (which would put a
    // reverted override straight back, before the save that would have made the revert stick)
    // and from recording its own content as overrides of it.
    inspected_root_.emplace<authoring_root_tag>();
    scene::adopt_document_statements(inspected_root_);

    return inspected_root_;
}

auto inspector_asset_handle_prefab::inspect_as_property(rtti::context& ctx, asset_handle<prefab>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<prefab>());

    return result;
}

auto inspector_asset_handle_prefab::inspect(rtti::context& ctx,
                                            entt::meta_any& var,
                                            const meta_any_proxy& var_proxy,
                                            const var_info& info,
                                            const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<prefab>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    auto prefab_entity = get_prefab_entity(ctx, data);
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    // Two authoring roots of one file would be last-writer-wins; while prefab mode has it,
    // this one only shows it.
    const bool open_in_prefab_mode = em.is_prefab_mode() && em.edited_prefab.uid() == data.uid();
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_prefab",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::BeginChild(ex::get_type(data.extension()).c_str());

            if(data && open_in_prefab_mode)
            {
                ImGui::TextWrapped("This prefab is open in prefab mode. Edit it there.");
            }
            else if(data)
            {
                result |= ::unravel::inspect(ctx, prefab_entity);
                if(result.edit_finished)
                {
                    fs::path absolute_key = fs::absolute(fs::resolve_protocol(data.id()));
                    asset_writer::atomic_save_to_file(absolute_key.string(), prefab_entity);
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Import"))
        {
            ImGui::TextUnformatted("Import options");

            if(ImGui::Button("Reimport"))
            {
                asset_actions::reimport(data);
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return result;
}

auto inspector_asset_handle_scene_prefab::inspect_as_property(rtti::context& ctx, asset_handle<scene_prefab>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};

    result |= pick_asset(filter, em, tm, am, data, ex::get_type<scene_prefab>());

    return result;
}

auto inspector_asset_handle_scene_prefab::inspect(rtti::context& ctx,
                                                  entt::meta_any& var,
                                                  const meta_any_proxy& var_proxy,
                                                  const var_info& info,
                                                  const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<scene_prefab>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_scene_prefab",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Import"))
        {
            ImGui::BeginChild("Import");

            ImGui::TextUnformatted("Import options");

            if(ImGui::Button("Reimport"))
            {
                asset_actions::reimport(data);
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return result;
}

auto inspector_asset_handle_physics_material::inspect_as_property(rtti::context& ctx,
                                                                  asset_handle<physics_material>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<physics_material>());

    return result;
}

auto inspector_asset_handle_physics_material::inspect(rtti::context& ctx,
                                                      entt::meta_any& var,
                                                      const meta_any_proxy& var_proxy,
                                                      const var_info& info,
                                                      const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<physics_material>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto data_var_proxy = make_mutable_asset_proxy<physics_material>(var, var_proxy);

        entt::meta_any data_var;
        if(data_var_proxy.impl->getter(data_var))
        {
            result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy);
        }
    }
    if(result.edit_finished)
    {
        asset_writer::atomic_save_to_file(data.id(), data);
    }

    return result;
}

auto inspector_asset_handle_audio_clip::inspect_as_property(rtti::context& ctx, asset_handle<audio_clip>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<audio_clip>());

    return result;
}

void inspector_asset_handle_audio_clip::inspect_clip(const std::shared_ptr<audio_clip>& var)
{
    if(!source_)
    {
        source_ = std::make_shared<audio::source>();
    }
    source_->update(audio::duration_t(0.0166));

    property_layout layout("clip",
                           [&]()
                           {
                               ImGui::BeginGroup();

                               if(ImGui::Button(ICON_MDI_PLAY))
                               {
                                   if(source_->is_playing())
                                   {
                                       source_->resume();
                                   }
                                   else
                                   {
                                       source_->bind(*var);
                                       source_->play();
                                   }
                               }
                               ImGui::SameLine();
                               if(ImGui::Button(ICON_MDI_PAUSE))
                               {
                                   source_->pause();
                               }
                               ImGui::SameLine();
                               if(ImGui::Button(ICON_MDI_STOP))
                               {
                                   source_->stop();
                               }
                               ImGui::EndGroup();
                           });

    auto duration = source_->has_bound_sound() ? source_->get_playback_duration() : var->get_info().duration;

    float total_time = floorf(float(duration.count()) * 100.0f) / 100.0f;

    auto current_time = float(source_->get_playback_position().count());

    if(ImGui::KnobSliderScalarT("##playing_offset", &current_time, 0.0f, total_time))
    {
        source_->set_playback_position(audio::duration_t(current_time));
    }
}

auto inspector_asset_handle_audio_clip::inspect(rtti::context& ctx,
                                                entt::meta_any& var,
                                                const meta_any_proxy& var_proxy,
                                                const var_info& info,
                                                const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<audio_clip>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_version_ != data.version())
    {
        inspected_asset_ = data;
        inspected_version_ = data.version();
        importer_ = nullptr;
    }

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_audio_clip",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::BeginChild(ex::get_type(data.extension()).c_str());

            auto data_var = data.get(false);
            if(data_var)
            {
                var_info data_var_info;
                data_var_info.read_only = true;
                data_var_info.is_copyable = false;

                auto data_var_proxy = make_asset_proxy<audio_clip>(var, var_proxy);

                entt::meta_any data_var;
                if(data_var_proxy.impl->getter(data_var))
                {
                    result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy, data_var_info);
                }

                auto clip = data.get(false);

                if(clip)
                {
                    inspect_clip(clip);
                }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Import"))
        {
            auto meta = am.get_metadata(data.uid());
            auto base_importer = meta.meta.importer;

            auto importer = std::static_pointer_cast<audio_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<audio_importer_meta>(*importer);
                }

                result |= ::unravel::inspect(ctx, *importer_);
            }

            if(ImGui::Button("Revert"))
            {
                importer_ = {};
            }
            ImGui::SameLine();
            if(ImGui::Button("Apply"))
            {
                if(importer_)
                {
                    *importer = *importer_;
                }

                auto meta_absolute_path = asset_writer::resolve_meta_file(data);
                asset_writer::atomic_save_to_file(meta_absolute_path.string(), meta.meta);
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    return result;
}

auto inspector_asset_handle_font::inspect_as_property(rtti::context& ctx, asset_handle<font>& data) -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<font>());

    return result;
}

auto inspector_asset_handle_font::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<font>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto data_var = data.get(false);
        if(data_var)
        {
            var_info data_var_info;
            data_var_info.read_only = true;
            data_var_info.is_copyable = false;

            auto data_var_proxy = make_asset_proxy<font>(var, var_proxy);
            entt::meta_any data_var;
            if(data_var_proxy.impl->getter(data_var))
            {
                result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy, data_var_info);
            }
        }
    }

    return result;
}

auto inspector_asset_handle_ui_tree::inspect_as_property(rtti::context& ctx, asset_handle<ui_tree>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<ui_tree>());

    return result;
}

auto inspector_asset_handle_ui_tree::inspect(rtti::context& ctx,
                                             entt::meta_any& var,
                                             const meta_any_proxy& var_proxy,
                                             const var_info& info,
                                             const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<ui_tree>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto data_var_proxy = make_mutable_asset_proxy<ui_tree>(var, var_proxy);

        entt::meta_any data_var;
        if(data_var_proxy.impl->getter(data_var))
        {
            result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy);
        }
    }
    if(result.edit_finished)
    {
        asset_writer::atomic_save_to_file(data.id(), data);
    }

    return result;
}

auto inspector_asset_handle_style_sheet::inspect_as_property(rtti::context& ctx, asset_handle<style_sheet>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<style_sheet>());

    return result;
}

auto inspector_asset_handle_style_sheet::inspect(rtti::context& ctx,
                                                 entt::meta_any& var,
                                                 const meta_any_proxy& var_proxy,
                                                 const var_info& info,
                                                 const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<style_sheet>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto data_var_proxy = make_mutable_asset_proxy<style_sheet>(var, var_proxy);

        entt::meta_any data_var;
        if(data_var_proxy.impl->getter(data_var))
        {
            result |= ::unravel::inspect_var(ctx, data_var, data_var_proxy);
        }
    }
    if(result.edit_finished)
    {
        asset_writer::atomic_save_to_file(data.id(), data);
    }

    return result;
}

} // namespace unravel
