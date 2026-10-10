#include "lumen_surface_cache.h"

#include "lumen_object_grid.h"

#include "lumen_pass_common.h"

#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/camera.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/gi/gi_constants.h>
#include <engine/rendering/gi/lumen_constants.h>
#include <engine/rendering/gi/surface_cache_system.h>
#include <engine/rendering/gi/surface_cache_view.h>

#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <logging/logging.h>

#include <algorithm>

namespace unravel
{
namespace
{

/// Threads per group edge of the copy, lighting and radiosity kernels (NUM_THREADS(8, 8, 1)), also their tile.
constexpr uint32_t tile_size = 8;
/// float4s per copy page record (lumen_surface_cache_lighting.sh LUMEN_CARD_COPY_PAGE_STRIDE).
constexpr uint32_t copy_page_stride = 6;
/// float4s per lighting page record (cs_lumen_card_lighting.sc, lumen_tile_records.sh LUMEN_LIGHT_PAGE_STRIDE).
constexpr uint32_t light_page_stride = 4;
/// A tile list's words (lumen_tile_records.sh): four per float4, page x 256 + column x 16 + row, in 8-texel tiles.
constexpr uint32_t tile_words_per_vec4 = 4;
constexpr uint32_t tile_word_page_scale = 256;
constexpr uint32_t tile_word_column_scale = 16;
/// The brightest a radiosity ray's radiance may be (its largest channel), in pre-exposed units: brighter rays are
/// scaled down to it.
constexpr float radiosity_max_ray_intensity = 40.0f;
/// Threads of a radiosity trace or filter group (lumen_radiosity_common.sh LUMEN_RADIOSITY_GROUP_THREADS).
constexpr uint32_t radiosity_group_threads = 64;
/// bgfx's per-dimension dispatch limit: larger tile lists go out in several dispatches.
constexpr uint32_t max_groups_per_dispatch = 65535;
/// Mixes a material's address into its card capture key (lumen_scene::source::material_key) before its revision.
constexpr uint64_t material_key_multiplier = 0x9E3779B97F4A7C15ull;
/// Copy tiles per dispatch row (lumen_surface_cache_lighting.sh LUMEN_CARD_COPY_DISPATCH_WIDTH).
constexpr uint32_t copy_dispatch_width = 256;

/// Groups (x, y) of a copy / resample dispatch over @p tiles tiles, in rows of copy_dispatch_width.
auto get_copy_dispatch(uint32_t tiles) -> math::uvec2
{
    return {std::min(tiles, copy_dispatch_width), (tiles + copy_dispatch_width - 1u) / copy_dispatch_width};
}
/// Instances changed in one update beyond which the direct lighting relights every page instead of placing them, and
/// the occluder-light tests per page beyond which it does the same.
constexpr size_t max_direct_occluder_changes = 256;
constexpr size_t max_direct_occluder_tests = 4096;
/// The capture camera for the G-buffer shader's distance dither sits this far in front of the card.
constexpr float capture_far_eye_distance = 1.0e5f;
/// The debug views' mesh distance-field march (the coverage view): its step count and surface bias.
constexpr float debug_max_steps = 256.0f;
constexpr float debug_surface_bias = 0.5f;
/// Updates between surface cache stats lines in the log.
constexpr uint32_t stats_log_period = 300;
/// A viewer's record serves the frame it was made in and the next: a camera that rendered last frame still counts.
constexpr uint64_t viewer_lifetime_frames = 1;
/// A fixed 50 m global distance field level 0, at the layout's 252 voxels per axis: experiment_fixed_card_tolerance
/// measures the card sampling bias of global-SDF hits in its voxel extents.
constexpr float fixed_card_tolerance_extent = 50.0f;

using lumen_pass::upload_vec4_table;

/// The GI scene settings the project's Global Illumination settings ask for, within the scene's limits.
auto make_scene_settings(const gi_project_settings& project) -> lumen_scene::settings
{
    lumen_scene::settings s;
    s.atlas_size = uint32_t(project.surface_cache_atlas_size);
    s.capture_atlas_size = uint32_t(project.card_capture_atlas_size);
    s.max_captures_per_frame = std::max(project.card_captures_per_frame, 1u);
    s.texel_density_scale = std::max(project.card_texel_density_scale, 0.0f);
    s.max_texel_density = std::max(project.card_max_texel_density, 0.0f);
    s.card_max_resolution = std::max(project.card_max_resolution, 1u);
    const bool is_high = project.global_illumination_quality == gi_project_settings::quality_level::high;
    s.card_min_resolution =
        std::max(project.card_min_resolution, is_high ? lumen_pass::high_card_min_resolution : 1u);
    s.mesh_cards_min_size = std::max(project.mesh_cards_min_size, 0.0f);
    s.card_capture_refresh_fraction = std::clamp(project.card_capture_refresh_fraction, 0.0f, 1.0f);
    s.lighting_update_factor_scale = is_high ? lumen_pass::high_lighting_update_factor_scale : 1u;
    return s;
}

/// The surface cache atlases' formats (lumen_surface_cache_lighting.sh): lighting and emissive in R11G11B10 float,
/// card depth in R16 unorm, the card-space normal in two unorm channels. The final lighting keeps RGBA16F: its alpha
/// carries the card depth for the hit tracers.
constexpr auto lighting_atlas_format = bgfx::TextureFormat::RG11B10F;
constexpr auto depth_atlas_format = bgfx::TextureFormat::R16;
constexpr auto normal_atlas_format = bgfx::TextureFormat::RG8;
constexpr auto final_atlas_format = bgfx::TextureFormat::RGBA16F;

/// The frame index the atlas stores dither by (LumenQuantizeCardLighting).
auto get_store_frame_index() -> float
{
    return lumen_pass::get_frame_index(gfx::get_render_frame());
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

/// Appends the words of page record @p page's 8 x 8 tiles, of a page of @p size texels, to @p words.
void append_tile_words(std::vector<float>& words, uint32_t page, const math::uvec2& size)
{
    for(uint32_t y = 0; y < size.y; y += tile_size)
    {
        for(uint32_t x = 0; x < size.x; x += tile_size)
        {
            const uint32_t word = page * tile_word_page_scale + (x / tile_size) * tile_word_column_scale + y / tile_size;
            words.push_back(float(word));
        }
    }
}

/// Appends @p words to @p table, four per float4, after its page records; returns the float4 the words start at.
auto append_tile_words_table(std::vector<math::vec4>& table, const std::vector<float>& words) -> uint32_t
{
    const auto base = uint32_t(table.size());
    for(size_t i = 0; i < words.size(); i += tile_words_per_vec4)
    {
        math::vec4 packed(0.0f);
        for(size_t lane = 0; lane < tile_words_per_vec4 && i + lane < words.size(); ++lane)
        {
            packed[int(lane)] = words[i + lane];
        }
        table.push_back(packed);
    }
    return base;
}

/// Appends a lighting page record (lumen_tile_records.sh LumenLightTile): (atlas offset, card, update index), the card
/// UV rectangle, (atlas offset, size), the page's mip span (page table offset, size in pages).
void append_light_page(std::vector<math::vec4>& records,
                       uint32_t update_index,
                       const lumen_scene::resident_page& page)
{
    records.emplace_back(float(page.atlas_offset.x), float(page.atlas_offset.y), float(page.card_index), float(update_index));
    records.push_back(page.card_uv_rect);
    records.emplace_back(float(page.atlas_offset.x), float(page.atlas_offset.y), float(page.size.x), float(page.size.y));
    records.emplace_back(float(page.mip_page_table_offset),
                         float(page.mip_size_in_pages.x),
                         float(page.mip_size_in_pages.y),
                         0.0f);
}

void bind_atlas_image(uint8_t stage, const gfx::texture::ptr& tex, bgfx::Access::Enum access, bgfx::TextureFormat::Enum format)
{
    bgfx::setImage(stage, tex->native_handle(), 0, access, format);
}

} // namespace

void lumen_surface_cache::uniforms::cache_uniforms()
{
    cache_uniform(nullptr, u_lumen_card_copy, "u_lumen_card_copy", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_card_lighting, "u_lumen_card_lighting", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_cloudShadow, "s_cloudShadow", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_cloudShadow, "u_cloudShadow", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_cloudShadow2, "u_cloudShadow2", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_radiosity, "u_lumen_radiosity", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, u_lumen_surface_cache, "u_lumen_surface_cache", bgfx::UniformType::Vec4);
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
    cache_uniform(nullptr, s_lumen_capture_rt3, "s_lumen_capture_rt3", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_card_final, "s_lumen_card_final", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_env_sh, "s_lumen_env_sh", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, u_lumen_debug3, "u_lumen_debug3", bgfx::UniformType::Vec4);
    cache_uniform(nullptr, s_lumen_debug_values, "s_lumen_debug_values", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_trace, "s_lumen_radiosity_trace", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_r, "s_lumen_radiosity_sh_r", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_g, "s_lumen_radiosity_sh_g", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_lumen_radiosity_sh_b, "s_lumen_radiosity_sh_b", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_atlas, "s_sdf_atlas", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap, "s_sdf_clipmap", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_coverage, "s_sdf_clipmap_coverage", bgfx::UniformType::Sampler);
    cache_uniform(nullptr, s_sdf_clipmap_mip, "s_sdf_clipmap_mip", bgfx::UniformType::Sampler);
}

lumen_surface_cache::~lumen_surface_cache()
{
    lumen_pass::destroy_handle(scene_buffer_);
    lumen_pass::destroy_handle(copy_tile_buffer_);
    lumen_pass::destroy_handle(resample_table_buffer_);
    lumen_pass::destroy_handle(page_ages_buffer_);
    lumen_pass::destroy_handle(light_tile_buffer_);
}

auto lumen_surface_cache::init(rtti::context& ctx) -> bool
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
    radiosity_trace_program_ = load("cs_lumen_radiosity_trace");
    radiosity_sh_program_ = load("cs_lumen_radiosity_sh");
    radiosity_integrate_program_ = load("cs_lumen_radiosity_integrate");
    auto vs_clip_quad = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.sc");
    auto fs_debug = am.get_asset<gfx::shader>("engine:/data/shaders/lumen/fs_lumen_scene_debug.sc");
    debug_program_ = std::make_unique<gpu_program>(vs_clip_quad, fs_debug);
    feedback_.init(ctx);
    // The atlases come with the first update with a user (has_targets).
    scene_.init(lumen_scene::settings{});
    if(!is_ready())
    {
        APPLOG_WARNING("[GI] Surface cache programs failed to load; the surface cache is unavailable.");
    }
    return is_ready();
}

auto lumen_surface_cache::acquire(rtti::context& ctx) -> std::shared_ptr<lumen_surface_cache>
{
    // surface_cache_system walks one world per frame for every camera; its cards are this one cache's.
    static std::weak_ptr<lumen_surface_cache> shared;
    if(auto existing = shared.lock())
    {
        return existing;
    }
    auto created = std::make_shared<lumen_surface_cache>();
    created->init(ctx);
    shared = created;
    return created;
}

void lumen_surface_cache::add_user()
{
    ++user_count_;
}

void lumen_surface_cache::remove_user()
{
    if(user_count_ == 0u)
    {
        return;
    }
    --user_count_;
    if(user_count_ == 0u)
    {
        release_targets();
    }
}

void lumen_surface_cache::register_viewer(const void* key,
                                          const lumen_scene::viewer& viewer,
                                          const global_sdf_clipmap* clipmap,
                                          uint64_t frame)
{
    viewer_record& record = viewers_[key];
    record.viewer = viewer;
    record.frame = frame;
    record.has_field = clipmap != nullptr;
    if(clipmap == nullptr)
    {
        return;
    }
    for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
    {
        const auto& lvl = clipmap->get_level(level);
        field_level_record& copy = record.field_levels[level];
        copy.compose_serial = lvl.compose_serial;
        copy.is_unscrolled_partial = lvl.is_partial && lvl.scroll_shift == math::ivec3(0);
        copy.boxes.clear();
        for(const auto& box : lvl.partial_boxes)
        {
            const math::vec3 box_min = lvl.origin + math::vec3(box.min) * lvl.voxel_size;
            copy.boxes.emplace_back(box_min, box_min + math::vec3(box.size) * lvl.voxel_size);
        }
    }
}

void lumen_surface_cache::forget_viewer(const void* key)
{
    viewers_.erase(key);
}

auto lumen_surface_cache::claim_update(uint64_t frame) -> bool
{
    if(claimed_frame_ == frame)
    {
        return false;
    }
    claimed_frame_ = frame;
    return true;
}

auto lumen_surface_cache::schedule_viewers(uint64_t frame) -> std::vector<lumen_scene::viewer>
{
    // The last schedule's pages of a viewer that did not light them (its camera stopped rendering) are due again.
    for(uint32_t index = 0; index < uint32_t(lit_viewers_.size()); ++index)
    {
        if(lit_viewers_[index] == 0u)
        {
            scene_.revoke_lighting(index);
        }
    }
    std::vector<lumen_scene::viewer> viewers;
    scheduled_viewers_.clear();
    for(auto it = viewers_.begin(); it != viewers_.end();)
    {
        if(it->second.frame + viewer_lifetime_frames < frame)
        {
            it = viewers_.erase(it);
            continue;
        }
        viewers.push_back(it->second.viewer);
        scheduled_viewers_.push_back(it->first);
        ++it;
    }
    lit_viewers_.assign(viewers.size(), 0u);
    return viewers;
}

auto lumen_surface_cache::is_ready() const -> bool
{
    const auto valid = [](const gpu_program* program)
    {
        return program != nullptr && program->is_valid();
    };
    return valid(copy_program_.get()) && valid(resample_program_.get()) && valid(lighting_program_.get()) &&
           valid(radiosity_trace_program_.get()) && valid(radiosity_sh_program_.get()) &&
           valid(radiosity_integrate_program_.get()) && valid(debug_program_.get());
}

void lumen_surface_cache::create_targets()
{
    const auto& s = scene_.get_settings();
    albedo_atlas_ = make_atlas(s.atlas_size, bgfx::TextureFormat::RGBA8);
    normal_atlas_ = make_atlas(s.atlas_size, normal_atlas_format);
    emissive_atlas_ = make_atlas(s.atlas_size, lighting_atlas_format);
    depth_atlas_ = make_atlas(s.atlas_size, depth_atlas_format);
    direct_atlas_ = make_atlas(s.atlas_size, lighting_atlas_format);
    indirect_atlas_ = make_atlas(s.atlas_size, lighting_atlas_format);
    final_atlas_ = make_atlas(s.atlas_size, final_atlas_format);
    // New radiosity atlases at the current layout.
    radiosity_trace_atlas_.reset();
    ensure_radiosity_targets(radiosity_layout_);
    radiosity_frames_ = make_atlas(s.atlas_size / tile_size, bgfx::TextureFormat::R32F);
    resample_direct_ = make_atlas(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F);
    resample_indirect_ = make_atlas(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F);
    // The G-buffer program's four targets, in its formats, plus depth.
    std::vector<gfx::texture::ptr> targets = {make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA8),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA16F),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::RGBA8),
                                              make_capture_texture(s.capture_atlas_size, bgfx::TextureFormat::D32F)};
    capture_target_ = std::make_shared<gfx::frame_buffer>(targets);
}

