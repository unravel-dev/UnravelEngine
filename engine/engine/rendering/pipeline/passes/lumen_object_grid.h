#pragma once

#include <engine/engine_export.h>

#include <engine/rendering/gpu_program.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/graphics.h>

#include <array>

namespace unravel
{

class surface_cache_system;
class surface_cache_view;

/**
 * @brief The GI object grid of one camera: per cell of 2 x 2 x 2 voxels of the camera's global distance field clipmap,
 *        the four GI instances whose surfaces are nearest within reach (cs_lumen_object_grid.sc), through whose cards
 *        every global-SDF hit sampler shades a hit (lumen_surface_cache.sh).
 *
 * The grid follows its camera's clipmap levels, so each camera keeps its own while the cards and the atlases are
 * shared (lumen_surface_cache). A level is rebuilt when its clipmap level recomposed: in its boxes after a partial
 * recompose, in full otherwise, two full levels per frame at most - every level at once when the instance order moved,
 * since the cells hold instance indices.
 */
class lumen_object_grid
{
public:
    lumen_object_grid() = default;
    ~lumen_object_grid();
    lumen_object_grid(const lumen_object_grid&) = delete;
    auto operator=(const lumen_object_grid&) -> lumen_object_grid& = delete;

    auto init(rtti::context& ctx) -> bool;
    auto is_ready() const -> bool;

    /// Brings the grid up to the camera's clipmap (@p view_cache) and this frame's instances (@p gi_scene).
    void update(const surface_cache_system& gi_scene, const surface_cache_view& view_cache);

    /// Frees the grid; the next update builds it again.
    void release();

    /// The grid exists (the first update built it).
    auto has_grid() const -> bool
    {
        return texture_ != nullptr;
    }

    /// Binds the grid at @p stage (a 1 x 1 x 1 stand-in before it exists: a 3D stage must stay 3D) and sets
    /// u_lumen_object_grid_levels and u_lumen_object_grid_params with the cards' sampling bias scale
    /// @p card_bias_scale.
    void bind(uint8_t stage, float card_bias_scale) const;

    auto get_texture() const -> const gfx::texture::ptr&
    {
        return texture_;
    }

    /// u_lumen_object_grid_levels: per level xyz = the cell (0, 0, 0) corner, w = the cell size.
    auto get_levels() const -> const std::array<math::vec4, 4>&
    {
        return levels_;
    }

    /// u_lumen_object_grid_params: x = the cells per axis, y = @p card_bias_scale.
    auto get_params(float card_bias_scale) const -> math::vec4;

private:
    struct uniforms : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_object_grid;
        gfx::program::uniform_ptr u_lumen_object_grid_origin;
        gfx::program::uniform_ptr u_brick_dispatch;
        gfx::program::uniform_ptr u_lumen_object_grid_levels;
        gfx::program::uniform_ptr u_lumen_object_grid_params;
        gfx::program::uniform_ptr s_lumen_object_grid;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
    };

    /// The grid's state for one clipmap level: built for this origin and content.
    struct built_level
    {
        math::vec3 origin{0.0f};
        float voxel_size = 0.0f;
        /// The clipmap level's global_sdf_clipmap::level::compose_serial when built.
        uint64_t compose_serial = 0;
        /// surface_cache_system::get_instance_order_hash when built: the cells store instance indices.
        uint64_t instance_order = 0;
        bool is_built = false;
    };

    void bind_sdf_instances(const surface_cache_system& gi_scene) const;

    uniforms uniforms_;
    gpu_program::ptr program_;
    gfx::texture::ptr texture_;
    ///< A 1x1x1 stand-in bound while no grid exists.
    gfx::texture::ptr dummy_;
    ///< The frame's brick table (global_sdf_clipmap::append_brick_boxes, in cells).
    bgfx::DynamicVertexBufferHandle boxes_{bgfx::kInvalidHandle};
    std::array<built_level, 4> built_{};
    std::array<math::vec4, 4> levels_{};
    uint32_t resolution_ = 0;
    ///< The grid's id range has been reported as too small for the scene (once).
    bool has_warned_ids_ = false;
};

} // namespace unravel
