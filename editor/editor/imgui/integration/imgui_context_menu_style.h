#pragma once

#include <imgui/imgui.h>

namespace ImGui
{

void PushContextMenuStyle();
void PopContextMenuStyle();

struct ContextMenuStyleScope
{
    ContextMenuStyleScope()
    {
        PushContextMenuStyle();
    }

    ~ContextMenuStyleScope()
    {
        PopContextMenuStyle();
    }

    ContextMenuStyleScope(const ContextMenuStyleScope&) = delete;
    ContextMenuStyleScope& operator=(const ContextMenuStyleScope&) = delete;
};

auto MenuItemIcon(const char* icon,
                  const char* label,
                  const char* shortcut = nullptr,
                  bool enabled = true) -> bool;
auto BeginMenuIcon(const char* icon, const char* label, bool enabled = true) -> bool;

} // namespace ImGui
