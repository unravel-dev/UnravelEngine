$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"

uniform vec4 u_parameters;
uniform vec4 u_sunDirection;
uniform vec4 u_sunLuminance;
uniform vec4 u_skyLuminance;
uniform vec4 u_cloudParams;
uniform vec4 u_cloudParams2;
uniform vec4 u_cloudFrame;
uniform mat4 u_prevViewProj;

#define u_exposition u_parameters.z
#define u_time u_parameters.w

#define u_cloud_coverage       u_cloudParams.x
#define u_cloud_base_altitude  u_cloudParams.y
#define u_cloud_time           u_cloudParams.z
#define u_cloud_density        u_cloudParams.w

#define u_cloud_absorption       u_cloudParams2.x
#define u_cloud_light_absorption u_cloudParams2.y
#define u_cloud_top_altitude     u_cloudParams2.z

SAMPLER3D(s_cloudNoise, 0);
SAMPLER2D(s_cloudHistory, 1);

#define CLOUD_VOL_BASE_DENSITY   0.04
#define CLOUD_VOL_WIND_SPEED     0.3
#define CLOUD_VOL_STEPS_MAX      32
#define CLOUD_VOL_STEPS_MIN      8
#define CLOUD_VOL_LIGHT_STEPS    1
#define CLOUD_VOL_HG_FORWARD     0.45
#define CLOUD_VOL_HG_BACK       -0.15
#define CLOUD_VOL_HG_BLEND       0.65
#define CLOUD_VOL_AMBIENT        0.22
#define CLOUD_VOL_SUN_INTENSITY  24.0
#define CLOUD_VOL_HORIZON_FADE   0.05
#define CLOUD_VOL_HORIZON_SCALE  0.05
#define CLOUD_VOL_UV_SCALE       0.00008
#define CLOUD_VOL_MAX_ACCUM      16.0

#define CLOUD_PI 3.14159265
#define CLOUD_NOISE_PERIOD 6.0

