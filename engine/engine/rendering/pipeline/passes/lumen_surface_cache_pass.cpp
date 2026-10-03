#include "lumen_surface_cache_pass.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>

#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// Threads per group edge of the copy, lighting and radiosity kernels (NUM_THREADS(8, 8, 1)), also their tile.
constexpr uint32_t tile_size = 8;
/// float4s per copy tile record (lumen_surface_cache_lighting.sh LUMEN_CARD_COPY_TILE_STRIDE).
constexpr uint32_t copy_tile_stride = 4;
/// float4s per lighting tile record (cs_lumen_card_lighting.sc, lumen_radiosity_common.sh).
constexpr uint32_t light_tile_stride = 3;
/// Radiosity probes every 4 texels (lumen_radiosity_common.sh), SH atlases at a quarter of the atlas.
constexpr uint32_t radiosity_probe_spacing = 4;
/// MaxRayIntensity of radiosity rays, in pre-exposed units.
constexpr float radiosity_max_ray_intensity = 40.0f;
/// bgfx's per-dimension dispatch limit: larger tile lists go out in several dispatches.
constexpr uint32_t max_groups_per_dispatch = 65535;
/// The object grid: cells of 2 x 2 x 2 clipmap voxels, 4 x 4 x 4 threads per group.
constexpr uint32_t object_grid_downsample = 2;
constexpr uint32_t object_grid_group = 4;
/// Object grid reach: the cell's half diagonal (1.44 x its half extent) plus 3 voxel extents.
constexpr float object_grid_diagonal = 1.44f;
constexpr float object_grid_range_voxel_extents = 3.0f;
/// Levels whose object grid is rebuilt per frame at most.
constexpr uint32_t object_grid_levels_per_frame = 2;
/// The capture camera for the G-buffer shader's distance dither sits this far in front of the card.
constexpr float capture_far_eye_distance = 1.0e5f;
/// Camera-ray trace of the Lumen Scene views: UE's Lumen max trace distance (200 m, the Reflection View's); the step
/// count and surface bias drive the mesh distance-field march of the coverage view.
constexpr float debug_max_distance = 200.0f;
constexpr float debug_max_steps = 256.0f;
constexpr float debug_surface_bias = 0.5f;
/// Updates between surface cache stats lines in the log.
constexpr uint32_t stats_log_period = 300;
/// UE's global distance field level 0 in Lumen views (R/LumenScene.cpp:77), 252 voxels like ours:
/// experiment_ue_card_tolerance measures the card sampling bias of global-SDF hits in its voxel extents.
constexpr float ue_global_sdf_extent = 50.0f;

auto get_vec4_layout() -> const bgfx::VertexLayout&
{
    static const bgfx::VertexLayout layout = []()
    {
        bgfx::VertexLayout decl;
        decl.begin().add(bgfx::Attrib::TexCoord0, 4, bgfx::AttribType::Float).end();
        return decl;
    }();
    return layout;
}

/// Uploads a vec4 table, growing the buffer when needed; an empty table keeps one zero element.
void upload_vec4_table(bgfx::DynamicVertexBufferHandle& buffer, const std::vector<math::vec4>& data)
{
    const uint32_t count = std::max(uint32_t(data.size()), 1u);
    if(!bgfx::isValid(buffer))
    {
        buffer = bgfx::createDynamicVertexBuffer(count,
                                                 get_vec4_layout(),
                                                 BGFX_BUFFER_COMPUTE_READ | BGFX_BUFFER_ALLOW_RESIZE);
    }
    if(data.empty())
    {
        const math::vec4 zero(0.0f);
        bgfx::update(buffer, 0, bgfx::copy(&zero, sizeof(zero)));
        return;
    }
    bgfx::update(buffer, 0, bgfx::copy(data.data(), uint32_t(data.size() * sizeof(math::vec4))));
}

