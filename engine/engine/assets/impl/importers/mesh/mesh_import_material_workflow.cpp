#include "mesh_import_material_workflow.h"

#include "mesh_import_paths.h"

#include <logging/logging.h>
#include <math/math.h>
#include <string_utils/utils.h>

#include <assimp/GltfMaterial.h>

#include <array>
#include <cmath>
#include <string_view>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

auto phong_shininess_exponent_to_roughness(float shininess) -> float
{
    return math::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
}

/**
 * @brief True when the diffuse path is already the material's authored base color (PBR), not
 * KHR spec-gloss extension albedo that must be reconstructed with the specular map.
 *
 * Uses Assimp texture slots only - no filename heuristics:
 *   - BASE_COLOR alone -> dedicated PBR base color (e.g. Vespa BaseColor.dds).
 *   - BASE_COLOR and DIFFUSE bound to the same path -> MR base color is the albedo source.
 *   - BASE_COLOR and DIFFUSE differ -> dual-authored glTF (MR fallback vs KHR SG diffuse).
 */
auto base_color_texture_is_authoritative(const aiMaterial* material) -> bool
{
    aiString base_path{};
    aiString diffuse_path{};
    const bool has_base =
        material->GetTexture(aiTextureType_BASE_COLOR, 0, &base_path) == AI_SUCCESS && base_path.length > 0;
    const bool has_diffuse =
        material->GetTexture(aiTextureType_DIFFUSE, 0, &diffuse_path) == AI_SUCCESS && diffuse_path.length > 0;

    if(has_base && has_diffuse)
    {
        return material_texture_paths_equal(normalize_assimp_path(base_path.C_Str()),
                                            normalize_assimp_path(diffuse_path.C_Str()));
    }

    return has_base;
}

auto material_shading_is_phong_family(aiShadingMode shading) -> bool
{
    switch(shading)
    {
    case aiShadingMode_Phong:
    case aiShadingMode_Blinn:
    case aiShadingMode_Minnaert:
    case aiShadingMode_Gouraud:
    case aiShadingMode_Flat:
        return true;
    default:
        return false;
    }
}

/**
 * @brief AI_MATKEY_SHADING_MODEL of @p material. Read as int: FBX and glTF store an aiShadingMode
 * buffer, most other importers (OBJ, Collada, 3DS, X, ...) an int, and Get<aiShadingMode> accepts
 * only the buffer while the int getter accepts both.
 */
auto get_shading_model(const aiMaterial* material, aiShadingMode& shading) -> bool
{
    int value = 0;
    if(material->Get(AI_MATKEY_SHADING_MODEL, value) != AI_SUCCESS)
    {
        return false;
    }
    shading = static_cast<aiShadingMode>(value);
    return true;
}

auto material_has_pbr_brdf_shading(const aiMaterial* material) -> bool
{
    aiShadingMode shading = aiShadingMode_Flat;
    return get_shading_model(material, shading) && shading == aiShadingMode_PBR_BRDF;
}

/**
 * @brief glTF writes its unlit materials (KHR_materials_unlit) with metallic/roughness factors and an
 * Unlit shading model - and a shininess derived from roughness that would otherwise read as Phong.
 */
auto material_has_unlit_shading(const aiMaterial* material) -> bool
{
    aiShadingMode shading = aiShadingMode_Flat;
    return get_shading_model(material, shading) && shading == aiShadingMode_Unlit;
}

/// Assimp's glTF importer gives every material an alphaMode; no other importer writes the key.
auto is_gltf_material(const aiMaterial* material) -> bool
{
    aiString alpha_mode;
    return material->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == AI_SUCCESS;
}

/**
 * @brief 3ds Max PBR materials (FBX) with useGlossiness == 1 author glossiness, and Assimp parks those
 * maps in aiTextureType_SHININESS. useGlossiness == 2 puts a true roughness map in DIFFUSE_ROUGHNESS.
 */
auto shininess_slot_holds_glossiness(const aiMaterial* material) -> bool
{
    int use_glossiness = 0;
    return material->Get("$raw.3dsMax|main|useGlossiness", aiTextureType_NONE, 0, use_glossiness) == AI_SUCCESS
           && use_glossiness == 1;
}

