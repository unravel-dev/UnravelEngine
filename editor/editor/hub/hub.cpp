#include "hub.h"
#include "imgui_widgets/utils.h"
#include <editor/events.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/system/project_manager.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/rendering/renderer.h>
#include <hpp/optional.hpp>

#include <filedialog/filedialog.h>
#include <imgui/imgui.h>
#include <imgui_widgets/markdown.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>
#include <memory>

namespace unravel
{

namespace
{
struct project_item
{
    ImGui::Font::Enum font{ImGui::Font::Count};
    float scale = 1.0f;
    std::string tag{};
    std::string name{};
};

void draw_item(const std::vector<project_item>& v, std::function<void(ImVec2)> callback)
{
    ImGui::BeginGroup();

    auto pos = ImGui::GetCursorPos();

    float height = 0;
    for(const auto& item : v)
    {
        if(item.font != ImGui::Font::Count)
        {
            ImGui::PushFont(item.font);
        }
        if(item.scale > 0)
        {
            ImGui::PushWindowFontScale(item.scale);
        }
        height += ImGui::GetFrameHeightWithSpacing();

        if(item.scale > 0)
        {
            ImGui::PopWindowFontScale();
        }

        if(item.font != ImGui::Font::Count)
        {
            ImGui::PopFont();
        }
    }
    ImVec2 item_size(ImGui::GetContentRegionAvail().x, height);

    callback(item_size);
    bool hovered = ImGui::IsItemHovered();
    ImGui::SetCursorPos(pos);
    ImGui::Dummy({});
    ImGui::Indent();

    // ImGui::BeginGroup();
    // {
    //     ImGui::AlignTextToFramePadding();
    //     ImGui::NewLine();

    //     ImGui::AlignTextToFramePadding();
    //     ImGui::TextUnformatted(fmt::format("{} {}.", 0, ICON_MDI_FOLDER).c_str());

    //     ImGui::AlignTextToFramePadding();
    //     ImGui::NewLine();
    // }
    // ImGui::EndGroup();
    // ImGui::SameLine(0.0f, 1.0f);
    ImGui::BeginGroup();

    size_t i = 0;
    for(const auto& item : v)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", item.tag.c_str());

        ImGui::SameLine();

        if(item.font != ImGui::Font::Count)
        {
            ImGui::PushFont(item.font);
        }
        if(item.scale > 0)
        {
            ImGui::PushWindowFontScale(item.scale);
        }

        ImGui::AlignTextToFramePadding();
        if(i == 0 && hovered)
        {
            ImGui::TextLink(fmt::format("{}", item.name).c_str());
        }
        else
        {
            ImGui::Text("%s", item.name.c_str());
        }

        if(item.scale > 0)
        {
            ImGui::PopWindowFontScale();
        }

        if(item.font != ImGui::Font::Count)
        {
            ImGui::PopFont();
        }

        i++;
    }
    ImGui::EndGroup();
    ImGui::Unindent();

    ImGui::EndGroup();
}
} // namespace

