#pragma once

#include <engine/engine_export.h>
#include <engine/rendering/gi/global_sdf_clipmap.h>
#include <engine/rendering/gi/global_sdf_clipmap_gpu.h>
#include <engine/rendering/gi/radiance_cache_gpu.h>
#include <engine/rendering/gi/sdf_atlas.h>
#include <engine/rendering/gi/sdf_instance_grid.h>
// For material::sptr, which is a nested typedef and so needs the complete type.
#include <engine/rendering/material.h>
#include <engine/rendering/gpu_light_buffer.h>

#include <context/context.hpp>
#include <hpp/uuid.hpp>
#include <math/math.h>

#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace unravel
{

class scene;
class mesh;
class model;
class model_component;

/**
 * @brief World state for surface cache global illumination.
 *
 * Owns the distance field residency shared by every camera, which is why it lives in
 * @c rtti::context rather than in a @c gfx::render_view: the fields describe the world, not
 * a view of it, and a scene rendered by three cameras must not upload them three times.
 *
 * Responsibilities in this phase:
 *   - make a mesh's baked field resident on first use, keyed by asset uid;
 *   - rebuild the per-frame instance list the tracer transforms rays through.
 */
class surface_cache_service
{
public:
    /// One resident field placed in the world. Mirrors the GPU instance layout.
    struct instance
    {
        ///< World to local transform, so a ray can be moved into field space.
        math::mat4 world_to_local{1.0f};
        ///< Local to world, for turning a local hit back into a world position.
        math::mat4 local_to_world{1.0f};
        ///< World-space bounds of the field, for broad-phase rejection.
        math::bbox world_bounds{};
        ///< Index into the atlas header buffer.
        uint32_t header_index = sdf_atlas::invalid_index;
        ///< Uniform scale factor applied to distances sampled in local space. Non-uniform
        ///< scale uses the smallest axis, which keeps the field conservative (a sphere trace
        ///< under-steps rather than overshooting through geometry).
        float local_to_world_scale = 1.0f;
        ///< Diffuse colour of this placement's material, and its emission.
        ///
        ///< A distance field carries geometry only, so a cell first discovered by a BOUNCE ray
        ///< has no material and has to fall back to a neutral grey until an on-screen pixel
        ///< registers one -- which never happens for anything the camera does not look at. The
        ///< instance is where the material can be recovered without storing material voxels: a
        ///< submesh is drawn with exactly one material, and one field is baked per submesh, so
        ///< the mapping is already one to one.
        ///
        ///< Averages over the material's maps are NOT included; this is the base colour factor,
        ///< so a texture-dominated material is approximated by its tint.
        math::vec3 albedo{0.5f, 0.5f, 0.5f};
        math::vec3 emissive{0.0f, 0.0f, 0.0f};
    };

    auto init(rtti::context& ctx) -> bool;
    auto deinit(rtti::context& ctx) -> bool;

    /**
     * @brief Rebuilds the world-space state for this frame and flushes pending atlas uploads.
     *
     * Walks every model in the scene, not just the visible set: geometry behind the camera
     * still bounces light, and excluding it would reintroduce exactly the offscreen blindness
     * the screen-space path suffers from.
     *
     * Takes no camera, and that is the point of the split. Everything here is a function of the
     * WORLD, so it is identical for every camera and is skipped after the first call in a frame --
     * with two cameras it used to rebuild the same instance list and re-upload the same grid twice.
     * The camera-dependent half is @ref surface_cache_view.
     */
    void update_world(scene& scn);

    /// Placements the cascade composes from. Rebuilt by @ref update_world; borrowed by each
    /// camera's @ref surface_cache_view, which must therefore compose within the same frame.
    auto get_clipmap_instances() const -> const std::vector<global_sdf_instance>&
    {
        return clipmap_instances_;
    }

    auto get_atlas() -> sdf_atlas&
    {
        return atlas_;
    }

    auto get_light_buffer() const -> const gpu_light_buffer&
    {
        return light_buffer_;
    }

    auto get_radiance_cache() -> radiance_cache_gpu&
    {
        return cache_gpu_;
    }

    /// Per-frame instance list packed for the tracer. Owned here rather than by a pass, since
    /// every pass that traces needs the same one and packing it twice would be wasted work.
    auto get_instance_buffer() const -> gfx::dynamic_vertex_buffer_handle
    {
        return instance_buffer_;
    }

    /// vec4 elements per packed instance. Must match SDF_INSTANCE_STRIDE in gi/sdf_common.sh.
    /// Two of the ten carry the material; emission is HDR, so it gets its own vec4 rather than
    /// being packed into a spare component.
    static constexpr uint32_t instance_vec4_stride = 10;

    /// CSR offsets of the instance cull grid, one entry per cell plus a terminator.
    auto get_grid_offset_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return grid_offset_buffer_;
    }

    /// Instance indices the cull grid's cells refer to.
    auto get_grid_instance_buffer() const -> gfx::dynamic_index_buffer_handle
    {
        return grid_instance_buffer_;
    }

    auto get_instance_grid() const -> const sdf_instance_grid&
    {
        return grid_;
    }

    /**
     * @brief The two vec4s every tracer binds to address the cull grid.
     *
     * [0] = grid origin xyz, cell size w. [1] = cell counts xyz, non-zero w when the grid is
     * usable. Built here for the same reason the clipmap's sampling parameters are: several
     * passes traverse this grid and any disagreement between them changes which instances a ray
     * finds, which does not fail loudly -- it just means some geometry stops occluding for one
     * pass and not another.
     */
    auto get_grid_params() const -> const float*
    {
        return grid_params_.data();
    }

    auto get_instances() const -> const std::vector<instance>&
    {
        return instances_;
    }

    auto is_enabled() const -> bool
    {
        return enabled_ && atlas_.is_valid();
    }

    void set_enabled(bool enabled)
    {
        enabled_ = enabled;
    }

private:
    /// Generation value no atlas ever reports, marking a field that has never been attempted so the
    /// first try always happens regardless of what the atlas has released.
    static constexpr uint32_t never_attempted = 0xFFFFFFFFu;

    /// Residency record for one mesh asset.
    struct mesh_residency
    {
        uint32_t header_index = sdf_atlas::invalid_index;
        ///< Set when the mesh has no baked field at all. PERMANENT, and deliberately distinct from
        ///< the atlas refusing an upload for want of room: that is a statement about the atlas at
        ///< one moment, not about the mesh, and it stops being true as soon as anything is
        ///< released. Conflating the two is what left a scene's meshes excluded from GI forever
        ///< after a busier scene had filled the atlas once.
        bool has_no_field = false;
        ///< World frame this was last asked for. Anything not asked for in the current frame is
        ///< released, which is the only thing that ever returns bricks to the atlas.
        uint64_t last_used_frame = 0;
        ///< Atlas release generation at the last upload attempt, or @ref never_attempted.
        ///
        ///< A refusal for want of room is retried only once the atlas has actually freed something,
        ///< because nothing else can change the answer. Retrying every frame instead is not merely
        ///< wasteful: a scene that overruns the atlas refuses thousands of meshes, so it re-attempts
        ///< thousands of doomed uploads per frame, and the refusal counters climb into the billions.
        uint32_t attempt_generation = never_attempted;
    };

    /**
     * @brief Returns the header index for a mesh, uploading its field on first use.
     *
     * Also marks the record as used this frame, which is what keeps it out of the sweep at the end
     * of @ref update_world.
     */
    auto acquire_field(const hpp::uuid& mesh_uid, const mesh& m, uint32_t submesh_index) -> uint32_t;

    /**
     * @brief Releases every field nothing referenced this frame.
     *
     * The atlas is keyed by mesh ASSET and has no other reclamation path, so without this it only
     * ever grows: loading a second scene keeps the first scene's bricks resident, the new scene's
     * meshes are refused for want of room, and GI silently does not run at all -- every field is
     * refused, so the instance list comes out empty and every pass early-outs.
     */
    void release_unused_fields();

    /**
     * @brief Appends one placement of a resident field to this frame's instance list.
     * @param local_to_world The transform the RENDERER draws the geometry with, which for a
     *        model with submesh nodes is the node's transform, not the model root's.
     * @param mat The material this submesh is DRAWN with, override included. Null falls back to a
     *        neutral albedo; it must resolve the same way the renderer does, or a bounce would
     *        tint light with a colour the surface is not actually painted.
     */
    void add_instance(uint32_t header_index,
                      const mesh_sdf& sdf,
                      const math::mat4& local_to_world,
                      const std::shared_ptr<mesh>& owner,
                      const material::sptr& mat);

    /**
     * @brief The material a submesh is drawn with, resolving per-submesh overrides first.
     *
     * Mirrors resolve_submesh_material in model.cpp. The renderer is the authority on what colour
     * a surface actually is, so bouncing light off a different one would tint the scene with a
     * material nothing on screen is painted with.
     */
    static auto resolve_submesh_material(const model& mdl,
                                         const model_component& model_comp,
                                         const mesh& m,
                                         uint32_t submesh_index) -> material::sptr;

    /**
     * @brief Packs the instance list into the layout SdfLoadInstance expects and uploads it.
     *
     * Transforms are written as the three rows of an affine 3x4 rather than as a mat4, so the
     * GPU side has no matrix-convention ambiguity to get wrong. See the note in sdf_common.sh.
     */
    void upload_instances();

    sdf_atlas atlas_;
    gpu_light_buffer light_buffer_;
    radiance_cache_gpu cache_gpu_;
    /// Identifies one submesh's field. Residency is per SUBMESH, not per mesh: each submesh has
    /// its own field and is uploaded to the atlas independently.
    struct field_key
    {
        hpp::uuid mesh_uid{};
        uint32_t submesh_index{};

        auto operator==(const field_key& other) const -> bool
        {
            return mesh_uid == other.mesh_uid && submesh_index == other.submesh_index;
        }
    };

    struct field_key_hash
    {
        auto operator()(const field_key& key) const -> size_t
        {
            const size_t uid_hash = std::hash<hpp::uuid>{}(key.mesh_uid);
            return uid_hash ^ (size_t(key.submesh_index) * 0x9e3779b97f4a7c15ull);
        }
    };

    std::unordered_map<field_key, mesh_residency, field_key_hash> residency_;
    std::vector<instance> instances_;
    /// Clipmap composition input, rebuilt each frame alongside @ref instances_.
    std::vector<global_sdf_instance> clipmap_instances_;
    /// Keeps every mesh referenced by @ref clipmap_instances_ alive for the duration of
    /// composition. The composer borrows raw mesh_sdf pointers, so an asset unloading
    /// mid-compose would otherwise dangle.
    std::vector<std::shared_ptr<mesh>> clipmap_keepalive_;
    /**
     * @brief Rebuilds the instance cull grid and uploads it.
     *
     * Rebuilt in full every frame because the instance list is. The grid holds no state worth
     * carrying forward, so there is nothing to invalidate and no staleness to reason about.
     */
    void upload_instance_grid();

    /// Packed instance data and its GPU mirror, rebuilt each frame.
    gfx::dynamic_vertex_buffer_handle instance_buffer_{bgfx::kInvalidHandle};
    uint32_t instance_buffer_capacity_ = 0;
    std::vector<float> instance_data_;
    /// Broad-phase over @ref instances_, so a ray tests the instances near it rather than all of
    /// them. Rebuilt with the instance list each frame.
    sdf_instance_grid grid_;
    std::vector<math::bbox> grid_bounds_;
    gfx::dynamic_index_buffer_handle grid_offset_buffer_{bgfx::kInvalidHandle};
    gfx::dynamic_index_buffer_handle grid_instance_buffer_{bgfx::kInvalidHandle};
    uint32_t grid_offset_capacity_ = 0;
    uint32_t grid_instance_capacity_ = 0;
    std::array<float, 8> grid_params_{};
    bool enabled_ = true;
    /// Frame the world state was last rebuilt in, so several cameras in one frame share one
    /// rebuild. Starts at a value no frame counter produces, so the first call always runs.
    uint64_t world_frame_ = std::numeric_limits<uint64_t>::max();
};

} // namespace unravel
