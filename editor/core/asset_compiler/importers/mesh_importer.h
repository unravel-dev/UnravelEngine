#pragma once
#include <engine/animation/animation.h>
#include <engine/assets/asset_manager.h>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>

namespace unravel
{
namespace importer
{
struct imported_material
{
    std::string name;
    std::shared_ptr<material> mat;
};

struct imported_texture
{
    std::string name;
    std::string semantic;
    bool inverse{};
    int embedded_index{-1};
    int process_count{};
    uint32_t flags{std::numeric_limits<uint32_t>::max()};
};

bool load_mesh_data_from_file(asset_manager& am,
                              const fs::path& path,
                              const mesh_importer_meta& import_meta,
                              mesh::load_data& load_data,
                              std::vector<animation_clip>& animations,
                              std::vector<imported_material>& materials,
                              std::vector<imported_texture>& textures);

/// External sidecar files required by a multi-file mesh source (.gltf buffers/images, .obj mtllib).
/// Used by the importer wait path and by asset_compiler::resolve_dependencies<mesh>.
auto collect_mesh_external_dependencies(const fs::path& path) -> std::vector<fs::path>;

void mesh_importer_init();
} // namespace importer
} // namespace unravel
