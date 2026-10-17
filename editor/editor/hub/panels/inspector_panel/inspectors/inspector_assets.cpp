#include "inspector_assets.h"
#include "inspector_asset_picker.h"
#include <editor/editing/authoring_root.h>
#include "inspectors.h"

#include <engine/animation/animation.h>
#include <editor/hub/panels/animation_panel/animation_controller_ui.h>
#include <engine/animation/animation_controller.h>
#include <engine/meta/animation/animation_controller.hpp>
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
#include <editor/assets/asset_icons.h>
#include <editor/editing/actions/lambda_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/thumbnail_manager.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>
#include <editor/imgui/integration/imgui_style.h>

#include <filesystem/filesystem.h>
#include <algorithm>
#include <sstream>
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

/// The import settings as the binary archive writes them.
auto save_import_settings_bytes(const animation_importer_meta& settings) -> std::string
{
    std::stringstream stream;
    {
        ser20::oarchive_binary_t archive(stream);
        try_save(archive, ser20::make_nvp("settings", settings));
    }
    return stream.str();
}

/// Whether two import settings hold the same values; compared as saved, so every saved field counts.
auto is_same_import_settings(const animation_importer_meta& lhs, const animation_importer_meta& rhs) -> bool
{
    return save_import_settings_bytes(lhs) == save_import_settings_bytes(rhs);
}

} // namespace

