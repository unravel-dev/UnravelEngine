#include "lumen_visualize_pass.h"

#include "lumen_gather_pass.h"
#include "lumen_pass_common.h"
#include "lumen_reflection_pass.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/surface_cache_system.h>

#include <graphics/render_pass.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace unravel
{
namespace
{

/// r.Lumen.Visualize.MaxTraceDistance (100000 cm): how far the visualize's camera rays trace.
constexpr float visualize_max_trace_distance = 1000.0f;
/// The card coverage view's mesh distance-field march reaches this far.
constexpr float coverage_max_trace_distance = 200.0f;
/// StochasticLightingVisualize: the overview's tiles per row and the pixels between them.
constexpr int overview_tiles_per_row = 3;
constexpr int overview_tile_margin = 4;
/// UE's labels sit two margins in from a tile's left edge and this many pixels above its bottom (three-line labels
/// higher up).
constexpr float label_offset_y = 20.0f;
constexpr float label_offset_y_three_lines = 50.0f;
/// UE draws a card's front face with this opacity over its wire box.
constexpr float card_face_opacity = 0.25f;
/// The card's front face: added to the scene colour at card_face_opacity and seen from both sides (UE's
/// EmissiveMeshMaterial is two-sided and additive).
constexpr uint64_t card_face_state = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
/// The card box's twelve edges and its front (+z) face's two triangles over its corners (bit 0 = +x, bit 1 = +y,
/// bit 2 = +z; AddBoxFaceTriangles' face 1).
constexpr std::array<std::array<uint32_t, 2>, 12> card_box_edges = {
    {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
constexpr std::array<uint32_t, 6> card_face_triangles = {4, 7, 5, 4, 6, 7};
/// The radiosity probes: one slot per 4x4 texel cell of a 128 x 128 page (LUMEN_RADIOSITY_PROBE_SPACING), drawn
/// as 36-vertex cubes (vs_lumen_visualize_probe.sc).
constexpr uint32_t radiosity_probe_slots_per_page = (128u / 4u) * (128u / 4u);
constexpr uint32_t probe_cube_vertices = 36;
/// vs_lumen_visualize_probe.sc's modes.
constexpr float probe_mode_radiosity = 0.0f;
constexpr float probe_mode_radiance_cache = 1.0f;
/// UE's radiance cache spheres: the radius scale times this many cell sizes (VisualizeRadiusScale * 0.05).
constexpr float radiance_cache_radius_cells = 0.05f;
/// The radiance cache bindings of the probe program (indirection in the vertex shader, radiance atlas in the pixel
/// shader; the depth atlas the binding also sets goes unread).
constexpr uint8_t radiance_cache_indirection_stage = 6;
constexpr uint8_t radiance_cache_final_stage = 7;
constexpr uint8_t radiance_cache_depth_stage = 8;
/// UE DrawSurfels: discs of 2 cm (times CardGenerationSurfelScale), their fan corners (0, k, k + 1) for k = 1-4 as
/// positions with the corner in x.
constexpr float surfel_radius = 0.02f;
constexpr std::array<float, 36> surfel_disc_corners = {0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 0, 0, 2, 0, 0, 3, 0, 0,
                                                       0, 0, 0, 3, 0, 0, 4, 0, 0, 0, 0, 0, 4, 0, 0, 5, 0, 0};
/// vec4s of instance data per disc.
constexpr uint32_t surfel_instance_stride = 3;
/// DrawSurfels' colours: kept / dropped candidates, an earlier card's / an idle surfel, a ray that hit / did not.
const math::vec4 surfel_valid_color{0.0f, 1.0f, 0.0f, 1.0f};
const math::vec4 surfel_invalid_color{1.0f, 0.0f, 0.0f, 1.0f};
const math::vec4 surfel_used_color{0.5f, 0.5f, 0.5f, 1.0f};
const math::vec4 surfel_idle_color{0.0f, 0.0f, 1.0f, 1.0f};
const math::vec4 ray_hit_color{1.0f, 0.0f, 0.0f, 1.0f};
const math::vec4 ray_miss_color{1.0f, 1.0f, 1.0f, 1.0f};
/// A cluster's bounds grow 1 cm, and its mean normal is drawn 1 m long, in red.
constexpr float cluster_bounds_margin = 0.01f;
constexpr float cluster_normal_length = 1.0f;
const math::vec3 cluster_normal_color{1.0f, 0.0f, 0.0f};
/// The lines' buffer (vs_lumen_visualize_lines.sc) and the scene depth (fs_lumen_visualize_lines.sc).
constexpr uint8_t lines_buffer_stage = 0;
constexpr uint8_t lines_depth_stage = 1;
/// Two vertices per line.
constexpr uint32_t line_vertices = 2;
/// The print buffer's stages of the reflection trace (cs_lumen_visualize_reflection_trace.sc) and the probe counts
/// (cs_lumen_visualize_probe_counts.sc), and the counts' adaptive state.
constexpr uint8_t reflection_trace_print_stage = 5;
constexpr uint8_t counts_adaptive_stage = 0;
constexpr uint8_t counts_print_stage = 1;
/// The reflection trace: its ray and the three lines of the cross at its hit.
constexpr uint32_t reflection_trace_lines = 4;
/// Both texts start in the view's top left, away from the editor's navigation cube in the top right: the reflection
/// trace's below the probe counts' three lines and a blank one.
constexpr uint32_t reflection_text_line_below_probe_counts = 4;
/// vs_lumen_visualize_probe_placement.sc: four lines per probe atlas tile.
constexpr uint32_t placement_vertices_per_probe = 8;
/// UE's lines rasterize with MSAA on (its default rasterizer state), which D3D draws as quads 1.4 pixels wide.
constexpr uint64_t line_raster_state = BGFX_STATE_PT_LINES | BGFX_STATE_MSAA;
/// ShaderPrint's premultiplied composition; the image's alpha stays.
constexpr uint64_t overlay_line_state =
    BGFX_STATE_WRITE_RGB | line_raster_state | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
/// fs_lumen_visualize_screen.sc's modes.
constexpr float screen_mode_dedicated_reflection_rays = 0.0f;
constexpr float screen_mode_screen_probe_frames = 1.0f;

static_assert(int(lumen_visualize_pass::view::reflection_view) == int(lumen_surface_cache_pass::debug_mode::reflection_view) &&
                  int(lumen_visualize_pass::view::radiosity_frames) ==
                      int(lumen_surface_cache_pass::debug_mode::radiosity_frames) &&
                  int(lumen_visualize_pass::view::indirect_lighting) ==
                      int(lumen_surface_cache_pass::debug_mode::indirect_lighting),
              "the surface cache's views share their values with lumen_surface_cache_pass::debug_mode");

auto is_screen_view(lumen_visualize_pass::view mode) -> bool
{
    return mode == lumen_visualize_pass::view::dedicated_reflection_rays ||
           mode == lumen_visualize_pass::view::screen_probe_frames;
}

/// StochasticLightingVisualize::GetTileOutputView: tile @p index of the rows of overview_tiles_per_row along the
/// view's top, each a third of the view's width and height.
auto get_tile_rect(const usize32_t& size, int index) -> irect32_t
{
    const int column = index % overview_tiles_per_row;
    const int row = index / overview_tiles_per_row;
    const int margins = overview_tile_margin * (overview_tiles_per_row + 1);
    const int width = (int(size.width) - margins) / overview_tiles_per_row;
    const int height = (int(size.height) - margins) / overview_tiles_per_row;
    const int left = width * column + overview_tile_margin * (column + 1);
    const int top = height * row + overview_tile_margin * (row + 1);
    return irect32_t(left, top, left + width, top + height);
}

/// UE's label of a view (VisualizeLumenScene's LabelText); empty for the views UE draws without one.
auto get_view_label(lumen_visualize_pass::view mode, const gi_settings& gi) -> std::string
{
    switch(mode)
    {
        case lumen_visualize_pass::view::geometry_normals:
            return "Geometry Normals";
        case lumen_visualize_pass::view::albedo:
            return "GI Scene Albedo";
        case lumen_visualize_pass::view::normals:
            return "GI Scene Normals";
        case lumen_visualize_pass::view::radiosity_frames:
            return "Radiosity Num Frames Accumulated";
        case lumen_visualize_pass::view::reflection_view:
            return "Reflection View";
        case lumen_visualize_pass::view::surface_cache:
            return "GI Scene, Pink - missing Surface Cache coverage, Yellow - culled Surface Cache";
        case lumen_visualize_pass::view::dedicated_reflection_rays:
        {
            std::array<char, 160> text{};
            std::snprintf(text.data(),
                          text.size(),
                          "Pixels tracing dedicated reflection rays.\nRed - traced.\nMaxRoughness: %.2f",
                          double(lumen_pass::get_max_roughness_to_trace(gi.reflections)));
            return text.data();
        }
        default:
            return {};
    }
}

/// UE FLinearColor::MakeFromHSV8(hue, 255, 255): the fully saturated, full-value colour of @p hue (0-255 around the
/// wheel), linear.
auto make_hue_color(uint8_t hue) -> math::vec3
{
    const float h = std::fmod(float(hue) * 6.0f / 255.0f, 6.0f);
    const float sector = std::floor(h);
    const float fraction = h - sector;
    switch(int(sector))
    {
        case 0:
            return {1.0f, fraction, 0.0f};
        case 1:
            return {1.0f - fraction, 1.0f, 0.0f};
        case 2:
            return {0.0f, 1.0f, fraction};
        case 3:
            return {0.0f, 1.0f - fraction, 1.0f};
        case 4:
            return {fraction, 0.0f, 1.0f};
        default:
            return {1.0f, 0.0f, 1.0f - fraction};
    }
}

/// UE's cluster colour key: a hash of the card's mesh-space box and its index among the mesh's cards.
auto get_cluster_hash(const lumen_card& card, uint32_t index) -> uint32_t
{
    const std::array<float, 7> key = {card.origin.x,
                                      card.origin.y,
                                      card.origin.z,
                                      card.extent.x,
                                      card.extent.y,
                                      card.extent.z,
                                      float(index)};
    uint32_t hash = 2166136261u;
    const auto* bytes = reinterpret_cast<const uint8_t*>(key.data());
    for(size_t i = 0; i < sizeof(key); ++i)
    {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

auto get_surfel_instance_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin()
            .add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord2, 4, bgfx::AttribType::Float)
            .end();
        return decl;
    }();
    return layout;
}

auto get_surfel_disc_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
        return decl;
    }();
    return layout;
}

/// The instances of @p surfels of @p type (in order), in @p color, appended to @p out; returns their range.
template<typename Range>
auto append_surfels(const std::vector<lumen_card_build_debug::surfel>& surfels,
                    lumen_card_build_debug::surfel_type type,
                    const math::vec4& color,
                    std::vector<math::vec4>& out) -> Range
{
    Range range;
    range.first = uint32_t(out.size() / surfel_instance_stride);
    for(const auto& surfel : surfels)
    {
        if(surfel.type == type)
        {
            out.emplace_back(surfel.position, 0.0f);
            out.emplace_back(surfel.normal, 0.0f);
            out.push_back(color);
            ++range.count;
        }
    }
    return range;
}

auto get_primitive_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
            .end();
        return decl;
    }();
    return layout;
}

} // namespace

