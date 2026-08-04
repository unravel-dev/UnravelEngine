#include "gi_component.hpp"

#include <serialization/associative_archive.h>
#include <serialization/binary_archive.h>

namespace unravel
{

// `trace_resolution` is reflected once, by ssr_component.cpp. Registering it again would be a
// duplicate type in the meta registry, so this file only uses it as a field type.

REFLECT_INLINE(gi_cache_pass::settings)
{
    using settings = gi_cache_pass::settings;

    entt::meta_factory<settings>{}
        .type("gi_cache_pass::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_cache_pass::settings"},
            entt::attribute{"pretty_name", "Cache"},
        })
        .data<&settings::insert_stride>("insert_stride"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "insert_stride"},
            entt::attribute{"pretty_name", "Insert Stride"},
            entt::attribute{"group", "Insertion"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"tooltip", "Pixel stride for registering visible surfaces.\nCells are far "
                                       "larger than a pixel, so sampling every pixel resolves the same "
                                       "handful of cells thousands of times over."},
        })
        .data<&settings::insert_max_distance>("insert_max_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "insert_max_distance"},
            entt::attribute{"pretty_name", "Insert Max Distance"},
            entt::attribute{"group", "Insertion"},
            entt::attribute{"min", 10.0f},
            entt::attribute{"max", 500.0f},
            entt::attribute{"tooltip", "Surfaces beyond this are not registered.\nClamped internally to "
                                       "the outermost cascade's reach, because a surface the cascade "
                                       "cannot cover has no address to register under."},
        })
        .data<&settings::surface_offset_cells>("surface_offset_cells"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_offset_cells"},
            entt::attribute{"pretty_name", "Surface Offset (cells)"},
            entt::attribute{"group", "Insertion"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "How far the recorded point is lifted before lighting, as a "
                                       "fraction of THIS ENTRY'S CELL.\nInsertion snaps the point to "
                                       "the cell grid, so it can sit up to a cell inside the geometry "
                                       "-- an error that scales with the cell, which runs 0.25 m to "
                                       "2 m. A fixed world distance cannot cover both ends.\nToo small "
                                       "and the far field goes black; too large and entries float off "
                                       "their surface and everything reads over-lit. Judge it in the "
                                       "cache debug view, not the lit one."},
        })
        .data<&settings::min_alpha>("min_alpha"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "min_alpha"},
            entt::attribute{"pretty_name", "Min Alpha"},
            entt::attribute{"group", "Accumulation"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.5f},
            entt::attribute{"tooltip", "Floor on the accumulation blend weight.\nWithout it a mature "
                                       "entry freezes and a light that switches off stays visible."},
        })
        .data<&settings::max_samples>("max_samples"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_samples"},
            entt::attribute{"pretty_name", "Max Samples"},
            entt::attribute{"group", "Accumulation"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip", "Cap on accumulated samples per entry.\nHigher is smoother and "
                                       "slower to respond to lighting that actually changed."},
        })
        .data<&settings::bounce_rays>("bounce_rays"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_rays"},
            entt::attribute{"pretty_name", "Bounce Rays"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip", "Bounce rays cast per entry per frame.\nOne is usually enough: "
                                       "the result feeds a running mean, so successive frames explore "
                                       "different directions. These rays are also what create entries "
                                       "for geometry the camera has never seen."},
        })
        .data<&settings::default_albedo>("default_albedo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "default_albedo"},
            entt::attribute{"pretty_name", "Default Albedo"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Albedo assumed for cells NO on-screen pixel has registered, "
                                       "whose material is unknown because the fields carry geometry "
                                       "only.\nReplaced by the real material the moment the camera "
                                       "looks at the surface, so this does not bound an authored "
                                       "material -- see Max Albedo for that."},
        })
        .data<&settings::max_albedo>("max_albedo"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_albedo"},
            entt::attribute{"pretty_name", "Max Albedo"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "Ceiling on any cell's albedo, whatever its material says.\nA "
                                       "bounce ray reads other entries' radiance and the update writes "
                                       "this one's, so the loop gain PER CHANNEL is exactly the "
                                       "albedo.\nAt 1.0 a sealed room conserves light forever -- it "
                                       "neither converges nor diverges, which looks like a cache that "
                                       "never invalidates. sRGB 255 is linear 1.0, so a pure authored "
                                       "colour lands exactly on that unstable point: a 255,0,0 room "
                                       "stays red indefinitely.\nSet to 1 to restore the old "
                                       "behaviour for comparison."},
        })
        .data<&settings::bounce_distance>("bounce_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_distance"},
            entt::attribute{"pretty_name", "Bounce Distance"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 200.0f},
            entt::attribute{"tooltip", "How far a bounce ray travels."},
        })
        .data<&settings::bounce_near_field>("bounce_near_field"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_near_field"},
            entt::attribute{"pretty_name", "Bounce Near Field"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"tooltip", "Range in which a bounce ray traces per-instance fields.\nThe "
                                       "same cost/accuracy trade as the resolve's near field, and it "
                                       "drives most of the Cache Update pass time."},
        })
        .data<&settings::bounce_max_steps>("bounce_max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_max_steps"},
            entt::attribute{"pretty_name", "Bounce Max Steps"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip", "Sphere-trace steps per instance for a bounce ray."},
        })
        .data<&settings::bounce_surface_bias>("bounce_surface_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "bounce_surface_bias"},
            entt::attribute{"pretty_name", "Bounce Surface Bias"},
            entt::attribute{"group", "Bounce"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Hit acceptance as a fraction of a voxel of whichever field "
                                       "answered.\nNot a world distance: voxel size varies with bake "
                                       "resolution, instance scale and cascade level."},
        })
        .data<&settings::shadow_distance>("shadow_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_distance"},
            entt::attribute{"pretty_name", "Shadow Distance"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 200.0f},
            entt::attribute{"tooltip", "How far a shadow ray travels before giving up and treating the "
                                       "point as lit."},
        })
        .data<&settings::shadow_normal_bias_voxels>("shadow_normal_bias_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_normal_bias_voxels"},
            entt::attribute{"pretty_name", "Shadow Normal Bias (voxels)"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip", "How far along the normal a shadow ray starts, in VOXELS of the "
                                       "level covering the point.\nToo small and every shadow ray starts "
                                       "occluded, so the entry converges to BLACK -- which looks exactly "
                                       "like correct shadowing in the final image, so it fails quietly. "
                                       "Check the cache debug view rather than the lit one."},
        })
        .data<&settings::shadow_near_field>("shadow_near_field"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_near_field"},
            entt::attribute{"pretty_name", "Shadow Near Field"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"tooltip", "Range in which a shadow ray traces per-instance fields.\nThe "
                                       "same lever as the resolve's near field but over far more rays -- "
                                       "one per light per entry -- so it is usually the largest single "
                                       "cost in the Cache Update pass. A shadow ray only answers hit or "
                                       "miss, so it can afford a shorter near field than a gather ray "
                                       "that has to land somewhere addressable."},
        })
        .data<&settings::shadow_max_steps>("shadow_max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_max_steps"},
            entt::attribute{"pretty_name", "Shadow Max Steps"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip", "Steps per shadow ray. An exhausted ray counts as LIT, because "
                                       "over-lighting degrades far more gracefully than stamping shadow "
                                       "onto a region.\nPrefer Shadow Step Relaxation over raising this "
                                       "-- relaxation bounds the step count rather than paying for it."},
        })
        .data<&settings::shadow_ray_start_voxels>("shadow_ray_start_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_ray_start_voxels"},
            entt::attribute{"pretty_name", "Shadow Ray Start (voxels)"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip",
                            "How far along its OWN direction a ray starts, in voxels of the "
                            "level covering the point. This does the job a large normal bias "
                            "was doing, without its cost. Both skip the region where a ray "
                            "would hit the surface it started on, which is unavoidable: the ray "
                            "originates on the RASTER surface but is traced against the SDF, "
                            "and those disagree by up to a voxel. The difference is that a "
                            "normal offset MOVES the shading point, so it sees past nearby "
                            "geometry and everything reads over-lit, while this leaves the "
                            "point where it is. Raise this and lower the normal bias, not the "
                            "other way round."},
        })
        .data<&settings::shadow_surface_bias>("shadow_surface_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_surface_bias"},
            entt::attribute{"pretty_name", "Shadow Surface Bias"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Hit acceptance for a shadow ray, as a fraction of a voxel of "
                                       "whichever field answered."},
        })
        .data<&settings::shadow_step_relaxation>("shadow_step_relaxation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "shadow_step_relaxation"},
            entt::attribute{"pretty_name", "Shadow Step Relaxation"},
            entt::attribute{"group", "Shadow"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip", "Cone relaxation for shadow rays.\nMatters more here than on any "
                                       "other ray. Toward a low sun a shadow ray runs nearly parallel to "
                                       "the ground, and a grazing sphere trace advances by a distance "
                                       "that stays small the whole way -- so it burns its budget without "
                                       "resolving, and an exhausted ray counts as LIT. That reads as "
                                       "over-bright ground, not as a missing shadow.\nBoth effects point "
                                       "the same way: grazing rays terminate sooner (cheaper) and it can "
                                       "only stop a ray EARLY, so it errs toward finding the occluder."},
        });
}

