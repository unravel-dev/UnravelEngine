#pragma once

#include <engine/rendering/camera.h>
#include <engine/rendering/gi/gi_settings.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>
#include <engine/rendering/pipeline/pre_exposure.h>

#include <graphics/frame_buffer.h>
#include <graphics/texture.h>

namespace unravel
{

class lumen_surface_cache_pass;

/// UE r.Lumen.ScreenProbeGather.VisualizeTraces for one view (lumen_gather_pass::get_visualized_traces).
struct lumen_visualize_traces
{
    ///< Record one screen probe's rays; meanwhile the gather holds its jitter at UE's fixed index.
    bool enabled = false;
    ///< r.Lumen.ScreenProbeGather.VisualizeTracesFreeze: keep the rays recorded last.
    bool freeze = false;
    ///< The full-resolution pixel whose probe is recorded (UE View.CursorPosition); negative for the view's centre.
    math::vec2 cursor{-1.0f};
};

/**
 * @brief One view's inputs to the Lumen passes this frame (the gather, its radiance cache and adaptive probes, the
 *        short-range AO and the reflections), filled by the deferred pipeline.
 */
struct lumen_run_params
{
    /// The surface cache: global distance field hits read its cards.
    const lumen_surface_cache_pass* lumen_surface_cache = nullptr;
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
    const camera* cam{};
    /// The GI scene: its instances, distance fields and lights.
    surface_cache_system* surface_cache{};
    /// This camera's global distance field.
    surface_cache_view* view_cache{};
    /// The view's scene-colour pre-exposure: every Lumen target is in pre-exposed space.
    pre_exposure_state pre_exposure{};
    gi_settings settings{};
    /// The screen probe traces the view visualizes.
    lumen_visualize_traces visualize_traces{};
};

} // namespace unravel