void lumen_visualize_pass::probe_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_probe, "u_lumen_visualize_probe", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_visualize_probe2, "u_lumen_visualize_probe2", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_surface_cache, "u_lumen_surface_cache", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_lumen_card_depth, "s_lumen_card_depth", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_radiosity_sh_r, "s_lumen_radiosity_sh_r", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_radiosity_sh_g, "s_lumen_radiosity_sh_g", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_radiosity_sh_b, "s_lumen_radiosity_sh_b", bgfx::UniformType::Sampler);
}

void lumen_visualize_pass::primitive_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_primitive, "u_lumen_visualize_primitive", bgfx::UniformType::Vec4);
}

void lumen_visualize_pass::surfel_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_surfel, "u_lumen_visualize_surfel", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_visualize_primitive, "u_lumen_visualize_primitive", bgfx::UniformType::Vec4);
}

void lumen_visualize_pass::lines_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_lines, "u_lumen_visualize_lines", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_scene_depth, "s_scene_depth", bgfx::UniformType::Sampler);
}

void lumen_visualize_pass::counts_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_counts, "u_lumen_visualize_counts", bgfx::UniformType::Vec4);
}

void lumen_visualize_pass::placement_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_placement, "u_lumen_visualize_placement", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_frame, "u_lumen_frame", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_probes, "u_lumen_probes", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_view, "u_lumen_view", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_lumen_probe_records, "s_lumen_probe_records", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_scene_depth, "s_scene_depth", bgfx::UniformType::Sampler);
}

