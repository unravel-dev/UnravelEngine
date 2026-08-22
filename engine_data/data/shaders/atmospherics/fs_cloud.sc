$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"
#include "atmospherics/clouds.sh"

// Volumetric cloud pre-pass. Runs at half resolution into a premultiplied (rgb, transmittance)
// target and accumulates against a reprojected history (camera rotation, camera translation
// and wind). fs_cloud_composite.sc blends the result over the whole frame (sky and geometry)
// with a depth-aware upsample.
//
// The layer is a spherical shell around the planet centre (clouds.sh): the camera may be
// below, inside or above it, rays are clipped to the shell and to the scene depth, and the
// horizon curves away instead of stretching to infinity.
//
// MRT: target 0 = (scattered radiance, transmittance), target 1 = (history sample count /
// CLOUD_VOL_MAX_ACCUM, scene distance in km at this texel). The count is per pixel so a
// freshly disoccluded pixel is not trusted as a converged history; the distance drives the
// depth-aware upsample in the composite.

uniform vec4 u_parameters;
uniform vec4 u_sunDirection;
uniform vec4 u_sunLuminance;
uniform vec4 u_skyLuminance;
uniform vec4 u_cloudFrame;
uniform vec4 u_cloudHistory;
uniform mat4 u_prevViewProj;

#define u_exposition u_parameters.z

// x = jitter frame index (wrapped on the CPU), y = history valid (0/1).
#define u_frame_index           u_cloudFrame.x
#define u_history_valid         u_cloudFrame.y
// xyz = offset that moves a feature seen now to where the previous frame saw it (camera
// relative): wind advance in world units plus the camera translation since the last frame.
#define u_history_offset        u_cloudHistory.xyz

SAMPLER3D(s_cloudNoise, 0);
SAMPLER2D(s_cloudHistory, 1);
SAMPLER2D(s_cloudHistoryAux, 2);
SAMPLER2D(s_cloudNoise2D, 3);
SAMPLER2D(s_depth, 4);

// View march: the step size targets thickness / STEPS_MIN, and the ray is cut at
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
#define CLOUD_VOL_TRANSMITTANCE_EXIT   0.01
// Interleaved update: each half-res pixel marches once every INTERLEAVE^2 frames (its 2x2 cell
// position against the frame index) and carries its reprojected history otherwise; pixels with
// no usable history march regardless.
#define CLOUD_VOL_INTERLEAVE           2
// Deep inside a cloud (view transmittance below this) the sample barely shows: reuse the
// previous light march instead of marching again.
#define CLOUD_VOL_LIGHT_REUSE_TRANSMITTANCE 0.25
// Reprojection deltas below this (in history texels) read the pixel itself: an exact copy.
#define CLOUD_VOL_REPROJECT_SNAP_PX    0.05
// Scene distance stored in the aux target, in km (fits RG16F for any scene). Sky pixels
// (depth at the far plane) count as CLOUD_VOL_SKY_DISTANCE: the far clip is far closer than
// the layer, and must not clip the march.
#define CLOUD_VOL_DISTANCE_SCALE       0.001
#define CLOUD_VOL_SKY_DISTANCE         1.0e7
#define CLOUD_VOL_SKY_DEPTH            0.99999

vec2 world_to_prev_uv(vec3 rel_pos, out bool o_valid)
{
    vec4 prev_clip4 = mul(u_prevViewProj, vec4(rel_pos, 1.0));
    o_valid = prev_clip4.w > 0.0;
    vec3 prev_clip = prev_clip4.xyz / max(prev_clip4.w, 1e-6);
    prev_clip = clipTransform(prev_clip);
    return prev_clip.xy * 0.5 + 0.5;
}

