#include "inspector_assets.h"
#include "inspector_asset_picker.h"
#include "inspector_asset_preview.h"
#include "property_override_view.h"
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
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/material_graph_library.h>
#include <editor/hub/panels/material_graph_panel/material_graph_style.h>
#include <engine/rendering/mesh.h>


#include <editor/assets/asset_actions.h>
#include <editor/assets/asset_icons.h>
#include <editor/editing/actions/lambda_actions.h>
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/thumbnail_manager.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>
#include <editor/hub/panels/profile_labels.h>
#include <editor/imgui/integration/imgui_style.h>

#include <filesystem/filesystem.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <type_traits>
#include <utility>
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

/// Where an asset field opens its asset, and how; empty for a type nothing opens.
template<typename T>
struct asset_opener
{
    /// Where it shows the asset, "the Material Graph".
    std::string target;
    std::function<void(const asset_handle<T>&)> open;
};

//-----------------------------------------------------------------------------
/// <summary>
/// The field of an asset property: thumbnail, name and the buttons under it, a drop target for
/// assets of the type, and the picker window it opens. The first tile of the picker, the empty
/// handle, clears the property. With an @p opener, the field also opens its asset there.
/// </summary>
//-----------------------------------------------------------------------------
template<typename T>
auto pick_asset(ImGuiTextFilter& filter,
                editing_manager& em,
                thumbnail_manager& tm,
                asset_manager& am,
                asset_handle<T>& data,
                const std::string& type,
                const asset_opener<T>& opener = {}) -> inspect_result
{
    inspect_result result{};

    asset_picker::field_content content{};
    content.thumbnail = asset_picker::make_preview(tm, data);
    content.type = type;
    if(data)
    {
        content.name = data.name();
        content.path = data.id();
    }
    if(opener.open)
    {
        content.open_in = opener.target;
    }
    const asset_picker::field_request request = asset_picker::draw_field(content);
    if(request.is_open_pressed && data && opener.open)
    {
        opener.open(data);
    }

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
        tile.thumbnail = asset_picker::make_preview(tm, asset);
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
template<typename Settings>
auto save_import_settings_bytes(const Settings& settings) -> std::string
{
    std::stringstream stream;
    {
        ser20::oarchive_binary_t archive(stream);
        try_save(archive, ser20::make_nvp("settings", settings));
    }
    return stream.str();
}

/// Whether two import settings hold the same values; compared as saved, so every saved field counts.
template<typename Settings>
auto is_same_import_settings(const Settings& lhs, const Settings& rhs) -> bool
{
    return save_import_settings_bytes(lhs) == save_import_settings_bytes(rhs);
}

/**
 * @brief The Revert and Apply buttons under an asset's import settings, enabled while `edited` differs from
 * `applied`: Revert drops the edits, Apply writes them to the asset's .meta, which imports it again.
 */
template<typename Settings>
void draw_import_buttons(const std::string& key,
                         const asset_meta& meta,
                         const std::shared_ptr<Settings>& applied,
                         std::shared_ptr<Settings>& edited)
{
    ImGui::Spacing();
    const bool has_unapplied_edits = applied && edited && !is_same_import_settings(*edited, *applied);
    ImGui::BeginDisabled(!has_unapplied_edits);
    if(ImGui::Button("Revert"))
    {
        edited = {};
    }
    ImGui::SameLine();
    if(ImGui::Button("Apply"))
    {
        *applied = *edited;
        editor_actions::save_import_settings(key, meta);
    }
    ImGui::EndDisabled();
}

/// The properties `names` of import settings (or a part of them), in rows as the inspector draws them.
template<typename Settings>
auto inspect_import_properties(rtti::context& ctx, Settings& settings, std::initializer_list<entt::id_type> names)
    -> inspect_result
{
    entt::meta_any object = entt::forward_as_meta(settings);
    const auto type = entt::resolve<Settings>();
    inspect_result result;
    for(const auto name : names)
    {
        if(const auto prop = type.data(name))
        {
            result |= inspect_property(ctx, object, make_proxy(object), prop);
        }
    }
    return result;
}

/// Opens a platform's tab of import settings; its label is bold while the platform overrides the Default tab, as an
/// overridden setting is. False when the tab is not the open one.
auto begin_platform_tab(target_platform platform, bool is_overridden) -> bool
{
    const auto label = fmt::format("{} {}", profile_labels::get_platform_icon(platform), profile_labels::get_platform_name(platform));
    if(is_overridden)
    {
        ImGui::PushFont(ImGui::Font::ExtraBold);
        // The tab bar sizes its tabs in its own font: the bold label keeps a width measured in bold.
        ImGui::SetNextItemWidth(ImGui::TabItemCalcSize(label.c_str(), false).x);
    }
    const bool is_open = ImGui::BeginTabItem(label.c_str());
    if(is_overridden)
    {
        ImGui::PopFont();
    }
    return is_open;
}

/**
 * @brief A platform's quality tab: whether the platform overrides the Default tab's quality, then its own
 * quality while it does, or the Default tab's, read-only, while it does not. The tab's label is bold while
 * the platform overrides it, as an overridden setting is.
 */
auto inspect_platform_quality_tab(rtti::context& ctx,
                                  target_platform platform,
                                  texture_importer_meta::platform_quality_meta& own,
                                  const texture_importer_meta::quality_meta& fallback) -> inspect_result
{
    inspect_result result;
    if(!begin_platform_tab(platform, own.is_overridden))
    {
        return result;
    }
    const bool was_overridden = own.is_overridden;
    entt::meta_any object = entt::forward_as_meta(own);
    const auto prop = entt::resolve<texture_importer_meta::platform_quality_meta>().data("is_overridden"_hs);
    result |= inspect_property(ctx, object, make_proxy(object), prop);
    // An override starts from the quality the platform had, as the Default tab's.
    if(own.is_overridden && !was_overridden && own.quality == texture_importer_meta::quality_meta{})
    {
        own.quality = fallback;
    }
    if(own.is_overridden)
    {
        result |= inspect(ctx, own.quality);
    }
    else
    {
        auto inherited = fallback;
        ImGui::BeginDisabled();
        inspect(ctx, inherited);
        ImGui::EndDisabled();
    }
    ImGui::EndTabItem();
    return result;
}

/// The texture's import settings: what it is, then its quality, by default and per platform, in tabs.
auto inspect_texture_import_settings(rtti::context& ctx, texture_importer_meta& importer) -> inspect_result
{
    inspect_result result =
        inspect_import_properties(ctx, importer, {"type"_hs, "colorspace"_hs, "generate_mipmaps"_hs, "invert_normal_y"_hs});
    ImGui::SeparatorText("Quality");
    if(!ImGui::BeginTabBar("##texture_quality"))
    {
        return result;
    }
    if(ImGui::BeginTabItem("Default"))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::TextWrapped("%s",
                           "Every platform compiles with this quality unless its tab overrides it. Project Default is the "
                           "texture default of the profile deployed. The editor compiles with the Desktop tab and the "
                           "project settings' defaults.");
        ImGui::PopStyleColor();
        result |= inspect(ctx, importer.quality);
        ImGui::EndTabItem();
    }
    result |= inspect_platform_quality_tab(ctx, target_platform::desktop, importer.platforms.desktop, importer.quality);
    result |= inspect_platform_quality_tab(ctx, target_platform::android, importer.platforms.android, importer.quality);
    ImGui::EndTabBar();
    return result;
}