// Interleaved Gradient Noise (Jimenez 2014, "Next Generation Post Processing in Call of Duty").
// Produces high-frequency spatially-uniform noise ideal for temporal accumulation.
float InterleavedGradientNoise(vec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

// PCG hash for stable per-pixel spatial offset (breaks up banding at edges).
uvec3 Rand3DPCG16(ivec3 p)
{
    uvec3 v = uvec3(p);
    v = v * 1664525u + 1013904223u;
    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    v.x += v.y*v.z;
    v.y += v.z*v.x;
    v.z += v.x*v.y;
    return v >> 16u;
}

vec2 WorldToScreenPrevious(vec3 ws_pos)
{
    vec4 prev_clip4 = mul(u_prevViewProj, vec4(ws_pos, 1.0));
    vec3 prev_clip = prev_clip4.xyz / prev_clip4.w;
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

vec3 ComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

vec2 ComputePreviousFrameUV(vec2 uv, float z)
{
    vec3 vs_pos = ComputeViewspacePosition(uv, z);
    vec4 ws_pos = mul(u_invView, vec4(vs_pos, 1.0));
    return WorldToScreenPrevious(ws_pos.xyz);
}

float henyey_greenstein(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * CLOUD_PI * denom * sqrt(denom));
}

float dual_lobe_phase(float cos_theta)
{
    float hg_forward = henyey_greenstein(cos_theta, CLOUD_VOL_HG_FORWARD);
    float hg_back = henyey_greenstein(cos_theta, CLOUD_VOL_HG_BACK);
    return mix(hg_back, hg_forward, CLOUD_VOL_HG_BLEND);
}

float powder_effect(float density, float cos_theta)
{
    float powder = 1.0 - exp(-density * 2.0);
    float backlit = saturate(-cos_theta * 0.5 + 0.5);
    return mix(1.0, powder * 2.0, backlit * 0.3);
}

float height_gradient(float height_fraction)
{
    float bottom = smoothstep(0.0, 0.2, height_fraction);
    float top = smoothstep(1.0, 0.7, height_fraction);
    return bottom * top;
}

vec3 cloud_sample_pos(vec3 world_pos)
{
    vec3 sp = world_pos * CLOUD_VOL_UV_SCALE;
    sp.x += u_cloud_time * CLOUD_VOL_WIND_SPEED * 10.0;
    sp.z += u_cloud_time * CLOUD_VOL_WIND_SPEED * 4.0;
    return sp;
}

vec3 cloud_uvw(vec3 sp)
{
    return sp / CLOUD_NOISE_PERIOD;
}

float sample_cloud_density_full(vec3 world_pos)
{
    float layer_thickness = u_cloud_top_altitude - u_cloud_base_altitude;
    float height_fraction = saturate((world_pos.y - u_cloud_base_altitude) / layer_thickness);

    float h_grad = height_gradient(height_fraction);
    if(h_grad < 0.001) return 0.0;

    vec3 sp = cloud_sample_pos(world_pos);
    float base_noise = texture3D(s_cloudNoise, cloud_uvw(sp)).r;

    float threshold = 1.0 - u_cloud_coverage;
    float density = smoothstep(threshold, threshold + 0.4, base_noise);
    density *= h_grad;

    if(density < 0.001) return 0.0;
    if(density > 0.7) return density * CLOUD_VOL_BASE_DENSITY * u_cloud_density;

    vec3 detail_uvw = cloud_uvw(sp * 5.0 + vec3(17.3, 41.7, 23.1));
    vec4 dns = texture3D(s_cloudNoise, detail_uvw);
    float detail = dns.g * 0.625 + dns.b * 0.25 + dns.a * 0.125;
    float edge_factor = 1.0 - density * density;
    density = max(0.0, density - detail * 0.35 * edge_factor);

    return density * CLOUD_VOL_BASE_DENSITY * u_cloud_density;
}

float sample_cloud_density_cheap(vec3 world_pos)
{
    float layer_thickness = u_cloud_top_altitude - u_cloud_base_altitude;
    float height_fraction = saturate((world_pos.y - u_cloud_base_altitude) / layer_thickness);

    float h_grad = height_gradient(height_fraction);
    if(h_grad < 0.001) return 0.0;

    vec3 sp = cloud_sample_pos(world_pos);
    float pw = texture3D(s_cloudNoise, cloud_uvw(sp)).r;

    float threshold = 1.0 - u_cloud_coverage;
    float density = smoothstep(threshold, threshold + 0.35, pw);
    density *= h_grad * 0.5;

    return density * CLOUD_VOL_BASE_DENSITY * u_cloud_density;
}

float light_march(vec3 pos, vec3 light_dir)
{
    float dist_to_top = (u_cloud_top_altitude - pos.y) / max(light_dir.y, 0.001);
    dist_to_top = min(dist_to_top, 1500.0);
    float light_step = dist_to_top / float(CLOUD_VOL_LIGHT_STEPS);

    float shadow_density = 0.0;
    for(int j = 0; j < CLOUD_VOL_LIGHT_STEPS; j++)
    {
        pos += light_dir * light_step;
        shadow_density += sample_cloud_density_cheap(pos) * light_step;
    }

    float optical_depth = shadow_density * u_cloud_light_absorption;
    float beer = exp(-optical_depth);
    float ms = exp(-optical_depth * 0.2) * 0.35;
    return max(beer, ms);
}

void main()
{
    vec3 viewDir = normalize(v_viewDir);
    vec3 lightDir = normalize(u_sunDirection.xyz);

    if(viewDir.y < 0.01)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 rd = viewDir;

    float t_base = u_cloud_base_altitude / rd.y;
    float t_top = u_cloud_top_altitude / rd.y;
    float t_min = max(0.0, min(t_base, t_top));
    float t_max = max(t_base, t_top);

    if(t_min >= t_max || t_max < 0.0)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float ray_length = t_max - t_min;
    float layer_thickness = u_cloud_top_altitude - u_cloud_base_altitude;
    float target_step = layer_thickness / float(CLOUD_VOL_STEPS_MIN);
    int steps = clamp(int(ceil(ray_length / target_step)), CLOUD_VOL_STEPS_MIN, CLOUD_VOL_STEPS_MAX);
    float step_size = ray_length / float(steps);

    float ign = InterleavedGradientNoise(gl_FragCoord.xy);
    float jitter = fract(ign + u_cloudFrame.y * 0.6180339887);

    float transmittance = 1.0;
    vec3 accum_light = vec3_splat(0.0);

    float cos_theta = dot(rd, lightDir);
    float phase = dual_lobe_phase(cos_theta);


    float night_factor = saturate(-lightDir.y * 3.0 - 0.2);
    vec3 sun_color = saturate(u_sunLuminance.xyz) * (1.0 - night_factor * 0.9);
    vec3 ambient_color = u_skyLuminance.xyz * CLOUD_VOL_AMBIENT * (1.0 - night_factor * 0.8);

    for(int i = 0; i < steps; i++)
    {
        float t = t_min + (float(i) + jitter) * step_size;
        vec3 sample_pos = rd * t;

        float density = sample_cloud_density_full(sample_pos);
        if(density < 0.001) continue;

        float sample_density = density * step_size;

        float sun_atten = light_march(sample_pos, lightDir);
        float powder = powder_effect(sample_density, cos_theta);

        vec3 lit_color = sun_color * sun_atten * phase * CLOUD_VOL_SUN_INTENSITY * powder + ambient_color;

        float sample_transmittance = exp(-sample_density * u_cloud_absorption);
        accum_light += lit_color * (1.0 - sample_transmittance) * transmittance;
        transmittance *= sample_transmittance;

        if(transmittance < 0.01) break;
    }

    float horizon_factor = mix(CLOUD_VOL_HORIZON_SCALE, 1.0, smoothstep(CLOUD_VOL_HORIZON_FADE, 0.4, rd.y));
    accum_light *= horizon_factor;
    transmittance = mix(1.0, transmittance, horizon_factor);

    accum_light *= u_exposition * 8.0;

    vec4 new_cloud = vec4(accum_light, transmittance);

    float W = min(u_cloudFrame.y, CLOUD_VOL_MAX_ACCUM);

    if(W < 0.5)
    {
        gl_FragColor = new_cloud;
        return;
    }

    vec2 uv = clipToUv(v_clipPos * 0.5 + 0.5);

    // Wind + camera reprojection (for history lookup)
    float cloud_mid_alt = (u_cloud_base_altitude + u_cloud_top_altitude) * 0.5;
    float t_hit = cloud_mid_alt / max(viewDir.y, 0.01);

    float cloud_time_dt = u_cloudFrame.z;
    float inv_scale = 1.0 / CLOUD_VOL_UV_SCALE;
    vec3 wind_per_frame = vec3(
        cloud_time_dt * CLOUD_VOL_WIND_SPEED * 10.0 * inv_scale,
        0.0,
        cloud_time_dt * CLOUD_VOL_WIND_SPEED * 4.0 * inv_scale);

    vec3 prev_cloud_dir = normalize(viewDir * t_hit + wind_per_frame) * cloud_mid_alt;
    vec2 prev_uv = WorldToScreenPrevious(prev_cloud_dir);

    if(any(lessThan(prev_uv, vec2_splat(0.0))) ||
       any(greaterThan(prev_uv, vec2_splat(1.0))))
    {
        gl_FragColor = new_cloud;
        return;
    }

    vec4 history = texture2D(s_cloudHistory, prev_uv);

    gl_FragColor = (history * W + new_cloud) / (W + 1.0);
}