auto hub::draw_project_card(const std::string& id, 
                           const std::string& name, 
                           const std::string& directory, 
                           const std::chrono::system_clock::time_point& last_modified,
                           bool is_selected,
                           bool enable_interaction,
                           float form_width) -> bool
{
    // Create project card with auto-resize height
    ImVec2 card_size(form_width, 0);
    
    // Card background and interaction styling
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

    bool is_hovered = false;
    
    // Different background color for selected projects
    if(is_selected)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.4f));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg, 0.6f));
    }
    
    if(ImGui::BeginChild(id.c_str(), card_size, ImGuiChildFlags_FrameStyle| ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        is_hovered = ImGui::IsWindowHovered();
        
        // Enhanced hover effect with gradient
        if(is_hovered)
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 p_min = ImGui::GetWindowPos();
            ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowSize().x, p_min.y + ImGui::GetWindowSize().y);
            draw_list->AddRectFilled(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.4f), 8.0f);
            
            // Add subtle border highlight
            if(is_selected)
            {
                draw_list->AddRect(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.8f), 8.0f, 0, 2.0f);
            }
            else
            {
                draw_list->AddRect(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.6f), 8.0f, 0, 2.0f);
            }
        }
        else if(is_selected)
        {
            // Add selection border even when not hovered
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 p_min = ImGui::GetWindowPos();
            ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowSize().x, p_min.y + ImGui::GetWindowSize().y);
            draw_list->AddRect(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.6f), 8.0f, 0, 1.5f);
        }
        
        // Project content layout with proper internal padding
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 8, ImGui::GetCursorPosY() + 4));
        ImGui::BeginGroup();
        {
            // Project name (large, bold) with enhanced styling
            ImGui::PushFont(ImGui::Font::Black);

            ImGui::PushWindowFontScale(1.2);
            if(is_hovered || is_selected)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_ButtonActive));
                ImGui::Text("%s", name.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::Text("%s", name.c_str());
            }
            ImGui::PopWindowFontScale();
            ImGui::PopFont();

            ImGui::Spacing();
            
            // Project location and date in improved horizontal layout
            ImGui::BeginGroup();
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f));
                
                // Location on the left
                ImGui::Text(ICON_MDI_FOLDER " %s", directory.c_str());
                
                // Date aligned to the right
                ImGui::SameLine();
                try
                {
                    auto date_text = fmt::format(ICON_MDI_CLOCK_OUTLINE " {:%m/%d/%Y}", last_modified);
                    float date_width = ImGui::CalcTextSize(date_text.c_str()).x;
                    float available_width = ImGui::GetContentRegionAvail().x;
                    
                    ImGui::AlignedItem(1.0f,
                                      available_width,
                                      date_width,
                                      [&]()
                                      {
                                          ImGui::Text("%s", date_text.c_str());
                                      });
                }
                FMT_CATCH(...)
                {
                    const char* unknown_text = ICON_MDI_CLOCK_OUTLINE " Unknown";
                    float date_width = ImGui::CalcTextSize(unknown_text).x;
                    float available_width = ImGui::GetContentRegionAvail().x;
                    
                    ImGui::AlignedItem(1.0f,
                                      available_width,
                                      date_width,
                                      [&]()
                                      {
                                          ImGui::Text("%s", unknown_text);
                                      });
                }
                
                ImGui::PopStyleColor();
            }
            ImGui::EndGroup();
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    
    return is_hovered && enable_interaction;
}

hub::hub(rtti::context& ctx)
{
    auto& ui_ev = ctx.get_cached<ui_events>();
    auto& ev = ctx.get_cached<events>();

    ev.on_frame_update.connect(sentinel_, this, &hub::on_frame_update);
    ev.on_frame_before_render.connect(sentinel_, this, &hub::on_frame_before_render);
    ev.on_frame_render.connect(sentinel_, this, &hub::on_frame_render);
    ev.on_play_begin.connect(sentinel_, -999, this, &hub::on_play_begin);
    ev.on_script_recompile.connect(sentinel_, 10000, this, &hub::on_script_recompile);
    ev.on_os_event.connect(sentinel_, 10000, this, &hub::on_os_event);

    ui_ev.on_frame_ui_render.connect(sentinel_, this, &hub::on_frame_ui_render);
}

auto hub::init(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    panels_.init(ctx);

    return true;
}

auto hub::deinit(rtti::context& ctx) -> bool
{
    APPLOG_TRACE("{}::{}", hpp::type_name_str(*this), __func__);

    panels_.deinit(ctx);

    return true;
}

void hub::open_project_settings(rtti::context& ctx, const std::string& hint)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }

    panels_.get_project_settings_panel().show(true, hint);
}

void hub::on_frame_update(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_update(ctx, dt);
}

void hub::on_frame_before_render(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_before_render(ctx, dt);
}

void hub::on_frame_render(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        return;
    }
    panels_.on_frame_render(ctx, dt);
}

