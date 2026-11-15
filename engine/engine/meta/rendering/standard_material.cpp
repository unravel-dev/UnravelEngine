#include "standard_material.hpp"
#include "material.hpp"

#include <engine/meta/assets/asset_handle.hpp>
#include <engine/meta/core/math/vector.hpp>

#include <serialization/types/string.hpp>
#include <serialization/types/unordered_map.hpp>
#include <serialization/types/utility.hpp>

#include <cmath>

namespace unravel
{

namespace
{
/// An alpha cutoff a file wrote for an opaque material: the dither alpha its shader used, not a cutoff.
constexpr float LEGACY_OPAQUE_CUTOFF_SENTINEL = 0.25f;
constexpr float LEGACY_CUTOFF_TOLERANCE = 0.001f;

/// A masked material's cutoff of 0 clipped nothing; the PBR material read it as the default.
constexpr float DEFAULT_MASK_CUTOFF = 0.5f;

auto is_legacy_opaque_sentinel(float value) -> bool
{
    return std::abs(value - LEGACY_OPAQUE_CUTOFF_SENTINEL) <= LEGACY_CUTOFF_TOLERANCE;
}

/// Files written before the alpha mode was saved said "masked" by a cutoff other than the opaque sentinel.
auto infer_legacy_alpha_mode(float alpha_cutoff, float surface_alpha) -> alpha_mode
{
    return is_legacy_opaque_sentinel(alpha_cutoff) && is_legacy_opaque_sentinel(surface_alpha) ? alpha_mode::opaque
                                                                                                : alpha_mode::mask;
}

/// The cutoff as the PBR material's setter kept it: within 0 to 1, and never 0 while masked.
auto normalize_legacy_cutoff(const standard_surface& surface) -> float
{
    const float cutoff = math::clamp(surface.alpha_cutoff, 0.0f, 1.0f);
    return surface.alpha == alpha_mode::mask && cutoff <= 0.0f ? DEFAULT_MASK_CUTOFF : cutoff;
}

/// The values a text file names, in each spelling files used: older ones packed roughness, metalness, bumpiness and
/// the alpha test into one vector, and left the alpha mode to the cutoff.
template<typename Archive>
void load_legacy_text_surface(Archive& ar, standard_surface& surface)
{
    const bool has_alpha_mode = try_load(ar, ser20::make_nvp("alpha_mode", surface.alpha));
    const bool has_alpha_cutoff = try_load(ar, ser20::make_nvp("alpha_cutoff", surface.alpha_cutoff));
    math::vec4 surface_data(surface.roughness, surface.metallic, surface.bumpiness, LEGACY_OPAQUE_CUTOFF_SENTINEL);
    if(try_load(ar, ser20::make_nvp("surface_data", surface_data)))
    {
        surface.roughness = surface_data.x;
        surface.metallic = surface_data.y;
        surface.bumpiness = surface_data.z;
        surface.alpha_cutoff = has_alpha_cutoff ? surface.alpha_cutoff : surface_data.w;
    }
    else
    {
        try_load(ar, ser20::make_nvp("roughness", surface.roughness));
        try_load(ar, ser20::make_nvp("metalness", surface.metallic));
        try_load(ar, ser20::make_nvp("bumpiness", surface.bumpiness));
    }
    if(!has_alpha_cutoff)
    {
        try_load(ar, ser20::make_nvp("alpha_test_value", surface.alpha_cutoff));
    }
    if(!has_alpha_mode)
    {
        surface.alpha = infer_legacy_alpha_mode(surface.alpha_cutoff, surface_data.w);
        if(surface.alpha == alpha_mode::mask && is_legacy_opaque_sentinel(surface.alpha_cutoff))
        {
            surface.alpha_cutoff = surface_data.w;
        }
    }
}
} // namespace

REFLECT(legacy_pbr_material)
{
    entt::meta_factory<legacy_pbr_material>{}
        .type("legacy_pbr_material"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "legacy_pbr_material"},
            entt::attribute{"pretty_name", "PBR Material"},
        })
        .base<material>()
        .func<&legacy_pbr_material::get_meta_type>("get_meta_type"_hs)
        .func<&legacy_pbr_material::get_static_meta_type>("get_static_meta_type"_hs)
        .func<&legacy_pbr_material::as_derived>("as_derived"_hs);
}