/**
 * @brief Assimp only writes AI_MATKEY_GLOSSINESS_FACTOR for glTF KHR_materials_pbrSpecularGlossiness.
 */
auto material_has_glossiness_factor(const aiMaterial* material) -> bool
{
    ai_real glossiness = 0.0f;
    return material->Get(AI_MATKEY_GLOSSINESS_FACTOR, glossiness) == AI_SUCCESS;
}

auto string_ends_with(std::string_view value, std::string_view suffix) -> bool
{
    return value.size() >= suffix.size()
           && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * @brief Bistro FBX export: paired *BaseColor* + *Specular* DDS paths (not the *_spec* naming).
 */
auto lumberyard_bistro_basecolor_specular_pair(const aiMaterial* material) -> bool
{
    if(material == nullptr)
    {
        return false;
    }

    aiString diffuse_path{};
    aiString specular_path{};
    if(material->GetTexture(aiTextureType_DIFFUSE, 0, &diffuse_path) != AI_SUCCESS || diffuse_path.length == 0)
    {
        return false;
    }
    if(material->GetTexture(aiTextureType_SPECULAR, 0, &specular_path) != AI_SUCCESS || specular_path.length == 0)
    {
        return false;
    }

    const std::string diffuse_stem =
        string_utils::to_lower(normalize_assimp_path(diffuse_path.C_Str()).stem().string());
    const std::string specular_stem =
        string_utils::to_lower(normalize_assimp_path(specular_path.C_Str()).stem().string());

    if(!string_ends_with(diffuse_stem, "_basecolor") || !string_ends_with(specular_stem, "_specular"))
    {
        return false;
    }

    constexpr std::string_view k_basecolor_suffix = "_basecolor";
    constexpr std::string_view k_specular_suffix = "_specular";
    const std::string diffuse_prefix =
        diffuse_stem.substr(0, diffuse_stem.size() - k_basecolor_suffix.size());
    const std::string specular_prefix =
        specular_stem.substr(0, specular_stem.size() - k_specular_suffix.size());
    return diffuse_prefix == specular_prefix;
}

/**
 * @brief *_spec* maps and Bistro *_Specular pairs: combined MR+AO is stored in SPECULAR
 * (glTF layout: R=occlusion, G=roughness, B=metallic).
 */
auto specular_texture_path_looks_like_packed_mr(const aiMaterial* material) -> bool
{
    if(material == nullptr)
    {
        return false;
    }

    if(lumberyard_bistro_basecolor_specular_pair(material))
    {
        return true;
    }

    aiString path{};
    if(material->GetTexture(aiTextureType_SPECULAR, 0, &path) != AI_SUCCESS || path.length == 0)
    {
        return false;
    }

    const std::string stem = string_utils::to_lower(normalize_assimp_path(path.C_Str()).stem().string());
    if(string_ends_with(stem, "_spec"))
    {
        return true;
    }
    return stem.find("_spec_") != std::string::npos;
}

/**
 * @brief True when the material has dedicated PBR metallic/roughness texture slots.
 * Factor-only evidence is handled separately (requires PBR_BRDF, no glossiness factor).
 */
auto has_metallic_roughness_texture_evidence(const aiMaterial* material) -> bool
{
    static constexpr std::array<aiTextureType, 4> mr_slots = {
        aiTextureType_METALNESS,
        aiTextureType_GLTF_METALLIC_ROUGHNESS,
        aiTextureType_DIFFUSE_ROUGHNESS,
        aiTextureType_MAYA_SPECULAR_ROUGHNESS,
    };
    for(const aiTextureType slot : mr_slots)
    {
        if(material->GetTextureCount(slot) > 0)
        {
            return true;
        }
    }

    // FBX / legacy exporters sometimes park combined MR maps in UNKNOWN (see Assimp #5969).
    if(material->GetTextureCount(aiTextureType_UNKNOWN) > 0)
    {
        ai_real metallic_dummy = 0.0f;
        if(material_has_pbr_brdf_shading(material)
           || material->Get(AI_MATKEY_METALLIC_FACTOR, metallic_dummy) == AI_SUCCESS)
        {
            return true;
        }
    }

    if(material_has_packed_mr_in_specular_slot(material))
    {
        return true;
    }

    return false;
}

/**
 * @brief PBR_BRDF material with MR factors and no KHR glossiness factor (per Assimp material.h guidance).
 */
auto has_native_mr_factor_evidence(const aiMaterial* material) -> bool
{
    if(material_has_glossiness_factor(material))
    {
        return false;
    }
    if(!material_has_pbr_brdf_shading(material) && !material_has_unlit_shading(material))
    {
        return false;
    }
    ai_real dummy = 0.0f;
    return material->Get(AI_MATKEY_METALLIC_FACTOR, dummy) == AI_SUCCESS
           || material->Get(AI_MATKEY_ROUGHNESS_FACTOR, dummy) == AI_SUCCESS;
}

/**
 * @brief Legacy Phong/Blinn material signals from Assimp (shininess exponent, not KHR glossiness).
 */
auto is_phong_legacy_material(const aiMaterial* material) -> bool
{
    if(material_has_glossiness_factor(material))
    {
        return false;
    }
    if(has_metallic_roughness_texture_evidence(material))
    {
        return false;
    }
    if(has_native_mr_factor_evidence(material))
    {
        return false;
    }

    aiShadingMode shading = aiShadingMode_Flat;
    if(get_shading_model(material, shading) && material_shading_is_phong_family(shading))
    {
        return true;
    }

    ai_real shininess = 0.0f;
    if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
    {
        return true;
    }

    if(material->GetTextureCount(aiTextureType_SHININESS) > 0)
    {
        return true;
    }

    if(!material_has_pbr_brdf_shading(material) && material->GetTextureCount(aiTextureType_SPECULAR) > 0)
    {
        return true;
    }

    return false;
}

constexpr float k_import_opaque_opacity_threshold = 0.999f;

} // namespace

