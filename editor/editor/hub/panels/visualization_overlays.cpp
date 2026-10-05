#include "visualization_overlays.h"
#include "visualization_menu.h"
#include "visualization_widgets.h"

#include <editor/imgui/integration/imgui.h>
#include <hpp/span.hpp>

#include <array>

namespace unravel::visualization_overlays
{
namespace
{
using visualization_menu::state;
using world_settings = lumen_visualize_pass::world_settings;

/// The card distance range of the sliders, in metres.
constexpr float card_min_distance = 1.0f;
constexpr float card_max_distance = 500.0f;
/// The card index range of the sliders (-1 = every card; a mesh gets at most 32 cards per submesh).
constexpr int card_max_index = 31;
constexpr float radiosity_probe_min_radius = 0.01f;
constexpr float radiosity_probe_max_radius = 1.0f;
constexpr float radiance_cache_min_radius_scale = 0.1f;
constexpr float radiance_cache_max_radius_scale = 10.0f;
/// The radiance cache's last clipmap (-1 = all of them).
constexpr int radiance_cache_max_clipmap = 3;
constexpr float surfel_min_scale = 0.1f;
constexpr float surfel_max_scale = 10.0f;
constexpr int surfel_max_count = 10000;
/// world_settings::card_placement_direction + 1.
constexpr std::array<const char*, 7> card_sides = {"All", "-X", "+X", "-Y", "+Y", "-Z", "+Z"};
/// world_settings::screen_probe_placement.
constexpr std::array<const char*, 4> placement_views = {"Counts Only", "Uniform + Adaptive", "Adaptive", "Normals"};
/// state::is_card_generation_cluster as an index.
constexpr std::array<const char*, 2> card_generation_kinds = {"Surfels", "Clusters"};

struct overlay_entry
{
    const char* label;
    const char* description;
    auto (*is_on)(const state&) -> bool;
    void (*set_on)(state&, bool);
    /// nullptr for an overlay without options.
    void (*draw_options)(state&);
};

struct overlay_section
{
    const char* label;
    hpp::span<const overlay_entry> entries;
};

void draw_distance_option(world_settings& settings, const char* tooltip)
{
    visualization_widgets::draw_option_label("Distance", tooltip);
    ImGui::SliderFloat("##distance", &settings.card_placement_distance, card_min_distance, card_max_distance, "%.0f m");
}

void draw_card_index_option(world_settings& settings, const char* tooltip)
{
    visualization_widgets::draw_option_label("Card Index", tooltip);
    ImGui::SliderInt("##card_index",
                     &settings.card_placement_index,
                     -1,
                     card_max_index,
                     settings.card_placement_index < 0 ? "All" : "%d");
}

// -- Card placement -------------------------------------------------------------

auto is_card_placement_on(const state& menu_state) -> bool
{
    return menu_state.lumen.card_placement;
}

void set_card_placement_on(state& menu_state, bool is_on)
{
    menu_state.lumen.card_placement = is_on;
}

void draw_card_placement_options(state& menu_state)
{
    draw_distance_option(menu_state.lumen, "Only objects whose bounds come within this distance of the camera.");
    draw_card_index_option(menu_state.lumen,
                           "Only the card with this index among its mesh's cards. All shows every card.");
}

// -- Card generation ------------------------------------------------------------

auto is_card_generation_on(const state& menu_state) -> bool
{
    return menu_state.lumen.is_card_generation();
}

void set_card_generation_on(state& menu_state, bool is_on)
{
    menu_state.lumen.card_generation_surfels = is_on && !menu_state.is_card_generation_cluster;
    menu_state.lumen.card_generation_cluster = is_on && menu_state.is_card_generation_cluster;
}

void draw_card_generation_options(state& menu_state)
{
    auto& settings = menu_state.lumen;
    int kind = settings.card_generation_surfels ? 0 : 1;
    visualization_widgets::draw_option_label("Show",
                                             "Surfels: every surface sample of the build. Clusters: the samples "
                                             "each card took.");
    if(visualization_widgets::draw_segmented("##kind",
                                             card_generation_kinds.data(),
                                             nullptr,
                                             int(card_generation_kinds.size()),
                                             ImGui::CalcItemWidth(),
                                             kind))
    {
        menu_state.is_card_generation_cluster = kind == 1;
        set_card_generation_on(menu_state, true);
    }
    draw_distance_option(settings,
                         "Only meshes whose bounds come within this distance of the camera. Shared with Card "
                         "Placement.");
    if(settings.card_generation_cluster)
    {
        draw_card_index_option(settings,
                               "Only the card with this index among its mesh's cards. All shows every card. Shared "
                               "with Card Placement.");
        int side = settings.card_placement_direction + 1;
        visualization_widgets::draw_option_label("Card Side", "Only the cards facing this side of their mesh.");
        if(ImGui::Combo("##card_side", &side, card_sides.data(), int(card_sides.size())))
        {
            settings.card_placement_direction = side - 1;
        }
    }
    visualization_widgets::draw_option_label("Surfel Scale", "The discs' radius, in multiples of 2 cm.");
    ImGui::SliderFloat("##surfel_scale", &settings.card_generation_surfel_scale, surfel_min_scale, surfel_max_scale, "%.1f");
    visualization_widgets::draw_option_label("Max Surfels",
                                             "At most this many discs of each kind per mesh. From 0 on, the "
                                             "clusters also show each cluster's bounds and mean normal.");
    ImGui::SliderInt("##max_surfels",
                     &settings.card_generation_max_surfel,
                     -1,
                     surfel_max_count,
                     settings.card_generation_max_surfel < 0 ? "All" : "%d");
}

// -- Radiosity probes -----------------------------------------------------------

auto is_radiosity_probes_on(const state& menu_state) -> bool
{
    return menu_state.lumen.radiosity_probes;
}

void set_radiosity_probes_on(state& menu_state, bool is_on)
{
    menu_state.lumen.radiosity_probes = is_on;
}

void draw_radiosity_probe_options(state& menu_state)
{
    auto& settings = menu_state.lumen;
    visualization_widgets::draw_option_label("Radius", "The spheres' radius.");
    ImGui::SliderFloat("##radius",
                       &settings.radiosity_probe_radius,
                       radiosity_probe_min_radius,
                       radiosity_probe_max_radius,
                       "%.2f m");
    visualization_widgets::draw_option_label("Show Invalid", "Also the probes whose texel holds no surface, in pink.");
    visualization_widgets::draw_switch("##show_invalid", settings.radiosity_show_invalid);
}

// -- Radiance cache probes ------------------------------------------------------

auto is_radiance_cache_probes_on(const state& menu_state) -> bool
{
    return menu_state.lumen.radiance_cache_probes;
}

void set_radiance_cache_probes_on(state& menu_state, bool is_on)
{
    menu_state.lumen.radiance_cache_probes = is_on;
}

void draw_radiance_cache_options(state& menu_state)
{
    auto& settings = menu_state.lumen;
    visualization_widgets::draw_option_label("Radius Scale", "The spheres' radius, in 5% of their clipmap's cell size.");
    ImGui::SliderFloat("##radius_scale",
                       &settings.radiance_cache_radius_scale,
                       radiance_cache_min_radius_scale,
                       radiance_cache_max_radius_scale,
                       "%.1f");
    visualization_widgets::draw_option_label("Clipmap", "Only this clipmap's probes. All shows every clipmap.");
    ImGui::SliderInt("##clipmap",
                     &settings.radiance_cache_clipmap,
                     -1,
                     radiance_cache_max_clipmap,
                     settings.radiance_cache_clipmap < 0 ? "All" : "%d");
}

// -- Radiance cache stats -------------------------------------------------------

auto is_radiance_cache_stats_on(const state& menu_state) -> bool
{
    return menu_state.lumen.radiance_cache_stats;
}

void set_radiance_cache_stats_on(state& menu_state, bool is_on)
{
    menu_state.lumen.radiance_cache_stats = is_on;
}

// -- Screen probes --------------------------------------------------------------

auto is_screen_probes_on(const state& menu_state) -> bool
{
    return menu_state.lumen.screen_probe_gather_debug;
}

void set_screen_probes_on(state& menu_state, bool is_on)
{
    menu_state.lumen.screen_probe_gather_debug = is_on;
    if(is_on)
    {
        menu_state.lumen.screen_probe_placement = menu_state.screen_probe_placement;
    }
}

void draw_screen_probe_options(state& menu_state)
{
    auto& settings = menu_state.lumen;
    visualization_widgets::draw_option_label("Placement",
                                             "Uniform + Adaptive marks every probe at its pixel, the uniform ones "
                                             "yellow and the adaptive ones magenta. Adaptive marks the adaptive ones "
                                             "alone. Normals draws each probe as a cross with a 10 cm line along its "
                                             "normal. Counts Only leaves the text alone.");
    if(ImGui::Combo("##placement", &settings.screen_probe_placement, placement_views.data(), int(placement_views.size())))
    {
        menu_state.screen_probe_placement = settings.screen_probe_placement;
    }
}

// -- Screen probe rays ----------------------------------------------------------

auto is_screen_probe_rays_on(const state& menu_state) -> bool
{
    return menu_state.lumen.screen_probe_traces;
}

void set_screen_probe_rays_on(state& menu_state, bool is_on)
{
    menu_state.lumen.screen_probe_traces = is_on;
}

void draw_screen_probe_ray_options(state& menu_state)
{
    visualization_widgets::draw_option_label("Freeze", "Keep the rays recorded last, to look at them from elsewhere.");
    visualization_widgets::draw_switch("##freeze", menu_state.lumen.screen_probe_traces_freeze);
}

// -- Reflection ray -------------------------------------------------------------

auto is_reflection_ray_on(const state& menu_state) -> bool
{
    return menu_state.lumen.reflection_traces;
}

void set_reflection_ray_on(state& menu_state, bool is_on)
{
    menu_state.lumen.reflection_traces = is_on;
}

constexpr std::array<overlay_entry, 2> surface_cache_overlays = {{
    {"Card Placement",
     "The box of every card that holds a page of the surface cache, each in a color of its own, with the face the "
     "card captures the surface from shaded in that color.",
     is_card_placement_on,
     set_card_placement_on,
     draw_card_placement_options},
    {"Card Generation",
     "What each mesh's card build saw. Surfels shows every surface sample the build took as a small disc, green when "
     "kept and red when dropped as inside geometry. Clusters shows, per card, the samples it took in its color, those "
     "an earlier card of the same side took in grey and the rest in blue, with white rays to the planes they were "
     "seen from. Meshes in range build their cards again, recording, the first time they show.",
     is_card_generation_on,
     set_card_generation_on,
     draw_card_generation_options},
}};

constexpr std::array<overlay_entry, 4> probe_overlays = {{
    {"Radiosity Probes",
     "Every radiosity probe of the surface cache as a sphere lit by the irradiance it gathered, one probe per 4 x 4 "
     "card texels.",
     is_radiosity_probes_on,
     set_radiosity_probes_on,
     draw_radiosity_probe_options},
    {"Radiance Cache Probes",
     "Every probe of the world-space radiance cache as a sphere showing the radiance it holds in each direction.",
     is_radiance_cache_probes_on,
     set_radiance_cache_probes_on,
     draw_radiance_cache_options},
    {"Radiance Cache Stats",
     "The radiance cache's update as text in the top left of the view: the trace cost in each priority bucket, the "
     "bucket the budget ran out in, what the update traced, and how full the probe atlas is (red when full: new probes "
     "then go without one).",
     is_radiance_cache_stats_on,
     set_radiance_cache_stats_on,
     nullptr},
    {"Screen Probes",
     "The screen probe counts as text in the top left of the view, and where the probes sit.",
     is_screen_probes_on,
     set_screen_probes_on,
     draw_screen_probe_options},
}};

constexpr std::array<overlay_entry, 2> ray_overlays = {{
    {"Screen Probe Rays",
     "The rays of the screen probe under the mouse (the view's center while the mouse is elsewhere), each a line from "
     "the probe to its hit in the radiance it brought back. Hits closer than 1 cm show 5 cm long in red. The probes "
     "hold their jitter still meanwhile.",
     is_screen_probe_rays_on,
     set_screen_probe_rays_on,
     draw_screen_probe_ray_options},
    {"Reflection Ray",
     "The reflection ray of the pixel under the mouse: a line from the pixel to where its trace ended, in the radiance "
     "it brought back, with a yellow cross at the end and its values as text in the top left of the view. Nothing "
     "shows while the mouse is elsewhere or where the pixel traces no reflection ray.",
     is_reflection_ray_on,
     set_reflection_ray_on,
     nullptr},
}};

constexpr std::array<overlay_section, 3> overlay_sections = {{
    {"Surface Cache", surface_cache_overlays},
    {"Probes", probe_overlays},
    {"Rays", ray_overlays},
}};

} // namespace

auto count_active(const visualization_menu::state& menu_state) -> int
{
    int count = 0;
    for(const auto& section : overlay_sections)
    {
        for(const auto& entry : section.entries)
        {
            count += entry.is_on(menu_state) ? 1 : 0;
        }
    }
    return count;
}

void turn_off(visualization_menu::state& menu_state)
{
    for(const auto& section : overlay_sections)
    {
        for(const auto& entry : section.entries)
        {
            entry.set_on(menu_state, false);
        }
    }
}

void draw_tab(visualization_menu::state& menu_state)
{
    visualization_widgets::push_field_style();
    for(const auto& section : overlay_sections)
    {
        visualization_widgets::draw_section_header(section.label);
        for(const auto& entry : section.entries)
        {
            ImGui::PushID(entry.label);
            bool is_on = entry.is_on(menu_state);
            if(visualization_widgets::draw_switch_row(entry.label, entry.description, is_on))
            {
                entry.set_on(menu_state, is_on);
            }
            if(is_on && entry.draw_options != nullptr)
            {
                entry.draw_options(menu_state);
            }
            ImGui::PopID();
        }
    }
    visualization_widgets::pop_field_style();
}

} // namespace unravel::visualization_overlays
