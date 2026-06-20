#pragma once
#include <engine/engine_export.h>
#include <engine/assets/asset_handle.h>
#include <engine/rendering/material.h>
#include <base/basetypes.hpp>
#include <graphics/graphics.h>
#include <hpp/span.hpp>
#include <hpp/uuid.hpp>
#include <math/math.h>

#include <map>
#include <memory>
#include <vector>

namespace unravel
{
class camera;

namespace triangle_flags
{
enum e
{
    none = 0,
    degenerate = 0x1,
};
} // namespace triangle_flags

enum class mesh_status
{
    not_prepared,
    preparing,
    prepared
};

enum class mesh_create_origin
{
    bottom,
    center,
    top
};

/**
 * @brief Structure describing how a skinned mesh should be bound to any bones
 * that influence its vertices.
 */
class skin_bind_data
{
public:
    /**
     * @brief Describes how a bone influences a specific vertex.
     */
    struct vertex_influence
    {
        ///< The index of the vertex influenced by the bone.
        uint32_t vertex_index = 0;
        ///< The weight of the influence.
        float weight = 0.0f;
    };
    using vertex_influence_array_t = std::vector<vertex_influence>;

    /**
     * @brief Describes the vertices that are connected to the referenced bone and how much influence it has on them.
     */
    struct bone_influence
    {
        ///< The unique identifier of the bone.
        std::string bone_id;
        ///< The "bind pose" or "offset" transform of the bone.
        math::transform bind_pose_transform;
        ///< List of vertices influenced by the bone.
        vertex_influence_array_t influences;
    };
    using bone_influence_array_t = std::vector<bone_influence>;

    /**
     * @brief Contains per-vertex influence and weight information.
     */
    struct vertex_data
    {
        ///< List of bones that influence this vertex.
        std::vector<int32_t> influences;
        ///< List of weights for each influence.
        std::vector<float> weights;
        ///< Index of the palette to which this vertex has been assigned.
        int32_t palette;
        ///< The index of the original vertex.
        uint32_t original_vertex;
    };
    using vertex_data_array_t = std::vector<vertex_data>;

    /**
     * @brief Adds influence information for a specific bone.
     *
     * @param bone The bone influence information to add.
     */
    void add_bone(const bone_influence& bone);

    /**
     * @brief Removes any bones that do not contain any influences.
     */
    void remove_empty_bones();

    /**
     * @brief Constructs a list of bone influences and weights for each vertex based on the binding data provided.
     *
     * @param vertex_count The total number of vertices.
     * @param vertex_remap The remap array for vertices.
     * @param table The output table of vertex data.
     */
    void build_vertex_table(uint32_t vertex_count,
                            const std::vector<uint32_t>& vertex_remap,
                            vertex_data_array_t& table);

    /**
     * @brief Remaps the vertex references stored in the binding based on the supplied remap array.
     *
     * @param remap The remap array.
     */
    void remap_vertices(const std::vector<uint32_t>& remap);

    /**
     * @brief Retrieves a list of all bones that influence the skin in some way.
     *
     * @return const bone_influence_array_t& The list of bone influences.
     */
    auto get_bones() const -> const bone_influence_array_t&;

    /**
     * @brief Retrieves a list of all bones that influence the skin in some way.
     *
     * @return bone_influence_array_t& The list of bone influences.
     */
    auto get_bones() -> bone_influence_array_t&;

    /**
     * @brief Checks whether the skin data has any bones.
     *
     * @return true If there are bones.
     * @return false If there are no bones.
     */
    auto has_bones() const -> bool;

    struct bone_query
    {
        const bone_influence* bone{};
        int index{-1};
    };

    /**
     * @brief Finds a bone by its unique identifier.
     *
     * @param id The unique identifier of the bone.
     * @return const bone_influence* Pointer to the bone influence data if found, otherwise nullptr.
     */
    auto find_bone_by_id(const std::string& id) const -> bone_query;

    /**
     * @brief Releases memory allocated for vertex influences in each stored bone.
     */
    void clear_vertex_influences();

    /**
     * @brief Clears out the bone information stored in this object.
     */
    void clear();

private:
    bone_influence_array_t bones_; ///< List of bones that influence the skin mesh vertices.
};

/**
 * @brief Outlines a collection of bones that influence a given set of faces/vertices in the mesh.
 */
class bone_palette
{
public:
    using bone_index_map_t = std::map<uint32_t, uint32_t>;

    /**
     * @brief Constructs a bone palette with the given size.
     *
     * @param paletteSize The maximum size of the palette.
     */
    bone_palette(uint32_t paletteSize);

    /**
     * @brief Gathers the bone/palette information and matrices ready for drawing the skinned mesh.
     *
     * @param node_transforms The node transforms.
     * @param bind_data The skin bind data.
     * @param compute_inverse_transpose Whether to compute the inverse transpose of the matrices.
     * @return std::vector<math::transform> The skinning matrices.
     */
    auto get_skinning_matrices(const std::vector<math::transform>& node_transforms,
                               const skin_bind_data& bind_data) const -> const std::vector<math::mat4>&;

    auto get_skinning_matrices(const std::vector<math::mat4>& node_transforms, const skin_bind_data& bind_data) const
        -> const std::vector<math::mat4>&;