auto material_workflow_label(material_workflow workflow) -> const char*
{
    switch(workflow)
    {
    case material_workflow::metallic_roughness:
        return "Metallic/Roughness";
    case material_workflow::khr_specular_glossiness:
        return "KHR Specular/Glossiness";
    case material_workflow::phong_specular_gloss:
        return "Phong Specular/Gloss";
    default:
        return "Unknown";
    }
}

/**
 * @brief KHR rebakes extension diffuse into PBR base color. Phong keeps diffuse as albedo.
 */
auto should_reconstruct_base_color_for_spec_gloss_pair(material_workflow workflow,
                                                       const aiMaterial* material) -> bool
{
    return workflow == material_workflow::khr_specular_glossiness
           && !base_color_texture_is_authoritative(material);
}

/**
 * @brief Packed metallic-roughness stored in aiTextureType_SPECULAR (Bistro pairs / *_spec maps).
 * Not KHR spec/gloss and not Phong specular-color maps.
 */
auto material_has_packed_mr_in_specular_slot(const aiMaterial* material) -> bool
{
    // glTF SPECULAR holds KHR_materials_specular strength/color maps, never packed metal-rough.
    if(material == nullptr || material_has_glossiness_factor(material) || is_gltf_material(material))
    {
        return false;
    }
    if(material->GetTextureCount(aiTextureType_SPECULAR) == 0)
    {
        return false;
    }
    if(material->GetTextureCount(aiTextureType_SHININESS) > 0)
    {
        return false;
    }

    const bool has_albedo_texture = material->GetTextureCount(aiTextureType_DIFFUSE) > 0
                                    || material->GetTextureCount(aiTextureType_BASE_COLOR) > 0;
    if(!has_albedo_texture)
    {
        return false;
    }

    // Path naming is the reliable packed-map signal. Do not require COLOR_SPECULAR ~ white:
    // Assimp FBX keeps Phong defaults (often ~0.2-0.5) even though the engine multiplies by white.
    return specular_texture_path_looks_like_packed_mr(material);
}

/**
 * @brief KHR specular-only fallback: convert one specular map to combined MR (no diffuse pair).
 */