SAVE(legacy_pbr_material)
{
    const auto& surface = obj.surface;
    const math::vec2 dither_threshold(0.5f, surface.camera_fade_distance);
    try_save(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    try_save(ar, ser20::make_nvp("base_color", surface.base_color));
    try_save(ar, ser20::make_nvp("subsurface_color", surface.subsurface_color));
    try_save(ar, ser20::make_nvp("emissive_color", surface.emissive_color));
    try_save(ar, ser20::make_nvp("emissive_intensity", surface.emissive_intensity));
    try_save(ar, ser20::make_nvp("roughness", surface.roughness));
    try_save(ar, ser20::make_nvp("metalness", surface.metallic));
    try_save(ar, ser20::make_nvp("bumpiness", surface.bumpiness));
    try_save(ar, ser20::make_nvp("alpha_mode", surface.alpha));
    try_save(ar, ser20::make_nvp("alpha_cutoff", surface.alpha_cutoff));
    try_save(ar, ser20::make_nvp("tiling", surface.tiling));
    try_save(ar, ser20::make_nvp("dither_threshold", dither_threshold));
    try_save(ar, ser20::make_nvp("color_map", surface.color_map));
    try_save(ar, ser20::make_nvp("normal_map", surface.normal_map));
    try_save(ar, ser20::make_nvp("roughness_map", surface.roughness_map));
    try_save(ar, ser20::make_nvp("metalness_map", surface.metallic_map));
    try_save(ar, ser20::make_nvp("emissive_map", surface.emissive_map));
    try_save(ar, ser20::make_nvp("ao_map", surface.ao_map));
}
SAVE_INSTANTIATE(legacy_pbr_material, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(legacy_pbr_material, ser20::oarchive_binary_t);

LOAD(legacy_pbr_material)
{
    auto& surface = obj.surface;
    try_load(ar, ser20::make_nvp("base_type", ser20::base_class<material>(&obj)));
    surface.cull = obj.get_cull_type();
    try_load(ar, ser20::make_nvp("base_color", surface.base_color));
    try_load(ar, ser20::make_nvp("subsurface_color", surface.subsurface_color));
    try_load(ar, ser20::make_nvp("emissive_color", surface.emissive_color));
    try_load(ar, ser20::make_nvp("emissive_intensity", surface.emissive_intensity));
    if constexpr(is_binary_archive<Archive>())
    {
        // A binary file is read in the order it was written.
        try_load(ar, ser20::make_nvp("roughness", surface.roughness));
        try_load(ar, ser20::make_nvp("metalness", surface.metallic));
        try_load(ar, ser20::make_nvp("bumpiness", surface.bumpiness));
        try_load(ar, ser20::make_nvp("alpha_mode", surface.alpha));
        try_load(ar, ser20::make_nvp("alpha_cutoff", surface.alpha_cutoff));
    }
    else
    {
        load_legacy_text_surface(ar, surface);
    }
    math::vec2 dither_threshold(0.5f, surface.camera_fade_distance);
    try_load(ar, ser20::make_nvp("tiling", surface.tiling));
    try_load(ar, ser20::make_nvp("dither_threshold", dither_threshold));
    surface.camera_fade_distance = dither_threshold.y;
    try_load(ar, ser20::make_nvp("color_map", surface.color_map));
    try_load(ar, ser20::make_nvp("normal_map", surface.normal_map));
    try_load(ar, ser20::make_nvp("roughness_map", surface.roughness_map));
    try_load(ar, ser20::make_nvp("metalness_map", surface.metallic_map));
    try_load(ar, ser20::make_nvp("emissive_map", surface.emissive_map));
    try_load(ar, ser20::make_nvp("ao_map", surface.ao_map));
    surface.alpha_cutoff = normalize_legacy_cutoff(surface);
}
LOAD_INSTANTIATE(legacy_pbr_material, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(legacy_pbr_material, ser20::iarchive_binary_t);

} // namespace unravel
