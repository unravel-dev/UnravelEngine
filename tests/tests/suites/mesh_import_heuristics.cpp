/*
 * Mesh importer deduction heuristics, on synthetic Assimp data.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "mesh import heuristics"
 *
 * The importer infers a material's authoring workflow and where each map lives from the material
 * properties and texture slots Assimp reports, which differ per exporter. These pin, for the
 * exporter shapes the importer was tuned on, the workflow each is classified as and the slots
 * tried for each engine map, so a heuristic changes only on purpose:
 *   - glTF metallic-roughness, glTF KHR spec-gloss (dual-authored and not), FBX Phong,
 *     Lumberyard Bistro *_BaseColor / *_Specular pairs, CryEngine *_spec packed maps,
 *     FBX maps parked in UNKNOWN;
 *   - glTF unlit and KHR_materials_specular materials, 3ds Max glossiness maps;
 *   - double-sided detection, file names made from material / clip names, repeated clip names;
 *   - animation channels kept for a node that moves a mesh through a parent, and root motion
 *     taken only from the skeleton;
 *   - skinned meshes under different transforms sharing one bind pose per bone;
 *   - embedded texture extraction names, the glossiness conversion, spec-gloss bakes of shared
 *     textures, DDS alpha-mode headers and compiled-path to source-path mapping.
 */

#include "../tests.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/importers/mesh/mesh_import_animation.h>
#include <engine/assets/impl/importers/mesh/mesh_import_geometry.h>
#include <engine/assets/impl/importers/mesh/mesh_import_image.h>
#include <engine/assets/impl/importers/mesh/mesh_import_material_workflow.h>
#include <engine/assets/impl/importers/mesh/mesh_import_paths.h>
#include <engine/assets/impl/importers/mesh/mesh_import_texture_jobs.h>

#include <graphics/vertex_decl.h>

#include <assimp/GltfMaterial.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace unravel;
using namespace unravel::importer::mesh_import;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void add_texture(aiMaterial& material, aiTextureType type, const char* path)
{
    const aiString value(path);
    material.AddProperty(&value, AI_MATKEY_TEXTURE(type, 0));
}

void add_shading(aiMaterial& material, aiShadingMode mode)
{
    const int value = mode;
    material.AddProperty(&value, 1, AI_MATKEY_SHADING_MODEL);
}

void add_float(aiMaterial& material, float value, const char* key, unsigned int type, unsigned int index)
{
    material.AddProperty(&value, 1, key, type, index);
}

void add_name(aiMaterial& material, const char* name)
{
    const aiString value(name);
    material.AddProperty(&value, AI_MATKEY_NAME);
}

void add_gltf_alpha_mode(aiMaterial& material, const char* mode)
{
    const aiString value(mode);
    material.AddProperty(&value, AI_MATKEY_GLTF_ALPHAMODE);
}

void add_3ds_max_use_glossiness(aiMaterial& material, int value)
{
    material.AddProperty(&value, 1, "$raw.3dsMax|main|useGlossiness", aiTextureType_NONE, 0);
}

/// "TYPE:semantic" per candidate, for readable comparisons.
auto describe(const std::vector<texture_slot_candidate>& candidates) -> std::vector<std::string>
{
    std::vector<std::string> described;
    for(const auto& candidate : candidates)
    {
        described.push_back(std::string(aiTextureTypeToString(candidate.type)) + ":" + candidate.semantic);
    }
    return described;
}

void check_candidates(const aiMaterial& material,
                      texture_target target,
                      const std::vector<std::string>& expected,
                      const std::string& what)
{
    const auto actual = describe(get_texture_slot_candidates(&material, detect_material_workflow(&material), target));
    std::string joined;
    for(const auto& entry : actual)
    {
        joined += entry + " ";
    }
    check(actual == expected, what + " (got: " + joined + ")");
}