/**
 * @brief A platform's compression tab of an audio clip: whether the platform overrides the Default tab's
 * compression, then its own while it does, or the Default tab's, read-only, while it does not.
 */
auto inspect_platform_compression_tab(rtti::context& ctx,
                                      target_platform platform,
                                      audio_importer_meta::platform_compression_meta& own,
                                      audio_importer_meta::compression_quality fallback) -> inspect_result
{
    inspect_result result;
    if(!begin_platform_tab(platform, own.is_overridden))
    {
        return result;
    }
    const bool was_overridden = own.is_overridden;
    result |= inspect_import_properties(ctx, own, {"is_overridden"_hs});
    // An override starts from the compression the platform had, the Default tab's.
    if(own.is_overridden && !was_overridden && own.compression == audio_importer_meta::compression_quality::project_default)
    {
        own.compression = fallback;
    }
    if(own.is_overridden)
    {
        result |= inspect_import_properties(ctx, own, {"compression"_hs});
    }
    else
    {
        auto inherited = own;
        inherited.compression = fallback;
        ImGui::BeginDisabled();
        inspect_import_properties(ctx, inherited, {"compression"_hs});
        ImGui::EndDisabled();
    }
    ImGui::EndTabItem();
    return result;
}

/// The audio clip's import settings: Force To Mono, then its compression, by default and per platform, in tabs.
auto inspect_audio_import_settings(rtti::context& ctx, audio_importer_meta& importer) -> inspect_result
{
    inspect_result result = inspect_import_properties(ctx, importer, {"force_to_mono"_hs});
    ImGui::SeparatorText("Compression");
    if(!ImGui::BeginTabBar("##audio_compression"))
    {
        return result;
    }
    if(ImGui::BeginTabItem("Default"))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::TextWrapped("%s",
                           "Every platform compiles with this compression unless its tab overrides it. Project Default "
                           "is the audio default of the profile deployed. The editor compiles with the Desktop tab and "
                           "the project settings' default.");
        ImGui::PopStyleColor();
        result |= inspect_import_properties(ctx, importer, {"compression"_hs});
        ImGui::EndTabItem();
    }
    result |= inspect_platform_compression_tab(ctx, target_platform::desktop, importer.platforms.desktop, importer.compression);
    result |= inspect_platform_compression_tab(ctx, target_platform::android, importer.platforms.android, importer.compression);
    result |= inspect_platform_compression_tab(ctx, target_platform::web, importer.platforms.web, importer.compression);
    ImGui::EndTabBar();
    return result;
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

            auto importer = safe_pointer_cast<texture_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<texture_importer_meta>(*importer);
                }

                result |= inspect_texture_import_settings(ctx, *importer_);
            }
            draw_import_buttons(data.id(), meta.meta, importer, importer_);
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

    const asset_opener<material> opener{"the Material Graph",
                                        [&ctx](const asset_handle<material>& asset)
                                        {
                                            editor_actions::open_material_graph(ctx, asset);
                                        }};
    result |= pick_asset(filter, em, tm, am, data, ex::get_type<material>(), opener);

    return result;
}