auto make_atlas(uint32_t size, bgfx::TextureFormat::Enum format) -> gfx::texture::ptr
{
    return std::make_shared<gfx::texture>(uint16_t(size),
                                          uint16_t(size),
                                          false,
                                          1,
                                          format,
                                          BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
}

auto make_capture_texture(uint32_t size, bgfx::TextureFormat::Enum format) -> gfx::texture::ptr
{
    return std::make_shared<gfx::texture>(uint16_t(size),
                                          uint16_t(size),
                                          false,
                                          1,
                                          format,
                                          BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
}

void append_light_tiles(std::vector<math::vec4>& tiles,
                        uint32_t card_index,
                        uint32_t update_index,
                        const math::vec4& card_uv_rect,
                        const math::uvec2& atlas_offset,
                        const math::uvec2& size)
{
    for(uint32_t y = 0; y < size.y; y += tile_size)
    {
        for(uint32_t x = 0; x < size.x; x += tile_size)
        {
            tiles.emplace_back(float(atlas_offset.x + x), float(atlas_offset.y + y), float(card_index), float(update_index));
            tiles.push_back(card_uv_rect);
            tiles.emplace_back(float(atlas_offset.x), float(atlas_offset.y), float(size.x), float(size.y));
        }
    }
}

void bind_atlas_image(uint8_t stage, const gfx::texture::ptr& tex, bgfx::Access::Enum access, bgfx::TextureFormat::Enum format)
{
    bgfx::setImage(stage, tex->native_handle(), 0, access, format);
}

} // namespace

void lumen_surface_cache_pass::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_card_copy, "u_lumen_card_copy", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_card_lighting, "u_lumen_card_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_radiosity, "u_lumen_radiosity", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_surface_cache, "u_lumen_surface_cache", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_object_grid, "u_lumen_object_grid", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_object_grid_origin, "u_lumen_object_grid_origin", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_object_grid_levels, "u_lumen_object_grid_levels", bgfx::UniformType::Vec4, 4);
    cache_uniform(nullptr, u_lumen_object_grid_params, "u_lumen_object_grid_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_hit_lighting, "u_lumen_hit_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_debug, "u_lumen_debug", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_debug2, "u_lumen_debug2", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_sdf_params, "u_sdf_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr,
                  u_sdf_grid_params,
                  "u_sdf_grid_params",
                  bgfx::UniformType::Vec4,
                  uint16_t(gi::GI_SDF_GRID_PARAMS_VEC4));
    cache_uniform(nullptr,
                  u_sdf_clipmap_levels,
                  "u_sdf_clipmap_levels",
                  bgfx::UniformType::Vec4,
                  uint16_t(global_sdf_clipmap::level_count));
    cache_uniform(nullptr, u_sdf_clipmap_params, "u_sdf_clipmap_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_gpu_light_params, "u_gpu_light_params", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_capture_rt0, "s_lumen_capture_rt0", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_capture_rt1, "s_lumen_capture_rt1", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_capture_rt2, "s_lumen_capture_rt2", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_capture_depth, "s_lumen_capture_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_albedo, "s_lumen_card_albedo", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_normal, "s_lumen_card_normal", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_emissive, "s_lumen_card_emissive", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_depth, "s_lumen_card_depth", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_direct, "s_lumen_card_direct", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_indirect, "s_lumen_card_indirect", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_frames, "s_lumen_radiosity_frames", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_resample_direct, "s_lumen_resample_direct", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_resample_indirect, "s_lumen_resample_indirect", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_resample_frames, "s_lumen_resample_frames", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_final, "s_lumen_card_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_object_grid, "s_lumen_object_grid", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_trace, "s_lumen_radiosity_trace", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_r, "s_lumen_radiosity_sh_r", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_g, "s_lumen_radiosity_sh_g", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_b, "s_lumen_radiosity_sh_b", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
}

auto lumen_surface_cache_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
    uniforms_.cache_uniforms();
    const auto load = [&](const char* name) -> gpu_program::ptr
    {
        auto shader = am.get_asset<gfx::shader>(std::string("engine:/data/shaders/lumen/") + name + ".sc");
        return std::make_shared<gpu_program>(shader);
    };
    copy_program_ = load("cs_lumen_card_copy");
    resample_program_ = load("cs_lumen_card_resample");
    lighting_program_ = load("cs_lumen_card_lighting");
    object_grid_program_ = load("cs_lumen_object_grid");
    radiosity_trace_program_ = load("cs_lumen_radiosity_trace");
    radiosity_sh_program_ = load("cs_lumen_radiosity_sh");
    radiosity_integrate_program_ = load("cs_lumen_radiosity_integrate");
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_debug = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_scene_debug.sc");
    debug_program_ = std::make_unique<gpu_program>(vs_clip_quad, fs_debug);
    scene_.init(lumen_scene::settings{});
    create_targets();
    if(!is_ready())
    {
        APPLOG_WARNING("[Lumen] Surface cache programs failed to load; the surface cache is unavailable.");
    }
    return is_ready();
}

auto lumen_surface_cache_pass::is_ready() const -> bool
{
    const auto valid = [](const gpu_program* program)
    {
        return program != nullptr && program->is_valid();
    };
    return valid(copy_program_.get()) && valid(resample_program_.get()) && valid(lighting_program_.get()) &&
           valid(object_grid_program_.get()) &&
           valid(radiosity_trace_program_.get()) && valid(radiosity_sh_program_.get()) &&
           valid(radiosity_integrate_program_.get()) && valid(debug_program_.get());
}

