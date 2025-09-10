#include "imgui_messagebox.h"
#include "fonts/icons/icons_material_design_icons.h"
#include "imgui/imgui.h"
#include <algorithm>

namespace ImBox
{

// Static member initialization
int MsgBox::next_id_counter_ = 1;

MsgBox::MsgBox(const std::string& title, const std::string& message, int buttons, const MsgBoxConfig& config)
    : title_(title)
    , message_(message)
    , buttons_(buttons)
    , config_(config)
    , id_counter_(next_id_counter_++)
    , animation_state_(AnimationState::Closed)
    , animation_progress_(0.0f)
    , custom_icon_(nullptr)
{
    CalculateButtonLayout();
}

MsgBox::~MsgBox() = default;

auto MsgBox::OpenPopup(std::function<void(ModalResult)> callback) -> void
{
    callback_ = std::move(callback);
    animation_state_ = AnimationState::Opening;
    animation_start_ = std::chrono::steady_clock::now();
    animation_progress_ = 0.0f;
    open_requested_ = true;
}

auto MsgBox::Draw() -> bool
{
    UpdateAnimation();
    
    if (animation_state_ == AnimationState::Closed)
    {
        return false;
    }

    // Apply animation alpha
    float alpha = GetAnimationAlpha();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    
    // Set up window positioning and styling
    if (config_.center_on_screen)
    {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    }
    
    // Apply custom background color if specified
    if (config_.background_color.w > 0.0f)
    {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, config_.background_color);
    }
    
    // Enhanced window padding and rounding
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 8});
    
    // Window flags
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (!config_.allow_resize)
    {
        flags |= ImGuiWindowFlags_NoResize;
    }
    if (!config_.show_close_button)
    {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    bool is_open = true;
    // Create unique window title using the ID counter to avoid conflicts with same titles
    std::string window_title = config_.show_close_button ? 
        (title_ + "###MsgBox" + std::to_string(id_counter_)) : 
        ("###MsgBox" + std::to_string(id_counter_));
    
	if (open_requested_)
	{
		ImGui::OpenPopup(window_title.c_str());
		open_requested_ = false;
	}

    if (ImGui::BeginPopupModal(window_title.c_str(), &is_open, flags))
    {
        // Constrain window size
        ImVec2 window_size = ImGui::GetWindowSize();
        if (window_size.x < config_.min_size.x || window_size.y < config_.min_size.y ||
            window_size.x > config_.max_size.x || window_size.y > config_.max_size.y)
        {
            ImVec2 new_size = {
                std::max(config_.min_size.x, std::min(config_.max_size.x, window_size.x)),
                std::max(config_.min_size.y, std::min(config_.max_size.y, window_size.y))
            };
            ImGui::SetWindowSize(new_size);
        }

        // Draw content
        DrawIcon();
        ImGui::SameLine();
        DrawMessage();
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        DrawButtons();
        
        ImGui::EndPopup();
    }
    else if (!is_open && animation_state_ != AnimationState::Closing)
    {
        // User closed via X button or Escape
        animation_state_ = AnimationState::Closing;
        animation_start_ = std::chrono::steady_clock::now();
        if (callback_)
        {
            callback_(ModalResult::Cancel);
        }
    }
    
    // Clean up style variables
    ImGui::PopStyleVar(3); // WindowPadding, WindowRounding, ItemSpacing
    
    if (config_.background_color.w > 0.0f)
    {
        ImGui::PopStyleColor();
    }
    
    ImGui::PopStyleVar(); // Alpha
    
    return animation_state_ != AnimationState::Closed;
}

auto MsgBox::CalculateButtonLayout() -> void
{
    button_layout_.clear();
    
    // Define button order and labels
    std::vector<std::pair<ModalResult, std::string>> button_definitions = {
        {ModalResult::Save, "Save"},
        {ModalResult::DontSave, "Don't Save"},
        {ModalResult::Delete, "Delete"},
        {ModalResult::CancelDelete, "Cancel"},
        {ModalResult::Yes, "Yes"},
        {ModalResult::YesToAll, "Yes to All"},
        {ModalResult::Ok, "OK"},
        {ModalResult::Apply, "Apply"},
        {ModalResult::Retry, "Retry"},
        {ModalResult::Ignore, "Ignore"},
        {ModalResult::No, "No"},
        {ModalResult::NoToAll, "No to All"},
        {ModalResult::Abort, "Abort"},
        {ModalResult::Cancel, "Cancel"},
        {ModalResult::Close, "Close"},
        {ModalResult::Discard, "Discard"},
        {ModalResult::Help, "Help"},
        {ModalResult::Reset, "Reset"}
    };
    
    for (const auto& [result, label] : button_definitions)
    {
        if (buttons_ & result)
        {
            button_layout_.push_back({result, label});
        }
    }
}