void lumen_surface_cache::release_targets()
{
    if(!has_targets())
    {
        return;
    }
    for(auto* texture : {&albedo_atlas_, &normal_atlas_, &emissive_atlas_, &depth_atlas_, &direct_atlas_, &indirect_atlas_,
                         &final_atlas_, &radiosity_trace_atlas_, &radiosity_sh_r_, &radiosity_sh_g_, &radiosity_sh_b_,
                         &radiosity_frames_, &resample_direct_, &resample_indirect_})
    {
        texture->reset();
    }
    capture_target_.reset();
    scene_.init(scene_.get_settings());
    is_lit_ = false;
    has_previous_lighting_inputs_ = false;
    previous_compose_serials_.clear();
    scheduled_viewers_.clear();
    lit_viewers_.clear();
    direct_ranges_.clear();
    radiosity_ranges_.clear();
}

void lumen_surface_cache::ensure_radiosity_targets(const lumen_pass::radiosity_layout& layout)
{
    if(radiosity_trace_atlas_ && layout == radiosity_layout_)
    {
        return;
    }
    // A new layout starts its probes over; the indirect atlas keeps the lighting it accumulated.
    radiosity_layout_ = layout;
    const uint32_t probe_atlas_size = scene_.get_settings().atlas_size / layout.probe_spacing;
    radiosity_trace_atlas_ = make_atlas(probe_atlas_size * layout.hemisphere_resolution, lighting_atlas_format);
    radiosity_sh_r_ = make_atlas(probe_atlas_size, bgfx::TextureFormat::RGBA16F);
    radiosity_sh_g_ = make_atlas(probe_atlas_size, bgfx::TextureFormat::RGBA16F);
    radiosity_sh_b_ = make_atlas(probe_atlas_size, bgfx::TextureFormat::RGBA16F);
}