auto khr_needs_combined_specular_mr(const aiMaterial* material, material_workflow workflow) -> bool
{
    if(workflow != material_workflow::khr_specular_glossiness)
    {
        return false;
    }

    if(material->GetTextureCount(aiTextureType_METALNESS) > 0
       || material->GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0
       || material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0)
    {
        return false;
    }

    if(material->GetTextureCount(aiTextureType_SPECULAR) > 0)
    {
        APPLOG_TRACE("Mesh Importer: KHR specular-only -> combined SpecularToMetallicRoughness conversion");
        return true;
    }

    return false;
}

/**
 * @brief Classify material input workflow before converting to engine metallic-roughness.
 *
 * Order: KHR (glossiness factor) -> native MR (textures / PBR factors) -> Phong legacy -> unknown.
 */
auto detect_material_workflow(const aiMaterial* material) -> material_workflow
{
    if(material_has_glossiness_factor(material))
    {
        APPLOG_TRACE("Mesh Importer: Workflow=KHR (AI_MATKEY_GLOSSINESS_FACTOR present)");
        return material_workflow::khr_specular_glossiness;
    }

    if(has_metallic_roughness_texture_evidence(material))
    {
        if(material_has_packed_mr_in_specular_slot(material))
        {
            APPLOG_TRACE("Mesh Importer: Workflow=MR (packed metallic-roughness in aiTextureType_SPECULAR)");
        }
        else
        {
            APPLOG_TRACE("Mesh Importer: Workflow=MR (dedicated metallic/roughness texture slots)");
        }
        return material_workflow::metallic_roughness;
    }

    if(has_native_mr_factor_evidence(material))
    {
        APPLOG_TRACE("Mesh Importer: Workflow=MR (PBR_BRDF + metallic/roughness factors, no glossiness)");
        return material_workflow::metallic_roughness;
    }

    if(is_phong_legacy_material(material))
    {
        APPLOG_TRACE("Mesh Importer: Workflow=Phong (shininess/specular legacy signals)");
        return material_workflow::phong_specular_gloss;
    }

    APPLOG_TRACE("Mesh Importer: Workflow=Unknown (no KHR/MR/Phong signals)");
    return material_workflow::unknown;
}

auto gather_spec_gloss_factors(const aiMaterial* material) -> spec_gloss_factors_t
{
    spec_gloss_factors_t factors{};
    if(!material)
    {
        return factors;
    }

    aiColor4D diffuse_factor_color{1.0f, 1.0f, 1.0f, 1.0f};
    if(material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_factor_color) == AI_SUCCESS)
    {
        factors.diffuse_r = diffuse_factor_color.r;
        factors.diffuse_g = diffuse_factor_color.g;
        factors.diffuse_b = diffuse_factor_color.b;
        factors.diffuse_a = diffuse_factor_color.a;
    }

    aiColor3D specular_factor_color{1.0f, 1.0f, 1.0f};
    material->Get(AI_MATKEY_COLOR_SPECULAR, specular_factor_color);
    float specular_factor_scalar = 1.0f;
    material->Get(AI_MATKEY_SPECULAR_FACTOR, specular_factor_scalar);
    factors.specular_r = specular_factor_color.r * specular_factor_scalar;
    factors.specular_g = specular_factor_color.g * specular_factor_scalar;
    factors.specular_b = specular_factor_color.b * specular_factor_scalar;

    factors.glossiness = 1.0f;
    if(material->Get(AI_MATKEY_GLOSSINESS_FACTOR, factors.glossiness) != AI_SUCCESS)
    {
        float shininess = 32.0f;
        if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
        {
            factors.glossiness = math::clamp(1.0f - std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
        }
    }

    return factors;
}

/**
 * @brief Process material with intelligent property extraction and conversion.
 * First tries to get actual PBR properties, then converts missing ones from available data.
 * KHR spec/gloss factors go through the metallic solve; Phong uses shininess exponent mapping.
 */