auto MsgBox::DrawIcon() -> void
{
    const char* icon = custom_icon_ ? custom_icon_ : GetTypeIcon();
    if (!icon) return;
    
    ImVec4 icon_color = GetTypeColor();
    ImGui::PushStyleColor(ImGuiCol_Text, icon_color);
    
    // Large icon
    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 2.0f);
    ImGui::Text("%s", icon);
    ImGui::PopFont();
    
    ImGui::PopStyleColor();
}

auto MsgBox::DrawMessage() -> void
{
    ImGui::BeginGroup();
    
    // Title (if not shown in window title bar)
    if (!config_.show_close_button && !title_.empty())
    {
        ImGui::Text("%s", title_.c_str());
        ImGui::Spacing();
    }
    
    // Message content with proper wrapping
    float available_width = std::min(config_.max_size.x - 100.0f, 400.0f); // Reserve space for icon and padding
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + available_width);
    ImGui::TextWrapped("%s", message_.c_str());
    ImGui::PopTextWrapPos();
    
    ImGui::EndGroup();
}

auto MsgBox::DrawButtons() -> void
{
    if (button_layout_.empty()) return;
    
    const float button_width = 100.0f;
    const float button_height = 30.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    
    // Calculate total width needed
    float total_width = button_layout_.size() * button_width + (button_layout_.size() - 1) * spacing;
    float available_width = ImGui::GetContentRegionAvail().x;
    
    // Center the buttons
    if (total_width < available_width)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available_width - total_width) * 0.5f);
    }
    
    // Draw buttons
    for (size_t i = 0; i < button_layout_.size(); ++i)
    {
        const auto& [result, label] = button_layout_[i];
        
        if (i > 0) ImGui::SameLine();
        
        // Set focus to first button when appearing
        if (i == 0 && ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        
        // Apply button styling based on type
        bool is_primary = (result == ModalResult::Ok || result == ModalResult::Yes || result == ModalResult::Save);
        bool is_destructive = (result == ModalResult::No || result == ModalResult::Cancel || result == ModalResult::Delete ||
                             result == ModalResult::Abort || result == ModalResult::Discard);
                             
        
        if (is_primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.1f, 1.0f));
        }
        else if (is_destructive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        }
        
        bool clicked = ImGui::Button(label.c_str(), {button_width, button_height});
        
        if (is_primary || is_destructive)
        {
            ImGui::PopStyleColor(3);
        }
        
        // Handle button click
        if (clicked)
        {
            animation_state_ = AnimationState::Closing;
            animation_start_ = std::chrono::steady_clock::now();
            if (callback_)
            {
                callback_(result);
            }
        }
        
        // Handle keyboard shortcuts
        if ((result == ModalResult::Ok && buttons_ == ModalResult::Ok && ImGui::IsKeyPressed(ImGuiKey_Escape)) ||
            (result == ModalResult::Cancel && ImGui::IsKeyPressed(ImGuiKey_Escape)) ||
            (result == ModalResult::Cancel && ImGui::IsKeyPressed(ImGuiKey_C)) ||
            (result == ModalResult::CancelDelete && ImGui::IsKeyPressed(ImGuiKey_Escape)) ||
            (result == ModalResult::CancelDelete && ImGui::IsKeyPressed(ImGuiKey_C)) ||
            (result == ModalResult::Ok && ImGui::IsKeyPressed(ImGuiKey_Enter)) ||
            (result == ModalResult::Save && ImGui::IsKeyPressed(ImGuiKey_Enter)) ||
            (result == ModalResult::Delete && ImGui::IsKeyPressed(ImGuiKey_Enter)) ||
            (result == ModalResult::Yes && ImGui::IsKeyPressed(ImGuiKey_Y)) ||
            (result == ModalResult::No && ImGui::IsKeyPressed(ImGuiKey_N)))
        {
            animation_state_ = AnimationState::Closing;
            animation_start_ = std::chrono::steady_clock::now();
            if (callback_)
            {
                callback_(result);
            }
        }
    }
}

auto MsgBox::UpdateAnimation() -> void
{
    if (animation_state_ == AnimationState::Open || animation_state_ == AnimationState::Closed)
    {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - animation_start_).count();
    animation_progress_ = std::min(1.0f, elapsed / config_.animation_duration);
    
    if (animation_progress_ >= 1.0f)
    {
        if (animation_state_ == AnimationState::Opening)
        {
            animation_state_ = AnimationState::Open;
        }
        else if (animation_state_ == AnimationState::Closing)
        {
            animation_state_ = AnimationState::Closed;
            ImGui::CloseCurrentPopup();
        }
    }
}

