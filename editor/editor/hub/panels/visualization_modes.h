#pragma once
#include <hpp/span.hpp>

#include <string_view>

namespace unravel
{

/**
 * @brief Debug visualization modes for the Scene/Game panel viewports and the MCP
 * viewport_set_debug_view tool - the single source of truth shared by all three.
 *
 * Values are the deferred pipeline's debug pass ids (rendering::deferred::debug_pass_*;
 * modes 0..14 are the G-buffer visualizer shader's switch). static_asserts in the .cpp
 * pin the enum to the engine constants so the two cannot drift.
 *
 * The enumerator names follow the STABLE ID of each mode (visualization_mode_entry::name),
 * which is what the MCP contract and the docs use.
 */
enum class visualization_mode : int
{
    full = -1,
    base_color = 0,
    diffuse_color = 1,
    specular_color = 2,
    /// RBUFFER rgb: SSR composited over the GI reflection tier.
    reflections = 3,
    irradiance = 4,
    ambient_occlusion = 5,
    normals = 6,
    roughness = 7,
    metalness = 8,
    emissive_color = 9,
    subsurface_color = 10,
    depth = 11,
    /// GI_RESOLVE when the surface cache runs, SSIL otherwise.
    indirect_diffuse = 12,
    /// RBUFFER alpha: how strongly the reflection buffer replaces probe specular.
    reflection_coverage = 13,
    specular_occlusion = 14,
    sdf_normals = 15,
    sdf_step_count = 16,
    sdf_headers = 17,
    sdf_brick_probe = 18,
    sdf_bounds_entry = 19,
    sdf_clipmap = 20,
    gi_direct_lighting = 21,
    sdf_cascade_levels = 22,
    gi_voxel_albedo = 23,
    gi_light_voxels = 24,
    gi_world_probes = 25,
    gi_sun_tiers = 26,
    gi_probe_sky = 27,
    gi_vis_memo = 28,
    velocity = 29,
    gtao = 30,
    gtao_bent_normal = 31,
    gi_attr_emissive = 32,
    gi_cage_health = 33,
    gi_dirty_regions = 34,
    gi_probe_lattice = 35,
    gi_screen_probes = 36,
    gi_temporal = 37,
    gi_probe_tiers = 38,
    /// The temporal's reset cause per pixel (fresh / dirty region / camera / moving / detector).
    gi_temporal_cause = 39,
    /// Explicit emitter sampling census per screen probe (aimed energy share, aimed rays, emitters).
    gi_emitter_share = 40,
};

/// Menu grouping. Ordering here is the order the groups appear in the viewport menu.
enum class visualization_group : int
{
    /// Not a group: only visualization_mode::full, drawn as the menu's top-level off switch.
    none = 0,
    /// G-Buffer channels exactly as the geometry pass wrote them.
    surface,
    /// The occlusion chain: material AO, GTAO, and the derived specular term.
    occlusion,
    /// Lighting buffers the deferred passes produce and consume.
    lighting,
    /// Screen-space motion.
    motion,
    /// Distance-field integrity: sphere-traced from the camera, not the raster image.
    distance_field,
    /// Global illumination caches read at the traced hit.
    global_illumination,
    count,
};

/// One row of a mode's color legend: the color as it appears on screen and what it means.
struct visualization_swatch
{
    /// Display-space RGB, 0..1 - the value the debug shader writes to the output.
    float color[3];
    const char* meaning;
};

struct visualization_mode_entry
{
    visualization_mode mode;
    visualization_group group;
    /// Stable snake_case id - the MCP tool contract.
    const char* name;
    /// Human-readable menu label.
    const char* label;
    /// One line stating what the view actually shows, including its continuous ranges.
    const char* description;
    /// Categorical colors only; empty for views that are a plain continuous readout.
    hpp::span<const visualization_swatch> legend;
};

struct visualization_group_entry
{
    visualization_group group;
    /// Stable snake_case id - the MCP tool contract, same rules as a mode's name.
    const char* name;
    /// ICON_MDI_* UTF-8 literal for the menu.
    const char* icon;
    const char* label;
    /// One line covering what the whole group reads from, including its preconditions.
    const char* description;
};

/// Every mode in menu order (full first, then group by group).
auto get_visualization_modes() -> hpp::span<const visualization_mode_entry>;

/// Every group in menu order.
auto get_visualization_groups() -> hpp::span<const visualization_group_entry>;

/// The modes belonging to one group, in menu order.
auto get_visualization_modes(visualization_group group) -> hpp::span<const visualization_mode_entry>;

/// Descriptor for a group; never null for a group below visualization_group::count.
auto find_visualization_group(visualization_group group) -> const visualization_group_entry*;

/// Descriptor for a stable snake_case group id; nullptr when unknown.
auto find_visualization_group(std::string_view name) -> const visualization_group_entry*;

/// Entry for a raw pipeline id; nullptr when the id maps to no mode.
auto find_visualization_mode(int value) -> const visualization_mode_entry*;

/// Entry for a stable snake_case name; nullptr when unknown.
auto find_visualization_mode(std::string_view name) -> const visualization_mode_entry*;

/// Stable name for a mode ("unknown" for unmapped raw values).
auto to_string(visualization_mode mode) -> const char*;

} // namespace unravel
