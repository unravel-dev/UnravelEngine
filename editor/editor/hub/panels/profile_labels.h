#pragma once

#include <deploy/profile.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>

#include <logging/logging.h>

#include <string>

/// How the editor's windows name settings profiles: one icon per platform, then the name.
namespace unravel::profile_labels
{

inline auto get_platform_icon(target_platform platform) -> const char*
{
    return platform == target_platform::android ? ICON_MDI_ANDROID : ICON_MDI_MONITOR;
}

inline auto get_platform_name(target_platform platform) -> const char*
{
    return platform == target_platform::android ? "Android" : "Desktop";
}

/// "<platform icon> <name>".
inline auto get_label(const settings_profile& profile) -> std::string
{
    return fmt::format("{} {}", get_platform_icon(profile.platform), profile.name);
}

} // namespace unravel::profile_labels
