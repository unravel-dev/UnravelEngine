#include "atmospheric_pass_perez.h"
#include <engine/assets/asset_manager.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/perez_luminance.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <algorithm>
#include <cstring>

namespace unravel
{

namespace
{
#ifndef ANONYMOUS
#define ANONYMOUS anonymous
#endif
namespace ANONYMOUS
{
// Represents color. Color-space depends on context.
// In the code below, used to represent color in XYZ, and RGB color-space
typedef bx::Vec3 Color;

// Performs piecewise linear interpolation of a Color parameter.
class dynamic_value_controller
{
    using value_type = Color;
    using key_map = std::map<float, value_type>;

public:
    dynamic_value_controller(const key_map& keymap) : key_map_(keymap)
    {
    }

    value_type get_value(float time) const
    {
        auto itUpper = key_map_.upper_bound(time + 1e-6f);
        auto itLower = itUpper;
        --itLower;

        if(itLower == key_map_.end())
        {
            return itUpper->second;
        }

        if(itUpper == key_map_.end())
        {
            return itLower->second;
        }

        float lowerTime = itLower->first;
        const auto& lowerVal = itLower->second;
        float upperTime = itUpper->first;
        const auto& upperVal = itUpper->second;

        if(lowerTime == upperTime)
        {
            return lowerVal;
        }

        return interpolate(lowerTime, lowerVal, upperTime, upperVal, time);
    };

private:
    value_type interpolate(float lowerTime,
                           const value_type& lowerVal,
                           float upperTime,
                           const value_type& upperVal,
                           float time) const
    {
        const float tt = (time - lowerTime) / (upperTime - lowerTime);
        const auto result = bx::lerp(lowerVal, upperVal, tt);
        return result;
    };