// Optical depth toward the sun from a camera-relative position: exponentially spaced samples
// over a distance relative to the layer thickness, scaled by the view extinction and the
// shadow-strength fraction.
float light_march_optical_depth(vec3 rel_pos, vec3 light_dir, float jitter)
{
    vec3 planet_center = cloud_planet_center_rel();
    float layer_top_radius = CLOUD_PLANET_RADIUS + u_cloud_base_altitude + u_cloud_thickness;
    vec2 top_hit = cloud_ray_sphere(rel_pos, light_dir, planet_center, layer_top_radius);
    float dist_to_top = top_hit.y > 0.0 ? top_hit.y : u_cloud_thickness;
    float march_dist = min(dist_to_top, u_cloud_thickness * CLOUD_VOL_LIGHT_DISTANCE);
    // Step sizes s0 * 2^i sum to march_dist.
    float step_size = march_dist / float((1 << CLOUD_VOL_LIGHT_STEPS) - 1);
    float t = 0.0;
    float optical_depth = 0.0;
    for(int j = 0; j < CLOUD_VOL_LIGHT_STEPS; j++)
    {
        float sample_t = t + step_size * jitter;
        vec3 sample_rel = rel_pos + light_dir * sample_t;
        vec3 sample_world = u_cloud_camera_pos + sample_rel;
        float h = cloud_height_fraction_rel(sample_rel);
        vec3 sp;
        float density;
        if(j < CLOUD_VOL_LIGHT_DETAIL_STEPS)
        {
            density = cloud_sample_density(s_cloudNoise, s_cloudNoise2D, sample_world, h);
        }
        else
        {
            density = cloud_sample_shape(s_cloudNoise, s_cloudNoise2D, sample_world, h, sp);
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
    // Exact per-pixel ray from the clip position (the vertex-interpolated v_viewDir is off by a
    // sub-pixel amount, enough to make a static camera reproject onto neighbouring texels and
    // blur the history copies). The view is camera-relative, so the far-plane point is the
    // direction.
    vec4 far_point = mul(u_invViewProj, vec4(v_clipPos, 1.0, 1.0));
    vec3 rd = normalize(far_point.xyz / far_point.w);
    vec3 light_dir = normalize(u_sunDirection.xyz);

    // Scene distance at this pixel (sky pixels sit at the far plane).
    vec2 uv = clipToUv(v_clipPos * 0.5 + 0.5);
    float depth = texture2DLod(s_depth, uv, 0.0).r;
    float t_depth = depth >= CLOUD_VOL_SKY_DEPTH ? CLOUD_VOL_SKY_DISTANCE : length(computeViewSpacePosition(uv, depth));
    float depth_km = t_depth * CLOUD_VOL_DISTANCE_SCALE;

    vec4 new_cloud = vec4(0.0, 0.0, 0.0, 1.0);

    // Ray / shell interval, clipped to the scene depth.
    float t_start;
    float t_end;
    bool in_shell = cloud_shell_interval(rd, t_start, t_end);
    t_end = min(t_end, t_depth);
    if(!in_shell || t_end <= t_start)
    {
        gl_FragData[0] = new_cloud;
        gl_FragData[1] = vec4(0.0, depth_km, 0.0, 0.0);
        return;
    }

    float ray_length = min(t_end - t_start, u_cloud_thickness * CLOUD_VOL_MAX_MARCH_THICKNESS);
    float t_mid = t_start + ray_length * 0.5;

    // History reprojection: the feature now at camera-relative P sat at P + history_offset
    // last frame (wind advance + camera translation); the previous view-projection is
    // camera-relative, so only that offset and the rotation move it on screen.
    vec3 prev_dir = rd * t_mid + u_history_offset;
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
        // Static camera and no wind: the reprojection lands on this texel; copy it exactly
        // instead of re-filtering (repeated resampling would blur the interleaved copies).
        vec2 delta_px = (prev_uv - uv) / texel;
        vec2 hist_uv = all(lessThan(abs(delta_px), vec2_splat(CLOUD_VOL_REPROJECT_SNAP_PX))) ? uv : prev_uv;
        hist_uv = clamp(hist_uv, texel * 0.5, vec2_splat(1.0) - texel * 0.5);
        history = sample_history(hist_uv, texel);
        history_count = texture2DLod(s_cloudHistoryAux, hist_uv, 0.0).r * CLOUD_VOL_MAX_ACCUM;
    }

    // Interleaved update: only the cell position of this frame marches; the rest carry history.
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    int cell = (pixel.x % CLOUD_VOL_INTERLEAVE) + (pixel.y % CLOUD_VOL_INTERLEAVE) * CLOUD_VOL_INTERLEAVE;
    int active_cell = int(mod(u_frame_index, float(CLOUD_VOL_INTERLEAVE * CLOUD_VOL_INTERLEAVE)));
    bool march = cell == active_cell || !history_ok || history_count < 0.5;
    if(!march)
    {
        gl_FragData[0] = history;
        gl_FragData[1] = vec4(history_count / CLOUD_VOL_MAX_ACCUM, depth_km, 0.0, 0.0);
        return;
    }

    float target_step = u_cloud_thickness / float(CLOUD_VOL_STEPS_MIN);
    int steps = clamp(int(ceil(ray_length / target_step)), CLOUD_VOL_STEPS_MIN, CLOUD_VOL_STEPS_MAX);
    float step_size = ray_length / float(steps);

    // A pixel marches once per INTERLEAVE^2 frames, so the golden-ratio sequence must advance
    // per march, not per frame (per frame it degenerates: fract(4 * phi) ~ 0.47 alternates
    // between two offsets and the steps never average out).
    float march_index = floor(u_frame_index / float(CLOUD_VOL_INTERLEAVE * CLOUD_VOL_INTERLEAVE));
    float ign = cloud_interleaved_gradient_noise(gl_FragCoord.xy);
    float jitter = fract(ign + march_index * 0.6180339887);
    float light_jitter = fract(jitter + 0.5);

    float cos_theta = dot(rd, light_dir);
    vec3 sun_radiance = cloud_sun_radiance(u_sunLuminance.xyz, u_exposition);

    float transmittance = 1.0;
    vec3 accum_light = vec3_splat(0.0);
    float od_sun = 0.0;
    bool has_od_sun = false;

    for(int i = 0; i < steps; i++)
    {
        float t = t_start + (float(i) + jitter) * step_size;
        vec3 sample_rel = rd * t;
        vec3 sample_world = u_cloud_camera_pos + sample_rel;
        float height_fraction = cloud_height_fraction_rel(sample_rel);

        float density = cloud_sample_density(s_cloudNoise, s_cloudNoise2D, sample_world, height_fraction);
        if(density < CLOUD_DENSITY_EPS)
        {
            continue;
        }

        float extinction = density * CLOUD_BASE_EXTINCTION * u_cloud_density;
        float sample_transmittance = exp(-extinction * step_size);

        if(!has_od_sun || transmittance > CLOUD_VOL_LIGHT_REUSE_TRANSMITTANCE)
        {
            od_sun = light_march_optical_depth(sample_rel, light_dir, light_jitter);
            has_od_sun = true;
        }
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

    // Aerial perspective toward the marched segment midpoint: distant clouds fade into the
    // sky behind them (the composite shows the scene through the raised transmittance).
    float aerial = cloud_aerial_transmittance(t_mid, u_cloud_base_altitude);
    accum_light *= aerial;
    transmittance = mix(1.0, transmittance, aerial);

    new_cloud = vec4(accum_light, transmittance);

    vec4 blended = (history * history_count + new_cloud) / (history_count + 1.0);
    float new_count = min(history_count + 1.0, CLOUD_VOL_MAX_ACCUM);

    gl_FragData[0] = blended;
    gl_FragData[1] = vec4(new_count / CLOUD_VOL_MAX_ACCUM, depth_km, 0.0, 0.0);
}
