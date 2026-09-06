#include "gtao_pass.h"
#include <algorithm>
#include <engine/assets/asset_manager.h>
#include <engine/profiler/profiler.h>
#include <engine/rendering/default_textures.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>

namespace unravel
{
namespace
{
constexpr const char* TEX_DEPTH_MIPS = "GTAO_DEPTH";
constexpr const char* TEX_RAW = "GTAO_RAW";
constexpr const char* TEX_DENOISE_A = "GTAO_DENOISE_A";
constexpr const char* TEX_DENOISE_B = "GTAO_DENOISE_B";
constexpr const char* TEX_HISTORY_0 = "GTAO_HISTORY_0";
constexpr const char* TEX_HISTORY_1 = "GTAO_HISTORY_1";
constexpr const char* TEX_OUTPUT = "GTAO";
constexpr const char* DATA_STATE = "GTAO_STATE";

/// Mips of view depth the main pass may read (gtao_common.sh GTAO_DEPTH_MIP_LEVELS).
constexpr uint8_t depth_mip_levels = 5;
/// Compute group edge for the 8x8 passes.
constexpr uint32_t group_size = 8;
/// The prefilter's group covers a 16x16 tile of mip 0.
constexpr uint32_t prefilter_tile = 16;
/// log2 pixel distance below which the horizon search reads mip 0 (XeGTAO's value).
constexpr float depth_mip_sampling_offset = 3.3f;
/// Relative view-depth sigma of the denoise and upsample edge stops.
constexpr float depth_sigma = 0.05f;
/// Normal power of the denoise edge stop.
constexpr float normal_power = 8.0f;
/// Exponent that concentrates horizon samples near the pixel (XeGTAO's value).
constexpr float sample_distribution_power = 2.0f;
/// The temporal noise index cycles through this many frames.
constexpr uint32_t noise_period = 64;
constexpr uint64_t clamp_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr uint64_t point_flags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;

struct gtao_state
{
    uint32_t parity = 0;
    bool history_valid = false;
    usize32_t history_size{};
};

auto group_count(uint32_t size, uint32_t group) -> uint32_t
{
    return (size + group - 1u) / group;
}
} // namespace

auto gtao_pass::get_slice_count(int32_t quality_level) -> uint32_t
{
    switch(std::clamp(quality_level, 0, 3))
    {
        case 0:
            return 1u;
        case 1:
            return 2u;
        case 2:
            return 3u;
        default:
            return 9u;
    }
}

auto gtao_pass::get_steps_per_slice(int32_t quality_level) -> uint32_t
{
    return std::clamp(quality_level, 0, 3) >= 2 ? 3u : 2u;
}

auto gtao_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto cs_prefilter = am.get_asset<gfx::shader>("engine:/data/shaders/gtao/cs_gtao_prefilter_depth.sc");
    auto cs_main = am.get_asset<gfx::shader>("engine:/data/shaders/gtao/cs_gtao_main.sc");
    auto cs_denoise = am.get_asset<gfx::shader>("engine:/data/shaders/gtao/cs_gtao_denoise.sc");
    auto cs_temporal = am.get_asset<gfx::shader>("engine:/data/shaders/gtao/cs_gtao_temporal.sc");
    auto cs_upsample = am.get_asset<gfx::shader>("engine:/data/shaders/gtao/cs_gtao_upsample.sc");
    // Uniforms before programs (the GL contract in gpu_program.h).
    common_cache_.cache_uniform(nullptr, common_.u_gtao_size, "u_gtao_size", gfx::uniform_type::Vec4);
    common_cache_.cache_uniform(nullptr, common_.u_gtao_full_size, "u_gtao_full_size", gfx::uniform_type::Vec4);
    common_cache_.cache_uniform(nullptr, common_.u_gtao_params0, "u_gtao_params0", gfx::uniform_type::Vec4);
    common_cache_.cache_uniform(nullptr, common_.u_gtao_params1, "u_gtao_params1", gfx::uniform_type::Vec4);
    common_cache_.cache_uniform(nullptr, common_.u_gtao_params2, "u_gtao_params2", gfx::uniform_type::Vec4);
    prefilter_program_.cache_uniforms();
    prefilter_program_.program = std::make_unique<gpu_program>(cs_prefilter);
    main_program_.cache_uniforms();
    main_program_.program = std::make_unique<gpu_program>(cs_main);
    denoise_program_.cache_uniforms();
    denoise_program_.program = std::make_unique<gpu_program>(cs_denoise);
    temporal_program_.cache_uniforms();
    temporal_program_.program = std::make_unique<gpu_program>(cs_temporal);
    upsample_program_.cache_uniforms();
    upsample_program_.program = std::make_unique<gpu_program>(cs_upsample);
    return prefilter_program_.is_valid() && main_program_.is_valid() && upsample_program_.is_valid();
}