/// glTF 2.0 metallic-roughness as Assimp's glTF importer writes it.
auto make_gltf_metallic_roughness() -> std::unique_ptr<aiMaterial>
{
    auto material = std::make_unique<aiMaterial>();
    add_texture(*material, aiTextureType_DIFFUSE, "base.png");
    add_texture(*material, aiTextureType_BASE_COLOR, "base.png");
    add_texture(*material, aiTextureType_GLTF_METALLIC_ROUGHNESS, "orm.png");
    add_texture(*material, aiTextureType_METALNESS, "orm.png");
    add_texture(*material, aiTextureType_DIFFUSE_ROUGHNESS, "orm.png");
    add_float(*material, 1.0f, AI_MATKEY_METALLIC_FACTOR);
    add_float(*material, 1.0f, AI_MATKEY_ROUGHNESS_FACTOR);
    add_float(*material, 0.0f, AI_MATKEY_SHININESS);
    add_shading(*material, aiShadingMode_PBR_BRDF);
    return material;
}

/// glTF KHR_materials_pbrSpecularGlossiness: DIFFUSE / SPECULAR carry the extension maps.
auto make_gltf_spec_gloss(const char* base_color_path) -> std::unique_ptr<aiMaterial>
{
    auto material = std::make_unique<aiMaterial>();
    add_texture(*material, aiTextureType_BASE_COLOR, base_color_path);
    add_texture(*material, aiTextureType_DIFFUSE, "sg_diffuse.png");
    add_texture(*material, aiTextureType_SPECULAR, "sg_specular_gloss.png");
    add_float(*material, 0.8f, AI_MATKEY_GLOSSINESS_FACTOR);
    add_shading(*material, aiShadingMode_PBR_BRDF);
    return material;
}

/// FBX Phong with a specular color map and a shininess exponent.
auto make_fbx_phong() -> std::unique_ptr<aiMaterial>
{
    auto material = std::make_unique<aiMaterial>();
    add_texture(*material, aiTextureType_DIFFUSE, "wood_albedo.png");
    add_texture(*material, aiTextureType_SPECULAR, "wood_specular_color.png");
    add_float(*material, 32.0f, AI_MATKEY_SHININESS);
    add_shading(*material, aiShadingMode_Phong);
    return material;
}

void test_material_workflows()
{
    const auto gltf_mr = make_gltf_metallic_roughness();
    check(detect_material_workflow(gltf_mr.get()) == material_workflow::metallic_roughness, "glTF MR -> metallic_roughness");
    check_candidates(*gltf_mr, texture_target::base_color, {"BaseColor:BaseColor", "Diffuse:BaseColor"}, "glTF MR base color slots");
    check_candidates(*gltf_mr,
                     texture_target::metallic,
                     {"glTFMetallicRoughness:MetallicRoughness", "Metalness:Metallic", "Unknown:MetallicRoughness"},
                     "glTF MR metallic slots");
    const auto khr_dual = make_gltf_spec_gloss("mr_fallback_base.png");
    check(detect_material_workflow(khr_dual.get()) == material_workflow::khr_specular_glossiness, "KHR spec-gloss -> khr");
    check_candidates(*khr_dual, texture_target::base_color, {"Diffuse:BaseColor"}, "KHR albedo comes from DIFFUSE");
    check_candidates(*khr_dual, texture_target::metallic, {}, "KHR ignores MR fallback maps");
    check(should_reconstruct_base_color_for_spec_gloss_pair(material_workflow::khr_specular_glossiness, khr_dual.get()),
          "dual-authored KHR rebakes its diffuse into base color");
    const auto khr_single = make_gltf_spec_gloss("sg_diffuse.png");
    check(!should_reconstruct_base_color_for_spec_gloss_pair(material_workflow::khr_specular_glossiness, khr_single.get()),
          "KHR whose BASE_COLOR is the diffuse keeps it as authored");
    const auto phong = make_fbx_phong();
    check(detect_material_workflow(phong.get()) == material_workflow::phong_specular_gloss, "FBX Phong -> phong");
    check_candidates(*phong, texture_target::base_color, {"Diffuse:BaseColor"}, "Phong albedo comes from DIFFUSE");
    check_candidates(*phong,
                     texture_target::roughness,
                     {"glTFMetallicRoughness:MetallicRoughness", "DiffuseRoughness:Roughness", "Shininess:ShininessToRoughness"},
                     "Phong roughness falls back to a converted shininess map");
    auto unknown = std::make_unique<aiMaterial>();
    check(detect_material_workflow(unknown.get()) == material_workflow::unknown, "no signals -> unknown");
    auto unlit = make_gltf_metallic_roughness();
    add_shading(*unlit, aiShadingMode_Unlit);
    check(detect_material_workflow(unlit.get()) == material_workflow::metallic_roughness,
          "glTF unlit (with its roughness-derived shininess) -> metallic_roughness, not phong");
}

