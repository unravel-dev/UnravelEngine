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
 */
enum class visualization_mode : int
{
    full = -1,
    base_color = 0,
    diffuse_color = 1,
    specular_color = 2,
    radiance = 3,
    irradiance = 4,
    ambient_occlusion = 5,
    normals = 6,
    roughness = 7,
    metalness = 8,
    emissive_color = 9,
    subsurface_color = 10,
    depth = 11,
    ssil = 12,
    radiance_alpha = 13,
    specular_occlusion = 14,
    sdf_normals = 15,
    sdf_step_count = 16,
    sdf_headers = 17,
    sdf_probe = 18,
    sdf_entry = 19,
    sdf_clipmap = 20,
    sdf_direct = 21,
    sdf_cascade_levels = 22,
    sdf_attr_albedo = 23,
    sdf_light_voxels = 24,
    sdf_world_probes = 25,
    sdf_sun_tiers = 26,
    sdf_probe_sky = 27,
    sdf_vis_memo = 28,
    velocity = 29,
};

struct visualization_mode_entry
{
    visualization_mode mode;
    /// Stable snake_case id - the MCP tool contract. Never rename existing ids.
    const char* name;
    /// Human-readable menu label.
    const char* label;
};

/// Every mode in menu order (full first, then pipeline id order).
auto get_visualization_modes() -> hpp::span<const visualization_mode_entry>;

/// Entry for a raw pipeline id; nullptr when the id maps to no mode.
auto find_visualization_mode(int value) -> const visualization_mode_entry*;

/// Entry for a stable snake_case name; nullptr when unknown.
auto find_visualization_mode(std::string_view name) -> const visualization_mode_entry*;

/// Stable name for a mode ("unknown" for unmapped raw values).
auto to_string(visualization_mode mode) -> const char*;

} // namespace unravel
