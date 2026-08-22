$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"
#include "atmospherics/clouds.sh"

// Volumetric cloud pre-pass. Runs at half resolution into a premultiplied (rgb, transmittance)
// target and accumulates against a reprojected history (camera rotation + wind). The sky pass
// composites the result over the dome at far-depth pixels.
//
// MRT: target 0 = (scattered radiance, transmittance), target 1 = per-pixel history sample
// count / CLOUD_VOL_MAX_ACCUM. The count is per pixel so a freshly disoccluded pixel is not
// trusted as a converged history.

uniform vec4 u_parameters;
uniform vec4 u_sunDirection;
uniform vec4 u_sunLuminance;
uniform vec4 u_skyLuminance;
uniform vec4 u_cloudParams;
uniform vec4 u_cloudParams2;
uniform vec4 u_cloudParams3;
uniform vec4 u_cloudParams4;
uniform vec4 u_cloudFrame;
uniform mat4 u_prevViewProj;

#define u_exposition u_parameters.z

#define u_cloud_coverage        u_cloudParams.x
#define u_cloud_base_altitude   u_cloudParams.y
#define u_cloud_thickness       u_cloudParams.z
#define u_cloud_density         u_cloudParams.w

#define u_cloud_shadow_strength u_cloudParams2.x
#define u_cloud_inv_size        u_cloudParams2.y
#define u_cloud_softness        u_cloudParams2.z

#define u_cloud_detail_erode    u_cloudParams3.x
#define u_cloud_macro_variation u_cloudParams3.y
#define u_cloud_wind_offset     u_cloudParams3.zw

// x = jitter frame index (wrapped on the CPU), y = history valid (0/1),
// zw = wind offset advanced since the previous frame (noise units).
#define u_frame_index           u_cloudFrame.x
#define u_history_valid         u_cloudFrame.y
#define u_wind_delta            u_cloudFrame.zw

SAMPLER3D(s_cloudNoise, 0);
SAMPLER2D(s_cloudHistory, 1);
SAMPLER2D(s_cloudHistoryConf, 2);
SAMPLER2D(s_cloudNoise2D, 3);

// View march: the step size targets thickness / STEPS_MIN at the zenith, and the ray is cut at
// MAX_MARCH_THICKNESS layer thicknesses so slanted rays never get steps several layers long.
#define CLOUD_VOL_STEPS_MIN            16
#define CLOUD_VOL_STEPS_MAX            48
#define CLOUD_VOL_MAX_MARCH_THICKNESS  6.0
// Light march toward the sun: exponentially spaced samples over LIGHT_DISTANCE thicknesses
// (capped at the layer exit); the nearest LIGHT_DETAIL_STEPS use the eroded density so the
// visible bulges shadow each other.
#define CLOUD_VOL_LIGHT_STEPS          5
#define CLOUD_VOL_LIGHT_DETAIL_STEPS   2
#define CLOUD_VOL_LIGHT_DISTANCE       0.6
#define CLOUD_VOL_MAX_ACCUM            16.0
#define CLOUD_VOL_DENSITY_EPS          0.001
#define CLOUD_VOL_TRANSMITTANCE_EXIT   0.01

