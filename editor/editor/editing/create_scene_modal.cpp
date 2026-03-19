#include "create_scene_modal.h"
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <editor/imgui/integration/imgui.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace unravel
{

namespace
{

struct preset_card_info
{
    const char* id;
    const char* icon;
    const char* title;
    const char* description;
    defaults::scene_preset preset;
};

const preset_card_info presets[] = {
    {"preset_low",
     ICON_MDI_LIGHTNING_BOLT_OUTLINE,
     "Low End",
     "Basic lighting and minimal post-processing.\n"
     "Best for low-end hardware or mobile targets.",
     defaults::scene_preset::low},
    {"preset_medium",
     ICON_MDI_LAYERS,
     "Standard",
     "Balanced lighting, soft shadows and post-processing.\n"
     "Recommended for most projects.",
     defaults::scene_preset::medium},
    {"preset_high",
     ICON_MDI_DIAMOND_STONE,
     "High End",
     "Full lighting, SSR, Bloom, volumetric clouds, soft shadows and post-processing.\n"
     "Best for high-end desktop builds.",
     defaults::scene_preset::high},
    {"preset_showcase",
     ICON_MDI_GIFT,
     "Showcase",
     "Full lighting, SSIL, SSR, Bloom, volumetric clouds, soft shadows and post-processing.\n"
     "Best for showcase builds. Requires a powerful GPU.",
     defaults::scene_preset::showcase},
};

enum class card_action
{
    none,
    selected,
    confirmed
};

auto draw_preset_card(const preset_card_info& info, float card_width, bool is_selected) -> card_action
{
    auto result = card_action::none;
    bool is_hovered = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

    if(is_selected)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.25f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f));
    }

    ImVec2 card_size(card_width, 0);
    if(ImGui::BeginChild(info.id, card_size,
                         ImGuiChildFlags_FrameStyle | ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        is_hovered = ImGui::IsWindowHovered();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 p_min = ImGui::GetWindowPos();
        ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowSize().x, p_min.y + ImGui::GetWindowSize().y);

        if(is_hovered)
        {
            draw_list->AddRectFilled(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.4f), 8.0f);
            draw_list->AddRect(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.6f), 8.0f, 0, 2.0f);
        }
        else if(is_selected)
        {
            draw_list->AddRect(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.6f), 8.0f, 0, 1.5f);
        }

        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 4, ImGui::GetCursorPosY() + 2));
        ImGui::BeginGroup();
        {
            ImGui::PushWindowFontScale(1.3f);
            if(is_hovered || is_selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            }
            // Icon in regular font (Black font has no icons)
            ImGui::Text("%s", info.icon);
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            // Title in Black font
            ImGui::PushFont(ImGui::Font::Black);
            ImGui::Text("%s", info.title);
            ImGui::PopFont();
            if(is_hovered || is_selected)
            {
                ImGui::PopStyleColor();
            }
            ImGui::PopWindowFontScale();

            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f));
            ImGui::TextWrapped("%s", info.description);
            ImGui::PopStyleColor();
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    if(is_hovered && ImGui::IsMouseDoubleClicked(0))
    {
        result = card_action::confirmed;
    }
    else if(is_hovered && ImGui::IsMouseClicked(0))
    {
        result = card_action::selected;
    }

    return result;
}

} // namespace

void create_scene_modal::show(std::function<void(defaults::scene_preset)> on_preset_selected)
{
    auto& inst = get_instance();
    inst.callback_ = std::move(on_preset_selected);
    inst.show_requested_ = true;
    inst.open_requested_ = true;
    inst.selected_preset_ = defaults::scene_preset::medium;
}

void create_scene_modal::render()
{
    auto& inst = get_instance();
    if(!inst.show_requested_)
    {
        return;
    }

    // if(inst.open_requested_)
    {
        ImGui::OpenPopup("Create Scene");
        // inst.open_requested_ = false;
    }

    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(),
    ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));

    const float modal_width = viewport->Size.x * 0.3f;
    ImGui::SetNextWindowSize(ImVec2(modal_width, 0), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12, 10));

    if(!ImGui::BeginPopupModal("Create Scene", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::PopStyleVar(3);
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.8f));
    ImGui::TextWrapped("Choose a preset for your new scene.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Cards list
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10.0f));
    float card_width = ImGui::GetContentRegionAvail().x;
    bool should_confirm = false;

    for(const auto& p : presets)
    {
        bool is_selected = (p.preset == inst.selected_preset_);
        auto action = draw_preset_card(p, card_width, is_selected);
        if(action == card_action::confirmed)
        {
            inst.selected_preset_ = p.preset;
            should_confirm = true;
        }
        else if(action == card_action::selected)
        {
            inst.selected_preset_ = p.preset;
        }
    }
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Bottom button row, right-aligned
    float button_width = 100.0f;
    float total_buttons_w = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    ImGui::AlignedItem(1.0f, ImGui::GetContentRegionAvail().x, total_buttons_w, [&]() -> void
    {
        if(ImGui::Button("Create", ImVec2(button_width, 30)) || should_confirm)
        {
            if(inst.callback_)
            {
                inst.callback_(inst.selected_preset_);
            }
            ImGui::CloseCurrentPopup();
            inst.show_requested_ = false;
        }

        ImGui::SameLine();

        if(ImGui::Button("Cancel", ImVec2(button_width, 30)))
        {
            ImGui::CloseCurrentPopup();
            inst.show_requested_ = false;
        }
    });

    ImGui::PopStyleVar(); // FrameRounding

    ImGui::EndPopup();
    ImGui::PopStyleVar(3); // WindowPadding, WindowRounding, ItemSpacing
}

auto create_scene_modal::get_instance() -> create_scene_modal&
{
    static create_scene_modal instance;
    return instance;
}

} // namespace unravel