    const key_map& key_map_;
};

// HDTV rec. 709 matrix.
static constexpr float M_XYZ2RGB[] = {
    3.240479f,
    -0.969256f,
    0.055648f,
    -1.53715f,
    1.875991f,
    -0.204043f,
    -0.49853f,
    0.041556f,
    1.057311f,
};

// Converts color representation from CIE XYZ to RGB color-space.
Color xyzToRgb(const Color& xyz)
{
    Color rgb(bx::InitNone);
    rgb.x = M_XYZ2RGB[0] * xyz.x + M_XYZ2RGB[3] * xyz.y + M_XYZ2RGB[6] * xyz.z;
    rgb.y = M_XYZ2RGB[1] * xyz.x + M_XYZ2RGB[4] * xyz.y + M_XYZ2RGB[7] * xyz.z;
    rgb.z = M_XYZ2RGB[2] * xyz.x + M_XYZ2RGB[5] * xyz.y + M_XYZ2RGB[8] * xyz.z;
    return rgb;
};

// Precomputed luminance of sunlight in XYZ colorspace.
// Computed using code from Game Engine Gems, Volume One, chapter 15. Implementation based on Dr. Richard Bird model.
// This table is used for piecewise linear interpolation. Transitions from and to 0.0 at sunset and sunrise are highly
// inaccurate
static std::map<float, Color> sunLuminanceXYZTable = {
    {5.0f, {0.000000f, 0.000000f, 0.000000f}},
    {7.0f, {12.703322f, 12.989393f, 9.100411f}},
    {8.0f, {13.202644f, 13.597814f, 11.524929f}},
    {9.0f, {13.192974f, 13.597458f, 12.264488f}},
    {10.0f, {13.132943f, 13.535914f, 12.560032f}},
    {11.0f, {13.088722f, 13.489535f, 12.692996f}},
    {12.0f, {13.067827f, 13.467483f, 12.745179f}},
    {13.0f, {13.069653f, 13.469413f, 12.740822f}},
    {14.0f, {13.094319f, 13.495428f, 12.678066f}},
    {15.0f, {13.142133f, 13.545483f, 12.526785f}},
    {16.0f, {13.201734f, 13.606017f, 12.188001f}},
    {17.0f, {13.182774f, 13.572725f, 11.311157f}},
    {18.0f, {12.448635f, 12.672520f, 8.267771f}},
    {20.0f, {0.000000f, 0.000000f, 0.000000f}},
};

// Precomputed luminance of sky in the zenith point in XYZ colorspace.
// Computed using code from Game Engine Gems, Volume One, chapter 15. Implementation based on Dr. Richard Bird model.
// This table is used for piecewise linear interpolation. Day/night transitions are highly inaccurate.
// The scale of luminance change in Day/night transitions is not preserved.
// Luminance at night was increased to eliminate need the of HDR render.
static std::map<float, Color> skyLuminanceXYZTable = {
    {0.0f, bx::mul({0.308f, 0.308f, 0.411f}, 0.0f)},
    //{1.0f, {0.308f, 0.308f, 0.410f}},
    //{2.0f, {0.301f, 0.301f, 0.402f}},
    //{3.0f, {0.287f, 0.287f, 0.382f}},
    {4.0f, bx::mul({0.258f, 0.258f, 0.344f}, 0.05f)},
    {5.0f, {0.258f, 0.258f, 0.344f}},
    {7.0f, {0.962851f, 1.000000f, 1.747835f}},
    {8.0f, {0.967787f, 1.000000f, 1.776762f}},
    {9.0f, {0.970173f, 1.000000f, 1.788413f}},
    {10.0f, {0.971431f, 1.000000f, 1.794102f}},
    {11.0f, {0.972099f, 1.000000f, 1.797096f}},
    {12.0f, {0.972385f, 1.000000f, 1.798389f}},
    {13.0f, {0.972361f, 1.000000f, 1.798278f}},
    {14.0f, {0.972020f, 1.000000f, 1.796740f}},
    {15.0f, {0.971275f, 1.000000f, 1.793407f}},
    {16.0f, {0.969885f, 1.000000f, 1.787078f}},
    {17.0f, {0.967216f, 1.000000f, 1.773758f}},
    {18.0f, {0.961668f, 1.000000f, 1.739891f}},
    {20.0f, {0.264f, 0.264f, 0.352f}},
    {21.0f, bx::mul({0.264f, 0.264f, 0.352f}, 0.05f)},
    //{22.0f, {0.290f, 0.290f, 0.386f}},
    {23.0f, bx::mul({0.308f, 0.308f, 0.411f}, 0.0f)},
    {24.0f, bx::mul({0.308f, 0.308f, 0.411f}, 0.0f)},
};

// Turbidity tables. Taken from:
// A. J. Preetham, P. Shirley, and B. Smits. A Practical Analytic Model for Daylight. SIGGRAPH '99
// Coefficients correspond to xyY colorspace.
static constexpr Color ABCDE[] = {
    {-0.2592f, -0.2608f, -1.4630f},
    {0.0008f, 0.0092f, 0.4275f},
    {0.2125f, 0.2102f, 5.3251f},
    {-0.8989f, -1.6537f, -2.5771f},
    {0.0452f, 0.0529f, 0.3703f},
};

static constexpr Color ABCDE_t[] = {
    {-0.0193f, -0.0167f, 0.1787f},
    {-0.0665f, -0.0950f, -0.3554f},
    {-0.0004f, -0.0079f, -0.0227f},
    {-0.0641f, -0.0441f, 0.1206f},
    {-0.0033f, -0.0109f, -0.0670f},
};

void compute_perez_coeff(float _turbidity, float* _outPerezCoeff)
{
    const bx::Vec3 turbidity = {_turbidity, _turbidity, _turbidity};
    for(uint32_t ii = 0; ii < 5; ++ii)
    {
        const bx::Vec3 tmp = bx::mad(ABCDE_t[ii], turbidity, ABCDE[ii]);
        float* out = _outPerezCoeff + 4 * ii;
        bx::store(out, tmp);
        out[3] = 0.0f;
    }
}

float hour_of_day(math::vec3 sun_dir)
{
    // Define the ground normal vector (assuming flat and horizontal ground)
    math::vec3 normal(0.0, -1.0, 0.0);

    auto v1 = sun_dir;
    auto v2 = normal;
    auto ref = math::vec3(-1.0f, 0.0f, 0.0f);

    float angle = math::orientedAngle(v1, v2, ref);  // angle in [-pi, pi]
    angle = math::mod(angle, 2 * math::pi<float>()); // angle in [0, 2pi]
    angle = math::degrees(angle);
    // The hour angle is 0 at 6:00, 90 at 12:00, and 180 at 18:00
    // Therefore, we can use a simple linear formula to map the hour angle to the hour of day
    float hour_of_day = angle / 15;

    // Return the hour of day
    return hour_of_day;
}
}
} // namespace

atmospheric_pass_perez::~atmospheric_pass_perez()
{
    vb_.reset();
    ib_.reset();
}

auto atmospheric_pass_perez::init(rtti::context& ctx) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto vs_sky = am.get_asset<gfx::shader>("engine:/data/shaders/atmospherics/vs_sky.sc");
    auto fs_sky = am.get_asset<gfx::shader>("engine:/data/shaders/atmospherics/fs_sky.sc");
    auto fs_cloud = am.get_asset<gfx::shader>("engine:/data/shaders/atmospherics/fs_cloud.sc");

