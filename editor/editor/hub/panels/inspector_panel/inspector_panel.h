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

private:
    entt::meta_any locked_object_;
    bool debug_{};
};
} // namespace unravel
