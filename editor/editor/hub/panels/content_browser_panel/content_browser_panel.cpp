#include "content_browser_panel.h"
#include <editor/events.h>
#include <editor/system/project_manager.h>
#include "../panel.h"
#include "../panel_toolbar.h"
#include "../panels_defs.h"
#include "filesystem/filesystem.h"
#include "imgui_widgets/utils.h"
#include <editor/editing/editing_manager.h>
#include <editor/editing/editor_actions.h>
#include <editor/editing/thumbnail_manager.h>
#include <editor/assets/asset_actions.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui_context_menu_style.h>
#include <editor/imgui/integration/imgui_messagebox.h>
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
#include <engine/meta/ui/ui_tree.hpp>
#include <engine/meta/ui/style_sheet.hpp>
#include <engine/physics/physics_material.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/font.h>
#include <engine/ui/ui_tree.h>
#include <engine/ui/style_sheet.h>
#include <engine/rendering/renderer.h>
#include <engine/scripting/script.h>

#include <engine/audio/audio_clip.h>
#include <engine/engine.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/assets/impl/asset_writer.h>

#include <filedialog/filedialog.h>
#include <filesystem/watcher.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string_utils/utils.h>
#include <hpp/utility.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_internal.h>
#include <imgui_widgets/imcoolbar.h>
#include <logging/logging.h>
#include <subprocess/subprocess.hpp>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>

