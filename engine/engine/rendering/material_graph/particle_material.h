#pragma once
#include <engine/engine_export.h>

#include "material_graph.h"

#include <hpp/optional.hpp>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{

class graph_material;

/// The texture node every Standard particle material reads its picture through: what a material instance names to give
/// an emitter a texture of its own.
inline constexpr const char* PARTICLE_TEXTURE_NAME = "Texture";
/// The normal map node of the Standard particle materials that read one.
inline constexpr const char* PARTICLE_NORMAL_MAP_NAME = "Normal Map";

/**
 * @brief A Standard particle material: a Surface material with Particle Use that draws a texture, or the frames of a
 * texture sheet, tinted by the particle's colour. A description only: make_particle_material_graph turns it into the
 * graph that draws it. The engine ships one material for each (get_particle_material_key).
 *
 * The tinted texture is the surface's Base Color, so the emitter's Lighting decides how it shows: as the light the
 * particle gives off (Unlit), or lit by the scene (Volumetric, Surface). Modulate filters the scene and is never lit.
 */
struct particle_surface
{
    /// How the particles blend over the scene: Translucent, Premultiplied, Additive or Modulate.
    material_blend_mode blend{material_blend_mode::translucent};
    /// The texture is a mask: its luminance is the opacity and its colour, tinted by the particle's, shows as bright as
    /// the mask is. Off, the texture gives a colour and an alpha.
    bool is_mask{};
    /// Surface lighting takes each pixel's normal from a normal map laid out like the texture. Off, a particle shades as
    /// a sphere.
    bool has_normal_map{};

    auto operator==(const particle_surface& other) const -> bool = default;
};

/// @p surface as the engine ships it: no normal map on a Modulate surface, which is never lit, and a mask of the
/// Translucent Blend Mode as the Premultiplied one, which draws it (a mask's colour carries its opacity).
auto get_shipped_particle_surface(const particle_surface& surface) -> particle_surface;

/// The graph that draws @p surface, as the engine ships it (get_shipped_particle_surface).
auto make_particle_material_graph(const particle_surface& surface) -> material_graph;

/// A graph material made of make_particle_material_graph(@p surface).
auto make_particle_material(const particle_surface& surface) -> std::shared_ptr<graph_material>;

/// The engine material that draws @p surface: "engine:/data/materials/particles/particle_mask_additive.mat".
auto get_particle_material_key(const particle_surface& surface) -> std::string;

/// Every surface the engine ships a particle material for, each once.
auto get_shipped_particle_surfaces() -> std::vector<particle_surface>;

/// The surface the engine's material @p key draws, as it is asked for (a mask's one alpha-blended material answers
/// Translucent, a Modulate one without a normal map); empty for any other key.
auto find_shipped_particle_surface(const std::string& key) -> hpp::optional<particle_surface>;

} // namespace unravel