void hub::on_frame_ui_render(rtti::context& ctx, delta_t dt)
{
    auto& pm = ctx.get_cached<project_manager>();

    if(!pm.has_open_project())
    {
        on_start_page_render(ctx);
    }
    else
    {
        on_opened_project_render(ctx);
    }
}

void hub::on_script_recompile(rtti::context& ctx, const std::string& protocol, uint64_t version)
{
    panels_.get_console_log_panel().on_recompile();
}

void hub::on_play_begin(rtti::context& ctx)
{
    panels_.get_console_log_panel().on_play();
}

void hub::on_os_event(rtti::context& ctx, os::event& e)
{
    auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        return;
    }

    if(e.type == os::events::drop_position)
    {
        panels_.set_external_drop_position(ImVec2{e.drop.x, e.drop.y});
    }
    else if(e.type == os::events::drop_begin)
    {
        panels_.set_external_drop_in_progress(true);
    }
    else if(e.type == os::events::drop_file)
    {
        panels_.add_external_drop_file(e.drop.data);
    }
    else if(e.type == os::events::drop_complete)
    {
        panels_.set_external_drop_in_progress(false);
    }
    else if(e.type == os::events::window)
    {
        if(e.window.type == os::window_event_id::close)
        {
            auto window_id = e.window.window_id;

            auto& rend = ctx.get_cached<renderer>();
            auto& render_window = rend.get_main_window();
            if(render_window)
            {
                if(render_window->get_window().get_id() == window_id)
                {
                    if(!editor_actions::prompt_save_scene(ctx))
                    {
                        e = {};
                    }
                }
            }
        }
    }
}

void hub::on_opened_project_render(rtti::context& ctx)
{
    panels_.on_frame_ui_render(ctx);
}