void lumen_visualize_pass::reflection_trace_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize_reflection, "u_lumen_visualize_reflection", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_lumen_reflection_ray, "s_lumen_reflection_ray", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_reflection_hit, "s_lumen_reflection_hit", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_reflection_radiance, "s_lumen_reflection_radiance", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_depth, "s_lumen_depth", bgfx::UniformType::Sampler);
}

void lumen_visualize_pass::screen_program::cache_uniforms()
{
    cache_uniform(program.get(), u_lumen_visualize, "u_lumen_visualize", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), u_lumen_visualize_tile, "u_lumen_visualize_tile", bgfx::UniformType::Vec4);
    cache_uniform(program.get(), s_gbuffer0, "s_gbuffer0", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_gbuffer1, "s_gbuffer1", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_gbuffer_depth, "s_gbuffer_depth", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_lumen_history, "s_lumen_history", bgfx::UniformType::Sampler);
    cache_uniform(program.get(), s_scene_color, "s_scene_color", bgfx::UniformType::Sampler);
}

lumen_visualize_pass::~lumen_visualize_pass()
{
    lumen_pass::destroy_handle(page_buffer_);
    lumen_pass::destroy_handle(reflection_lines_);
    lumen_pass::destroy_handle(surfel_disc_);
    for(auto& [build, buffers] : card_generation_cache_)
    {
        lumen_pass::destroy_handle(buffers.surfels);
        lumen_pass::destroy_handle(buffers.rays);
    }
}

auto lumen_visualize_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    screen_program_.cache_uniforms();
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_screen = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_visualize_screen.sc");
    screen_program_.program = std::make_unique<gpu_program>(vs_clip_quad, fs_screen);
    primitive_program_.cache_uniforms();
    auto vs_primitive = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/vs_lumen_visualize_primitive.sc");
    auto fs_primitive = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_visualize_primitive.sc");
    primitive_program_.program = std::make_unique<gpu_program>(vs_primitive, fs_primitive);
    probe_program_.cache_uniforms();
    auto vs_probe = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/vs_lumen_visualize_probe.sc");
    auto fs_probe = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_visualize_probe.sc");
    probe_program_.program = std::make_unique<gpu_program>(vs_probe, fs_probe);
    lines_program_.cache_uniforms();
    auto vs_lines = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/vs_lumen_visualize_lines.sc");
    auto fs_lines = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_visualize_lines.sc");
    lines_program_.program = std::make_unique<gpu_program>(vs_lines, fs_lines);
    placement_program_.cache_uniforms();
    auto vs_placement = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/vs_lumen_visualize_probe_placement.sc");
    placement_program_.program = std::make_unique<gpu_program>(vs_placement, fs_lines);
    reflection_trace_program_.cache_uniforms();
    auto cs_reflection_trace =
        am.get_asset<gfx::shader>("engine:/data/shaders/lumen/cs_lumen_visualize_reflection_trace.sc");
    reflection_trace_program_.program = std::make_unique<gpu_program>(cs_reflection_trace);
    counts_program_.cache_uniforms();
    auto cs_counts = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/cs_lumen_visualize_probe_counts.sc");
    counts_program_.program = std::make_unique<gpu_program>(cs_counts);
    surfel_program_.cache_uniforms();
    auto vs_surfel = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/vs_lumen_visualize_surfel.sc");
    surfel_program_.program = std::make_unique<gpu_program>(vs_surfel, fs_primitive);
    return screen_program_.program->is_valid() && primitive_program_.program->is_valid() &&
           probe_program_.program->is_valid() && lines_program_.program->is_valid() &&
           placement_program_.program->is_valid() && reflection_trace_program_.program->is_valid() &&
           surfel_program_.program->is_valid() && counts_program_.program->is_valid();
}

void lumen_visualize_pass::run(const run_params& params, std::vector<debug_view_label>& labels)
{
    APP_SCOPE_PERF("GI/Visualize");
    if(!params.output || params.cam == nullptr || params.rview == nullptr)
    {
        return;
    }
    switch(params.mode)
    {
        case view::overview:
            // The lit image stays; the tiles go over it (UE copies the scene colour first).
            draw_tile(params, view::geometry_normals, 0, labels);
            draw_tile(params, view::reflection_view, 1, labels);
            draw_tile(params, view::surface_cache, 2, labels);
            return;
        case view::performance_overview:
            draw_tile(params, view::dedicated_reflection_rays, 0, labels);
            return;
        default:
        {
            const irect32_t full{};
            draw_view(params, params.mode, full);
            add_label(params, params.mode, full, labels);
            return;
        }
    }
}

void lumen_visualize_pass::draw_tile(const run_params& params,
                                     view mode,
                                     int tile_index,
                                     std::vector<debug_view_label>& labels)
{
    const auto size = params.output->get_size();
    const irect32_t tile = get_tile_rect(size, tile_index);
    if(tile.width() <= 0 || tile.height() <= 0)
    {
        return;
    }
    draw_view(params, mode, tile);
    add_label(params, mode, tile, labels);
}

void lumen_visualize_pass::add_label(const run_params& params,
                                     view mode,
                                     const irect32_t& tile,
                                     std::vector<debug_view_label>& labels) const
{
    debug_view_label label;
    label.text = get_view_label(mode, params.gi);
    if(label.text.empty())
    {
        return;
    }
    // Two margins in from the view's (tile's) left edge, offset_y pixels above its bottom.
    const auto size = params.output->get_size();
    const irect32_t area = tile.empty() ? irect32_t(0, 0, int32_t(size.width), int32_t(size.height)) : tile;
    const float offset_y = mode == view::dedicated_reflection_rays ? label_offset_y_three_lines : label_offset_y;
    label.position = math::vec2((float(area.left) + 2.0f * float(overview_tile_margin)) / float(size.width),
                                (float(area.top + area.height()) - offset_y) / float(size.height));
    labels.push_back(std::move(label));
}

void lumen_visualize_pass::draw_view(const run_params& params, view mode, const irect32_t& tile)
{
    if(is_screen_view(mode))
    {
        draw_screen_view(params, mode, tile);
        return;
    }
    if(params.surface_cache == nullptr)
    {
        return;
    }
    lumen_surface_cache_pass::debug_params scene;
    scene.output = params.output.get();
    scene.cam = params.cam;
    scene.gi_scene = params.gi_scene;
    scene.view_cache = params.view_cache;
    scene.mode = lumen_surface_cache_pass::debug_mode(int(mode));
    // The reflection view traces as far as the reflections do (UE Lumen::GetMaxTraceDistance); the others as far as
    // UE's visualize.
    scene.max_trace_distance = mode == view::reflection_view
                                   ? lumen_pass::get_max_trace_distance(params.gi.diffuse.max_trace_distance)
                               : mode == view::card_coverage ? coverage_max_trace_distance
                                                             : visualize_max_trace_distance;
    scene.tile = tile;
    scene.environment_sh = params.rview->tex_safe_get("IRRADIANCE_SH");
    scene.exposure = params.exposure;
    scene.tonemapping = params.tonemapping;
    params.surface_cache->run_debug(scene);
}