void lumen_surface_cache_pass::create_targets()
{
    const auto& s = scene_.get_settings();
    albedo_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA8);
    normal_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA8);
    emissive_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA16F);
    depth_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::R32F);
    direct_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA16F);
    indirect_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA16F);
    final_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA16F);
    radiosity_trace_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA16F);
    radiosity_sh_r_ = make_atlas(s.atlas_size / radiosity_probe_spacing, bgfx::TextureFormat::RGBA16F);
    radiosity_sh_g_ = make_atlas(s.atlas_size / radiosity_probe_spacing, bgfx::TextureFormat::RGBA16F);
    radiosity_sh_b_ = make_atlas(s.atlas_size / radiosity_probe_spacing, bgfx::TextureFormat::RGBA16F);
    radiosity_frames_ = make_atlas(s.atlas_size / tile_size, bgfx::TextureFormat::R32F);
    resample_direct_ = make_atlas(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F);
    resample_indirect_ = make_atlas(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F);
    resample_frames_ = make_atlas(s.capture_atlas_size / tile_size, bgfx::TextureFormat::R32F);
    // The G-buffer program's four targets, in its formats, plus depth.
    std::vector<gfx::texture::ptr> targets = {make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA8),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA8),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::D32F)};
    capture_target_ = std::make_shared<gfx::frame_buffer>(targets);
    object_grid_dummy_ = std::make_shared<gfx::texture>(uint16_t(1),
                                                        uint16_t(1),
                                                        uint16_t(1),
                                                        false,
                                                        bgfx::TextureFormat::RGBA32F,
                                                        BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
}

auto lumen_surface_cache_pass::has_lighting() const -> bool
{
    return is_lit_ && object_grid_ != nullptr;
}

void lumen_surface_cache_pass::bind_for_sampling(uint8_t scene_stage,
                                                 uint8_t final_stage,
                                                 uint8_t grid_stage,
                                                 bool enabled) const
{
    const bool use = enabled && has_lighting();
    bgfx::setBuffer(scene_stage, scene_buffer_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_card_final, final_stage, final_atlas_);
    gfx::set_texture(uniforms_.s_lumen_object_grid, grid_stage, object_grid_ ? object_grid_ : object_grid_dummy_);
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    gfx::set_uniform(uniforms_.u_lumen_object_grid_levels, object_grid_levels_.data(), 4);
    gfx::set_uniform(uniforms_.u_lumen_object_grid_params, get_object_grid_params());
    const math::vec4 hit_lighting(use ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_hit_lighting, hit_lighting);
}

auto lumen_surface_cache_pass::get_surface_cache_params() const -> math::vec4
{
    return {float(scene_.get_settings().atlas_size),
            float(page_table_base_),
            float(instance_table_base_),
            float(scene_.get_instance_table().size() / lumen_scene::instance_stride)};
}

auto lumen_surface_cache_pass::get_object_grid_params() const -> math::vec4
{
    return {float(object_grid_resolution_), card_bias_scale_, 0.0f, 0.0f};
}

void lumen_surface_cache_pass::update(const surface_cache_system& gi_scene,
                                      const math::vec3& view_origin,
                                      const math::frustum& view_frustum)
{
    APP_SCOPE_PERF("GI/Lumen/Surface Cache Update");
    const auto& lumen_sources = gi_scene.get_lumen_sources();
    experiment_flags_ = gi_scene.get_experiment_flags();
    card_bias_scale_ = (experiment_flags_ & experiment_ue_card_tolerance) != 0u
                           ? ue_global_sdf_extent / gi::lumen::LUMEN_GLOBAL_SDF_EXTENT
                           : 1.0f;
    sources_.clear();
    sources_.reserve(lumen_sources.size());
    const auto& instances = gi_scene.get_instances();
    for(const auto& src : lumen_sources)
    {
        sources_.push_back({src.identity,
                            src.instance_index,
                            src.cards,
                            src.local_to_world,
                            instances[src.instance_index].is_emissive_light_source});
    }
    scene_.set_hold_resident_resolutions((experiment_flags_ & experiment_hold_card_resolution) != 0u);
    scene_.update(sources_, uint32_t(gi_scene.get_instances().size()), view_origin);
    scene_.schedule_lighting(view_origin, view_frustum);
    upload_scene_table();
    upload_vec4_table(resample_table_buffer_, scene_.get_resample_table());
    build_copy_tiles();
    build_light_tiles();
    reallocated_since_log_ += scene_.get_stats().reallocated;
    if((++update_count_ % stats_log_period) == 0u)
    {
        const auto& st = scene_.get_stats();
        APPLOG_INFO("[Lumen] surface cache: {} cards, {} resident, pages {} / {}, desired texels {}, "
                    "captures {}, downgraded {}, reallocated {} in {} frames, lit tiles {} direct (cut bucket {}) / "
                    "{} radiosity (cut bucket {})",
                    st.cards,
                    st.resident_cards,
                    st.pages_used,
                    st.pages_total,
                    st.texels_desired,
                    st.captures,
                    st.downgraded,
                    reallocated_since_log_,
                    stats_log_period,
                    st.lit_tiles[lumen_scene::lighting_direct],
                    st.cut_bucket[lumen_scene::lighting_direct],
                    st.lit_tiles[lumen_scene::lighting_radiosity],
                    st.cut_bucket[lumen_scene::lighting_radiosity]);
        reallocated_since_log_ = 0;
    }
}

