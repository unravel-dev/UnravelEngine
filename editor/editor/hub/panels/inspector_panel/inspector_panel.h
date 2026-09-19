#pragma once

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <reflection/reflection.h>
#include <editor/hub/panels/entity_panel.h>

namespace unravel
{
class inspector_panel : public entity_panel
{
public:
    inspector_panel(imgui_panels* parent, const char* name);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void draw_ui(rtti::context& ctx) override;
    auto get_window_flags() const -> ImGuiWindowFlags override;

private:
    void draw_toolbar(const entt::meta_any& selected);
    /// Pins the inspector to the object it shows, or lets it follow the selection again.
    void draw_lock_toggle(const entt::meta_any& selected);
    void draw_debug_toggle();
    /// The locked object, else the selection: inspected when it is one object, summed up
    /// when it is several.
    void draw_inspected_object(rtti::context& ctx, entt::meta_any& selected, size_t selections_count);

    entt::meta_any locked_object_;
    bool debug_{};
};
} // namespace unravel