REFLECT_INLINE(gi_resolve_pass::settings)
{
    using settings = gi_resolve_pass::settings;

    entt::meta_factory<settings>{}
        .type("gi_resolve_pass::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_resolve_pass::settings"},
            entt::attribute{"pretty_name", "Resolve"},
        })
        .data<&settings::ray_count>("ray_count"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ray_count"},
            entt::attribute{"pretty_name", "Ray Count"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"tooltip", "Rays per pixel. The bluntest lever on this pass -- cost is very "
                                       "nearly linear in it.\nLow works because each ray returns a "
                                       "prefiltered cell rather than a point sample, so the variance a "
                                       "path tracer fights here has already been paid down."},
        })
        .data<&settings::max_distance>("max_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_distance"},
            entt::attribute{"pretty_name", "Max Distance"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 10.0f},
            entt::attribute{"max", 500.0f},
            entt::attribute{"tooltip", "How far a gather ray travels before giving up and leaving the "
                                       "environment probe to cover that direction."},
        })
        .data<&settings::normal_bias_voxels>("normal_bias_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "normal_bias_voxels"},
            entt::attribute{"pretty_name", "Normal Bias (voxels)"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip", "Lift off the surface before tracing, in VOXELS of the field "
                                       "answering at the shading point.\nA ray starting on the "
                                       "isosurface reads zero distance and reports its own origin as an "
                                       "occluder -- that is surface acne. The lift has to clear the hit "
                                       "acceptance, which is Surface Bias voxels, so it is measured the "
                                       "same way.\nNot a world distance: the cascade voxel runs 0.25 m "
                                       "to 2 m across one view, so a fixed distance is either too small "
                                       "far away or lifts rays over the contact detail near by.\nUse "
                                       "the smallest value that clears the acne."},
        })
        .data<&settings::intensity>("intensity"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "intensity"},
            entt::attribute{"pretty_name", "Intensity"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"tooltip", "Artistic gain on the cached bounce. The environment fallback is "
                                       "left at probe intensity, so this scales the scene's own "
                                       "contribution only."},
        })
        .data<&settings::near_field_distance>("near_field_distance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "near_field_distance"},
            entt::attribute{"pretty_name", "Near Field Distance"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 100.0f},
            entt::attribute{"tooltip", "Range in which per-instance fields are traced; beyond it the "
                                       "cascade answers.\nTHIS IS WHERE THE FRAME TIME IS -- measured at "
                                       "~89% of this pass. It is not free to shorten: the cascade cannot "
                                       "represent thin geometry, so occlusion degrades into surface acne "
                                       "that dances as the cascade re-snaps. Prefer Step Relaxation "
                                       "first, which errs toward over-occluding instead."},
        })
        .data<&settings::max_steps>("max_steps"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_steps"},
            entt::attribute{"pretty_name", "Max Steps"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 8.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip", "Sphere-trace steps, per instance in the near field and per ray "
                                       "in the cascade."},
        })
        .data<&settings::surface_bias>("surface_bias"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "surface_bias"},
            entt::attribute{"pretty_name", "Surface Bias"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.1f},
            entt::attribute{"max", 2.0f},
            entt::attribute{"tooltip", "Hit acceptance as a fraction of a voxel of whichever field "
                                       "answered.\nAn absolute distance is meaningless here, because "
                                       "voxel size varies with bake resolution, instance scale and "
                                       "cascade level."},
        })
        .data<&settings::step_relaxation>("step_relaxation"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "step_relaxation"},
            entt::attribute{"pretty_name", "Step Relaxation"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip", "Cone half-angle tangent: hit acceptance grows by this fraction "
                                       "of distance travelled.\nThe cheapest large saving available -- "
                                       "0.05 measured a 28% cut in this pass, because it bounds grazing "
                                       "rays, which is where the cost concentrates.\nSafe in one "
                                       "direction only: it widens what counts as a HIT and never the "
                                       "step, so it can stop a ray early but never carry it through a "
                                       "wall. It cannot leak light. It CAN over-occlude at range."},
        })
        .data<&settings::ray_start_voxels>("ray_start_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "ray_start_voxels"},
            entt::attribute{"pretty_name", "Ray Start (voxels)"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip",
                            "How far along its OWN direction a ray starts, in voxels of the "
                            "level covering the point. This does the job a large normal bias "
                            "was doing, without its cost. Both skip the region where a ray "
                            "would hit the surface it started on, which is unavoidable: the ray "
                            "originates on the RASTER surface but is traced against the SDF, "
                            "and those disagree by up to a voxel. The difference is that a "
                            "normal offset MOVES the shading point, so it sees past nearby "
                            "geometry and everything reads over-lit, while this leaves the "
                            "point where it is. Raise this and lower the normal bias, not the "
                            "other way round."},
        })
        .data<&settings::debug_ray_diagnostics>("debug_ray_diagnostics"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "debug_ray_diagnostics"},
            entt::attribute{"pretty_name", "Debug: Ray Diagnostics"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"min", 0},
            entt::attribute{"max", 3},
            entt::attribute{"tooltip",
                            "Replace the GI output with a per-ray diagnostic. 1 = which STAGE "
                            "fails: a ray contributes light only if it HITS geometry (red), its "
                            "hit can be ADDRESSED (green), and a cache entry is FOUND there "
                            "(blue); the dark channel names the stage, white is a working pixel. "
                            "2 = where the rays LANDED: red is the fraction hitting within four "
                            "voxels of their own origin, which means the gather is reading the "
                            "surface it is shading and feeding it back. Mode 1 cannot see that, "
                            "because a self-hit succeeds at every stage."
                            " 3 = the three numbers behind the remaining theories: red is the "
                            "fraction of rays that read the very entry being shaded, by EXACT key "
                            "rather than by distance; green is the lift actually applied in "
                            "voxels; blue is the cascade distance at the shading point in voxels, "
                            "mid grey meaning exactly on the isosurface."},
        })
        .data<&settings::interpolate_cache>("interpolate_cache"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "interpolate_cache"},
            entt::attribute{"pretty_name", "Interpolate Cache"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip", "Interpolate across the four cells bracketing a hit instead of "
                                       "point sampling one.\nCosts ~10% of this pass and roughly halves "
                                       "the cache miss rate (measured 49% -> 87% agreement), as well as "
                                       "removing the cell-sized blocks a point lookup produces."},
        })
        .data<&settings::occlude_on_cache_miss>("occlude_on_cache_miss"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "occlude_on_cache_miss"},
            entt::attribute{"pretty_name", "Occlude On Cache Miss"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip",
                            "Treat a ray that HIT geometry but found no cache entry as occluded "
                            "rather than as unknown.\nA miss says the cell is not lit YET; it does "
                            "not say the cell is dark. But the ray did hit something, so it does say "
                            "the sky is blocked in that direction.\nOff, that part of the hemisphere "
                            "falls back to the SH irradiance probe -- and in a sealed room every ray "
                            "misses, so the room stays lit through solid walls and never converges to "
                            "black.\nOn, a cache that is still filling in reads dark rather than "
                            "probe-coloured. That is transient; the leak is permanent."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Trace Resolution"},
            entt::attribute{"group", "Gather"},
            entt::attribute{"tooltip", "Indirect diffuse is low frequency, so tracing below full "
                                       "resolution costs little quality and scales this pass directly."},
        })
        .data<&settings::enable_temporal>("enable_temporal"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_temporal"},
            entt::attribute{"pretty_name", "Enable Temporal"},
            entt::attribute{"group", "Temporal"},
            entt::attribute{"tooltip", "Averaging across frames is what turns a handful of rays into an "
                                       "effective sample count in the hundreds."},
        })
        .data<&settings::max_accum_frames>("max_accum_frames"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_accum_frames"},
            entt::attribute{"pretty_name", "Max Accum Frames"},
            entt::attribute{"group", "Temporal"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 256.0f},
            entt::attribute{"tooltip", "Frames the running mean may reach.\nThe weight is 1/n while n "
                                       "grows toward this, which is a true mean and genuinely settles; a "
                                       "fixed weight would keep shimmering forever. The cap is what "
                                       "keeps it responsive to light that really changed."},
        })
        .data<&settings::reprojection_tolerance>("reprojection_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "reprojection_tolerance"},
            entt::attribute{"pretty_name", "Reprojection Tolerance"},
            entt::attribute{"group", "Temporal"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 1.0f},
            entt::attribute{"tooltip", "History validation tolerance, as a fraction of view distance so "
                                       "one value works near and far.\nAt 1.0 the test never rejects and "
                                       "is effectively off, which is the measured best setting -- "
                                       "rejection is what CAUSES fireflies, because a rejected pixel "
                                       "falls back to a single frame of a four-ray gather. History is "
                                       "guarded by the neighbourhood clamp instead.\nLower it if "
                                       "ghosting appears behind fast-moving geometry: that is the one "
                                       "case the clamp cannot catch."},
        })
        .data<&settings::history_clamp_sigma>("history_clamp_sigma"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "history_clamp_sigma"},
            entt::attribute{"pretty_name", "History Clamp Sigma"},
            entt::attribute{"group", "Temporal"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 8.0f},
            entt::attribute{"tooltip", "Width of the history clamp in neighbourhood standard "
                                       "deviations. Zero restores accept-or-reject.\nClamping is what "
                                       "avoids choosing between fireflies and ghosting: agreeing history "
                                       "survives intact, disagreeing history is pulled to the edge of "
                                       "what this frame sees."},
        })
        .data<&settings::enable_spatial_denoise>("enable_spatial_denoise"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_spatial_denoise"},
            entt::attribute{"pretty_name", "Enable Spatial Denoise"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"tooltip", "Edge-preserving a-trous filter over the accumulated result."},
        })
        .data<&settings::denoise_passes>("denoise_passes"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_passes"},
            entt::attribute{"pretty_name", "Denoise Passes"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 6.0f},
            entt::attribute{"tooltip", "Tap spacing doubles each pass, so reach grows exponentially "
                                       "while cost stays linear."},
        })
        .data<&settings::denoise_normal_power>("denoise_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_normal_power"},
            entt::attribute{"pretty_name", "Denoise Normal Power"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip", "Exponent on normal agreement. Higher keeps light from turning "
                                       "corners."},
        })
        .data<&settings::denoise_luma_phi>("denoise_luma_phi"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_luma_phi"},
            entt::attribute{"pretty_name", "Denoise Luma Phi"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.5f},
            entt::attribute{"max", 32.0f},
            entt::attribute{"tooltip", "Multiplier on the measured luminance standard error.\nThe "
                                       "tolerance already shrinks as the estimate accumulates, so this "
                                       "sets how hard the filter works on a pixel that has NOT settled. "
                                       "Raise it if converged areas look grainy."},
        })
        .data<&settings::denoise_plane_tolerance>("denoise_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_plane_tolerance"},
            entt::attribute{"pretty_name", "Denoise Plane Tolerance"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip", "How far off the centre pixel's plane a tap may sit, as a "
                                       "fraction of view distance."},
        })
        .data<&settings::denoise_low_count_boost>("denoise_low_count_boost"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "denoise_low_count_boost"},
            entt::attribute{"pretty_name", "Denoise Low Count Boost"},
            entt::attribute{"group", "Denoise"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 64.0f},
            entt::attribute{"tooltip", "Tolerance multiplier at one accumulated sample, decaying to 1 as "
                                       "the count grows.\nCovers freshly disoccluded pixels, which have "
                                       "no usable variance estimate and are noisiest exactly there."},
        })
        .data<&settings::enable_bilateral_upsample>("enable_bilateral_upsample"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enable_bilateral_upsample"},
            entt::attribute{"pretty_name", "Enable Bilateral Upsample"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"tooltip", "Surface-aware reconstruction to full resolution.\nA plain "
                                       "bilinear tap blends across silhouettes, which is where the "
                                       "gather is noisiest, so it spreads exactly what should not "
                                       "spread."},
        })
        .data<&settings::upsample_normal_power>("upsample_normal_power"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_normal_power"},
            entt::attribute{"pretty_name", "Upsample Normal Power"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"max", 128.0f},
            entt::attribute{"tooltip", "Exponent on normal agreement between a low-resolution tap and "
                                       "the pixel being shaded."},
        })
        .data<&settings::upsample_plane_tolerance>("upsample_plane_tolerance"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "upsample_plane_tolerance"},
            entt::attribute{"pretty_name", "Upsample Plane Tolerance"},
            entt::attribute{"group", "Upsample"},
            entt::attribute{"min", 0.001f},
            entt::attribute{"max", 0.2f},
            entt::attribute{"tooltip", "How far off the pixel's plane a low-resolution tap may sit."},
        });
}