void lumen_visualize_pass::draw_world(const world_params& params)
{
    if(!params.scene_color || !params.scene_depth || params.rview == nullptr || params.cam == nullptr ||
       params.surface_cache == nullptr)
    {
        return;
    }
    const auto target = acquire_world_target(params);
    if(params.settings.radiance_cache_probes)
    {
        draw_radiance_cache_probes(params, target);
    }
    if(params.settings.radiosity_probes)
    {
        draw_radiosity_probes(params, target);
    }
    if(params.settings.card_placement)
    {
        draw_card_placement(params, target);
    }
    if(params.settings.is_card_generation())
    {
        draw_card_generation(params, target);
    }
}

void lumen_visualize_pass::release_card_generation(surface_cache_system& gi_scene)
{
    if(!has_card_generation_requests_)
    {
        return;
    }
    for(auto& [build, buffers] : card_generation_cache_)
    {
        lumen_pass::destroy_handle(buffers.surfels);
        lumen_pass::destroy_handle(buffers.rays);
    }
    card_generation_cache_.clear();
    gi_scene.release_lumen_card_build_debug();
    has_card_generation_requests_ = false;
}

void lumen_visualize_pass::draw_card_generation(const world_params& params, const gfx::frame_buffer::ptr& target)
{
    APP_SCOPE_PERF("GI/Visualize Card Generation");
    if(params.gi_scene == nullptr || !surfel_program_.program || !surfel_program_.program->is_valid())
    {
        return;
    }
    if(!bgfx::isValid(surfel_disc_))
    {
        surfel_disc_ = bgfx::createVertexBuffer(bgfx::copy(surfel_disc_corners.data(), sizeof(surfel_disc_corners)),
                                                get_surfel_disc_layout());
    }
    has_card_generation_requests_ = true;
    gfx::render_pass pass("GI/Card Generation");
    pass.bind(target.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    line_vertices_.clear();
    // UE draws the clusters only with the surfels off.
    if(params.settings.card_generation_surfels)
    {
        draw_card_generation_surfels(params, pass.id);
    }
    else
    {
        draw_card_generation_clusters(params, pass.id);
    }
    draw_primitives(params, line_vertices_, true, pass.id);
}

void lumen_visualize_pass::draw_card_generation_surfels(const world_params& params, uint16_t view_id)
{
    auto& gi_scene = *params.gi_scene;
    const auto& settings = params.settings;
    for(const auto& source : gi_scene.get_lumen_sources())
    {
        if(!source.cards || !lumen_scene::is_visualized(source.cards->bounds,
                                                         source.local_to_world,
                                                         params.cam->get_position(),
                                                         settings.card_placement_distance,
                                                         params.cam->get_frustum()))
        {
            continue;
        }
        const auto build = gi_scene.acquire_lumen_card_build_debug(source);
        if(!build)
        {
            continue;
        }
        const auto& buffers = get_card_generation_buffers(build, *source.cards);
        // Kept and dropped candidates; UE's surfel rays are never recorded by its build.
        draw_surfels(params, buffers, buffers.candidates[0], source.local_to_world, view_id);
        draw_surfels(params, buffers, buffers.candidates[1], source.local_to_world, view_id);
    }
}

void lumen_visualize_pass::draw_card_generation_clusters(const world_params& params, uint16_t view_id)
{
    auto& gi_scene = *params.gi_scene;
    const auto& settings = params.settings;
    const auto& sources = gi_scene.get_lumen_sources();
    params.surface_cache->get_visualized_cards(params.cam->get_position(),
                                               settings.card_placement_distance,
                                               params.cam->get_frustum(),
                                               visualized_cards_);
    for(const auto& card : visualized_cards_)
    {
        const bool is_index_shown =
            settings.card_placement_index < 0 || card.index_in_mesh == uint32_t(settings.card_placement_index);
        const bool is_direction_shown =
            settings.card_placement_direction < 0 || card.direction == uint32_t(settings.card_placement_direction);
        if(!is_index_shown || !is_direction_shown || card.source_index >= sources.size())
        {
            continue;
        }
        const auto& source = sources[card.source_index];
        const auto build = source.cards ? gi_scene.acquire_lumen_card_build_debug(source) : nullptr;
        if(!build || card.index_in_mesh >= build->clusters.size() ||
           card.index_in_mesh >= source.cards->cards.size())
        {
            continue;
        }
        const auto& buffers = get_card_generation_buffers(build, *source.cards);
        for(const auto& range : buffers.clusters[card.index_in_mesh])
        {
            draw_surfels(params, buffers, range, source.local_to_world, view_id);
        }
        const auto& rays = buffers.cluster_rays[card.index_in_mesh];
        if(rays.count > 0 && primitive_program_.program && primitive_program_.program->is_valid())
        {
            primitive_program_.program->begin();
            gfx::set_uniform(primitive_program_.u_lumen_visualize_primitive,
                             math::vec4(params.pre_exposure, 0.0f, 0.0f, 0.0f));
            gfx::set_transform(source.local_to_world);
            bgfx::setVertexBuffer(0, buffers.rays, rays.first, rays.count);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | line_raster_state);
            bgfx::submit(view_id, primitive_program_.program->native_handle());
            primitive_program_.program->end();
        }
        if(settings.card_generation_max_surfel >= 0)
        {
            const uint8_t hue = uint8_t(get_cluster_hash(source.cards->cards[card.index_in_mesh],
                                                         card.index_in_mesh) &
                                        0xffu);
            add_cluster_bounds(params, build->clusters[card.index_in_mesh], make_hue_color(hue), source.local_to_world);
        }
    }
}

