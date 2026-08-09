#include "atmospheric_pass_perez.h"
#include <engine/assets/asset_manager.h>
#include <engine/rendering/default_textures.h>
#include <engine/rendering/perez_luminance.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
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
    float exposition[4] = {0.02f, 3.0f, perez.exposition, hour};
    float cloud_params[4] = {params.cloud_coverage, params.cloud_base_altitude, params.cloud_time, params.cloud_density};
    float cloud_params2[4] = {params.cloud_absorption, params.cloud_light_absorption, params.cloud_top_altitude, float(params.cloud_mode)};
    float cloud_params3[4] = {params.cloud_vol_uv_scale,
                              params.cloud_vol_edge_width,
                              params.cloud_vol_shape_power,
                              params.cloud_vol_detail_erode};
    float cloud_params4[4] = {params.cloud_vol_macro_strength,
                              params.cloud_vol_coarse_scale,
                              params.cloud_vol_base_mix,
                              params.cloud_vol_sun_intensity};

    auto& cloud_noise = default_textures::get().cloud_noise();

    // === Pass 1: Cloud pre-pass at half resolution with temporal blending (ping-pong) ===
    // Only run when cloud_mode == volumetric (2)
    if(params.cloud_mode == 2 && cloud_program_.program && cloud_program_.program->is_valid())
    {
        uint32_t half_w = output_size.width / 2;
        uint32_t half_h = output_size.height / 2;
        if(half_w < 1) half_w = 1;
        if(half_h < 1) half_h = 1;

        constexpr uint64_t cloud_tex_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

        auto& cloud_frame_count = rview.data_get_or_emplace("CLOUD_FRAME_COUNT");

        auto& prev_cloud_time_bits = rview.data_get_or_emplace("CLOUD_PREV_TIME");
        float prev_cloud_time{};
        std::memcpy(&prev_cloud_time, &prev_cloud_time_bits, sizeof(float));
        float cloud_time_delta = params.cloud_time - prev_cloud_time;
        uint32_t cur_time_bits{};
        std::memcpy(&cur_time_bits, &params.cloud_time, sizeof(float));
        prev_cloud_time_bits = cur_time_bits;

        auto& cloud_tex_a = rview.tex_get_or_emplace("CLOUD_PING");
        if(gfx::needs_recreate(cloud_tex_a, {half_w, half_h}))
        {
            cloud_tex_a.reset();
            cloud_tex_a = std::make_shared<gfx::texture>(half_w, half_h, false, 1,
                gfx::texture_format::RGBA16F, cloud_tex_flags);
            cloud_frame_count = 0;
        }

        auto& cloud_tex_b = rview.tex_get_or_emplace("CLOUD_PONG");
        if(gfx::needs_recreate(cloud_tex_b, {half_w, half_h}))
        {
            cloud_tex_b.reset();
            cloud_tex_b = std::make_shared<gfx::texture>(half_w, half_h, false, 1,
                gfx::texture_format::RGBA16F, cloud_tex_flags);
            cloud_frame_count = 0;
        }

        uint32_t cur = cloud_frame_count & 1;
        auto& current_tex  = (cur == 0) ? cloud_tex_a : cloud_tex_b;
        auto& history_tex  = (cur == 0) ? cloud_tex_b : cloud_tex_a;

        auto& cloud_fbo_a = rview.fbo_get_or_emplace("CLOUD_FBO_PING");
        if(gfx::needs_recreate(cloud_fbo_a, {half_w, half_h}))
        {
            cloud_fbo_a.reset();
            cloud_fbo_a = std::make_shared<gfx::frame_buffer>();
            cloud_fbo_a->populate({cloud_tex_a});
        }

        auto& cloud_fbo_b = rview.fbo_get_or_emplace("CLOUD_FBO_PONG");
        if(gfx::needs_recreate(cloud_fbo_b, {half_w, half_h}))
        {
            cloud_fbo_b.reset();
            cloud_fbo_b = std::make_shared<gfx::frame_buffer>();
            cloud_fbo_b->populate({cloud_tex_b});
        }

        auto& current_fbo = (cur == 0) ? cloud_fbo_a : cloud_fbo_b;

        gfx::render_pass cloud_pass("Atmospherics/Cloud Pre-Pass");
        cloud_pass.bind(current_fbo.get());
        cloud_pass.set_view_proj(view, proj);
        cloud_pass.clear(BGFX_CLEAR_COLOR, 0x000000FF, 0.0f, 0);

        cloud_program_.program->begin();

        gfx::set_uniform(cloud_program_.u_skyLuminanceXYZ, perez.sky_luminance_xyz);
        gfx::set_uniform(cloud_program_.u_skyLuminance, perez.sky_luminance_rgb);
        gfx::set_uniform(cloud_program_.u_sunLuminance, perez.sun_luminance_rgb);
        gfx::set_uniform(cloud_program_.u_sunDirection, perez.sun_direction);
        gfx::set_uniform(cloud_program_.u_parameters, exposition);
        gfx::set_uniform(cloud_program_.u_perezCoeff, &perez.perez_coeff[0][0], 5);
        gfx::set_uniform(cloud_program_.u_cloudParams, cloud_params);
        gfx::set_uniform(cloud_program_.u_cloudParams2, cloud_params2);
        gfx::set_uniform(cloud_program_.u_cloudParams3, cloud_params3);
        gfx::set_uniform(cloud_program_.u_cloudParams4, cloud_params4);

        float cloud_frame[4] = {float(gfx::get_render_frame()), float(cloud_frame_count), cloud_time_delta, 0.0f};
        gfx::set_uniform(cloud_program_.u_cloudFrame, cloud_frame);

        auto prev_vp = camera.get_prev_view_projection_relative();
        gfx::set_uniform(cloud_program_.u_prevViewProj, prev_vp.get_matrix());

        if(cloud_noise.base_noise)
        {
            gfx::set_texture(cloud_program_.s_cloudNoise, 0, cloud_noise.base_noise.get());
        }
        gfx::set_texture(cloud_program_.s_cloudHistory, 1, history_tex.get());

        irect32_t cloud_rect(0, 0, half_w, half_h);
        gfx::set_scissor(cloud_rect.left, cloud_rect.top, cloud_rect.width(), cloud_rect.height());

        gfx::set_state(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        gfx::set_index_buffer(ib_->native_handle());
        gfx::set_vertex_buffer(0, vb_->native_handle());
        gfx::submit(cloud_pass.id, cloud_program_.program->native_handle());

        gfx::set_state(BGFX_STATE_DEFAULT);
        cloud_program_.program->end();

        cloud_frame_count++;
    }

    // === Pass 2: Sky pass (full resolution, composites half-res clouds) ===
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
        gfx::set_uniform(atmospheric_program_.u_parameters, exposition);
        gfx::set_uniform(atmospheric_program_.u_perezCoeff, &perez.perez_coeff[0][0], 5);
        gfx::set_uniform(atmospheric_program_.u_cloudParams, cloud_params);
        gfx::set_uniform(atmospheric_program_.u_cloudParams2, cloud_params2);

        auto cloud_frame_count = rview.data_get("CLOUD_FRAME_COUNT");
        uint32_t prev = (cloud_frame_count - 1) & 1;
        const auto& cloud_tex = rview.tex_safe_get(prev == 0 ? "CLOUD_PING" : "CLOUD_PONG");
        if(cloud_tex)
        {
            gfx::set_texture(atmospheric_program_.s_cloudTex, 0, cloud_tex);
        }

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

    float sun_altitude = sun_dir.y;
    // At zenith use 1.0, at horizon use 0.6 (was 0.35) for more vibrant sunsets
    float altitude_factor = bx::lerp(0.6f, 1.0f, bx::clamp(bx::abs(sun_altitude), 0.0f, 1.0f));
    out.exposition = 0.1f * altitude_factor;

    ANONYMOUS::compute_perez_coeff(turbidity, &out.perez_coeff[0][0]);
}

} // namespace unravel
