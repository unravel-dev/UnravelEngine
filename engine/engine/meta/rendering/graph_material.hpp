#pragma once
#include "shader_variants.hpp"

#include <engine/rendering/material_graph/graph_material.h>
#include <reflection/reflection.h>
#include <serialization/serialization.h>

#include <string>
#include <vector>

namespace unravel
{
SAVE_EXTERN(graph_material);
LOAD_EXTERN(graph_material);
REFLECT_EXTERN(graph_material);

/**
 * @brief A graph material's shader compiled for one renderer: a file beside the compiled material
 * (get_graph_material_shader_file), in the place a compiled shader asset's file of that renderer would have.
 */
struct compiled_graph_material_shader
{
    /// graph_material::get_shader_hash of the material it was compiled for.
    std::string code_hash;
    /// What the compile of its renderer depended on beside the code (asset compiler shader compile parameters).
    std::string compile_parameters;
    /// The variants the generated shader declares, as a compiled .eshader holds them.
    std::vector<compiled_shader_variant> variants;
    /// The variants of the generated vertex shader, for a graph whose World Position Offset moves its vertices; none
    /// for every other graph.
    std::vector<compiled_shader_variant> vertex_variants;
};

SAVE_EXTERN(compiled_graph_material_shader);
LOAD_EXTERN(compiled_graph_material_shader);

void save_to_file_bin(const std::string& absolute_path, const compiled_graph_material_shader& shader);
auto load_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader& shader) -> bool;

/// What a freshness check of a graph material's shader file reads: what it was compiled from and whether it holds code.
struct compiled_graph_material_shader_header
{
    std::string code_hash;
    std::string compile_parameters;
    /// The compile gave code (the record of a failed compile holds none).
    bool has_variants{};
};

/// Reads the header of the shader file at @p absolute_path, not its variants' binaries; false when it cannot.
auto load_header_from_file_bin(const std::string& absolute_path, compiled_graph_material_shader_header& header) -> bool;

/// The file holding a graph material's shader for the renderer of @p renderer_extension (".dxil"): the compiled
/// material's file with the renderer's extension added, as compiled shaders name theirs.
auto get_graph_material_shader_file(const std::string& compiled_material_path, const std::string& renderer_extension)
    -> std::string;

} // namespace unravel

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>
SERIALIZE_REGISTER_TYPE_WITH_NAME(unravel::graph_material, "graph_material")
