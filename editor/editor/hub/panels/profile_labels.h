#pragma once

#include <deploy/profile.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>

#include <logging/logging.h>

#include <string>

/// How the editor's windows name settings profiles: one icon per platform, then the name.
namespace unravel::profile_labels
{

/// Every platform a profile can build for, in the order the editor offers them.
constexpr target_platform PLATFORMS[] = {target_platform::desktop, target_platform::android, target_platform::web};

inline auto get_platform_icon(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return ICON_MDI_ANDROID;
        case target_platform::web:
            return ICON_MDI_WEB;
        case target_platform::desktop:
        default:
            return ICON_MDI_MONITOR;
    }
}

inline auto get_platform_name(target_platform platform) -> const char*
{
    switch(platform)
    {
        case target_platform::android:
            return "Android";
        case target_platform::web:
            return "Web";
        case target_platform::desktop:
        default:
            return "Desktop";
    }
}

/// "<platform icon> <name>".
inline auto get_label(const settings_profile& profile) -> std::string
{
    return fmt::format("{} {}", get_platform_icon(profile.platform), profile.name);
}

} // namespace unravel::profile_labels