void test_glossiness_maps()
{
    auto max_metal_rough = std::make_unique<aiMaterial>();
    add_texture(*max_metal_rough, aiTextureType_DIFFUSE, "albedo.png");
    add_texture(*max_metal_rough, aiTextureType_METALNESS, "metal.png");
    add_texture(*max_metal_rough, aiTextureType_SHININESS, "gloss.png");
    add_3ds_max_use_glossiness(*max_metal_rough, 1);
    check(detect_material_workflow(max_metal_rough.get()) == material_workflow::metallic_roughness,
          "3ds Max metal/rough with a glossiness map -> metallic_roughness");
    check_candidates(*max_metal_rough,
                     texture_target::roughness,
                     {"glTFMetallicRoughness:MetallicRoughness", "DiffuseRoughness:Roughness",
                      "Shininess:GlossinessToRoughness", "Unknown:MetallicRoughness"},
                     "3ds Max glossiness map in SHININESS is read as 1 - gloss");
    auto max_spec_gloss = make_fbx_phong();
    add_texture(*max_spec_gloss, aiTextureType_SHININESS, "gloss.png");
    add_3ds_max_use_glossiness(*max_spec_gloss, 1);
    check_candidates(*max_spec_gloss,
                     texture_target::roughness,
                     {"glTFMetallicRoughness:MetallicRoughness", "DiffuseRoughness:Roughness",
                      "Shininess:GlossinessToRoughness"},
                     "Phong with a 3ds Max glossiness map does not treat it as a shininess exponent");
    std::array<uint8_t, 8> texels{255, 255, 255, 255, 0, 0, 0, 255};
    bimg::ImageContainer image{};
    image.m_data = texels.data();
    image.m_size = static_cast<uint32_t>(texels.size());
    image.m_width = 2;
    image.m_height = 1;
    image.m_format = bimg::TextureFormat::RGBA8;
    image.m_numMips = 1;
    check(is_pixel_conversion("GlossinessToRoughness", false), "glossiness conversion rewrites texels");
    apply_texture_conversion(&image, "GlossinessToRoughness", false);
    check(texels[0] == 0 && texels[4] == 255, "full gloss -> zero roughness, no gloss -> full roughness");
}

void test_packed_metallic_roughness()
{
    auto bistro = std::make_unique<aiMaterial>();
    add_texture(*bistro, aiTextureType_DIFFUSE, "Textures/Paris_Wall_BaseColor.dds");
    add_texture(*bistro, aiTextureType_SPECULAR, "Textures/Paris_Wall_Specular.dds");
    add_shading(*bistro, aiShadingMode_Phong);
    add_float(*bistro, 20.0f, AI_MATKEY_SHININESS);
    check(material_has_packed_mr_in_specular_slot(bistro.get()), "Bistro *_BaseColor + *_Specular pair is packed MR");
    check(detect_material_workflow(bistro.get()) == material_workflow::metallic_roughness, "Bistro pair -> metallic_roughness");
    check_candidates(*bistro,
                     texture_target::roughness,
                     {"glTFMetallicRoughness:MetallicRoughness", "DiffuseRoughness:Roughness", "Unknown:MetallicRoughness",
                      "Specular:MetallicRoughness"},
                     "Bistro roughness reads the packed SPECULAR map");
    auto cryengine = std::make_unique<aiMaterial>();
    add_texture(*cryengine, aiTextureType_DIFFUSE, "wall_diff.dds");
    add_texture(*cryengine, aiTextureType_SPECULAR, "wall_spec.dds");
    check(material_has_packed_mr_in_specular_slot(cryengine.get()), "CryEngine *_spec is packed MR");
    auto mismatched_pair = std::make_unique<aiMaterial>();
    add_texture(*mismatched_pair, aiTextureType_DIFFUSE, "Door_BaseColor.dds");
    add_texture(*mismatched_pair, aiTextureType_SPECULAR, "Wall_Specular.dds");
    check(!material_has_packed_mr_in_specular_slot(mismatched_pair.get()), "*_BaseColor / *_Specular of different stems is not a pair");
    const auto phong = make_fbx_phong();
    check(!material_has_packed_mr_in_specular_slot(phong.get()), "Phong specular color map is not packed MR");
    auto with_shininess_map = std::make_unique<aiMaterial>();
    add_texture(*with_shininess_map, aiTextureType_DIFFUSE, "wall_diff.dds");
    add_texture(*with_shininess_map, aiTextureType_SPECULAR, "wall_spec.dds");
    add_texture(*with_shininess_map, aiTextureType_SHININESS, "wall_gloss.dds");
    check(!material_has_packed_mr_in_specular_slot(with_shininess_map.get()), "a shininess map rules out packed MR");
    auto unknown_slot = std::make_unique<aiMaterial>();
    add_texture(*unknown_slot, aiTextureType_DIFFUSE, "albedo.png");
    add_texture(*unknown_slot, aiTextureType_UNKNOWN, "combined_mr.png");
    add_shading(*unknown_slot, aiShadingMode_PBR_BRDF);
    check(detect_material_workflow(unknown_slot.get()) == material_workflow::metallic_roughness,
          "PBR material with a map in UNKNOWN -> metallic_roughness");
    auto gltf_specular = make_gltf_metallic_roughness();
    add_gltf_alpha_mode(*gltf_specular, "OPAQUE");
    add_texture(*gltf_specular, aiTextureType_SPECULAR, "panel_spec.png");
    check(!material_has_packed_mr_in_specular_slot(gltf_specular.get()),
          "glTF KHR_materials_specular map named *_spec is not packed MR");
}

