#pragma once
#include <engine/engine_export.h>

#include "material_graph.h"

#include <engine/rendering/material.h>
#include <math/math.h>

#include <memory>
#include <string>
#include <vector>

namespace unravel
{

/**
 * @brief How a Standard material's surface is authored. Both fill the one metal / roughness G-buffer: the specular
 * workflow's diffuse, specular and glossiness are converted per pixel (mg_specular_to_metallic in material_graph.sh).
 */
enum class standard_workflow : uint8_t
{
    /// Base color, metallic and roughness.
    metallic,
    /// Diffuse color, specular color and glossiness (KHR_materials_pbrSpecularGlossiness).
    specular,
};

/**
 * @brief The values and maps of a Standard material: what a PBR material held, and the specular workflow's. A
 * description only: make_standard_material_graph turns it into the graph that draws it. Colors are as a color picker
 * gives them (sRGB); the graph reads them in linear space.
 */
struct standard_surface
{
    standard_workflow workflow{standard_workflow::metallic};
    /// The base color, the diffuse color in the specular workflow. Its alpha is what a masked material clips.
    math::color base_color{1.0f, 1.0f, 1.0f, 1.0f};
    /// Its alpha is the subsurface opacity.
    math::color subsurface_color{0.0f, 0.0f, 0.0f, 0.8f};
    math::color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
    float emissive_intensity{1.0f};
    float roughness{0.3f};
    float metallic{0.0f};
    /// The specular workflow's specular color, times the specular map's rgb.
    math::color specular_color{1.0f, 1.0f, 1.0f, 1.0f};
    /// The specular workflow's glossiness, times the specular map's alpha.
    float glossiness{1.0f};
    float bumpiness{1.0f};
    alpha_mode alpha{alpha_mode::opaque};
    float alpha_cutoff{0.5f};
    math::vec2 tiling{1.0f, 1.0f};
    /// Within this distance of the camera the surface dithers out; 0: it does not.
    float camera_fade_distance{0.0f};
    cull_type cull{cull_type::counter_clockwise};
    /// The surface shows its color without lighting (Shading Model Unlit): the base color, tinted by the color map, is
    /// its Emissive.
    bool unlit{false};
    /// The index of refraction of a see-through surface (the Surface Output's Refraction): 1 bends nothing.
    float refraction{1.0f};
    /// The base color and its alpha are multiplied by the mesh's vertex colour (a Vertex Color node): what a glTF
    /// primitive with vertex colours means. A mesh without colours reads white, so nothing changes on it.
    bool multiply_vertex_color{false};
    asset_handle<gfx::texture> color_map;
    asset_handle<gfx::texture> normal_map;
    /// Roughness in red, or in green when it is the metallic map too (one map packing both, glTF 2.0).
    asset_handle<gfx::texture> roughness_map;
    /// Metallic in red, or in blue when it is the roughness map too.
    asset_handle<gfx::texture> metallic_map;
    asset_handle<gfx::texture> emissive_map;
    asset_handle<gfx::texture> ao_map;
    /// The specular workflow's map: specular color in rgb, glossiness in alpha.
    asset_handle<gfx::texture> specular_map;
};

/**
 * @brief A property of the Standard material by the name a PBR material, a script or a tool gave it ("base_color",
 * "color_map"), and where the Standard graph keeps it.
 */
struct standard_property
{
    enum class kind : uint8_t
    {
        /// A color picker value (sRGB); the parameter holds it in linear space.
        color,
        scalar,
        /// Two numbers in the x and y of a vector parameter (the tiling).
        vector2,
        texture,
    };

    /// The PBR material's property name, the key a material instance, a script and a tool use.
    const char* key{};
    /// The Standard graph's parameter or texture node name.
    const char* name{};
    kind type{kind::scalar};
};

/// Every property the Standard graph keeps, in the order the PBR inspector showed them.
auto get_standard_properties() -> const std::vector<standard_property>&;

/// The Standard property named @p key ("base_color"), or null.
auto find_standard_property(hpp::string_view key) -> const standard_property*;

/// The key a PBR material saved Standard property @p property under: its own key, but "metalness" and
/// "metalness_map" for Metallic and its map.
auto get_legacy_pbr_key(const standard_property& property) -> const char*;

/// The Standard property a PBR material's key names (get_legacy_pbr_key's inverse); null for none.
auto find_legacy_pbr_property(hpp::string_view key) -> const standard_property*;

/**
 * @brief The value the Standard graph's parameter of @p property holds for @p value, the property as a PBR material, a
 * script or a tool gives it: a color (sRGB) in linear space, a number in x, the tiling's two numbers in x and y.
 */
auto make_standard_parameter_value(const standard_property& property, const math::vec4& value) -> math::vec4;

/// The inverse of make_standard_parameter_value: @p parameter as the property's value.
auto make_standard_property_value(const standard_property& property, const math::vec4& parameter) -> math::vec4;

/// Where a Standard property's value lives in a graph material (resolve_standard_property).
struct standard_property_target
{
    /// The property whose parameter or texture holds the value; null when the graph has none.
    const standard_property* property{};
    /// The held value is 1 - the asked one: Roughness held as Glossiness.
    bool is_inverted{};
};

class graph_material;

/**
 * @brief Where @p property's value lives in @p mat: its own parameter or texture when the graph has one. Else Roughness
 * lives in Glossiness as 1 - roughness, on a graph with Glossiness and no Roughness (the specular workflow), so what
 * sets a metallic material's roughness sets a specular one's glossiness. Null for any other property the graph lacks.
 */
auto resolve_standard_property(const graph_material& mat, const standard_property& property) -> standard_property_target;

/// @p value as @p target holds it, and back: 1 - value when the target is inverted, @p value otherwise.
auto apply_standard_inversion(const standard_property_target& target, const math::vec4& value) -> math::vec4;

/**
 * @brief The Standard material's graph for @p surface: the surface every material of the engine started from,
 * written as nodes. Every map is a named texture node read at the tiled UV, set or not (an empty one reads the
 * default texture); every value is a named parameter with the PBR inspector's name, slider and hint. The values a
 * material instance overrides per entity take the first registers, so overrides keep the shared draw. One map
 * packing roughness and metallic is a Static Switch Parameter, set as the maps are.
 */
auto make_standard_material_graph(const standard_surface& surface) -> material_graph;

/// A graph material made of make_standard_material_graph(@p surface): what the importer, a new material and a legacy
/// PBR material become.
auto make_standard_material(const standard_surface& surface) -> std::shared_ptr<graph_material>;

/**
 * @brief The base color, metallic and roughness the specular workflow's @p diffuse, @p specular (sRGB-encoded, as
 * the solve is calibrated for) and @p glossiness draw as: the C++ twin of material_graph.sh's mg_specular_to_metallic,
 * and the solve the importer baked textures with.
 */
struct metallic_surface
{
    math::vec3 base_color{};
    float metallic{};
    float roughness{};
};
auto convert_specular_to_metallic(const math::vec3& diffuse, const math::vec3& specular, float glossiness) -> metallic_surface;

/**
 * @brief A material file written before every material was a graph holds a PBR material: it loads as this, and
 * upgrade_legacy_material turns it into the Standard graph material. Never drawn.
 */
class legacy_pbr_material final : public crtp_meta_type<legacy_pbr_material, material>
{
public:
    SERIALIZABLE(legacy_pbr_material)

    standard_surface surface;
};

/// @p mat as the engine draws it: a legacy PBR material becomes the Standard graph material; any other comes back.
auto upgrade_legacy_material(std::shared_ptr<material> mat) -> std::shared_ptr<material>;

} // namespace unravel