auto lumen_surface_cache::get_feedback() -> lumen_surface_cache_feedback*
{
    return uses_hi_res_pages_ && feedback_.is_ready() && has_targets() ? &feedback_ : nullptr;
}

auto lumen_surface_cache::has_lighting() const -> bool
{
    return is_lit_ && has_targets();
}

void lumen_surface_cache::bind_for_sampling(uint8_t scene_stage,
                                            uint8_t final_stage,
                                            uint8_t grid_stage,
                                            bool enabled,
                                            const lumen_object_grid& object_grid) const
{
    const bool use = enabled && has_lighting() && object_grid.has_grid();
    bgfx::setBuffer(scene_stage, scene_buffer_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_card_final,
                     final_stage,
                     final_atlas_ ? final_atlas_ : default_textures::get().black_texture());
    object_grid.bind(grid_stage, card_bias_scale_);
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    const math::vec4 hit_lighting(use ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_hit_lighting, hit_lighting);
}

auto lumen_surface_cache::get_surface_cache_params() const -> math::vec4
{
    return {float(scene_.get_settings().atlas_size),
            float(page_table_base_),
            float(instance_table_base_),
            float(scene_.get_instance_table().size() / lumen_scene::instance_stride)};
}

auto lumen_surface_cache::collect_direct_lighting_changes(const surface_cache_system& gi_scene)
    -> lumen_scene::direct_lighting_changes
{
    lumen_scene::direct_lighting_changes changes;
    changes.all = !has_previous_lighting_inputs_ || gi_scene.has_global_lighting_change() ||
                  (experiment_flags_ & lumen_pass::experiment_no_direct_page_skip) != 0u;
    // The lights: a changed directional light or light count relights everything, a changed local light its reach
    // before and after.
    const auto& light_buffer = gi_scene.get_light_buffer();
    const std::vector<float>& lights = light_buffer.get_light_data();
    const size_t record = size_t(gpu_light_buffer::light_vec4_stride) * 4u;
    const auto is_directional = [](const float* light)
    {
        return uint32_t(light[3]) == uint32_t(gpu_light_buffer::gpu_light_type::directional);
    };
    const auto reach_box = [](const float* light)
    {
        const math::vec3 position(light[0], light[1], light[2]);
        const math::vec3 range(light[7]);
        return math::bbox(position - range, position + range);
    };
    changes.all = changes.all || lights.size() != previous_lights_.size();
    for(size_t base = 0; base + record <= lights.size() && !changes.all; base += record)
    {
        const float* light = lights.data() + base;
        const float* previous = previous_lights_.data() + base;
        if(std::equal(light, light + record, previous))
        {
            continue;
        }
        if(is_directional(light) || is_directional(previous))
        {
            changes.all = true;
            break;
        }
        changes.regions.push_back(reach_box(previous));
        changes.regions.push_back(reach_box(light));
    }
    for(size_t base = 0; base + record <= lights.size(); base += record)
    {
        const float* light = lights.data() + base;
        lumen_scene::light_reach reach;
        reach.is_directional = is_directional(light);
        reach.direction = -math::vec3(light[4], light[5], light[6]);
        reach.position = math::vec3(light[0], light[1], light[2]);
        reach.range = light[7];
        changes.lights.push_back(reach);
    }
    previous_lights_ = lights;
    // The sun's cloud shadow: a new map relights every page a directional light reaches.
    const bool has_directional = std::any_of(changes.lights.begin(),
                                             changes.lights.end(),
                                             [](const lumen_scene::light_reach& reach)
                                             {
                                                 return reach.is_directional;
                                             });
    changes.all = changes.all || (has_directional && cloud_shadow_.signature != previous_cloud_signature_);
    previous_cloud_signature_ = cloud_shadow_.signature;
    // The distance fields the viewers light with: what each level recomposed since the last update. A partial
    // recompose rewrote its boxes (each changed instance's reach), a full one or a moved level every voxel. A field
    // first seen (a new camera, or a camera whose field started over) relights everything; the fields of cameras gone
    // are forgotten.
    std::unordered_map<const void*, std::array<uint64_t, global_sdf_clipmap::level_count>> serials;
    for(const void* key : scheduled_viewers_)
    {
        const viewer_record& record = viewers_.at(key);
        if(!record.has_field)
        {
            continue;
        }
        const auto previous = previous_compose_serials_.find(key);
        changes.all = changes.all || previous == previous_compose_serials_.end();
        auto& current = serials[key];
        for(uint32_t level = 0; level < global_sdf_clipmap::level_count; ++level)
        {
            const field_level_record& lvl = record.field_levels[level];
            const uint64_t serial = lvl.compose_serial;
            const uint64_t previous_serial = previous != previous_compose_serials_.end() ? previous->second[level] : 0u;
            current[level] = serial;
            if(serial == previous_serial)
            {
                continue;
            }
            // Only the last recompose's boxes are known: two since the last update leave the first one's unseen.
            const bool is_single_partial = lvl.is_unscrolled_partial && serial == previous_serial + 1u;
            changes.all = changes.all || !is_single_partial;
            changes.occluders.insert(changes.occluders.end(), lvl.boxes.begin(), lvl.boxes.end());
        }
    }
    previous_compose_serials_ = std::move(serials);
    changes.all = changes.all || changes.occluders.size() > max_direct_occluder_changes ||
                  changes.occluders.size() * changes.lights.size() > max_direct_occluder_tests;
    has_previous_lighting_inputs_ = true;
    if(changes.all)
    {
        changes.regions.clear();
        changes.occluders.clear();
    }
    return changes;
}

