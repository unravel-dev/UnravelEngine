#include "sample_catalog.h"

#include <editor/hub/engine_links.h>
#include <editor/imgui/integration/fonts/icons/icons_material_design_icons.h>

#include <algorithm>
#include <cctype>

namespace unravel::sample_catalog
{
namespace
{
constexpr const char* SAMPLE_CATALOG_DEFAULT_BRANCH = "main";

auto is_same_name(const std::string& lhs, const std::string& rhs) -> bool
{
    return std::equal(lhs.begin(),
                      lhs.end(),
                      rhs.begin(),
                      rhs.end(),
                      [](char lhs_character, char rhs_character)
                      {
                          return std::tolower(static_cast<unsigned char>(lhs_character)) ==
                                 std::tolower(static_cast<unsigned char>(rhs_character));
                      });
}
} // namespace

auto get_samples() -> const std::vector<sample_info>&
{
    static const std::vector<sample_info> samples{
        {"Barrowlight",
         "UnravelEngine-Sample-Survivors",
         "Complete game",
         "A survivors-style game from the title screen to the final boss: four heroes, weapons that fire on "
         "their own, an upgrade every level and fifteen minutes against thousands of animated enemies. Shows "
         "crowds simulated in parallel in C# and drawn as instances, particles, menus for gamepad and touch, "
         "audio buses and save data.",
         {"Complete Game", "Crowds", "C# Scripts"},
         "editor:/samples/sample_survivors.jpg",
         ICON_MDI_SWORD_CROSS,
         true},
        {"2048",
         "UnravelEngine-Sample-2048",
         "Casual game",
         "The sliding tile puzzle on a glowing 3D board: swipe, use the arrow keys or a gamepad to merge tiles "
         "up to 2048. Shows input actions with swipes, pooled prefabs, tiles animated by coroutines, a HUD "
         "bound to C#, merge sounds pitched by tile value and a best score saved between sessions.",
         {"Puzzle", "Mobile", "C# Scripts"},
         "editor:/samples/sample_2048.jpg",
         ICON_MDI_NUMERIC_2_BOX,
         true},
        {"Physics Playground",
         "UnravelEngine-Sample-Physics",
         "Physics",
         "Six stations around a plaza, walked in first person with a tool that pushes, carries and throws. "
         "Shows ragdolls that are knocked down and get up again, joints with motors, springs and break forces, "
         "physics materials, wind, water, magnets and explosions, a buggy on wheel joints, and ray, shape and "
         "overlap queries.",
         {"Physics", "Ragdolls", "Joints"},
         "editor:/samples/sample_physics.jpg",
         ICON_MDI_BOWLING,
         true},
        {"Animation Gallery",
         "UnravelEngine-Sample-Animation",
         "Animation",
         "A playable hero walks a plaza of exhibits: blend spaces, layers with masks and additive hits, clip "
         "events and curves, a state machine controller, root motion, one clip retargeted onto five rigs, "
         "foot, hand, chain and look-at IK, and spring bones.",
         {"Animation", "IK", "Retargeting"},
         "editor:/samples/sample_animation.jpg",
         ICON_MDI_RUN,
         true},
        {"Spell Arena",
         "UnravelEngine-Sample-VFX",
         "Visual effects",
         "A night arena where spells play out: lightning, fireball, frost nova, arcane vortex and holy light, "
         "with braziers, embers, mist, fireflies and a meteor storm. Shows GPU particles with sub-emitters, "
         "ribbons, soft and lit particles, and the scripts that cast them.",
         {"Particles", "VFX", "C# Scripts"},
         "editor:/samples/sample_vfx.jpg",
         ICON_MDI_AUTO_FIX,
         true},
        {"GI Showcase",
         "UnravelEngine-Sample-GI",
         "Global illumination",
         "A walk through a grand hall, a light lab, a gallery, a courtyard and a plaza, lit by the sun, "
         "emissive surfaces and lights alone. Shows dynamic global illumination and reflections, emissive "
         "light and a sun you can sweep through the day.",
         {"Global Illumination", "Reflections", "Lighting"},
         "editor:/samples/sample_gi.jpg",
         ICON_MDI_WHITE_BALANCE_SUNNY,
         true},
        {"Game UI",
         "UnravelEngine-Sample-UI",
         "User interface",
         "A main menu and three scenes of game UI in an RPG skin: every element and control, data models "
         "bound to C# scripts, and UI placed in the world. Built with RmlUi documents and style sheets.",
         {"UI", "RmlUi", "Data Binding"},
         "editor:/samples/sample_ui.jpg",
         ICON_MDI_VIEW_DASHBOARD_OUTLINE,
         true},
    };
    return samples;
}

auto get_repository_url(const sample_info& sample) -> std::string
{
    return std::string(engine_links::ORGANIZATION) + "/" + sample.repository;
}

auto get_download_url(const sample_info& sample) -> std::string
{
    return get_repository_url(sample) + "/archive/refs/heads/" + SAMPLE_CATALOG_DEFAULT_BRANCH + ".zip";
}

auto find_installed(const sample_info& sample, const std::vector<fs::path>& recent_projects) -> fs::path
{
    const auto it = std::find_if(recent_projects.begin(),
                                 recent_projects.end(),
                                 [&](const fs::path& project_path)
                                 {
                                     fs::error_code error;
                                     return is_same_name(project_path.filename().string(), sample.repository) &&
                                            fs::exists(project_path, error);
                                 });
    return it != recent_projects.end() ? *it : fs::path{};
}
} // namespace unravel::sample_catalog