    /**
     * @brief Determines the relevant "fit" information that can be used to discover if and how the specified
     * combination of bones will fit into this palette.
     *
     * @param input The input bone index map.
     * @param current_space The current space available in the palette.
     * @param common_base The common base index.
     * @param additional_bones The number of additional bones required.
     */
    void compute_palette_fit(bone_index_map_t& input,
                             int32_t& current_space,
                             int32_t& common_base,
                             int32_t& additional_bones);

    /**
     * @brief Assigns the specified bones (and faces) to this bone palette.
     *
     * @param bones The bones to assign.
     * @param faces The faces influenced by these bones.
     */
    void assign_bones(bone_index_map_t& bones, std::vector<uint32_t>& faces);
    void assign_bones(std::vector<bool>& bones, std::vector<uint32_t>& faces);

    /**
     * @brief Assigns the specified bones to this bone palette.
     *
     * @param bones The bones to assign.
     */
    void assign_bones(const std::vector<uint32_t>& bones);

    /**
     * @brief Translates the specified bone index into its associated position in the palette.
     *
     * @param bone_index The bone index.
     * @return uint32_t The position in the palette, or -1 if not found.
     */
    auto translate_bone_to_palette(uint32_t bone_index) const -> uint32_t;

    /**
     * @brief Retrieves the maximum vertex blend index for this palette.
     *
     * @return int32_t The maximum vertex blend index.
     */
    auto get_maximum_blend_index() const -> int32_t;

    /**
     * @brief Retrieves the maximum size of the palette.
     *
     * @return uint32_t The maximum size of the palette.
     */
    auto get_maximum_size() const -> uint32_t;

    /**
     * @brief Retrieves the identifier of the data group assigned to the submesh of the mesh reserved for this bone
     * palette.
     *
     * @return uint32_t The data group identifier.
     */
    auto get_data_group() const -> uint32_t;

    /**
     * @brief Retrieves the list of faces assigned to this palette.
     *
     * @return std::vector<uint32_t>& The list of faces.
     */
    auto get_influenced_faces() -> std::vector<uint32_t>&;

    /**
     * @brief Retrieves the indices of the bones referenced by this palette.
     *
     * @return const std::vector<uint32_t>& The list of bone indices.
     */
    auto get_bones() const -> const std::vector<uint32_t>&;

    /**
     * @brief Sets the maximum vertex blend index for this palette.
     *
     * @param index The maximum vertex blend index.
     */
    void set_maximum_blend_index(int index);

    /**
     * @brief Sets the identifier of the data group assigned to the submesh of the mesh reserved for this bone palette.
     *
     * @param group The data group identifier.
     */
    void set_data_group(uint32_t group);

    /**
     * @brief Clears out the temporary face influences array.
     */
    void clear_influenced_faces();

protected:
    ///< Sorted list of bones in this palette.
    bone_index_map_t bones_lut_;
    ///< Main palette of indices that reference the bones outlined in the main skin binding data.
    std::vector<uint32_t> bones_;
    ///< List of faces assigned to this palette.
    std::vector<uint32_t> faces_;
    ///< The data group identifier used to separate the mesh data into submeshes relevant tothis bone palette.
    uint32_t data_group_id_;
    ///< The maximum size of the palette.
    uint32_t maximum_size_;
    ///< The maximum vertex blend index for this palette.
    int32_t maximum_blend_index_;
};

/**
 * @brief Main class representing a 3D mesh with support for different LODs, submeshes, and skinning.
 */
class mesh
{
public:
    /**
     * @brief Structure describing an individual "piece" of the mesh, often grouped by material, but can be any
     * arbitrary collection of triangles.
     */
    struct submesh
    {
        ///< The unique user assigned "data group" that can be used to separate submeshes.
        uint32_t data_group_id{0};
        ///< The beginning vertex for this batch.
        int32_t vertex_start{-1};
        ///< Number of vertices included in this batch.
        uint32_t vertex_count{0};
        ///< The initial face, from the index buffer, to render in this batch.
        int32_t face_start{-1};
        ///< Number of faces to render in this batch.
        uint32_t face_count{0};

        math::bbox bbox{};

        bool skinned{};
    };

    struct info
    {
        ///< Structure describing LOD level information.
        struct lod_info
        {
            ///< Number of triangles in this LOD.
            uint32_t triangles = 0;
            ///< Percentage of triangles in this LOD.
            float percent = 0.0f;
        };

        ///< Total number of vertices.
        uint32_t vertices = 0;
        ///< Total number of triangles.
        uint32_t triangles = 0;
        ///< Total number of submeshes.
        uint32_t submeshes = 0;
        ///< Total number of data groups(materials).
        uint32_t data_groups = 0;
        ///< Information about each LOD level.
        std::vector<lod_info> lods;
    };

    /**
     * @brief Structure describing data for a single triangle in the mesh.
     */
    struct triangle
    {
        ///< Data group identifier for this triangle.
        uint32_t data_group_id = 0;
        ///< Indices of the vertices in this triangle.
        std::array<uint32_t, 3> indices = {0, 0, 0};
        ///< Flags for this triangle.
        uint8_t flags = 0;
    };

