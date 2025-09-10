#pragma once
#include <functional>
#include <imgui_includes.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace ImBox
{

/// @brief Modal result flags for message box buttons
enum ModalResult
{
    None = 0x0,
    Ok = 1 << 0,
    Cancel = 1 << 1,
    Yes = 1 << 2,
    No = 1 << 3,
    Abort = 1 << 4,
    Retry = 1 << 5,
    Ignore = 1 << 6,
    YesToAll = 1 << 7,
    NoToAll = 1 << 8,
    Apply = 1 << 9,
    Discard = 1 << 10,
    Help = 1 << 11,
    Reset = 1 << 12,
    Close = 1 << 13,
    Save = 1 << 14,
    Delete = 1 << 15,
    DontSave = 1 << 16,
    CancelDelete = 1 << 17
};

/// @brief Message box types that determine appearance and icon
enum class MessageType
{
    Info,
    Warning,
    Error,
    Success,
    Question,
    Custom
};

/// @brief Animation state for smooth transitions
enum class AnimationState
{
    Opening,
    Open,
    Closing,
    Closed
};

/// @brief Configuration for message box appearance and behavior
struct MsgBoxConfig
{
    MessageType type = MessageType::Info;
    ImVec2 min_size = {300, 150};
    ImVec2 max_size = {600, 400};
    float animation_duration = 0.1f;
    bool center_on_screen = true;
    bool allow_resize = false;
    bool show_close_button = true;
    ImVec4 background_color = {0.0f, 0.0f, 0.0f, 0.0f}; // Use default if alpha is 0
};

/// @brief Individual message box instance
class MsgBox
{
public:
    /// @brief Constructor with configuration
    MsgBox(const std::string& title, const std::string& message, int buttons, 
               const MsgBoxConfig& config = MsgBoxConfig{});
    
    /// @brief Destructor
    ~MsgBox();

    /// @brief Open the popup with callback
    auto OpenPopup(std::function<void(ModalResult)> callback) -> void;
    
    /// @brief Draw the message box (returns true if still open)
    auto Draw() -> bool;
    
    /// @brief Check if the message box is open
    auto IsOpen() const -> bool { return animation_state_ != AnimationState::Closed; }

    
    /// @brief Set custom icon (overrides type-based icon)
    auto SetCustomIcon(const char* icon) -> void { custom_icon_ = icon; }

private:
    auto CalculateButtonLayout() -> void;
    auto DrawIcon() -> void;
    auto DrawMessage() -> void;
    auto DrawButtons() -> void;
    auto UpdateAnimation() -> void;
    auto GetTypeIcon() const -> const char*;
    auto GetTypeColor() const -> ImVec4;
    auto GetAnimationAlpha() const -> float;
    
    std::string title_;
    std::string message_;
    int buttons_;
    MsgBoxConfig config_;
    
    std::function<void(ModalResult)> callback_;
    
    // Animation
    AnimationState animation_state_;
    std::chrono::steady_clock::time_point animation_start_;
    float animation_progress_;
    
    // Layout
    std::vector<std::pair<ModalResult, std::string>> button_layout_;
    ImVec2 content_size_;
    
    // Custom styling
    const char* custom_icon_;

	bool open_requested_{false};
    int id_counter_{}; // Store the unique ID counter for this instance
    
    static int next_id_counter_;
};

/// @brief Message box manager for handling multiple popups
class MsgBoxManager
{
public:
    /// @brief Get the singleton instance
    static auto GetInstance() -> MsgBoxManager&;
    
    /// @brief Create and show a message box
    auto ShowMessageBox(
        const std::string& title, 
        const std::string& message, 
        int buttons = ModalResult::Ok,
        const MsgBoxConfig& config = MsgBoxConfig{},
        std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;
    
    /// @brief Render all active message boxes
    auto RenderAll() -> void;
    
    /// @brief Close all message boxes
    auto CloseAll() -> void;
    
    /// @brief Get count of active message boxes
    auto GetActiveCount() const -> size_t { return active_boxes_.size(); }

private:
    MsgBoxManager() = default;
    auto CleanupClosedBoxes() -> void;
    
    std::vector<std::shared_ptr<MsgBox>> active_boxes_;
};

// Convenience functions for common message box types

/// @brief Show an information message box
auto ShowInfo(const std::string& title, const std::string& message, 
              std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a warning message box
auto ShowWarning(const std::string& title, const std::string& message, 
                 std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show an error message box
auto ShowError(const std::string& title, const std::string& message, 
               std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a success message box
auto ShowSuccess(const std::string& title, const std::string& message, 
                 std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a question message box with Yes/No buttons
auto ShowQuestion(const std::string& title, const std::string& message, 
                  std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a confirmation dialog with OK/Cancel buttons
auto ShowConfirmation(const std::string& title, const std::string& message, 
                      std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a save confirmation dialog with Save/Don't Save/Cancel buttons
auto ShowSaveConfirmation(const std::string& title, const std::string& message, 
                          std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Show a delete confirmation dialog with Delete/Cancel buttons
auto ShowDeleteConfirmation(const std::string& title, const std::string& message, 
                            std::function<void(ModalResult)> callback = nullptr) -> std::shared_ptr<MsgBox>;

/// @brief Render all message boxes (call this in your main render loop)
auto RenderMessageBoxes() -> void;

} // namespace ImBox