    atmospheric_program_.cache_uniforms();
    atmospheric_program_.program = std::make_unique<gpu_program>(vs_sky, fs_sky);

    cloud_program_.cache_uniforms();
    cloud_program_.program = std::make_unique<gpu_program>(vs_sky, fs_cloud);

    int vertical_count = 32;
    int horizontal_count = 32;
    std::vector<gfx::screen_pos_vertex> vertices(vertical_count * horizontal_count);

    for(int i = 0; i < vertical_count; i++)
    {
        for(int j = 0; j < horizontal_count; j++)
        {
            gfx::screen_pos_vertex& v = vertices[i * vertical_count + j];
            v.x = float(j) / (horizontal_count - 1) * 2.0f - 1.0f;
            v.y = float(i) / (vertical_count - 1) * 2.0f - 1.0f;
        }
    }

    std::vector<uint16_t> indices((vertical_count - 1) * (horizontal_count - 1) * 6);

    int k = 0;
    for(int i = 0; i < vertical_count - 1; i++)
    {
        for(int j = 0; j < horizontal_count - 1; j++)
        {
            indices[k++] = (uint16_t)(j + 0 + horizontal_count * (i + 0));
            indices[k++] = (uint16_t)(j + 1 + horizontal_count * (i + 0));
            indices[k++] = (uint16_t)(j + 0 + horizontal_count * (i + 1));

            indices[k++] = (uint16_t)(j + 1 + horizontal_count * (i + 0));
            indices[k++] = (uint16_t)(j + 1 + horizontal_count * (i + 1));
            indices[k++] = (uint16_t)(j + 0 + horizontal_count * (i + 1));
        }
    }

    vb_ = std::make_unique<gfx::vertex_buffer>(
        gfx::copy(vertices.data(), sizeof(gfx::screen_pos_vertex) * vertical_count * horizontal_count),
        gfx::screen_pos_vertex::get_layout());
    ib_ = std::make_unique<gfx::index_buffer>(gfx::copy(indices.data(), sizeof(uint16_t) * k));

    sun_.update(0);

    return true;
}

namespace
{
constexpr int cloud_mode_volumetric = 2;
// Jitter sequence length uploaded as a float: the golden-ratio fract loses precision past
// ~2^20, and the sequence only needs to cover the accumulation window.
constexpr uint32_t cloud_jitter_period = 1024;
constexpr const char* cloud_frame_count_key = "CLOUD_FRAME_COUNT";
constexpr const char* cloud_prev_wind_keys[2] = {"CLOUD_PREV_WIND_X", "CLOUD_PREV_WIND_Y"};
constexpr const char* cloud_tex_keys[2] = {"CLOUD_PING", "CLOUD_PONG"};
constexpr const char* cloud_conf_keys[2] = {"CLOUD_CONF_PING", "CLOUD_CONF_PONG"};
constexpr const char* cloud_fbo_keys[2] = {"CLOUD_FBO_PING", "CLOUD_FBO_PONG"};
} // namespace