void lumen_surface_cache_pass::upload_scene_table()
{
    // One buffer: cards, then the page table, then the per-instance card ranges.
    const auto& cards = scene_.get_card_table();
    const auto& pages = scene_.get_page_table();
    const auto& instances = scene_.get_instance_table();
    std::vector<math::vec4> table;
    table.reserve(cards.size() + pages.size() + instances.size());
    table.insert(table.end(), cards.begin(), cards.end());
    table.insert(table.end(), pages.begin(), pages.end());
    table.insert(table.end(), instances.begin(), instances.end());
    page_table_base_ = uint32_t(cards.size());
    instance_table_base_ = uint32_t(cards.size() + pages.size());
    upload_vec4_table(scene_buffer_, table);
}

void lumen_surface_cache_pass::build_copy_tiles()
{
    std::vector<math::vec4> tiles;
    const bool resamples = (experiment_flags_ & experiment_no_lighting_resample) == 0u;
    has_resample_ = false;
    for(const auto& cap : scene_.get_captures())
    {
        const int32_t resample_card = resamples ? cap.resample_card : -1;
        has_resample_ = has_resample_ || resample_card >= 0;
        for(uint32_t y = 0; y < cap.size.y; y += tile_size)
        {
            for(uint32_t x = 0; x < cap.size.x; x += tile_size)
            {
                tiles.emplace_back(float(cap.capture_offset.x + x),
                                   float(cap.capture_offset.y + y),
                                   float(cap.atlas_offset.x + x),
                                   float(cap.atlas_offset.y + y));
                tiles.push_back(cap.card_uv_rect);
                tiles.emplace_back(float(x), float(y), float(cap.size.x), float(cap.size.y));
                tiles.emplace_back(float(resample_card), 0.0f, 0.0f, 0.0f);
            }
        }
    }
    copy_tile_count_ = uint32_t(tiles.size() / copy_tile_stride);
    upload_vec4_table(copy_tile_buffer_, tiles);
}

void lumen_surface_cache_pass::build_light_tiles()
{
    std::vector<math::vec4> tiles;
    const auto& pages = scene_.get_resident_pages();
    const auto append_context = [&](lumen_scene::lighting_context context)
    {
        for(const auto& lit : scene_.get_lit_pages(context))
        {
            const auto& page = pages[lit.resident_page];
            append_light_tiles(tiles, page.card_index, lit.update_index, page.card_uv_rect, page.atlas_offset, page.size);
        }
    };
    append_context(lumen_scene::lighting_direct);
    direct_tile_count_ = uint32_t(tiles.size() / light_tile_stride);
    append_context(lumen_scene::lighting_radiosity);
    radiosity_tile_base_ = direct_tile_count_;
    radiosity_tile_count_ = uint32_t(tiles.size() / light_tile_stride) - direct_tile_count_;
    upload_vec4_table(light_tile_buffer_, tiles);
}

