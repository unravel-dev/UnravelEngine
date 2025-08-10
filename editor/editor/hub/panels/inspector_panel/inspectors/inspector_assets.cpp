#include "inspector_assets.h"
#include "inspectors.h"

#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/audio/audio_clip.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/physics/physics_material.h>

#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/meta/rendering/texture.hpp>
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
                                             rttr::variant& var,
                                             const var_info& info,
                                             const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<gfx::texture>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    bool changed = false;
    if(inspected_asset_ != data || inspected_asset_.version() != data.version())
    {
        inspected_asset_ = data;
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
                const auto tex = data.get(false);
                if(tex)
                {
                    rttr::variant tex_var = tex->info;
                    var_info tex_var_info;
                    tex_var_info.read_only = true;

                    result |= ::unravel::inspect_var(ctx, tex_var, tex_var_info);
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
            ;

            if(importer)
            {
                if(!importer_)
                {
                    importer_ = std::make_shared<texture_importer_meta>(*importer);
                }

                result |= ::unravel::inspect(ctx, importer_.get());
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
                                              rttr::variant& var,
                                              const var_info& info,
                                              const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<material>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};
    {
        auto var = data.get(false);
        if(var)
        {
            result |= ::unravel::inspect(ctx, *var);
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
                                        rttr::variant& var,
                                        const var_info& info,
                                        const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<std::shared_ptr<material>>();


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
                result |= ::unravel::inspect(ctx, *data);

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
                                          rttr::variant& var,
                                          const var_info& info,
                                          const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<mesh>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_asset_.version() != data.version())
    {
        inspected_asset_ = data;
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
                const auto& mesh = data.get(false);
                if(mesh)
                {
                    mesh::info mesh_info;
                    mesh_info.vertices = mesh->get_vertex_count();
                    mesh_info.primitives = mesh->get_face_count();
                    mesh_info.submeshes = static_cast<std::uint32_t>(mesh->get_submeshes_count());
                    mesh_info.data_groups = static_cast<std::uint32_t>(mesh->get_data_groups_count());

                    rttr::variant var = mesh_info;
                    var_info mesh_var_info;
                    mesh_var_info.read_only = true;
                    result |= ::unravel::inspect_var(ctx, var, mesh_var_info);
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
                                               rttr::variant& var,
                                               const var_info& info,
                                               const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<animation_clip>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_asset_.version() != data.version())
    {
        inspected_asset_ = data;
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
                auto clip = data.get();
                rttr::variant clip_var = clip.get();
                var_info clip_var_info;
                clip_var_info.read_only = true;

                result |= ::unravel::inspect_var(ctx, clip_var, clip_var_info);
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

inspector_asset_handle_prefab::inspector_asset_handle_prefab()
{
    auto& ctx = engine::context();
    auto& ev = ctx.get_cached<events>();

    ev.on_script_recompile.connect(sentinel_, 1000, this, &inspector_asset_handle_prefab::on_script_recompile);
    ev.on_play_before_begin.connect(sentinel_, 1000, this, &inspector_asset_handle_prefab::reset_cache);
    ev.on_play_after_end.connect(sentinel_, 1000, this, &inspector_asset_handle_prefab::reset_cache);
}
void inspector_asset_handle_prefab::on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version)
{
    reset_cache(ctx);
}

void inspector_asset_handle_prefab::reset_cache(rtti::context& ctx)
{
    inspected_asset_ = {};
    inspected_scene_.unload();
    inspected_prefab_ = {};
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

void inspector_asset_handle_prefab::refresh(rtti::context& ctx)
{
    reset_cache(ctx);
}
auto inspector_asset_handle_prefab::inspect(rtti::context& ctx,
                                            rttr::variant& var,
                                            const var_info& info,
                                            const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<prefab>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    if(inspected_asset_ != data || inspected_asset_.version() != data.version())
    {
        inspected_scene_.unload();
        inspected_asset_ = data;
        inspected_prefab_ = inspected_scene_.instantiate(data);
    }

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
                rttr::variant var = inspected_prefab_;
                result |= inspect_var(ctx, var);

                if(result.changed)
                {
                    inspected_prefab_ = var.get_value<entt::handle>();
                }

                if(result.edit_finished)
                {
                    fs::path absolute_key = fs::absolute(fs::resolve_protocol(data.id()));
                    asset_writer::atomic_save_to_file(absolute_key.string(), inspected_prefab_);
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
                                                  rttr::variant& var,
                                                  const var_info& info,
                                                  const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<scene_prefab>>();

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
            if(data)
            {
                //                rttr::variant vari = &data.get();
                //                changed |= inspect_var(ctx, vari);
            }
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
                                                      rttr::variant& var,
                                                      const var_info& info,
                                                      const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<physics_material>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto var = data.get(false);
        if(var)
        {
            result |= ::unravel::inspect(ctx, *var);
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
                                                rttr::variant& var,
                                                const var_info& info,
                                                const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<audio_clip>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    auto& am = ctx.get_cached<asset_manager>();
    inspect_result result{};

    {
        auto var = data.get(false);
        if(var)
        {
            const auto& info = var->get_info();
            result |= ::unravel::inspect(ctx, info);

            inspect_clip(var);
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
                                                      rttr::variant& var,
                                                      const var_info& info,
                                                      const meta_getter& get_metadata) -> inspect_result
{
    auto& data = var.get_value<asset_handle<font>>();

    if(info.is_property)
    {
        return inspect_as_property(ctx, data);
    }

    inspect_result result{};

    {
        auto var = data.get(false);
        if(var)
        {
            result |= ::unravel::inspect(ctx, *var);
        }
    }


    return result;
}

} // namespace unravel
