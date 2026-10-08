#pragma once

#include <engine/rendering/gi/gi_settings.h>
#include <engine/rendering/gpu_program.h>
#include <engine/rendering/pipeline/passes/lumen_radiance_cache.h>
#include <engine/rendering/pipeline/passes/lumen_surface_cache_pass.h>
#include <engine/rendering/pipeline/passes/shader_print.h>
#include <engine/rendering/pipeline/passes/tonemapping_pass.h>

#include <base/basetypes.hpp>
#include <context/context.hpp>
#include <graphics/render_view.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace unravel
{

class camera;
class lumen_gather_pass;
class surface_cache_system;
class surface_cache_view;

/// Text a debug view puts on the image (UE's visualize canvas labels: a view's name, the overview tiles' names).
struct debug_view_label
{
    ///< The text's top-left corner, as a fraction of the view's size.
    math::vec2 position{0.0f};
    std::string text;
};

/**
 * @brief Lumen's debug views (UE 5.8 r.Lumen.Visualize, and our card atlas, coverage and object grid views), drawn
 *        over the finished image: the scene views the surface cache traces (lumen_surface_cache_pass::run_debug), the
 *        screen-space views (fs_lumen_visualize_screen.sc: Dedicated Reflection Rays, ScreenProbeGather frames
 *        accumulated), and UE's two overviews, which tile views over the lit image and name each tile.
 */
class lumen_visualize_pass
{
public:
    /// The views, in the order of their debug pass ids (rendering::deferred::debug_pass_lumen_scene + view). The
    /// surface cache's views share their values with lumen_surface_cache_pass::debug_mode.
    enum class view : int
    {
        lumen_scene = 0,
        card_atlas,
        card_coverage,
        albedo,
        surface_cache,
        object_grid,
        direct_lighting,
        indirect_lighting,
        dedicated_reflection_rays,
        reflection_view,
        geometry_normals,
        normals,
        emissive,
        card_weights,
        direct_lighting_updates,
        indirect_lighting_updates,
        radiosity_frames,
        screen_probe_frames,
        overview,
        performance_overview,
        count,
    };

    struct run_params
    {
        view mode = view::lumen_scene;
        gfx::frame_buffer::ptr output;
        const camera* cam = nullptr;
        gfx::render_view* rview = nullptr;
        lumen_surface_cache_pass* surface_cache = nullptr;
        const surface_cache_system* gi_scene = nullptr;
        const surface_cache_view* view_cache = nullptr;
        ///< The view's GI settings: the reflections' trace distance and roughness, the gather temporal's frames.
        gi_settings gi{};
        ///< The view's exposure (its pre-exposure), applied to the lighting before the tone map.
        float exposure = 1.0f;
        ///< The lit image's tone mapping operator, which the views go through as UE's do.
        tonemapping_method tonemapping = tonemapping_method::none;
    };

    /// UE's world-space Lumen visualizations: console variables of their own, drawn over whichever view is active.
    struct world_settings
    {
        ///< r.Lumen.Visualize.CardPlacement: every resident card's box, with its front face translucent.
        bool card_placement = false;
        ///< r.Lumen.Visualize.CardPlacementDistance (5000 cm): the placements drawn come at least this close.
        float card_placement_distance = 50.0f;
        ///< r.Lumen.Visualize.CardPlacementIndex: only the card with this index among its mesh's cards (-1 = all).
        int card_placement_index = -1;
        ///< r.LumenScene.Radiosity.VisualizeProbes: the radiosity probes as spheres lit by their irradiance.
        bool radiosity_probes = false;
        ///< r.LumenScene.Radiosity.VisualizeProbeRadius (10 cm).
        float radiosity_probe_radius = 0.1f;
        ///< r.LumenScene.Radiosity.VisualizeProbes.ShowInvalid: also the probes with no surface to trace from, pink.
        bool radiosity_show_invalid = false;
        ///< r.Lumen.RadianceCache.Visualize 1: the radiance cache's probes as spheres showing their radiance.
        bool radiance_cache_probes = false;
        ///< r.Lumen.RadianceCache.VisualizeRadiusScale: the spheres' radius, in 0.05 cell sizes.
        float radiance_cache_radius_scale = 1.0f;
        ///< r.Lumen.RadianceCache.VisualizeClipmapIndex: only this clipmap (-1 = all of them).
        int radiance_cache_clipmap = -1;
        ///< r.Lumen.ScreenProbeGather.VisualizeTraces: the rays of the screen probe under the cursor (the view's
        ///< centre without one), lines from the probe to their hits in their radiance.
        bool screen_probe_traces = false;
        ///< r.Lumen.ScreenProbeGather.VisualizeTracesFreeze: keep the rays recorded last.
        bool screen_probe_traces_freeze = false;
        ///< r.Lumen.Reflections.VisualizeTraces: the reflection ray of the pixel under the cursor (none without one),
        ///< a line in its radiance from the pixel to its hit, with a cross at the hit.
        bool reflection_traces = false;
        ///< r.Lumen.ScreenProbeGather.Debug: the gather's probe counts as text, and the probe placement.
        bool screen_probe_gather_debug = false;
        ///< r.Lumen.RadianceCache.Stats: the radiance cache's update counters as text (its priority histogram, what it
        ///< traced, the probe atlas's occupancy).
        bool radiance_cache_stats = false;
        ///< r.Lumen.ScreenProbeGather.Debug.ProbePlacement (with the gather debug on): 0 off; 1 every screen probe as a
        ///< point, the uniform ones yellow, the adaptive ones magenta; 2 the adaptive probes; 3 every probe as a cross
        ///< with its normal.
        int screen_probe_placement = 0;
        ///< r.Lumen.Visualize.CardGenerationSurfels: every surfel candidate of each mesh's card build as a disc, green
        ///< kept, red dropped as inside geometry.
        bool card_generation_surfels = false;
        ///< r.Lumen.Visualize.CardGenerationCluster (with the surfels off): per resident card, its cluster's surfels in
        ///< the card's colour, the side's surfels an earlier card took grey, the rest blue, and rays to the near planes
        ///< the cluster's surfels were seen from.
        bool card_generation_cluster = false;
        ///< r.Lumen.Visualize.CardGenerationSurfelScale: the discs' radius, in 2 cm.
        float card_generation_surfel_scale = 1.0f;
        ///< r.Lumen.Visualize.CardGenerationMaxSurfel: at most this many discs per kind and mesh, and from 0 on each
        ///< cluster's bounds and mean normal (-1 = every disc, no bounds).
        int card_generation_max_surfel = -1;
        ///< r.Lumen.Visualize.CardPlacementDirection: the cluster view's cards facing this side only (-1 = every side;
        ///< 0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z).
        int card_placement_direction = -1;
        ///< UE View.CursorPosition: the view's pixel under the mouse; negative when the mouse is elsewhere.
        math::vec2 cursor{-1.0f};

        /// Whether one of the card generation views is on (they draw from recorded card builds).
        auto is_card_generation() const -> bool
        {
            return card_generation_surfels || card_generation_cluster;
        }

        /// Whether a visualization drawn into the scene colour is on.
        auto is_any_in_scene_color() const -> bool
        {
            return card_placement || radiosity_probes || radiance_cache_probes || is_card_generation();
        }

        /// Whether a visualization drawn over the finished image (UE's ShaderPrint ones) is on.
        auto is_any_overlay() const -> bool
        {
            return screen_probe_traces || reflection_traces || screen_probe_gather_debug || radiance_cache_stats;
        }
    };

    struct world_params
    {
        ///< The scene colour (LBUFFER) and the scene depth, before post-processing: UE draws these in the scene,
        ///< where the exposure and the tone map apply to them, depth tested.
        gfx::texture::ptr scene_color;
        gfx::texture::ptr scene_depth;
        gfx::render_view* rview = nullptr;
        const camera* cam = nullptr;
        const lumen_surface_cache_pass* surface_cache = nullptr;
        ///< The GI scene: the placements and their recorded card builds.
        surface_cache_system* gi_scene = nullptr;
        ///< The radiance cache this frame's gather updated (null when it did not run).
        const lumen_radiance_cache* radiance_cache = nullptr;
        ///< The scene colour's pre-exposure: a primitive's colour is its radiance.
        float pre_exposure = 1.0f;
        world_settings settings{};
    };

    /// UE's visualizations drawn with ShaderPrint (without their text), over the finished image: the screen probe
    /// traces, the reflection ray under the cursor and the screen probe placement.
    struct overlay_params
    {
        gfx::frame_buffer::ptr output;
        ///< The scene depth the lines behind it are checkered against.
        gfx::texture::ptr scene_depth;
        const camera* cam = nullptr;
        ///< The view, which holds the reflection pass's trace targets.
        gfx::render_view* rview = nullptr;
        ///< The gather's recorded rays and probe placement.
        const lumen_gather_pass* gather = nullptr;
        ///< The view's print buffer, which UE's text goes into.
        shader_print* print = nullptr;
        world_settings settings{};
        ///< The image's tone mapping operator, which the screen probe rays' radiance goes through (UE
        ///< VisualizeTonemap).
        tonemapping_method tonemapping = tonemapping_method::none;
    };

    lumen_visualize_pass() = default;
    ~lumen_visualize_pass();
    lumen_visualize_pass(const lumen_visualize_pass&) = delete;
    auto operator=(const lumen_visualize_pass&) -> lumen_visualize_pass& = delete;

    auto init(rtti::context& ctx) -> bool;

    /// Draws @p params.mode over @p params.output and appends the text it puts on the image to @p labels.
    void run(const run_params& params, std::vector<debug_view_label>& labels);

    /// Draws the world-space visualizations @p params.settings turns on.
    void draw_world(const world_params& params);

    /// Frees what the card generation views keep (their discs and the recorded card builds they asked for); a no-op
    /// unless they ran since the last release.
    void release_card_generation(surface_cache_system& gi_scene);

    /// Draws the overlays @p params.settings turns on.
    void draw_overlays(const overlay_params& params);

private:
    struct probe_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_probe;
        gfx::program::uniform_ptr u_lumen_visualize_probe2;
        gfx::program::uniform_ptr u_lumen_surface_cache;
        gfx::program::uniform_ptr u_lumen_radiosity;
        gfx::program::uniform_ptr s_lumen_card_depth;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_r;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_g;
        gfx::program::uniform_ptr s_lumen_radiosity_sh_b;
        ///< The radiance cache's atlas (fs_lumen_visualize_probe.sc stage 7): a black stand-in for the radiosity
        ///< probes, which do not read it.
        gfx::program::uniform_ptr s_lumen_rc_final;
        std::unique_ptr<gpu_program> program;
    };

    struct primitive_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_primitive;
        std::unique_ptr<gpu_program> program;
    };

    /// One vertex of the world-space primitives: a world position and a linear colour with alpha.
    struct primitive_vertex
    {
        float x;
        float y;
        float z;
        float r;
        float g;
        float b;
        float a;
    };

    struct surfel_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_surfel;
        gfx::program::uniform_ptr u_lumen_visualize_primitive;
        std::unique_ptr<gpu_program> program;
    };

    /// One recorded card build's discs and rays on the GPU, in mesh space.
    struct card_generation_buffers
    {
        struct range
        {
            uint32_t first = 0;
            uint32_t count = 0;
        };

        std::weak_ptr<const lumen_card_build_debug> debug;
        ///< Per disc, three vec4s of instance data: position, normal, colour.
        bgfx::VertexBufferHandle surfels{bgfx::kInvalidHandle};
        ///< The clusters' rays (primitive_vertex lines).
        bgfx::VertexBufferHandle rays{bgfx::kInvalidHandle};
        ///< The surfel candidates: kept, then dropped (each in the build's order).
        std::array<range, 2> candidates{};
        ///< Per card: its cluster's surfels, the side's surfels an earlier card took, the rest.
        std::vector<std::array<range, 3>> clusters;
        ///< Per card: its rays' vertices.
        std::vector<range> cluster_rays;
    };

    struct lines_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_lines;
        gfx::program::uniform_ptr s_scene_depth;
        std::unique_ptr<gpu_program> program;
    };

    struct counts_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_counts;
        std::unique_ptr<gpu_program> program;
    };

    /// cs_lumen_visualize_rc_stats.sc.
    struct rc_stats_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_rc_stats;
        std::unique_ptr<gpu_program> program;
    };

    struct placement_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_placement;
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_probes;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr s_lumen_probe_records;
        gfx::program::uniform_ptr s_scene_depth;
        std::unique_ptr<gpu_program> program;
    };

    struct reflection_trace_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize_reflection;
        /// The reflection pass's frame, view and trace downsample (lumen_reflection_common.sh), to find the pixel a
        /// trace texel traced from.
        gfx::program::uniform_ptr u_lumen_frame;
        gfx::program::uniform_ptr u_lumen_view;
        gfx::program::uniform_ptr u_lumen_reflection_quality;
        gfx::program::uniform_ptr s_lumen_reflection_ray;
        gfx::program::uniform_ptr s_lumen_reflection_hit;
        gfx::program::uniform_ptr s_lumen_reflection_radiance;
        gfx::program::uniform_ptr s_lumen_reflection_tiles;
        gfx::program::uniform_ptr s_lumen_depth;
        std::unique_ptr<gpu_program> program;
    };

    struct screen_program : uniforms_cache
    {
        void cache_uniforms();

        gfx::program::uniform_ptr u_lumen_visualize;
        gfx::program::uniform_ptr u_lumen_visualize_tile;
        gfx::program::uniform_ptr s_gbuffer0;
        gfx::program::uniform_ptr s_gbuffer1;
        gfx::program::uniform_ptr s_gbuffer_depth;
        gfx::program::uniform_ptr s_lumen_env_sh;
        gfx::program::uniform_ptr s_lumen_history;
        gfx::program::uniform_ptr s_scene_color;
        std::unique_ptr<gpu_program> program;
    };

    /// One view over @p tile of the output (UE's overview tile), or over all of it when @p tile is empty.
    void draw_view(const run_params& params, view mode, const irect32_t& tile);
    /// UE's overview tile @p tile_index (StochasticLightingVisualize::GetTileOutputView), drawn and labelled.
    void draw_tile(const run_params& params, view mode, int tile_index, std::vector<debug_view_label>& labels);
    /// UE's label of @p mode drawn over @p tile (all of the output when empty), if UE labels it.
    void add_label(const run_params& params,
                   view mode,
                   const irect32_t& tile,
                   std::vector<debug_view_label>& labels) const;
    void draw_screen_view(const run_params& params, view mode, const irect32_t& tile);
    /// A copy of the finished image, which the ScreenProbeGather frames view greys under its overlay.
    auto copy_output(const run_params& params) -> gfx::texture::ptr;
    /// The scene colour with a copy of the scene depth the visualizations may write (the scene's own depth is
    /// read again next frame).
    auto acquire_world_target(const world_params& params) -> gfx::frame_buffer::ptr;
    /// UE VisualizeCardPlacement.
    void draw_card_placement(const world_params& params, const gfx::frame_buffer::ptr& target);
    /// UE VisualizeCardGeneration.
    void draw_card_generation(const world_params& params, const gfx::frame_buffer::ptr& target);
    /// The surfels view: every visualized placement's surfel candidates.
    void draw_card_generation_surfels(const world_params& params, uint16_t view_id);
    /// The cluster view: every visualized resident card's cluster.
    void draw_card_generation_clusters(const world_params& params, uint16_t view_id);
    /// @p build's GPU buffers, made on first use (the cluster colours come from @p cards).
    auto get_card_generation_buffers(const std::shared_ptr<const lumen_card_build_debug>& build,
                                     const lumen_mesh_cards& cards) -> const card_generation_buffers&;
    /// UE DrawSurfels: @p range's discs (at most CardGenerationMaxSurfel) at @p local_to_world.
    void draw_surfels(const world_params& params,
                      const card_generation_buffers& buffers,
                      const card_generation_buffers::range& range,
                      const math::mat4& local_to_world,
                      uint16_t view_id);
    /// UE DrawSurfels' bounds of a cluster drawn with CardGenerationMaxSurfel set: the box of its first discs and the
    /// line along their mean normal, into line_vertices_.
    void add_cluster_bounds(const world_params& params,
                            const lumen_card_build_debug::cluster& cluster,
                            const math::vec3& color,
                            const math::mat4& local_to_world);
    /// UE RenderLumenRadiosityProbeVisualization.
    void draw_radiosity_probes(const world_params& params, const gfx::frame_buffer::ptr& target);
    /// UE RenderLumenRadianceCacheVisualization (VISUALIZE_MODE_RADIANCE).
    void draw_radiance_cache_probes(const world_params& params, const gfx::frame_buffer::ptr& target);
    /// The probe program's resources and uniforms shared by both modes.
    void bind_probe_program(const world_params& params, const math::vec4& probe);
    /// Draws @p line_count lines of @p lines (lumen_visualize.sh) over the finished image, their colours radiance tone
    /// mapped as the image is or, when @p is_raw_color, taken as they are.
    void draw_lines(const overlay_params& params,
                    bgfx::DynamicVertexBufferHandle lines,
                    uint32_t line_count,
                    bool is_raw_color);
    /// UE r.Lumen.ScreenProbeGather.VisualizeTraces.
    void draw_screen_probe_traces(const overlay_params& params);
    /// UE r.Lumen.Reflections.VisualizeTraces (VisualizeReflectionTracesCS), its text from the text line
    /// @p first_line on.
    void draw_reflection_trace(const overlay_params& params, uint32_t first_line);
    /// UE r.Lumen.ScreenProbeGather.Debug.ProbePlacement (ScreenProbeGatherDebugCS).
    void draw_probe_placement(const overlay_params& params);
    /// UE r.Lumen.ScreenProbeGather.Debug's text (ScreenProbeGatherDebugCS): the probe counts.
    void print_probe_counts(const overlay_params& params);
    /// UE r.Lumen.RadianceCache.Stats, from text line @p first_line.
    void print_radiance_cache_stats(const overlay_params& params, uint32_t first_line);
    /// Submits @p vertices with the primitive program: lines (opaque) or triangles (additive, both sides).
    void draw_primitives(const world_params& params,
                         const std::vector<primitive_vertex>& vertices,
                         bool is_lines,
                         uint16_t view_id);

    screen_program screen_program_;
    primitive_program primitive_program_;
    probe_program probe_program_;
    lines_program lines_program_;
    surfel_program surfel_program_;
    ///< The disc's twelve fan corners (vs_lumen_visualize_surfel.sc).
    bgfx::VertexBufferHandle surfel_disc_{bgfx::kInvalidHandle};
    std::unordered_map<const lumen_card_build_debug*, card_generation_buffers> card_generation_cache_;
    ///< The card generation views asked for recorded builds since the last release.
    bool has_card_generation_requests_ = false;
    placement_program placement_program_;
    counts_program counts_program_;
    rc_stats_program rc_stats_program_;
    reflection_trace_program reflection_trace_program_;
    bgfx::DynamicVertexBufferHandle page_buffer_{bgfx::kInvalidHandle};
    ///< The reflection trace's lines (cs_lumen_visualize_reflection_trace.sc).
    bgfx::DynamicVertexBufferHandle reflection_lines_{bgfx::kInvalidHandle};
    std::vector<math::vec4> visualized_pages_;
    std::vector<lumen_scene::visualized_card> visualized_cards_;
    std::vector<primitive_vertex> line_vertices_;
    std::vector<primitive_vertex> face_vertices_;
};

} // namespace unravel
