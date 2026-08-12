#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/gpu_program.h>

#include <graphics/render_pass.h>
#include <graphics/render_view.h>

namespace unravel
{

/**
 * @brief Visualises the resident distance fields by sphere tracing them from the camera.
 *
 * Diagnostic only: nothing in the lighting path depends on it. It exists because every stage
 * of the field pipeline (bake, atlas upload, indirection rewrite, instance transform, GPU
 * sampling) fails in a way that is invisible in the final image but obvious here.
 */
class sdf_debug_pass
{
public:
    enum class debug_mode : uint8_t
    {
        ///< Shade the traced isosurface by its gradient normal.
        normals = 0,
        ///< Heat map of sphere-trace steps, for spotting fields that will not let rays skip.
        step_count = 1,
        ///< Paints each field's bounds with its header contents.
        headers = 2,
        ///< What the brick lookup resolves to just inside each field's bounds.
        probe = 3,
        ///< Classifies the FIRST sample of the trace, at the bounds entry point.
        entry = 4,
        ///< Traces the global cascade alone, with no per-instance fields.
        clipmap = 5,
        ///< Direct lighting evaluated at the traced hit, from the resident light buffer.
        direct = 6,
        ///< Which cascade answers at the traced surface, and its cross-fade band.
        cascade_levels = 7,
        ///< GI v2: the attribute voxel albedo at the traced hit (yellow = unattributed).
        attr_albedo = 8,
        ///< GI v2: the light voxels at the traced hit - what a gather ray reads.
        light_voxels = 9,
        ///< GI v2: world probe irradiance interpolated at the traced hit.
        world_probes = 10,
        ///< GI v2: which tier answered SUN visibility per voxel face. Needs the light-voxel
        ///< pass's matching debug write (gi_light_voxel_pass::run_params::sun_tier_debug),
        ///< which replaces the volume's radiance with tier colors while active.
        sun_tiers = 11,
        ///< GI v2: SKY FRACTION of the world-probe answer at the traced hit. Sealed interiors
        ///< must read zero; warm colors mean probe rays complete with sky - the bounce-path
        ///< injection channel the sun-tier view cannot see. Pure read, no debug write needed.
        probe_sky = 12,
    };

    struct settings
    {
        /// Sphere-trace steps per instance. Generous because this is a diagnostic view where
        /// coverage matters more than cost; the GI tracer will run a much tighter budget.
        ///
        /// Raising this pushes the falloff further out but does not remove it: a ray grazing
        /// along a surface advances by a distance that stays small for its whole length, so
        /// the steps needed grow without bound as the angle flattens. The step-count mode
        /// paints exhausted rays blue.
        int max_steps = 256;
        float max_distance = 500.0f;
        debug_mode mode = debug_mode::normals;
        /// Distance at which a step counts as a hit, as a FRACTION OF A VOXEL of the field
        /// being traced (converted to world units per instance by the tracer).
        ///
        /// Deliberately not an absolute world distance: a field resolves nothing finer than
        /// one voxel, and a voxel's world size varies with both bake resolution and instance
        /// scale. An absolute threshold larger than an instance's voxel makes even the
        /// saturated far-field value read as a hit, so the field's bounding box renders solid.
        float surface_bias = 0.5f;
        /// World distance at which the per-instance tier hands over to the global cascade.
        /// Per-instance fields resolve thin geometry but cost a bounds test per instance, so
        /// they are bounded to the near field; the cascade covers everything beyond in one
        /// lookup at a resolution that no longer matters at that range.
        float near_field_distance = 30.0f;
        /// How far a shadow ray travels before giving up and treating the point as lit.
        float shadow_distance = 80.0f;
        /// How far along the surface normal a shadow ray starts. Without an offset the ray
        /// begins on the surface it was cast from, where the field reads zero, so every ray
        /// reports itself occluded and the scene goes black.
        float shadow_normal_bias = 0.15f;
        /// Steps per shadow ray. Much tighter than the primary trace: there is one of these per
        /// light per shaded point, and a shadow ray only needs to answer hit or miss.
        int shadow_max_steps = 64;
        /// Cone half-angle tangent: the hit acceptance radius grows by this fraction of the
        /// distance travelled, in the FAR field only. Bounds a grazing ray's step count, which
        /// is otherwise unbounded and makes distant surfaces fade out as the march runs out of
        /// budget.
        ///
        /// This widens the ACCEPTANCE, never the step. Forcing a minimum step instead makes a
        /// grazing ray jump straight through a surface and miss in bands -- concentric rings,
        /// far worse than the fade it was meant to cure. Widening acceptance can only stop a
        /// ray early, so the trace stays conservative.
        ///
        /// The cost is that distant surfaces are fattened by the cone radius, which errs toward
        /// over-occluding at range -- the safe direction. Zero gives an exact sphere trace and
        /// restores the fade.
        /// Defaults to zero: an exact sphere trace. Two attempts at trading exactness for
        /// reach here made the image worse, so the honest default is the exact trace with its
        /// known falloff, and this stays available to experiment with.
        float step_relaxation = 0.0f;
    };