void hub::on_start_page_render(rtti::context& ctx)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    window_flags |=
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("START PAGE", nullptr, window_flags);
    ImGui::PopStyleVar(2);

    ImGui::OpenPopup("PROJECTS");
    
    // Calculate popup size with proper margins - reduced size
    ImVec2 viewport_size = ImGui::GetMainViewport()->Size;
    ImVec2 popup_size = ImVec2(viewport_size.x * 0.5f, viewport_size.y * 0.5f);
    ImGui::SetNextWindowSize(popup_size, ImGuiCond_Appearing);

    // Add moderate padding to the popup window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 10.0f));
    
    if(ImGui::BeginPopupModal("PROJECTS", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
    {
        switch(current_view_)
        {
            case view_state::projects_list:
                render_projects_list_view(ctx);
                break;
            case view_state::new_project_creator:
                render_new_project_creator_view(ctx);
                break;
            case view_state::project_remover:
                render_project_remover_view(ctx);
                break;
        }

        ImGui::EndPopup();
    }
    
    ImGui::PopStyleVar(2);

    ImGui::End();
}

void hub::render_projects_list_view(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();

    auto on_open_project = [&](const std::string& p)
    {
        auto path = fs::path(p).make_preferred();
        pm.open_project(ctx, path);
    };

    // Header section with improved styling
    ImGui::BeginGroup();
    {
   
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.8f));
        ImGui::Text("Open an existing project or create a new one");
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Subtle separator with custom styling
    ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.3f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    
    ImGui::Spacing();
    ImGui::Spacing();

    // Main content area with improved sidebar layout
    float sidebar_width = 200.0f;
    float content_spacing = 20.0f;
    float content_width = ImGui::GetContentRegionAvail().x - sidebar_width - content_spacing;

    // Projects list area with enhanced styling
    ImGui::BeginGroup();
    {
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::Text("Projects");
        ImGui::PopFont();
        
        ImGui::Spacing();

        // Projects container with modern card layout
        ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoSavedSettings;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_WindowBg, 0.8f));
        
        if(ImGui::BeginChild("projects_content", ImVec2(content_width, ImGui::GetContentRegionAvail().y - 24), ImGuiChildFlags_Borders, flags))
        {
            const auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
            
            if(recent_projects.empty())
            {
                // Enhanced empty state
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 60);
                ImGui::BeginGroup();
                {
                    float center_x = (ImGui::GetContentRegionAvail().x - 200) * 0.5f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x);
                    
                    ImGui::PushFont(ImGui::Font::Bold);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::Text("No recent projects found");
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                    
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_x - 30);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f));
                    ImGui::Text("Create a new project or browse for an existing one");
                    ImGui::PopStyleColor();
                }
                ImGui::EndGroup();
            }
            else
            {
                for(size_t i = 0; i < recent_projects.size(); ++i)
                {
                    const auto& prj = recent_projects[i];
                    auto p = fs::path(prj);
                    
                    // Get project metadata
                    fs::error_code ec;
                    auto ftime = fs::last_write_time(p / "settings" / "settings.cfg", ec);
                    auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());

                    auto name = p.stem().string();
                    auto dir = p.parent_path().string();
                    
                    // Check if this project is selected
                    bool is_selected = (selected_project_ == prj.string());
                    
                    // Draw project card using our shared function
                    bool is_hovered = draw_project_card(
                        fmt::format("project_card_{}", i),
                        name,
                        dir,
                        system_time,
                        is_selected,
                        true,
                        content_width
                    );
                    
                    // Handle interactions
                    if(is_hovered)
                    {
                        // Single click - select project
                        if(ImGui::IsMouseClicked(0))
                        {
                            selected_project_ = prj.string();
                        }
                        
                        // Double click - open project
                        if(ImGui::IsMouseDoubleClicked(0))
                        {
                            on_open_project(prj.string());
                        }
                        
                        // Right click - open context menu
                        if(ImGui::IsMouseClicked(1))
                        {
                            selected_project_ = prj.string();
                            ImGui::OpenPopup(fmt::format("project_context_menu_{}", i).c_str());
                        }
                    }
                    
                    // Context menu
                    if(ImGui::BeginPopup(fmt::format("project_context_menu_{}", i).c_str()))
                    {
                        ImGui::PushFont(ImGui::Font::Bold);
                        ImGui::Text("%s", name.c_str());
                        ImGui::PopFont();
                        ImGui::Separator();
                        
                        if(ImGui::MenuItem("Open Project"))
                        {
                            on_open_project(prj.string());
                        }
                        
                        ImGui::Separator();
                        
                        if(ImGui::MenuItem("Remove from Recents"))
                        {
                            auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
                            auto it = std::find(recent_projects.begin(), recent_projects.end(), fs::path(prj.string()));
                            if(it != recent_projects.end())
                            {
                                recent_projects.erase(it);
                                pm.save_editor_settings();
                                if(selected_project_ == prj.string())
                                {
                                    selected_project_.clear();
                                }
                            }
                        }
                        
                        if(ImGui::MenuItem("Delete Project Folder"))
                        {
                            // TODO: Implement folder deletion with confirmation dialog
                            auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
                            auto it = std::find(recent_projects.begin(), recent_projects.end(), fs::path(prj.string()));
                            if(it != recent_projects.end())
                            {
                                recent_projects.erase(it);
                                pm.save_editor_settings();
                                if(selected_project_ == prj.string())
                                {
                                    selected_project_.clear();
                                }
                            }
                        }
                        
                        ImGui::EndPopup();
                    }
                }
            }
        }
        ImGui::EndChild();
        
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, content_spacing);

    // Enhanced actions sidebar
    ImGui::BeginGroup();
    {
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::Text("Actions");
        ImGui::PopFont();
        
        ImGui::Spacing();
        
        // New Project button with enhanced styling
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.9f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.2f));
        ImGui::PushFont(ImGui::Font::Bold);
        
        if(ImGui::Button("New Project", ImVec2(sidebar_width, 45)))
        {
            current_view_ = view_state::new_project_creator;
            project_name_ = "";
            project_directory_ = "";
        }
        
        ImGui::PopFont();
        ImGui::PopStyleColor(3);
        
        ImGui::Spacing();
        
        // Open Selected button - only enabled when a project is selected
        bool has_selection = !selected_project_.empty();
        if(!has_selection)
        {
            ImGui::BeginDisabled();
        }
        
        if(ImGui::Button("Open Selected", ImVec2(sidebar_width, 35)))
        {
            if(has_selection)
            {
                on_open_project(selected_project_);
            }
        }
        
        if(!has_selection)
        {
            ImGui::EndDisabled();
        }
        
        if(has_selection)
        {
            auto project_name = fs::path(selected_project_).stem().string();
            ImGui::SetItemTooltipEx("Open: %s", project_name.c_str());
        }
        
        ImGui::Spacing();
        
        // Browse for Project button (external folder picker)
        if(ImGui::Button("Browse for Project", ImVec2(sidebar_width, 35)))
        {
            std::string path;
            if(native::pick_folder_dialog(path))
            {
                on_open_project(path);
            }
        }
        
        ImGui::Spacing();
        
        // Remove Selected button - only enabled when a project is selected
        if(!has_selection)
        {
            ImGui::BeginDisabled();
        }
        
        if(ImGui::Button("Remove Selected", ImVec2(sidebar_width, 35)))
        {
            if(has_selection)
            {
                project_to_remove_ = selected_project_;
                current_view_ = view_state::project_remover;
            }
        }
        
        if(!has_selection)
        {
            ImGui::EndDisabled();
        }
        
        if(has_selection)
        {
            auto project_name = fs::path(selected_project_).stem().string();
            ImGui::SetItemTooltipEx("Remove: %s", project_name.c_str());
        }
        
        ImGui::PopStyleVar();
        
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Additional info section with better styling
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.8f));
        ImGui::PushFont(ImGui::Font::Medium);
        ImGui::TextWrapped("Click to select, double-click to open, or right-click for options.");
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
}

