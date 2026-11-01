#include "tonemapping_pass.h"
#include "pass_helpers.h"
#include <engine/assets/asset_manager.h>
#include <engine/rendering/default_textures.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <math/math.h>
#include <algorithm>
#include <array>

namespace unravel
{

namespace
{

// CIE xy chromaticity to CAT02 LMS cone response (Y = 1).
auto cie_xy_to_lms(float x, float y) -> math::vec3
{
    const float Y = 1.0f;
    const float X = Y * x / y;
    const float Z = Y * (1.0f - x - y) / y;
    return {0.7328f * X + 0.4296f * Y - 0.1624f * Z,
            -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
            0.0030f * X + 0.0136f * Y + 0.9834f * Z};
}

// Von Kries white balance: temperature/tint in [-1, 1] move the assumed white
// point along the Planckian locus (and orthogonally for tint); the returned LMS
// scale re-adapts to D65.
auto compute_white_balance_lms(float temperature, float tint) -> math::vec3
{
    const float t1 = temperature * 10.0f / 6.0f;
    const float t2 = tint * 10.0f / 6.0f;
    const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
    const float standard_y = 2.87f * x - 3.0f * x * x - 0.27509507f;
    const float y = standard_y + t2 * 0.05f;
    const math::vec3 w1 = cie_xy_to_lms(0.31271f, 0.32902f); // D65
    const math::vec3 w2 = cie_xy_to_lms(x, y);
    return {w1.x / w2.x, w1.y / w2.y, w1.z / w2.z};
}

constexpr const char* lut_key = "TONEMAPPING_LUT";

/// The tone curve and colour grading a tonemapping LUT holds: the uniforms of fs_tonemapping_lut.eshader.
struct lut_grading
{
    std::array<float, 4> tonemapping{};
    std::array<float, 4> grading{};
    std::array<float, 4> white_balance{};
    std::array<float, 4> lift{};
    std::array<float, 4> gamma_inv{};
    std::array<float, 4> gain{};

    auto operator==(const lut_grading&) const -> bool = default;
};

/// What the view's LUT was last rendered from.
struct lut_state
{
    static constexpr const char* view_key = "TONEMAPPING_LUT_STATE";

    lut_grading grading{};
    /// The LUT program's revision the LUT was rendered with: a reloaded shader renders it again.
    std::uint32_t program_revision = 0;
    bool is_valid = false;
};

auto make_lut_grading(const tonemapping_pass::settings& config) -> lut_grading
{
    lut_grading grading;
    // w = 1: operators keep their native response. Display-white comes from Auto Exposure Compensation, not from
    // remapping curves onto AgX.
    grading.tonemapping = {0.0f, static_cast<float>(config.method), 0.0f, 1.0f};
    grading.grading = {config.contrast, config.saturation, 0.0f, 0.0f};
    const auto wb_lms = compute_white_balance_lms(config.temperature, config.tint);
    grading.white_balance = {wb_lms.x, wb_lms.y, wb_lms.z, 0.0f};
    // Lift/gamma/gain map from their neutral-gray (0.5) authoring space:
    // lift: +-0.15 additive at black; gain: 0..2x at white; gamma: per-channel
    // exponent uploaded pre-inverted so the shader does a single pow.
    const auto& lift = config.lift.value;
    const auto& gamma = config.gamma.value;
    const auto& gain = config.gain.value;
    grading.lift = {(lift.x - 0.5f) * 0.3f, (lift.y - 0.5f) * 0.3f, (lift.z - 0.5f) * 0.3f, 0.0f};
    grading.gamma_inv = {1.0f / std::max(gamma.x * 2.0f, 0.05f),
                         1.0f / std::max(gamma.y * 2.0f, 0.05f),
                         1.0f / std::max(gamma.z * 2.0f, 0.05f),
                         0.0f};
    grading.gain = {gain.x * 2.0f, gain.y * 2.0f, gain.z * 2.0f, 0.0f};
    return grading;
}

} // namespace

auto tonemapping_pass::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();

    auto vs_clip_quad_ex = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    auto fs_atmospherics = am.get_asset<gfx::shader>("engine:/data/shaders/tonemapping/fs_tonemapping.eshader");
    auto fs_lut = am.get_asset<gfx::shader>("engine:/data/shaders/tonemapping/fs_tonemapping_lut.eshader");

    tonemapping_program_.cache_uniforms();
    tonemapping_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_atmospherics);

    lut_program_.cache_uniforms();
    lut_program_.program = std::make_unique<gpu_program>(vs_clip_quad_ex, fs_lut);

    return tonemapping_program_.program->is_valid() && lut_program_.program->is_valid();
}