void lumen_surface_cache::update(const surface_cache_system& gi_scene,
                                 uint64_t frame,
                                 const gi_settings::scene_settings& view_settings,
                                 const gi_project_settings& project_settings)
{
    APP_SCOPE_PERF("GI/Surface Cache Update");
    if(scene_.apply_settings(make_scene_settings(project_settings)) || !has_targets())
    {
        // The first update, or the scene started over: new, unlit atlases at the current sizes.
        create_targets();
        is_lit_ = false;
    }
    ensure_radiosity_targets(
        lumen_pass::get_radiosity_layout(view_settings.lighting_quality, project_settings.global_illumination_quality));
    const auto& lumen_sources = gi_scene.get_lumen_sources();
    experiment_flags_ = gi_scene.get_experiment_flags();
    card_bias_scale_ = (experiment_flags_ & experiment_fixed_card_tolerance) != 0u
                           ? fixed_card_tolerance_extent / gi::lumen::LUMEN_GLOBAL_SDF_EXTENT
                           : 1.0f;
    sources_.clear();
    sources_.reserve(lumen_sources.size());
    const auto& instances = gi_scene.get_instances();
    for(const auto& src : lumen_sources)
    {
        const uint64_t material_identity = uint64_t(reinterpret_cast<uintptr_t>(src.material.get()));
        const uint64_t material_key =
            src.material ? (material_identity * material_key_multiplier) ^ src.material->get_revision() : 0u;
        sources_.push_back({src.identity,
                            src.instance_index,
                            src.cards,
                            src.local_to_world,
                            instances[src.instance_index].is_emissive_light_source,
                            material_key});
    }
    scene_.set_hold_resident_resolutions((experiment_flags_ & experiment_hold_card_resolution) != 0u);
    // The reflections' feedback of a few frames ago asks for this frame's hi-res pages.
    uses_hi_res_pages_ = project_settings.hi_res_reflection_pages &&
                         (experiment_flags_ & lumen_pass::experiment_no_hi_res_pages) == 0u;
    scene_.set_hi_res_pages_enabled(uses_hi_res_pages_);
    uint64_t feedback_revision = 0;
    uint32_t feedback_min_hits = 0;
    uint32_t feedback_samples = 0;
    if(feedback_.take(feedback_elements_, feedback_revision, feedback_min_hits, feedback_samples))
    {
        scene_.set_feedback(feedback_elements_, feedback_revision, feedback_min_hits, feedback_samples);
    }
    scene_.set_card_residency_without_group_gate((experiment_flags_ & experiment_card_residency_without_group_gate) != 0u);
    scene_.set_view_settings(view_settings);
    const std::vector<lumen_scene::viewer> viewers = schedule_viewers(frame);
    if(viewers.empty())
    {
        return;
    }
    scene_.update(sources_, uint32_t(gi_scene.get_instances().size()), viewers);
    scene_.invalidate_direct_lighting(collect_direct_lighting_changes(gi_scene));
    scene_.schedule_lighting(viewers);
    upload_scene_table();
    upload_vec4_table(resample_table_buffer_, scene_.get_resample_table());
    build_copy_tiles();
    build_light_tiles();
    reallocated_since_log_ += scene_.get_stats().reallocated;
    if((++update_count_ % stats_log_period) == 0u)
    {
        const auto& st = scene_.get_stats();
        const lumen_surface_cache_feedback::stats feedback_stats = feedback_.take_stats();
        APPLOG_TRACE("[GI] surface cache: {} cards, {} resident, pages {} / {}, desired texels {}, "
                    "captures {}, downgraded {}, reallocated {} in {} frames, lit tiles {} direct (cut bucket {}, {} pages due) / "
                    "{} radiosity (cut bucket {}), hi-res pages {} ({} asked for), feedback frames {} restarts {} "
                    "readbacks {} elements {} max hits {}",
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
                    st.direct_dirty_pages,
                    st.lit_tiles[lumen_scene::lighting_radiosity],
                    st.cut_bucket[lumen_scene::lighting_radiosity],
                    st.hi_res_pages,
                    st.hi_res_requests,
                    feedback_stats.frames,
                    feedback_stats.restarts,
                    feedback_stats.readbacks,
                    feedback_stats.elements,
                    feedback_stats.max_hits);
        reallocated_since_log_ = 0;
    }
}

