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

#include <chrono>

namespace unravel
{
namespace importer
{
namespace
{

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
    return importer.ReadFile(file.string(), flags);
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
