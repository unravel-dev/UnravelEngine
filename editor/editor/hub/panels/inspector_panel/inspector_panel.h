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
    inspector_panel(imgui_panels* parent);

    void init(rtti::context& ctx);
    void deinit(rtti::context& ctx);

    void on_frame_ui_render(rtti::context& ctx, const char* name);

private:
    rttr::variant locked_object_;
    bool debug_{};
};
} // namespace unravel