auto gtao_pass::create_or_update_texture(gfx::render_view& rview,
                                         const std::string& name,
                                         const usize32_t& size,
                                         gfx::texture_format format,
                                         bool has_mips,
                                         uint64_t flags) -> gfx::texture::ptr
{
    auto& tex = rview.tex_get_or_emplace(name);
    if(gfx::needs_recreate(tex, size, format))
    {
        tex.reset();
        tex = std::make_shared<gfx::texture>(static_cast<uint16_t>(size.width),
                                             static_cast<uint16_t>(size.height),
                                             has_mips,
                                             1,
                                             format,
                                             BGFX_TEXTURE_COMPUTE_WRITE | flags);
    }
    return tex;
}

void gtao_pass::set_common_uniforms(const frame_context& ctx) const
{
    const float size[4] = {float(ctx.ao_size.width),
                           float(ctx.ao_size.height),
                           1.0f / float(ctx.ao_size.width),
                           1.0f / float(ctx.ao_size.height)};
    const float full_size[4] = {float(ctx.full_size.width),
                                float(ctx.full_size.height),
                                1.0f / float(ctx.full_size.width),
                                1.0f / float(ctx.full_size.height)};
    const float params0[4] = {std::max(ctx.config.radius, 0.01f),
                              std::clamp(ctx.config.falloff_range, 0.05f, 1.0f),
                              std::max(ctx.config.final_power, 0.1f),
                              std::clamp(ctx.config.thin_occluder_compensation, 0.0f, 0.9f)};
    const float noise_index = float(gfx::get_render_frame() % noise_period);
    const float params1[4] = {float(get_slice_count(ctx.config.quality_level)),
                              float(get_steps_per_slice(ctx.config.quality_level)),
                              noise_index,
                              depth_mip_sampling_offset};
    const float params2[4] = {depth_sigma,
                              normal_power,
                              sample_distribution_power,
                              std::clamp(ctx.config.max_screen_radius, 0.05f, 1.0f)};
    gfx::set_uniform(common_.u_gtao_size, size);
    gfx::set_uniform(common_.u_gtao_full_size, full_size);
    gfx::set_uniform(common_.u_gtao_params0, params0);
    gfx::set_uniform(common_.u_gtao_params1, params1);
    gfx::set_uniform(common_.u_gtao_params2, params2);
}

void gtao_pass::run_prefilter(gfx::render_view& rview,
                              const frame_context& ctx,
                              const run_params& params,
                              const gfx::texture::ptr& depth_mips)
{
    (void)rview;
    gfx::render_pass pass("GTAO/Prefilter Depth");
    pass.set_view_proj(ctx.cam->get_view(), ctx.cam->get_projection());
    set_common_uniforms(ctx);
    gfx::set_texture(prefilter_program_.s_gtao_depth, 0, params.g_buffer->get_texture(4));
    for(uint8_t mip = 0; mip < depth_mip_levels; ++mip)
    {
        gfx::set_image(uint8_t(1 + mip), depth_mips->native_handle(), mip, bgfx::Access::Write);
    }
    gfx::dispatch(pass.id,
                  prefilter_program_.program->native_handle(),
                  group_count(ctx.ao_size.width, prefilter_tile),
                  group_count(ctx.ao_size.height, prefilter_tile),
                  1);
}

