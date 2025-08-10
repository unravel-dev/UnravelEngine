#include "content_browser_panel.h"
#include "../panel.h"
#include "../panels_defs.h"
#include "imgui_widgets/utils.h"
#include <editor/editing/editing_manager.h>
#include <editor/editing/thumbnail_manager.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/system/project_manager.h>
#include <editor/shortcuts.h>
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/ecs/components/id_component.h>
#include <engine/ecs/components/tag_component.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/meta/ecs/entity.hpp>
#include <engine/meta/physics/physics_material.hpp>
#include <engine/meta/rendering/material.hpp>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/font.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/script.h>

#include <engine/audio/audio_clip.h>
#include <engine/engine.h>
#include <engine/assets/impl/asset_writer.h>

#include <filedialog/filedialog.h>
#include <filesystem/watcher.h>
#include <hpp/utility.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>

namespace unravel
{
using namespace std::literals;
namespace
{

fs::path pending_rename;

auto get_new_file(const fs::path& path, const std::string& name, const std::string& ext = "") -> fs::path
{
    int i = 0;
    fs::error_code err;
    while(fs::exists(path / (fmt::format("{} ({})", name.c_str(), i) + ext), err))
    {
        ++i;
    }

    return path / (fmt::format("{} ({})", name.c_str(), i) + ext);
}

auto get_new_file_simple(const fs::path& path, const std::string& name, const std::string& ext = "") -> fs::path
{
    int i = 0;
    fs::error_code err;
    while(fs::exists(path / (fmt::format("{}{}", name.c_str(), i) + ext), err))
    {
        ++i;
    }

    return path / (fmt::format("{}{}", name.c_str(), i) + ext);
}

auto process_drag_drop_source(const gfx::texture::ptr& preview, const fs::path& absolute_path) -> bool
{
    if(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        const auto filename = absolute_path.filename();
        const std::string extension = filename.has_extension() ? filename.extension().string() : "folder";
        const std::string id = absolute_path.string();
        const std::string strfilename = filename.string();
        ImVec2 item_size = {64, 64};
        ImVec2 texture_size = ImGui::GetSize(preview);
        texture_size = ImMax(texture_size, item_size);

        ImGui::ContentItem citem{};
        citem.texId = ImGui::ToId(preview);
        citem.name = strfilename.c_str();
        citem.texture_size = texture_size;
        citem.image_size = item_size;

        ImGui::ContentButtonItem(citem);

        ImGui::SetDragDropPayload(extension.c_str(), id.data(), id.size());
        ImGui::EndDragDropSource();
        return true;
    }

    return false;
}

void process_drag_drop_target(const fs::path& absolute_path)
{
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

        fs::error_code err;
        if(fs::is_directory(absolute_path, err))
        {
            static const auto types = ex::get_all_formats();

            const auto process_drop = [&absolute_path](const std::string& type)
            {
                auto payload = ImGui::AcceptDragDropPayload(type.c_str());
                if(payload != nullptr)
                {
                    std::string data(reinterpret_cast<const char*>(payload->Data), std::size_t(payload->DataSize));
                    fs::path new_name = absolute_path / fs::path(data).filename();
                    if(data != new_name)
                    {
                        fs::error_code err;

                        if(!fs::exists(new_name, err))
                        {
                            fs::rename(data, new_name, err);
                        }
                    }
                }
                return payload;
            };

            for(const auto& asset_set : types)
            {
                for(const auto& type : asset_set)
                {
                    if(process_drop(type) != nullptr)
                    {
                        break;
                    }
                }
            }
            {
                process_drop("folder");
            }
            {
                {
                    auto payload = ImGui::AcceptDragDropPayload("entity");
                    if(payload != nullptr)
                    {
                        entt::handle dropped{};
                        std::memcpy(&dropped, payload->Data, size_t(payload->DataSize));
                        if(dropped)
                        {
                            auto& ctx = engine::context();
                            auto& em = ctx.get_cached<editing_manager>();

                            auto do_action = [&](entt::handle dropped)
                            {
                                auto& comp = dropped.get<tag_component>();
                                auto prefab_path = absolute_path / fs::path(comp.name + ".pfb").make_preferred();
                                asset_writer::atomic_save_to_file(prefab_path.string(), dropped);

                                auto& am = ctx.get_cached<asset_manager>();
                                auto key = fs::convert_to_protocol(prefab_path);
                                dropped.get_or_emplace<prefab_component>().source = am.get_asset<prefab>(key.generic_string());
                            };


                            if(em.is_selected(dropped))
                            {
                                for(auto e : em.try_get_selections_as<entt::handle>())
                                {
                                    if(e)
                                    {
                                        do_action(*e);
                                    }
                                }
                            }
                            else
                            {
                                do_action(dropped);
                            }

                        }
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

auto draw_item(const content_browser_item& item)
{
    bool is_directory = item.entry.entry.is_directory();
    const auto& absolute_path = item.entry.entry.path();
    const auto& name = item.entry.stem;
    const auto& filename = item.entry.filename;
    const auto& file_ext = item.entry.extension;
    const auto& file_type = ex::get_type(file_ext, is_directory);
    enum class entry_action
    {
        none,
        clicked,
        double_clicked,
        renamed,
        deleted,
        duplicate,
    };

    auto duplicate_entry = [&]()
    {
        fs::error_code err;
        const auto available = get_new_file(absolute_path.parent_path(), name, file_ext);
        fs::copy(absolute_path, available, fs::copy_options::overwrite_existing, err);
    };

    bool is_popup_opened = false;
    entry_action action = entry_action::none;

    bool open_rename_menu = false;

    ImGui::PushID(name.c_str());
    if(item.is_selected && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused())
    {
        if(ImGui::IsKeyPressed(shortcuts::rename_item))
        {
            open_rename_menu = true;
        }

        if(ImGui::IsKeyPressed(shortcuts::delete_item))
        {
            action = entry_action::deleted;
        }

        if(ImGui::IsItemCombinationKeyPressed(shortcuts::duplicate_item))
        {
            action = entry_action::duplicate;
        }
    }

    bool is_editing_label_after_create = pending_rename == absolute_path;
    if(is_editing_label_after_create)
    {
        open_rename_menu = true;
    }

    ImVec2 item_size = {item.size, item.size};
    ImVec2 texture_size = ImGui::GetSize(item.icon, item_size);

    auto pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

    auto file_type_font = ImGui::GetFont(ImGui::Font::Black);

    ImGui::ContentItem citem{};
    citem.texId = ImGui::ToId(item.icon);
    citem.name = name.c_str();
    citem.type = file_type.c_str();
    citem.type_font = file_type_font;
    citem.texture_size = texture_size;
    citem.image_size = item_size;

    // Track double-click state across frames
    static ImGuiID last_double_clicked_id = 0;
    static float last_double_click_time = -1.0f;
    const float double_click_timeout = 0.5f; // seconds
    
    ImGuiID current_id = ImGui::GetID(name.c_str());
    float current_time = ImGui::GetTime();
    
    bool button_clicked = false;//ImGui::ContentButtonItem(citem);

    if(!item.is_loading)
    {
        button_clicked = ImGui::ContentButtonItem(citem);
        ImGui::DrawItemActivityOutline(ImGui::OutlineFlags_All);

    }
    else
    {
        
        auto spinner_size = item_size.x;

        ImSpinner::Spinner<ImSpinner::SpinnerTypeT::e_st_eclipse>("spinner", 
            ImSpinner::Radius{spinner_size * 0.5f},
            ImSpinner::Thickness{6.0f},
            ImSpinner::Color{ImSpinner::white},
            ImSpinner::Speed{6.0f});
    }

    pos.y += ImGui::GetItemRectSize().y;

    ImGui::PopStyleVar();

    // Check for double-click
    bool is_double_clicked = ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left);
    if(is_double_clicked)
    {
        last_double_clicked_id = current_id;
        last_double_click_time = current_time;
        action = entry_action::double_clicked;
    }
    // Only handle regular click if it's not a double-click and not recently double-clicked
    else if(button_clicked && 
           !(last_double_clicked_id == current_id && 
             current_time - last_double_click_time < double_click_timeout))
    {
        action = entry_action::clicked;
    }

    // Check if this item just received focus through keyboard navigation
    if(ImGui::IsItemFocused())
    {
        // Use the new IsItemFocusChanged function to detect navigation focus changes
        if(ImGui::IsItemFocusChanged() && !item.is_selected)
        {
            APPLOG_INFO("Focus Changed");

            // Only trigger click when the item wasn't previously selected
            action = entry_action::clicked;
        }
        
        if(ImGui::IsKeyPressed(shortcuts::item_action) || ImGui::IsKeyPressed(shortcuts::item_action_alt))
        {
            action = entry_action::double_clicked;
        }

        if(ImGui::IsKeyPressed(shortcuts::item_cancel))
        {
            action = entry_action::none;
        }
    }

    if(ImGui::IsItemHovered())
    {
        if(item.on_double_click)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    ImGui::AddItemTooltipEx("%s", filename.c_str());

    if(!file_type.empty())
    {
        ImGui::PushFont(file_type_font);
        ImGui::AddItemTooltipEx("%s", file_type.c_str());
        ImGui::PopFont();
    }

    auto input_buff = ImGui::CreateInputTextBuffer(name);

    if(ImGui::BeginPopupContextItem("ENTRY_CONTEXT_MENU"))
    {
        is_popup_opened = true;
        
        if(ImGui::Selectable("Open in Explorer"))
        {
            fs::show_in_graphical_env(absolute_path);
        }
        if(ImGui::MenuItem("Rename", ImGui::GetKeyName(shortcuts::rename_item)))
        {
            open_rename_menu = true;
            ImGui::CloseCurrentPopup();
        }

        if(ImGui::MenuItem("Duplicate", ImGui::GetKeyCombinationName(shortcuts::duplicate_item).c_str()))
        {
            action = entry_action::duplicate;
            ImGui::CloseCurrentPopup();
        }

        if(ImGui::MenuItem("Delete", ImGui::GetKeyName(shortcuts::delete_item)))
        {
            action = entry_action::deleted;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const float rename_field_width = 150.0f;
    if(open_rename_menu)
    {
        ImGui::OpenPopup("ENTRY_RENAME_MENU");

        const auto& style = ImGui::GetStyle();
        float rename_field_with_padding = rename_field_width + style.WindowPadding.x * 2.0f;
        if(item.size < rename_field_with_padding)
        {
            auto diff = rename_field_with_padding - item.size;
            pos.x -= diff * 0.5f;
        }

        ImGui::SetNextWindowPos(pos);
    }

    if(ImGui::BeginPopup("ENTRY_RENAME_MENU"))
    {
        is_popup_opened = true;
        if(open_rename_menu)
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::PushItemWidth(rename_field_width);

        if(ImGui::InputTextWidget("##NAME",
                                  input_buff,
                                  false,
                                  ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            action = entry_action::renamed;
            ImGui::CloseCurrentPopup();
        }

        if(open_rename_menu)
        {
            ImGui::ActivateItemByID(ImGui::GetItemID());
        }

        if(is_editing_label_after_create && ImGui::IsItemKeyPressed(shortcuts::item_cancel))
        {
            action = entry_action::deleted;
        }

        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }
    if(item.is_selected)
    {
        ImGui::SetItemFocusFrame();
    }

    if(item.is_focused)
    {
        ImGui::SetItemFocusFrame(ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 0.0f, 1.0f)));
    }

    if(item.is_loading)
    {
        action = entry_action::none;
    }

    if(open_rename_menu)
    {
        if(item.on_click)
        {
            item.on_click();
        }
    }
    switch(action)
    {
        case entry_action::clicked:
        {
            pending_rename.clear();
            if(item.on_click)
            {
                item.on_click();
            }
        }
        break;
        case entry_action::double_clicked:
        {
            pending_rename.clear();

            if(item.on_double_click)
            {
                item.on_double_click();
            }
        }
        break;
        case entry_action::renamed:
        {
            pending_rename.clear();

            const std::string new_name = std::string(input_buff.data());
            if(new_name != name && !new_name.empty())
            {
                if(item.on_rename)
                {
                    item.on_rename(new_name);
                }
            }
        }
        break;
        case entry_action::deleted:
        {
            pending_rename.clear();

            if(item.on_delete)
            {
                item.on_delete();
            }
        }
        break;

        case entry_action::duplicate:
        {
            pending_rename.clear();
            duplicate_entry();
        }
        break;

        default:
            break;
    }

    if(!process_drag_drop_source(item.icon, absolute_path))
    {
        process_drag_drop_target(absolute_path);
    }

    ImGui::PopID();
    return is_popup_opened;
}

} // namespace
content_browser_panel::content_browser_panel(imgui_panels* parent) : parent_(parent)
{
}
void content_browser_panel::init(rtti::context& ctx)
{
}

void content_browser_panel::deinit(rtti::context& ctx)
{
    filter_ = {};
}

void content_browser_panel::on_frame_ui_render(rtti::context& ctx, const char* name)
{
    if(ImGui::Begin(name, nullptr))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        draw(ctx);

        handle_external_drop(ctx);
    }
    ImGui::End();
}

void content_browser_panel::handle_external_drop(rtti::context& ctx)
{
    if(!parent_->get_external_drop_in_progress())
    {
        const auto& files = parent_->get_external_drop_files();
        if(!files.empty())
        {
            on_import(ctx, files, cache_.get_path());

            parent_->clear_external_drop_files();
        }
    }
}

void content_browser_panel::draw(rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();

    const auto root_path = fs::resolve_protocol("app:/data");

    fs::error_code err;
    if(root_ != root_path || !fs::exists(cache_.get_path(), err))
    {
        root_ = root_path;
        set_cache_path(root_);
    }

    if(!em.focused_data.focus_path.empty())
    {
        set_cache_path(em.focused_data.focus_path);
        em.focused_data.focus_path.clear();
    }

    auto avail = ImGui::GetContentRegionAvail();
    if(avail.x < 1.0f || avail.y < 1.0f)
    {
        return;
    }

    if(ImGui::BeginChild("DETAILS_AREA",
                         avail * ImVec2(0.15f, 1.0f),
                         ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));

        if(fs::is_directory(root_path, err))
        {
            draw_details(ctx, root_path);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if(ImGui::BeginChild("EXPLORER"))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));
        draw_as_explorer(ctx, root_path);
    }
    ImGui::EndChild();

    const auto& current_path = cache_.get_path();
    process_drag_drop_target(current_path);

    if(refresh_ > 0)
    {
        refresh_--;
    }
}

void content_browser_panel::draw_details(rtti::context& ctx, const fs::path& path)
{
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;

        const auto& selected_path = cache_.get_path();
        if(selected_path == path)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if(refresh_ > 0 && (path == selected_path || fs::is_any_parent_path(path, selected_path)))
        {
            ImGui::SetNextItemOpen(true);
        }

        auto stem = path.stem();
        bool open = ImGui::TreeNodeEx(fmt::format("{} {}", ICON_MDI_FOLDER, stem.generic_string()).c_str(), flags);
        process_drag_drop_target(path);

        // Add context menu for the folder item using the refactored function
        context_menu(ctx, true, path);

        const bool clicked = !ImGui::IsItemToggledOpen() && ImGui::IsItemClicked(ImGuiMouseButton_Left);
        
        // Use the new IsItemFocusChanged function to detect navigation focus changes
        if (ImGui::IsItemFocused() && ImGui::IsItemFocusChanged())
        {
            // Item just received focus through keyboard navigation
            set_cache_path(path);
        }

        if(open)
        {
            const fs::directory_iterator it(path);
            for(const auto& p : it)
            {
                if(fs::is_directory(p.status()))
                {
                    const auto& path = p.path();
                    draw_details(ctx, path);
                }
            }

            ImGui::TreePop();
        }

        if(clicked)
        {
            set_cache_path(path);
        }
    }
}

void content_browser_panel::draw_as_explorer(rtti::context& ctx, const fs::path& root_path)
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();

    const float size = ImGui::GetFrameHeight() * 6.0f * scale_;
    const auto hierarchy = fs::split_until(cache_.get_path(), root_path);

    // Handle backspace key to navigate to parent directory
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(shortcuts::navigate_back) && 
        hierarchy.size() > 1)
    {
        // Navigate to parent directory
        fs::path parent_path = cache_.get_path().parent_path();
        if (fs::exists(parent_path) && parent_path != cache_.get_path())
        {
            set_cache_path(parent_path);
        }
    }

