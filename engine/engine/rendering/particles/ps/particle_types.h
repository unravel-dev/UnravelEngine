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
    /// Base disc of a cone; particles launch along its slant (the rim at the cone angle, the centre straight up).
    cone,
    /// A segment along X.
    line,
    /// A ring in the XZ plane with a round tube (torus_thickness of the ring's radius).
    torus,
    /// The surface or the vertices of a mesh.
    mesh,
    count
};

/// What a particle does where its path first crosses a collision plane.
enum class collision_mode : int
{
    none = 0,
    /// It dies there.
    kill,
    /// It bounces off (restitution, friction), up to max_bounces times, then dies at the next contact.
    bounce,
    count
};

/// What happened to a particle (sub-emitter triggers; bits of emitter_desc::event_mask).
enum class particle_event_type : uint32_t
{
    birth = 1u << 0,
    death = 1u << 1,
    collision = 1u << 2,
};

/// How particles are lit.
enum class particle_lighting : int
{
    /// The color is the light the particle gives off (fire, magic, sparks).
    unlit = 0,
    /// Lit once per particle, like a small cloud: sky, sun (with its shadow) and nearby lights, the same from
    /// every side (smoke, dust, steam).
    volumetric,
    /// Lit like a small sphere facing the camera: the sun per pixel with a soft wrap, sky and nearby lights per
    /// particle (debris, snow, leaves).
    surface,
    count
};

/// Where a mesh shape spawns.
enum class mesh_spawn_mode : int
{
    /// Anywhere on the surface, evenly by area.
    surface = 0,
    /// On the vertices.
    vertices,
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
    /// Parallel to the camera's view plane: every particle shows the same face, also at the screen edge.
    billboard = 0,
    horizontal_billboard,
    vertical_billboard,
    /// Turned toward the camera's position: no stretch near the screen edges, no swim as the camera turns.
    camera_facing,
    /// Stretched along its motion, its width facing the camera (sparks, rain, streaks).
    stretched,
    /// A ribbon along the particle's own path over the last trail_length seconds, facing the camera (comets,
    /// tracers, magic streaks): each point of it is the particle evaluated at that earlier time.
    trail,
    /// One ribbon through all of the emitter's particles in birth order, facing the camera (swords' arcs, smoke
    /// streams behind a moving emitter, energy beams): each particle is a point of it, oldest at the tail.
    ribbon,
    /// A mesh per particle with its material, drawn with the scene's opaque geometry (debris, rocks, shells,
    /// leaves): lit, shadowed and with motion vectors like any model.
    mesh,
    /// Upright along facing_axis (in the emitter's space), turning about it toward the camera (beams, grass,
    /// falling streaks along a slope).
    fixed_axis,
    count
};

/// Shortest ribbon_tile_time, seconds.
constexpr float k_min_ribbon_tile_time = 0.01f;

/// How a ribbon's texture runs along it (its V coordinate; U crosses the ribbon).
enum class ribbon_texture_mode : int
{
    /// With each particle's age: 0 where particles are born, 1 where they die. The texture flows with them.
    age = 0,
    /// Once over the whole ribbon, head to tail, whatever its length.
    stretch,
    /// Repeated every ribbon_tile_time seconds of the particles' age, flowing with them.
    tile,
    count
};

/// Which way a ribbon's width runs at each of its particles (the width is the particle's size, centered on it).
enum class ribbon_facing : int
{
    /// Across the ribbon's direction, facing the camera (beams, streams, magic streaks).
    camera = 0,
    /// Along the particle's launch direction, as it was at birth: particles launched along a blade sweep its arc
    /// as one sheet from hilt to tip (sword slashes). Launch them slowly; the direction is all that counts.
    launch_direction,
    count
};

/// Blend mode. Ordinals match legacy BlendMode.
enum class blend_mode : int
{
    normal = 0,
    additive,
    multiply,
    /// Normal blending of a texture whose color is already multiplied by its alpha.
    premultiplied,
};

