#pragma once
#include <graphics/graphics.h>
#include <hpp/span.hpp>
#include <hpp/string_view.hpp>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace unravel
{

/**
 * @brief One morph target (blend shape) of a mesh: a shape its vertices move toward as the target's weight goes from
 *        0 to 1, kept as deltas from the rest shape.
 */
struct morph_target
{
    /// The target's name, unique within its mesh. Animation curves and weights find it by this name.
    std::string name;
    /// The weight a model has for this target until it sets its own.
    float default_weight{0.0f};
    /// The farthest any vertex moves at weight 1, in the mesh's units.
    float max_displacement{0.0f};
    /// This target's entries in morph_target_data::deltas.
    uint32_t first_delta{0};
    uint32_t delta_count{0};
};

/**
 * @brief How far one target moves one morphed vertex at weight 1.
 */
struct morph_delta
{
    /// The vertex's place in morph_target_data::morphed_vertices.
    uint32_t slot{0};
    /// The position delta, in the mesh's units.
    std::array<float, 3> position{};
    /// The normal delta (each component in -2..2), packed by pack_morph_normal.
    uint32_t normal{0};
};

/**
 * @brief The morph targets of a mesh: every target's deltas over the vertices any of them moves.
 */
struct morph_target_data
{
    std::vector<morph_target> targets;
    /// Every vertex some target moves, ascending. A vertex's index in this list is its slot.
    std::vector<uint32_t> morphed_vertices;
    /// Every target's deltas, by target, then by slot.
    std::vector<morph_delta> deltas;

    /// Whether the mesh has no morph targets.
    auto empty() const -> bool
    {
        return targets.empty();
    }

    /// The index of the target named @p name, or -1.
    auto find_target(hpp::string_view name) const -> int;
};

/**
 * @brief What a draw of a morphed model binds: the deform streams its morph deform pass wrote for one mesh. Stream 0
 *        is the deformed position and basis, stream 1 the mesh's own buffer under a layout that skips them, and in the
 *        velocity pass stream 2 the previous frame's deformed positions, read as a_texcoord7.
 */
struct morph_deform_binding
{
    /// The mesh the streams deform: a draw of any other mesh (another LOD's) ignores them.
    const void* deformed_mesh{nullptr};
    /// This frame's deformed vertices, and last frame's.
    bgfx::DynamicVertexBufferHandle current = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle previous = BGFX_INVALID_HANDLE;
    /// The mesh's layout without position and basis, and the previous stream's layout (position as TexCoord7).
    bgfx::VertexLayoutHandle rest_layout = BGFX_INVALID_HANDLE;
    bgfx::VertexLayoutHandle previous_layout = BGFX_INVALID_HANDLE;
    /// The current stream can also be bound as a read-only shader buffer (bgfx::setBuffer): compute writes it. The
    /// CPU path's streams cannot, and binding one is undefined.
    bool is_shader_readable{false};

    /// Whether the streams deform @p mesh.
    auto deforms(const void* mesh) const -> bool
    {
        return mesh != nullptr && mesh == deformed_mesh && bgfx::isValid(current) && bgfx::isValid(rest_layout);
    }

    /// Whether a shader can read the deformed vertices of @p mesh from the current stream as a buffer (vertex pulling).
    auto deforms_readable(const void* mesh) const -> bool
    {
        return is_shader_readable && deforms(mesh);
    }
};

/// A normal delta (each component in -2..2) as three 10-bit signed normalized values of its half.
auto pack_morph_normal(const math::vec3& delta) -> uint32_t;

/// The normal delta pack_morph_normal packed.
auto unpack_morph_normal(uint32_t packed) -> math::vec3;

/**
 * @brief Collects the vertex deltas of targets as the importer reads them and builds a mesh's morph_target_data.
 *
 * Targets that share a name are one target: a Blink on the face and on the eyelashes, which a file keeps as two
 * meshes, moves both.
 */
class morph_target_builder
{
public:
    /// One vertex a target moves, and how.
    struct vertex_delta
    {
        uint32_t vertex{0};
        math::vec3 position{0.0f};
        math::vec3 normal{0.0f};
    };

    /// Adds @p deltas to the target named @p name, which is created with @p default_weight on first use.
    void add(const std::string& name, float default_weight, const std::vector<vertex_delta>& deltas);

    /// Whether no target was added.
    auto empty() const -> bool;

    /**
     * @brief The morph targets, each keeping only the vertices it moves at least @p position_threshold (in the mesh's
     *        units) or whose normal it turns by at least @p normal_threshold. A target that keeps no vertex is kept too:
     *        its name still answers animation and scripts.
     */
    auto build(float position_threshold, float normal_threshold) const -> morph_target_data;

private:
    struct target_entry
    {
        std::string name;
        float default_weight{0.0f};
        std::vector<vertex_delta> deltas;
    };
    std::vector<target_entry> targets_;
};

/**
 * @brief Gives each copy of a morphed vertex the deltas of its original: @p copies holds (copy, original) pairs, as the
 *        skin split makes them when a vertex is drawn by more than one bone palette.
 */
void add_morph_vertex_copies(morph_target_data& data, const std::vector<std::pair<uint32_t, uint32_t>>& copies);

/// The largest distance each of @p vertex_count vertices moves under any one target at weight 1.
auto get_morph_vertex_displacements(const morph_target_data& data, uint32_t vertex_count) -> std::vector<float>;

/// Whether a weight moves its target: weights closer to 0 are skipped.
constexpr float k_morph_weight_threshold = 1e-4f;

/**
 * @brief Adds every target whose weight is not 0 into per-slot deltas: 6 floats a slot, the position delta then the
 *        normal delta. @p weights holds one weight per target, in the mesh's target order; missing ones and ones that
 *        are not finite numbers count as 0.
 */
void accumulate_morph_deltas(const morph_target_data& data, hpp::span<const float> weights, std::vector<float>& deltas);

/**
 * @brief Moves @p positions (one for each of the mesh's vertices, at rest) by the position deltas
 *        accumulate_morph_deltas made: the shape a CPU test of the mesh (a raycast) meets.
 */
void apply_morph_position_deltas(const morph_target_data& data,
                                 const std::vector<float>& deltas,
                                 std::vector<math::vec3>& positions);

/**
 * @brief How far @p weights move a vertex at most, in the mesh's units: the sum over the targets of the weight's size
 *        times the target's max_displacement. The mesh's own bounds hold each target alone at weight 1, so a model
 *        grows its bounds by this while several targets are on or a weight passes 1. Weights that are not finite
 *        numbers count as 0, as in accumulate_morph_deltas.
 */
auto get_morph_bounds_padding(const morph_target_data& data, hpp::span<const float> weights) -> float;

/// A morph target found by the animation curve that keys it.
struct morph_curve_target
{
    /// The animation curve id of the target's name (get_animation_curve_id).
    uint32_t curve_id{0};
    /// The target's index in morph_target_data::targets.
    uint32_t target{0};
};

/// The targets of @p data by the curve ids of their names, ascending: a pose's curves (sorted by id) find their
/// targets in one walk, without hashing names every frame.
auto make_morph_curve_targets(const morph_target_data& data) -> std::vector<morph_curve_target>;

/// The vertex layout a deformed vertex is written in: the mesh's position and its packed normal, tangent and bitangent.
auto get_morph_deform_layout() -> const bgfx::VertexLayout&;

/**
 * @brief Writes the morphed vertices into @p deformed (get_morph_deform_layout, one entry for each of the mesh's
 *        vertices): base vertex from @p mesh_vertices plus the slot's delta from accumulate_morph_deltas. The normal
 *        is renormalized, the tangent made orthogonal to it again and the bitangent rebuilt with its old handedness.
 *        Vertices no target moves are not written.
 */
void write_morphed_vertices(const morph_target_data& data,
                            const bgfx::VertexLayout& mesh_layout,
                            const uint8_t* mesh_vertices,
                            const std::vector<float>& deltas,
                            uint8_t* deformed);

/// Writes every vertex of the mesh at rest into @p deformed (get_morph_deform_layout).
void write_rest_vertices(const bgfx::VertexLayout& mesh_layout,
                         const uint8_t* mesh_vertices,
                         uint32_t vertex_count,
                         uint8_t* deformed);

} // namespace unravel