auto lumen_visualize_pass::get_card_generation_buffers(const std::shared_ptr<const lumen_card_build_debug>& build,
                                                       const lumen_mesh_cards& cards) -> const card_generation_buffers&
{
    using surfel_type = lumen_card_build_debug::surfel_type;
    using range = card_generation_buffers::range;
    auto& buffers = card_generation_cache_[build.get()];
    if(buffers.debug.lock() == build && bgfx::isValid(buffers.surfels))
    {
        return buffers;
    }
    lumen_pass::destroy_handle(buffers.surfels);
    lumen_pass::destroy_handle(buffers.rays);
    buffers = card_generation_buffers{};
    buffers.debug = build;
    std::vector<math::vec4> instances;
    buffers.candidates[0] = append_surfels<range>(build->surfels, surfel_type::valid, surfel_valid_color, instances);
    buffers.candidates[1] = append_surfels<range>(build->surfels, surfel_type::invalid, surfel_invalid_color, instances);
    std::vector<primitive_vertex> rays;
    for(uint32_t c = 0; c < uint32_t(build->clusters.size()); ++c)
    {
        const auto& cluster = build->clusters[c];
        const uint8_t hue = c < cards.cards.size() ? uint8_t(get_cluster_hash(cards.cards[c], c) & 0xffu) : 0u;
        const math::vec4 card_color(make_hue_color(hue), 1.0f);
        buffers.clusters.push_back({append_surfels<range>(cluster.surfels, surfel_type::cluster, card_color, instances),
                                    append_surfels<range>(cluster.surfels, surfel_type::used, surfel_used_color, instances),
                                    append_surfels<range>(cluster.surfels, surfel_type::idle, surfel_idle_color, instances)});
        range ray_range;
        ray_range.first = uint32_t(rays.size());
        for(const auto& ray : cluster.rays)
        {
            const math::vec4& color = ray.is_hit ? ray_hit_color : ray_miss_color;
            rays.push_back({ray.start.x, ray.start.y, ray.start.z, color.x, color.y, color.z, color.w});
            rays.push_back({ray.end.x, ray.end.y, ray.end.z, color.x, color.y, color.z, color.w});
        }
        ray_range.count = uint32_t(rays.size()) - ray_range.first;
        buffers.cluster_rays.push_back(ray_range);
    }
    if(instances.empty())
    {
        instances.emplace_back(0.0f);
        instances.emplace_back(0.0f);
        instances.emplace_back(0.0f);
    }
    buffers.surfels = bgfx::createVertexBuffer(bgfx::copy(instances.data(), uint32_t(instances.size() * sizeof(math::vec4))),
                                               get_surfel_instance_layout());
    if(rays.empty())
    {
        rays.push_back({});
        rays.push_back({});
    }
    buffers.rays = bgfx::createVertexBuffer(bgfx::copy(rays.data(), uint32_t(rays.size() * sizeof(primitive_vertex))),
                                            get_primitive_layout());
    return buffers;
}

void lumen_visualize_pass::draw_surfels(const world_params& params,
                                        const card_generation_buffers& buffers,
                                        const card_generation_buffers::range& range,
                                        const math::mat4& local_to_world,
                                        uint16_t view_id)
{
    const int max_surfels = params.settings.card_generation_max_surfel;
    const uint32_t count = max_surfels >= 0 ? std::min(range.count, uint32_t(max_surfels)) : range.count;
    if(count == 0)
    {
        return;
    }
    auto& program = surfel_program_;
    program.program->begin();
    gfx::set_uniform(program.u_lumen_visualize_surfel,
                     math::vec4(surfel_radius * params.settings.card_generation_surfel_scale, 0.0f, 0.0f, 0.0f));
    gfx::set_uniform(program.u_lumen_visualize_primitive, math::vec4(params.pre_exposure, 0.0f, 0.0f, 0.0f));
    gfx::set_transform(local_to_world);
    bgfx::setVertexBuffer(0, surfel_disc_);
    bgfx::setInstanceDataBuffer(buffers.surfels, range.first, count);
    // UE's unlit discs: opaque, depth tested and written (into the depth copy), seen from their normal's side only.
    // The fan winds counter-clockwise on screen from that side, so the clockwise side is culled.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);
    bgfx::submit(view_id, program.program->native_handle());
    program.program->end();
}

void lumen_visualize_pass::add_cluster_bounds(const world_params& params,
                                              const lumen_card_build_debug::cluster& cluster,
                                              const math::vec3& color,
                                              const math::mat4& local_to_world)
{
    // DrawSurfels counts the discs it draws of the cluster's own kind and boxes them.
    const uint32_t max_surfels = uint32_t(params.settings.card_generation_max_surfel);
    math::bbox local_bounds;
    local_bounds.reset();
    math::vec3 normal_sum(0.0f);
    const math::mat3 normal_matrix = glm::transpose(glm::inverse(math::mat3(local_to_world)));
    uint32_t count = 0;
    for(const auto& surfel : cluster.surfels)
    {
        if(count >= max_surfels)
        {
            break;
        }
        if(surfel.type != lumen_card_build_debug::surfel_type::cluster)
        {
            continue;
        }
        local_bounds.add_point(surfel.position);
        normal_sum += math::normalize(normal_matrix * surfel.normal);
        ++count;
    }
    if(count == 0)
    {
        return;
    }
    local_bounds.min -= math::vec3(cluster_bounds_margin);
    local_bounds.max += math::vec3(cluster_bounds_margin);
    std::array<math::vec3, 8> corners{};
    for(uint32_t i = 0; i < 8; ++i)
    {
        const math::vec3 local((i & 1u) != 0u ? local_bounds.max.x : local_bounds.min.x,
                               (i & 2u) != 0u ? local_bounds.max.y : local_bounds.min.y,
                               (i & 4u) != 0u ? local_bounds.max.z : local_bounds.min.z);
        corners[i] = math::vec3(local_to_world * math::vec4(local, 1.0f));
    }
    for(const auto& edge : card_box_edges)
    {
        for(uint32_t corner : edge)
        {
            const auto& p = corners[corner];
            line_vertices_.push_back({p.x, p.y, p.z, color.x, color.y, color.z, 1.0f});
        }
    }
    // UE adds the discs' mean world normal to the box's local centre.
    const math::vec3 center = local_bounds.get_center();
    const math::vec3 start(local_to_world * math::vec4(center, 1.0f));
    const math::vec3 end(local_to_world *
                         math::vec4(center + math::normalize(normal_sum) * cluster_normal_length, 1.0f));
    line_vertices_.push_back({start.x, start.y, start.z, cluster_normal_color.x, cluster_normal_color.y,
                              cluster_normal_color.z, 1.0f});
    line_vertices_.push_back({end.x, end.y, end.z, cluster_normal_color.x, cluster_normal_color.y,
                              cluster_normal_color.z, 1.0f});
}

