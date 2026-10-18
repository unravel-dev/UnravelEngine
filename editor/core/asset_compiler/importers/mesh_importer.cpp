#include "mesh_importer.h"

#include "mesh/mesh_import_animation.h"
#include "mesh/mesh_import_dependencies.h"
#include "mesh/mesh_import_geometry.h"
#include "mesh/mesh_import_materials.h"

#include <engine/meta/assets/asset_importer_meta.hpp>

#include <graphics/graphics.h>
#include <logging/logging.h>

#include <assimp/DefaultLogger.hpp>
#include <assimp/Importer.hpp>
#include <assimp/LogStream.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

namespace unravel
{
namespace importer
{
namespace
{

/// Axis indices an FBX file's settings name (X, Y, Z).
constexpr int32_t FBX_AXIS_COUNT = 3;

auto is_fbx_file(const fs::path& file) -> bool
{
    std::string extension = file.extension().string();
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c)
                   {
                       return char(std::tolower(c));
                   });
    return extension == ".fbx";
}

/**
 * @brief Turns the root of an FBX scene without meshes by the file's axis settings.
 *
 * Assimp turns an FBX scene's root by the file's UpAxis / FrontAxis / CoordAxis settings only when
 * the file has meshes. An animation-only export (a clip saved without its skin) would keep the
 * authoring tool's axes while the skinned file it belongs to is turned upright: the same correction,
 * read and applied as Assimp does before post-processing, keeps both on one root frame.
 */
void correct_meshless_fbx_root(aiScene& scene, const fs::path& file)
{
    if(scene.mNumMeshes > 0 || scene.mRootNode == nullptr || scene.mMetaData == nullptr || !is_fbx_file(file))
    {
        return;
    }
    int32_t up_axis = 1;
    int32_t up_axis_sign = 1;
    int32_t front_axis = 2;
    int32_t front_axis_sign = 1;
    int32_t coord_axis = 0;
    int32_t coord_axis_sign = 1;
    double unit_scale_factor = 1.0;
    const aiMetadata& metadata = *scene.mMetaData;
    metadata.Get("UpAxis", up_axis);
    metadata.Get("UpAxisSign", up_axis_sign);
    metadata.Get("FrontAxis", front_axis);
    metadata.Get("FrontAxisSign", front_axis_sign);
    metadata.Get("CoordAxis", coord_axis);
    metadata.Get("CoordAxisSign", coord_axis_sign);
    // Read as a double, as Assimp reads it for files with meshes, so both kinds get the same scale.
    metadata.Get("UnitScaleFactor", unit_scale_factor);
    const auto is_axis = [](int32_t axis)
    {
        return axis >= 0 && axis < FBX_AXIS_COUNT;
    };
    if(!is_axis(up_axis) || !is_axis(front_axis) || !is_axis(coord_axis))
    {
        return;
    }
    const float scale = float(unit_scale_factor);
    aiVector3D up;
    aiVector3D forward;
    aiVector3D right;
    up[unsigned(up_axis)] = float(up_axis_sign) * scale;
    forward[unsigned(front_axis)] = float(front_axis_sign) * scale;
    right[unsigned(coord_axis)] = float(coord_axis_sign) * scale;
    const aiMatrix4x4 correction(right.x, right.y, right.z, 0.0f,
                                 up.x, up.y, up.z, 0.0f,
                                 forward.x, forward.y, forward.z, 0.0f,
                                 0.0f, 0.0f, 0.0f, 1.0f);
    scene.mRootNode->mTransformation *= correction;
}

void process_imported_scene(asset_manager& am,
                            const fs::path& filename,
                            const fs::path& output_dir,
                            const aiScene* scene,
                            mesh::load_data& load_data,
                            std::vector<animation_clip>& animations,
                            std::vector<imported_material>& materials,
                            std::vector<imported_texture>& textures)
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Mesh Importer: Parse Imported Data");

    load_data.vertex_format = gfx::mesh_vertex::get_layout();

    auto name_to_index_lut = mesh_import::assign_node_indices(scene);

    APPLOG_TRACE("Mesh Importer: Processing materials (collect jobs -> run jobs -> bind) ...");
    mesh_import::process_materials(am, filename, output_dir, scene, materials, textures);

    APPLOG_TRACE("Mesh Importer: Processing meshes ...");
    const auto submesh_placements = mesh_import::process_meshes(scene, load_data);

    APPLOG_TRACE("Mesh Importer: Processing nodes ...");
    mesh_import::process_nodes(scene, load_data, submesh_placements, name_to_index_lut);

    APPLOG_TRACE("Mesh Importer: Processing animations ...");
    mesh_import::process_animations(scene, filename, name_to_index_lut, animations);

    // Note: bounds are intentionally bind-pose only. Animation-driven expansion happens at
    // runtime from per-bone bind-space bounds (skinned) and per-node submesh proxy bounds
    // (rigid attachments), which track the actual pose instead of pre-sampled clips.
    if(!load_data.bbox.is_populated())
    {
        load_data.bbox = {};
        mesh_import::accumulate_bounds_from_armature(load_data, load_data.bbox);
    }

    mesh_import::apply_import_facing_correction_to_load_data(load_data);

    APPLOG_TRACE("Mesh Importer: bbox min {}, max {}", load_data.bbox.min, load_data.bbox.max);
}

