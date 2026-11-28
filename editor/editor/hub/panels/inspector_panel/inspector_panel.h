#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <reflection/reflection.h>
#include <editor/hub/panels/entity_panel.h>

namespace unravel
{
struct inspector_context;
struct inspector_filter;

class inspector_panel : public entity_panel
{
public:
    inspector_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void draw_ui(rtti::context& ctx) override;
    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    void draw_toolbar(const entt::meta_any& selected, inspector_context& inspector_ctx);
    /// Pins the inspector to the object it shows, or lets it follow the selection again.
    void draw_lock_toggle(const entt::meta_any& selected);
    /// Expand all and collapse all of the component headers, each enabled while some header is
    /// not in its state yet.
    void draw_fold_buttons(inspector_context& inspector_ctx);
    void draw_debug_toggle();
    /// The search field and the overridden toggle: what the inspected object shows of its rows.
    void draw_search_field();
    void draw_overridden_toggle();
    /// What the filter shows, for the inspection about to be drawn.
    auto make_filter() const -> inspector_filter;
    /// The locked object, else the selection: inspected when it is one object, summed up
    /// when it is several.
    void draw_inspected_object(rtti::context& ctx, entt::meta_any& selected, size_t selections_count);
    /// One object's rows. An asset the project does not own (asset_actions::is_project_asset) draws under a banner
    /// that says so, with its values read only.
    void inspect_object(rtti::context& ctx, entt::meta_any& object);
    /// Above an asset the engine or the editor ships: why its values cannot be changed, and Duplicate Into Project.
    void draw_shipped_asset_banner(rtti::context& ctx, const std::string& key);
    /// Shows the copy Duplicate Into Project made once the asset watcher has imported it, while the Inspector
    /// still shows the asset it was made of: it selects the copy, or, locked, locks on it and leaves the selection
    /// alone. An Inspector that moved on to something else keeps what it shows.
    void select_pending_copy(rtti::context& ctx);
    /// A selection of several entities: drawn as the one selected last, with edits reaching them all.
    void inspect_selected_entities(rtti::context& ctx, entt::meta_any& selected, size_t selections_count);
    /// A selection of several assets of one type with import settings: drawn as the one selected last, with Apply
    /// of its import settings reaching them all.
    void inspect_selected_assets(rtti::context& ctx, entt::meta_any& selected, size_t selections_count);
    /// Ends the inspection of the object shown last frame when `shown` (null for none) is another.
    void follow_shown_object(rtti::context& ctx, const entt::meta_any* shown);
    /// Tells the inspector of the object shown last that it left (see inspector::end_inspection).
    void end_inspection(rtti::context& ctx);

    /// A copy Duplicate Into Project made that is still being imported: its key, the key of the asset it copies,
    /// and when the wait for it ends (ImGui time).
    struct pending_copy
    {
        std::string key;
        std::string source;
        double deadline{};
    };
    pending_copy pending_copy_;

    entt::meta_any locked_object_;
    /// The object the panel showed last frame, to tell its inspector when it leaves.
    entt::meta_any shown_object_;
    bool debug_{};
    ImGuiTextFilter search_;
    bool only_overridden_{};
};
} // namespace unravel