void test_material_properties()
{
    auto flagged = std::make_unique<aiMaterial>();
    const int two_sided = 1;
    flagged->AddProperty(&two_sided, 1, AI_MATKEY_TWOSIDED);
    check(is_material_two_sided(flagged.get()), "AI_MATKEY_TWOSIDED is two sided");
    auto named = std::make_unique<aiMaterial>();
    add_name(*named, "Leaves_DoubleSided");
    check(is_material_two_sided(named.get()), "double-sided marker in the name is two sided");
    auto plain = std::make_unique<aiMaterial>();
    add_name(*plain, "Leaves");
    check(!is_material_two_sided(plain.get()), "plain material is one sided");
    check(replace_invalid_file_name_characters("Armature|Walk") == "Armature_Walk", "Blender clip name");
    check(replace_invalid_file_name_characters("[0] rig:skin") == "[0] rig_skin", "Maya namespace material name");
    check(replace_invalid_file_name_characters("a/b\\c<d>e\"f?g*h") == "a_b_c_d_e_f_g_h", "path and wildcard characters");
    check(replace_invalid_file_name_characters("Take 001") == "Take 001", "valid name unchanged");
}

auto make_animation(const char* name, unsigned int channel_count) -> aiAnimation*
{
    auto* animation = new aiAnimation();
    animation->mName = aiString(name);
    animation->mDuration = 10.0;
    animation->mTicksPerSecond = 10.0;
    animation->mNumChannels = channel_count;
    animation->mChannels = channel_count > 0 ? new aiNodeAnim*[channel_count] : nullptr;
    return animation;
}

/// Two keys of the given kinds, ten ticks apart.
auto make_channel(const char* node, bool is_translated, bool is_rotated) -> aiNodeAnim*
{
    auto* channel = new aiNodeAnim();
    channel->mNodeName = aiString(node);
    if(is_rotated)
    {
        channel->mNumRotationKeys = 2;
        channel->mRotationKeys = new aiQuatKey[2];
        channel->mRotationKeys[1].mTime = 10.0;
    }
    if(is_translated)
    {
        channel->mNumPositionKeys = 2;
        channel->mPositionKeys = new aiVectorKey[2];
        channel->mPositionKeys[1].mTime = 10.0;
        channel->mPositionKeys[1].mValue = aiVector3D(0.0f, 0.0f, 1.0f);
    }
    return channel;
}

/**
 * @brief root -> Camera (animated, moves nothing) and root -> Pivot (rotates, no mesh) -> Door (mesh
 * "Cube.003"); clips "Open" and two unnamed ones.
 */
