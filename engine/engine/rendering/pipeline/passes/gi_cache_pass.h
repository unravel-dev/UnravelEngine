#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/surface_cache_service.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Populates and lights the world-space radiance cache.
 *
 * Two dispatches with different natural parallelism. Insertion runs per PIXEL, because that is
 * where surfaces are discovered; lighting runs per ENTRY, because a cell must be lit once
 * however many pixels resolve to it, and because one thread per entry makes the accumulation
 * race-free without atomics.
 *
 * Lighting per entry also means an entry keeps being lit after its surface leaves the screen,
 * so it stays valid and can be read back instantly when the surface returns -- instead of
 * converging from nothing, which is what a screen-space history has to do.
 */
class gi_cache_pass
{
public:
    struct settings
    {
        /// Pixel stride for insertion. Cells are far larger than a pixel, so sampling every
        /// pixel would resolve the same handful of cells thousands of times over.
        float insert_stride = 4.0f;
        /// Surfaces beyond this distance are not registered. Their cells would be enormous and
        /// contribute almost nothing, while still costing an entry each.
        float insert_max_distance = 200.0f;
        /// How far the recorded point is lifted along its normal before lighting, as a FRACTION OF
        /// THIS ENTRY'S CELL.
        ///
        /// The stored point is not the sampled surface: insertion snaps it to the cell grid, so it
        /// can sit up to a cell inside the geometry it represents. That error scales with the cell,
        /// which runs 0.25 m at level 0 to 2 m at the level cap, so a fixed world distance cannot
        /// cover both -- at the 0.05 world this used to be, distant entries shaded from inside their
        /// own surface and converged to black.
        ///
        /// Wrong in either direction and neither announces itself. Too small and the far field goes
        /// black, which is indistinguishable from correct shadowing in the lit image. Too large and
        /// the entry floats off its surface, shadow rays sail over nearby occluders, and everything
        /// reads over-lit. Check it in the cache debug view, not the lit one.
        float surface_offset_cells = 0.5f;
        /// Floor on the accumulation blend weight, so a mature entry keeps following change.
        float min_alpha = 0.05f;
        /// Cap on accumulated samples, which sets how fast a converged entry can still move.
        float max_samples = 32.0f;
        /// Bounce rays cast per entry per frame.
        ///
        /// One is enough. The result feeds a running mean, so successive frames explore different
        /// directions and the estimate converges over time rather than within a frame -- cost
        /// stays fixed while the effective sample count grows. These rays are also what create
        /// entries for geometry the camera has never seen.
        float bounce_rays = 1.0f;
        /// Albedo assumed for cells no on-screen pixel has registered, whose material is unknown
        /// because the fields carry geometry only. Real albedo replaces it the moment the camera
        /// looks at the surface. Below one either way, which is what keeps the bounce feedback
        /// loop convergent -- each bounce carries less energy than the last.
        float default_albedo = 0.5f;
        /// Ceiling on any cell's albedo, whatever its material says.
        ///
        /// A bounce ray reads other entries' radiance and the update writes this one's, so the
        /// recursion is L = albedo * mean(L_in) and the loop gain PER CHANNEL is exactly the albedo.
        /// At 1.0 a sealed room conserves light forever -- it neither converges nor diverges, which
        /// presents as a cache that never invalidates rather than as anything obviously wrong. Above
        /// 1.0 indirect climbs without bound.
        ///
        /// sRGB 255 is linear 1.0, so a pure authored colour lands exactly on that unstable point.
        /// @ref default_albedo cannot prevent it: that supplies a value where none exists, and an
        /// entry stops using it the moment an on-screen pixel registers a real material.
        ///
        /// Set to 1 to restore the unclamped behaviour for comparison.
        float max_albedo = 0.9f;
        /// How far a bounce ray travels. Shorter than a gather ray on purpose: this runs for every
        /// resident entry, and the near field is where the bounce actually matters.
        float bounce_distance = 40.0f;
        float bounce_near_field = 2.0f;
        float bounce_max_steps = 24.0f;
        /// Hit acceptance as a fraction of a voxel of whichever field answered.
        float bounce_surface_bias = 0.35f;

        // --- Shadow rays ---
        //
        // One per light per resident entry, which makes them the densest ray population in the
        // whole system and usually the reason the update pass costs what it does. They were
        // hardcoded until they turned out to dominate.