void atmospheric_pass_perez::release_cloud_resources(gfx::render_view& rview)
{
    if(!rview.tex_safe_get(cloud_tex_keys[0]))
    {
        return;
    }
    for(int i = 0; i < 2; ++i)
    {
        rview.fbo_remove(cloud_fbo_keys[i]);
        rview.tex_remove(cloud_tex_keys[i]);
        rview.tex_remove(cloud_conf_keys[i]);
    }
    rview.data_get_or_emplace(cloud_frame_count_key) = 0;
    rview.data_get_or_emplace(cloud_prev_wind_keys[0]) = 0;
    rview.data_get_or_emplace(cloud_prev_wind_keys[1]) = 0;
}

auto atmospheric_pass_perez::run_cloud_prepass(const camera& camera,
                                               gfx::render_view& rview,
                                               const usize32_t& output_size,
                                               const irradiance_perez_params& perez,
                                               const cloud_uniform_block& uniforms,
                                               const run_params& params) -> gfx::texture::ptr
{
    const auto& view = camera.get_view_relative();
    const auto& proj = camera.get_projection();

    const uint32_t half_w = std::max(1u, output_size.width / 2);
    const uint32_t half_h = std::max(1u, output_size.height / 2);
    const usize32_t half_size{half_w, half_h};

    constexpr uint64_t cloud_tex_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    auto& cloud_frame_count = rview.data_get_or_emplace(cloud_frame_count_key);

    // The wind offset wraps to the noise tile period; the per-frame advance (unwrapped)
    // drives the history reprojection.
    constexpr float wind_period = float(cloud_noise_textures::tile_period);
    float wind_delta[2] = {0.0f, 0.0f};
    for(int i = 0; i < 2; ++i)
    {
        auto& prev_bits = rview.data_get_or_emplace(cloud_prev_wind_keys[i]);
        float prev{};
        std::memcpy(&prev, &prev_bits, sizeof(float));
        const float cur = params.cloud_wind_offset[i];
        float delta = cur - prev;
        if(delta > 0.5f * wind_period)
        {
            delta -= wind_period;
        }
        else if(delta < -0.5f * wind_period)
        {
            delta += wind_period;
        }
        wind_delta[i] = delta;
        std::memcpy(&prev_bits, &cur, sizeof(float));
    }

    bool recreated = false;
    gfx::texture::ptr cloud_tex[2];
    gfx::texture::ptr cloud_conf[2];
    for(int i = 0; i < 2; ++i)
    {
        auto& tex = rview.tex_get_or_emplace(cloud_tex_keys[i]);
        if(gfx::needs_recreate(tex, half_size))
        {
            tex = std::make_shared<gfx::texture>(half_w, half_h, false, 1, gfx::texture_format::RGBA16F, cloud_tex_flags);
            recreated = true;
        }
        auto& conf = rview.tex_get_or_emplace(cloud_conf_keys[i]);
        if(gfx::needs_recreate(conf, half_size))
        {
            conf = std::make_shared<gfx::texture>(half_w, half_h, false, 1, gfx::texture_format::R8, cloud_tex_flags);
            recreated = true;
        }
        cloud_tex[i] = tex;
        cloud_conf[i] = conf;
    }

    gfx::frame_buffer::ptr cloud_fbo[2];
    for(int i = 0; i < 2; ++i)
    {
        auto& fbo = rview.fbo_get_or_emplace(cloud_fbo_keys[i]);
        if(recreated || gfx::needs_recreate(fbo, half_size))
        {
            fbo = std::make_shared<gfx::frame_buffer>();
            fbo->populate({cloud_tex[i], cloud_conf[i]});
        }
        cloud_fbo[i] = fbo;
    }
    if(recreated)
    {
        cloud_frame_count = 0;
    }

    const uint32_t cur = cloud_frame_count & 1;
    const uint32_t prev = cur ^ 1;

    gfx::render_pass cloud_pass("Atmospherics/Cloud Pre-Pass");
    cloud_pass.bind(cloud_fbo[cur].get());
    cloud_pass.set_view_proj(view, proj);
    cloud_pass.clear(BGFX_CLEAR_COLOR, 0x000000FF, 0.0f, 0);

    cloud_program_.program->begin();

    gfx::set_uniform(cloud_program_.u_skyLuminanceXYZ, perez.sky_luminance_xyz);
    gfx::set_uniform(cloud_program_.u_skyLuminance, perez.sky_luminance_rgb);
    gfx::set_uniform(cloud_program_.u_sunLuminance, perez.sun_luminance_rgb);
    gfx::set_uniform(cloud_program_.u_sunDirection, perez.sun_direction);
    gfx::set_uniform(cloud_program_.u_parameters, uniforms.exposition);
    gfx::set_uniform(cloud_program_.u_perezCoeff, &perez.perez_coeff[0][0], 5);
    gfx::set_uniform(cloud_program_.u_cloudParams, uniforms.cloud_params);
    gfx::set_uniform(cloud_program_.u_cloudParams2, uniforms.cloud_params2);
    gfx::set_uniform(cloud_program_.u_cloudParams3, uniforms.cloud_params3);
    gfx::set_uniform(cloud_program_.u_cloudParams4, uniforms.cloud_params4);

    // x = jitter index, y = history valid, zw = wind offset advanced since the last frame.
    const float history_valid = cloud_frame_count > 0 ? 1.0f : 0.0f;
    float cloud_frame[4] = {float(cloud_frame_count % cloud_jitter_period), history_valid, wind_delta[0], wind_delta[1]};
    gfx::set_uniform(cloud_program_.u_cloudFrame, cloud_frame);

    auto prev_vp = camera.get_prev_view_projection_relative();
    gfx::set_uniform(cloud_program_.u_prevViewProj, prev_vp.get_matrix());

    auto& cloud_noise = default_textures::get().cloud_noise();
    if(cloud_noise.base_noise)
    {
        gfx::set_texture(cloud_program_.s_cloudNoise, 0, cloud_noise.base_noise.get());
    }
    gfx::set_texture(cloud_program_.s_cloudHistory, 1, cloud_tex[prev].get());
    gfx::set_texture(cloud_program_.s_cloudHistoryConf, 2, cloud_conf[prev].get());
    if(cloud_noise.flat_noise)
    {
        gfx::set_texture(cloud_program_.s_cloudNoise2D, 3, cloud_noise.flat_noise.get());
    }

    irect32_t cloud_rect(0, 0, half_w, half_h);
    gfx::set_scissor(cloud_rect.left, cloud_rect.top, cloud_rect.width(), cloud_rect.height());

    gfx::set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    gfx::set_index_buffer(ib_->native_handle());
    gfx::set_vertex_buffer(0, vb_->native_handle());
    gfx::submit(cloud_pass.id, cloud_program_.program->native_handle());

    gfx::set_state(BGFX_STATE_DEFAULT);
    cloud_program_.program->end();

    cloud_frame_count++;
    return cloud_tex[cur];
}

