#include "content_browser_panel.h"
#include <editor/events.h>
#include <editor/system/project_manager.h>
#include "../panel.h"
#include "../panels_defs.h"
#include "filesystem/filesystem.h"
#include "imgui_widgets/utils.h"
#include <editor/editing/editing_manager.h>
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

/// Instantiate a script template, substituting #SCRIPTNAME# with the
/// destination file stem (which doubles as the class name).
auto create_script_from_template(const fs::path& template_path, const fs::path& dst) -> bool
{
    std::ifstream input(template_path);
    if(!input.is_open())
    {
        APPLOG_ERROR("Script template not found: {}", template_path.string());
        return false;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    auto content = buffer.str();

    string_utils::alterable::replace(content, "#SCRIPTNAME#", dst.stem().string());

    std::ofstream output(dst);
    if(!output.is_open())
    {
        APPLOG_ERROR("Failed to create script file: {}", dst.string());
        return false;
    }
    output << content;
    return true;
}

auto is_valid_csharp_identifier(const std::string& name) -> bool
{
    if(name.empty() || (std::isdigit(static_cast<unsigned char>(name.front())) != 0))
    {
        return false;
    }
    return std::all_of(name.begin(),
                       name.end(),
                       [](unsigned char c)
                       {
                           return std::isalnum(c) != 0 || c == '_';
                       });
}

/// If the renamed file is a C# script whose class name still matches the old
/// file stem (i.e. the user never touched the file), keep them in sync by
/// renaming the class too. Uses word-boundary matching to avoid corrupting
/// identifiers that merely contain the stem.
void sync_script_class_name(const fs::path& script_path, const std::string& old_stem, const std::string& new_stem)
{
    // Only touch the file when both names are plain identifiers - anything
    // else can't be a class name (and could break the regex below).
    if(!is_valid_csharp_identifier(old_stem) || !is_valid_csharp_identifier(new_stem))
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

// Formats a raw byte count as a compact, human friendly string (e.g. "1.4 MB").
auto format_file_size(std::uintmax_t bytes) -> std::string
{
    constexpr std::array<const char*, 5> units{"B", "KB", "MB", "GB", "TB"};
    auto value = static_cast<double>(bytes);
    int unit = 0;
    while(value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    if(unit == 0)
    {
        return fmt::format("{} {}", bytes, units[0]);
    }
    return fmt::format("{:.1f} {}", value, units[unit]);
}

namespace
{

struct asset_tooltip_style_scope
{
    static constexpr int k_style_var_count = 4;
    static constexpr int k_style_color_count = 2;

    asset_tooltip_style_scope()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 5.0f));

        ImVec4 window_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        window_bg.x = std::min(window_bg.x + 0.035f, 1.0f);
        window_bg.y = std::min(window_bg.y + 0.035f, 1.0f);
        window_bg.z = std::min(window_bg.z + 0.035f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, window_bg);

        ImVec4 border = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        border.w = std::min(border.w * 1.35f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, border);
    }

    ~asset_tooltip_style_scope()
    {
        ImGui::PopStyleColor(k_style_color_count);
        ImGui::PopStyleVar(k_style_var_count);
    }

    asset_tooltip_style_scope(const asset_tooltip_style_scope&) = delete;
    asset_tooltip_style_scope& operator=(const asset_tooltip_style_scope&) = delete;
};

auto draw_asset_tooltip_thumbnail(const ImGui::ContentItem& citem, ImVec2 texture_size, float thumb_side) -> void
{
    constexpr float thumb_rounding = 8.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, thumb_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 52));
    if(ImGui::BeginChild("asset_tooltip_thumb",
                         ImVec2(thumb_side, thumb_side),
                         ImGuiChildFlags_None,
                         ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::ImageWithAspect(citem.texId, texture_size, ImVec2(thumb_side, thumb_side), ImVec2(0.5f, 0.5f));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

auto draw_asset_tooltip_detail_row(const char* label,
                                   float label_width,
                                   float wrap_width,
                                   const std::string& value) -> void
{
    if(value.empty())
    {
        return;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(label_width);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

} // namespace

// Near-white, theme-independent caption color shared by the card type label and the tooltip type label
// so they stay consistent and never pick up an off-palette tint.
constexpr ImU32 content_caption_color = IM_COL32(224, 226, 231, 255);

// Returns a distinct accent color per asset type so cards read at a glance, similar to the colored
// type bar Unreal shows beneath each asset thumbnail.
auto asset_type_accent(const char* type) -> ImU32
{
    constexpr ImU32 fallback = IM_COL32(150, 150, 158, 255);
    if(type == nullptr || type[0] == '\0')
    {
        return fallback;
    }

    struct type_color
    {
        const char* name;
        ImU32 color;
    };
    static constexpr std::array<type_color, 13> table{{
        {"Texture", IM_COL32(226, 96, 92, 255)},
        {"Material", IM_COL32(86, 180, 168, 255)},
        {"Physics Material", IM_COL32(214, 124, 72, 255)},
        {"Mesh", IM_COL32(234, 138, 64, 255)},
        {"Shader", IM_COL32(156, 116, 222, 255)},
        {"Prefab", IM_COL32(82, 179, 222, 255)},
        {"Scene", IM_COL32(232, 168, 70, 255)},
        {"Animation Clip", IM_COL32(124, 200, 96, 255)},
        {"Audio Clip", IM_COL32(220, 112, 178, 255)},
        {"Script", IM_COL32(94, 172, 206, 255)},
        {"Font", IM_COL32(186, 186, 196, 255)},
        {"UI Tree", IM_COL32(126, 138, 224, 255)},
        {"Style Sheet", IM_COL32(170, 134, 224, 255)},
    }};

    for(const auto& entry : table)
    {
        if(std::strcmp(type, entry.name) == 0)
        {
            return entry.color;
        }
    }
    return fallback;
}

// Draws a clean, professional asset card: the thumbnail centered on top (aspect preserved), a colored
// type-accent bar beneath it, then the asset name with a subtle type caption. The card has no hard
// outline; it uses a faint tile that brightens on hover/active and the theme selection color when
// selected, so the look stays consistent across editor themes. Registers a single ImGui item so all
// surrounding interaction (selection, focus, drag-drop, context menu) keeps working unchanged.
auto draw_content_card(const ImGui::ContentItem& item, bool selected) -> bool
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if(window->SkipItems)
    {
        return false;
    }

    ImDrawList* draw_list = window->DrawList;
    const ImGuiID id = window->GetID(item.name);

    constexpr float rounding = 6.0f;
    const ImVec2 inner_pad(6.0f, 6.0f);
    const float card_w = item.image_size.x > 0.0f ? item.image_size.x : ImGui::GetFrameHeight() * 4.0f;
    const float content_w = card_w - inner_pad.x * 2.0f;
    const float thumb_h = content_w; // Square thumbnail region.

    const bool has_name = (item.name != nullptr) && (item.name[0] != '\0') && (item.name[0] != '#');
    const bool has_type = (item.type != nullptr) && (item.type[0] != '\0') && (item.type[0] != '#');
    const bool is_folder = has_type && (std::strcmp(item.type, "Folder") == 0);
    const bool show_accent = has_type && !is_folder;

    // Keep the type caption clearly secondary to the name. The type uses a heavy font, so size it
    // relative to the name (not its own native size) to guarantee it stays smaller and reads as a caption.
    const float base_font_size = ImGui::GetFontSize();
    const float name_font_size = item.name_font != nullptr ? item.name_font->LegacySize : base_font_size;
    const float type_font_size = name_font_size * 0.8f;

    const auto line_height = [](ImFont* font, float size) -> float
    {
        if(font == nullptr)
        {
            return ImGui::GetTextLineHeight();
        }
        ImGui::PushFont(font, size);
        const float height = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        return height;
    };

    // Reserve the name and caption rows (and the accent strip) unconditionally so every card is the same
    // height and the grid rows stay aligned, even for folders and unknown file types.
    const float name_h = line_height(item.name_font, name_font_size);
    const float type_h = line_height(item.type_font, type_font_size);

    const float accent_h = ImGui::GetStyle().SeparatorSize + 1;
    constexpr float pad_thumb_to_accent = 4.0f;
    constexpr float pad_accent_to_name = 4.0f;
    constexpr float pad_name_to_type = 1.0f;

    const float card_h = inner_pad.y + thumb_h + pad_thumb_to_accent + accent_h + pad_accent_to_name + name_h +
                         pad_name_to_type + type_h + inner_pad.y;

    const ImVec2 card_min = window->DC.CursorPos;
    const ImVec2 card_max = card_min + ImVec2(card_w, card_h);
    const ImRect bb(card_min, card_max);

    ImGui::ItemSize(bb);
    if(!ImGui::ItemAdd(bb, id))
    {
        return false;
    }

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    // Faint tile background, no hard outline. Brighten on hover/active; use the theme selection color
    // (plus a thin accent ring) when selected so it matches whatever editor theme is active.
    if(selected)
    {
        draw_list->AddRectFilled(card_min, card_max, ImGui::GetColorU32(ImGuiCol_Header), rounding);
        draw_list->AddRect(card_min, card_max, ImGui::GetColorU32(ImGuiCol_NavCursor), rounding, 0, 1.5f);
    }
    else if(!is_folder || hovered || held)
    {
        // Folders blend into the panel when idle (no tile, no border); everything else keeps a faint tile.
        ImU32 tile = IM_COL32(255, 255, 255, 10);
        if(held)
        {
            tile = IM_COL32(255, 255, 255, 32);
        }
        else if(hovered)
        {
            tile = IM_COL32(255, 255, 255, 20);
        }
        draw_list->AddRectFilled(card_min, card_max, tile, rounding);
    }

    // Thumbnail image, aspect preserved and centered.
    const ImVec2 thumb_min = card_min + inner_pad;
    const ImVec2 thumb_max = thumb_min + ImVec2(content_w, thumb_h);
    if(item.texId)
    {
        ImVec2 img = item.texture_size;
        if(img.x > 0.0f && img.y > 0.0f)
        {
            const float scale = ImMin(content_w / img.x, thumb_h / img.y);
            img.x *= scale;
            img.y *= scale;
            const ImVec2 img_min(thumb_min.x + (content_w - img.x) * 0.5f, thumb_min.y + (thumb_h - img.y) * 0.5f);
            const ImVec2 img_max = img_min + img;
            draw_list->AddImageRounded(item.texId,
                                       img_min,
                                       img_max,
                                       item.uv0,
                                       item.uv1,
                                       ImGui::GetColorU32(item.tint_col),
                                       rounding * 0.5f);
        }
    }

    // Colored type-accent bar beneath the thumbnail (skipped for folders / unknown types).
    float cursor_y = thumb_max.y + pad_thumb_to_accent;
    if(show_accent)
    {
        draw_list->AddRectFilled(ImVec2(thumb_min.x, cursor_y),
                                 ImVec2(thumb_max.x, cursor_y + accent_h),
                                 asset_type_accent(item.type),
                                 accent_h * 0.5f);
    }
    cursor_y += accent_h + pad_accent_to_name;

    // Draws a single line of text centered within the card content width, ellipsized when too wide.
    const auto draw_centered_label = [&](const char* text, ImFont* font, float font_size, ImU32 color) -> void
    {
        if(font != nullptr)
        {
            ImGui::PushFont(font, font_size);
        }
        ImVec2 ts = ImGui::CalcTextSize(text, nullptr, true);
        ImVec2 start(thumb_min.x, cursor_y);
        const float region_w = thumb_max.x - thumb_min.x;
        if(region_w > ts.x)
        {
            start.x += (region_w - ts.x) * 0.5f;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextEllipsis(draw_list,
                                  start,
                                  ImVec2(thumb_max.x, cursor_y + ts.y),
                                  thumb_max.x,
                                  text,
                                  nullptr,
                                  &ts);
        ImGui::PopStyleColor();
        if(font != nullptr)
        {
            ImGui::PopFont();
        }
    };

    if(has_name)
    {
        draw_centered_label(item.name, item.name_font, name_font_size, ImGui::GetColorU32(ImGuiCol_Text));
    }
    cursor_y += name_h + pad_name_to_type;

    if(has_type && show_accent)
    {
        draw_centered_label(item.type, item.type_font, type_font_size, content_caption_color);
    }

    return pressed;
}

auto draw_item(const content_browser_item& item)
{
    bool is_directory = item.entry.entry.is_directory();
    const auto& absolute_path = item.entry.entry.path();
    const auto& name = item.entry.stem;
    const auto& filename = item.entry.filename;
    const auto& file_ext = item.entry.extension;
    const auto& file_type = ex::get_type(file_ext, is_directory);
    auto description = item.description;
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
    citem.description = description.c_str();
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
    
    bool button_clicked = false;

    if(!item.is_loading)
    {
        button_clicked = draw_content_card(citem, item.is_selected);
        // The card renders its own hover/selection visuals, so keep only the active (click/keyboard)
        // highlight and drop the inactive/hovered outlines that otherwise box every item (notably the
        // now-transparent idle folders).
        ImGui::DrawItemActivityOutline(ImGui::OutlineFlags_WhenActive | ImGui::OutlineFlags_HighlightActive);

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

    const bool show_shift_preview_tooltip =
        ImGui::IsItemHovered() && !item.is_loading && ImGui::GetIO().KeyShift;
    if(show_shift_preview_tooltip)
    {
        ImGui::SetNextWindowViewportToCurrent();
        ImGui::SetNextWindowPos(ImGui::GetIO().MousePos, ImGuiCond_None, ImVec2(0.5f, 1.0f));
        asset_tooltip_style_scope tooltip_style;

        if(ImGui::BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None))
        {
            constexpr float preview_scale = 2.75f;
            constexpr float preview_max_side = 384.0f;
            const float preview_side = ImClamp(item.size * preview_scale, item.size + 16.0f, preview_max_side);
            ImGui::PushID("shift_thumbnail_preview");
            ImGui::ContentItem preview_item = citem;
            preview_item.image_size = ImVec2(preview_side, preview_side);
            ImGui::ContentButtonItem(preview_item);
            ImGui::PopID();
            ImGui::EndTooltip();
        }
    }
    else if(!item.is_loading && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        constexpr float thumb_side = 72.0f;
        constexpr float wrap_width = 360.0f;
        ImGui::SetNextWindowViewportToCurrent();
        asset_tooltip_style_scope tooltip_style;
        if(ImGui::BeginTooltipEx(ImGuiTooltipFlags_None, ImGuiWindowFlags_None))
        {
            // Header: rounded thumbnail well next to the name and type.
            draw_asset_tooltip_thumbnail(citem, texture_size, thumb_side);
            ImGui::SameLine();
            ImGui::BeginGroup();
            {
                auto name_font = ImGui::GetFont(ImGui::Font::Bold);
                ImGui::PushFont(name_font, name_font->LegacySize);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width - thumb_side - ImGui::GetStyle().ItemSpacing.x);
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopFont();

                if(!file_type.empty())
                {
                    ImGui::PushFont(file_type_font, file_type_font->LegacySize * 0.9f);
                    ImGui::PushStyleColor(ImGuiCol_Text, content_caption_color);
                    ImGui::TextUnformatted(file_type.c_str());
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
            }
            ImGui::EndGroup();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Separator, asset_type_accent(file_type.c_str()));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Spacing();

            const float label_width = 130.0f;

            draw_asset_tooltip_detail_row("Name", label_width, wrap_width, filename);
            draw_asset_tooltip_detail_row("Path", label_width, wrap_width, item.entry.protocol_path);

            if(!is_directory)
            {
                fs::error_code ec;
                const auto bytes = fs::file_size(absolute_path, ec);
                if(!ec)
                {
                    draw_asset_tooltip_detail_row("Disk Size", label_width, wrap_width, format_file_size(bytes));
                }

                const auto compiled_path =
                    asset_reader::resolve_compiled_asset_path(item.entry.protocol_path, file_ext);
                if(!compiled_path.empty())
                {
                    ec.clear();
                    if(fs::exists(compiled_path, ec))
                    {
                        const auto compiled_bytes = fs::file_size(compiled_path, ec);
                        if(!ec)
                        {
                            draw_asset_tooltip_detail_row("Compiled Disk Size",
                                                          label_width,
                                                          wrap_width,
                                                          format_file_size(compiled_bytes));
                        }
                    }
                }
            }

            if(!is_directory)
            {
                draw_asset_tooltip_detail_row("UID", label_width, wrap_width, description);
            }
            ImGui::EndTooltip();
        }
    }

    auto input_buff = ImGui::CreateInputTextBuffer(name);

    if(ImGui::BeginPopupContextItem("ENTRY_CONTEXT_MENU"))
    {
        is_popup_opened = true;
        {
            ImGui::ContextMenuStyleScope style_scope;

            if(ImGui::MenuItemIcon(ICON_MDI_FOLDER_OPEN, "Open in Explorer"))
            {
                fs::show_in_graphical_env(absolute_path);
            }

            const bool can_reimport_file = asset_actions::can_reimport(absolute_path);
            if(ImGui::MenuItemIcon(ICON_MDI_REFRESH, "Reimport", nullptr, can_reimport_file))
            {
                asset_actions::reimport_path(absolute_path);
            }

            ImGui::Separator();

            if(ImGui::MenuItemIcon(ICON_MDI_PENCIL, "Rename", ImGui::GetKeyName(shortcuts::rename_item)))
            {
                open_rename_menu = true;
                ImGui::CloseCurrentPopup();
            }

            if(ImGui::MenuItemIcon(ICON_MDI_CONTENT_COPY,
                                   "Duplicate",
                                   ImGui::GetKeyCombinationName(shortcuts::duplicate_item).c_str()))
            {
                action = entry_action::duplicate;
                ImGui::CloseCurrentPopup();
            }

            if(ImGui::MenuItemIcon(ICON_MDI_DELETE, "Delete", ImGui::GetKeyName(shortcuts::delete_item)))
            {
                action = entry_action::deleted;
                ImGui::CloseCurrentPopup();
            }
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
            action = entry_action::canceled;
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

        if(!ImGui::IsItemVisible())
        {
            ImGui::SetScrollHereY();
        }

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

        case entry_action::canceled:
        {
            pending_rename.clear();
            if(item.on_cancel)
            {
                item.on_cancel();
            }
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
                           ImGui::KnobSliderScalarT("##scale", &scale_, 0.5f, 1.0f);
                           ImGui::SetItemTooltipEx("%s", "Icons scale");
                           ImGui::PopItemWidth();
                       });

    ImGui::Separator();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    fs::path current_path = cache_.get_path();

    if(ImGui::BeginChild("assets_content", ImGui::GetContentRegionAvail(), false, flags))
    {

        bool is_popup_opened = false;
        

        auto process_cache_entry = [&, this](const auto& cache_entry)
        {
            const auto& absolute_path = cache_entry.entry.path();
            const auto& name = cache_entry.stem;
            const auto& filename = cache_entry.filename;
            const auto& relative = cache_entry.protocol_path;
            const auto& file_ext = cache_entry.extension;

            content_browser_item item(cache_entry);
            item.size = size;
            
            // Use reusable rename handler
            setup_rename_handler(item, absolute_path, file_ext);

            bool known = false;
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
                    if(known)
                    {
                        return;
                    }

                    using asset_t = typename std::decay_t<decltype(tag)>::type;

                    if(ex::is_format<asset_t>(file_ext))
                    {
                        known = true;
                        setup_asset_item<asset_t>(ctx, item, absolute_path, relative, file_ext);
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

                item.on_click = [&em, entry, &item]()
                {
                    bool is_directory = item.entry.entry.is_directory();
                    const auto& file_ext = item.entry.extension;
                    const auto& file_type = ex::get_type(file_ext, is_directory);
                    const auto& name = item.entry.stem;
                    em.select(entry, em.get_select_mode(), name + " (" + file_type + ")");
                };

                // Use reusable template delete handler for unknown assets
                setup_delete_handler(item, relative, absolute_path, entry, ctx);

                // Use reusable rename handler
                setup_rename_handler(item, absolute_path, file_ext);

                if(fs::is_directory(cache_entry.entry.status()))
                {
                    item.on_double_click = [&current_path, &em, entry]()
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
                const auto& extension = cache_entry.extension;
                bool passed = false;

                if(filter_.PassFilter(name.c_str()))
                {
                    passed = true;
                    filtered_entries.emplace_back(cache_entry);
                }

                if(!passed)
                {
                    if(filter_.PassFilter(ex::get_type(extension, cache_entry.entry.is_directory()).c_str()))
                    {
                        passed = true;
                        filtered_entries.emplace_back(cache_entry);
                    }
                }
                
                if(!passed)
                {
                    const auto& metadata = am.get_metadata_for_path(cache_entry.entry.path()).meta;
                    if(filter_.PassFilter(metadata.uid.to_string().c_str()))
                    {
                        filtered_entries.emplace_back(cache_entry);
                    }
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


        if(ImGui::MenuItemIcon(ICON_MDI_IMPORT, "Import..."))
        {
            import(ctx, target_path);
        }
        ImGui::SetItemTooltipEx("If import asset consists of multiple files,\n"
                                "just copy paste all the files the data folder.\n"
                                "Preferably in a new folder. The importer will\n"
                                "automatically pick them up as dependencies.");
    }
    ImGui::EndPopup();
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

            if(create_script_from_template(new_script_template, available))
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
                if(fs::is_directory(path, err))
                {
                    fs::copy(path, dir, fs::copy_options::recursive, err);
                    if(err)
                    {
                        APPLOG_ERROR("Failed to import directory {}, error: {}", path.string(), err.message());
                    }
                }
                else 
                {
                    asset_writer::atomic_copy_file(path, dir, err);
                }
            },
            p,
            filename);
    }
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
void content_browser_panel::setup_delete_handler(content_browser_item& item, const std::string& relative, 
                                                const fs::path& absolute_path, const EntryType& entry, rtti::context& ctx)
{
    auto& em = ctx.get_cached<editing_manager>();
    
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

    item.on_cancel = [this, relative, absolute_path, &em, entry]()
    {
        fs::error_code err;
        fs::remove_all(absolute_path, err);
        em.unselect(entry);  // Works for both asset handles and fs::path
    };
}

void content_browser_panel::setup_rename_handler(content_browser_item& item, const fs::path& absolute_path,
                                                const std::string& file_ext)
{
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
void content_browser_panel::setup_asset_item(rtti::context& ctx, content_browser_item& item, 
                                            const fs::path& absolute_path, 
                                            const std::string& relative,
                                            const std::string& file_ext)
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    auto& tm = ctx.get_cached<thumbnail_manager>();
    
    using entry_t = asset_handle<AssetType>;
    const auto& entry = am.find_asset<AssetType>(relative);

    item.description = entry.uid().to_string();
    item.icon = tm.get_thumbnail(entry);
    item.is_selected = em.is_selected(entry);
    item.is_focused = em.is_focused(entry);
    item.is_loading = !entry.is_ready();
    
    // Simple click handler
    item.on_click = [&em, entry, &item]()
    {
        bool is_directory = item.entry.entry.is_directory();
        const auto& file_ext = item.entry.extension;
        const auto& file_type = ex::get_type(file_ext, is_directory);
        const auto& name = item.entry.stem;

        em.select(entry, em.get_select_mode(), name + " (" + file_type + ")");
    };

    // Use reusable template delete handler
    setup_delete_handler(item, relative, absolute_path, entry, ctx);

    // Use reusable rename handler
    setup_rename_handler(item, absolute_path, file_ext);

    // Set up double-click handlers based on asset type
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
        item.on_double_click = [absolute_path]()
        {
            editor_actions::open_workspace_on_file(absolute_path);
        };
    }
    // For other asset types, no double-click action for now
}

} // namespace unravel