void process_material_with_workflow_conversion(const aiMaterial* material, 
                                             material_workflow workflow,
                                             aiColor3D& base_color,
                                             float& metallic,
                                             float& roughness)
{
    bool has_base_color = (material->Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS);
    bool has_metallic = (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS);
    bool has_roughness = (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS);
    
    // KHR spec-gloss: derive MR (+ optional base color) from diffuse/specular/gloss factors when textures are absent.
    if(workflow == material_workflow::khr_specular_glossiness)
    {
        aiColor3D diffuse_color{1.0f, 1.0f, 1.0f};
        material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_color);
        
        aiColor3D specular_color{0.04f, 0.04f, 0.04f};
        float specular_factor = 1.0f;
        material->Get(AI_MATKEY_COLOR_SPECULAR, specular_color);
        material->Get(AI_MATKEY_SPECULAR_FACTOR, specular_factor);
        specular_color.r *= specular_factor;
        specular_color.g *= specular_factor;
        specular_color.b *= specular_factor;
        
        float glossiness = 0.5f;
        if(material->Get(AI_MATKEY_GLOSSINESS_FACTOR, glossiness) != AI_SUCCESS)
        {
            float shininess = 32.0f;
            if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            {
                glossiness = math::clamp(1.0f - std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
            }
        }
        
        auto [converted_base_color, converted_metallic, converted_roughness] = 
            convert_specular_gloss_to_metallic_roughness(diffuse_color, specular_color, glossiness);
        
        if(!has_base_color)
        {
            base_color = converted_base_color;
            APPLOG_TRACE("Mesh Importer: Converted base color from specular/diffuse workflow");
        }
        metallic = converted_metallic;
        roughness = converted_roughness;
        APPLOG_TRACE("Mesh Importer: Converted PBR factors from KHR spec/gloss: metallic={:.3f}, roughness={:.3f}",
                     metallic, roughness);
    }
    else if(workflow == material_workflow::phong_specular_gloss)
    {
        // Albedo tint is COLOR_DIFFUSE; BASE_COLOR is often a white MR fallback on dual-authored assets.
        aiColor3D diffuse_tint{1.0f, 1.0f, 1.0f};
        if(material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_tint) != AI_SUCCESS)
        {
            diffuse_tint = aiColor3D{1.0f, 1.0f, 1.0f};
        }
        base_color = diffuse_tint;

        metallic = 0.0f;
        float shininess = 32.0f;
        if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
        {
            roughness = phong_shininess_exponent_to_roughness(shininess);
        }
        else
        {
            roughness = 0.5f;
        }
        APPLOG_TRACE("Mesh Importer: Phong -> MR factors: metallic={:.3f}, roughness={:.3f}",
                     metallic, roughness);
    }
    else if(workflow == material_workflow::metallic_roughness)
    {
        if(!has_base_color)
        {
            if(material->Get(AI_MATKEY_COLOR_DIFFUSE, base_color) != AI_SUCCESS)
            {
                base_color = aiColor3D{1.0f, 1.0f, 1.0f};
            }
        }

        if(!has_metallic)
        {
            metallic = 0.0f;
        }

        if(!has_roughness)
        {
            float shininess = 32.0f;
            if(material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
            {
                roughness = std::sqrt(2.0f / (shininess + 2.0f));
            }
            else
            {
                roughness = 0.5f;
            }
        }
    }
    else
    {
        if(!has_base_color)
        {
            if(material->Get(AI_MATKEY_COLOR_DIFFUSE, base_color) != AI_SUCCESS)
            {
                base_color = aiColor3D{1.0f, 1.0f, 1.0f};
            }
        }

        metallic = 0.0f;
        roughness = 0.5f;
    }
    
    APPLOG_TRACE("Mesh Importer: Final PBR values - BaseColor: ({:.3f}, {:.3f}, {:.3f}), "
                "Metallic: {:.3f}, Roughness: {:.3f} [{}{}{}]",
                base_color.r, base_color.g, base_color.b, metallic, roughness,
                has_base_color ? "B" : "b",
                has_metallic ? "M" : "m", 
                has_roughness ? "R" : "r");
}

