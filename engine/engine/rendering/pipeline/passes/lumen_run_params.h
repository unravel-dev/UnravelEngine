#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_project_settings.h>
#include <engine/rendering/gi/gi_settings.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/pipeline/pre_exposure.h>

#include <graphics/frame_buffer.h>
#include <graphics/texture.h>

namespace unravel
{

class lumen_surface_cache_pass;
class lumen_surface_cache_feedback;

/// The screen probe trace visualization for one view (lumen_gather_pass::get_visualized_traces).
struct lumen_visualize_traces
{
    ///< Record one screen probe's rays; meanwhile the gather holds its jitter at a fixed index, so the rays hold still.
    bool enabled = false;
    ///< Keep the rays recorded last.
    bool freeze = false;
    ///< The full-resolution pixel whose probe is recorded (the cursor); negative for the view's centre.
    math::vec2 cursor{-1.0f};
};

/**
 * @brief One view's inputs to the GI passes this frame (the gather, its radiance cache and adaptive probes, the
 *        short-range AO and the reflections), filled by the deferred pipeline.
 */
struct lumen_run_params
{
    /// The surface cache: global distance field hits read its cards.
    const lumen_surface_cache_pass* lumen_surface_cache = nullptr;
    /// Its feedback, which the reflections fill (lumen_surface_cache_pass::get_feedback); null runs none.
    lumen_surface_cache_feedback* surface_cache_feedback = nullptr;
    gfx::frame_buffer::ptr g_buffer;
    /// Last frame's depth, which validates reprojected history. Null starts every history over.
    gfx::texture::ptr prev_depth;
    /// This frame's Hi-Z depth pyramid for the screen traces; null traces the distance field alone.
    gfx::texture::ptr hiz;
    /// The environment's radiance SH, the sky a ray reads when it leaves the scene. Null before the irradiance
    /// pass has run once.
    gfx::texture::ptr irradiance_sh;
    /// Last frame's linear scene colour (PREV_SCENE_HDR), the radiance of screen trace hits. Null on the first
    /// frame.
    gfx::texture::ptr prev_color;
    /// This frame's velocity buffer (velocity_encoding.sh): moving surfaces reproject from where they were last frame
    /// and screen hits on them shorten the gather's history. Null when the velocity pass did not run: every surface
    /// reprojects as static.
    gfx::texture::ptr velocity;
    const camera* cam{};
    /// The GI scene: its instances, distance fields and lights.
    surface_cache_system* surface_cache{};
    /// This camera's global distance field.
    surface_cache_view* view_cache{};
    /// The view's scene-colour pre-exposure: every GI target is in pre-exposed space.
    pre_exposure_state pre_exposure{};
    gi_settings settings{};
    /// The project's scalability tiers (gi_project_settings).
    gi_project_settings::quality_level gi_quality = gi_project_settings::quality_level::epic;
    gi_project_settings::quality_level reflection_quality = gi_project_settings::quality_level::epic;
    /// The traced reflections follow the gather this frame and read its rough specular; without them nothing does, and
    /// the gather skips it.
    bool has_traced_reflections = true;
    /// The screen probe traces the view visualizes.
    lumen_visualize_traces visualize_traces{};
    /// The camera jumped this frame: the screen-space histories start over.
    bool camera_cut = false;
    /// The user is editing the scene in this view (pipeline::run_params::is_being_edited).
    bool is_being_edited = false;
    /// The lighting changed globally this frame (surface_cache_system::has_global_lighting_change): the radiance cache
    /// rebuilds and the gather history starts over.
    bool global_lighting_change = false;
};

} // namespace unravel