namespace unravel
{
using namespace std::literals;
namespace
{
constexpr float CONTENT_SEARCH_FIELD_WIDTH = 220.0f;
constexpr float CONTENT_SEARCH_FIELD_MIN_WIDTH = 70.0f;
constexpr float CONTENT_SCALE_SLIDER_WIDTH = 100.0f;
/// Side of a grid item at scale 1, in frame heights.
constexpr float CONTENT_ITEM_SIZE_IN_FRAMES = 6.0f;

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

/// If the renamed file is a C# script whose class name still matches the old
/// file stem (i.e. the user never touched the file), keep them in sync by
/// renaming the class too. Uses word-boundary matching to avoid corrupting
/// identifiers that merely contain the stem.
void sync_script_class_name(const fs::path& script_path, const std::string& old_stem, const std::string& new_stem)
{
    // Only touch the file when both names are plain identifiers - anything
    // else can't be a class name (and could break the regex below).
    if(!asset_actions::is_valid_csharp_identifier(old_stem) ||
       !asset_actions::is_valid_csharp_identifier(new_stem))
    {
        return;
    }

    std::ifstream input(script_path);
    if(!input.is_open())
    {
        return;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    auto content = buffer.str();
    input.close();

    const std::regex identifier(fmt::format("\\b{}\\b", old_stem));
    if(!std::regex_search(content, identifier))
    {
        return;
    }

    content = std::regex_replace(content, identifier, new_stem);

    std::ofstream output(script_path);
    if(output.is_open())
    {
        output << content;
    }
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

                                // The entity may already be an instance of a different prefab.
                                // Its old bookkeeping is keyed by uids that also exist in the
                                // file just written (prefab uids survive the save), so keeping
                                // it would suppress those properties from the new asset on
                                // every resync - and a stale instance_id would claim a slot in
                                // a document that never wrote this instance. Becoming the
                                // source of a new prefab leaves nothing overridden by
                                // definition.
                                //
                                // Reset field by field, not replaced: emplace_or_replace on an
                                // existing component takes entt's patch path, which assigns a
                                // default-constructed component over the live one - owner
                                // handle included - and fires only on_update, so the owner
                                // never gets re-stamped. That null owner surfaced as a crash
                                // in the inspector's Apply All. And not removed-and-re-added
                                // either: the on_destroy hook strips prefab ids from the whole
                                // subtree.
                                // A nested instance saved as its own prefab is no longer its
                                // container's slot: the container's document would put the old
                                // instance back there on its next replay. The slot is stated
                                // removed on the container, and the new instance is the user's.
                                if(const auto* old_prefab = dropped.try_get<prefab_component>();
                                   old_prefab != nullptr && !old_prefab->instance_id.is_nil())
                                {
                                    const auto* trans = dropped.try_get<transform_component>();
                                    auto container = trans != nullptr
                                                         ? prefab_override_context::find_prefab_root_entity(trans->get_parent())
                                                         : entt::handle{};
                                    if(auto* container_prefab = container ? container.try_get<prefab_component>() : nullptr)
                                    {
                                        container_prefab->remove_instance(old_prefab->instance_id);
                                        container_prefab->changed = true;
                                    }
                                }
                                auto& prefab_comp = dropped.get_or_emplace<prefab_component>();
                                prefab_comp.clear_overrides();
                                prefab_comp.instance_id = {};
                                prefab_comp.instance_document = {};
                                prefab_comp.source = am.get_asset<prefab>(key.generic_string());
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

// Unity builds merge the anonymous namespaces of the editor's sources; the grid entry helpers' own
// namespace keeps their generic names from clashing.
namespace browser_entry
{
/// A second press this soon after a double click on the same entry is not a click.
constexpr float DOUBLE_CLICK_TIMEOUT = 0.5f;
constexpr float RENAME_FIELD_WIDTH = 150.0f;
constexpr size_t RENAME_BUFFER_SIZE = 64;

using name_buffer_t = std::array<char, RENAME_BUFFER_SIZE>;

/// What the user did to a grid entry this frame.
enum class entry_action
{
    none,
    clicked,
    double_clicked,
    renamed,
    deleted,
    canceled,
    duplicate,
};

/// What happened to one grid entry while it was drawn, acted on once it is through.
struct entry_interaction
{
    entry_action action{entry_action::none};
    bool open_rename_menu{};
    bool is_popup_opened{};
};

/// The label an entry is selected with, e.g. "crate (Mesh)".
auto get_selection_label(const content_browser_item& item) -> std::string
{
    return fmt::format("{} ({})", item.entry.stem, item.type);
}

/// Copies the entry next to itself under the first free "name (n)" file name.
void duplicate_entry(const fs::directory_cache::cache_entry& entry)
{
    fs::error_code err;
    const auto& absolute_path = entry.entry.path();
    const auto available = get_new_file(absolute_path.parent_path(), entry.stem, entry.extension);
    fs::copy(absolute_path, available, fs::copy_options::overwrite_existing, err);
}

/// The rename, delete and duplicate shortcuts of the selected entry, and the rename a just created
/// entry starts with.
void read_entry_shortcuts(const content_browser_item& item,
                          bool is_editing_label_after_create,
                          entry_interaction& interaction)
{
    if(item.is_selected && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused())
    {
        if(ImGui::IsKeyPressed(shortcuts::rename_item))
        {
            interaction.open_rename_menu = true;
        }
        if(ImGui::IsKeyPressed(shortcuts::delete_item))
        {
            interaction.action = entry_action::deleted;
        }
        if(ImGui::IsItemCombinationKeyPressed(shortcuts::duplicate_item))
        {
            interaction.action = entry_action::duplicate;
        }
    }
    if(is_editing_label_after_create)
    {
        interaction.open_rename_menu = true;
    }
}

/// Turns presses on the card drawn last, double clicks and keyboard navigation into the entry's
/// action.
void read_entry_clicks(const content_browser_item& item, bool is_card_pressed, entry_interaction& interaction)
{
    // The release of a double click's second press also reports a press; it is ignored for a
    // moment so the double click's action is not followed by a click.
    static ImGuiID last_double_clicked_id = 0;
    static float last_double_click_time = -1.0f;
    const ImGuiID current_id = ImGui::GetID(item.entry.stem.c_str());
    const auto current_time = float(ImGui::GetTime());
    const bool was_just_double_clicked = last_double_clicked_id == current_id &&
                                         current_time - last_double_click_time < DOUBLE_CLICK_TIMEOUT;
    if(ImGui::IsItemDoubleClicked(ImGuiMouseButton_Left))
    {
        last_double_clicked_id = current_id;
        last_double_click_time = current_time;
        interaction.action = entry_action::double_clicked;
    }
    else if(is_card_pressed && !was_just_double_clicked)
    {
        interaction.action = entry_action::clicked;
    }
    if(!ImGui::IsItemFocused())
    {
        return;
    }
    // Keyboard navigation selects the entry it lands on.
    if(ImGui::IsItemFocusChanged() && !item.is_selected)
    {
        interaction.action = entry_action::clicked;
    }
    if(ImGui::IsKeyPressed(shortcuts::item_action) || ImGui::IsKeyPressed(shortcuts::item_action_alt))
    {
        interaction.action = entry_action::double_clicked;
    }
    if(ImGui::IsKeyPressed(shortcuts::item_cancel))
    {
        interaction.action = entry_action::none;
    }
}

/// The entry's right-click menu: show in the explorer, copy the path, reimport, rename, duplicate
/// and delete.
void draw_entry_context_menu(const fs::path& absolute_path, entry_interaction& interaction)
{
    if(!ImGui::BeginPopupContextItem("ENTRY_CONTEXT_MENU"))
    {
        return;
    }
    interaction.is_popup_opened = true;
    {
        ImGui::ContextMenuStyleScope style_scope;
        if(ImGui::MenuItemIcon(ICON_MDI_FOLDER_OPEN, "Open in Explorer"))
        {
            fs::show_in_graphical_env(absolute_path);
        }
        if(ImGui::MenuItemIcon(ICON_MDI_LINK, "Copy Path"))
        {
            const std::string protocol_path = fs::convert_to_protocol(absolute_path).generic_string();
            ImGui::SetClipboardText(protocol_path.c_str());
        }
        const bool can_reimport_file = asset_actions::can_reimport(absolute_path);
        if(ImGui::MenuItemIcon(ICON_MDI_REFRESH, "Reimport", nullptr, can_reimport_file))
        {
            asset_actions::reimport_path(absolute_path);
        }
        ImGui::Separator();
        if(ImGui::MenuItemIcon(ICON_MDI_PENCIL, "Rename", ImGui::GetKeyName(shortcuts::rename_item)))
        {
            interaction.open_rename_menu = true;
            ImGui::CloseCurrentPopup();
        }
        if(ImGui::MenuItemIcon(ICON_MDI_CONTENT_COPY,
                               "Duplicate",
                               ImGui::GetKeyCombinationName(shortcuts::duplicate_item).c_str()))
        {
            interaction.action = entry_action::duplicate;
            ImGui::CloseCurrentPopup();
        }
        if(ImGui::MenuItemIcon(ICON_MDI_DELETE, "Delete", ImGui::GetKeyName(shortcuts::delete_item)))
        {
            interaction.action = entry_action::deleted;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

/// The rename field, opened at popup_pos (under the card) and centered on it when wider; Enter
/// renames, and Escape on a just created entry cancels its creation.
void draw_entry_rename_popup(const content_browser_item& item,
                             ImVec2 popup_pos,
                             bool is_editing_label_after_create,
                             name_buffer_t& name_buffer,
                             entry_interaction& interaction)
{
    if(interaction.open_rename_menu)
    {
        ImGui::OpenPopup("ENTRY_RENAME_MENU");
        const float field_with_padding = RENAME_FIELD_WIDTH + ImGui::GetStyle().WindowPadding.x * 2.0f;
        if(item.size < field_with_padding)
        {
            popup_pos.x -= (field_with_padding - item.size) * 0.5f;
        }
        ImGui::SetNextWindowPos(popup_pos);
    }
    if(!ImGui::BeginPopup("ENTRY_RENAME_MENU"))
    {
        return;
    }
    interaction.is_popup_opened = true;
    if(interaction.open_rename_menu)
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushItemWidth(RENAME_FIELD_WIDTH);
    if(ImGui::InputTextWidget("##NAME",
                              name_buffer,
                              false,
                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
    {
        interaction.action = entry_action::renamed;
        ImGui::CloseCurrentPopup();
    }
    if(interaction.open_rename_menu)
    {
        ImGui::ActivateItemByID(ImGui::GetItemID());
    }
    if(is_editing_label_after_create && ImGui::IsItemKeyPressed(shortcuts::item_cancel))
    {
        interaction.action = entry_action::canceled;
    }
    ImGui::PopItemWidth();
    ImGui::EndPopup();
}

/// Runs the entry's action once its card, menu and popups are drawn.
void apply_entry_action(const content_browser_item& item, entry_action action, const std::string& typed_name)
{
    if(action != entry_action::none)
    {
        pending_rename.clear();
    }
    const auto run = [](const content_browser_item::on_action_t& callback)
    {
        if(callback)
        {
            callback();
        }
    };
    switch(action)
    {
        case entry_action::clicked:
            run(item.on_click);
            break;
        case entry_action::double_clicked:
            run(item.on_double_click);
            break;
        case entry_action::renamed:
            if(item.on_rename && !typed_name.empty() && typed_name != item.entry.stem)
            {
                item.on_rename(typed_name);
            }
            break;
        case entry_action::deleted:
            run(item.on_delete);
            break;
        case entry_action::duplicate:
            duplicate_entry(item.entry);
            break;
        case entry_action::canceled:
            run(item.on_cancel);
            break;
        default:
            break;
    }
}

/// Draws one grid entry and acts on what the user did to it; returns true while one of its
/// popups is open.
auto draw_item(const content_browser_item& item) -> bool
{
    const auto& absolute_path = item.entry.entry.path();
    entry_interaction interaction;
    ImGui::PushID(item.entry.stem.c_str());
    const bool is_editing_label_after_create = pending_rename == absolute_path;
    read_entry_shortcuts(item, is_editing_label_after_create, interaction);
    const ImVec2 card_pos = ImGui::GetCursorScreenPos();
    const bool is_card_pressed = draw_content_card(item);
    const ImVec2 rename_pos(card_pos.x, card_pos.y + ImGui::GetItemRectSize().y);
    // The card draws its own hover and selection, so the outline only highlights the active card.
    ImGui::DrawItemActivityOutline(ImGui::OutlineFlags_WhenActive | ImGui::OutlineFlags_HighlightActive);
    read_entry_clicks(item, is_card_pressed, interaction);
    if(item.on_double_click && ImGui::IsItemHovered())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    draw_content_tooltip(item);
    draw_entry_context_menu(absolute_path, interaction);
    auto name_buffer = ImGui::CreateInputTextBuffer<RENAME_BUFFER_SIZE>(item.entry.stem);
    draw_entry_rename_popup(item, rename_pos, is_editing_label_after_create, name_buffer, interaction);
    if(item.is_loading)
    {
        interaction.action = entry_action::none;
    }
    if(interaction.open_rename_menu && item.on_click)
    {
        item.on_click();
    }
    apply_entry_action(item, interaction.action, std::string(name_buffer.data()));
    if(!process_drag_drop_source(item.icon, absolute_path))
    {
        process_drag_drop_target(absolute_path);
    }
    ImGui::PopID();
    return interaction.is_popup_opened;
}
} // namespace browser_entry

} // namespace
content_browser_panel::content_browser_panel(imgui_panels* parent, const char* name) : panel_base(name), parent_(parent)
{
}
void content_browser_panel::init(rtti::context& ctx)
{
    auto& ui_ev = ctx.get_cached<ui_events>();
    ui_ev.on_close_project.connect(sentinel_, 100, this, &content_browser_panel::on_project_closed);
}

void content_browser_panel::on_project_closed(rtti::context& /*ctx*/)
{
    cache_.clear();
    root_.clear();
}

void content_browser_panel::deinit(rtti::context& ctx)
{
    filter_ = {};
}

auto content_browser_panel::get_window_flags() const -> ImGuiWindowFlags
{
    return 0;
}

void content_browser_panel::draw_ui(rtti::context& ctx)
{
    draw(ctx);
    handle_external_drop(ctx);
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
    auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        if(!cache_.get_path().empty())
        {
            on_project_closed(ctx);
        }
        return;
    }

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
        reveal_path_ = em.focused_data.focus_path;
        set_cache_path(reveal_path_.parent_path());
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
            draw_folder_tree(ctx, root_path);
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if(ImGui::BeginChild("EXPLORER"))
    {
        // ImGui::WindowTimeBlock block(ImGui::GetFont(ImGui::Font::Mono));
        draw_explorer(ctx, root_path);
    }
    ImGui::EndChild();

    const auto& current_path = cache_.get_path();
    process_drag_drop_target(current_path);

    if(refresh_ > 0)
    {
        refresh_--;
    }

    draw_external_drop_overlay();
}

void content_browser_panel::draw_external_drop_overlay() const
{
    if(parent_ == nullptr || !parent_->get_external_drop_in_progress())
    {
        return;
    }

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window == nullptr)
    {
        return;
    }

    const ImRect bounds(window->InnerRect.Min, window->InnerRect.Max);


    if(bounds.GetWidth() < 1.0f || bounds.GetHeight() < 1.0f)
    {
        return;
    }

    // Foreground draw list renders above all panel children without affecting layout/scroll.
    ImDrawList* draw_list = ImGui::GetForegroundDrawList(window->Viewport);
    draw_list->PushClipRect(bounds.Min, bounds.Max, true);

    draw_list->AddRectFilled(bounds.Min,
                             bounds.Max,
                             ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg, 0.72f));

    const ImU32 border_color = ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.95f);
    draw_list->AddRect(bounds.Min, bounds.Max, border_color, 0.0f, 0, 2.0f);

    const char* headline = ICON_MDI_IMPORT "  Drop to import";
    const std::string folder_line = fmt::format("Import into: {}", cache_.get_path().generic_string());
    const char* hint = "Release to add files to this folder";

    ImFont* headline_font = ImGui::GetFont(ImGui::Font::Bold);
    if(headline_font == nullptr)
    {
        headline_font = ImGui::GetFont();
    }
    ImFont* body_font = ImGui::GetFont();

    constexpr float card_padding = 28.0f;
    constexpr float line_spacing = 10.0f;

    const float headline_font_size = headline_font->LegacySize * 1.65f;
    const float body_font_size = body_font->LegacySize * 1.5f;

    const ImVec2 headline_size = headline_font->CalcTextSizeA(headline_font_size, FLT_MAX, 0.0f, headline);
    const ImVec2 folder_size = body_font->CalcTextSizeA(body_font_size, FLT_MAX, 0.0f, folder_line.c_str());
    const ImVec2 hint_size = body_font->CalcTextSizeA(body_font_size, FLT_MAX, 0.0f, hint);

    const float card_width =
        std::max({headline_size.x, folder_size.x, hint_size.x}) + card_padding * 2.0f;
    const float card_height =
        headline_size.y + folder_size.y + hint_size.y + line_spacing * 2.0f + card_padding * 2.0f;

    const ImVec2 center = bounds.GetCenter();
    const ImVec2 card_min(center.x - card_width * 0.5f, center.y - card_height * 0.5f);
    const ImVec2 card_max(center.x + card_width * 0.5f, center.y + card_height * 0.5f);

    draw_list->AddRectFilled(card_min, card_max, ImGui::GetColorU32(ImGuiCol_PopupBg, 0.98f), 8.0f);
    draw_list->AddRect(card_min, card_max, border_color, 8.0f, 0, 1.5f);

    ImVec2 text_pos(card_min.x + card_padding, card_min.y + card_padding);
    draw_list->AddText(headline_font,
                       headline_font_size,
                       text_pos,
                       ImGui::GetColorU32(ImGuiCol_Text),
                       headline);

    text_pos.y += headline_size.y + line_spacing;
    draw_list->AddText(body_font,
                       body_font_size,
                       text_pos,
                       ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       folder_line.c_str());

    text_pos.y += folder_size.y + line_spacing;
    draw_list->AddText(body_font,
                       body_font_size,
                       text_pos,
                       ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       hint);

    draw_list->PopClipRect();
}

void content_browser_panel::draw_folder_tree(rtti::context& ctx, const fs::path& path)
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
    // The id is the name alone, so the icon can follow the open state without changing it.
    const std::string name = path.stem().generic_string();
    const std::string node_id = fmt::format("###{}", name);
    const bool was_open = ImGui::GetStateStorage()->GetInt(ImGui::GetID(node_id.c_str()), 0) != 0;
    const char* icon = was_open ? ICON_MDI_FOLDER_OPEN : ICON_MDI_FOLDER;
    const bool open = ImGui::TreeNodeEx(fmt::format("{} {}{}", icon, name, node_id).c_str(), flags);
    process_drag_drop_target(path);
    context_menu(ctx, true, path);
    const bool clicked = !ImGui::IsItemToggledOpen() && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    // Keyboard navigation through the tree browses the folders as it goes.
    if(ImGui::IsItemFocused() && ImGui::IsItemFocusChanged())
    {
        set_cache_path(path);
    }
    if(open)
    {
        const fs::directory_iterator it(path);
        for(const auto& p : it)
        {
            if(fs::is_directory(p.status()))
            {
                draw_folder_tree(ctx, p.path());
            }
        }
        ImGui::TreePop();
    }
    if(clicked)
    {
        set_cache_path(path);
    }
}

void content_browser_panel::draw_explorer(rtti::context& ctx, const fs::path& root_path)
{
    handle_navigate_back(root_path);
    draw_toolbar(ctx, root_path);
    const float status_bar_height = ImGui::GetFrameHeightWithSpacing();
    const ImVec2 assets_size(0.0f, ImMax(ImGui::GetContentRegionAvail().y - status_bar_height, 1.0f));
    const size_t shown_count = draw_assets(ctx, assets_size);
    draw_status_bar(shown_count);
}

void content_browser_panel::handle_navigate_back(const fs::path& root_path)
{
    const bool is_listening = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive();
    if(!is_listening || !ImGui::IsKeyPressed(shortcuts::navigate_back))
    {
        return;
    }
    const bool is_at_root = fs::split_until(cache_.get_path(), root_path).size() <= 1;
    if(is_at_root)
    {
        return;
    }
    const fs::path parent_path = cache_.get_path().parent_path();
    if(fs::exists(parent_path) && parent_path != cache_.get_path())
    {
        set_cache_path(parent_path);
    }
}

void content_browser_panel::draw_toolbar(rtti::context& ctx, const fs::path& root_path)
{
    if(panel_toolbar::begin_strip("##content_toolbar"))
    {
        draw_add_dropdown(ctx);
        panel_toolbar::separator();
        draw_breadcrumb(root_path);
        panel_toolbar::align_right();
        draw_search_field();
    }
    panel_toolbar::end_strip();
}

void content_browser_panel::draw_add_dropdown(rtti::context& ctx)
{
    if(!panel_toolbar::begin_dropdown("##add", ICON_MDI_PLUS " Add", "Create or import assets in this folder"))
    {
        return;
    }
    {
        ImGui::ContextMenuStyleScope style_scope;
        const fs::path target_path = cache_.get_path();
        context_create_menu(ctx, target_path);
        draw_import_menu_item(ctx, target_path);
    }
    panel_toolbar::end_dropdown();
}

void content_browser_panel::draw_breadcrumb(const fs::path& root_path)
{
    const auto hierarchy = fs::split_until(cache_.get_path(), root_path);
    int step = 0;
    for(const auto& dir : hierarchy)
    {
        const bool is_root = step == 0;
        if(!is_root)
        {
            panel_toolbar::path_separator();
        }
        const std::string name = dir.filename().string();
        const std::string text = is_root ? fmt::format("{} app:/{}", ICON_MDI_HOME, name) : name;
        const std::string id = fmt::format("##step_{}", step++);
        if(panel_toolbar::button(id.c_str(), text.c_str(), nullptr))
        {
            // The steps behind this one are gone with the click.
            set_cache_path(dir);
            break;
        }
        process_drag_drop_target(dir);
    }
}

void content_browser_panel::draw_search_field()
{
    const float width =
        panel_toolbar::calc_flexible_width(CONTENT_SEARCH_FIELD_MIN_WIDTH, CONTENT_SEARCH_FIELD_WIDTH);
    panel_toolbar::begin_field(width);
    ImGui::DrawFilterWithHint(filter_, ICON_MDI_MAGNIFY " Search...", width);
    ImGui::DrawItemActivityOutline();
    panel_toolbar::end_field();
}

auto content_browser_panel::passes_filter(asset_manager& am,
                                          const fs::directory_cache::cache_entry& cache_entry) const -> bool
{
    if(filter_.PassFilter(cache_entry.stem.c_str()))
    {
        return true;
    }
    const auto& type = ex::get_type(cache_entry.extension, cache_entry.entry.is_directory());
    if(filter_.PassFilter(type.c_str()))
    {
        return true;
    }
    const auto& metadata = am.get_metadata_for_path(cache_entry.entry.path()).meta;
    return filter_.PassFilter(metadata.uid.to_string().c_str());
}

auto content_browser_panel::collect_shown_entries(asset_manager& am) const -> std::vector<size_t>
{
    std::vector<size_t> shown_entries;
    shown_entries.reserve(cache_.size());
    for(size_t index = 0; index < cache_.size(); ++index)
    {
        if(!filter_.IsActive() || passes_filter(am, cache_[index]))
        {
            shown_entries.emplace_back(index);
        }
    }
    return shown_entries;
}

auto content_browser_panel::find_shown_index(const std::vector<size_t>& shown_entries, const fs::path& path) const -> int
{
    if(path.empty())
    {
        return -1;
    }
    const auto found = std::find_if(shown_entries.begin(),
                                    shown_entries.end(),
                                    [&](size_t index)
                                    {
                                        return cache_[index].entry.path() == path;
                                    });
    return found == shown_entries.end() ? -1 : int(std::distance(shown_entries.begin(), found));
}

auto content_browser_panel::draw_assets(rtti::context& ctx, const ImVec2& size) -> size_t
{
    size_t shown_count = 0;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoSavedSettings;
    if(ImGui::BeginChild("assets_content", size, false, flags))
    {
        auto& am = ctx.get_cached<asset_manager>();
        const float item_size = ImGui::GetFrameHeight() * CONTENT_ITEM_SIZE_IN_FRAMES * scale_;
        const std::vector<size_t> shown_entries = collect_shown_entries(am);
        shown_count = shown_entries.size();
        const int reveal_index = find_shown_index(shown_entries, reveal_path_);
        reveal_path_.clear();
        // A double click on a folder lands here and is applied once the grid is through.
        fs::path current_path = cache_.get_path();
        bool is_popup_opened = false;
        ImGui::ItemBrowser(
            item_size,
            shown_entries.size(),
            [&](int index)
            {
                const auto& cache_entry = cache_[shown_entries[index]];
                is_popup_opened |= draw_cache_entry(ctx, cache_entry, item_size, current_path);
            },
            reveal_index);
        if(!is_popup_opened)
        {
            context_menu(ctx, false, cache_.get_path());
        }
        set_cache_path(current_path);
        handle_window_empty_click(ctx);
    }
    ImGui::EndChild();
    return shown_count;
}

auto content_browser_panel::draw_cache_entry(rtti::context& ctx,
                                             const fs::directory_cache::cache_entry& cache_entry,
                                             float item_size,
                                             fs::path& current_path) -> bool
{
    content_browser_item item(cache_entry);
    item.size = item_size;
    setup_rename_handler(item);
    if(!setup_known_asset_item(ctx, item))
    {
        setup_path_item(ctx, item, current_path);
    }
    return browser_entry::draw_item(item);
}

auto content_browser_panel::setup_known_asset_item(rtti::context& ctx, content_browser_item& item) -> bool
{
    bool is_known_asset = false;
    hpp::for_each_type<gfx::texture,
                       gfx::shader,
                       scene_prefab,
                       material,
                       physics_material,
                       ui_tree,
                       style_sheet,
                       audio_clip,
                       mesh,
                       prefab,
                       animation_clip,
                       font,
                       script>(
        [&](auto tag)
        {
            using asset_t = typename std::decay_t<decltype(tag)>::type;
            if(!is_known_asset && ex::is_format<asset_t>(item.entry.extension))
            {
                is_known_asset = true;
                setup_asset_item<asset_t>(ctx, item);
            }
        });
    return is_known_asset;
}

void content_browser_panel::setup_path_item(rtti::context& ctx, content_browser_item& item, fs::path& current_path)
{
    auto& em = ctx.get_cached<editing_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    const fs::path& entry = item.entry.entry.path();
    item.icon = tm.get_thumbnail(entry);
    item.is_selected = em.is_selected(entry);
    item.is_focused = em.is_focused(entry);
    item.on_click = [&em, entry, &item]()
    {
        em.select(entry, em.get_select_mode(), browser_entry::get_selection_label(item));
    };
    setup_delete_handler(item, entry, ctx);
    if(item.is_folder())
    {
        item.on_double_click = [&current_path, &em, entry]()
        {
            current_path = entry;
            em.try_unselect<fs::path>();
        };
    }
}

void content_browser_panel::draw_status_bar(size_t shown_count)
{
    const std::string summary = filter_.IsActive() ? fmt::format("{} of {} items", shown_count, cache_.size())
                                                   : fmt::format("{} items", shown_count);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", summary.c_str());
    ImGui::SameLine();
    ImGui::AlignedItem(1.0f,
                       ImGui::GetContentRegionAvail().x,
                       CONTENT_SCALE_SLIDER_WIDTH,
                       [&]()
                       {
                           ImGui::PushItemWidth(CONTENT_SCALE_SLIDER_WIDTH);
                           ImGui::KnobSliderScalarT("##scale", &scale_, 0.5f, 1.0f);
                           ImGui::SetItemTooltipEx("%s", "Icons scale");
                           ImGui::PopItemWidth();
                       });
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
    const bool opened = use_context_item ? ImGui::BeginPopupContextItem()
                                         : ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight);
    if(!opened)
    {
        return;
    }

    {
        ImGui::ContextMenuStyleScope style_scope;

        set_cache_path(target_path);

        context_create_menu(ctx, target_path);


        if(ImGui::MenuItemIcon(ICON_MDI_FOLDER_OPEN, "Open in Explorer"))
        {
            fs::show_in_graphical_env(target_path);
        }


        draw_import_menu_item(ctx, target_path);
    }
    ImGui::EndPopup();
}

void content_browser_panel::draw_import_menu_item(rtti::context& ctx, const fs::path& target_path)
{
    if(ImGui::MenuItemIcon(ICON_MDI_IMPORT, "Import..."))
    {
        import(ctx, target_path);
    }
    ImGui::SetItemTooltipEx("If import asset consists of multiple files,\n"
                            "just copy paste all the files the data folder.\n"
                            "Preferably in a new folder. The importer will\n"
                            "automatically pick them up as dependencies.");
}

void content_browser_panel::context_create_menu(rtti::context& ctx, const fs::path& target_path)
{
    if(ImGui::BeginMenuIcon(ICON_MDI_PLUS, "Create"))
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
            const auto available =
                get_new_file_simple(target_path, "NewScriptComponent", ex::get_format<script>());

            // The template lives outside the compiled scripts tree (.cs.in)
            // so it never ends up in the engine assembly. Instantiate it with
            // the unique file stem as the class name: the file must be valid,
            // collision-free C# from the moment it exists, because a
            // recompile can trigger before the user finishes renaming.
            auto new_script_template = fs::resolve_protocol("engine:/data/templates/TemplateComponent" +
                                                            ex::get_format<script>() + ".in");

            if(asset_actions::create_script_from_template(new_script_template, available))
            {
                pending_rename = available;
            }
        }