REFLECT_INLINE(global_sdf_clipmap::settings)
{
    using settings = global_sdf_clipmap::settings;

    entt::meta_factory<settings>{}
        .type("global_sdf_clipmap::settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "global_sdf_clipmap::settings"},
            entt::attribute{"pretty_name", "Cascade"},
        })
        .data<&settings::compose_on_gpu>("compose_on_gpu"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "compose_on_gpu"},
            entt::attribute{"pretty_name", "Compose On GPU"},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Build the cascade voxels in a compute dispatch instead of on the CPU. "
                            "The CPU composer blocks the main thread for milliseconds whenever the "
                            "camera moves far enough to re-snap a level, which lands as a stutter; "
                            "measured at 4.20 ms wall against 0.42 ms plus ~0.5 ms of GPU. Output is "
                            "identical -- pinned byte for byte by a parity test -- so this is a cost "
                            "switch, not a quality one. Turn it off to compare, or if a backend "
                            "misbehaves."},
        })
        .data<&settings::resolution>("resolution"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolution"},
            entt::attribute{"pretty_name", "Resolution"},
            entt::attribute{"min", 16},
            entt::attribute{"max", 128},
            entt::attribute{"group", "Composition"},
            entt::attribute{"tooltip",
                            "Voxels per axis in every cascade level. Memory and composition work are "
                            "both CUBIC in this: 128 is four times the spatial detail and eight times "
                            "the cost of 64. It was held at 64 because the CPU composer could not "
                            "afford more; on the GPU 128 is reachable. Changing it rebuilds the "
                            "cascade, so it flickers for a few frames."},
        })
        .data<&settings::base_extent>("base_extent"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "base_extent"},
            entt::attribute{"pretty_name", "Base Extent"},
            entt::attribute{"min", 1.0f},
            entt::attribute{"step", 1.0f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "World-space size of the finest level. With Level Scale this sets both "
                            "how fine the near field is and how far GI sees at all: total coverage is "
                            "base extent times level scale cubed. Rebuilds the cascade."},
        })
        .data<&settings::level_scale>("level_scale"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "level_scale"},
            entt::attribute{"pretty_name", "Level Scale"},
            entt::attribute{"min", 1.5f},
            entt::attribute{"max", 4.0f},
            entt::attribute{"step", 0.1f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "Size multiplier between consecutive levels. Doubling keeps the far "
                            "cascades fine enough that a floor does not appear to float -- a tracer "
                            "stops within a fraction of a VOXEL, so at 8 m voxels that error is "
                            "metres. Quadrupling buys range and costs exactly that. Rebuilds the "
                            "cascade."},
        })
        .data<&settings::blend_voxels>("blend_voxels"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "blend_voxels"},
            entt::attribute{"pretty_name", "Level Blend Band"},
            entt::attribute{"min", 0.0f},
            entt::attribute{"max", 16.0f},
            entt::attribute{"step", 0.5f},
            entt::attribute{"group", "Coverage"},
            entt::attribute{"tooltip",
                            "Width of the cross-fade into the next level, in voxels of the level "
                            "fading out. Levels are composed independently so their isosurfaces sit "
                            "about a coarse voxel apart; the band has to be wider than that to hide "
                            "it. Zero restores a hard switch, which pops as the camera moves."},
        })
        .data<&settings::max_levels_per_update>("max_levels_per_update"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "max_levels_per_update"},
            entt::attribute{"pretty_name", "Levels Per Update"},
            entt::attribute{"min", 1},
            entt::attribute{"max", global_sdf_clipmap::level_count},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Cascade levels recomposed per frame at most. Composing touches every "
                            "voxel of a level, so rebuilding all four in the frame something moved is "
                            "a visible hitch. The cost of budgeting is that a moved object keeps "
                            "occluding from its old position for a few frames. Raise it now that "
                            "composition is on the GPU and cheaper."},
        })
        .data<&settings::cull_composition>("cull_composition"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cull_composition"},
            entt::attribute{"pretty_name", "Cull Composition"},
            entt::attribute{"group", "Budget"},
            entt::attribute{"tooltip",
                            "Bin instances so a voxel tests only the ones that can reach it, instead "
                            "of every instance the level overlaps. Pure acceleration -- composing with "
                            "it off produces byte-identical voxels, which a test asserts. Present so "
                            "that comparison can be made; leave it on."},
        });
}