auto gtao_pass::run_main(gfx::render_view& rview,
                         const frame_context& ctx,
                         const run_params& params,
                         const gfx::texture::ptr& depth_mips) -> gfx::texture::ptr
{
    auto raw = create_or_update_texture(rview, TEX_RAW, ctx.ao_size, gfx::texture_format::RGBA8, false, clamp_flags);
    gfx::render_pass pass("GTAO/Main");
    pass.set_view_proj(ctx.cam->get_view(), ctx.cam->get_projection());
    set_common_uniforms(ctx);
    gfx::set_texture(main_program_.s_gtao_depth_mips, 0, depth_mips);
    gfx::set_texture(main_program_.s_gtao_normal, 1, params.g_buffer->get_texture(1));
    gfx::set_image(2, raw->native_handle(), 0, bgfx::Access::Write);
    gfx::dispatch(pass.id,
                  main_program_.program->native_handle(),
                  group_count(ctx.ao_size.width, group_size),
                  group_count(ctx.ao_size.height, group_size),
                  1);
    return raw;
}

auto gtao_pass::run_denoise(gfx::render_view& rview,
                            const frame_context& ctx,
                            const run_params& params,
                            const gfx::texture::ptr& depth_mips,
                            gfx::texture::ptr input) -> gfx::texture::ptr
{
    const int32_t passes = std::clamp(ctx.config.denoise_passes, 0, 3);
    if(passes == 0 || !denoise_program_.is_valid())
    {
        rview.tex_remove(TEX_DENOISE_A);
        rview.tex_remove(TEX_DENOISE_B);
        return input;
    }
    auto tex_a = create_or_update_texture(rview, TEX_DENOISE_A, ctx.ao_size, gfx::texture_format::RGBA8, false, clamp_flags);
    auto tex_b = create_or_update_texture(rview, TEX_DENOISE_B, ctx.ao_size, gfx::texture_format::RGBA8, false, clamp_flags);
    for(int32_t i = 0; i < passes; ++i)
    {
        auto output = (i % 2 == 0) ? tex_a : tex_b;
        gfx::render_pass pass("GTAO/Denoise");
        pass.set_view_proj(ctx.cam->get_view(), ctx.cam->get_projection());
        set_common_uniforms(ctx);
        gfx::set_texture(denoise_program_.s_gtao_input, 0, input);
        gfx::set_texture(denoise_program_.s_gtao_depth_mips, 1, depth_mips);
        gfx::set_texture(denoise_program_.s_gtao_normal, 2, params.g_buffer->get_texture(1));
        gfx::set_image(3, output->native_handle(), 0, bgfx::Access::Write);
        gfx::dispatch(pass.id,
                      denoise_program_.program->native_handle(),
                      group_count(ctx.ao_size.width, group_size),
                      group_count(ctx.ao_size.height, group_size),
                      1);
        input = output;
    }
    return input;
}

auto gtao_pass::run_temporal(gfx::render_view& rview,
                             const frame_context& ctx,
                             const run_params& params,
                             const gfx::texture::ptr& depth_mips,
                             const gfx::texture::ptr& input) -> gfx::texture::ptr
{
    auto& state = rview.data().get_or_emplace<gtao_state>(DATA_STATE);
    if(!ctx.config.enable_temporal || ctx.config.temporal_history <= 0.0f || !temporal_program_.is_valid())
    {
        release_history(rview);
        return input;
    }
    // A resize (or a first frame) has no history worth reading.
    if(state.history_size.width != ctx.ao_size.width || state.history_size.height != ctx.ao_size.height)
    {
        state.history_valid = false;
        state.history_size = ctx.ao_size;
    }
    const char* read_name = (state.parity % 2 == 0) ? TEX_HISTORY_0 : TEX_HISTORY_1;
    const char* write_name = (state.parity % 2 == 0) ? TEX_HISTORY_1 : TEX_HISTORY_0;
    auto history = create_or_update_texture(rview, read_name, ctx.ao_size, gfx::texture_format::RGBA8, false, clamp_flags);
    auto output = create_or_update_texture(rview, write_name, ctx.ao_size, gfx::texture_format::RGBA8, false, clamp_flags);
    const bool history_valid = state.history_valid && params.prev_depth != nullptr;
    gfx::render_pass pass("GTAO/Temporal");
    pass.set_view_proj(ctx.cam->get_view(), ctx.cam->get_projection());
    set_common_uniforms(ctx);
    // The TAA-unjittered previous pair, like every temporal consumer in the pipeline: the
    // jittered prev misaligns a still camera's reprojection by the jitter delta each frame.
    auto prev_view_proj = ctx.cam->get_prev_view_projection_unjittered();
    gfx::set_uniform(temporal_program_.u_gtao_prev_view_proj, prev_view_proj.get_matrix());
    const float temporal[4] = {std::clamp(ctx.config.temporal_history, 0.0f, 0.98f),
                               params.velocity ? 1.0f : 0.0f,
                               std::max(ctx.config.temporal_depth_threshold, 0.005f),
                               history_valid ? 1.0f : 0.0f};
    gfx::set_uniform(temporal_program_.u_gtao_temporal, temporal);
    gfx::set_texture(temporal_program_.s_gtao_current, 0, input);
    gfx::set_texture(temporal_program_.s_gtao_history, 1, history);
    gfx::set_texture(temporal_program_.s_gtao_velocity, 2, params.velocity ? params.velocity : default_textures::get().black_texture());
    gfx::set_texture(temporal_program_.s_gtao_depth_mips, 3, depth_mips);
    gfx::set_texture(temporal_program_.s_gtao_prev_depth, 4, params.prev_depth ? params.prev_depth : default_textures::get().white_texture());
    gfx::set_image(5, output->native_handle(), 0, bgfx::Access::Write);
    gfx::dispatch(pass.id,
                  temporal_program_.program->native_handle(),
                  group_count(ctx.ao_size.width, group_size),
                  group_count(ctx.ao_size.height, group_size),
                  1);
    state.parity ^= 1u;
    state.history_valid = true;
    return output;
}