        ImGui::Separator();

        if(ImGui::MenuItem(ex::get_type<material>().c_str()))
        {
            auto& am = ctx.get_cached<asset_manager>();

            auto new_name = fmt::format("New {}", ex::get_type<material>());
            const auto available = get_new_file(target_path, new_name, ex::get_format<material>());
            const auto key = fs::convert_to_protocol(available).generic_string();

            auto new_mat_future = am.get_asset_from_instance<material>(key, std::make_shared<pbr_material>());
            asset_writer::atomic_save_to_file(new_mat_future.id(), new_mat_future);

            {
                pending_rename = available;
            }
        }

        if(ImGui::MenuItem(ex::get_type<physics_material>().c_str()))
        {
            auto& am = ctx.get_cached<asset_manager>();

            auto new_name = fmt::format("New {}", ex::get_type<physics_material>());
            const auto available =
                get_new_file(target_path, new_name, ex::get_format<physics_material>());
            const auto key = fs::convert_to_protocol(available).generic_string();

            auto new_mat_future =
                am.get_asset_from_instance<physics_material>(key, std::make_shared<physics_material>());
            asset_writer::atomic_save_to_file(new_mat_future.id(), new_mat_future);

            {
                pending_rename = available;
            }
        }

        ImGui::Separator();

        if(ImGui::MenuItem(ex::get_type<ui_tree>().c_str()))
        {
            auto& am = ctx.get_cached<asset_manager>();

            auto new_name = fmt::format("New {}", ex::get_type<ui_tree>());
            const auto available =
                get_new_file(target_path, new_name, ex::get_format<ui_tree>());
            const auto key = fs::convert_to_protocol(available).generic_string();


            fs::error_code err;
            asset_writer::atomic_write_file(
            available,
            [&](const fs::path& temp)
            {
                fs::error_code ec;
                fs::copy(fs::resolve_protocol("engine:/data/ui/template.rhtml"), available, ec);
            },
            err);

            {
                pending_rename = available;
            }
        }