auto MsgBox::GetTypeIcon() const -> const char*
{
    switch (config_.type)
    {
        case MessageType::Info:     return ICON_MDI_INFORMATION;
        case MessageType::Warning:  return ICON_MDI_ALERT;
        case MessageType::Error:    return ICON_MDI_ALERT_CIRCLE;
        case MessageType::Success:  return ICON_MDI_CHECK_CIRCLE;
        case MessageType::Question: return ICON_MDI_HELP_CIRCLE;
        case MessageType::Custom:   return nullptr;
        default:                    return ICON_MDI_INFORMATION;
    }
}

auto MsgBox::GetTypeColor() const -> ImVec4
{
    switch (config_.type)
    {
        case MessageType::Info:     return ImVec4(0.3f, 0.7f, 1.0f, 1.0f);  // Blue
        case MessageType::Warning:  return ImVec4(1.0f, 0.8f, 0.0f, 1.0f);  // Orange
        case MessageType::Error:    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
        case MessageType::Success:  return ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // Green
        case MessageType::Question: return ImVec4(0.7f, 0.5f, 1.0f, 1.0f);  // Purple
        case MessageType::Custom:   return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White
        default:                    return ImVec4(0.3f, 0.7f, 1.0f, 1.0f);  // Blue
    }
}

auto MsgBox::GetAnimationAlpha() const -> float
{
    if (animation_state_ == AnimationState::Open)
    {
        return 1.0f;
    }
    else if (animation_state_ == AnimationState::Closed)
    {
        return 0.0f;
    }
    else if (animation_state_ == AnimationState::Opening)
    {
        // Smooth ease-out animation
        float t = animation_progress_;
        return t * t * (3.0f - 2.0f * t); // Smoothstep
    }
    else if (animation_state_ == AnimationState::Closing)
    {
        // Smooth ease-in animation
        float t = 1.0f - animation_progress_;
        return t * t * (3.0f - 2.0f * t); // Smoothstep
    }
    
    return 1.0f;
}

// MsgBoxManager implementation

auto MsgBoxManager::GetInstance() -> MsgBoxManager&
{
    static MsgBoxManager instance;
    return instance;
}

auto MsgBoxManager::ShowMessageBox(
    const std::string& title, 
    const std::string& message, 
    int buttons,
    const MsgBoxConfig& config,
    std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    auto message_box = std::make_shared<MsgBox>(title, message, buttons, config);
    message_box->OpenPopup(callback);
    active_boxes_.push_back(message_box);
    return message_box;
}

auto MsgBoxManager::RenderAll() -> void
{
    CleanupClosedBoxes();
    
    // Render in reverse order so newer popups appear on top
    for (auto it = active_boxes_.rbegin(); it != active_boxes_.rend(); ++it)
    {
        (*it)->Draw();
		break;
    }
}

auto MsgBoxManager::CloseAll() -> void
{
    active_boxes_.clear();
}

auto MsgBoxManager::CleanupClosedBoxes() -> void
{
    active_boxes_.erase(
        std::remove_if(active_boxes_.begin(), active_boxes_.end(),
                      [](const std::shared_ptr<MsgBox>& box) {
                          return !box || !box->IsOpen();
                      }),
        active_boxes_.end());
}

// Convenience functions

auto ShowInfo(const std::string& title, const std::string& message, 
              std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Info;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, ModalResult::Ok, config, callback);
}

auto ShowWarning(const std::string& title, const std::string& message, 
                 std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Warning;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, ModalResult::Ok, config, callback);
}

auto ShowError(const std::string& title, const std::string& message, 
               std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Error;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, ModalResult::Ok, config, callback);
}

auto ShowSuccess(const std::string& title, const std::string& message, 
                 std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Success;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, ModalResult::Ok, config, callback);
}

auto ShowQuestion(const std::string& title, const std::string& message, 
                  std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Question;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, 
                                                        ModalResult::Yes | ModalResult::No, config, callback);
}

auto ShowConfirmation(const std::string& title, const std::string& message, 
                      std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Question;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, 
                                                        ModalResult::Ok | ModalResult::Cancel, config, callback);
}

auto ShowSaveConfirmation(const std::string& title, const std::string& message, 
                          std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Question;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, 
                                                        ModalResult::Save | ModalResult::DontSave | ModalResult::Cancel, 
                                                        config, callback);
}

auto ShowDeleteConfirmation(const std::string& title, const std::string& message, 
                            std::function<void(ModalResult)> callback) -> std::shared_ptr<MsgBox>
{
    MsgBoxConfig config;
    config.type = MessageType::Question;
    return MsgBoxManager::GetInstance().ShowMessageBox(title, message, 
                                                        ModalResult::Delete | ModalResult::CancelDelete, config, callback);
}

auto RenderMessageBoxes() -> void
{
    MsgBoxManager::GetInstance().RenderAll();
}



} // namespace ImBox