    struct run_params
    {
        gfx::frame_buffer::ptr output;
        const camera* cam{};
        surface_cache_system* surface_cache{};
        /// This camera's cascade. The cascade is snapped around a viewer, so it cannot live on
        /// the service without two cameras fighting over one set of levels.
        surface_cache_view* view_cache{};
        settings settings;
    };

    auto init(rtti::context& ctx) -> bool;

    /**
     * @brief Traces and composites the visualisation over @c params.output.
     * @return false when the pass could not run (no program, no resident fields).
     */
    auto run(gfx::render_view& rview, const run_params& params) -> bool;

    void release_resources();

private:
    struct debug_program : uniforms_cache
    {
        gpu_program::ptr program;
        gfx::program::uniform_ptr u_sdf_params;
        gfx::program::uniform_ptr u_sdf_grid_params;
        gfx::program::uniform_ptr u_sdf_debug_params;
        gfx::program::uniform_ptr u_sdf_debug_params2;
        gfx::program::uniform_ptr s_sdf_atlas;
        gfx::program::uniform_ptr s_sdf_clipmap;
        gfx::program::uniform_ptr s_attr_albedo;
        gfx::program::uniform_ptr s_light_voxels;
        gfx::program::uniform_ptr s_world_probe_irradiance;
        gfx::program::uniform_ptr s_world_probe_depth;
        gfx::program::uniform_ptr u_gi_world_probe_params;
        gfx::program::uniform_ptr u_gi_world_probe_atlas;
        gfx::program::uniform_ptr u_gi_light_voxel_params;
        gfx::program::uniform_ptr u_sdf_clipmap_levels;
        gfx::program::uniform_ptr u_sdf_clipmap_params;
        gfx::program::uniform_ptr u_gpu_light_params;
        gfx::program::uniform_ptr u_gi_shadow_params;
        gfx::program::uniform_ptr u_gi_debug_camera;

        void cache_uniforms()
        {
            cache_uniform(program.get(), u_sdf_params, "u_sdf_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_grid_params, "u_sdf_grid_params", gfx::uniform_type::Vec4, 2);
            cache_uniform(program.get(), u_sdf_debug_params, "u_sdf_debug_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_debug_params2, "u_sdf_debug_params2", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), s_sdf_atlas, "s_sdf_atlas", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_sdf_clipmap, "s_sdf_clipmap", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_attr_albedo, "s_attr_albedo", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_light_voxels, "s_light_voxels", gfx::uniform_type::Sampler);
            cache_uniform(program.get(),
                          s_world_probe_irradiance,
                          "s_world_probe_irradiance",
                          gfx::uniform_type::Sampler);
            cache_uniform(program.get(), s_world_probe_depth, "s_world_probe_depth", gfx::uniform_type::Sampler);
            cache_uniform(program.get(), u_gi_world_probe_params, "u_gi_world_probe_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_world_probe_atlas, "u_gi_world_probe_atlas", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_light_voxel_params, "u_gi_light_voxel_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_sdf_clipmap_levels, "u_sdf_clipmap_levels", gfx::uniform_type::Vec4,
                          global_sdf_clipmap::level_count);
            cache_uniform(program.get(), u_sdf_clipmap_params, "u_sdf_clipmap_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gpu_light_params, "u_gpu_light_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_shadow_params, "u_gi_shadow_params", gfx::uniform_type::Vec4);
            cache_uniform(program.get(), u_gi_debug_camera, "u_gi_debug_camera", gfx::uniform_type::Vec4);
        }

        auto is_valid() const -> bool
        {
            return program && program->is_valid();
        }
    } debug_program_;

};

} // namespace unravel
