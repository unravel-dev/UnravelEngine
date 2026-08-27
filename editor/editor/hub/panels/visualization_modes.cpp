#include "visualization_modes.h"

#include <engine/rendering/pipeline/deferred/pipeline.h>

#include <array>

namespace unravel
{
namespace
{

constexpr std::array<visualization_mode_entry, 31> k_visualization_modes = {{
    {visualization_mode::full, "full", "Full"},
    {visualization_mode::base_color, "base_color", "Base Color"},
    {visualization_mode::diffuse_color, "diffuse_color", "Diffuse Color"},
    {visualization_mode::specular_color, "specular_color", "Specular Color"},
    {visualization_mode::radiance, "radiance", "Radiance"},
    {visualization_mode::irradiance, "irradiance", "Irradiance"},
    {visualization_mode::ambient_occlusion, "ambient_occlusion", "Ambient Occlusion"},
    {visualization_mode::normals, "normals", "Normals (World Space)"},
    {visualization_mode::roughness, "roughness", "Roughness"},
    {visualization_mode::metalness, "metalness", "Metalness"},
    {visualization_mode::emissive_color, "emissive_color", "Emissive Color"},
    {visualization_mode::subsurface_color, "subsurface_color", "Subsurface Color"},
    {visualization_mode::depth, "depth", "Depth"},
    {visualization_mode::ssil, "ssil", "SSIL"},
    {visualization_mode::radiance_alpha, "radiance_alpha", "Radiance Alpha"},
    {visualization_mode::specular_occlusion, "specular_occlusion", "Specular Occlusion"},
    {visualization_mode::sdf_normals, "sdf_normals", "SDF (Normals)"},
    {visualization_mode::sdf_step_count, "sdf_step_count", "SDF (Step Count)"},
    {visualization_mode::sdf_headers, "sdf_headers", "SDF (Headers)"},
    {visualization_mode::sdf_probe, "sdf_probe", "SDF (Probe)"},
    {visualization_mode::sdf_entry, "sdf_entry", "SDF (Entry)"},
    {visualization_mode::sdf_clipmap, "sdf_clipmap", "SDF (Clipmap)"},
    {visualization_mode::sdf_direct, "sdf_direct", "SDF (Direct Light)"},
    {visualization_mode::sdf_cascade_levels, "sdf_cascade_levels", "SDF (Cascade Levels)"},
    {visualization_mode::sdf_attr_albedo, "sdf_attr_albedo", "SDF (Attr Albedo)"},
    {visualization_mode::sdf_light_voxels, "sdf_light_voxels", "SDF (Light Voxels)"},
    {visualization_mode::sdf_world_probes, "sdf_world_probes", "SDF (World Probes)"},
    {visualization_mode::sdf_sun_tiers, "sdf_sun_tiers", "SDF (Sun Tiers)"},
    {visualization_mode::sdf_probe_sky, "sdf_probe_sky", "SDF (Probe Sky)"},
    {visualization_mode::sdf_vis_memo, "sdf_vis_memo", "SDF (Vis Memo)"},
    {visualization_mode::velocity, "velocity", "Velocity"},
}};

// Drift guards: the enum is the editor-side mirror of the engine's debug pass ids.
static_assert(static_cast<int>(visualization_mode::sdf_normals) == rendering::deferred::debug_pass_sdf_normals,
              "visualization_mode drifted from deferred::debug_pass_sdf_normals");
static_assert(static_cast<int>(visualization_mode::sdf_vis_memo) == rendering::deferred::debug_pass_sdf_vis_memo,
              "visualization_mode drifted from deferred::debug_pass_sdf_vis_memo");
static_assert(static_cast<int>(visualization_mode::velocity) == rendering::deferred::debug_pass_velocity,
              "visualization_mode drifted from deferred::debug_pass_velocity");

} // namespace

auto get_visualization_modes() -> hpp::span<const visualization_mode_entry>
{
    return {k_visualization_modes.data(), k_visualization_modes.size()};
}

auto find_visualization_mode(int value) -> const visualization_mode_entry*
{
    for(const auto& entry : k_visualization_modes)
    {
        if(static_cast<int>(entry.mode) == value)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto find_visualization_mode(std::string_view name) -> const visualization_mode_entry*
{
    for(const auto& entry : k_visualization_modes)
    {
        if(name == entry.name)
        {
            return &entry;
        }
    }
    return nullptr;
}

auto to_string(visualization_mode mode) -> const char*
{
    const auto* entry = find_visualization_mode(static_cast<int>(mode));
    return entry != nullptr ? entry->name : "unknown";
}

} // namespace unravel