auto read_file(Assimp::Importer& importer, const fs::path& file, uint32_t flags) -> const aiScene*
{
    APPLOG_TRACE_PERF_NAMED(std::chrono::milliseconds, "Importer Read File");
    // Read without post-processing first: a mesh-less FBX root is corrected in the frame the file
    // was read in, as Assimp corrects the others, and only then converted.
    const aiScene* scene = importer.ReadFile(file.string(), 0);
    if(scene == nullptr)
    {
        return nullptr;
    }
    // The importer owns the scene and post-processing rewrites it in place anyway.
    correct_meshless_fbx_root(*const_cast<aiScene*>(scene), file);
    return importer.ApplyPostProcessing(flags);
}

/// Assimp post-processing for an import with @p import_meta's model and material options.
auto get_postprocess_flags(const mesh_importer_meta& import_meta) -> uint32_t
{
    // clang-format off
    uint32_t flags = aiProcess_ConvertToLeftHanded |
                     aiProcess_RemoveComponent     |
                     aiProcess_Triangulate         |
                     aiProcess_CalcTangentSpace    |
                     aiProcess_GenUVCoords         |
                     aiProcess_GenSmoothNormals    |
                     aiProcess_GenBoundingBoxes    |
                     aiProcess_ImproveCacheLocality|
                     aiProcess_LimitBoneWeights    |
                     aiProcess_SortByPType         |
                     aiProcess_TransformUVCoords   |
                     aiProcess_GlobalScale;
    // clang-format on

    if(import_meta.model.weld_vertices)
    {
        flags |= aiProcess_JoinIdenticalVertices;
    }
    if(import_meta.model.optimize_meshes)
    {
        flags |= aiProcess_OptimizeMeshes;
    }
    if(import_meta.model.split_large_meshes)
    {
        flags |= aiProcess_SplitLargeMeshes;
    }
    if(import_meta.model.find_degenerates)
    {
        flags |= aiProcess_FindDegenerates;
    }
    if(import_meta.model.find_invalid_data)
    {
        flags |= aiProcess_FindInvalidData;
    }
    if(import_meta.materials.remove_redundant_materials)
    {
        flags |= aiProcess_RemoveRedundantMaterials;
    }
    return flags;
}

/// Scene components aiProcess_RemoveComponent strips: cameras, lights and whatever the meta turns off.
auto get_removed_components(const mesh_importer_meta& import_meta) -> int
{
    int rvc_flags = aiComponent_CAMERAS | aiComponent_LIGHTS;
    if(!import_meta.model.import_meshes)
    {
        rvc_flags |= aiComponent_MESHES;
    }
    if(!import_meta.animations.import_animations)
    {
        rvc_flags |= aiComponent_ANIMATIONS;
    }
    if(!import_meta.materials.import_materials)
    {
        rvc_flags |= aiComponent_MATERIALS;
    }
    return rvc_flags;
}

} // namespace

void mesh_importer_init()
{
    struct log_stream : public Assimp::LogStream
    {
        log_stream(Assimp::Logger::ErrorSeverity s) : severity(s)
        {
        }

        void write(const char* message) override
        {
            switch(severity)
            {
                case Assimp::Logger::Info:
                    APPLOG_INFO("Mesh Importer: {0}", message);
                    break;
                case Assimp::Logger::Warn:
                    APPLOG_WARNING("Mesh Importer: {0}", message);
                    break;
                case Assimp::Logger::Err:
                    APPLOG_ERROR("Mesh Importer: {0}", message);
                    break;
                default:
                    APPLOG_TRACE("Mesh Importer: {0}", message);
                    break;
            }
        }

        Assimp::Logger::ErrorSeverity severity{};
    };

    // if(Assimp::DefaultLogger::isNullLogger())
    // {
    //     auto logger = Assimp::DefaultLogger::create("", Assimp::Logger::VERBOSE);

    //     logger->attachStream(new log_stream(Assimp::Logger::Debugging), Assimp::Logger::Debugging);
    //     logger->attachStream(new log_stream(Assimp::Logger::Info), Assimp::Logger::Info);
    //     logger->attachStream(new log_stream(Assimp::Logger::Warn), Assimp::Logger::Warn);
    //     logger->attachStream(new log_stream(Assimp::Logger::Err), Assimp::Logger::Err);
    // }
}

auto load_mesh_data_from_file(asset_manager& am,
                              const fs::path& path,
                              const mesh_importer_meta& import_meta,
                              mesh::load_data& load_data,
                              std::vector<animation_clip>& animations,
                              std::vector<imported_material>& materials,
                              std::vector<imported_texture>& textures) -> bool
{
    // Multi-file formats (glTF + .bin/textures, OBJ + mtllib) must be complete on disk
    // before Assimp reads them; otherwise vertex buffers can import as empty.
    if(!mesh_import::wait_for_mesh_source_dependencies(path))
    {
        return false;
    }
    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, get_removed_components(import_meta));
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_LINE | aiPrimitiveType_POINT);
    importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, true);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

    APPLOG_TRACE("Mesh Importer: Loading {}", path.generic_string());

    const aiScene* scene = read_file(importer, path, get_postprocess_flags(import_meta));
    if(scene == nullptr)
    {
        APPLOG_ERROR(importer.GetErrorString());
        return false;
    }

    process_imported_scene(am, path.stem(), path.parent_path(), scene, load_data, animations, materials, textures);

    APPLOG_TRACE("Mesh Importer: Done with {}", path.generic_string());

    return true;
}

} // namespace importer
} // namespace unravel