        if(ImGui::MenuItem(ex::get_type<style_sheet>().c_str()))
        {
            auto& am = ctx.get_cached<asset_manager>();

            auto new_name = fmt::format("New {}", ex::get_type<style_sheet>());
            const auto available =
                get_new_file(target_path, new_name, ex::get_format<style_sheet>());
            const auto key = fs::convert_to_protocol(available).generic_string();

            fs::error_code err;
            asset_writer::atomic_write_file(
            available,
            [&](const fs::path& temp)
            {
                fs::error_code ec;
                fs::copy(fs::resolve_protocol("engine:/data/ui/template.rcss"), available, ec);
            },
            err);

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

    auto resolved = fs::resolve_protocol("app:/data");
    
    
    fs::error_code ec;
    if(!fs::equivalent(resolved, path, ec))
    {
        if(!fs::is_any_parent_path(resolved, path))
        {
            return;
        }
    }


    if(!fs::exists(path, ec))
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
    editor_actions::import_files(ctx, paths, target_path, true);
}

void content_browser_panel::prompt_delete_asset(const std::string& name, const std::function<void()>& on_delete)
{
    ImBox::ShowDeleteConfirmation("Delete selected asset?",
        fmt::format("{}\n\nYou cannot undo the delete asset action.", name),
        [on_delete](ImBox::ModalResult result)
        {
            if(result == ImBox::ModalResult::Delete)
            {
                on_delete();
            }
        });
}

template<typename EntryType>
void content_browser_panel::setup_delete_handler(content_browser_item& item, const EntryType& entry, rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    const std::string& relative = item.entry.protocol_path;
    const fs::path& absolute_path = item.entry.entry.path();
    item.on_delete = [this, relative, absolute_path, &em, entry]()
    {
        auto delete_impl = [&em, absolute_path, entry]()
        {
            fs::error_code err;
            fs::remove_all(absolute_path, err);
            em.unselect(entry);  // Works for both asset handles and fs::path
        };
        this->prompt_delete_asset(relative, delete_impl);
    };
    item.on_cancel = [absolute_path, &em, entry]()
    {
        fs::error_code err;
        fs::remove_all(absolute_path, err);
        em.unselect(entry);  // Works for both asset handles and fs::path
    };
}

void content_browser_panel::setup_rename_handler(content_browser_item& item)
{
    const fs::path& absolute_path = item.entry.entry.path();
    const std::string& file_ext = item.entry.extension;
    item.on_rename = [absolute_path, file_ext](const std::string& new_name)
    {
        fs::path new_absolute_path = absolute_path;
        new_absolute_path.remove_filename();
        new_absolute_path /= new_name + file_ext;
        fs::error_code err;
        fs::rename(absolute_path, new_absolute_path, err);
        if(!err && file_ext == ex::get_format<script>())
        {
            sync_script_class_name(new_absolute_path, absolute_path.stem().string(), new_name);
        }
    };
}

template<typename AssetType>
void content_browser_panel::setup_asset_item(rtti::context& ctx, content_browser_item& item)
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    const auto& entry = am.find_asset<AssetType>(item.entry.protocol_path);
    item.uid = entry.uid();
    item.icon = tm.get_thumbnail(entry);
    item.is_selected = em.is_selected(entry);
    item.is_focused = em.is_focused(entry);
    item.is_loading = !entry.is_ready();
    item.summary = get_asset_summary(entry);
    item.collect_details = [entry](content_item_sections& sections)
    {
        collect_asset_details(entry, sections);
    };
    item.on_click = [&em, entry, &item]()
    {
        em.select(entry, em.get_select_mode(), browser_entry::get_selection_label(item));
    };
    setup_delete_handler(item, entry, ctx);
    if constexpr(std::is_same_v<AssetType, scene_prefab>)
    {
        item.on_double_click = [&ctx, entry]()
        {
            editor_actions::open_scene_from_asset(ctx, entry);
        };
    }
    else if constexpr(std::is_same_v<AssetType, prefab>)
    {
        item.on_double_click = [this, &ctx, entry]()
        {
            auto& em_local = ctx.get_cached<editing_manager>();
            auto& scene_panel = parent_->get_scene_panel();
            bool auto_save = scene_panel.get_auto_save_prefab();
            em_local.enter_prefab_mode(ctx, entry, auto_save);
        };
    }
    else if constexpr(std::is_same_v<AssetType, script> ||
                      std::is_same_v<AssetType, gfx::shader> ||
                      std::is_same_v<AssetType, style_sheet> ||
                      std::is_same_v<AssetType, ui_tree>)
    {
        item.on_double_click = [absolute_path = item.entry.entry.path()]()
        {
            editor_actions::open_workspace_on_file(absolute_path);
        };
    }
    // Other asset types have no double-click action.
}

} // namespace unravel