void inspector_asset_handle_texture::draw_image(const asset_handle<gfx::texture>& data, ImVec2 size)
{
    // try_get, not is_ready: it also starts the load of a texture nothing has loaded yet.
    if(data.try_get())
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

            if(data.try_get())
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

                editor_actions::save_import_settings(data.id(), meta.meta);
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

    inspect_import_origin(ctx, data);
    // try_get, not is_ready: it also starts the load of a material nothing has loaded yet.
    const auto loaded = data.try_get();
    if(asset_handle<material>::is_empty(loaded))
    {
        // A failed load hands out the shared empty material; editing it would write that over the file.
        ImGui::TextColored(imgui_style::get_muted_text_color(), "This material could not be loaded.");
    }
    else if(loaded)
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

namespace
{
constexpr float IMPORT_BANNER_ROUNDING = 4.0f;
constexpr float IMPORT_BANNER_ICON_GAP_EM = 0.4f;
/// The banner is tinted with its color at this alpha, as the inspector's notes are.
constexpr float IMPORT_BANNER_BACKGROUND_ALPHA = 0.12f;
constexpr const char* MISSING_SOURCE_NAME = "a model no longer in the project";

/// The icon in the banner's color at the cursor; the cursor moves past it, to where the text starts.
void draw_import_banner_icon(const char* icon, ImU32 color)
{
    const float icon_width = ImGui::GetFontSize();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::RenderIconCentered(ImGui::GetWindowDrawList(),
                              ImVec2(min.x + icon_width * 0.5f, min.y + ImGui::GetTextLineHeight() * 0.5f),
                              icon,
                              color);
    ImGui::SetCursorScreenPos(ImVec2(min.x + icon_width * (1.0f + IMPORT_BANNER_ICON_GAP_EM), min.y));
}

/// A bold title over a muted, wrapped message.
void draw_import_banner_text(const std::string& title, const std::string& message)
{
    ImGui::PushFont(ImGui::Font::Bold);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(message.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

/// The banner's text and, for an edited asset, Reset to Imported.
void draw_import_banner_body(rtti::context& ctx, const asset_actions::generated_status& status, const std::string& key)
{
    const std::string source = status.source_name.empty() ? MISSING_SOURCE_NAME : status.source_name;
    if(!status.is_edited)
    {
        draw_import_banner_text("Imported from " + source,
                                "Each reimport of " + source + " rewrites this material until it is edited.");
        return;
    }
    draw_import_banner_text("Edited import of " + source, "Reimports of " + source + " keep these changes.");
    if(ImGui::Button(ICON_MDI_RESTORE " Reset to Imported"))
    {
        asset_actions::reset_to_imported(ctx, key);
    }
    ImGui::SetItemTooltipEx("Discards the edits and reimports %s.", source.c_str());
}
} // namespace

void inspector_asset_handle_material::inspect_import_origin(rtti::context& ctx, const asset_handle<material>& data)
{
    const auto status = asset_actions::get_generated_status(ctx.get_cached<asset_manager>(), data.uid());
    if(!status.is_generated)
    {
        return;
    }
    const ImU32 color =
        status.is_edited ? imgui_style::get_edited_import_color_u32() : imgui_style::get_imported_asset_color_u32();
    ImVec4 background = ImGui::ColorConvertU32ToFloat4(color);
    background.w = IMPORT_BANNER_BACKGROUND_ALPHA;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, IMPORT_BANNER_ROUNDING);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImGui::GetStyle().FramePadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    if(ImGui::BeginChild("##import_banner",
                         ImVec2(0.0f, 0.0f),
                         ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY))
    {
        draw_import_banner_icon(status.is_edited ? ICON_MDI_PENCIL_OUTLINE : ICON_MDI_IMPORT, color);
        ImGui::BeginGroup();
        draw_import_banner_body(ctx, status, data.id());
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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

                    if(ImGui::BeginTabItem("Cards"))
                    {
                        result |= ::unravel::inspect(ctx, importer_->cards);

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

                editor_actions::save_import_settings(data.id(), meta.meta);
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

void inspector_asset_handle_animation::apply_import_settings(rtti::context& ctx)
{
    auto& am = ctx.get_cached<asset_manager>();
    auto meta = am.get_metadata(inspected_asset_.uid());
    auto importer = std::static_pointer_cast<animation_importer_meta>(meta.meta.importer);
    if(importer && importer_)
    {
        *importer = *importer_;
        editor_actions::save_import_settings(inspected_asset_.id(), meta.meta);
    }
}

auto inspector_asset_handle_animation::has_unapplied_edits(rtti::context& ctx) const -> bool
{
    if(!committed_ || !inspected_asset_)
    {
        return false;
    }
    const auto meta = ctx.get_cached<asset_manager>().get_metadata(inspected_asset_.uid());
    const auto applied = std::static_pointer_cast<animation_importer_meta>(meta.meta.importer);
    return applied && !is_same_import_settings(*committed_, *applied);
}

void inspector_asset_handle_animation::drop_staged_edits()
{
    importer_ = nullptr;
    committed_ = nullptr;
}

void inspector_asset_handle_animation::end_inspection(rtti::context& ctx)
{
    if(has_unapplied_edits(ctx))
    {
        apply_import_settings(ctx);
    }
    drop_staged_edits();
    inspected_asset_ = {};
}

void inspector_asset_handle_animation::record_import_edit(rtti::context& ctx)
{
    // A finished edit that changed nothing (a marker clicked, not moved) is neither an undo step
    // nor an unapplied change.
    if(is_same_import_settings(*committed_, *importer_))
    {
        return;
    }
    const animation_importer_meta before = *committed_;
    const animation_importer_meta after = *importer_;
    *committed_ = after;
    std::weak_ptr<animation_importer_meta> staged = importer_;
    std::weak_ptr<animation_importer_meta> committed = committed_;
    const auto restore = [staged, committed](const animation_importer_meta& settings)
    {
        auto staged_settings = staged.lock();
        auto committed_settings = committed.lock();
        if(staged_settings && committed_settings)
        {
            *staged_settings = settings;
            *committed_settings = settings;
        }
    };
    ctx.get_cached<editing_manager>().do_action<tracked_editor_state_action_t>(
        "Edit Import Settings",
        [restore, after]()
        {
            restore(after);
        },
        [restore, before]()
        {
            restore(before);
        });
}

void inspector_asset_handle_animation::draw_unapplied_bar(rtti::context& ctx)
{
    if(!has_unapplied_edits(ctx))
    {
        return;
    }
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), ICON_MDI_ALERT " Unapplied import changes");
    ImGui::SetItemTooltipEx("Apply reimports the clip with them; showing anything else in the inspector applies them too.");
    ImGui::SameLine();
    if(ImGui::SmallButton("Apply##unapplied"))
    {
        apply_import_settings(ctx);
    }
    ImGui::SameLine();
    if(ImGui::SmallButton("Revert##unapplied"))
    {
        drop_staged_edits();
    }
    ImGui::Separator();
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
        const bool unapplied = has_unapplied_edits(ctx);
        // Edits nobody applied or reverted are applied rather than lost with the clip shown.
        if(unapplied && inspected_asset_ != data)
        {
            apply_import_settings(ctx);
        }
        // A reimport of the same asset keeps them staged, still offered to Apply or Revert; anything
        // else reads the settings afresh.
        if(!unapplied || inspected_asset_ != data)
        {
            drop_staged_edits();
        }
        inspected_asset_ = data;
        inspected_version_ = data.version();
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
                    committed_ = std::make_shared<animation_importer_meta>(*importer);
                }
                draw_unapplied_bar(ctx);
                // The staged settings record their own undo step per finished edit.
                inspect_result import_result;
                if(importer_ && ImGui::BeginTabBar("asset_handle_mesh_import",
                                                   ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
                                                       ImGuiTabBarFlags_FittingPolicyScroll))
                {
                    inspect_without_undo_scope no_undo;
                    if(ImGui::BeginTabItem(ICON_MDI_MAP_MARKER " Events"))
                    {
                        import_result |= timeline_.draw_events(ctx, data, *importer_);
                        ImGui::EndTabItem();
                    }
                    if(ImGui::BeginTabItem(ICON_MDI_CHART_BELL_CURVE " Curves"))
                    {
                        import_result |= timeline_.draw_curves(ctx, data, *importer_);
                        ImGui::EndTabItem();
                    }
                    if(ImGui::BeginTabItem("Root Motion"))
                    {
                        import_result |= ::unravel::inspect(ctx, importer_->root_motion);
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                if(import_result.edit_finished && importer_)
                {
                    record_import_edit(ctx);
                }
                result.changed |= import_result.changed;
                result.edit_finished |= import_result.edit_finished;
            }

            if(ImGui::Button("Revert"))
            {
                drop_staged_edits();
            }
            ImGui::SameLine();
            if(ImGui::Button("Apply"))
            {
                apply_import_settings(ctx);
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

auto inspector_asset_handle_animation_controller::inspect_as_property(rtti::context& ctx,
                                                                      asset_handle<animation_controller>& data)
    -> inspect_result
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    auto& em = ctx.get_cached<editing_manager>();

    inspect_result result{};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<animation_controller>());

    return result;
}

auto inspector_asset_handle_animation_controller::inspect(rtti::context& ctx,
                                                          entt::meta_any& var,
                                                          const meta_any_proxy& var_proxy,
                                                          const var_info& info,
                                                          const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<asset_handle<animation_controller>&>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};
    // try_get, not is_ready: it also starts the load of a controller nothing has used yet.
    const auto controller = data.try_get();
    if(!controller)
    {
        ImGui::TextDisabled("Loading...");
        return result;
    }
    if(asset_handle<animation_controller>::is_empty(controller))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), ICON_MDI_ALERT " The controller could not be loaded.");
        return result;
    }

    if(ImGui::Button(ICON_MDI_STATE_MACHINE " Open in Animation Controller Editor", ImVec2(-1.0f, 0.0f)))
    {
        editor_actions::open_animation_controller(ctx, data);
    }

    const auto problems = validate_animation_controller(*controller);
    if(!problems.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        for(const auto& problem : problems)
        {
            ImGui::TextWrapped(ICON_MDI_ALERT " %s", problem.c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    // A summary: the controller is edited in its own window, which keeps references to renamed
    // states and parameters, undo and running instances in step.
    size_t state_count = 0;
    size_t transition_count = 0;
    for(const auto& layer : controller->layers)
    {
        state_count += layer.states.size();
        transition_count += layer.transitions.size();
    }
    ImGui::TextDisabled("%d layers, %d states, %d transitions",
                        int(controller->layers.size()),
                        int(state_count),
                        int(transition_count));
    if(controller->preview_model)
    {
        ImGui::TextDisabled("Preview model: %s", controller->preview_model.name().c_str());
    }
    if(!controller->parameters.empty())
    {
        ImGui::SeparatorText("Parameters");
    }
    if(!controller->parameters.empty() && ImGui::BeginTable("##controller_parameters", 3, ImGuiTableFlags_RowBg))
    {
        for(const auto& parameter : controller->parameters)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(parameter.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", get_parameter_type_name(parameter.type));
            ImGui::TableSetColumnIndex(2);
            switch(parameter.type)
            {
                case animation_parameter_type::float_value:
                    ImGui::TextDisabled("%.2f", parameter.default_float);
                    break;
                case animation_parameter_type::int_value:
                    ImGui::TextDisabled("%d", parameter.default_int);
                    break;
                case animation_parameter_type::bool_value:
                    ImGui::TextDisabled("%s", parameter.default_bool ? "true" : "false");
                    break;
                case animation_parameter_type::trigger:
                    break;
            }
        }
        ImGui::EndTable();
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

                editor_actions::save_import_settings(data.id(), meta.meta);
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
