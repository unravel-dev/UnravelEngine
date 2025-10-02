#include "inspector_assets.h"
#include "inspectors.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>
#include <engine/ui/ui_tree.h>
#include <engine/ui/style_sheet.h>

#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/ecs/components/prefab_component.h>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/texture.hpp>
#include <engine/meta/ui/ui_tree.hpp>
#include <engine/meta/ui/style_sheet.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/font.h>

#include <editor/editing/editing_manager.h>
#include <editor/editing/thumbnail_manager.h>

// must be below all
#include <engine/assets/impl/asset_writer.h>

#include <filesystem/filesystem.h>
#include <filesystem/watcher.h>
#include <graphics/texture.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/sequencer/imgui_neo_sequencer.h>
#include <logging/logging.h>

namespace unravel
{
namespace
{
auto resolve_path(const std::string& key) -> fs::path
{
    return fs::absolute(fs::resolve_protocol(key).string());
}

template<typename T>
auto reimport(const asset_handle<T>& asset)
{
    fs::watcher::touch(resolve_path(asset.id()), false);
}

template<typename T>
auto process_drag_drop_target(asset_manager& am, asset_handle<T>& entry) -> bool
{
    for(const auto& type : ex::get_suported_formats<T>())
    {
        if(ImGui::IsDragDropPossibleTargetForType(type.c_str()))
        {
            ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
            break;
        }
    }

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

template<typename T>
auto pick_asset(ImGuiTextFilter& filter,
                editing_manager& em,
                thumbnail_manager& tm,
                asset_manager& am,
                asset_handle<T>& data,
                const std::string& type) -> inspect_result
{
    inspect_result result{};

    auto fh = ImGui::GetFrameHeight();
    ImVec2 item_size = ImVec2(fh, fh) * 4.0f;
    ImGui::BeginGroup();
    if(data)
    {
        const auto& thumbnail = tm.get_thumbnail(data);

        ImVec2 texture_size = ImGui::GetSize(thumbnail, item_size);

        ImGui::ContentItem citem{};
        citem.texId = ImGui::ToId(thumbnail);
        citem.texture_size = texture_size;
        citem.image_size = item_size;

        if(ImGui::ContentButtonItem(citem))
        {
            em.focus(data);
            em.focus_path(fs::resolve_protocol(fs::path(data.id()).parent_path()));
        }

        ImGui::DrawItemActivityOutline();
    }
    else
    {
        ImGui::Dummy(item_size);
        ImGui::RenderFrameEx(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    }

    bool drag_dropped = process_drag_drop_target(am, data);
    result.changed |= drag_dropped;
    result.edit_finished |= drag_dropped;

    ImGui::SameLine();

    std::string item = data ? data.name() : fmt::format("None ({})", type);
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();

    auto popup_name = fmt::format("Pick {}", type);
    bool clicked = ImGui::Button(item.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));
    ImGui::DrawItemActivityOutline();

    ImGui::SetItemTooltipEx("%s\n\nPick an Asset", item.c_str());
    if(clicked)
    {
        filter.Clear();
        ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size * 0.4f);
        ImGui::OpenPopup(popup_name.c_str());
    }

    if(ImGui::Button(ICON_MDI_FILE_FIND))
    {
        em.focus(data);
        em.focus_path(fs::resolve_protocol(fs::path(data.id()).parent_path()));
    }
    ImGui::DrawItemActivityOutline();

    ImGui::SetItemTooltipEx("Locate the asset in the content browser.\n%s", data.id().c_str());

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    if(ImGui::Button(ICON_MDI_UNDO_VARIANT))
    {
        if(data)
        {
            data = asset_handle<T>::get_empty();
            result.changed = true;
            result.edit_finished = true;
        }
    }
    ImGui::DrawItemActivityOutline();

    ImGui::SetItemTooltipEx("Reset to default.");

    ImGui::EndGroup();

    bool open = true;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));
    if(ImGui::BeginPopupModal(popup_name.c_str(), &open))
    {
        if(!open)
        {
            ImGui::CloseCurrentPopup();
        }

        if(ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::DrawFilterWithHint(filter, "Search...", ImGui::GetContentRegionAvail().x);
        ImGui::DrawItemActivityOutline();

        auto assets = am.get_assets_with_predicate<T>(
            [&](const auto& asset)
            {
                const auto& id = asset.id();
                hpp::string_view id_view(id);
                return !id_view.starts_with("editor:/") && filter.PassFilter(asset.name().c_str());
            });

        const float size = 100.0f;

        ImGui::BeginChild("##items", {-1.0f, -1.0f});
        ImGui::ItemBrowser(size,
                           assets.size(),
                           [&](int index)
                           {
                               auto& asset = assets[index];
                               const auto& thumbnail = tm.get_thumbnail(asset);

                               ImVec2 item_size = {size, size};
                               ImVec2 texture_size = ImGui::GetSize(thumbnail, item_size);

                               // copy so that we can pass c_str
                               auto name = asset.name();

                               ImGui::ContentItem citem{};
                               citem.texId = ImGui::ToId(thumbnail);
                               citem.name = name.c_str();
                               citem.texture_size = texture_size;
                               citem.image_size = item_size;

                               if(ImGui::ContentButtonItem(citem))
                               {
                                   data = asset;
                                   result.changed = true;
                                   result.edit_finished = true;
                                   ImGui::CloseCurrentPopup();
                               }

                               ImGui::SetItemTooltipEx("%s", asset.name().c_str());
                           });

        ImGui::EndChild();

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    ImGui::EndGroup();

    return result;
}

template<typename T>
auto make_asset_instance_proxy(entt::meta_any& var, const meta_any_proxy& var_proxy) -> meta_any_proxy
{
    meta_any_proxy data_var_proxy;
    data_var_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
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
    data_var_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
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
    data_var_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
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
    data_var_proxy.impl->setter = [parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
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
    data_var_proxy.impl->get_name = [parent_proxy = var_proxy]()
    {
        return parent_proxy.impl->get_name();
    };
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
    
    data_var_proxy.impl->setter = [data, parent_proxy = var_proxy](meta_any_proxy& proxy, const entt::meta_any& value, uint64_t execution_count) mutable
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
                ImGui::SliderInt("Mip", &inspected_mip_, 0, tex->info.numMips - 1);
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

auto inspector_asset_handle_prefab::get_prefab_entity(rtti::context& ctx, const asset_handle<prefab>& prefab) -> entt::handle
{
    entt::handle instance{};
    auto view = inspected_scene_.registry->view<prefab_component>();
    view.each(
        [&](auto e, auto&& comp)
        {
            if(comp.source == prefab && comp.source.version() == inspected_version_)
            {
                instance = inspected_scene_.create_handle(e);
            }
        });
    if(!instance)
    {
        inspected_scene_.unload();
        instance = inspected_scene_.instantiate(prefab);
        inspected_version_ = prefab.version();
    }

    return instance;
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
    inspect_result result{};

    if(ImGui::BeginTabBar("asset_handle_prefab",
                          ImGuiTabBarFlags_NoCloseWithMiddleMouseButton | ImGuiTabBarFlags_FittingPolicyScroll))
    {
        if(ImGui::BeginTabItem(ex::get_type(data.extension()).c_str()))
        {
            ImGui::BeginChild(ex::get_type(data.extension()).c_str());

            if(data)
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
                reimport(data);
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
                reimport(data);
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

    if(ImGui::SliderFloat("##playing_offset", &current_time, 0.0f, total_time))
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

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    {
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
    }

    return result;
}


auto inspector_asset_handle_font::inspect_as_property(rtti::context& ctx,
                                                      asset_handle<font>& data)
    -> inspect_result
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