void lumen_surface_cache::upload_scene_table()
{
    APP_SCOPE_PERF("GI/Upload Scene Table");
    // The buffer keeps the last upload while the scene's tables are unchanged.
    if(bgfx::isValid(scene_buffer_) && uploaded_tables_revision_ == scene_.get_tables_revision())
    {
        return;
    }
    uploaded_tables_revision_ = scene_.get_tables_revision();
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

void lumen_surface_cache::build_copy_tiles()
{
    APP_SCOPE_PERF("GI/Copy Tiles");
    std::vector<math::vec4> table;
    std::vector<float> words;
    const bool resamples = (experiment_flags_ & experiment_no_lighting_resample) == 0u;
    has_resample_ = false;
    const auto& cards = scene_.get_card_table();
    const auto& captures = scene_.get_captures();
    table.reserve(captures.size() * copy_page_stride);
    for(uint32_t page = 0; page < uint32_t(captures.size()); ++page)
    {
        const auto& cap = captures[page];
        const int32_t resample_card = resamples ? cap.resample_card : -1;
        const bool keeps_lighting = cap.keeps_lighting && resample_card >= 0;
        // A page refreshed in place keeps its lighting where it is (cs_lumen_card_copy.sc): only reallocations
        // resample.
        has_resample_ = has_resample_ || (resample_card >= 0 && !keeps_lighting);
        // The card's world axes (the card table's rows 1-3, lumen_surface_cache.sh LumenLoadCard): the copy stores
        // normals in them.
        const size_t card_row = size_t(cap.card_index) * lumen_scene::card_stride;
        const math::vec3 axis_x(cards[card_row + 1]);
        const math::vec3 axis_y(cards[card_row + 2]);
        const math::vec3 axis_z(cards[card_row + 3]);
        table.emplace_back(float(cap.capture_offset.x),
                           float(cap.capture_offset.y),
                           float(cap.atlas_offset.x),
                           float(cap.atlas_offset.y));
        table.push_back(cap.card_uv_rect);
        table.emplace_back(0.0f, 0.0f, float(cap.size.x), float(cap.size.y));
        table.emplace_back(float(resample_card), axis_x.x, axis_x.y, axis_x.z);
        table.emplace_back(axis_y, keeps_lighting ? 1.0f : 0.0f);
        table.emplace_back(axis_z, 0.0f);
        append_tile_words(words, page, cap.size);
    }
    copy_tile_count_ = uint32_t(words.size());
    copy_tile_words_base_ = append_tile_words_table(table, words);
    upload_vec4_table(copy_tile_buffer_, table);
}

void lumen_surface_cache::build_light_tiles()
{
    APP_SCOPE_PERF("GI/Light Tiles");
    std::vector<math::vec4> table;
    std::vector<float> words;
    const auto& pages = scene_.get_resident_pages();
    const auto viewer_count = uint32_t(scheduled_viewers_.size());
    // Each viewer's pages in one run of tile words, per context.
    const auto append_context = [&](lumen_scene::lighting_context context, std::vector<tile_range>& ranges)
    {
        ranges.assign(viewer_count, tile_range{});
        for(uint32_t viewer = 0; viewer < viewer_count; ++viewer)
        {
            ranges[viewer].first = uint32_t(words.size());
            for(const auto& lit : scene_.get_lit_pages(context))
            {
                if(lit.viewer != viewer)
                {
                    continue;
                }
                const auto& page = pages[lit.resident_page];
                const auto record = uint32_t(table.size() / light_page_stride);
                append_light_page(table, lit.update_index, page);
                append_tile_words(words, record, page.size);
            }
            ranges[viewer].count = uint32_t(words.size()) - ranges[viewer].first;
        }
    };
    append_context(lumen_scene::lighting_direct, direct_ranges_);
    const auto direct_tiles = uint32_t(words.size());
    append_context(lumen_scene::lighting_radiosity, radiosity_ranges_);
    const uint32_t radiosity_tiles = uint32_t(words.size()) - direct_tiles;
    light_tile_words_base_ = append_tile_words_table(table, words);
    radiosity_page_words_base_ = -1;
    if(radiosity_tiles > 0u && (experiment_flags_ & lumen_pass::experiment_page_bound_radiosity) == 0u)
    {
        std::vector<float> page_indices;
        scene_.get_page_radiosity_indices(page_indices);
        radiosity_page_words_base_ = int32_t(append_tile_words_table(table, page_indices));
    }
    upload_vec4_table(light_tile_buffer_, table);
}

auto lumen_surface_cache::compute_capture_view(const lumen_scene::capture& cap) const -> capture_view
{
    const auto& cards = scene_.get_card_table();
    const size_t base = size_t(cap.card_index) * lumen_scene::card_stride;
    const math::vec3 origin(cards[base + 0]);
    const math::vec3 axis_x(cards[base + 1]);
    const math::vec3 axis_y(cards[base + 2]);
    const math::vec3 axis_z(cards[base + 3]);
    const math::vec3 extent(cards[base + 1].w, cards[base + 2].w, cards[base + 3].w);
    // The page's card-local rectangle: u along +axis_x, v along -axis_y (card_uv = (0.5, -0.5) x / extent + 0.5).
    const float x0 = (2.0f * cap.card_uv_rect.x - 1.0f) * extent.x;
    const float x1 = (2.0f * cap.card_uv_rect.z - 1.0f) * extent.x;
    const float y_top = (1.0f - 2.0f * cap.card_uv_rect.y) * extent.y;
    const float y_bottom = (1.0f - 2.0f * cap.card_uv_rect.w) * extent.y;
    const float cx = 0.5f * (x0 + x1);
    const float cy = 0.5f * (y_top + y_bottom);
    const float hx = std::max(0.5f * (x1 - x0), 1e-6f);
    const float hy = std::max(0.5f * (y_top - y_bottom), 1e-6f);
    const float ez = std::max(extent.z, 1e-6f);
    // The card's clip space: tile.x = (q.x - cx) / hx, tile.y = (q.y - cy) / hy, depth = (ez - q.z) / (2 ez), with
    // q = card-local position: depth 0 at the card's front plane, 1 at its back.
    // One pass draws every capture (deferred::capture_lumen_cards), so the transform also places the tile in the
    // capture atlas: x = tile.x w / W + (2 left + w) / W - 1, y = tile.y h / H + 1 - (2 top + h) / H with the first row
    // at the top. The copy reads the captures by texel (cs_lumen_card_copy.sc), so they fill the same rows with the
    // same depth on every renderer: y flips where a target's first row is its bottom (OpenGL), and the depth maps to
    // -1..1 where clip space depth is homogeneous.
    const auto atlas = capture_target_->get_size();
    const float atlas_width = float(atlas.width);
    const float atlas_height = float(atlas.height);
    const float tile_left = float(cap.capture_offset.x);
    const float tile_top = float(cap.capture_offset.y);
    const float tile_width = float(cap.size.x);
    const float tile_height = float(cap.size.y);
    const bool is_origin_bottom_left = gfx::is_origin_bottom_left();
    const float y_sign = is_origin_bottom_left ? -1.0f : 1.0f;
    const float scale_x = tile_width / atlas_width;
    const float scale_y = y_sign * tile_height / atlas_height;
    const float offset_x = (2.0f * tile_left + tile_width) / atlas_width - 1.0f;
    const float offset_y = y_sign * (1.0f - (2.0f * tile_top + tile_height) / atlas_height);
    // The backend's own clip depth range, not gfx::is_homogeneous_depth(): that is the engine's projection convention
    // (zero to one everywhere; shaders reading a camera depth undo the backend's mapping), while the capture must clip
    // at the card's front plane and the copy reads its depth raw.
    const bool is_homogeneous_depth = bgfx::getCaps()->homogeneousDepth;
    const float z_scale = is_homogeneous_depth ? 2.0f : 1.0f;
    const float z_offset = is_homogeneous_depth ? -1.0f : 0.0f;
    const math::vec3 row_x = axis_x / hx * scale_x;
    const math::vec3 row_y = axis_y / hy * scale_y;
    const math::vec3 row_z = -axis_z / (2.0f * ez) * z_scale;
    math::mat4 m(0.0f);
    for(int c = 0; c < 3; ++c)
    {
        m[c][0] = row_x[c];
        m[c][1] = row_y[c];
        m[c][2] = row_z[c];
    }
    m[3][0] = -(math::dot(origin, axis_x) + cx) / hx * scale_x + offset_x;
    m[3][1] = -(math::dot(origin, axis_y) + cy) / hy * scale_y + offset_y;
    m[3][2] = z_scale * (ez + math::dot(origin, axis_z)) / (2.0f * ez) + z_offset;
    m[3][3] = 1.0f;
    capture_view view;
    view.view_proj = m;
    view.orientation = math::determinant(math::mat3(m)) < 0.0f ? -1.0f : 1.0f;
    view.far_eye = origin + axis_z * capture_far_eye_distance;
    // bgfx turns a scissor's top into rows counted from the bottom where a target's first row is its bottom
    // (OpenGL), so there the tile goes in mirrored to cover the rows the copy reads.
    const int32_t left = int32_t(cap.capture_offset.x);
    const int32_t width = int32_t(cap.size.x);
    const int32_t height = int32_t(cap.size.y);
    const int32_t top = is_origin_bottom_left ? int32_t(atlas.height) - int32_t(cap.capture_offset.y) - height
                                              : int32_t(cap.capture_offset.y);
    view.scissor = irect32_t(left, top, left + width, top + height);
    return view;
}

void lumen_surface_cache::copy_captures()
{
    if(copy_tile_count_ == 0 || !is_ready() || !has_targets())
    {
        return;
    }
    APP_SCOPE_PERF("GI/Card Copy");
    if(has_resample_)
    {
        resample_lighting();
    }
    gfx::render_pass pass("GI/Card Copy");
    copy_program_->begin();
    bind_atlas_image(0, albedo_atlas_, bgfx::Access::Write, bgfx::TextureFormat::RGBA8);
    bind_atlas_image(1, normal_atlas_, bgfx::Access::Write, normal_atlas_format);
    bind_atlas_image(2, emissive_atlas_, bgfx::Access::Write, lighting_atlas_format);
    bind_atlas_image(3, depth_atlas_, bgfx::Access::Write, depth_atlas_format);
    bind_atlas_image(4, final_atlas_, bgfx::Access::Write, final_atlas_format);
    // Direct and indirect read-write: a page refreshed in place reads its own lighting back for the final lighting.
    bind_atlas_image(5, indirect_atlas_, bgfx::Access::ReadWrite, lighting_atlas_format);
    bind_atlas_image(6, radiosity_frames_, bgfx::Access::Write, bgfx::TextureFormat::R32F);
    bind_atlas_image(7, direct_atlas_, bgfx::Access::ReadWrite, lighting_atlas_format);
    gfx::set_texture(uniforms_.s_lumen_capture_rt0, 8, capture_target_->get_texture(0));
    gfx::set_texture(uniforms_.s_lumen_capture_rt1, 9, capture_target_->get_texture(1));
    gfx::set_texture(uniforms_.s_lumen_capture_rt2, 10, capture_target_->get_texture(2));
    gfx::set_texture(uniforms_.s_lumen_capture_depth, 11, capture_target_->get_texture(4));
    gfx::set_texture(uniforms_.s_lumen_resample_direct, 12, resample_direct_);
    gfx::set_texture(uniforms_.s_lumen_resample_indirect, 13, resample_indirect_);
    gfx::set_texture(uniforms_.s_lumen_capture_rt3, 14, capture_target_->get_texture(3));
    bgfx::setBuffer(15, copy_tile_buffer_, bgfx::Access::Read);
    const math::vec4 params(float(copy_tile_count_), float(copy_tile_words_base_), get_store_frame_index(), 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_card_copy, params);
    const math::uvec2 groups = get_copy_dispatch(copy_tile_count_);
    bgfx::dispatch(pass.id, copy_program_->native_handle(), groups.x, groups.y, 1);
    copy_program_->end();
}

void lumen_surface_cache::resample_lighting()
{
    gfx::render_pass pass("GI/Card Lighting Resample");
    resample_program_->begin();
    bind_atlas_image(0, resample_direct_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
    bind_atlas_image(1, resample_indirect_, bgfx::Access::Write, bgfx::TextureFormat::RGBA16F);
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
    const math::vec4 params(float(copy_tile_count_), float(copy_tile_words_base_), 0.0f, 0.0f);
    gfx::set_uniform(uniforms_.u_lumen_card_copy, params);
    const math::uvec2 groups = get_copy_dispatch(copy_tile_count_);
    bgfx::dispatch(pass.id, resample_program_->native_handle(), groups.x, groups.y, 1);
    resample_program_->end();
}

void lumen_surface_cache::bind_sdf_instances(const surface_cache_system& gi_scene) const
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
                                0.0f);
    gfx::set_uniform(uniforms_.u_sdf_params, sdf_params);
}