auto is_material_two_sided(const aiMaterial* material) -> bool
{
    if(!material)
    {
        return false;
    }

    // Assimp stores TWOSIDED as bool/int depending on the importer; the bool
    // Get() specialization handles all of those. ai_real does not.
    bool two_sided = false;
    if(material->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS && two_sided)
    {
        return true;
    }

    // FBX and some other exports encode double-sided in the material name but
    // never set AI_MATKEY_TWOSIDED - Assimp's FBX converter ignores Model::Culling.
    aiString mat_name{};
    if(material->Get(AI_MATKEY_NAME, mat_name) == AI_SUCCESS && mat_name.length > 0)
    {
        const std::string name = string_utils::to_lower(mat_name.C_Str());
        static constexpr std::array<const char*, 6> markers = {
            "doublesided",
            "double-sided",
            "double sided",
            "twosided",
            "two-sided",
            "two sided",
        };
        for(const char* marker : markers)
        {
            if(name.find(marker) != std::string::npos)
            {
                return true;
            }
        }
    }

    return false;
}

auto material_opacity_factor_suggests_cutout(const aiMaterial* material, ai_real& out_opacity) -> bool
{
    out_opacity = 1.0f;
    return material
           && material->Get(AI_MATKEY_OPACITY, out_opacity) == AI_SUCCESS
           && out_opacity < k_import_opaque_opacity_threshold;
}

auto resolve_import_alpha_cutoff(const aiMaterial* material, ai_real fallback) -> ai_real
{
    ai_real cutoff = fallback;
    if(material && material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS && cutoff > 0.0f)
    {
        return math::clamp(cutoff, 0.0f, 1.0f);
    }
    return fallback;
}

auto get_texture_slot_candidates(const aiMaterial* material, material_workflow workflow, texture_target target)
    -> std::vector<texture_slot_candidate>
{
    const bool is_khr = workflow == material_workflow::khr_specular_glossiness;
    const bool is_phong = workflow == material_workflow::phong_specular_gloss;
    std::vector<texture_slot_candidate> candidates;
    if(target == texture_target::base_color)
    {
        // KHR and Phong: extension/legacy albedo lives in DIFFUSE; BASE_COLOR is often MR fallback.
        if(!is_khr && !is_phong)
        {
            candidates.push_back({aiTextureType_BASE_COLOR, 0, "BaseColor"});
        }
        candidates.push_back({aiTextureType_DIFFUSE, 0, "BaseColor"});
        return candidates;
    }
    // Dual-authored KHR glTF carries MR fallback textures - ignore them when glossiness is present.
    if(is_khr)
    {
        return candidates;
    }
    const bool is_metallic = target == texture_target::metallic;
    const bool is_glossiness_in_shininess = shininess_slot_holds_glossiness(material);
    candidates.push_back({aiTextureType_GLTF_METALLIC_ROUGHNESS, 0, "MetallicRoughness"});
    candidates.push_back(is_metallic ? texture_slot_candidate{aiTextureType_METALNESS, 0, "Metallic"}
                                     : texture_slot_candidate{aiTextureType_DIFFUSE_ROUGHNESS, 0, "Roughness"});
    if(!is_metallic && is_glossiness_in_shininess)
    {
        candidates.push_back({aiTextureType_SHININESS, 0, "GlossinessToRoughness"});
    }
    // FBX exporters often emit combined MR under aiTextureType_UNKNOWN when canonical slots are
    // empty. Allow recovery for native MR and unknown workflows, not KHR/Phong.
    if(!is_phong)
    {
        candidates.push_back({aiTextureType_UNKNOWN,
                              0,
                              "MetallicRoughness",
                              "Recovering metallic-roughness texture from aiTextureType_UNKNOWN slot"});
    }
    if(material_has_packed_mr_in_specular_slot(material))
    {
        candidates.push_back({aiTextureType_SPECULAR,
                              0,
                              "MetallicRoughness",
                              "Using packed metallic-roughness from aiTextureType_SPECULAR"});
    }
    if(!is_metallic && is_phong && !is_glossiness_in_shininess)
    {
        candidates.push_back({aiTextureType_SHININESS, 0, "ShininessToRoughness"});
    }
    return candidates;
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