void hub::render_new_project_creator_view(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();

    auto on_create_project = [&](const std::string& p)
    {
        auto path = fs::path(p).make_preferred();
        pm.create_project(ctx, path);
    };

    // Header section with title and back button
    ImGui::BeginGroup();
    {
        ImGui::PushFont(ImGui::Font::Black);
        ImGui::Text("Create New Project");
        ImGui::PopFont();
        
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
        if(ImGui::Button("Back", ImVec2(80, 0)))
        {
            current_view_ = view_state::projects_list;
            project_name_.clear();
            project_directory_.clear();
        }
    }
    ImGui::EndGroup();
    
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Center the form content
    float form_width = 600.0f;
    float center_offset = (ImGui::GetContentRegionAvail().x - form_width) * 0.5f;
    if(center_offset > 0)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_offset);
    }

    ImGui::BeginGroup();
    {
        // Project Name Section
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::Text("Project Name");
        ImGui::PopFont();
        ImGui::Spacing();
        
        ImGui::SetNextItemWidth(form_width);
        ImGui::InputTextWidget("##project_name", project_name_, false);
        
        if(project_name_.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::Text("Enter a name for your project");
            ImGui::PopStyleColor();
        }
        
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();
        
        // Directory Section
        ImGui::PushFont(ImGui::Font::Bold);
        ImGui::Text("Project Location");
        ImGui::PopFont();
        ImGui::Spacing();
        
        ImGui::BeginGroup();
        {
            float button_width = 40.0f;
            float input_width = form_width - button_width - ImGui::GetStyle().ItemSpacing.x;
            
            ImGui::SetNextItemWidth(input_width);
            ImGui::InputTextWidget("##project_directory", project_directory_, false);
            
            ImGui::SameLine();
            if(ImGui::Button(ICON_MDI_FOLDER_OPEN "##dir_picker", ImVec2(button_width, 0)))
            {
                std::string picked_dir;
                if(native::pick_folder_dialog(picked_dir))
                {
                    project_directory_ = picked_dir;
                }
            }
            ImGui::SetItemTooltipEx("Browse for folder...");
        }
        ImGui::EndGroup();
    }
    ImGui::EndGroup();
     
    if(project_directory_.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        float text_width = ImGui::CalcTextSize("Choose where to create your project").x;
        float available_width = ImGui::GetContentRegionAvail().x;
        ImGui::AlignedItem(0.5f, available_width, text_width, [&]() {
            ImGui::Text("Choose where to create your project");
        });
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        std::string text = fmt::format("Project will be created at: {}", (fs::path(project_directory_) / project_name_).string());
        float text_width = ImGui::CalcTextSize(text.c_str()).x;
        float available_width = ImGui::GetContentRegionAvail().x;
        ImGui::AlignedItem(0.5f, available_width, text_width, [&]() {
            ImGui::Text("%s", text.c_str());
        });
        ImGui::PopStyleColor();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Buttons Section
    bool can_create = !project_name_.empty() && !project_directory_.empty();
    
    // Center the buttons
    float button_width = 120.0f;
    float buttons_total_width = button_width * 2 + ImGui::GetStyle().ItemSpacing.x;
    float button_center_offset = (form_width - buttons_total_width) * 0.5f;
    
    //ImGui::SetCursorPosX(ImGui::GetCursorPosX() + button_center_offset);
    
    ImGui::AlignedItem(0.5f, 
        ImGui::GetContentRegionAvail().x, 
        buttons_total_width, 
    [&]() 
    {
        ImGui::BeginGroup();

        // Create button with different styling based on state
        if(can_create)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.2f));
        }
        else
        {
            ImGui::BeginDisabled();
        }
        
        if(ImGui::Button("Create Project", ImVec2(button_width, 35)))
        {
            if(can_create)
            {
                auto project_path = fs::path(project_directory_) / project_name_;
                on_create_project(project_path.string());
                ImGui::CloseCurrentPopup();
                current_view_ = view_state::projects_list;
                project_name_.clear();
                project_directory_.clear();
            }
        }
        
        if(can_create)
        {
            ImGui::PopStyleColor(2);
        }
        else
        {
            ImGui::EndDisabled();
        }
        
        ImGui::SameLine();
        
        if(ImGui::Button("Cancel", ImVec2(button_width, 35)))
        {
            current_view_ = view_state::projects_list;
            project_name_.clear();
            project_directory_.clear();
        }

        ImGui::EndGroup();
    });
    
    
}