auto tonemapping_pass::update_lut(gfx::render_view& rview, const settings& config) -> const gfx::texture::ptr&
{
    auto& state = rview.data().get_or_emplace<lut_state>(lut_state::view_key);
    const auto& lut = rview.tex_acquire(lut_key,
                                        {.size = {lut_size * lut_size, lut_size},
                                         .format = bgfx::TextureFormat::RGBA16F,
                                         .flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP},
                                        [&state](const gfx::texture::ptr&)
                                        {
                                            state.is_valid = false;
                                        });
    const lut_grading grading = make_lut_grading(config);
    const bool can_render = lut_program_.program->begin();
    const std::uint32_t program_revision = lut_program_.program->get_revision();
    if(!can_render || (state.is_valid && state.grading == grading && state.program_revision == program_revision))
    {
        return lut;
    }
    const auto fbo = acquire_frame_buffer(rview, lut_key, {lut});
    // Ahead of the tonemapping pass's own view, so the table is complete before it is read.
    gfx::render_pass pass("Tonemapping/LUT Pass");
    pass.bind(fbo.get());
    pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);
    gfx::set_uniform(lut_program_.u_tonemapping, grading.tonemapping.data());
    gfx::set_uniform(lut_program_.u_grading, grading.grading.data());
    gfx::set_uniform(lut_program_.u_wb_lms, grading.white_balance.data());
    gfx::set_uniform(lut_program_.u_lift, grading.lift.data());
    gfx::set_uniform(lut_program_.u_gamma_inv, grading.gamma_inv.data());
    gfx::set_uniform(lut_program_.u_gain, grading.gain.data());
    bgfx::setState(gfx::clip_quad(1.0f) | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, lut_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    lut_program_.program->end();
    state.grading = grading;
    state.program_revision = program_revision;
    state.is_valid = true;
    return lut;
}

auto tonemapping_pass::run(gfx::render_view& rview, const run_params& params) -> gfx::frame_buffer::ptr
{
    const auto& input = params.input;
    const gfx::texture::ptr lut = update_lut(rview, params.config);
    auto output = get_output_frame_buffer(rview, "TONEMAPPING_OUTPUT", params.input, params.output, bgfx::TextureFormat::RGBA8);

    gfx::render_pass pass("Tonemapping/Pass");
    pass.bind(output.get());
    // Every pixel is drawn below: a clear instead of a load spares a tile-based GPU reading the old contents.
    pass.clear(BGFX_CLEAR_COLOR, 0, 1.0f, 0);

    const auto output_size = output->get_size();

    if(!tonemapping_program_.program->begin())
    {
        bgfx::discard();
        return output;
    }

    const bool apply_output_noise = !params.defer_output_noise;
    // The final linear colour is the scene colour / pre-exposure x the global exposure. The shader
    // multiplies this by the adapted exposure texture (and divides out the GPU's share of the pre-exposure).
    float tonemap[4] = {params.config.exposure / std::max(params.pre_exposure.value, 1e-12f),
                        static_cast<float>(params.config.method),
                        (apply_output_noise && params.config.dithering) ? 1.0f : 0.0f,
                        1.0f};
    gfx::set_uniform(tonemapping_program_.u_tonemapping, tonemap);

    // z = grain amount (slider scaled so 0.1-0.3 is a filmic range and 1.0 is heavy
    // stylized grain), w = animation seed. Deferred to FXAA when that pass follows.
    const float grain_amount = apply_output_noise ? params.config.grain_intensity * OUTPUT_GRAIN_SCALE : 0.0f;
    float grading[4] = {params.config.contrast, params.config.saturation, grain_amount, get_output_grain_seed()};
    gfx::set_uniform(tonemapping_program_.u_grading, grading);

    float vignette[4] = {params.config.vignette_intensity, params.config.vignette_smoothness, 0.0f, 0.0f};
    gfx::set_uniform(tonemapping_program_.u_vignette, vignette);

    gfx::set_texture(tonemapping_program_.s_input, 0, input->get_texture());
    gfx::set_texture(tonemapping_program_.s_exposure, 1, params.exposure_texture ? params.exposure_texture : default_textures::get().white_texture());

    // The bloom pyramid, added after local exposure scaled the scene; z = 0 skips it when the bloom did not run.
    const auto& bloom = params.bloom;
    const bool has_bloom = static_cast<bool>(bloom.pyramid);
    const auto& black = default_textures::get().black_texture();
    // A dirt intensity only with a mask: at 0 the shader skips the mask's fetch.
    const bool has_dirt = has_bloom && bloom.dirt && bloom.dirt != black;
    float bloom_params[4] = {bloom.intensity, has_dirt ? bloom.dirt_intensity : 0.0f, has_bloom ? 1.0f : 0.0f, 0.0f};
    float bloom_tint[4] = {bloom.tint.x, bloom.tint.y, bloom.tint.z, 0.0f};
    gfx::set_uniform(tonemapping_program_.u_bloom, bloom_params);
    gfx::set_uniform(tonemapping_program_.u_bloom_tint, bloom_tint);
    gfx::set_texture(tonemapping_program_.s_bloom, 4, has_bloom ? bloom.pyramid : black);
    gfx::set_texture(tonemapping_program_.s_dirt, 5, has_dirt ? bloom.dirt : black);
    gfx::set_texture(tonemapping_program_.s_tonemapping_lut, 6, lut);
    tonemapping_program_.local_exposure.submit(params.local_exposure, params.pre_exposure.value);
    params.pre_exposure.bind();

    irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
    bgfx::setScissor(rect.left, rect.top, rect.width(), rect.height());
    auto topology = gfx::clip_quad(1.0f);
    bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(pass.id, tonemapping_program_.program->native_handle());
    bgfx::setState(BGFX_STATE_DEFAULT);
    tonemapping_program_.program->end();

    bgfx::discard();

    return output;
}

void tonemapping_pass::release_resources(gfx::render_view& rview)
{
    rview.fbo_remove("TONEMAPPING_OUTPUT");
    rview.tex_remove("TONEMAPPING_OUTPUT");
    rview.fbo_remove(lut_key);
    rview.tex_remove(lut_key);
    rview.data().get_or_emplace<lut_state>(lut_state::view_key) = {};
}

} // namespace unravel