    ImGui::DrawFilterWithHint(filter_, ICON_MDI_FILE_SEARCH " Search...", 200.0f);
    ImGui::DrawItemActivityOutline();
    ImGui::SameLine();
    ImGui::Text("%s", ICON_MDI_HOME);
    ImGui::SameLine(0.0f, 0.0f);
    int id = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, 0.0f));

    for(const auto& dir : hierarchy)
    {
        const bool is_first = &dir == &hierarchy.front();
        const bool is_last = &dir == &hierarchy.back();
        ImGui::PushID(id++);

        if(!is_first)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("/");
            ImGui::SameLine(0.0f, 0.0f);
        }

        if(is_last)
        {
            ImGui::PushFont(ImGui::Font::Bold);
        }

        auto filename = dir.filename().string();
        if(is_first)
        {
            filename = fmt::format("app:/{}", filename);
        }
        const bool clicked = ImGui::Button(filename.c_str());

        if(is_last)
        {
            ImGui::PopFont();
        }
        ImGui::PopID();

        if(clicked)
        {
            set_cache_path(dir);
            break;
        }
        process_drag_drop_target(dir);
    }
    ImGui::PopStyleVar(2);

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x,
                       80.0f,
                       [&]()
                       {
                           ImGui::PushItemWidth(80.0f);
                           ImGui::SliderFloat("##scale", &scale_, 0.5f, 1.0f);
                           ImGui::SetItemTooltipEx("%s", "Icons scale");
                           ImGui::PopItemWidth();
                       });

    ImGui::Separator();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    fs::path current_path = cache_.get_path();

    if(ImGui::BeginChild("assets_content", ImGui::GetContentRegionAvail(), false, flags))
    {
        ImGui::PushWindowFontSize(16);

        bool is_popup_opened = false;

        auto process_cache_entry = [&](const auto& cache_entry)
        {
            const auto& absolute_path = cache_entry.entry.path();
            const auto& name = cache_entry.stem;
            const auto& filename = cache_entry.filename;
            const auto& relative = cache_entry.protocol_path;
            const auto& file_ext = cache_entry.extension;

            const auto on_rename = [&](const std::string& new_name)
            {
                fs::path new_absolute_path = absolute_path;
                new_absolute_path.remove_filename();
                new_absolute_path /= new_name + file_ext;
                fs::error_code err;
                fs::rename(absolute_path, new_absolute_path, err);
            };


            content_browser_item item(cache_entry);
            item.on_rename = on_rename;
            item.size = size;

            bool known = false;
            hpp::for_each_type<gfx::texture,
                               gfx::shader,
                               scene_prefab,
                               material,
                               physics_material,
                               audio_clip,
                               mesh,
                               prefab,
                               animation_clip,
                               font,
                               script>(
                [&](auto tag)
                {
                    if(known)
                    {
                        return;
                    }

                    using asset_t = typename std::decay_t<decltype(tag)>::type;

                    if(ex::is_format<asset_t>(file_ext))
                    {
                        known = true;
                        using entry_t = asset_handle<asset_t>;
                        const auto& entry = am.find_asset<asset_t>(relative);

                        item.icon = tm.get_thumbnail(entry);
                        item.is_selected = em.is_selected(entry);
                        item.is_focused = em.is_focused(entry);
                        item.is_loading = !entry.is_ready();
                        item.on_click = [&]()
                        {
                            em.select(entry, em.get_select_mode());
                        };

                        item.on_delete = [&]()
                        {
                            fs::error_code err;
                            fs::remove_all(absolute_path, err);

                            em.unselect(entry);
                        };

                        if constexpr(std::is_same_v<asset_t, scene_prefab>)
                        {
                            item.on_double_click = [&]()
                            {
                                editor_actions::open_scene_from_asset(ctx, entry);
                            };
                        }

                        if constexpr(std::is_same_v<asset_t, prefab>)
                        {
                            item.on_double_click = [&]()
                            {
                                auto& em = ctx.get_cached<editing_manager>();
                                auto& scene_panel = parent_->get_scene_panel();
                                
                                bool auto_save = scene_panel.get_auto_save_prefab();
                                em.enter_prefab_mode(ctx, entry, auto_save);
                            };
                        }

                        if constexpr(std::is_same_v<asset_t, script>)
                        {
                            item.on_double_click = [&]()
                            {
                                editor_actions::open_workspace_on_file(absolute_path);
                            };
                        }

                        is_popup_opened |= draw_item(item);
                    }
                });

            if(!known)
            {

                fs::error_code ec;
                using entry_t = fs::path;
                const entry_t& entry = absolute_path;
                item.icon = tm.get_thumbnail(entry);
                item.is_selected = em.is_selected(entry);
                item.is_focused = em.is_focused(entry);

                item.on_click = [&]()
                {
                    em.select(entry, em.get_select_mode());
                };

                item.on_delete = [&]()
                {
                    fs::error_code err;
                    fs::remove_all(absolute_path, err);

                    em.unselect(entry);
                };

                if(fs::is_directory(cache_entry.entry.status()))
                {
                    item.on_double_click = [&]()
                    {
                        current_path = entry;
                        em.try_unselect<entry_t>();
                    };
                }

                is_popup_opened |= draw_item(item);
            }
        };

        auto cache_size = cache_.size();

        if(!filter_.IsActive())
        {
            ImGui::ItemBrowser(size,
                               cache_size,
                               [&](int index)
                               {
                                   auto& cache_entry = cache_[index];
                                   process_cache_entry(cache_entry);
                               });
        }
        else
        {
            std::vector<fs::directory_cache::cache_entry> filtered_entries;
            for(size_t index = 0; index < cache_size; ++index)
            {
                const auto& cache_entry = cache_[index];

                const auto& name = cache_entry.stem;
                const auto& filename = cache_entry.filename;

                if(filter_.PassFilter(name.c_str()))
                {
                    filtered_entries.emplace_back(cache_entry);
                }
            }

            ImGui::ItemBrowser(size,
                               filtered_entries.size(),
                               [&](int index)
                               {
                                   auto& cache_entry = filtered_entries[index];
                                   process_cache_entry(cache_entry);
                               });
        }

        if(!is_popup_opened)
        {
            context_menu(ctx, false, cache_.get_path());
        }
        set_cache_path(current_path);

        ImGui::PopWindowFontSize();

        handle_window_empty_click(ctx);
    }
    ImGui::EndChild();
}