auto lumen_surface_cache_pass::compute_capture_view(const lumen_scene::capture& cap) const -> capture_view
{
    const auto& cards = scene_.get_card_table();
    const size_t base = size_t(cap.card_index) * lumen_scene::card_stride;
    const math::vec3 origin(cards[base + 0]);
    const math::vec3 axis_x(cards[base + 1]);
    const math::vec3 axis_y(cards[base + 2]);
    const math::vec3 axis_z(cards[base + 3]);
    const math::vec3 extent(cards[base + 1].w, cards[base + 2].w, cards[base + 3].w);
    // The page's card-local rectangle: u along +axis_x, v along -axis_y (CardUV = (0.5, -0.5) x / extent + 0.5).
    const float x0 = (2.0f * cap.card_uv_rect.x - 1.0f) * extent.x;
    const float x1 = (2.0f * cap.card_uv_rect.z - 1.0f) * extent.x;
    const float y_top = (1.0f - 2.0f * cap.card_uv_rect.y) * extent.y;
    const float y_bottom = (1.0f - 2.0f * cap.card_uv_rect.w) * extent.y;
    const float cx = 0.5f * (x0 + x1);
    const float cy = 0.5f * (y_top + y_bottom);
    const float hx = std::max(0.5f * (x1 - x0), 1e-6f);
    const float hy = std::max(0.5f * (y_top - y_bottom), 1e-6f);
    const float ez = std::max(extent.z, 1e-6f);
    // clip.x = (q.x - cx) / hx, clip.y = (q.y - cy) / hy, clip.z = (ez - q.z) / (2 ez), with
    // q = card-local position: depth 0 at the card's front plane, 1 at its back.
    const math::vec3 row_x = axis_x / hx;
    const math::vec3 row_y = axis_y / hy;
    const math::vec3 row_z = -axis_z / (2.0f * ez);
    math::mat4 m(0.0f);
    for(int c = 0; c < 3; ++c)
    {
        m[c][0] = row_x[c];
        m[c][1] = row_y[c];
        m[c][2] = row_z[c];
    }
    m[3][0] = -(math::dot(origin, axis_x) + cx) / hx;
    m[3][1] = -(math::dot(origin, axis_y) + cy) / hy;
    m[3][2] = (ez + math::dot(origin, axis_z)) / (2.0f * ez);
    m[3][3] = 1.0f;
    capture_view view;
    view.view_proj = m;
    view.orientation = math::determinant(math::mat3(m)) < 0.0f ? -1.0f : 1.0f;
    view.far_eye = origin + axis_z * capture_far_eye_distance;
    return view;
}

