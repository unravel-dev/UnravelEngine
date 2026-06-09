#pragma once
#include "../panel_base.h"
#include <editor/imgui/integration/imgui.h>
#include <filesystem/cache.hpp>

#include <base/basetypes.hpp>
#include <context/context.hpp>

namespace unravel
{
class imgui_panels;

struct content_browser_item
{
    using on_action_t = std::function<void()>;
    using on_rename_t = std::function<void(const std::string&)>;

    content_browser_item(const fs::directory_cache::cache_entry& e)
        : entry(e)
    {

    }

    const fs::directory_cache::cache_entry& entry;
    on_action_t on_click;
    on_action_t on_double_click;
    on_action_t on_delete;
    on_action_t on_cancel;
    on_rename_t on_rename;

    gfx::texture::ptr icon;
    std::string description;
    bool is_loading{};
    bool is_selected{};
    bool is_focused{};
    float size{};
};


class content_browser_panel : public panel_base
{
public:
    content_browser_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_project_closed(rtti::context& ctx);

    void draw_ui(rtti::context& ctx) override;

    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    void draw(rtti::context& ctx);
    void draw_details(rtti::context& ctx, const fs::path& root_path);

    void draw_as_explorer(rtti::context& ctx, const fs::path& root_path);
    void context_menu(rtti::context& ctx, bool use_context_item, const fs::path& target_path);
    void context_create_menu(rtti::context& ctx, const fs::path& target_path);
    void set_cache_path(const fs::path& path);
    void handle_external_drop(rtti::context& ctx);
    void import(rtti::context& ctx, const fs::path& target_path);
    void on_import(rtti::context& ctx, const std::vector<std::string>& paths, const fs::path& target_path);
    void handle_window_empty_click(rtti::context& ctx) const;
    void draw_external_drop_overlay() const;
    
    // Helper method to avoid MSVC ICE with complex template lambdas
    template<typename AssetType>
    void setup_asset_item(rtti::context& ctx, content_browser_item& item, 
                         const fs::path& absolute_path,
                         const std::string& relative,
                         const std::string& file_ext);
    
    // Reusable member functions for consistent behavior
    template<typename EntryType>
    void setup_delete_handler(content_browser_item& item, const std::string& relative, 
                             const fs::path& absolute_path, const EntryType& entry, rtti::context& ctx);
    void setup_rename_handler(content_browser_item& item, const fs::path& absolute_path, 
                             const std::string& file_ext);
    void prompt_delete_asset(const std::string& name, const std::function<void()>& on_delete);

    fs::directory_cache cache_;

    std::shared_ptr<int> sentinel_ = std::make_shared<int>(0);

    ImGuiTextFilter filter_;
    fs::path root_;
    int refresh_{};
    float scale_ = 0.65f;

    int focus_frames_{};

    imgui_panels* parent_{};

};
} // namespace unravel
