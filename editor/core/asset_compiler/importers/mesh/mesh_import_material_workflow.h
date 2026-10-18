#pragma once

#include "mesh_import_texture_conversion.h"

#include <assimp/material.h>

#include <vector>

namespace unravel
{
namespace importer
{
namespace mesh_import
{

/// Material input workflows (all compile to engine metallic-roughness + base color).
enum class material_workflow
{
    unknown,
    metallic_roughness,      ///< glTF/FBX PBR MR: base color + metallic/roughness maps/factors
    khr_specular_glossiness, ///< KHR_materials_pbrSpecularGlossiness: bake diffuse+spec textures
    phong_specular_gloss,    ///< Legacy Phong/Blinn: diffuse pass-through, shininess -> roughness
};

/// Engine material slot a texture is looked up for.
enum class texture_target
{
    base_color,
    metallic,
    roughness,
};

/**
 * @brief One Assimp texture slot to try for a texture_target, in priority order.
 */
struct texture_slot_candidate
{
    aiTextureType type{aiTextureType_NONE};
    unsigned int index{0};
    /// Semantic the imported texture is tagged with; selects its conversion.
    const char* semantic{""};
    /// Trace line logged when this candidate is the one used (nullptr: none).
    const char* trace_on_use{nullptr};
};

auto material_workflow_label(material_workflow workflow) -> const char*;

/**
 * @brief Classify material input workflow before converting to engine metallic-roughness.
 *
 * Order: KHR (glossiness factor) -> native MR (textures / PBR factors) -> Phong legacy -> unknown.
 */
auto detect_material_workflow(const aiMaterial* material) -> material_workflow;

/**
 * @brief Texture slots to try, first hit wins, for @p target under @p workflow.
 *
 * KHR and Phong read albedo from DIFFUSE (BASE_COLOR is the MR fallback there); KHR ignores the MR
 * fallback maps entirely; packed MR (Bistro pairs, *_spec maps) is read from SPECULAR; Phong roughness falls back
 * to a converted shininess map, and a 3ds Max glossiness map in SHININESS converts as 1 - gloss.
 */
auto get_texture_slot_candidates(const aiMaterial* material, material_workflow workflow, texture_target target)
    -> std::vector<texture_slot_candidate>;

/// KHR rebakes extension diffuse into PBR base color unless BASE_COLOR is the authored albedo.
auto should_reconstruct_base_color_for_spec_gloss_pair(material_workflow workflow, const aiMaterial* material)
    -> bool;

/// Packed metallic-roughness stored in aiTextureType_SPECULAR (Bistro pairs / *_spec maps), never glTF.
auto material_has_packed_mr_in_specular_slot(const aiMaterial* material) -> bool;

/// KHR specular-only fallback: convert one specular map to combined MR (no diffuse pair).
auto khr_needs_combined_specular_mr(const aiMaterial* material, material_workflow workflow) -> bool;

/// KHR diffuse / specular / glossiness multipliers a spec-gloss bake folds into its textures.
auto gather_spec_gloss_factors(const aiMaterial* material) -> spec_gloss_factors_t;

/**
 * @brief Scalar base color / metallic / roughness for @p workflow: authored PBR factors where
 * present, converted from KHR spec-gloss or Phong shininess otherwise.
 */
void process_material_with_workflow_conversion(const aiMaterial* material,
                                               material_workflow workflow,
                                               aiColor3D& base_color,
                                               float& metallic,
                                               float& roughness);

/// AI_MATKEY_TWOSIDED, or a double/two-sided marker in the material name (FBX never sets the key).
auto is_material_two_sided(const aiMaterial* material) -> bool;

/// Legacy opacity factor below opaque: reported through @p out_opacity.
auto material_opacity_factor_suggests_cutout(const aiMaterial* material, ai_real& out_opacity) -> bool;

/// glTF alphaCutoff when set and positive, else @p fallback.
auto resolve_import_alpha_cutoff(const aiMaterial* material, ai_real fallback = 0.5f) -> ai_real;

} // namespace mesh_import
} // namespace importer
} // namespace unravel
