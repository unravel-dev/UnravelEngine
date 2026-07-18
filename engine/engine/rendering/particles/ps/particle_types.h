#pragma once

#include <cstdint>
#include <limits>

namespace unravel
{
namespace ps_soa
{

struct emitter_handle
{
    uint16_t idx = std::numeric_limits<uint16_t>::max();
};

inline auto is_valid(emitter_handle handle) -> bool
{
    return handle.idx != std::numeric_limits<uint16_t>::max();
}

/// Emission volume shape. Ordinals match legacy EmitterShape for content compatibility.
enum class emitter_shape : int
{
    sphere = 0,
    hemisphere,
    circle,
    box,
    rect,
    count
};

/// Initial emission direction. Ordinals match legacy EmitterDirection.
enum class emitter_direction : int
{
    up = 0,
    outward,
    inward,
    count
};

/// Where particles spawn within the shape. Ordinals match legacy EmitterSpawnLocation.
enum class spawn_location : int
{
    inside = 0,
    surface,
    count
};

/// Simulation space. Ordinals match legacy SimulationSpace.
enum class simulation_space : int
{
    world = 0,
    local,
    count
};

/// Texture interpretation. Ordinals match legacy TextureMode.
enum class texture_mode : int
{
    multi_channel = 0,
    mask,
    count
};

/// Billboard / facing mode. Ordinals match legacy RenderMode.
enum class render_mode : int
{
    billboard = 0,
    horizontal_billboard,
    vertical_billboard,
    count
};

/// Blend mode. Ordinals match legacy BlendMode.
enum class blend_mode : int
{
    normal = 0,
    additive,
    multiply
};

/// Feature bits baked from authoring desc for specialized update/render paths.
enum class emitter_feature : uint32_t
{
    none = 0,
    align_to_direction = 1u << 0,
    texsheet = 1u << 1,
    color_by_speed = 1u << 2,
    size_by_speed = 1u << 3,
    lifetime_by_emitter_speed = 1u << 4,
    local_space = 1u << 5,
    non_linear_ease = 1u << 6,
};

inline constexpr auto operator|(emitter_feature a, emitter_feature b) -> emitter_feature
{
    return static_cast<emitter_feature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr auto operator&(emitter_feature a, emitter_feature b) -> emitter_feature
{
    return static_cast<emitter_feature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr auto has_feature(emitter_feature mask, emitter_feature bit) -> bool
{
    return (static_cast<uint32_t>(mask) & static_cast<uint32_t>(bit)) != 0u;
}

} // namespace ps_soa
} // namespace unravel
