#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <reflection/reflection.h>
#include <editor/hub/panels/entity_panel.h>

namespace unravel
{
struct inspector_context;

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
    /// The locked object, else the selection: inspected when it is one object, summed up
    /// when it is several.
    void draw_inspected_object(rtti::context& ctx, entt::meta_any& selected, size_t selections_count);
    /// Ends the inspection of the object shown last frame when `shown` (null for none) is another.
    void follow_shown_object(rtti::context& ctx, const entt::meta_any* shown);
    /// Tells the inspector of the object shown last that it left (see inspector::end_inspection).
    void end_inspection(rtti::context& ctx);

    entt::meta_any locked_object_;
    /// The object the panel showed last frame, to tell its inspector when it leaves.
    entt::meta_any shown_object_;
    bool debug_{};
};
} // namespace unravel