void lumen_visualize_pass::draw_overlays(const overlay_params& params)
{
    APP_SCOPE_PERF("GI/Visualize Overlays");
    if(!params.output || !params.scene_depth || params.cam == nullptr || params.rview == nullptr ||
       params.gather == nullptr)
    {
        return;
    }
    const auto& settings = params.settings;
    if(settings.screen_probe_traces)
    {
        draw_screen_probe_traces(params);
    }
    if(settings.reflection_traces)
    {
        draw_reflection_trace(params,
                              settings.screen_probe_gather_debug ? reflection_text_line_below_probe_counts : 0u);
    }
    if(settings.screen_probe_gather_debug && settings.screen_probe_placement > 0)
    {
        draw_probe_placement(params);
    }
    if(settings.screen_probe_gather_debug)
    {
        print_probe_counts(params);
    }
}

void lumen_visualize_pass::print_probe_counts(const overlay_params& params)
{
    auto& program = counts_program_;
    const auto& placement = params.gather->get_probe_placement();
    if(params.print == nullptr || placement.render_frame != gfx::get_render_frame() || !program.program ||
       !program.program->is_valid())
    {
        return;
    }
    gfx::render_pass pass("GI/Visualize Probe Counts");
    program.program->begin();
    params.gather->bind_adaptive_state(counts_adaptive_stage);
    params.print->bind(counts_print_stage, params.scene_depth->get_size());
    const float lattice_probes = placement.probes[0] * placement.probes[1];
    gfx::set_uniform(program.u_lumen_visualize_counts,
                     math::vec4(lattice_probes, float(placement.adaptive_capacity), 0.0f, 0.0f));
    bgfx::dispatch(pass.id, program.program->native_handle(), 1, 1, 1);
    program.program->end();
}

void lumen_visualize_pass::draw_screen_probe_traces(const overlay_params& params)
{
    const auto traces = params.gather->get_visualized_traces();
    if(bgfx::isValid(traces))
    {
        draw_lines(params, traces, lumen_gather_pass::visualized_trace_count, false);
    }
}

void lumen_visualize_pass::draw_reflection_trace(const overlay_params& params, uint32_t first_line)
{
    auto& program = reflection_trace_program_;
    auto& rview = *params.rview;
    const auto& cursor = params.settings.cursor;
    const auto size = params.scene_depth->get_size();
    // UE shows nothing without the cursor, outside the view, or before this frame's reflections traced.
    const bool is_cursor_in_view = cursor.x >= 0.0f && cursor.y >= 0.0f && cursor.x < float(size.width) &&
                                   cursor.y < float(size.height);
    const auto ray = rview.tex_safe_get(lumen_reflection_pass::ray_texture);
    const auto hit = rview.tex_safe_get(lumen_reflection_pass::hit_texture);
    const auto radiance = rview.tex_safe_get(lumen_reflection_pass::radiance_texture);
    const bool is_traced = rview.data_get(lumen_reflection_pass::traced_frame_key, 0u) == gfx::get_render_frame();
    if(!is_cursor_in_view || !is_traced || !ray || !hit || !radiance || !program.program ||
       !program.program->is_valid() || ray->get_size() != size)
    {
        return;
    }
    if(!bgfx::isValid(reflection_lines_))
    {
        reflection_lines_ = bgfx::createDynamicVertexBuffer(reflection_trace_lines *
                                                                lumen_gather_pass::visualized_trace_stride,
                                                            lumen_pass::get_vec4_layout(),
                                                            BGFX_BUFFER_COMPUTE_READ_WRITE);
    }
    gfx::render_pass pass("GI/Visualize Reflection Trace");
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    program.program->begin();
    gfx::set_texture(program.s_lumen_reflection_ray, 0, ray);
    gfx::set_texture(program.s_lumen_reflection_hit, 1, hit);
    gfx::set_texture(program.s_lumen_reflection_radiance, 2, radiance);
    gfx::set_texture(program.s_lumen_depth, 3, params.scene_depth);
    bgfx::setBuffer(4, reflection_lines_, bgfx::Access::Write);
    if(params.print != nullptr)
    {
        params.print->bind(reflection_trace_print_stage, size);
    }
    gfx::set_uniform(program.u_lumen_visualize_reflection, math::vec4(cursor.x, cursor.y, float(first_line), 0.0f));
    bgfx::dispatch(pass.id, program.program->native_handle(), 1, 1, 1);
    program.program->end();
    draw_lines(params, reflection_lines_, reflection_trace_lines, true);
}

void lumen_visualize_pass::draw_probe_placement(const overlay_params& params)
{
    auto& program = placement_program_;
    const auto& placement = params.gather->get_probe_placement();
    if(!placement.records || placement.render_frame != gfx::get_render_frame() || !program.program ||
       !program.program->is_valid())
    {
        return;
    }
    // The atlas tiles: the uniform probes' rows and the adaptive probes' below them.
    const uint32_t probe_slots = uint32_t(placement.probes[0]) * uint32_t(placement.probes[3]);
    gfx::render_pass pass("GI/Screen Probe Placement");
    pass.bind(params.output.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    program.program->begin();
    gfx::set_texture(program.s_lumen_probe_records, 0, placement.records);
    gfx::set_texture(program.s_scene_depth, lines_depth_stage, params.scene_depth);
    gfx::set_uniform(program.u_lumen_frame, placement.frame.data());
    gfx::set_uniform(program.u_lumen_probes, placement.probes.data());
    gfx::set_uniform(program.u_lumen_view, placement.view.data());
    const float mode = float(std::clamp(params.settings.screen_probe_placement, 1, 3));
    gfx::set_uniform(program.u_lumen_visualize_placement, math::vec4(mode, 0.0f, 0.0f, 0.0f));
    bgfx::setVertexCount(probe_slots * placement_vertices_per_probe);
    bgfx::setState(overlay_line_state);
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
}

void lumen_visualize_pass::draw_lines(const overlay_params& params,
                                      bgfx::DynamicVertexBufferHandle lines,
                                      uint32_t line_count,
                                      bool is_raw_color)
{
    auto& program = lines_program_;
    if(!program.program || !program.program->is_valid())
    {
        return;
    }
    gfx::render_pass pass("GI/Visualize Lines");
    pass.bind(params.output.get());
    // Over the resolved image: the unjittered projection.
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection_unjittered());
    program.program->begin();
    bgfx::setBuffer(lines_buffer_stage, lines, bgfx::Access::Read);
    gfx::set_texture(program.s_scene_depth, lines_depth_stage, params.scene_depth);
    gfx::set_uniform(program.u_lumen_visualize_lines,
                     math::vec4(float(params.tonemapping), is_raw_color ? 1.0f : 0.0f, 0.0f, 0.0f));
    bgfx::setVertexCount(line_count * line_vertices);
    bgfx::setState(overlay_line_state);
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
}