    using triangle_array_t = std::vector<triangle>;
    using submesh_array_t = std::vector<submesh*>;
    using bone_palette_array_t = std::vector<bone_palette>;
    using submesh_array_indices_t = std::vector<size_t>;
    using submesh_array_map_t = std::map<uint32_t, submesh_array_indices_t>;

    using data_group_submesh_map_t = std::map<uint32_t, submesh_array_t>;
    using byte_array_t = std::vector<uint8_t>;

    /**
     * @brief Structure describing a LOD level with its own index buffer and submeshes.
     */
    struct lod_level
    {
        ///< System memory index buffer for this LOD
        uint32_t* system_ib_{nullptr};
        ///< Hardware index buffer for this LOD
        std::shared_ptr<void> hardware_ib_;
        ///< Number of faces in this LOD
        uint32_t face_count_{0};
        ///< Submeshes for this LOD (face ranges may differ from base LOD)
        submesh_array_t submeshes_;
        ///< Cached skinned submesh indices per data group
        submesh_array_map_t skinned_submesh_indices_;
        ///< Cached non-skinned submesh indices per data group
        submesh_array_map_t non_skinned_submesh_indices_;
        ///< Error metric for this LOD (from meshoptimizer)
        float simplification_error_{0.0f};
    };

    struct armature_node
    {
        ///< Name of the armature node.
        std::string name;
        ///< Local transform of the armature node.
        math::transform local_transform;
        ///< Children nodes of this armature node.
        std::vector<std::unique_ptr<armature_node>> children;
        ///< submesh indices affected by this node
        std::vector<uint32_t> submeshes;

        int index{-1};
    };

    /**
     * @brief Structure describing a LOD level in load_data (for serialization).
     */
    struct lod_load_data
    {
        ///< Index buffer data for this LOD (as uint32_t array)
        std::vector<uint32_t> index_data;
        ///< Number of faces in this LOD
        uint32_t face_count = 0;
        ///< Submeshes for this LOD (face ranges may differ from base LOD)
        std::vector<mesh::submesh> submeshes;
        ///< Error metric for this LOD (from meshoptimizer)
        float simplification_error = 0.0f;
    };

    /**
     * @brief Struct used for mesh construction.
     */
    struct load_data
    {
        ///< The format of the vertex data.
        gfx::vertex_layout vertex_format;
        ///< Vertex data buffer.
        std::vector<uint8_t> vertex_data;
        ///< Total number of vertices.
        uint32_t vertex_count = 0;
        ///< Triangle data buffer.
        triangle_array_t triangle_data;
        ///< Total number of triangles.
        uint32_t triangle_count = 0;
        ///< submeshes descriptions
        std::vector<mesh::submesh> submeshes;
        ///< Total number of materials.
        uint32_t material_count = 0;
        ///< Skin data for this mesh.
        skin_bind_data skin_data;
        ///< True if skinning was baked into topology (vertex duplication + palette index encoding) during compilation.
        bool skin_is_prepared = false;
        ///< Bone palette bones per palette/submesh index. Used to restore palettes without running bind_skin.
        std::vector<std::vector<uint32_t>> bone_palette_bones;
        ///< Root node of the armature.
        std::unique_ptr<armature_node> root_node = nullptr;

        math::bbox bbox{};

        ///< Additional LOD levels (LOD 1, 2, 3, ...) with simplified index buffers
        std::vector<lod_load_data> lods;

        ///< UIDs of default materials generated during import, indexed by data_group_id.
        std::vector<hpp::uuid> default_material_uids;
    };

    /**
     * @brief Constructs a mesh object.
     */
    mesh();

    /**
     * @brief Destructor.
     */
    ~mesh();

    /**
     * @brief Clears out all the mesh data.
     */
    void dispose();

    auto get_info() const -> info;
    /**
     * @brief Binds the mesh data for rendering the selected batch of primitives.
     *
     * @param face_start The starting face index.
     * @param face_count The number of faces to render.
     * @param vertex_start The starting vertex index.
     * @param vertex_count The number of vertices to render.
     */
    void bind_render_buffers_for_submesh(const submesh* submesh, uint32_t lod_index = 0);

    /**
     * @brief Prepares the mesh with the specified vertex format.
     *
     * @param vertex_format The vertex format to use.
     * @return true If the mesh was successfully prepared.
     * @return false If the mesh preparation failed.
     */
    auto prepare_mesh(const gfx::vertex_layout& vertex_format) -> bool;

    /**
     * @brief Sets the source of the vertex buffer to pull data from while preparing the mesh.
     *
     * @param source The source vertex data.
     * @param vertex_count The number of vertices.
     * @param source_format The format of the source vertex data.
     * @return true If the vertex source was successfully set.
     * @return false If setting the vertex source failed.
     */
    auto set_vertex_source(void* source, uint32_t vertex_count, const gfx::vertex_layout& source_format) -> bool;
    auto set_vertex_source(byte_array_t&& source, uint32_t vertex_count, const gfx::vertex_layout& source_format)
        -> bool;

    auto set_bounding_box(const math::bbox& box) -> bool;

    auto set_submeshes(const std::vector<submesh>& submeshes) -> bool;