void lumen_surface_cache::dispatch_direct(const lighting_inputs& inputs, uint32_t first, uint32_t count)
{
    const auto& clipmap_gpu = inputs.view_cache->get_clipmap_gpu();
    const auto& light_buffer = inputs.gi_scene->get_light_buffer();
    gfx::render_pass pass("GI/Card Direct Lighting");
    lighting_program_->begin();
    bind_atlas_image(0, direct_atlas_, bgfx::Access::Write, lighting_atlas_format);
    bind_atlas_image(1, final_atlas_, bgfx::Access::Write, final_atlas_format);
    gfx::set_texture(uniforms_.s_lumen_card_albedo, 11, albedo_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_emissive, 12, emissive_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_indirect, 13, indirect_atlas_);
    gfx::set_texture(uniforms_.s_lumen_card_normal, 3, normal_atlas_);
    gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
    gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiment_flags_));
    gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 14, clipmap_gpu.get_mip_texture());
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
    gfx::set_texture(uniforms_.s_cloudShadow,
                     2,
                     cloud_shadow_.map ? cloud_shadow_.map : default_textures::get().white_texture());
    gfx::set_uniform(uniforms_.u_cloudShadow, cloud_shadow_.placement);
    gfx::set_uniform(uniforms_.u_cloudShadow2, cloud_shadow_.layer);
    bgfx::setBuffer(8, light_tile_buffer_, bgfx::Access::Read);
    bgfx::setBuffer(9, scene_buffer_, bgfx::Access::Read);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
    gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    const math::vec4 lighting(float(first), float(count), float(light_tile_words_base_), get_store_frame_index());
    gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
    bgfx::dispatch(pass.id, lighting_program_->native_handle(), count, 1, 1);
    lighting_program_->end();
}