auto lumen_visualize_pass::acquire_world_target(const world_params& params) -> gfx::frame_buffer::ptr
{
    auto& rview = *params.rview;
    const auto size = params.scene_depth->get_size();
    const auto format = params.scene_depth->info.format;
    auto& depth = rview.tex_get_or_emplace("LUMEN_VISUALIZE_DEPTH");
    if(gfx::needs_recreate(depth, size, format))
    {
        depth.reset();
        depth = std::make_shared<gfx::texture>(uint16_t(size.width),
                                               uint16_t(size.height),
                                               false,
                                               1,
                                               format,
                                               BGFX_TEXTURE_RT | BGFX_TEXTURE_BLIT_DST);
    }
    auto& target = rview.fbo_get_or_emplace("LUMEN_VISUALIZE_TARGET");
    if(!target || target->get_texture(0) != params.scene_color || target->get_texture(1) != depth)
    {
        target = std::make_shared<gfx::frame_buffer>();
        target->populate({params.scene_color, depth});
    }
    gfx::render_pass copy_pass("GI/Visualize Depth Copy");
    bgfx::blit(copy_pass.id,
               bgfx::TextureRegion{.handle = depth->native_handle()},
               bgfx::TextureRegion{.handle = params.scene_depth->native_handle()});
    return target;
}