    /**
     * @brief Adds primitives (triangles) to the mesh.
     *
     * @param triangles The triangles to add.
     * @return true If the primitives were successfully added.
     * @return false If adding the primitives failed.
     */
    auto set_primitives(triangle_array_t&& triangles) -> bool;

    /**
     * @brief Binds the mesh as a skin with the specified skin binding data.
     *
     * @param bind_data The skin binding data.
     * @return true If the mesh was successfully bound as a skin.
     * @return false If binding the mesh as a skin failed.
     */
    auto bind_skin(const skin_bind_data& bind_data) -> bool;

    /**
     * @brief Binds the armature tree.
     *
     * @param root The root node of the armature.
     * @return true If the armature was successfully bound.
     * @return false If binding the armature failed.
     */
    auto bind_armature(std::unique_ptr<armature_node>& root) -> bool;

    auto load_mesh(load_data&& data) -> bool;

    /**
     * @brief Creates a plane geometry.
     *
     * @param format The vertex format.
     * @param width The width of the plane.
     * @param height The height of the plane.
     * @param width_segments The number of segments along the width.
     * @param height_segments The number of segments along the height.
     * @param origin The origin of the plane.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the plane was successfully created.
     * @return false If creating the plane failed.
     */
    auto create_plane(const gfx::vertex_layout& format,
                      float width,
                      float height,
                      uint32_t width_segments,
                      uint32_t height_segments,
                      mesh_create_origin origin,
                      bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a heightfield on the XZ plane (Y up): vertex grid (segments_x+1)*(segments_z+1).
     * @param heights Row-major samples indexed by z * (segments_x+1) + x, values in arbitrary units scaled by height_scale.
     * @param half_extent_x Half world size along X; vertices span [-half_extent_x, half_extent_x].
     * @param half_extent_z Half world size along Z.
     * @param height_scale Multiplier applied to each height sample before placing along Y.
     */
    auto create_heightfield(const gfx::vertex_layout& format,
                          hpp::span<const float> heights,
                          uint32_t segments_x,
                          uint32_t segments_z,
                          float half_extent_x,
                          float half_extent_z,
                          float height_scale,
                          mesh_create_origin origin,
                          bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a cube geometry.
     *
     * @param format The vertex format.
     * @param width The width of the cube.
     * @param height The height of the cube.
     * @param depth The depth of the cube.
     * @param width_segments The number of segments along the width.
     * @param height_segments The number of segments along the height.
     * @param depth_segments The number of segments along the depth.
     * @param origin The origin of the cube.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the cube was successfully created.
     * @return false If creating the cube failed.
     */
    auto create_cube(const gfx::vertex_layout& format,
                     float width,
                     float height,
                     float depth,
                     uint32_t width_segments,
                     uint32_t height_segments,
                     uint32_t depth_segments,
                     mesh_create_origin origin,
                     bool hardware_copy = true) -> bool;

    auto create_rounded_cube(const gfx::vertex_layout& format,
                    float width,
                    float height,
                    float depth,
                    uint32_t width_segments,
                    uint32_t height_segments,
                    uint32_t depth_segments,
                    mesh_create_origin origin,
                    bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a sphere geometry.
     *
     * @param format The vertex format.
     * @param radius The radius of the sphere.
     * @param stacks The number of stacks.
     * @param slices The number of slices.
     * @param origin The origin of the sphere.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the sphere was successfully created.
     * @return false If creating the sphere failed.
     */
    auto create_sphere(const gfx::vertex_layout& format,
                       float radius,
                       uint32_t stacks,
                       uint32_t slices,
                       mesh_create_origin origin,
                       bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a cylinder geometry.
     *
     * @param format The vertex format.
     * @param radius The radius of the cylinder.
     * @param height The height of the cylinder.
     * @param stacks The number of stacks.
     * @param slices The number of slices.
     * @param origin The origin of the cylinder.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the cylinder was successfully created.
     * @return false If creating the cylinder failed.
     */
    auto create_cylinder(const gfx::vertex_layout& format,
                         float radius,
                         float height,
                         uint32_t stacks,
                         uint32_t slices,
                         mesh_create_origin origin,
                         bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a capsule geometry.
     *
     * @param format The vertex format.
     * @param radius The radius of the capsule.
     * @param height The height of the capsule.
     * @param stacks The number of stacks.
     * @param slices The number of slices.
     * @param origin The origin of the capsule.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the capsule was successfully created.
     * @return false If creating the capsule failed.
     */
    auto create_capsule(const gfx::vertex_layout& format,
                        float radius,
                        float height,
                        uint32_t stacks,
                        uint32_t slices,
                        mesh_create_origin origin,
                        bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a cone geometry.
     *
     * @param format The vertex format.
     * @param radius The base radius of the cone.
     * @param radius_tip The tip radius of the cone.
     * @param height The height of the cone.
     * @param stacks The number of stacks.
     * @param slices The number of slices.
     * @param origin The origin of the cone.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the cone was successfully created.
     * @return false If creating the cone failed.
     */
    auto create_cone(const gfx::vertex_layout& format,
                     float radius,
                     float radius_tip,
                     float height,
                     uint32_t stacks,
                     uint32_t slices,
                     mesh_create_origin origin,
                     bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a torus geometry.
     *
     * @param format The vertex format.
     * @param outer_radius The outer radius of the torus.
     * @param inner_radius The inner radius of the torus.
     * @param bands The number of bands.
     * @param sides The number of sides.
     * @param origin The origin of the torus.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the torus was successfully created.
     * @return false If creating the torus failed.
     */
    auto create_torus(const gfx::vertex_layout& format,
                      float outer_radius,
                      float inner_radius,
                      uint32_t bands,
                      uint32_t sides,
                      mesh_create_origin origin,
                      bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a teapot geometry.
     *
     * @param format The vertex format.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the teapot was successfully created.
     * @return false If creating the teapot failed.
     */
    auto create_teapot(const gfx::vertex_layout& format, bool hardware_copy = true) -> bool;

    /**
     * @brief Creates an icosahedron geometry.
     *
     * @param format The vertex format.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the icosahedron was successfully created.
     * @return false If creating the icosahedron failed.
     */
    auto create_icosahedron(const gfx::vertex_layout& format, bool hardware_copy = true) -> bool;

    /**
     * @brief Creates a dodecahedron geometry.
     *
     * @param format The vertex format.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the dodecahedron was successfully created.
     * @return false If creating the dodecahedron failed.
     */
    auto create_dodecahedron(const gfx::vertex_layout& format, bool hardware_copy = true) -> bool;

    /**
     * @brief Creates an icosphere geometry.
     *
     * @param format The vertex format.
     * @param tesselation_level The level of tesselation.
     * @param hardware_copy Whether to use hardware copy.
     * @return true If the icosphere was successfully created.
     * @return false If creating the icosphere failed.
     */
    auto create_icosphere(const gfx::vertex_layout& format, int tesselation_level, bool hardware_copy = true) -> bool;

    /**
     * @brief Ends the preparation of the mesh and builds the render data.
     *
     * @param hardware_copy Whether to use hardware copy.
     * @param weld Whether to weld vertices.
     * @param optimize Whether to optimize the mesh.
     * @param build_buffers Whether to build the render buffers.
     * @return true If the mesh was successfully prepared.
     * @return false If the mesh preparation failed.
     */
    auto end_prepare(bool hardware_copy = true, bool build_buffers = true, bool weld = false, bool optimize = false)
        -> bool;

    /**
     * @brief Builds the internal vertex buffer.
     *
     * @param hardware_copy Whether to use hardware copy.
     */
    void build_vb(bool hardware_copy = true);

    /**
     * @brief Builds the internal index buffer.
     *
     * @param hardware_copy Whether to use hardware copy.
     */
    void build_ib(bool hardware_copy = true);

    /**
     * @brief Generates edge-triangle adjacency information for the mesh data.
     *
     * @param adjacency The adjacency information output array.
     * @return true If the adjacency information was successfully generated.
     * @return false If generating the adjacency information failed.
     */
    auto generate_adjacency(std::vector<uint32_t>& adjacency) -> bool;

    /**
     * @brief Determines the number of faces stored in the mesh.
     *
     * @return uint32_t The number of faces.
     */
    auto get_face_count() const -> uint32_t;

    /**
     * @brief Determines the number of vertices stored in the mesh.
     *
     * @return uint32_t The number of vertices.
     */
    auto get_vertex_count() const -> uint32_t;

    /**
     * @brief Retrieves the underlying vertex data from the mesh.
     *
     * @return uint8_t* The vertex data.
     */
    auto get_system_vb() -> uint8_t*;

    /**
     * @brief Retrieves the underlying index data from the mesh.
     *
     * @return uint32_t* The index data.
     */
    auto get_system_ib() -> uint32_t*;

    /**
     * @brief Retrieves the format of the underlying mesh vertex data.
     *
     * @return const gfx::vertex_layout& The vertex format.
     */
    auto get_vertex_format() const -> const gfx::vertex_layout&;

    /**
     * @brief Retrieves the skin bind data if this mesh has been bound as a skin.
     *
     * @return const skin_bind_data& The skin bind data.
     */
    auto get_skin_bind_data() const -> const skin_bind_data&;

    /**
     * @brief Retrieves the compiled bone combination palette data if this mesh has been bound as a skin.
     *
     * @return const bone_palette_array_t& The bone palette data.
     */
    auto get_bone_palettes() const -> const bone_palette_array_t&;

    /**
     * @brief Retrieves the armature tree of the mesh.
     *
     * @return const std::unique_ptr<armature_node>& The root node of the armature tree.
     */
    auto get_armature() const -> const std::unique_ptr<armature_node>&;

    /**
     * @brief Computes the accumulated node transform for each submesh.
     *
     * The mesh vertex buffer stores positions in node-local space; the renderer applies
     * each submesh's accumulated armature node transform (relative to the model root) on
     * top. This helper walks the armature tree and returns, for every submesh index, the
     * transform (relative to the model root) of the node that owns that submesh. Submeshes
     * not referenced by any node receive identity.
     *
     * @param lod_index The LOD level whose submesh count is used to size the result.
     * @return std::vector<math::transform> Per-submesh node transforms indexed by submesh index.
     */
    auto get_submesh_node_transforms(uint32_t lod_index = 0) const -> std::vector<math::transform>;

    /**
     * @brief Calculates the screen rectangle of the mesh based on its world transform and the camera.
     * This version uses a conservative approach - if any corners are behind the camera, it returns
     * the full viewport as the screen rect. Fast but may overestimate coverage.
     *
     * @param world The world transform of the mesh.
     * @param cam The camera.
     * @return irect32_t The screen rectangle.
     */
    auto calculate_screen_rect(const math::transform& world, const camera& cam) const -> irect32_t;

    /**
     * @brief Calculates the screen rectangle of the mesh with precise near-plane clipping.
     * This version properly clips edges that cross the near plane for accurate screen coverage.
     * More accurate but slightly more expensive than calculate_screen_rect.
     *
     * @param world The world transform of the mesh.
     * @param cam The camera.
     * @return irect32_t The screen rectangle.
     */
    auto calculate_screen_rect_precise(const math::transform& world, const camera& cam) const -> irect32_t;

    /**
     * @brief Retrieves information about the submesh of the mesh associated with the specified data group identifier.
     *
     * @param data_group_id The data group identifier.
     * @return const submesh* Pointer to the submesh information.
     */
    auto get_submeshes(uint32_t lod_index = 0) const -> const submesh_array_t&;
    auto get_submesh(uint32_t submesh_index = 0, uint32_t lod_index = 0) const -> const submesh*;
    /**
     * @brief Gets the local bounding box for this mesh.
     *
     * @return const math::bbox& The bounding box.
     */
    auto get_bounds() const -> const math::bbox&;

    /**
     * @brief Gets the preparation status for this mesh.
     *
     * @return mesh_status The preparation status.
     */
    auto get_status() const -> mesh_status;

    /**
     * @brief Gets the number of data groups(materials) for this mesh.
     *
     * @return size_t The number of data groups.
     */
    auto get_data_groups_count() const -> size_t;

    /**
     * @brief Gets the default material UIDs generated during import.
     * @return A constant reference to the vector of material UIDs, indexed by data_group_id.
     */
    auto get_default_material_uids() const -> const std::vector<hpp::uuid>&;

        /**
     * @brief Gets the imported materials generated during import.
     * @return A constant reference to the vector of imported materials.
     */
     auto get_imported_materials() const -> std::vector<asset_handle<material>>;
    /**
     * @brief Gets the number of submeshes for this mesh.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return size_t The number of submeshes.
     */
    auto get_submeshes_count(uint32_t lod_index = 0) const -> size_t;

    /// Submesh count at or above which per-submesh frustum culling is worthwhile.
    static constexpr size_t many_submeshes_threshold = 8;

    /**
     * @brief Returns true when this LOD has enough submeshes to benefit from per-submesh culling.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     */
    auto has_many_submeshes(uint32_t lod_index = 0) const -> bool
    {
        return get_submeshes_count(lod_index) >= many_submeshes_threshold;
    }

    /**
     * @brief Gets the number of skinned submeshes for this mesh.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return size_t The number of skinned submeshes.
     */
    auto get_skinned_submeshes_count(uint32_t lod_index = 0) const -> size_t;
    /**
     * @brief Gets the indices of skinned submeshes for a specific data group.
     * @param data_group_id The data group identifier.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return const submesh_array_indices_t& The indices of skinned submeshes.
     */
    auto get_skinned_submeshes_indices(uint32_t data_group_id, uint32_t lod_index = 0) const -> const submesh_array_indices_t&;

    /**
     * @brief Gets the number of non-skinned submeshes for this mesh.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return size_t The number of non-skinned submeshes.
     */
    auto get_non_skinned_submeshes_count(uint32_t lod_index = 0) const -> size_t;
    /**
     * @brief Gets the indices of non-skinned submeshes for a specific data group.
     * @param data_group_id The data group identifier.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return const submesh_array_indices_t& The indices of non-skinned submeshes.
     */
    auto get_non_skinned_submeshes_indices(uint32_t data_group_id, uint32_t lod_index = 0) const -> const submesh_array_indices_t&;

    /**
     * @brief Gets the index of a submesh within the submesh array.
     * @param s Pointer to the submesh to find.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return Index of the submesh, or -1 if not found.
     */
    auto get_submesh_index(const submesh* s, uint32_t lod_index = 0) const -> int;

    /**
     * @brief Gets the number of LOD levels available.
     * @return Number of LODs (including base LOD 0)
     */
    auto get_lod_count() const -> uint32_t;

    /**
     * @brief Gets submeshes for a specific LOD level.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return Pointer to submesh array, or nullptr if LOD doesn't exist
     */
    auto get_lod_submeshes(uint32_t lod_index) const -> const submesh_array_t*;

    /**
     * @brief Gets the face count for a specific LOD level.
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @return Face count, or 0 if LOD doesn't exist
     */
    auto get_lod_face_count(uint32_t lod_index) const -> uint32_t;

    /**
     * @brief Gets index buffer data for a specific LOD level (for serialization).
     * @param lod_index LOD index (0 = base, 1+ = simplified)
     * @param out_indices Output vector to store index data
     * @param out_error Output simplification error
     * @return true if LOD data was retrieved successfully
     */
    auto get_lod_index_data(uint32_t lod_index, std::vector<uint32_t>& out_indices, float& out_error) const -> bool;


    /**
     * @brief Gets the maximum number of LODs that can be used for this mesh including base LOD.
     * @return The maximum number of LODs that can be used.
     */
    static auto get_max_lod_count() -> uint32_t;
    /**
     * @brief Gets the maximum number of LODs that can be generated for a mesh.
     * @return The maximum number of LODs that can be generated.
     */
    static auto get_max_generated_lod_count() -> uint32_t;
    /**
     * @brief Generates default LOD configurations based on mesh triangle count.
     * @param data The load_data to generate LOD configs for
     * @param target_error Base target error for LOD generation (default: 0.01)
     * @return Vector of LOD configurations: {target_triangle_count, target_error}
     */
    static auto generate_default_lod_configs(const load_data& data, float target_error = 0.01f) -> std::vector<std::pair<size_t, float>>;

    /**
     * @brief Applies skinning vertex duplication to load_data (for offline LOD generation).
     * @param data The load_data to apply skinning to (will be modified)
     * @return true if skinning was applied successfully
     */
    static auto apply_skin_to_load_data(load_data& data) -> bool;

    /**
     * @brief Generates LOD levels directly in load_data without creating GPU buffers.
     * @param data The load_data to generate LODs for (will be modified)
     * @param lod_configs Vector of LOD configurations: {target_triangle_count, target_error}
     * @param options meshoptimizer simplification options (default: 0)
     * @return true if LODs were generated successfully
     */
    static auto generate_lods_for_load_data(load_data& data, const std::vector<std::pair<size_t, float>>& lod_configs) -> bool;

    /**
     * @brief Restores LOD levels from load_data into the mesh.
     * @param data The load_data containing LOD information
     * @return true if LODs were restored successfully
     */
    auto restore_lods_from_load_data(const load_data& data) -> bool;

    struct preparation_data
    {
        enum flags
        {
            source_contains_normal = 0x1,
            source_contains_binormal = 0x2,
            source_contains_tangent = 0x4
        };

        ///< The source vertex data currently being used to prepare the mesh.
        uint8_t* vertex_source{nullptr};
        ///< Whether the source data is owned by this object.
        bool owns_source{false};
        ///< The format of the vertex data currently being used to prepare the mesh.
        gfx::vertex_layout source_format;
        ///< Records the location in the vertex buffer that each vertex has been placed during data insertion.
        std::vector<uint32_t> vertex_records;
        ///< Final vertex buffer currently being prepared.
        byte_array_t vertex_data;
        ///< Additional descriptive information about the vertices.
        byte_array_t vertex_flags;
        ///< Stores the current face/triangle data.
        triangle_array_t triangle_data;
        ///< Total number of triangles currently stored.
        uint32_t triangle_count{0};
        ///< Total number of vertices currently stored.
        uint32_t vertex_count{0};
        ///< Prepared substs information
        std::vector<submesh> submeshes;
        ///< Whether to compute vertex normals.
        bool compute_normals{false};
        ///< Whether to compute vertex binormals.
        bool compute_binormals{false};
        ///< Whether to compute vertex tangents.
        bool compute_tangents{false};
        ///< Whether to compute vertex barycentric coordinates.
        bool compute_barycentric{false};

        bool check_for_degenerates{false};

        bool compute_per_triangle_material_data{false};
    };

protected:
    struct optimizer_vertex_info
    {
        ///< The position of the vertex in the pseudo-cache.
        int32_t cache_position{-1};
        ///< The score associated with this vertex.
        float vertex_score{0.0f};
        ///< Total number of triangles that reference this vertex that have not yet been added.
        uint32_t unused_triangle_references{0};
        ///< List of all triangles referencing this vertex.
        std::vector<uint32_t> triangle_references;
    };

    struct optimizer_triangle_info
    {
        ///< The sum of all three child vertex scores.
        float triangle_score{0.0f};
        ///< Whether the triangle has been added to the draw list.
        bool added{false};
    };

    struct adjacent_edge_key
    {
        ///< Pointer to the first vertex in the edge.
        const math::vec3* vertex1{nullptr};
        ///< Pointer to the second vertex in the edge.
        const math::vec3* vertex2{nullptr};
    };

    struct mesh_submesh_key
    {
        ///< The data group identifier for this submesh.
        uint32_t data_group_id{0};
    };

    using submesh_key_map_t = std::map<mesh_submesh_key, submesh*>;
    using submesh_key_array_t = std::vector<mesh_submesh_key>;

    struct weld_key
    {
        ///< Pointer to the vertex.
        uint8_t* vertex{nullptr};
        ///< Format of the vertex.
        gfx::vertex_layout format;
        ///< Tolerance for welding vertices.
        float tolerance{};
    };

    struct face_influences
    {
        ///< List of unique bones that influence a given number of faces.
        bone_palette::bone_index_map_t bones;
    };

    struct bone_combination_key
    {
        ///< Pointer to the face influences.
        face_influences* influences{nullptr};
        ///< The data group identifier.
        uint32_t data_group_id{0};
    };

    using bone_combination_map_t = std::map<bone_combination_key, std::vector<uint32_t>*>;

    friend auto operator<(const adjacent_edge_key& key1, const adjacent_edge_key& key2) -> bool;
    friend auto operator<(const mesh_submesh_key& key1, const mesh_submesh_key& key2) -> bool;
    friend auto operator<(const weld_key& key1, const weld_key& key2) -> bool;
    friend auto operator<(const bone_combination_key& key1, const bone_combination_key& key2) -> bool;

    void check_for_degenerates();

    /**
     * @brief Generates any vertex components that may be missing, such as normals, tangents, or binormals.
     *
     * @param weld Whether to weld vertices.
     * @return true If the vertex components were successfully generated.
     * @return false If generating the vertex components failed.
     */
    auto generate_vertex_components(bool weld) -> bool;

    /**
     * @brief Generates vertex normals for the mesh.
     *
     * @param adjacency_ptr Pointer to the adjacency information.
     * @param remap_array_ptr Pointer to the vertex remap array.
     * @return true If the vertex normals were successfully generated.
     * @return false If generating the vertex normals failed.
     */
    auto generate_vertex_normals(uint32_t* adjacency_ptr, std::vector<uint32_t>* remap_array_ptr = nullptr) -> bool;

    /**
     * @brief Generates vertex barycentric coordinates for the mesh.
     *
     * @param adjacency Pointer to the adjacency information.
     * @return true If the vertex barycentric coordinates were successfully generated.
     * @return false If generating the vertex barycentric coordinates failed.
     */
    auto generate_vertex_barycentrics(uint32_t* adjacency) -> bool;

    /**
     * @brief Generates vertex tangents for the mesh.
     *
     * @return true If the vertex tangents were successfully generated.
     * @return false If generating the vertex tangents failed.
     */
    auto generate_vertex_tangents() -> bool;

    /**
     * @brief Welds the vertices together that can be combined.
     *
     * @param tolerance The tolerance for welding vertices.
     * @param vertexRemap Pointer to the vertex remap array.
     * @return true If the vertices were successfully welded.
     * @return false If welding the vertices failed.
     */
    auto weld_vertices(float tolerance = 0.000001f, std::vector<uint32_t>* vertex_remap_ptr = nullptr) -> bool;

    /**
     * @brief Sorts the data in the mesh into material and data group order.
     *
     * @return true If the mesh data was successfully sorted.
     * @return false If sorting the mesh data failed.
     */
    auto sort_mesh_data() -> bool;


protected:
    ///< Whether to force the generation of tangent space vectors.
    bool force_tangent_generation_ = false;
    ///< Whether to force the generation of vertex normals.
    bool force_normal_generation_ = false;
    ///< Whether to force the generation of vertex barycentric coordinates.
    bool force_barycentric_generation_ = false;
    ///< Whether to disable the automatic re-sort operation.
    bool disable_final_sort_ = false;

    ///< The vertex data during data insertion and system memory copy.
    uint8_t* system_vb_ = nullptr;
    ///< The vertex format used for the mesh internal vertex data.
    gfx::vertex_layout vertex_format_;
    ///< The final system memory copy of the index buffer (LOD 0 - base mesh).
    uint32_t* system_ib_ = nullptr;
    ///< Material and data group information for each triangle.
    submesh_key_array_t triangle_data_;
    ///< The actual hardware vertex buffer resource (shared by all LODs).
    std::shared_ptr<void> hardware_vb_;
    ///< The actual hardware index buffer resource (LOD 0 - base mesh).
    std::shared_ptr<void> hardware_ib_;

    ///< Additional LOD levels (LOD 1, 2, 3, ...) with simplified index buffers.
    std::vector<lod_level> lods_;
    ///< Current LOD count (including base LOD 0).
    uint32_t lod_count_ = 1;

    ///< The actual list of submeshes maintained by this mesh.
    submesh_array_t mesh_submeshes_;

    ///< Indices in the subset array which are skinned
    submesh_array_map_t skinned_submesh_indices_;
    size_t skinned_submesh_count_{};

    ///< Indices in the subset array which are not skinned
    submesh_array_map_t non_skinned_submesh_indices_;
    size_t non_skinned_submesh_count_{};

    ///< Lookup information mapping data groups to submeshes batched by material.
    data_group_submesh_map_t data_groups_;

    ///< Whether the mesh uses a hardware vertex/index buffer.
    bool hardware_mesh_ = true;
    ///< Whether the mesh was optimized when it was prepared.
    bool optimize_mesh_ = false;
    ///< Axis aligned bounding box describing object dimensions in object space.
    math::bbox bbox_;
    ///< Total number of faces in the prepared mesh.
    uint32_t face_count_ = 0;
    ///< Total number of vertices in the prepared mesh.
    uint32_t vertex_count_ = 0;

    ///< Preparation status of the mesh.
    mesh_status prepare_status_ = mesh_status::not_prepared;
    ///< Input data used for constructing the final mesh.
    preparation_data preparation_data_;

    ///< Data describing how the mesh should be bound as a skin with supplied bone matrices.
    skin_bind_data skin_bind_data_;
    ///< List of unique combinations of bones to use during rendering.
    bone_palette_array_t bone_palettes_;
    ///< List of armature nodes.
    std::unique_ptr<armature_node> root_ = nullptr;

    ///< UIDs of default materials generated during import, indexed by data_group_id.
    std::vector<hpp::uuid> default_material_uids_;
};

} // namespace unravel