vec2 world_to_prev_uv(vec3 ws_pos, out bool o_valid)
{
    vec4 prev_clip4 = mul(u_prevViewProj, vec4(ws_pos, 1.0));
    o_valid = prev_clip4.w > 0.0;
    vec3 prev_clip = prev_clip4.xyz / max(prev_clip4.w, 1e-6);
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

vec3 cloud_sample_pos(vec3 world_pos)
{
    vec3 sp = world_pos * u_cloud_inv_size;
    sp.xz += u_cloud_wind_offset;
    return sp;
}

// Base shape (no detail): used by the far light-march samples, and as the first stage of the
// full sample.
float sample_cloud_shape(vec3 world_pos, out float o_height_fraction, out vec3 o_sp)
{
    o_height_fraction = saturate((world_pos.y - u_cloud_base_altitude) / u_cloud_thickness);
    o_sp = vec3_splat(0.0);
    float h_grad = cloud_height_gradient(o_height_fraction);
    if(h_grad < CLOUD_VOL_DENSITY_EPS)
    {
        return 0.0;
    }
    vec3 sp = cloud_sample_pos(world_pos);
    o_sp = sp;
    float macro = texture2D(s_cloudNoise2D, cloud_macro_uv(sp.xz)).r;
    float base_noise = texture3D(s_cloudNoise, sp / CLOUD_NOISE_PERIOD).r;
    float threshold = cloud_threshold(u_cloud_coverage, macro, u_cloud_macro_variation, o_height_fraction);
    return cloud_shape_mask(base_noise, threshold, u_cloud_softness) * h_grad;
}

// Full normalized density [0,1]: shape eroded by the Worley detail octaves.
float sample_cloud_density(vec3 world_pos, out float o_height_fraction)
{
    vec3 sp;
    float density = sample_cloud_shape(world_pos, o_height_fraction, sp);
    if(density < CLOUD_VOL_DENSITY_EPS)
    {
        return 0.0;
    }
    vec3 detail_sp = sp * CLOUD_DETAIL_SCALE + CLOUD_DETAIL_OFFSET;
    vec3 worley_detail = texture3D(s_cloudNoise, detail_sp / CLOUD_NOISE_PERIOD).gba;
    vec3 worley_fine = texture3D(s_cloudNoise, (detail_sp * CLOUD_DETAIL2_SCALE + CLOUD_DETAIL2_OFFSET) / CLOUD_NOISE_PERIOD).gba;
    float detail = cloud_detail_value(worley_detail, worley_fine);
    return cloud_erode(density, detail, u_cloud_detail_erode, o_height_fraction);
}

// Optical depth toward the sun: exponentially spaced samples over a distance relative to the
// layer thickness, scaled by the view extinction and the shadow-strength fraction.
float light_march_optical_depth(vec3 pos, vec3 light_dir, float jitter)
{
    float dist_to_top = (u_cloud_base_altitude + u_cloud_thickness - pos.y) / max(light_dir.y, 1e-3);
    float march_dist = min(dist_to_top, u_cloud_thickness * CLOUD_VOL_LIGHT_DISTANCE);
    // Step sizes s0 * 2^i sum to march_dist.
    float step_size = march_dist / float((1 << CLOUD_VOL_LIGHT_STEPS) - 1);
    float t = 0.0;
    float optical_depth = 0.0;
    for(int j = 0; j < CLOUD_VOL_LIGHT_STEPS; j++)
    {
        float sample_t = t + step_size * jitter;
        float h;
        vec3 sp;
        vec3 sample_pos = pos + light_dir * sample_t;
        float density;
        if(j < CLOUD_VOL_LIGHT_DETAIL_STEPS)
        {
            density = sample_cloud_density(sample_pos, h);
        }
        else
        {
            density = sample_cloud_shape(sample_pos, h, sp);
        }
        optical_depth += density * step_size;
        t += step_size;
        step_size *= 2.0;
    }
    return optical_depth * CLOUD_BASE_EXTINCTION * u_cloud_density * u_cloud_shadow_strength;
}

// 5-fetch Catmull-Rom history reconstruction (same form as the TAA resolve): bilinear
// resampling of a moving history low-passes it every frame.
vec4 sample_history(vec2 uv, vec2 texel_size)
{
    vec2 sample_pos = uv / texel_size;
    vec2 tex_pos1 = floor(sample_pos - 0.5) + 0.5;
    vec2 f = sample_pos - tex_pos1;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / w12;
    vec2 tex_pos0 = (tex_pos1 - vec2_splat(1.0)) * texel_size;
    vec2 tex_pos3 = (tex_pos1 + vec2_splat(2.0)) * texel_size;
    vec2 tex_pos12 = (tex_pos1 + offset12) * texel_size;
    vec4 result =
        texture2DLod(s_cloudHistory, vec2(tex_pos12.x, tex_pos0.y), 0.0) * (w12.x * w0.y) +
        texture2DLod(s_cloudHistory, vec2(tex_pos0.x, tex_pos12.y), 0.0) * (w0.x * w12.y) +
        texture2DLod(s_cloudHistory, vec2(tex_pos12.x, tex_pos12.y), 0.0) * (w12.x * w12.y) +
        texture2DLod(s_cloudHistory, vec2(tex_pos3.x, tex_pos12.y), 0.0) * (w3.x * w12.y) +
        texture2DLod(s_cloudHistory, vec2(tex_pos12.x, tex_pos3.y), 0.0) * (w12.x * w3.y);
    float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    result /= weight;
    return vec4(max(result.rgb, vec3_splat(0.0)), saturate(result.a));
}

void main()
{
    vec3 rd = normalize(v_viewDir);
    vec3 light_dir = normalize(u_sunDirection.xyz);

    vec4 new_cloud = vec4(0.0, 0.0, 0.0, 1.0);
    if(rd.y < CLOUD_MIN_ELEVATION)
    {
        gl_FragData[0] = new_cloud;
        gl_FragData[1] = vec4_splat(0.0);
        return;
    }

    // The layer sits above the camera (camera-relative rendering): both planes are in front.
    float t_min = u_cloud_base_altitude / rd.y;
    float t_max = (u_cloud_base_altitude + u_cloud_thickness) / rd.y;
    float ray_length = min(t_max - t_min, u_cloud_thickness * CLOUD_VOL_MAX_MARCH_THICKNESS);

    float target_step = u_cloud_thickness / float(CLOUD_VOL_STEPS_MIN);
    int steps = clamp(int(ceil(ray_length / target_step)), CLOUD_VOL_STEPS_MIN, CLOUD_VOL_STEPS_MAX);
    float step_size = ray_length / float(steps);

    float ign = cloud_interleaved_gradient_noise(gl_FragCoord.xy);
    float jitter = fract(ign + u_frame_index * 0.6180339887);
    float light_jitter = fract(jitter + 0.5);

    float cos_theta = dot(rd, light_dir);
    vec3 sun_radiance = cloud_sun_radiance(u_sunLuminance.xyz, u_exposition);

    float transmittance = 1.0;
    vec3 accum_light = vec3_splat(0.0);

    for(int i = 0; i < steps; i++)
    {
        float t = t_min + (float(i) + jitter) * step_size;
        vec3 sample_pos = rd * t;

        float height_fraction;
        float density = sample_cloud_density(sample_pos, height_fraction);
        if(density < CLOUD_VOL_DENSITY_EPS)
        {
            continue;
        }

        float extinction = density * CLOUD_BASE_EXTINCTION * u_cloud_density;
        float sample_transmittance = exp(-extinction * step_size);

        float od_sun = light_march_optical_depth(sample_pos, light_dir, light_jitter);
        vec3 lit_color = sun_radiance * cloud_sun_scatter(od_sun, cos_theta) +
                         cloud_ambient_radiance(u_skyLuminance.xyz, u_exposition, height_fraction);

        // Albedo 1: the in-scattered energy over the step integrates to lit * (1 - T_step).
        accum_light += lit_color * (1.0 - sample_transmittance) * transmittance;
        transmittance *= sample_transmittance;

        if(transmittance < CLOUD_VOL_TRANSMITTANCE_EXIT)
        {
            break;
        }
    }

    // Aerial perspective toward the marched segment's midpoint: distant clouds fade into the
    // sky behind them (the composite shows the dome through the raised transmittance).
    float aerial = cloud_aerial_transmittance(t_min + ray_length * 0.5, u_cloud_base_altitude);
    accum_light *= aerial;
    transmittance = mix(1.0, transmittance, aerial);

    new_cloud = vec4(accum_light, transmittance);

    // History reprojection: the feature now at P sat at P + wind_delta / inv_size last frame
    // (the noise field is offset by the wind); the previous camera had the same origin
    // (camera-relative), so only the rotation and the wind move it on screen.
    float cloud_mid_alt = u_cloud_base_altitude + u_cloud_thickness * 0.5;
    float t_hit = cloud_mid_alt / rd.y;
    vec2 wind_per_frame = u_wind_delta / u_cloud_inv_size;
    vec3 prev_dir = rd * t_hit + vec3(wind_per_frame.x, 0.0, wind_per_frame.y);
    bool prev_in_front;
    vec2 prev_uv = world_to_prev_uv(prev_dir, prev_in_front);

    bool history_ok = u_history_valid > 0.5 && prev_in_front &&
                      all(greaterThanEqual(prev_uv, vec2_splat(0.0))) &&
                      all(lessThanEqual(prev_uv, vec2_splat(1.0)));

    vec4 history = new_cloud;
    float history_count = 0.0;
    if(history_ok)
    {
        vec2 texel = u_viewTexel.xy;
        vec2 hist_uv = clamp(prev_uv, texel * 0.5, vec2_splat(1.0) - texel * 0.5);
        history = sample_history(hist_uv, texel);
        history_count = texture2DLod(s_cloudHistoryConf, hist_uv, 0.0).r * CLOUD_VOL_MAX_ACCUM;
    }

    vec4 blended = (history * history_count + new_cloud) / (history_count + 1.0);
    float new_count = min(history_count + 1.0, CLOUD_VOL_MAX_ACCUM);

    gl_FragData[0] = blended;
    gl_FragData[1] = vec4(new_count / CLOUD_VOL_MAX_ACCUM, 0.0, 0.0, 0.0);
}