void lumen_surface_cache::dispatch_radiosity(const lighting_inputs& inputs, uint32_t first, uint32_t count)
{
    const auto& clipmap_gpu = inputs.view_cache->get_clipmap_gpu();
    const math::vec4 lighting(float(first), float(count), float(light_tile_words_base_), get_store_frame_index());
    const auto& environment =
        inputs.environment_sh ? inputs.environment_sh : default_textures::get().black_texture();
    // The probes' layout (lumen_radiosity_common.sh): a tile's rays in groups of radiosity_group_threads for the trace,
    // as many whole probes per group as fit for the filter.
    const auto& layout = radiosity_layout_;
    const math::vec4 radiosity(radiosity_max_ray_intensity / std::max(inputs.view_exposure, 1e-6f),
                               float(layout.probe_spacing),
                               float(layout.hemisphere_resolution),
                               float(radiosity_page_words_base_));
    const uint32_t probes_per_axis = tile_size / layout.probe_spacing;
    const uint32_t probes_per_tile = probes_per_axis * probes_per_axis;
    const uint32_t rays_per_probe = layout.hemisphere_resolution * layout.hemisphere_resolution;
    const uint32_t trace_groups_per_tile =
        lumen_pass::divide_round_up(probes_per_tile * rays_per_probe, radiosity_group_threads);
    const uint32_t probes_per_filter_group = std::max(radiosity_group_threads / rays_per_probe, 1u);
    const uint32_t filter_groups_per_tile = lumen_pass::divide_round_up(probes_per_tile, probes_per_filter_group);
    {
        gfx::render_pass pass("GI/Radiosity Trace");
        radiosity_trace_program_->begin();
        bind_atlas_image(0, radiosity_trace_atlas_, bgfx::Access::Write, lighting_atlas_format);
        gfx::set_texture(uniforms_.s_lumen_card_depth, 1, depth_atlas_);
        gfx::set_texture(uniforms_.s_lumen_card_normal, 2, normal_atlas_);
        gfx::set_texture(uniforms_.s_lumen_env_sh, 3, environment);
        gfx::set_texture(uniforms_.s_sdf_clipmap, 4, clipmap_gpu.get_texture());
        gfx::set_texture(uniforms_.s_sdf_clipmap_coverage, 10, lumen_pass::get_sdf_coverage(clipmap_gpu, experiment_flags_));
        gfx::set_texture(uniforms_.s_sdf_clipmap_mip, 9, clipmap_gpu.get_mip_texture());
        bgfx::setBuffer(5, light_tile_buffer_, bgfx::Access::Read);
        bgfx::setBuffer(6, scene_buffer_, bgfx::Access::Read);
        gfx::set_texture(uniforms_.s_lumen_card_final, 7, final_atlas_);
        inputs.object_grid->bind(8, card_bias_scale_);
        gfx::set_uniform(uniforms_.u_sdf_clipmap_levels, clipmap_gpu.get_level_params(), global_sdf_clipmap::level_count);
        gfx::set_uniform(uniforms_.u_sdf_clipmap_params, clipmap_gpu.get_sampling_params());
        gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
        gfx::set_uniform(uniforms_.u_lumen_card_lighting, lighting);
        gfx::set_uniform(uniforms_.u_lumen_radiosity, radiosity);
        bgfx::dispatch(pass.id, radiosity_trace_program_->native_handle(), count, trace_groups_per_tile, 1);
        radiosity_trace_program_->end();
    }
    {
        gfx::render_pass pass("GI/Radiosity Filter SH");
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
        gfx::set_uniform(uniforms_.u_lumen_radiosity, radiosity);
        bgfx::dispatch(pass.id, radiosity_sh_program_->native_handle(), count, filter_groups_per_tile, 1);
        radiosity_sh_program_->end();
    }
    {
        gfx::render_pass pass("GI/Radiosity Integrate");
        radiosity_integrate_program_->begin();
        bind_atlas_image(0, indirect_atlas_, bgfx::Access::ReadWrite, lighting_atlas_format);
        bind_atlas_image(1, final_atlas_, bgfx::Access::Write, final_atlas_format);
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
        gfx::set_uniform(uniforms_.u_lumen_radiosity, radiosity);
        bgfx::dispatch(pass.id, radiosity_integrate_program_->native_handle(), count, 1, 1);
        radiosity_integrate_program_->end();
    }
}