void lumen_surface_cache_pass::copy_captures()
{
    if(copy_tile_count_ == 0 || !is_ready())
    {
        return;
    }
    APP_SCOPE_PERF("GI/Lumen/Card Copy");
    if(has_resample_)
    {
        resample_lighting();
    }
    gfx::render_pass pass("GI/Lumen Card Copy");
    copy_program_->begin();
    bind_atlas_image(0, albedo_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
    bind_atlas_image(1, normal_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
    bind_atlas_image(2, emissive_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(3, depth_atlas_, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bind_atlas_image(4, final_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(5, indirect_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(6, radiosity_frames_, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bind_atlas_image(7, direct_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_capture_rt0, 8, capture_target_->get_texture(0));
    gfx::set_texture(uniforms_.s_lumen_capture_rt1, 9, capture_target_->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_capture_rt2, 10, capture_target_->get_texture(2));
    gfx::set_texture(uniforms_.s_lumen_capture_depth, 11, capture_target_->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_resample_direct, 12, resample_direct_);
    gfx::set_texture(uniforms_.s_lumen_resample_indirect, 13, resample_indirect_);
    gfx::set_texture(uniforms_.s_lumen_resample_frames, 14, resample_frames_);
    bgfx::setBuffer(15, copy_tile_buffer_, bgfx::Access::Read);
    const math::vec4 params(float(copy_tile_count_), 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_card_copy, params);
    bgfx::dispatch(pass.id, copy_program_->native_handle(), copy_tile_count_, 1, 1);
    copy_program_->end();
}

void lumen_surface_cache_pass::resample_lighting()
{
    gfx::render_pass pass("GI/Lumen Card Lighting Resample");
    resample_program_->begin();
    bind_atlas_image(0, resample_direct_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(1, resample_indirect_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(2, resample_frames_, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    gfx::set_texture(uniforms_.s_lumen_card_direct, 3, direct_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_indirect, 4, indirect_atlas_);
    gfx::set_texture(uniforms_.s_lumen_radiosity_frames, 5, radiosity_frames_);
    bgfx::setBuffer(6, copy_tile_buffer_, bgfx::Access::Read);
    bgfx::setBuffer(7, resample_table_buffer_, bgfx::Access::Read);
    // The resample table is a scene table of the previous allocations: its page entries follow the card records.
    const math::vec4 surface_cache(float(scene_.get_settings().atlas_size),
                                   float(scene_.get_resample_page_base()),
                                   0.0f,
                                   0.0f);
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, surface_cache);
    const math::vec4 params(float(copy_tile_count_), 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_card_copy, params);
    bgfx::dispatch(pass.id, resample_program_->native_handle(), copy_tile_count_, 1, 1);
    resample_program_->end();
}

void lumen_surface_cache_pass::bind_sdf_instances(const surface_cache_system& gi_scene) const
{
    auto& atlas = const_cast<surface_cache_system&>(gi_scene).get_atlas();
    gfx::set_texture(uniforms_.s_sdf_atlas, 0, atlas.get_atlas_texture());
    bgfx::setBuffer(1, atlas.get_header_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(2, atlas.get_indirection_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(3, gi_scene.get_instance_buffer(), bgfx::Access::Read);
    bgfx::setBuffer(12, gi_scene.get_grid_buffer(), bgfx::Access::Read);
    gfx::set_uniform(uniforms_.u_sdf_grid_params, gi_scene.get_grid_params(), gi::GI_SDF_GRID_PARAMS_VEC4);
    const math::vec4 sdf_params(float(atlas.get_atlas_brick_dim()),
                                float(atlas.get_atlas_voxel_dim()),
                                float(gi_scene.get_instances().size()),
                                float(gi_scene.get_emitters().size()));
    gfx::set_uniform(uniforms_.u_sdf_params, sdf_params);
}

void lumen_surface_cache_pass::update_object_grid(const surface_cache_system& gi_scene,
                                                  const surface_cache_view& view_cache)
{
    const auto& clipmap = view_cache.get_clipmap();
    const uint32_t resolution = view_cache.get_clipmap_gpu().get_attr_resolution();
    if(resolution != object_grid_resolution_ || !object_grid_)
    {
        object_grid_resolution_ = resolution;
        object_grid_ = std::make_shared<gfx::texture>(uint16_t(resolution),
                                                      uint16_t(resolution),
                                                      uint16_t(resolution * global_sdf_clipmap::level_count),
                                                      false,
                                                      bgfx::TextureFormat::RGBA32F,
                                                      BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_POINT |
                                                          BGFX_SAMPLER_UVW_CLAMP);
        object_grid_built_ = {};
    }
    gfx::render_pass pass("GI/Lumen Object Grid");
    // The cells hold instance indices, positions in a list rebuilt every frame: when the order moved every level is
    // stale at once and is rebuilt this frame, whatever the per-frame budget.
    const uint64_t instance_order = gi_scene.get_instance_order_hash();
    const bool order_moved = std::any_of(object_grid_built_.begin(),
                                         object_grid_built_.end(),
                                         [&](const object_grid_level& level)
                                         {
                                             return level.is_built && level.instance_order != instance_order;
                                         });
    const uint32_t budget = order_moved ? global_sdf_clipmap::level_count : object_grid_levels_per_frame;
    uint32_t rebuilt = 0;
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count && rebuilt < budget; ++level)
    {
        const auto& lvl = clipmap.get_level(level);
        auto& built = object_grid_built_[level];
        if(lvl.voxel_size <= 0.0f || (built.is_built && built.origin == lvl.origin &&
                                      built.content_fingerprint == lvl.content_fingerprint &&
                                      built.voxel_size == lvl.voxel_size && built.instance_order == instance_order))
        {
            continue;
        }
        const float cell_size = lvl.voxel_size * float(object_grid_downsample);
        const float voxel_extent = 0.5f * lvl.voxel_size;
        const float reach = object_grid_diagonal * 0.5f * cell_size + object_grid_range_voxel_extents * voxel_extent;
        object_grid_program_->begin();
        bind_sdf_instances(gi_scene);
        gfx::set_image_3d(4, object_grid_->native_handle(), 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
        const math::vec4 params(float(level), float(resolution), cell_size, reach);
        gfx::set_uniform(uniforms_.u_lumen_object_grid, params);
        const math::vec4 origin(lvl.origin, 0.0f);
        gfx::set_uniform(uniforms_.u_lumen_object_grid_origin, origin);
        const uint32_t groups = (resolution + object_grid_group - 1u) / object_grid_group;
        bgfx::dispatch(pass.id, object_grid_program_->native_handle(), groups, groups, groups);
        object_grid_program_->end();
        built = {lvl.origin, lvl.voxel_size, lvl.content_fingerprint, instance_order, true};
        object_grid_levels_[level] = math::vec4(lvl.origin, cell_size);
        ++rebuilt;
    }
}

void lumen_surface_cache_pass::dispatch_direct(const lighting_inputs& inputs, uint32_t first, uint32_t count)
{
    const auto& clipmap_gpu = inputs.view_cache->get_clipmap_gpu();
    const auto& light_buffer = inputs.gi_scene->get_light_buffer();
    gfx::render_pass pass("GI/Lumen Card Direct Lighting");
    lighting_program_->begin();
    bind_atlas_image(0, direct_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(1, final_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    gfx::set_texture(uniforms_.s_lumen_card_albedo, 11, albedo_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_emissive, 12, emissive_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_indirect, 13, indirect_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_normal, 3, normal_atlas_);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiment_flags_));
    if(light_buffer.is_valid())
    {
        bgfx::setBuffer(5, light_buffer.get_buffer(), bgfx::Access::Read);
    }
    const math::vec4 light_params(light_buffer.is_valid() ? float(light_buffer.get_light_count()) : 0.0f,
                                  0.0f,
                                  0.0f,
                                  0.0f);
    gfx::set_uniform(uniforms_.u_gpu_light_params, light_params);
    gfx::set_texture(uniforms_.s_lumen_card_depth, 7, depth_atlas_);
    bgfx::setBuffer(8, light_tile_buffer_, bgfx::Access::Read);
    bgfx::setBuffer(9, scene_buffer_, bgfx::Access::Read);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    const math::vec4 lighting(float(first), float(count), 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
    bgfx::dispatch(pass.id, lighting_program_->native_handle(), count, 1, 1);
    lighting_program_->end();
}

void lumen_surface_cache_pass::dispatch_radiosity(const lighting_inputs& inputs, uint32_t first, uint32_t count)
{
    const auto& clipmap_gpu = inputs.view_cache->get_clipmap_gpu();
    const math::vec4 lighting(float(first), float(count), 0.0f, 0.0f);
    const auto& environment =
        inputs.environment_sh ? inputs.environment_sh : default_textures::get().black_texture();
    {
        gfx::render_pass pass("GI/Lumen Radiosity Trace");
        radiosity_trace_program_->begin();
        bind_atlas_image(0, radiosity_trace_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(uniforms_.s_lumen_card_depth, 1, depth_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_normal, 2, normal_atlas_);
        gfx::set_texture(uniforms_.s_lumen_env_sh, 3, environment);
        gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiment_flags_));
        bgfx::setBuffer(5, light_tile_buffer_, bgfx::Access::Read);
        bgfx::setBuffer(6, scene_buffer_, bgfx::Access::Read);
        gfx::set_texture(uniforms_.s_lumen_card_final, 7, final_atlas_);
        gfx::set_texture(uniforms_.s_lumen_object_grid, 8, object_grid_);
        gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
        gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
        gfx::set_uniform(uniforms_.u_lumen_object_grid_levels, object_grid_levels_.data(), 4);
        gfx::set_uniform(uniforms_.u_lumen_object_grid_params, get_object_grid_params());
        gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
        const math::vec4 radiosity(radiosity_max_ray_intensity / std::max(inputs.view_exposure, 1e-6f), 0.0f, 0.0f, 0.0f);
        gfx::set_uniform(uniforms_.u_lumen_radiosity, radiosity);
        bgfx::dispatch(pass.id, radiosity_trace_program_->native_handle(), count, 1, 1);
        radiosity_trace_program_->end();
    }
    {
        gfx::render_pass pass("GI/Lumen Radiosity Filter SH");
        radiosity_sh_program_->begin();
        bind_atlas_image(0, radiosity_sh_r_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bind_atlas_image(1, radiosity_sh_g_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bind_atlas_image(2, radiosity_sh_b_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        gfx::set_texture(uniforms_.s_lumen_radiosity_trace, 3, radiosity_trace_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_depth, 4, depth_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_normal, 5, normal_atlas_);
        bgfx::setBuffer(6, light_tile_buffer_, bgfx::Access::Read);
        bgfx::setBuffer(7, scene_buffer_, bgfx::Access::Read);
        gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
        gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
        bgfx::dispatch(pass.id, radiosity_sh_program_->native_handle(), count, 1, 1);
        radiosity_sh_program_->end();
    }
    {
        gfx::render_pass pass("GI/Lumen Radiosity Integrate");
        radiosity_integrate_program_->begin();
        bind_atlas_image(0, indirect_atlas_, bgfx::Access::ReadWrite, bgfx::TextureFormat::RGBA16F);
        bind_atlas_image(1, final_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
        bind_atlas_image(2, radiosity_frames_, bgfx::Access::ReadWrite, bgfx::TextureFormat::R32F);
        gfx::set_texture(uniforms_.s_lumen_card_depth, 3, depth_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_normal, 4, normal_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_albedo, 5, albedo_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_emissive, 6, emissive_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_direct, 7, direct_atlas_);
        gfx::set_texture(uniforms_.s_lumen_radiosity_sh_r, 8, radiosity_sh_r_);
        gfx::set_texture(uniforms_.s_lumen_radiosity_sh_g, 9, radiosity_sh_g_);
        gfx::set_texture(uniforms_.s_lumen_radiosity_sh_b, 10, radiosity_sh_b_);
        bgfx::setBuffer(11, light_tile_buffer_, bgfx::Access::Read);
        bgfx::setBuffer(12, scene_buffer_, bgfx::Access::Read);
        gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
        gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
        bgfx::dispatch(pass.id, radiosity_integrate_program_->native_handle(), count, 1, 1);
        radiosity_integrate_program_->end();
    }
}

void lumen_surface_cache_pass::light(const lighting_inputs& inputs)
{
    if(!is_ready() || inputs.gi_scene == nullptr || inputs.view_cache == nullptr ||
       !inputs.view_cache->get_clipmap_gpu().is_valid())
    {
        return;
    }
    APP_SCOPE_PERF("GI/Lumen/Card Lighting");
    update_object_grid(*inputs.gi_scene, *inputs.view_cache);
    if(direct_tile_count_ + radiosity_tile_count_ == 0 || !object_grid_ ||
       (is_lit_ && (experiment_flags_ & experiment_freeze_card_lighting) != 0u))
    {
        return;
    }
    // Direct lighting and its combine first: the radiosity rays see this frame's direct updates (UE's order).
    for(uint32_t first = 0; first < direct_tile_count_; first += max_groups_per_dispatch)
    {
        dispatch_direct(inputs, first, std::min(max_groups_per_dispatch, direct_tile_count_ - first));
    }
    const uint32_t radiosity_end = radiosity_tile_base_ + radiosity_tile_count_;
    for(uint32_t first = radiosity_tile_base_; first < radiosity_end; first += max_groups_per_dispatch)
    {
        dispatch_radiosity(inputs, first, std::min(max_groups_per_dispatch, radiosity_end - first));
    }
    is_lit_ = true;
}

auto lumen_surface_cache_pass::run_debug(const debug_params& params) -> bool
{
    APP_SCOPE_PERF("GI/Lumen/Surface Cache Debug");
    if(!is_ready() || params.output == nullptr || params.cam == nullptr || params.gi_scene == nullptr)
    {
        return false;
    }
    const auto& gi_scene = *params.gi_scene;
    if(!gi_scene.is_enabled() || gi_scene.get_instances().empty())
    {
        return false;
    }
    gfx::render_pass pass("GI/Lumen Surface Cache Debug");
    pass.bind(params.output);
    pass.set_view_proj(params.cam->get_view(), params.cam->get_projection());
    debug_program_->begin();
    bind_sdf_instances(gi_scene);
    if(params.view_cache != nullptr && params.view_cache->get_clipmap_gpu().is_valid())
    {
        const auto& clipmap_gpu = params.view_cache->get_clipmap_gpu();
        gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 11, lumen_pass::get_sdf_coverage(clipmap_gpu, experiment_flags_));
        gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
        gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    }
    else
    {
        gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 11, default_textures::get().white_texture_3d());
    }
    bgfx::setBuffer(9, scene_buffer_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_card_final, 7, final_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_albedo, 8, albedo_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_direct, 5, direct_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_indirect, 6, indirect_atlas_);
    gfx::set_texture(uniforms_.s_lumen_object_grid, 10, object_grid_ ? object_grid_ : object_grid_dummy_);
    gfx::set_uniform(uniforms_.u_lumen_object_grid_levels, object_grid_levels_.data(), 4);
    gfx::set_uniform(uniforms_.u_lumen_object_grid_params, get_object_grid_params());
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    const math::vec4 debug(float(params.mode), debug_max_distance, debug_max_steps, debug_surface_bias);
    gfx::set_uniform(uniforms_.u_lumen_debug, debug);
    const auto size = params.output->get_size();
    const uint32_t debug_flags = (params.mode == 5 ? 1u : 0u) |
                                 ((experiment_flags_ & experiment_show_sdf_coverage) != 0u ? 2u : 0u);
    const math::vec4 debug2(float(size.width) / std::max(float(size.height), 1.0f),
                            params.exposure,
                            float(debug_flags),
                            float(params.tonemapping));
    gfx::set_uniform(uniforms_.u_lumen_debug2, debug2);
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, debug_program_->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    debug_program_->end();
    bgfx::discard();
    return true;
}

} // namespace unravel
