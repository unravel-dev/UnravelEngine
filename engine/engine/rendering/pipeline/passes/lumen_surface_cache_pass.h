#pragma once

#include <engine/engine_export.h>

#include <engine/rendering/pipeline/passes/lumen_object_grid.h>
#include <engine/rendering/pipeline/passes/lumen_surface_cache.h>

#include <memory>
#include <vector>

namespace unravel
{

/**
 * @brief One camera's handle on the GI surface cache: the process's shared lumen_surface_cache (cards, atlases,
 *        lighting) and the camera's own object grid (lumen_object_grid), which follows the camera's global distance
 *        field.
 *
 * Per frame, for a camera running the GI: update() records the camera as a viewer and claims the frame's update of
 * the shared cache; the camera that wins it rasterizes the captures (the deferred pipeline) and copies them; every
 * camera brings its object grid up to date (update_object_grid) and lights the pages nearest to it (light) before its
 * samplers read the cache. release_targets() leaves the cache (the last camera to leave frees its atlases) and frees
 * the object grid.
 */
class lumen_surface_cache_pass
{
public:
    using capture_view = lumen_surface_cache::capture_view;
    using cloud_shadow = lumen_surface_cache::cloud_shadow;
    using lighting_inputs = lumen_surface_cache::lighting_inputs;
    using debug_mode = lumen_surface_cache::debug_mode;
    using debug_params = lumen_surface_cache::debug_params;

    lumen_surface_cache_pass() = default;
    ~lumen_surface_cache_pass();
    lumen_surface_cache_pass(const lumen_surface_cache_pass&) = delete;
    auto operator=(const lumen_surface_cache_pass&) -> lumen_surface_cache_pass& = delete;

    /// Takes the shared cache (whose uniforms the samplers' programs need before they link on OpenGL) and initializes
    /// the object grid.
    auto init(rtti::context& ctx) -> bool;

    /// This frame's cloud shadow, before update().
    void set_cloud_shadow(const cloud_shadow& shadow);

    /**
     * @brief Joins the shared cache's users, records this camera's viewer (with its global distance field @p clipmap)
     *        and, for the frame's first camera, updates the cache (lumen_surface_cache::update).
     * @return This camera updated the cache this frame: it rasterizes the captures and copies them.
     */
    auto update(const surface_cache_system& gi_scene,
                const global_sdf_clipmap* clipmap,
                const math::vec3& view_origin,
                const math::frustum& view_frustum,
                const gi_settings::scene_settings& view_settings,
                const gi_project_settings& project_settings) -> bool;

    /// Brings this camera's object grid up to its global distance field and this frame's instances.
    void update_object_grid(const surface_cache_system& gi_scene, const surface_cache_view& view_cache);

    /// The shared card scene (after update()).
    auto get_scene() const -> const lumen_scene&;

    /// lumen_scene::get_visualized_cards over this frame's placements.
    void get_visualized_cards(const math::vec3& view_origin,
                              float distance,
                              const math::frustum& view_frustum,
                              std::vector<lumen_scene::visualized_card>& out) const;

    auto get_capture_target() const -> const gfx::frame_buffer::ptr&;
    auto compute_capture_view(const lumen_scene::capture& cap) const -> capture_view;
    void copy_captures();
    /// Lights the pages the schedule gave this camera, with its global distance field and object grid (@p inputs'
    /// own grid is ignored).
    void light(lighting_inputs inputs);
    /// The debug views with this camera's object grid (@p params' own is ignored).
    auto run_debug(debug_params params) -> bool;

    auto is_ready() const -> bool;
    /// The shared atlases exist.
    auto has_targets() const -> bool;
    /// Leaves the shared cache and frees the object grid: a camera whose GI turned off holds nothing of it.
    void release_targets();

    auto get_scene_buffer() const -> bgfx::DynamicVertexBufferHandle;
    auto get_final_atlas() const -> const gfx::texture::ptr&;
    auto get_depth_atlas() const -> const gfx::texture::ptr&;
    auto get_radiosity_sh(uint32_t channel) const -> const gfx::texture::ptr&;
    auto get_radiosity_layout() const -> const lumen_pass::radiosity_layout&;
    void get_visualized_pages(std::vector<math::vec4>& out) const;

    auto get_object_grid() const -> const gfx::texture::ptr&
    {
        return object_grid_.get_texture();
    }
    auto get_object_grid_levels() const -> const std::array<math::vec4, 4>&
    {
        return object_grid_.get_levels();
    }
    auto get_object_grid_params() const -> math::vec4;
    auto get_surface_cache_params() const -> math::vec4;

    /// The cards have been lit and this camera's object grid exists.
    auto has_lighting() const -> bool;

    /// The reflections' surface cache feedback, null while it cannot run.
    auto get_feedback() -> lumen_surface_cache_feedback*;
    auto get_card_index_revision() const -> uint64_t;

    /// lumen_surface_cache::bind_for_sampling with this camera's object grid.
    void bind_for_sampling(uint8_t scene_stage, uint8_t final_stage, uint8_t grid_stage, bool enabled) const;

private:
    std::shared_ptr<lumen_surface_cache> shared_;
    lumen_object_grid object_grid_;
    /// This camera is one of the shared cache's users (update() since the last release_targets()).
    bool is_user_ = false;
};

} // namespace unravel