void lumen_surface_cache::light(const lighting_inputs& inputs, const void* viewer_key)
{
    const auto scheduled = std::find(scheduled_viewers_.begin(), scheduled_viewers_.end(), viewer_key);
    if(scheduled == scheduled_viewers_.end())
    {
        return;
    }
    const auto viewer = uint32_t(scheduled - scheduled_viewers_.begin());
    if(!is_ready() || !has_targets() || inputs.gi_scene == nullptr || inputs.view_cache == nullptr ||
       !inputs.view_cache->get_clipmap_gpu().is_valid() || inputs.object_grid == nullptr ||
       !inputs.object_grid->has_grid())
    {
        return;
    }
    APP_SCOPE_PERF("GI/Card Lighting");
    lit_viewers_[viewer] = 1u;
    const tile_range& direct = direct_ranges_[viewer];
    const tile_range& radiosity = radiosity_ranges_[viewer];
    if(direct.count + radiosity.count == 0 || (is_lit_ && (experiment_flags_ & experiment_freeze_card_lighting) != 0u))
    {
        return;
    }
    // Direct lighting and its combine first: the radiosity rays see this frame's direct updates.
    const uint32_t direct_end = direct.first + direct.count;
    for(uint32_t first = direct.first; first < direct_end; first += max_groups_per_dispatch)
    {
        dispatch_direct(inputs, first, std::min(max_groups_per_dispatch, direct_end - first));
    }
    const uint32_t radiosity_end = radiosity.first + radiosity.count;
    for(uint32_t first = radiosity.first; first < radiosity_end; first += max_groups_per_dispatch)
    {
        dispatch_radiosity(inputs, first, std::min(max_groups_per_dispatch, radiosity_end - first));
    }
    is_lit_ = true;
}

auto lumen_surface_cache::get_debug_values(debug_mode mode) const -> const gfx::texture::ptr&
{
    switch(mode)
    {
        case debug_mode::normals:
            return normal_atlas_;
        case debug_mode::emissive:
            return emissive_atlas_;
        case debug_mode::radiosity_frames:
            return radiosity_frames_;
        default:
            // The sampler is declared for every mode; the others never read it.
            return albedo_atlas_;
    }
}

auto lumen_surface_cache::run_debug(const debug_params& params) -> bool
{
    APP_SCOPE_PERF("GI/Surface Cache Debug");
    if(!is_ready() || !has_targets() || params.output == nullptr || params.cam == nullptr || params.gi_scene == nullptr ||
       params.object_grid == nullptr)
    {
        return false;
    }
    const auto& gi_scene = *params.gi_scene;
    if(!gi_scene.is_enabled() || gi_scene.get_instances().empty())
    {
        return false;
    }
    gfx::render_pass pass("GI/Surface Cache Debug");
    pass.bind(params.output);
    const auto size = params.output->get_size();
    const bool is_tile = !params.tile.empty();
    if(is_tile)
    {
        const auto x = uint16_t(params.tile.left);
        const auto y = uint16_t(params.tile.top);
        const auto width = uint16_t(params.tile.width());
        const auto height = uint16_t(params.tile.height());
        bgfx::setViewRect(pass.id, x, y, width, height);
        bgfx::setViewScissor(pass.id, x, y, width, height);
    }
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
    params.object_grid->bind(10, card_bias_scale_);
    gfx::set_uniform(uniforms_.u_lumen_surface_cache, get_surface_cache_params());
    gfx::set_texture(uniforms_.s_lumen_debug_values, 13, get_debug_values(params.mode));
    if(params.mode == debug_mode::direct_lighting_updates || params.mode == debug_mode::indirect_lighting_updates)
    {
        scene_.get_page_lighting_ages(page_ages_);
        upload_vec4_table(page_ages_buffer_, page_ages_);
    }
    else if(!bgfx::isValid(page_ages_buffer_))
    {
        upload_vec4_table(page_ages_buffer_, {});
    }
    bgfx::setBuffer(14, page_ages_buffer_, bgfx::Access::Read);
    gfx::set_texture(uniforms_.s_lumen_env_sh,
                     15,
                     params.environment_sh ? params.environment_sh : default_textures::get().black_texture());
    const math::vec4 debug(float(params.mode), params.max_trace_distance, debug_max_steps, debug_surface_bias);
    gfx::set_uniform(uniforms_.u_lumen_debug, debug);
    const uint32_t debug_flags = (params.mode == debug_mode::object_grid ? 1u : 0u) |
                                 ((experiment_flags_ & experiment_show_sdf_coverage) != 0u ? 2u : 0u);
    const math::vec4 debug2(float(size.width) / std::max(float(size.height), 1.0f),
                            params.exposure,
                            float(debug_flags),
                            float(params.tonemapping));
    gfx::set_uniform(uniforms_.u_lumen_debug2, debug2);
    const math::vec4 debug3(is_tile ? float(params.tile.width()) : 0.0f,
                            is_tile ? float(params.tile.height()) : 0.0f,
                            float(size.width),
                            float(size.height));
    gfx::set_uniform(uniforms_.u_lumen_debug3, debug3);
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_DEPTH_TEST_NEVER | BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(pass.id, debug_program_->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    debug_program_->end();
    bgfx::discard();
    return true;
}

} // namespace unravel