void atmospheric_pass_perez::run(gfx::frame_buffer::ptr input,
                                 const camera& camera,
                                 gfx::render_view& rview,
                                 delta_t dt,
                                 const run_params& params)
{
    const auto& view = camera.get_view_relative();
    const auto& proj = camera.get_projection();

    const auto surface = input.get();
    const auto output_size = surface->get_size();

    irradiance_perez_params perez;
    compute_irradiance_perez_params(params.light_direction, params.turbidity, perez);
    perez.exposition *= params.sky_brightness;

    float hour = ANONYMOUS::hour_of_day(-params.light_direction);
    cloud_uniform_block uniforms{};
    uniforms.exposition[0] = 0.02f;
    uniforms.exposition[1] = 3.0f;
    uniforms.exposition[2] = perez.exposition;
    uniforms.exposition[3] = hour;
    // Layout shared with clouds.sh / fs_cloud.sc / fs_sky.sc.
    uniforms.cloud_params[0] = params.cloud_coverage;
    uniforms.cloud_params[1] = params.cloud_base_altitude;
    uniforms.cloud_params[2] = params.cloud_thickness;
    uniforms.cloud_params[3] = params.cloud_density;
    uniforms.cloud_params2[0] = params.cloud_shadow_strength;
    uniforms.cloud_params2[1] = 1.0f / std::max(params.cloud_size, 1.0f);
    uniforms.cloud_params2[2] = params.cloud_softness;
    uniforms.cloud_params2[3] = float(params.cloud_mode);
    uniforms.cloud_params3[0] = params.cloud_detail_erode;
    uniforms.cloud_params3[1] = params.cloud_macro_variation;
    uniforms.cloud_params3[2] = params.cloud_wind_offset.x;
    uniforms.cloud_params3[3] = params.cloud_wind_offset.y;
    uniforms.cloud_params4[0] = params.cloud_time;
    uniforms.cloud_params4[1] = 0.0f;
    uniforms.cloud_params4[2] = 0.0f;
    uniforms.cloud_params4[3] = 0.0f;

    // === Pass 1: cloud pre-pass at half resolution with temporal accumulation ===
    gfx::texture::ptr cloud_tex;
    const bool volumetric = params.cloud_mode == cloud_mode_volumetric;
    if(volumetric && cloud_program_.program && cloud_program_.program->is_valid())
    {
        cloud_tex = run_cloud_prepass(camera, rview, output_size, perez, uniforms, params);
    }
    else
    {
        release_cloud_resources(rview);
    }

    // === Pass 2: sky pass (full resolution, composites the half-res clouds) ===
    gfx::render_pass pass("Atmospherics/Sky Pass");
    pass.bind(surface);
    pass.set_view_proj(view, proj);

    if(atmospheric_program_.program->is_valid())
    {
        atmospheric_program_.program->begin();

        gfx::set_uniform(atmospheric_program_.u_sunLuminance, perez.sun_luminance_rgb);
        gfx::set_uniform(atmospheric_program_.u_skyLuminanceXYZ, perez.sky_luminance_xyz);
        gfx::set_uniform(atmospheric_program_.u_skyLuminance, perez.sky_luminance_rgb);
        gfx::set_uniform(atmospheric_program_.u_sunDirection, perez.sun_direction);
        gfx::set_uniform(atmospheric_program_.u_parameters, uniforms.exposition);
        gfx::set_uniform(atmospheric_program_.u_perezCoeff, &perez.perez_coeff[0][0], 5);
        gfx::set_uniform(atmospheric_program_.u_cloudParams, uniforms.cloud_params);
        gfx::set_uniform(atmospheric_program_.u_cloudParams2, uniforms.cloud_params2);
        gfx::set_uniform(atmospheric_program_.u_cloudParams3, uniforms.cloud_params3);
        gfx::set_uniform(atmospheric_program_.u_cloudParams4, uniforms.cloud_params4);

        if(cloud_tex)
        {
            gfx::set_texture(atmospheric_program_.s_cloudTex, 0, cloud_tex);
        }

        auto& cloud_noise = default_textures::get().cloud_noise();
        if(cloud_noise.flat_noise)
        {
            gfx::set_texture(atmospheric_program_.s_cloudNoise2D, 1, cloud_noise.flat_noise.get());
        }

        irect32_t rect(0, 0, irect32_t::value_type(output_size.width), irect32_t::value_type(output_size.height));
        gfx::set_scissor(rect.left, rect.top, rect.width(), rect.height());

        gfx::set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_EQUAL);
        gfx::set_index_buffer(ib_->native_handle());
        gfx::set_vertex_buffer(0, vb_->native_handle());
        gfx::submit(pass.id, atmospheric_program_.program->native_handle());

        gfx::set_state(BGFX_STATE_DEFAULT);
        atmospheric_program_.program->end();
    }

    gfx::discard();
}