auto make_rigid_hierarchy_scene() -> std::unique_ptr<aiScene>
{
    auto scene = std::make_unique<aiScene>();
    auto* root = new aiNode("Root");
    auto* pivot = new aiNode("Pivot");
    auto* door = new aiNode("Door");
    auto* camera = new aiNode("Camera");
    root->addChildren(1, &camera);
    root->addChildren(1, &pivot);
    pivot->addChildren(1, &door);
    door->mNumMeshes = 1;
    door->mMeshes = new unsigned int[1]{0};
    scene->mRootNode = root;
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1]{new aiMesh()};
    scene->mMeshes[0]->mName = aiString("Cube.003");
    auto* open = make_animation("Open", 2);
    open->mChannels[0] = make_channel("Pivot", false, true);
    open->mChannels[1] = make_channel("Camera", true, true);
    scene->mNumAnimations = 3;
    scene->mAnimations = new aiAnimation*[3]{open, make_animation("", 0), make_animation("", 0)};
    return scene;
}

void test_animation_relevance()
{
    const auto scene = make_rigid_hierarchy_scene();
    auto node_indices = assign_node_indices(scene.get());
    std::vector<animation_clip> clips;
    process_animations(scene.get(), fs::path("door"), node_indices, clips);
    check(clips.size() == 3, "one clip per animation");
    if(clips.size() != 3)
    {
        return;
    }
    const auto has_channel = [&](const char* node)
    {
        return std::any_of(clips[0].channels.begin(),
                           clips[0].channels.end(),
                           [&](const animation_channel& channel)
                           {
                               return channel.node_name == node;
                           });
    };
    check(clips[0].name == "door_Open", "clip named <file>_<animation>");
    check(has_channel("Pivot"), "a pivot moving a mesh node keeps its channel");
    check(!has_channel("Camera"), "a node moving no mesh or bone is dropped");
    check(clips[0].root_motion.position_node_name.empty() && clips[0].root_motion.rotation_node_name.empty(),
          "a rigid hierarchy (no skeleton) has no root motion: its pivot and camera never move the entity");
    check(clips[1].name == "door_" && clips[2].name == "door__2", "repeated clip names stay apart");
}

auto make_bone(const char* name, const aiMatrix4x4& offset) -> aiBone*
{
    auto* bone = new aiBone();
    bone->mName = aiString(name);
    bone->mOffsetMatrix = offset;
    bone->mNumWeights = 1;
    bone->mWeights = new aiVertexWeight[1]{aiVertexWeight(0, 1.0f)};
    return bone;
}

auto make_skinned_mesh(const char* name, const aiVector3D& vertex, const aiMatrix4x4& offset) -> aiMesh*
{
    auto* mesh = new aiMesh();
    mesh->mName = aiString(name);
    mesh->mNumVertices = 1;
    mesh->mVertices = new aiVector3D[1]{vertex};
    mesh->mNumBones = 1;
    mesh->mBones = new aiBone*[1]{make_bone("Arm", offset)};
    return mesh;
}

/**
 * Bone "Arm" binds at x = 1. Mesh A sits at the origin, mesh B under a node raised by 2, so Assimp gives
 * B the offset inverse(bind) * raise. Both meshes hold the vertex that sits on the bone at bind time.
 */
void test_shared_bind_space()
{
    aiMatrix4x4 inverse_bind;
    aiMatrix4x4::Translation(aiVector3D(-1.0f, 0.0f, 0.0f), inverse_bind);
    aiMatrix4x4 raise;
    aiMatrix4x4::Translation(aiVector3D(0.0f, 2.0f, 0.0f), raise);
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode("Root");
    scene->mNumMeshes = 2;
    scene->mMeshes = new aiMesh*[2]{make_skinned_mesh("A", aiVector3D(1.0f, 0.0f, 0.0f), inverse_bind),
                                    make_skinned_mesh("B", aiVector3D(1.0f, -2.0f, 0.0f), inverse_bind * raise)};
    mesh::load_data data;
    data.vertex_format = gfx::mesh_vertex::get_layout();
    const auto placements = process_meshes(scene.get(), data);
    check(data.skin_data.get_bones().size() == 1, "the meshes share one bone");
    check(!placements[0].has_value() && placements[1].has_value(), "only the raised mesh is re-expressed");
    float position[4] = {};
    bgfx::vertexUnpack(position, bgfx::Attrib::Position, data.vertex_format, data.vertex_data.data(), 1);
    const math::vec3 bound = data.skin_data.get_bones()[0].bind_pose_transform.transform_coord(
        math::vec3(position[0], position[1], position[2]));
    check(math::length(bound) < 1e-4f, "the raised mesh's vertex lands on the bone through the shared bind pose");
}