auto gtao_pass::run_upsample(gfx::render_view& rview,
                             const frame_context& ctx,
                             const run_params& params,
                             const gfx::texture::ptr& depth_mips,
                             const gfx::texture::ptr& input) -> gfx::texture::ptr
{
    auto output = create_or_update_texture(rview, TEX_OUTPUT, ctx.full_size, gfx::texture_format::RGBA8, false, clamp_flags);
    gfx::render_pass pass("GTAO/Upsample");
    pass.set_view_proj(ctx.cam->get_view(), ctx.cam->get_projection());
    set_common_uniforms(ctx);
    gfx::set_texture(upsample_program_.s_gtao_input, 0, input);
    gfx::set_texture(upsample_program_.s_gtao_depth_mips, 1, depth_mips);
    gfx::set_texture(upsample_program_.s_gtao_depth, 2, params.g_buffer->get_texture(4));
    gfx::set_image(3, output->native_handle(), 0, bgfx::Access::Write);
    gfx::dispatch(pass.id,
                  upsample_program_.program->native_handle(),
                  group_count(ctx.full_size.width, group_size),
                  group_count(ctx.full_size.height, group_size),
                  1);
    return output;
}

auto gtao_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::texture::ptr
{
    if(!params.g_buffer || !params.cam || !prefilter_program_.is_valid() || !main_program_.is_valid() ||
       !upsample_program_.is_valid())
    {
        return nullptr;
    }
    APP_SCOPE_PERF("Rendering/GTAO Pass");
    frame_context ctx;
    ctx.full_size = params.g_buffer->get_size();
    ctx.ao_size = compute_trace_size(ctx.full_size, params.config.resolution);
    ctx.cam = params.cam;
    ctx.config = params.config;
    gfx::render_pass::push_scope("GTAO");
    auto depth_mips = create_or_update_texture(rview, TEX_DEPTH_MIPS, ctx.ao_size, gfx::texture_format::R32F, true, clamp_flags | point_flags);
    run_prefilter(rview, ctx, params, depth_mips);
    auto result = run_main(rview, ctx, params, depth_mips);
    result = run_denoise(rview, ctx, params, depth_mips, result);
    result = run_temporal(rview, ctx, params, depth_mips, result);
    result = run_upsample(rview, ctx, params, depth_mips, result);
    gfx::render_pass::pop_scope();
    return result;
}

void gtao_pass::release_history(gfx::render_view& rview)
{
    rview.tex_remove(TEX_HISTORY_0);
    rview.tex_remove(TEX_HISTORY_1);
    auto& state = rview.data().get_or_emplace<gtao_state>(DATA_STATE);
    state.history_valid = false;
}

void gtao_pass::release_resources(gfx::render_view& rview)
{
    release_history(rview);
    rview.tex_remove(TEX_DEPTH_MIPS);
    rview.tex_remove(TEX_RAW);
    rview.tex_remove(TEX_DENOISE_A);
    rview.tex_remove(TEX_DENOISE_B);
    rview.tex_remove(TEX_OUTPUT);
}

} // namespace unravel