void lumen_visualize_pass::draw_radiosity_probes(const world_params& params, const gfx::frame_buffer::ptr& target)
{
    APP_SCOPE_PERF("GI/Visualize Radiosity Probes");
    auto& program = probe_program_;
    const auto* surface_cache = params.surface_cache;
    if(!program.program || !program.program->is_valid() || !surface_cache->has_lighting())
    {
        return;
    }
    surface_cache->get_visualized_pages(visualized_pages_);
    const uint32_t page_count = uint32_t(visualized_pages_.size() / 3);
    if(page_count == 0)
    {
        return;
    }
    lumen_pass::upload_vec4_table(page_buffer_, visualized_pages_);
    gfx::render_pass pass("GI/Radiosity Probes");
    pass.bind(target.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    program.program->begin();
    bind_probe_program(params,
                       math::vec4(probe_mode_radiosity,
                                  params.settings.radiosity_probe_radius,
                                  params.settings.radiosity_show_invalid ? 1.0f : 0.0f,
                                  float(radiosity_probe_slots_per_page)));
    bgfx::setVertexCount(page_count * radiosity_probe_slots_per_page * probe_cube_vertices);
    // Depth written into the copy, so nearer spheres hide farther ones; both cube sides shade the same sphere.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
}

void lumen_visualize_pass::draw_radiance_cache_probes(const world_params& params,
                                                      const gfx::frame_buffer::ptr& target)
{
    APP_SCOPE_PERF("GI/Visualize Radiance Cache");
    auto& program = probe_program_;
    if(!program.program || !program.program->is_valid() || params.radiance_cache == nullptr)
    {
        return;
    }
    constexpr int clipmap_count = int(gi::lumen::LUMEN_RADIANCE_CACHE_CLIPMAPS);
    constexpr uint32_t cells_per_clipmap = uint32_t(gi::lumen::LUMEN_RADIANCE_CACHE_GRID) *
                                           uint32_t(gi::lumen::LUMEN_RADIANCE_CACHE_GRID) *
                                           uint32_t(gi::lumen::LUMEN_RADIANCE_CACHE_GRID);
    const int selected = params.settings.radiance_cache_clipmap;
    const int first_clipmap = selected >= 0 ? std::min(selected, clipmap_count - 1) : 0;
    const uint32_t drawn_clipmaps = selected >= 0 ? 1u : uint32_t(clipmap_count);
    if(params.surface_cache != nullptr && params.surface_cache->has_lighting())
    {
        // The page list the probe program also reads: anything valid will do, the radiance cache mode skips it.
        lumen_pass::upload_vec4_table(page_buffer_, {});
    }
    gfx::render_pass pass("GI/Radiance Cache Probes");
    pass.bind(target.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    program.program->begin();
    bind_probe_program(params,
                       math::vec4(probe_mode_radiance_cache,
                                  radiance_cache_radius_cells * params.settings.radiance_cache_radius_scale,
                                  float(first_clipmap),
                                  0.0f));
    params.radiance_cache->bind_for_sampling(radiance_cache_indirection_stage,
                                             radiance_cache_final_stage,
                                             radiance_cache_depth_stage);
    bgfx::setVertexCount(drawn_clipmaps * cells_per_clipmap * probe_cube_vertices);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
}

void lumen_visualize_pass::bind_probe_program(const world_params& params, const math::vec4& probe)
{
    auto& program = probe_program_;
    const auto* surface_cache = params.surface_cache;
    if(!bgfx::isValid(page_buffer_))
    {
        lumen_pass::upload_vec4_table(page_buffer_, {});
    }
    bgfx::setBuffer(0, surface_cache->get_scene_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(1, page_buffer_, bgfx::Access::Read);
    gfx::set_texture(program.s_lumen_card_depth, 2, surface_cache->get_depth_atlas());
    gfx::set_texture(program.s_lumen_radiosity_sh_r, 3, surface_cache->get_radiosity_sh(0));
    gfx::set_texture(program.s_lumen_radiosity_sh_g, 4, surface_cache->get_radiosity_sh(1));
    gfx::set_texture(program.s_lumen_radiosity_sh_b, 5, surface_cache->get_radiosity_sh(2));
    gfx::set_uniform(program.u_lumen_surface_cache, surface_cache->get_surface_cache_params());
    gfx::set_uniform(program.u_lumen_visualize_probe, probe);
    gfx::set_uniform(program.u_lumen_visualize_probe2, math::vec4(params.pre_exposure, 0.0f, 0.0f, 0.0f));
}

void lumen_visualize_pass::draw_card_placement(const world_params& params, const gfx::frame_buffer::ptr& target)
{
    APP_SCOPE_PERF("GI/Visualize Card Placement");
    const auto& settings = params.settings;
    params.surface_cache->get_visualized_cards(params.cam->get_position(),
                                               settings.card_placement_distance,
                                               params.cam->get_frustum(),
                                               visualized_cards_);
    line_vertices_.clear();
    face_vertices_.clear();
    for(const auto& card : visualized_cards_)
    {
        if(settings.card_placement_index >= 0 && card.index_in_mesh != uint32_t(settings.card_placement_index))
        {
            continue;
        }
        const auto& box = card.box;
        const math::vec3 color = make_hue_color(uint8_t(card.hash & 0xffu));
        std::array<math::vec3, 8> corners{};
        for(uint32_t i = 0; i < 8; ++i)
        {
            corners[i] = box.origin + box.axis_x * ((i & 1u) != 0u ? box.extent.x : -box.extent.x) +
                         box.axis_y * ((i & 2u) != 0u ? box.extent.y : -box.extent.y) +
                         box.axis_z * ((i & 4u) != 0u ? box.extent.z : -box.extent.z);
        }
        // DrawWireBox, then the card's projection face translucent (alpha 0.25).
        for(const auto& edge : card_box_edges)
        {
            for(uint32_t corner : edge)
            {
                const auto& p = corners[corner];
                line_vertices_.push_back({p.x, p.y, p.z, color.x, color.y, color.z, 1.0f});
            }
        }
        for(uint32_t corner : card_face_triangles)
        {
            const auto& p = corners[corner];
            face_vertices_.push_back({p.x, p.y, p.z, color.x, color.y, color.z, card_face_opacity});
        }
    }
    if(line_vertices_.empty())
    {
        return;
    }
    gfx::render_pass pass("GI/Card Placement");
    pass.bind(target.get());
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    draw_primitives(params, line_vertices_, true, pass.id);
    draw_primitives(params, face_vertices_, false, pass.id);
}

void lumen_visualize_pass::draw_primitives(const world_params& params,
                                           const std::vector<primitive_vertex>& vertices,
                                           bool is_lines,
                                           uint16_t view_id)
{
    auto& program = primitive_program_;
    if(vertices.empty() || !program.program || !program.program->is_valid())
    {
        return;
    }
    const auto& layout = get_primitive_layout();
    // The transient buffer may hold fewer vertices than a frame's cards make: they go in batches of whole primitives.
    const uint32_t primitive_size = is_lines ? 2u : 3u;
    uint32_t first = 0;
    while(first < uint32_t(vertices.size()))
    {
        uint32_t count = std::min(bgfx::getAvailTransientVertexBuffer(uint32_t(vertices.size()) - first, layout),
                                  uint32_t(vertices.size()) - first);
        count -= count % primitive_size;
        if(count == 0)
        {
            return;
        }
        bgfx::TransientVertexBuffer buffer;
        bgfx::allocTransientVertexBuffer(&buffer, count, layout);
        std::memcpy(buffer.data, vertices.data() + first, size_t(count) * sizeof(primitive_vertex));
        program.program->begin();
        const math::vec4 scale(params.pre_exposure, 0.0f, 0.0f, 0.0f);
        gfx::set_uniform(program.u_lumen_visualize_primitive, scale);
        bgfx::setVertexBuffer(0, &buffer);
        // Depth tested against the scene, never written: the scene's depth is read again next frame.
        const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS |
                               (is_lines ? line_raster_state : card_face_state);
        bgfx::setState(state);
        bgfx::submit(view_id, program.program->native_handle());
        program.program->end();
        first += count;
    }
}

auto lumen_visualize_pass::copy_output(const run_params& params) -> gfx::texture::ptr
{
    const auto& source = params.output->get_texture(0);
    const auto size = params.output->get_size();
    auto& copy = params.rview->tex_get_or_emplace("LUMEN_VISUALIZE_SCENE_COLOR");
    if(gfx::needs_recreate(copy, size, source->info.format))
    {
        copy.reset();
        copy = std::make_shared<gfx::texture>(uint16_t(size.width),
                                              uint16_t(size.height),
                                              false,
                                              1,
                                              source->info.format,
                                              BGFX_TEXTURE_BLIT_DST | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
    }
    gfx::render_pass blit_pass("GI/Visualize Copy");
    bgfx::blit(blit_pass.id,
               bgfx::TextureRegion{.handle = copy->native_handle()},
               bgfx::TextureRegion{.handle = source->native_handle()});
    return copy;
}

void lumen_visualize_pass::draw_screen_view(const run_params& params, view mode, const irect32_t& tile)
{
    auto& program = screen_program_;
    const auto& gbuffer = params.rview->fbo_safe_get("GBUFFER");
    if(!program.program || !program.program->is_valid() || !gbuffer)
    {
        return;
    }
    const bool is_frames_view = mode == view::screen_probe_frames;
    const auto black = default_textures::get().black_texture();
    gfx::texture::ptr history;
    gfx::texture::ptr scene_color;
    if(is_frames_view)
    {
        // Without this frame's history UE reads a black stand-in: no frames anywhere.
        history = lumen_gather_pass::get_current_history(*params.rview);
        scene_color = copy_output(params);
    }
    const auto environment_sh = params.rview->tex_safe_get("IRRADIANCE_SH");
    gfx::render_pass pass("GI/Visualize");
    pass.bind(params.output.get());
    const bool is_tile = !tile.empty();
    if(is_tile)
    {
        const auto x = uint16_t(tile.left);
        const auto y = uint16_t(tile.top);
        const auto width = uint16_t(tile.width());
        const auto height = uint16_t(tile.height());
        bgfx::setViewRect(pass.id, x, y, width, height);
        bgfx::setViewScissor(pass.id, x, y, width, height);
    }
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    program.program->begin();
    gfx::set_texture(program.s_gbuffer0, 0, gbuffer->get_texture(0));
    gfx::set_texture(program.s_gbuffer1, 1, gbuffer->get_texture(1));
    gfx::set_texture(program.s_gbuffer_depth, 2, gbuffer->get_texture(4));
    gfx::set_texture(program.s_lumen_env_sh, 3, environment_sh ? environment_sh : black);
    gfx::set_texture(program.s_lumen_history, 4, history ? history : black);
    gfx::set_texture(program.s_scene_color, 5, scene_color ? scene_color : black);
    const math::vec4 visualize(is_frames_view ? screen_mode_screen_probe_frames : screen_mode_dedicated_reflection_rays,
                               lumen_pass::get_max_roughness_to_trace(params.gi.reflections),
                               float(params.tonemapping),
                               params.exposure);
    gfx::set_uniform(program.u_lumen_visualize, visualize);
    const math::vec4 tile_params(is_tile ? float(tile.width()) : 0.0f,
                                 is_tile ? float(tile.height()) : 0.0f,
                                 lumen_pass::get_temporal_max_frames(params.gi.diffuse.update_speed),
                                 0.0f);
    gfx::set_uniform(program.u_lumen_visualize_tile, tile_params);
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, program.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    program.program->end();
    bgfx::discard();
}

} // namespace unravel