namespace
{
/// The way to a graph material's graph, and a note while its shader is not compiled for the renderer that draws.
void draw_graph_material_header(rtti::context& ctx, const asset_handle<material>& asset, const graph_material& mat)
{
    if(ImGui::Button(ICON_MDI_GRAPH " Open in Material Graph", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
        editor_actions::open_material_graph(ctx, asset);
    }
    ImGui::SetItemTooltipEx("Edit the graph; every finished edit is saved to this material.");
    if(!mat.get_shader())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, imgui_style::get_muted_text_color_u32());
        ImGui::TextWrapped("Its shader is not compiled for this renderer yet, so it draws as a default material.");
        ImGui::PopStyleColor();
    }
}
} // namespace

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

    // try_get, not is_ready: it also starts the load of a material nothing has loaded yet.
    const auto loaded = data.try_get();
    inspect_import_origin(ctx, data);
    if(asset_handle<material>::is_empty(loaded))
    {
        // A failed load hands out the shared empty material; editing it would write that over the file.
        ImGui::TextColored(imgui_style::get_muted_text_color(), "This material could not be loaded.");
    }
    else if(auto* graph = loaded ? loaded->safe_cast<graph_material>() : nullptr)
    {
        result |= inspect_graph(ctx, data, *graph);
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

namespace
{
/// The mark of a value a material instance overrides: apart from the accent of prefab overrides.
constexpr ImVec4 MATERIAL_INSTANCE_OVERRIDE_COLOR{61.0f / 255.0f, 191.0f / 255.0f, 166.0f / 255.0f, 1.0f};

/// @p mat as its most derived reflected type, so its base type's properties and its own are found.
auto make_material_view(material& mat) -> entt::meta_any
{
    entt::meta_any object = entt::forward_as_meta(mat);
    entt::as_derived(object);
    return object;
}

auto get_property_id(const std::string& name) -> entt::id_type
{
    return entt::hashed_string{name.c_str()}.value();
}

/// The first override that gives the slot a draw of its own (one that is not per instance), empty when none does.
auto find_own_draw_override(const material_instance& instance, const entt::meta_type& type) -> std::string
{
    for(const auto& entry : instance.get_overrides())
    {
        const auto data = type.data(entry.property);
        if(data && !entt::get_attribute_as<bool>(data, "per_instance"))
        {
            return entt::get_attribute_as<std::string>(data, "pretty_name");
        }
    }
    return {};
}

void draw_instance_status(const material_instance& instance, const entt::meta_type& type, const std::string& shared)
{
    if(!instance.has_overrides())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("No overrides: the model draws exactly like %s.", shared.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    if(instance.shares_draw())
    {
        ImGui::TextWrapped("Draws together with the other models that use %s.", shared.c_str());
        ImGui::SetItemTooltipEx("Every override is per instance (a color or a number), so the model shares the "
                                "shared material's draw.");
        return;
    }
    const auto own_draw = find_own_draw_override(instance, type);
    const bool has_graph_overrides =
        !instance.get_graph_parameter_overrides().empty() || !instance.get_graph_texture_overrides().empty();
    if(own_draw.empty() && has_graph_overrides)
    {
        ImGui::TextWrapped("Draws on its own: it overrides a texture of the graph, a parameter past the graph's first "
                           "four registers, or a parameter of a graph whose World Position Offset moves the vertices.");
        return;
    }
    if(own_draw.empty())
    {
        ImGui::TextWrapped("Draws on its own until its shared material is loaded and its overrides are applied to it.");
        return;
    }
    ImGui::TextWrapped("Draws on its own: %s is overridden.", own_draw.c_str());
    ImGui::SetItemTooltipEx("Textures, tiling and modes are per material: overriding one gives the model a draw of "
                            "its own. Colors and numbers do not.");
}

/// Marks the values the instance overrides, and reverts the one whose context menu asks for it.
auto make_instance_marks(const material_instance& instance, hpp::optional<entt::id_type>& reverted)
    -> property_override_marks
{
    property_override_marks marks;
    marks.get_mark = [&instance](const std::string& path)
    {
        return instance.is_overridden(get_property_id(path)) ? override_mark::overridden : override_mark::none;
    };
    marks.revert = [&reverted](const std::string& path)
    {
        reverted = get_property_id(path);
    };
    marks.revert_label = "Revert to Shared Material";
    marks.overridden_note = "This model overrides the shared material's value.";
    marks.marker_color = MATERIAL_INSTANCE_OVERRIDE_COLOR;
    return marks;
}

/// The path segments of a graph material's overrides, as the instance saves them (material_instance's SAVE).
constexpr const char* GRAPH_PARAMETERS_SEGMENT = "graph_parameters";
constexpr const char* GRAPH_TEXTURES_SEGMENT = "graph_textures";

/// A graph override a row's path names: a parameter or a texture node, by name.
struct graph_override_ref
{
    bool is_texture{};
    std::string name;
};

auto parse_graph_override_path(const std::string& path) -> hpp::optional<graph_override_ref>
{
    for(const bool is_texture : {false, true})
    {
        const std::string prefix = std::string(is_texture ? GRAPH_TEXTURES_SEGMENT : GRAPH_PARAMETERS_SEGMENT) + "/";
        if(path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0)
        {
            return graph_override_ref{is_texture, path.substr(prefix.size())};
        }
    }
    return {};
}

/// Marks the graph parameters and textures the instance overrides, and reverts the one whose context menu asks.
auto make_graph_override_marks(const material_instance& instance, hpp::optional<graph_override_ref>& reverted)
    -> property_override_marks
{
    property_override_marks marks;
    marks.get_mark = [&instance](const std::string& path)
    {
        const auto ref = parse_graph_override_path(path);
        if(!ref)
        {
            return override_mark::none;
        }
        const bool is_overridden =
            ref->is_texture ? instance.is_graph_texture_overridden(ref->name) : instance.is_graph_parameter_overridden(ref->name);
        return is_overridden ? override_mark::overridden : override_mark::none;
    };
    marks.revert = [&reverted](const std::string& path)
    {
        reverted = parse_graph_override_path(path);
    };
    marks.revert_label = "Revert to Shared Material";
    marks.overridden_note = "This model overrides the shared material's value.";
    marks.marker_color = MATERIAL_INSTANCE_OVERRIDE_COLOR;
    return marks;
}

/// What the graph rows of a frame did to the instance.
struct graph_row_changes
{
    bool is_changed{};
    bool is_finished{};
};

/// The label of a graph parameter, texture or switch row: its name as written, or an id ("color_map") in words
/// ("Color Map").
auto make_graph_row_label(const std::string& name) -> std::string
{
    if(name.find(' ') != std::string::npos || name.find('_') == std::string::npos)
    {
        return name;
    }
    std::string label;
    bool starts_word = true;
    for(const char c : name)
    {
        if(c == '_')
        {
            label += ' ';
            starts_word = true;
            continue;
        }
        label += starts_word ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        starts_word = false;
    }
    return label;
}

/// Whether texture @p index of @p textures is the first of its name: one name (a function called twice) is one row.
auto is_first_texture_of_name(const std::vector<material_graph_texture>& textures, size_t index) -> bool
{
    return std::none_of(textures.begin(),
                        textures.begin() + std::ptrdiff_t(index),
                        [&](const material_graph_texture& texture)
                        {
                            return texture.name == textures[index].name;
                        });
}

/// The row's hint: the graph's description of it, else @p fallback.
auto make_graph_row_tooltip(const std::string& description, const char* fallback) -> std::string
{
    return description.empty() ? std::string(fallback) : description;
}

/// One parameter's row, its value as the instance draws it, edited as the graph edits its default; an edit overrides
/// it.
void draw_graph_parameter_row(rtti::context& ctx,
                              material_instance& instance,
                              const graph_material& graph,
                              const material_graph_parameter& parameter,
                              graph_row_changes& changes)
{
    auto value = instance.get_graph_parameter(parameter.name).value_or(parameter.default_value);
    property_layout layout(make_graph_row_label(parameter.name),
                           make_graph_row_tooltip(parameter.description,
                                                  "A parameter of the material graph. Overriding it keeps the model in "
                                                  "its shared material's draw while it is among the graph's first four "
                                                  "registers."));
    const auto edit = inspect_vector_value(ctx, value, make_parameter_value_style(graph.get_graph(), parameter));
    changes.is_finished = changes.is_finished || edit.edit_finished;
    if(edit.changed && instance.set_graph_parameter(parameter.name, value))
    {
        changes.is_changed = true;
    }
}

/// One texture node's row, the texture the instance samples; picking one overrides it.
void draw_graph_texture_row(rtti::context& ctx, material_instance& instance, const material_graph_texture& texture, graph_row_changes& changes)
{
    auto handle = instance.get_graph_texture(texture.name);
    property_layout layout(make_graph_row_label(texture.name),
                           make_graph_row_tooltip(texture.description,
                                                  "A texture node of the material graph. Overriding it gives the model a "
                                                  "draw of its own."));
    entt::meta_any value = entt::forward_as_meta(handle);
    var_info info;
    info.is_property = true;
    const auto result = inspect_var(ctx, value, make_proxy(value, texture.name), info);
    changes.is_finished = changes.is_finished || result.edit_finished;
    if(result.changed && instance.set_graph_texture(texture.name, handle))
    {
        changes.is_changed = true;
    }
}

/// Whether going from @p before to @p after changes the generated code (a Static Switch), not values alone.
auto is_code_edit(const material_graph& before, const material_graph& after) -> bool
{
    return !has_same_material_graph_code(before, after);
}

/// Puts @p graph into the graph material @p asset holds and saves it: what each step of an edit of its rows does, done
/// and undone. A graph of new code (a Static Switch) is saved from a copy, and reaches the loaded material with the
/// shader its compile makes.
void write_graph_material_graph(rtti::context& ctx, const asset_handle<material>& asset, const material_graph& graph)
{
    const auto loaded = asset.try_get();
    auto* mat = loaded && !asset_handle<material>::is_empty(loaded) ? loaded->safe_cast<graph_material>() : nullptr;
    if(mat == nullptr || is_code_edit(mat->get_graph(), graph))
    {
        // New code, or a material still loading (the reload a save started), is written from a copy: the loaded
        // material keeps its graph and its shader until the recompile the save starts reloads both, so no draw binds
        // the new layout to the old shader.
        asset_actions::save_material_graph(ctx, asset, graph);
        return;
    }
    mat->set_graph(graph);
    asset_writer::atomic_save_to_file(asset.id(), asset);
    ctx.get_cached<thumbnail_manager>().regenerate_thumbnail(asset.uid());
}

/// Records a finished edit of a graph material's rows, from @p before to @p after, as one undo step.
void record_graph_material_edit(rtti::context& ctx, const asset_handle<material>& asset, material_graph before, material_graph after)
{
    auto& em = ctx.get_cached<editing_manager>();
    em.do_action<tracked_editor_state_action_t>(
        "Edit Material Graph Values",
        [&ctx, asset, after = std::move(after)]()
        {
            write_graph_material_graph(ctx, asset, after);
        },
        [&ctx, asset, before = std::move(before)]()
        {
            write_graph_material_graph(ctx, asset, before);
        });
}

/// Whether a parameter or texture of @p mat comes from a node of its own graph, so the graph's rows can edit it: one
/// inside a material function is the function's.
auto is_own_graph_node(const graph_material& mat, const std::string& node_id) -> bool
{
    return find_material_graph_node(mat.get_graph(), node_id) >= 0;
}

/// One parameter's row of a graph material: its default; an edit writes it into @p edited, a copy of the graph.
void draw_graph_default_row(rtti::context& ctx,
                            const graph_material& mat,
                            const material_graph_parameter& parameter,
                            hpp::optional<material_graph>& edited,
                            graph_row_changes& changes)
{
    auto value = parameter.default_value;
    const bool is_own = is_own_graph_node(mat, parameter.node_id);
    property_layout layout(make_graph_row_label(parameter.name),
                           make_graph_row_tooltip(parameter.description,
                                                  is_own ? "A parameter of the material graph: its default, which "
                                                           "material instances and scripts may change."
                                                         : "A parameter of a material function the graph calls: its "
                                                           "default is the function's."));
    ImGui::BeginDisabled(!is_own);
    const auto edit = inspect_vector_value(ctx, value, make_parameter_value_style(mat.get_graph(), parameter));
    ImGui::EndDisabled();
    changes.is_finished = changes.is_finished || edit.edit_finished;
    if(edit.changed)
    {
        if(!edited)
        {
            edited = mat.get_graph();
        }
        changes.is_changed = set_material_graph_parameter_default(*edited, parameter.node_id, value) || changes.is_changed;
    }
}

/// One texture node's row of a graph material: the texture it reads; picking one writes it into @p edited.
void draw_graph_default_texture_row(rtti::context& ctx,
                                    const graph_material& mat,
                                    const material_graph_texture& texture,
                                    hpp::optional<material_graph>& edited,
                                    graph_row_changes& changes)
{
    auto handle = texture.texture;
    const bool is_own = is_own_graph_node(mat, texture.node_id);
    property_layout layout(make_graph_row_label(texture.name),
                           make_graph_row_tooltip(texture.description,
                                                  is_own ? "A texture node of the material graph: the texture it reads."
                                                         : "A texture node of a material function the graph calls: its "
                                                           "texture is the function's."));
    entt::meta_any value = entt::forward_as_meta(handle);
    var_info info;
    info.is_property = true;
    info.read_only = !is_own;
    const auto result = inspect_var(ctx, value, make_proxy(value, texture.name), info);
    changes.is_finished = changes.is_finished || result.edit_finished;
    if(result.changed && is_own)
    {
        if(!edited)
        {
            edited = mat.get_graph();
        }
        changes.is_changed = set_material_graph_node_texture(*edited, texture.node_id, handle) || changes.is_changed;
    }
}

/// One named Static Switch's row: a checkbox; a change writes it into @p edited, and the material compiles again.
void draw_graph_static_switch_row(const graph_material& mat,
                                  const material_graph_static_switch& entry,
                                  hpp::optional<material_graph>& edited,
                                  graph_row_changes& changes)
{
    property_layout layout(make_graph_row_label(entry.name),
                           make_graph_row_tooltip(entry.description,
                                                  "A Static Switch of the material graph: it picks code when the "
                                                  "material compiles, so a change compiles the material again."));
    bool value = entry.value;
    if(!ImGui::Checkbox(("##" + entry.node_id).c_str(), &value))
    {
        return;
    }
    if(!edited)
    {
        edited = mat.get_graph();
    }
    const bool is_changed = set_material_graph_static_switch(*edited, entry.node_id, value);
    changes.is_changed = is_changed || changes.is_changed;
    changes.is_finished = is_changed || changes.is_finished;
}

/// The row naming how the graph describes the surface, and its hint.
constexpr const char* WORKFLOW_ROW_LABEL = "Workflow";
constexpr const char* WORKFLOW_ROW_TOOLTIP = "How the graph describes the surface, set on its Surface Output in the "
                                             "Material Graph. Metallic: Base Color, Metallic and Roughness. Specular: "
                                             "Base Color is the diffuse color, with Specular and Glossiness, which the "
                                             "shader converts per pixel.";

/// The surface's workflow, read only: the graph's Surface Output sets it, and the Material Graph edits it.
void draw_graph_workflow_row(rtti::context& ctx, const graph_material& mat)
{
    if(!match_row_filter(ctx, WORKFLOW_ROW_LABEL).is_shown)
    {
        return;
    }
    std::string text = get_material_graph_workflow_label(mat.get_graph());
    entt::meta_any var = entt::forward_as_meta(text);
    const auto var_proxy = make_proxy(var);
    var_info info;
    info.is_property = true;
    info.read_only = true;
    property_layout layout(WORKFLOW_ROW_LABEL, WORKFLOW_ROW_TOOLTIP);
    inspect_var(ctx, var, var_proxy, info);
}

/// A graph material's workflow, then its parameter, texture and switch rows, under their headers, as the inspector's
/// filter shows them.
void draw_graph_default_rows(rtti::context& ctx, const graph_material& mat, hpp::optional<material_graph>& edited, graph_row_changes& changes)
{
    draw_graph_workflow_row(ctx, mat);
    bool has_header = false;
    for(const auto& parameter : mat.get_parameters())
    {
        if(match_row_filter(ctx, make_graph_row_label(parameter.name)).is_shown)
        {
            if(!std::exchange(has_header, true))
            {
                draw_property_header("Parameters", true);
            }
            draw_graph_default_row(ctx, mat, parameter, edited, changes);
        }
    }
    has_header = false;
    const auto& textures = mat.get_textures();
    for(size_t index = 0; index < textures.size(); ++index)
    {
        const auto& texture = textures[index];
        if(is_first_texture_of_name(textures, index) && match_row_filter(ctx, make_graph_row_label(texture.name)).is_shown)
        {
            if(!std::exchange(has_header, true))
            {
                draw_property_header("Textures", true);
            }
            draw_graph_default_texture_row(ctx, mat, texture, edited, changes);
        }
    }
    has_header = false;
    for(const auto& entry : mat.get_static_switches())
    {
        if(match_row_filter(ctx, entry.name).is_shown)
        {
            if(!std::exchange(has_header, true))
            {
                draw_property_header("Switches", true);
            }
            draw_graph_static_switch_row(mat, entry, edited, changes);
        }
    }
}

/**
 * @brief Draws a graph material's parameters and textures as the instance draws them, and records what an edit or a
 * revert of one changed as the slot's undo step.
 */
auto inspect_graph_overrides(rtti::context& ctx,
                             material_instance& instance,
                             const graph_material& graph,
                             const meta_any_proxy& slot_proxy,
                             const entt::meta_custom& custom) -> inspect_result
{
    inspect_result result{};
    if(graph.get_parameters().empty() && graph.get_textures().empty())
    {
        return result;
    }
    const material_instance before = instance;
    hpp::optional<graph_override_ref> reverted;
    graph_row_changes changes;
    {
        property_override_scope marked(ctx, {}, make_graph_override_marks(instance, reverted));
        inspect_without_undo_scope no_undo;
        auto& view = ctx.get_cached<property_override_view>();
        view.push_segment(GRAPH_PARAMETERS_SEGMENT);
        for(const auto& parameter : graph.get_parameters())
        {
            view.push_segment(parameter.name);
            if(match_row_filter(ctx, make_graph_row_label(parameter.name)).is_shown)
            {
                draw_graph_parameter_row(ctx, instance, graph, parameter, changes);
            }
            view.pop_segment();
        }
        view.pop_segment();
        view.push_segment(GRAPH_TEXTURES_SEGMENT);
        const auto& textures = graph.get_textures();
        for(size_t index = 0; index < textures.size(); ++index)
        {
            const auto& texture = textures[index];
            if(!is_first_texture_of_name(textures, index))
            {
                continue;
            }
            view.push_segment(texture.name);
            if(match_row_filter(ctx, make_graph_row_label(texture.name)).is_shown)
            {
                draw_graph_texture_row(ctx, instance, texture, changes);
            }
            view.pop_segment();
        }
        view.pop_segment();
    }
    if(reverted)
    {
        reverted->is_texture ? instance.revert_graph_texture(reverted->name) : instance.revert_graph_parameter(reverted->name);
        changes = {true, true};
    }
    if(!changes.is_changed)
    {
        result.edit_finished = changes.is_finished;
        return result;
    }
    result.changed = true;
    result.edit_finished = changes.is_finished;
    meta_any_proxy step_proxy = slot_proxy;
    add_property_action(ctx,
                        ctx.get_cached<prefab_override_context>(),
                        result,
                        step_proxy,
                        entt::meta_any{before},
                        entt::meta_any{instance},
                        custom);
    return result;
}

/// The slot's value under the name of one of its rows: the undo step of that row's edit or revert takes the name, so
/// undo and redo point at the row, not at the end of the slot.
auto make_row_proxy(const meta_any_proxy& slot_proxy, const entt::meta_data& row) -> meta_any_proxy
{
    meta_any_proxy row_proxy;
    row_proxy.impl->parent = slot_proxy.impl;
    row_proxy.impl->type_name = slot_proxy.impl->type_name;
    row_proxy.impl->name = make_property_proxy_name(slot_proxy.impl->name, row);
    row_proxy.impl->getter = [parent_proxy = slot_proxy](entt::meta_any& result)
    {
        return parent_proxy.impl->getter(result);
    };
    row_proxy.impl->setter =
        [parent_proxy = slot_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
    {
        return parent_proxy.impl->setter(parent_proxy, value, execution_count);
    };
    return row_proxy;
}

/// Applies a row's revert, or the edits made on the view, to the instance, and records the change as the undo step of
/// the row that changed.
auto apply_row_change(rtti::context& ctx,
                      material_instance& instance,
                      entt::meta_any& view,
                      const material& values,
                      const meta_any_proxy& slot_proxy,
                      const hpp::optional<entt::id_type>& reverted,
                      const entt::meta_custom& custom) -> inspect_result
{
    const material_instance before = instance;
    entt::meta_data row{};
    if(reverted)
    {
        instance.revert(*reverted);
        row = view.type().data(*reverted);
    }
    else
    {
        const auto changes = override_changed_values(instance, values);
        if(!changes.overridden.empty())
        {
            row = view.type().data(changes.overridden.back());
        }
    }
    inspect_result result{};
    result.changed = true;
    result.edit_finished = reverted.has_value();
    meta_any_proxy step_proxy = row ? make_row_proxy(slot_proxy, row) : slot_proxy;
    add_property_action(ctx,
                        ctx.get_cached<prefab_override_context>(),
                        result,
                        step_proxy,
                        entt::meta_any{before},
                        entt::meta_any{instance},
                        custom);
    return result;
}

/// A color as the theme sets it: the value under the first push of it this frame.
auto get_theme_color(ImGuiCol color) -> ImVec4
{
    for(const auto& pushed : ImGui::GetCurrentContext()->ColorStack)
    {
        if(pushed.Col == color)
        {
            return pushed.BackupValue;
        }
    }
    return ImGui::GetStyleColorVec4(color);
}

/**
 * @brief While alive, text is drawn in the base font and the theme's colors when the slot is drawn as a prefab
 * instance's override: the slot is one prefab override, so its values show the material instance's own overrides.
 */
class unmarked_slot_scope
{
public:
    explicit unmarked_slot_scope(rtti::context& ctx)
    {
        const auto& prefab_marks = ctx.get_cached<prefab_override_context>();
        is_pushed_ = prefab_marks.is_active && prefab_marks.is_path_overridden();
        if(!is_pushed_)
        {
            return;
        }
        ImGui::PushFont(ImGui::Font::Regular);
        ImGui::PushStyleColor(ImGuiCol_Text, get_theme_color(ImGuiCol_Text));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, get_theme_color(ImGuiCol_CheckMark));
    }

    ~unmarked_slot_scope()
    {
        if(is_pushed_)
        {
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
        }
    }

    unmarked_slot_scope(const unmarked_slot_scope&) = delete;
    auto operator=(const unmarked_slot_scope&) -> unmarked_slot_scope& = delete;

private:
    bool is_pushed_{};
};
} // namespace

auto inspector_asset_handle_material::inspect_graph(rtti::context& ctx, const asset_handle<material>& data, graph_material& mat)
    -> inspect_result
{
    draw_graph_material_header(ctx, data, mat);
    hpp::optional<material_graph> edited;
    graph_row_changes changes;
    {
        inspect_without_undo_scope no_undo;
        draw_graph_default_rows(ctx, mat, edited, changes);
    }
    if(edited && changes.is_changed && is_code_edit(mat.get_graph(), *edited))
    {
        // A Static Switch is one finished step that changes the code: it goes to the file alone (write_graph_material_graph).
        record_graph_material_edit(ctx, data, mat.get_graph(), std::move(*edited));
        edited.reset();
    }
    if(edited && changes.is_changed)
    {
        // The rows edit the loaded material while a drag goes on; the file and the undo step take the edit once it
        // is finished.
        if(!graph_edit_ || graph_edit_->asset != data.uid())
        {
            graph_edit_ = graph_edit{data.uid(), mat.get_graph()};
        }
        mat.set_graph(std::move(*edited));
    }
    if(changes.is_finished && graph_edit_ && graph_edit_->asset == data.uid())
    {
        record_graph_material_edit(ctx, data, std::move(graph_edit_->before), mat.get_graph());
        graph_edit_.reset();
    }
    inspect_result result{};
    result.changed = changes.is_changed;
    result.edit_finished = changes.is_finished;
    return result;
}

auto inspector_material_instance::get_rows_object(const entt::meta_any& value) const -> entt::meta_any
{
    const auto* data = value.try_cast<material_instance>();
    const auto shared = data != nullptr ? data->get_shared_material().get() : nullptr;
    if(!shared || asset_handle<material>::is_empty(shared))
    {
        return {};
    }
    return make_material_view(*shared);
}

auto inspector_material_instance::holds_row_name(const entt::meta_any& value, const std::string& lowercase_text) const -> bool
{
    const auto* data = value.try_cast<material_instance>();
    const auto shared = data != nullptr ? data->get_shared_material().get() : nullptr;
    const auto* graph = shared ? shared->safe_cast<graph_material>() : nullptr;
    if(graph == nullptr)
    {
        return false;
    }
    const auto& parameters = graph->get_parameters();
    const auto& textures = graph->get_textures();
    return std::any_of(parameters.begin(),
                       parameters.end(),
                       [&](const material_graph_parameter& parameter)
                       {
                           return holds_text(make_graph_row_label(parameter.name), lowercase_text);
                       }) ||
           std::any_of(textures.begin(),
                       textures.end(),
                       [&](const material_graph_texture& texture)
                       {
                           return holds_text(make_graph_row_label(texture.name), lowercase_text);
                       });
}

auto inspector_material_instance::has_overridden_rows(const entt::meta_any& value) const -> bool
{
    const auto* data = value.try_cast<material_instance>();
    return data != nullptr && data->has_overrides();
}

auto inspector_material_instance::inspect(rtti::context& ctx,
                                          entt::meta_any& var,
                                          const meta_any_proxy& var_proxy,
                                          const var_info& info,
                                          const entt::meta_custom& custom) -> inspect_result
{
    auto& data = var.cast<material_instance&>();
    inspect_result result{};
    const auto& shared_handle = data.get_shared_material();
    const auto shared = shared_handle.get();
    if(!shared || asset_handle<material>::is_empty(shared))
    {
        ImGui::TextDisabled("The slot has no shared material.");
        return result;
    }
    // A block: the slot's row folds the values out, and they line up with the model's other rows.
    const unmarked_slot_scope unmarked(ctx);
    auto values = make_instance_values(data, *shared);
    auto view = make_material_view(*values);
    // A text search shows the matching rows only: the slot's status and its Revert All are no rows.
    const bool is_searching = !ctx.get_cached<inspector_context>().filter.text.empty();
    if(!is_searching)
    {
        draw_instance_status(data, view.type(), shared_handle.name());
    }
    // The values are edited on a copy, as the shared material with the overrides; what changed becomes overrides of
    // the instance, and the instance is what the model's undo step records. The rows are named after the slot, as
    // that step is (make_row_proxy).
    hpp::optional<entt::id_type> reverted;
    inspect_result view_result;
    {
        property_override_scope marked(ctx, {}, make_instance_marks(data, reverted));
        inspect_without_undo_scope no_undo;
        view_result = ::unravel::inspect_var(ctx, view, make_proxy(view, var_proxy.impl->name));
    }
    if(reverted || view_result.changed)
    {
        result |= apply_row_change(ctx, data, view, *values, var_proxy, reverted, custom);
    }
    result.edit_finished |= view_result.edit_finished;
    if(const auto* graph = shared->safe_cast<graph_material>())
    {
        result |= inspect_graph_overrides(ctx, data, *graph, var_proxy, custom);
    }
    if(data.has_overrides() && !is_searching)
    {
        if(ImGui::Button("Revert All"))
        {
            data.revert_all();
            result.changed = true;
            result.edit_finished = true;
        }
        ImGui::SetItemTooltipEx("Drop every override: the model draws exactly like its shared material.");
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

            auto importer = safe_pointer_cast<mesh_importer_meta>(base_importer);

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
    auto importer = safe_pointer_cast<animation_importer_meta>(meta.meta.importer);
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
    const auto applied = safe_pointer_cast<animation_importer_meta>(meta.meta.importer);
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
    ImGui::SetItemTooltipEx("Apply reimports the clip with them. Showing anything else in the inspector applies them too.");
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

            auto importer = safe_pointer_cast<animation_importer_meta>(base_importer);

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
    const bool is_same_prefab = inspected_uid_ == prefab.uid() && inspected_version_ == prefab.version();
    const bool cached = is_same_prefab && (inspected_failed_ || (inspected_root_ && inspected_root_.valid()));
    if(cached)
    {
        return inspected_root_;
    }

    inspected_scene_.unload();
    inspected_root_ = inspected_scene_.instantiate(prefab, false);
    inspected_uid_ = prefab.uid();
    inspected_version_ = prefab.version();
    // A prefab that did not load (broken, or in a layout this build refuses) has nothing to inspect.
    inspected_failed_ = !inspected_root_;
    if(inspected_failed_)
    {
        return inspected_root_;
    }

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
            else if(data && !prefab_entity)
            {
                const auto problem = editor_actions::find_newer_prefab_problem(data);
                ImGui::TextWrapped("%s",
                                   problem.empty() ? "This prefab could not be loaded. The console says why."
                                                   : problem.c_str());
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

            auto importer = safe_pointer_cast<audio_importer_meta>(base_importer);

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<audio_importer_meta>(*importer);
                }

                result |= inspect_audio_import_settings(ctx, *importer_);
            }
            draw_import_buttons(data.id(), meta.meta, importer, importer_);
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