void hub::render_project_remover_view(rtti::context& ctx)
{
    auto& pm = ctx.get_cached<project_manager>();

    // Header section with title and back button
    ImGui::BeginGroup();
    {
        ImGui::PushFont(ImGui::Font::Black);
        ImGui::Text("Remove Project");
        ImGui::PopFont();
        
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
        if(ImGui::Button("Back", ImVec2(80, 0)))
        {
            current_view_ = view_state::projects_list;
            project_to_remove_.clear();
        }
    }
    ImGui::EndGroup();
    
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Center the form content with increased width
    float form_width = ImGui::GetContentRegionAvail().x * 0.8f;  // Increased from 500.0f
    float center_offset = (ImGui::GetContentRegionAvail().x - form_width) * 0.5f;
    if(center_offset > 0)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + center_offset);
    }

    ImGui::BeginGroup();
    {
        // Project selection section if no project is pre-selected
        if(project_to_remove_.empty())
        {
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::Text("Select Project to Remove");
            ImGui::PopFont();
            ImGui::Spacing();
            
            // Project selection dropdown
            const auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
            if(!recent_projects.empty())
            {
                static int selected_project = 0;
                
                // Create project names for combo
                std::vector<std::string> project_names;
                for(const auto& prj : recent_projects)
                {
                    auto p = fs::path(prj);
                    project_names.push_back(p.stem().string());
                }
                
                ImGui::SetNextItemWidth(form_width);
                if(ImGui::BeginCombo("##project_select", project_names[selected_project].c_str()))
                {
                    for(int i = 0; i < project_names.size(); ++i)
                    {
                        bool is_selected = (selected_project == i);
                        if(ImGui::Selectable(project_names[i].c_str(), is_selected))
                        {
                            selected_project = i;
                            project_to_remove_ = recent_projects[i].string();
                        }
                        if(is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                
                if(selected_project >= 0 && selected_project < recent_projects.size())
                {
                    project_to_remove_ = recent_projects[selected_project].string();
                }
            }
            
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();
        }
        
        // Project info section using the same card design as recent projects
        if(!project_to_remove_.empty())
        {
            auto project_path = fs::path(project_to_remove_);
            auto project_name = project_path.stem().string();
            auto project_dir = project_path.parent_path().string();
            
            // Get project metadata like in the recent view
            fs::error_code ec;
            auto ftime = fs::last_write_time(project_path / "settings" / "settings.cfg", ec);
            auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::Text("Project Information");
            ImGui::PopFont();
            ImGui::Spacing();
            
            // Use the shared project card function with interaction disabled
            draw_project_card(
                "project_card_removal",
                project_name,
                project_dir,
                system_time,
                false,  // Not selectable in this view
                false,   // Disable interaction
                form_width
            );
            
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();
            
            // Warning section with better styling
            ImGui::PushFont(ImGui::Font::Bold);
            ImGui::Text("Choose Removal Action");
            ImGui::PopFont();
            ImGui::Spacing();
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.9f));
            ImGui::TextWrapped("Select how you want to remove this project from your workspace:");
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Spacing();
            
            // Action buttons section with improved layout and wider buttons
            float button_spacing = 15.0f;
            float total_spacing = button_spacing * 2;
            float button_width = (form_width - total_spacing) / 3.0f;  // Increased because form_width is larger
            float button_height = 55.0f;  // Increased height for better text visibility
            
            // Remove from recents button (safe action) - primary
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_ButtonActive, 1.2f));
            ImGui::PushFont(ImGui::Font::Bold);
            
            if(ImGui::Button("Remove from Recents", ImVec2(button_width, button_height)))
            {
                auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
                auto it = std::find(recent_projects.begin(), recent_projects.end(), fs::path(project_to_remove_));
                if(it != recent_projects.end())
                {
                    recent_projects.erase(it);
                    pm.save_editor_settings();
                }
                current_view_ = view_state::projects_list;
                project_to_remove_.clear();
            }
            
            ImGui::PopFont();
            ImGui::PopStyleColor(3);
            
            ImGui::SetItemTooltipEx("Remove from recent projects list\n(Project folder remains untouched)");
            
            
            ImGui::SameLine(0, button_spacing);
            
            // Delete folder button (dangerous action) - red color scheme
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 60, 60, 255));        // Red base color
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(220, 80, 80, 255)); // Brighter red on hover
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 100, 100, 255)); // Even brighter red when pressed
            ImGui::PushFont(ImGui::Font::Bold);
            
            if(ImGui::Button("Delete Folder", ImVec2(button_width, button_height)))
            {

                auto& recent_projects = pm.get_editor_settings().projects.recent_projects;
                auto it = std::find(recent_projects.begin(), recent_projects.end(), fs::path(project_to_remove_));
                if(it != recent_projects.end())
                {
                    recent_projects.erase(it);
                    pm.save_editor_settings();
                }

                fs::remove_all(project_to_remove_);

                current_view_ = view_state::projects_list;
                project_to_remove_.clear();
            }
            
            ImGui::PopFont();
            ImGui::PopStyleColor(3);
            
            ImGui::SetItemTooltipEx("DANGER: Permanently delete the entire\nproject folder and all its contents");
            
            
            ImGui::SameLine(0, button_spacing);
            
            // Cancel button
            if(ImGui::Button("Cancel", ImVec2(button_width, button_height)))
            {
                current_view_ = view_state::projects_list;
                project_to_remove_.clear();
            }
            
            ImGui::PopStyleVar();

            ImGui::SetItemTooltipEx("Return to projects list without making changes");
            
            
            ImGui::Spacing();
            ImGui::Spacing();
            
            // Additional safety information
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f));
            ImGui::PushFont(ImGui::Font::Medium);
            ImGui::TextWrapped("Tip: 'Remove from Recents' is the safer option if you want to keep the project files.");
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndGroup();
}

} // namespace unravel