auto compute_perez_exposition(float sun_altitude) -> float
{
    const float altitude_factor =
        bx::lerp(perez_horizon_dim, 1.0f, bx::clamp(bx::abs(sun_altitude), 0.0f, 1.0f));
    return perez_luminance_to_engine * altitude_factor;
}

void compute_perez_luminance(const math::vec3& light_direction,
                             math::vec3& out_sky_luminance_rgb,
                             math::vec3& out_sun_luminance_rgb)
{
    auto hour = ANONYMOUS::hour_of_day(-light_direction);
    ANONYMOUS::dynamic_value_controller sun_luminance_dc(ANONYMOUS::sunLuminanceXYZTable);
    ANONYMOUS::dynamic_value_controller sky_luminance_dc(ANONYMOUS::skyLuminanceXYZTable);
    auto sunLuminanceXYZ = sun_luminance_dc.get_value(hour);
    auto sunLuminanceRGB = ANONYMOUS::xyzToRgb(sunLuminanceXYZ);
    out_sun_luminance_rgb = math::vec3(sunLuminanceRGB.x, sunLuminanceRGB.y, sunLuminanceRGB.z);
    auto skyLuminanceXYZ = sky_luminance_dc.get_value(hour);
    auto skyLuminanceRGB = ANONYMOUS::xyzToRgb(skyLuminanceXYZ);
    out_sky_luminance_rgb = math::vec3(skyLuminanceRGB.x, skyLuminanceRGB.y, skyLuminanceRGB.z);
}