REFLECT_INLINE(gi_settings)
{
    entt::meta_factory<gi_settings>{}
        .type("gi_settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_settings"},
            entt::attribute{"pretty_name", "Settings"},
        })
        .data<&gi_settings::cache>("cache"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "cache"},
            entt::attribute{"pretty_name", "Cache"},
            entt::attribute{"tooltip", "Populating and lighting the world-space entries."},
        })
        .data<&gi_settings::resolve>("resolve"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "resolve"},
            entt::attribute{"pretty_name", "Resolve"},
            entt::attribute{"tooltip", "Gathering those entries into the screen. Usually the more "
                                       "expensive of the two."},
        })
        .data<&gi_settings::clipmap>("clipmap"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "clipmap"},
            entt::attribute{"pretty_name", "Cascade"},
            entt::attribute{"tooltip", "The coarse world distance field both passes trace against. "
                                       "This is what lets offscreen and distant geometry contribute "
                                       "at all."},
        });
}

SAVE_INLINE(gi_cache_pass::settings)
{
    try_save(ar, ser20::make_nvp("insert_stride", obj.insert_stride));
    try_save(ar, ser20::make_nvp("insert_max_distance", obj.insert_max_distance));
    try_save(ar, ser20::make_nvp("surface_offset_cells", obj.surface_offset_cells));
    try_save(ar, ser20::make_nvp("min_alpha", obj.min_alpha));
    try_save(ar, ser20::make_nvp("max_samples", obj.max_samples));
    try_save(ar, ser20::make_nvp("bounce_rays", obj.bounce_rays));
    try_save(ar, ser20::make_nvp("default_albedo", obj.default_albedo));
    try_save(ar, ser20::make_nvp("max_albedo", obj.max_albedo));
    try_save(ar, ser20::make_nvp("bounce_distance", obj.bounce_distance));
    try_save(ar, ser20::make_nvp("bounce_near_field", obj.bounce_near_field));
    try_save(ar, ser20::make_nvp("bounce_max_steps", obj.bounce_max_steps));
    try_save(ar, ser20::make_nvp("bounce_surface_bias", obj.bounce_surface_bias));
    try_save(ar, ser20::make_nvp("shadow_distance", obj.shadow_distance));
    try_save(ar, ser20::make_nvp("shadow_normal_bias_voxels", obj.shadow_normal_bias_voxels));
    try_save(ar, ser20::make_nvp("shadow_near_field", obj.shadow_near_field));
    try_save(ar, ser20::make_nvp("shadow_max_steps", obj.shadow_max_steps));
    try_save(ar, ser20::make_nvp("shadow_surface_bias", obj.shadow_surface_bias));
    try_save(ar, ser20::make_nvp("shadow_ray_start_voxels", obj.shadow_ray_start_voxels));
    try_save(ar, ser20::make_nvp("shadow_step_relaxation", obj.shadow_step_relaxation));
}
SAVE_INSTANTIATE(gi_cache_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_cache_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_cache_pass::settings)
{
    try_load(ar, ser20::make_nvp("insert_stride", obj.insert_stride));
    try_load(ar, ser20::make_nvp("insert_max_distance", obj.insert_max_distance));
    // Renamed, not reinterpreted: this was a world distance and is now a cell fraction, so an old
    // scene's 0.05 would become a fortieth of a cell and read as a black far field.
    try_load(ar, ser20::make_nvp("surface_offset_cells", obj.surface_offset_cells));
    try_load(ar, ser20::make_nvp("min_alpha", obj.min_alpha));
    try_load(ar, ser20::make_nvp("max_samples", obj.max_samples));
    try_load(ar, ser20::make_nvp("bounce_rays", obj.bounce_rays));
    try_load(ar, ser20::make_nvp("default_albedo", obj.default_albedo));
    try_load(ar, ser20::make_nvp("max_albedo", obj.max_albedo));
    try_load(ar, ser20::make_nvp("bounce_distance", obj.bounce_distance));
    try_load(ar, ser20::make_nvp("bounce_near_field", obj.bounce_near_field));
    try_load(ar, ser20::make_nvp("bounce_max_steps", obj.bounce_max_steps));
    try_load(ar, ser20::make_nvp("bounce_surface_bias", obj.bounce_surface_bias));
    try_load(ar, ser20::make_nvp("shadow_distance", obj.shadow_distance));
    try_load(ar, ser20::make_nvp("shadow_normal_bias_voxels", obj.shadow_normal_bias_voxels));
    try_load(ar, ser20::make_nvp("shadow_near_field", obj.shadow_near_field));
    try_load(ar, ser20::make_nvp("shadow_max_steps", obj.shadow_max_steps));
    try_load(ar, ser20::make_nvp("shadow_surface_bias", obj.shadow_surface_bias));
    try_load(ar, ser20::make_nvp("shadow_ray_start_voxels", obj.shadow_ray_start_voxels));
    try_load(ar, ser20::make_nvp("shadow_step_relaxation", obj.shadow_step_relaxation));
}
LOAD_INSTANTIATE(gi_cache_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_cache_pass::settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_resolve_pass::settings)
{
    try_save(ar, ser20::make_nvp("ray_count", obj.ray_count));
    try_save(ar, ser20::make_nvp("max_distance", obj.max_distance));
    try_save(ar, ser20::make_nvp("normal_bias_voxels", obj.normal_bias_voxels));
    try_save(ar, ser20::make_nvp("intensity", obj.intensity));
    try_save(ar, ser20::make_nvp("near_field_distance", obj.near_field_distance));
    try_save(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_save(ar, ser20::make_nvp("surface_bias", obj.surface_bias));
    try_save(ar, ser20::make_nvp("step_relaxation", obj.step_relaxation));
    try_save(ar, ser20::make_nvp("interpolate_cache", obj.interpolate_cache));
    try_save(ar, ser20::make_nvp("debug_ray_diagnostics", obj.debug_ray_diagnostics));
    try_save(ar, ser20::make_nvp("ray_start_voxels", obj.ray_start_voxels));
    try_save(ar, ser20::make_nvp("occlude_on_cache_miss", obj.occlude_on_cache_miss));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_save(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
    try_save(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_save(ar, ser20::make_nvp("history_clamp_sigma", obj.history_clamp_sigma));
    try_save(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_save(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_save(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_save(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_save(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_save(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_save(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_save(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_save(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
}
SAVE_INSTANTIATE(gi_resolve_pass::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_resolve_pass::settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_resolve_pass::settings)
{
    try_load(ar, ser20::make_nvp("ray_count", obj.ray_count));
    try_load(ar, ser20::make_nvp("max_distance", obj.max_distance));
    // Renamed rather than reinterpreted: this used to be a world distance and is now a voxel
    // count, so an old scene's 0.05 would silently become a twentieth of a voxel -- far too small,
    // and presenting as acne rather than as a settings problem. The old key is simply not read, so
    // such a scene picks up the new default.
    try_load(ar, ser20::make_nvp("normal_bias_voxels", obj.normal_bias_voxels));
    try_load(ar, ser20::make_nvp("intensity", obj.intensity));
    try_load(ar, ser20::make_nvp("near_field_distance", obj.near_field_distance));
    try_load(ar, ser20::make_nvp("max_steps", obj.max_steps));
    try_load(ar, ser20::make_nvp("surface_bias", obj.surface_bias));
    try_load(ar, ser20::make_nvp("step_relaxation", obj.step_relaxation));
    try_load(ar, ser20::make_nvp("interpolate_cache", obj.interpolate_cache));
    try_load(ar, ser20::make_nvp("debug_ray_diagnostics", obj.debug_ray_diagnostics));
    try_load(ar, ser20::make_nvp("ray_start_voxels", obj.ray_start_voxels));
    try_load(ar, ser20::make_nvp("occlude_on_cache_miss", obj.occlude_on_cache_miss));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("enable_temporal", obj.enable_temporal));
    try_load(ar, ser20::make_nvp("max_accum_frames", obj.max_accum_frames));
    try_load(ar, ser20::make_nvp("reprojection_tolerance", obj.reprojection_tolerance));
    try_load(ar, ser20::make_nvp("history_clamp_sigma", obj.history_clamp_sigma));
    try_load(ar, ser20::make_nvp("enable_spatial_denoise", obj.enable_spatial_denoise));
    try_load(ar, ser20::make_nvp("denoise_passes", obj.denoise_passes));
    try_load(ar, ser20::make_nvp("denoise_normal_power", obj.denoise_normal_power));
    try_load(ar, ser20::make_nvp("denoise_luma_phi", obj.denoise_luma_phi));
    try_load(ar, ser20::make_nvp("denoise_plane_tolerance", obj.denoise_plane_tolerance));
    try_load(ar, ser20::make_nvp("denoise_low_count_boost", obj.denoise_low_count_boost));
    try_load(ar, ser20::make_nvp("enable_bilateral_upsample", obj.enable_bilateral_upsample));
    try_load(ar, ser20::make_nvp("upsample_normal_power", obj.upsample_normal_power));
    try_load(ar, ser20::make_nvp("upsample_plane_tolerance", obj.upsample_plane_tolerance));
}
LOAD_INSTANTIATE(gi_resolve_pass::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_resolve_pass::settings, ser20::iarchive_binary_t);

SAVE_INLINE(global_sdf_clipmap::settings)
{
    try_save(ar, ser20::make_nvp("compose_on_gpu", obj.compose_on_gpu));
    try_save(ar, ser20::make_nvp("resolution", obj.resolution));
    try_save(ar, ser20::make_nvp("base_extent", obj.base_extent));
    try_save(ar, ser20::make_nvp("level_scale", obj.level_scale));
    try_save(ar, ser20::make_nvp("blend_voxels", obj.blend_voxels));
    try_save(ar, ser20::make_nvp("max_levels_per_update", obj.max_levels_per_update));
    try_save(ar, ser20::make_nvp("cull_composition", obj.cull_composition));
}
SAVE_INSTANTIATE(global_sdf_clipmap::settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(global_sdf_clipmap::settings, ser20::oarchive_binary_t);

LOAD_INLINE(global_sdf_clipmap::settings)
{
    try_load(ar, ser20::make_nvp("compose_on_gpu", obj.compose_on_gpu));
    try_load(ar, ser20::make_nvp("resolution", obj.resolution));
    try_load(ar, ser20::make_nvp("base_extent", obj.base_extent));
    try_load(ar, ser20::make_nvp("level_scale", obj.level_scale));
    try_load(ar, ser20::make_nvp("blend_voxels", obj.blend_voxels));
    try_load(ar, ser20::make_nvp("max_levels_per_update", obj.max_levels_per_update));
    try_load(ar, ser20::make_nvp("cull_composition", obj.cull_composition));
}
LOAD_INSTANTIATE(global_sdf_clipmap::settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(global_sdf_clipmap::settings, ser20::iarchive_binary_t);

SAVE_INLINE(gi_settings)
{
    try_save(ar, ser20::make_nvp("cache", obj.cache));
    try_save(ar, ser20::make_nvp("resolve", obj.resolve));
    try_save(ar, ser20::make_nvp("clipmap", obj.clipmap));
}
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_settings, ser20::oarchive_binary_t);

LOAD_INLINE(gi_settings)
{
    try_load(ar, ser20::make_nvp("cache", obj.cache));
    try_load(ar, ser20::make_nvp("resolve", obj.resolve));
    try_load(ar, ser20::make_nvp("clipmap", obj.clipmap));
}
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_settings, ser20::iarchive_binary_t);

// --- Reflection + Serialization: gi_component ---

REFLECT(gi_component)
{
    entt::meta_factory<gi_component>{}
        .type("gi_component"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "gi_component"},
            entt::attribute{"category", "RENDERING"},
            entt::attribute{"pretty_name", "Global Illumination"},
        })
        .func<&component_meta<gi_component>::exists>("component_exists"_hs)
        .func<&component_meta<gi_component>::add>("component_add"_hs)
        .func<&component_meta<gi_component>::remove>("component_remove"_hs)
        .func<&component_meta<gi_component>::save>("component_save"_hs)
        .func<&component_meta<gi_component>::load>("component_load"_hs)
        .data<&gi_component::enabled>("enabled"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "enabled"},
            entt::attribute{"pretty_name", "Enabled"},
            entt::attribute{"tooltip", "Enable/disable surface cache global illumination.\nWhen off the "
                                       "indirect consumer falls back to SSIL, then to the environment "
                                       "probe."},
        })
        .data<&gi_component::settings>("settings"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "settings"},
            entt::attribute{"pretty_name", "Settings"},
            entt::attribute{"flattable", true},
        });
}

SAVE(gi_component)
{
    try_save(ar, ser20::make_nvp("enabled", obj.enabled));
    try_save(ar, ser20::make_nvp("settings", obj.settings));
}
SAVE_INSTANTIATE(gi_component, ser20::oarchive_associative_t);
SAVE_INSTANTIATE(gi_component, ser20::oarchive_binary_t);

LOAD(gi_component)
{
    try_load(ar, ser20::make_nvp("enabled", obj.enabled));
    try_load(ar, ser20::make_nvp("settings", obj.settings));
}
LOAD_INSTANTIATE(gi_component, ser20::iarchive_associative_t);
LOAD_INSTANTIATE(gi_component, ser20::iarchive_binary_t);

} // namespace unravel