        /// How far a shadow ray travels before giving up and treating the point as lit.
        float shadow_distance = 80.0f;
        /// How far along the normal a shadow ray starts, as a FRACTION OF A VOXEL of the level
        /// covering the point.
        ///
        /// Not a world distance, for the same reason @ref gi_resolve_pass::settings::
        /// normal_bias_voxels is not: what it has to clear is the field's own resolution, and the
        /// cascade's voxel spans 0.25 m to 2 m. Too small and every shadow ray starts occluded,
        /// which converges the entry to black -- and a black entry is indistinguishable from a
        /// correctly shadowed one in the final image, so this fails quietly.
        ///
        /// Measured on Bistro. Worth knowing that this only became tunable at all once
        /// @ref shadow_step_relaxation was non-zero: while grazing rays were exhausting and being
        /// counted as lit, no value worked -- low blacked the cache and high whitened the image,
        /// because raising it made exhaustion MORE likely rather than less.
        float shadow_normal_bias_voxels = 0.35f;
        /// Range in which a shadow ray traces per-instance fields before the cascade takes over.
        ///
        /// The same lever as the resolve pass's near field, and it applies to far more rays. A
        /// shadow ray only has to answer hit or miss, so it can usually afford a shorter near
        /// field than a gather ray that has to land somewhere addressable.
        float shadow_near_field = 10.0f;
        /// Steps per shadow ray. Tighter than a gather ray's budget on purpose.
        ///
        /// An exhausted ray counts as LIT, so running out here does not look like a missing shadow
        /// -- it looks like a surface that is too bright. Prefer @ref shadow_step_relaxation over
        /// raising this: relaxation bounds the step count instead of paying for it.
        float shadow_max_steps = 32.0f;
        /// Light each entry every Nth frame instead of every frame. 1 lights everything always.
        ///
        /// The update pass carries the densest ray populations in the system -- one shadow ray per
        /// light plus a bounce ray, per RESIDENT entry, whether or not anything looks at it -- and
        /// the cache is a running mean, so lighting an entry at half rate merely halves how fast it
        /// converges and reacts, not what it converges to. Entries are interleaved by slot, so the
        /// work spreads evenly across frames rather than pulsing. 2 halves the Cache Update pass
        /// for a barely visible latency cost; raise it further on scenes with many lights.
        float update_interval = 2.0f;
        /// How far along its OWN DIRECTION a SHADOW ray starts, in voxels of the level covering the
        /// point.
        ///
        /// This does the job a large normal bias was doing, without its cost. Both skip the
        /// region where a ray would hit the surface it started on -- unavoidable, because the
        /// ray originates on the RASTER surface while it is traced against the SDF, and those
        /// disagree by up to a voxel.
        ///
        /// The difference is what they do to the shading point. A normal offset MOVES it, so it
        /// sees past nearby geometry and everything reads over-lit -- and since the offset has
        /// to be large enough for the worst ray, that over-lighting is paid by every ray. This
        /// leaves the point exactly where it is and skips only along the ray, so occlusion
        /// stays correct.
        ///
        /// Raise this and lower the normal bias, not the other way round.
        float shadow_ray_start_voxels = 1.0f;
        /// Hit acceptance for a shadow ray, as a fraction of a voxel of whichever field answered.
        float shadow_surface_bias = 0.35f;
        /// Cone relaxation for shadow rays: acceptance grows by this fraction of distance travelled.
        ///
        /// Matters more here than on any other ray in the system. A shadow ray toward a low sun runs
        /// nearly parallel to the ground, and a grazing sphere trace advances by a distance that
        /// stays small for its whole length, so it burns the whole budget without resolving -- and
        /// an exhausted ray is counted as lit. The result is over-bright ground under a low sun,
        /// with nothing in the image to say a ray gave up.
        ///
        /// Both effects point the same way here: it terminates grazing rays sooner (cheaper) and it
        /// can only ever stop a ray EARLY, never carry it past an occluder, so it errs toward
        /// finding the shadow rather than missing it.
        ///
        /// Measured on Bistro: at zero the ground under a low sun washes out and no normal bias
        /// fixes it; 0.1 removes the wash and lets the bias drop from 2.0 to 0.35.
        float shadow_step_relaxation = 0.1f;
        // The cell size, base distance and level cap that KEYS are derived from deliberately do
        // not live here. Every pass touching the cache has to agree on them exactly, so they are
        // owned by the cache itself (radiance_cache_gpu::get_settings).
    };

    struct run_params
    {
        gfx::frame_buffer::ptr g_buffer;
        const camera* cam{};
        surface_cache_service* surface_cache{};
        /// This camera's cascade. The cascade is snapped around a viewer, so it cannot live on
        /// the service without two cameras fighting over one set of levels.
        surface_cache_view* view_cache{};
        settings settings;
    };

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Registers visible surfaces, then lights every resident entry.
     * @return false when the pass could not run.
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

private:
    struct insert_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_gi_insert_params;
        gfx::program::uniform_ptr u_gi_camera_position;
        gfx::program::uniform_ptr s_gi_depth;
        gfx::program::uniform_ptr s_gi_normal;
        gfx::program::uniform_ptr s_gi_base_color;
        gfx::program::uniform_ptr s_gi_emissive;
        // The field itself: insertion resolves the G-buffer surface onto the isosurface, because
        // that is the only surface a reader arriving along a ray can address.
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_insert_params, "u_gi_insert_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_camera_position, "u_gi_camera_position", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_gi_depth, "s_gi_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_normal, "s_gi_normal", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_base_color, "s_gi_base_color", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_gi_emissive, "s_gi_emissive", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } insert_program_;

    struct update_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_gi_cache_params;
        gfx::program::uniform_ptr u_gi_cache_params2;
        gfx::program::uniform_ptr u_gi_update_params;
        gfx::program::uniform_ptr u_gi_update_bounce;
        gfx::program::uniform_ptr u_gi_update_material;
        gfx::program::uniform_ptr u_gi_update_camera;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr u_gi_shadow_params;
        gfx::program::uniform_ptr u_gi_shadow_params2;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_gi_cache_params, "u_gi_cache_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_cache_params2, "u_gi_cache_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_params, "u_gi_update_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_bounce, "u_gi_update_bounce", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_material, "u_gi_update_material", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_update_camera, "u_gi_update_camera", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gpu_light_params, "u_gpu_light_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params, "u_gi_shadow_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params2, "u_gi_shadow_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } update_program_;
};

} // namespace unravel