void compute_irradiance_perez_params(const math::vec3& light_direction,
                                     float turbidity,
                                     irradiance_perez_params& out)
{
    math::vec3 sun_dir(-light_direction.x, -light_direction.y, -light_direction.z);
    sun_dir = math::normalize(sun_dir);

    auto hour = ANONYMOUS::hour_of_day(-light_direction);
    ANONYMOUS::dynamic_value_controller sun_luminance_dc(ANONYMOUS::sunLuminanceXYZTable);
    ANONYMOUS::dynamic_value_controller sky_luminance_dc(ANONYMOUS::skyLuminanceXYZTable);
    auto sunLuminanceXYZ = sun_luminance_dc.get_value(hour);
    auto sunLuminanceRGB = ANONYMOUS::xyzToRgb(sunLuminanceXYZ);
    out.sun_luminance_rgb = math::vec3(sunLuminanceRGB.x, sunLuminanceRGB.y, sunLuminanceRGB.z);

    auto skyLuminanceXYZ = sky_luminance_dc.get_value(hour);
    out.sky_luminance_xyz =
        math::vec3(skyLuminanceXYZ.x, skyLuminanceXYZ.y, skyLuminanceXYZ.z);
    auto skyLuminanceRGB = ANONYMOUS::xyzToRgb(skyLuminanceXYZ);
    out.sky_luminance_rgb = math::vec3(skyLuminanceRGB.x, skyLuminanceRGB.y, skyLuminanceRGB.z);

    out.sun_direction = sun_dir;

    // The one shared Perez -> engine conversion (see perez_luminance.h): the sky dome,
    // the irradiance bake and the flat ambient all inherit this value, so their ratios
    // cannot drift apart.
    out.exposition = compute_perez_exposition(sun_dir.y);

    ANONYMOUS::compute_perez_coeff(turbidity, &out.perez_coeff[0][0]);
}

} // namespace unravel