void content_browser_panel::handle_window_empty_click(rtti::context& ctx) const
{
    auto& em = ctx.get_cached<editing_manager>();
    if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if(!ImGui::IsAnyItemHovered())
        {
            em.unselect();
        }
    }
}

void content_browser_panel::context_menu(rtti::context& ctx, bool use_context_item, const fs::path& target_path)
{
    bool popup_opened = false;
    
    if (use_context_item)
    {
        popup_opened = ImGui::BeginPopupContextItem();
    }
    else
    {
        popup_opened = ImGui::BeginPopupContextWindowEx();
    }
    
    if (popup_opened)
    {
        set_cache_path(target_path);

        context_create_menu(ctx, target_path);

        ImGui::Separator();

        if(ImGui::Selectable("Open in Explorer"))
        {
            fs::show_in_graphical_env(target_path);
        }

        ImGui::Separator();

        if(ImGui::Selectable("Import..."))
        {
            import(ctx, target_path);
        }
        ImGui::SetItemTooltipEx("If import asset consists of multiple files,\n"
                                "just copy paste all the files the data folder.\n"
                                "Preferably in a new folder. The importer will\n"
                                "automatically pick them up as dependencies.");

        ImGui::EndPopup();
    }
}

void content_browser_panel::context_create_menu(rtti::context& ctx, const fs::path& target_path)
{
    if(ImGui::BeginMenu("Create"))
    {
        if(ImGui::MenuItem("Folder"))
        {
            const auto available = get_new_file(target_path, "New Folder");
            fs::error_code ec;
            fs::create_directory(available, ec);

            if(!ec)
            {
                pending_rename = available;
            }
        }

        ImGui::Separator();

        if(ImGui::MenuItem("C# Script"))
        {
            auto& am = ctx.get_cached<asset_manager>();

            const auto available =
                get_new_file_simple(target_path, "NewScriptComponent", ex::get_format<script>());

            fs::error_code ec;
            auto new_script_template =
                fs::resolve_protocol("engine:/data/scripts/template/TemplateComponent" + ex::get_format<script>());
            fs::copy(new_script_template, available, ec);

            if(!ec)
            {
                pending_rename = available;
            }
        }

        if(ImGui::MenuItem("Material"))
        {
            auto& am = ctx.get_cached<asset_manager>();

            const auto available = get_new_file(target_path, "New Material", ex::get_format<material>());
            const auto key = fs::convert_to_protocol(available).generic_string();

            auto new_mat_future = am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
            asset_writer::atomic_save_to_file(new_mat_future.id(), new_mat_future);

            {
                pending_rename = available;
            }
        }

        if(ImGui::MenuItem("Physics Material"))
        {
            auto& am = ctx.get_cached<asset_manager>();

            const auto available =
                get_new_file(target_path, "New Physics Material", ex::get_format<physics_material>());
            const auto key = fs::convert_to_protocol(available).generic_string();

            auto new_mat_future =
                am.get_asset_from_instance<physics_material>(key, std::make_shared<physics_material>());
            asset_writer::atomic_save_to_file(new_mat_future.id(), new_mat_future);

            {
                pending_rename = available;
            }
        }

        ImGui::EndMenu();
    }
}

void content_browser_panel::set_cache_path(const fs::path& path)
{
    if(cache_.get_path() == path)
    {
        return;
    }

    fs::pattern_filter filter;
    filter.add_include_pattern("*");
    filter.add_exclude_pattern("*" + ex::get_meta_format());
    cache_.set_path(path, filter);
    refresh_ = 3;
}

void content_browser_panel::import(rtti::context& ctx, const fs::path& target_path)
{
    std::vector<std::string> paths;
    if(native::open_files_dialog(paths, {}))
    {
        on_import(ctx, paths, target_path);
    }
}

void content_browser_panel::on_import(rtti::context& ctx, const std::vector<std::string>& paths, const fs::path& target_path)
{
    auto& ts = ctx.get_cached<threader>();

    for(auto& path : paths)
    {
        fs::path p = fs::path(path).make_preferred();
        fs::path filename = p.filename();

        APPLOG_INFO("Importing {0}", filename.string());
        auto task = ts.pool->schedule("Importing " + filename.extension().string(),
            [target_path](const fs::path& path, const fs::path& filename)
            {
                fs::error_code err;
                fs::path dir = target_path / filename;
                asset_writer::atomic_copy_file(path, dir, err);
            },
            p,
            filename);
    }
}
} // namespace unravel