/// Order the particles of one emitter draw in.
enum class sort_mode : int
{
    /// Back to front by distance for Normal blend; unsorted for Additive and Multiply, whose result does
    /// not depend on order.
    automatic = 0,
    /// Spawn slot order (stable, no sort).
    none,
    /// Back to front by distance to the camera.
    view_distance,
    /// Back to front along the camera's forward axis.
    view_depth,
    /// The oldest particles draw first, the youngest on top.
    oldest_first,
    /// The youngest particles draw first, the oldest on top.
    youngest_first,
    count
};

/// Geometry each particle is drawn with.
enum class particle_primitive : int
{
    /// The whole square.
    quad = 0,
    /// The square with its corners cut off (8 vertices): less overdraw for round, soft textures, whose
    /// corners are transparent anyway.
    octagon,
    /// The strip trail mode draws (render_mode::trail picks it; not an authoring choice).
    trail,
    /// The quad a ribbon segment draws between two particles, seen from both sides (render_mode::ribbon picks it).
    ribbon,
    /// A polygon of 8 vertices around each texture sheet frame's opaque texels, baked from the texture: the
    /// transparent texels around sparks, flames and debris are not drawn. The whole quad until the bake is in.
    cutout,
    count
};

/// How an emitter's culling bounds are found.
enum class bounds_mode : int
{
    /// Worked out every frame from the emitter's settings: the spawn region, how far its particles can
    /// travel and how large they can grow.
    automatic = 0,
    /// A box set by hand, in the emitter's local space.
    fixed,
    count
};

/// Feature bits baked from the authoring desc. The low bits match PARTICLE_FLAG_* in
/// engine_data/data/shaders/particles/particle_common.sh, which the GPU reads from the emitter table.
enum class emitter_feature : uint32_t
{
    none = 0,
    align_to_direction = 1u << 0,
    texsheet = 1u << 1,
    color_by_speed = 1u << 2,
    size_by_speed = 1u << 3,
    local_space = 1u << 4,
    non_linear_ease = 1u << 5,
    speed_curve = 1u << 6,
    frame_blending = 1u << 7,
    texsheet_randomize = 1u << 8,
    texsheet_single_pass = 1u << 9,
    random_spin_direction = 1u << 10,
    /// The texture's alpha dissolves away over life (erosion curve).
    erosion = 1u << 11,
    /// Particles smaller than a pixel stay one pixel and fade by the area they lost.
    subpixel_fade = 1u << 12,
    /// Particles fade out as they near the camera.
    camera_fade = 1u << 13,
    /// The size axes curve scales each axis over life (x and y; z for meshes).
    size_axis_curve = 1u << 14,
    /// The spin follows a curve over life (the angle is its integral).
    spin_curve = 1u << 15,
    /// The color's intensity follows a curve over life.
    intensity_curve = 1u << 16,
    /// Each particle's color is a random mix of two gradients.
    two_gradients = 1u << 17,
    /// The texture sheet frame follows a curve over life instead of cycling.
    texsheet_frame_curve = 1u << 18,
    /// Each particle plays one random row of the texture sheet.
    texsheet_random_row = 1u << 19,
    /// A curl-noise field displaces the particles (particle_noise.h).
    turbulence = 1u << 20,
    /// Particles beyond the max draw distance are culled, and fade out before it.
    draw_distance = 1u << 21,
    /// Lit as small clouds (particle_lighting::volumetric).
    lit_volumetric = 1u << 22,
    /// Lit as small spheres (particle_lighting::surface).
    lit_surface = 1u << 23,
    /// CPU only: the lifetime of new particles follows the emitter's speed.
    lifetime_by_emitter_speed = 1u << 24,
    /// CPU only: the particles are lit (either lighting mode).
    lit = lit_volumetric | lit_surface,
};

/// The bits of emitter_feature the shaders read (exact as a float in the emitter table: below 2^24).
constexpr uint32_t gpu_feature_bits = (1u << 24) - 1u;

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