void test_texture_files()
{
    aiTexture jpg;
    std::strcpy(jpg.achFormatHint, "jpg");
    check(get_texture_extension(&jpg, false) == ".jpg", "an embedded JPG is extracted as itself");
    check(get_texture_extension(&jpg, true) == ".png", "converted texels are written as PNG");
    aiTexture webp;
    std::strcpy(webp.achFormatHint, "webp");
    check(get_texture_extension(&webp, false) == ".png", "a format the texture compiler cannot read becomes PNG");
    aiTexture raw;
    raw.mHeight = 4;
    std::strcpy(raw.achFormatHint, "rgba8888");
    check(get_texture_extension(&raw, false) == ".png", "raw texel data is written as PNG");

    const auto make_pair_job = [](float diffuse_r)
    {
        texture_job job{};
        job.type = texture_job_type::spec_gloss_pair;
        job.desc.name = "wood_diffuse.png";
        job.desc.semantic = "Diffuse";
        job.specular_desc.name = "wood_specular.png";
        job.specular_desc.semantic = "Specular";
        job.spec_gloss_factors.diffuse_r = diffuse_r;
        job.output_base_color_relative = "wood_diffuse_BaseColor.png";
        job.output_mr_relative = "wood_specular_MetallicRoughness.png";
        return job;
    };
    texture_job_store store;
    store.try_add(make_pair_job(1.0f));
    store.try_add(make_pair_job(0.5f));
    texture_job lone = make_pair_job(1.0f);
    lone.desc.name = "stone_diffuse.png";
    lone.output_base_color_relative = "stone_diffuse_BaseColor.png";
    lone.output_mr_relative = "stone_specular_MetallicRoughness.png";
    lone.specular_desc.name = "stone_specular.png";
    store.try_add(lone);
    disambiguate_shared_pair_outputs(store);
    const auto& jobs = store.jobs();
    check(jobs.size() == 3, "pair jobs differing only in factors are distinct");
    check(jobs[0].output_base_color_relative != jobs[1].output_base_color_relative
              && jobs[0].output_mr_relative != jobs[1].output_mr_relative,
          "two materials tinting one texture differently bake separate files");
    check(jobs[2].output_base_color_relative == "stone_diffuse_BaseColor.png", "an unshared bake keeps its name");

    const fs::path dir = fs::temp_directory_path() / "unravel_mesh_import_heuristics";
    fs::error_code err;
    fs::create_directories(dir, err);
    const auto write_dds = [&](const char* name, const char* fourcc, uint32_t alpha_mode)
    {
        std::array<char, 148> header{};
        std::memcpy(header.data(), "DDS ", 4);
        std::memcpy(header.data() + 84, fourcc, 4);
        std::memcpy(header.data() + 144, &alpha_mode, sizeof(alpha_mode));
        std::ofstream(dir / name, std::ios::binary).write(header.data(), header.size());
        return dir / name;
    };
    check(dds_header_declares_alpha(write_dds("straight.dds", "DX10", 1)), "DX10 straight alpha is declared alpha");
    check(dds_header_declares_alpha(write_dds("premultiplied.dds", "DX10", 2)), "DX10 premultiplied alpha is declared alpha");
    check(!dds_header_declares_alpha(write_dds("opaque.dds", "DX10", 3)), "DX10 opaque declares no alpha");
    check(!dds_header_declares_alpha(write_dds("legacy.dds", "DXT1", 1)), "a legacy header has no alpha mode");

    check(ex::get_source_path_from_compiled("c/Models/Torso01.001_baseColor.png.asset")
              == fs::path("c/Models/Torso01.001_baseColor.png"),
          "a dotted source name survives the compiled-path mapping");
    check(ex::get_source_path_from_compiled("c/x.sc.asset.dxbc") == fs::path("c/x.sc"), "shader variant suffix dropped");
    check(ex::get_source_path_from_compiled("c/my.asset.png.asset") == fs::path("c/my.asset.png"),
          "only the trailing compiled suffix is dropped");
}

auto run_mesh_import_heuristics(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    test_material_workflows();
    test_glossiness_maps();
    test_packed_metallic_roughness();
    test_material_properties();
    test_animation_relevance();
    test_shared_bind_space();
    test_texture_files();
    std::printf("  %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("mesh import heuristics", run_mesh_import_heuristics)
