$input v_texcoord0, v_weye_dir, v_screenPos

#include "../common.sh"

uniform vec4 u_parameters;
uniform vec4 u_kr_and_intensity;
uniform vec4 u_turbidity_parameters1;
uniform vec4 u_turbidity_parameters2;
uniform vec4 u_turbidity_parameters3;
uniform vec4 u_cloud_params; // x - coverage, y - altitude, z - accumulated time, w - density

#define u_light_direction u_parameters.xyz
#define u_time u_parameters.w

// u_kr, u_intensity
#define u_kr u_kr_and_intensity.xyz
#define u_intensity u_kr_and_intensity.w

// u_rayleigh_strength, u_mie_strength, u_mie_distribution, u_scatter_strength
#define u_rayleigh_strength u_turbidity_parameters1.x
#define u_mie_strength u_turbidity_parameters1.y
#define u_mie_distribution u_turbidity_parameters1.z
#define u_scatter_strength u_turbidity_parameters1.w


// u_rayleigh_brightness, u_mie_brightness, u_spot_brightness, u_spot_distance
#define u_rayleigh_brightness u_turbidity_parameters2.x
#define u_mie_brightness u_turbidity_parameters2.y
#define u_spot_brightness u_turbidity_parameters2.z
#define u_spot_distance u_turbidity_parameters2.w


//u_rayleigh_collection_power, u_mie_collection_power, unused, unused
#define u_rayleigh_collection_power u_turbidity_parameters3.x
#define u_mie_collection_power u_turbidity_parameters3.y

#define u_cloud_coverage u_cloud_params.x
#define u_cloud_altitude u_cloud_params.y
#define u_cloud_time u_cloud_params.z  // accumulated seconds, pre-scaled by speed on CPU
#define u_cloud_density u_cloud_params.w

// https://www.shadertoy.com/view/4ssXRX
// http://www.loopit.dk/banding_in_games.pdf
// http://www.dspguide.com/ch2/6.htm

//uniformly distributed, normalized rand, [0, 1)
float nrand(in vec2 n)
{
	return fract(sin(dot(n.xy, vec2(12.9898, 78.233)))* 43758.5453);
}

float n4rand_ss(in vec2 n)
{
	float nrnd0 = nrand( n + 0.07*fract( u_time ) );
	float nrnd1 = nrand( n + 0.11*fract( u_time + 0.573953 ) );
	return 0.23*sqrt(-log(nrnd0+0.00001))*cos(2.0*3.141592*nrnd1)+0.5;
}

// ----------------------------- Procedural noise -----------------------------

// Hash-based 2D noise (no texture needed)
float hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Smooth value noise
float value_noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    // Quintic interpolation for smooth derivatives
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = hash(i + vec2(0.0, 0.0));
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractional Brownian Motion - layered noise for cloud shapes
float fbm(vec2 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for(int i = 0; i < octaves; i++)
    {
        value += amplitude * value_noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// ----------------------------- Atmospheric functions -------------------------

float atmospheric_depth(vec3 pos, vec3 dir) 
{
	float a = dot(dir, dir);
	float b = 2.0f * dot(dir, pos);
	float c = dot(pos, pos) - 1.0f;
	float det = b * b - 4.0f * a * c;
	float detSqrt = sqrt(det);
	float q = (-b - detSqrt) / 2.0f;
	float t1 = c / q;
	return t1;
}

float phase(float alpha, float g)
{
	float a = 3.0f * (1.0f - g * g);
	float b = 2.0f * (2.0f + g * g);
	float c = 1.0f + alpha * alpha;
	float d = pow(1.0f + g * g - 2.0f * g * alpha, 1.5f);
	return (a / b) * (c / d);
}

float horizon_extinction(vec3 pos, vec3 dir, float radius)
{
	float u = dot(dir, -pos);
	if(u < 0.0f) 
	{
		return 1.0f;
	}
	vec3 near = pos + u * dir;
	if(length(near) < radius + 0.001f) 
	{
		return 0.0f;
	} 
	else 
	{
		vec3 v2 = normalize(near) * radius - pos;
		float diff = acos(dot(normalize(v2), dir));
		return smoothstep(0.0f, 1.0f, pow(diff * 2.0f, 3.0f));
	}
}

// No longer using the old absorb() function.
// Replaced with proper Beer-Lambert exp(-beta * distance) in the scattering loop.

// ----------------------------- Stars ----------------------------------------

// 2D hash returning vec2 (for star sub-cell position)
vec2 hash2(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 render_stars(vec3 sky_color, vec3 view_dir, vec3 sun_toward)
{
    // Stars only above the horizon
    if(view_dir.y < 0.0)
    {
        return sky_color;
    }

    // ---- Night visibility ----
    // sun_toward points FROM the scene TOWARD the sun.
    // sun_toward.y > 0 means sun is above horizon (daytime).
    // Stars fade in as the sun drops below the horizon.
    float night_factor = saturate(-sun_toward.y * 4.0 - 0.1);
    if(night_factor < 0.001)
    {
        return sky_color;
    }

    // ---- Sky sphere grid ----
    float phi = atan2(view_dir.z, view_dir.x);         // -PI to PI
    float theta = acos(clamp(view_dir.y, -1.0, 1.0));  // 0 to PI

    float grid_scale = 20.0;
    vec2 grid_uv = vec2(phi, theta) * grid_scale;
    vec2 cell_id = floor(grid_uv);
    vec2 cell_fract = fract(grid_uv);

    vec3 star_contribution = vec3_splat(0.0);

    for(int ox = -1; ox <= 1; ox++)
    {
        for(int oy = -1; oy <= 1; oy++)
        {
            vec2 neighbor = vec2(float(ox), float(oy));
            vec2 current_cell = cell_id + neighbor;

            float star_chance = hash(current_cell * 1.73);
            if(star_chance > 0.08)
            {
                continue;
            }

            vec2 star_pos = hash2(current_cell * 2.41) * 0.8 + 0.1;
            vec2 diff = (neighbor + star_pos) - cell_fract;

            float dist_sq = dot(diff, diff);
            float star_radius = 0.06;
            float star_point = exp(-dist_sq / (star_radius * star_radius));

            float mag_rand = hash(current_cell * 3.17);
            float brightness = pow(mag_rand, 5.0) * 0.8 + 0.05;

            if(mag_rand > 0.97)
            {
                brightness = 1.2;
                star_radius = 0.08;
                star_point = exp(-dist_sq / (star_radius * star_radius));
            }

            float color_rand = hash(current_cell * 4.93);
            vec3 star_color;
            if(color_rand < 0.15)
            {
                star_color = vec3(0.7, 0.8, 1.0);
            }
            else if(color_rand < 0.55)
            {
                star_color = vec3(1.0, 1.0, 1.0);
            }
            else if(color_rand < 0.80)
            {
                star_color = vec3(1.0, 0.95, 0.8);
            }
            else if(color_rand < 0.92)
            {
                star_color = vec3(1.0, 0.85, 0.6);
            }
            else
            {
                star_color = vec3(1.0, 0.7, 0.5);
            }

            float twinkle = value_noise(current_cell * 0.5 + vec2(u_cloud_time * 0.8, u_cloud_time * 0.6));
            twinkle = mix(0.7, 1.0, twinkle);

            star_contribution += star_color * star_point * brightness * twinkle;
        }
    }

    // Atmospheric extinction: stars near horizon are dimmer
    float extinction = smoothstep(0.0, 0.25, view_dir.y);

    // Fixed HDR intensity (no exposition dependency for consistent night visibility)
    star_contribution *= night_factor * extinction * 0.6;

    return sky_color + star_contribution;
}

// ----------------------------- Cloud rendering ------------------------------

vec3 render_clouds(vec3 sky_color, vec3 eye_dir, vec3 sun_toward, float sun_height_01)
{
    // Only render clouds above the horizon
    if(eye_dir.y < 0.01)
    {
        return sky_color;
    }

    // ---- Cloud UV with dome curvature ----
    float dome_eps = 0.2;
    float y_dome = sqrt(eye_dir.y * eye_dir.y + dome_eps * dome_eps);
    float t = u_cloud_altitude / y_dome;
    vec2 cloud_uv = eye_dir.xz * t;

    // Animate with wind
    float wind_time = u_cloud_time * 0.4;
    cloud_uv += vec2(wind_time * 12.0, wind_time * 5.0);

    // Scale UVs for cloud size
    vec2 uv = cloud_uv * 0.0004;

    // Multi-octave noise for cloud shape
    float noise = fbm(uv, 5);

    // Shape the clouds: coverage controls the threshold
    float threshold = 1.0 - u_cloud_coverage;
    float cloud = smoothstep(threshold, threshold + 0.25, noise);
    cloud *= u_cloud_density;

    // Fade clouds near the horizon to simulate atmospheric haze
    float horizon_fade = smoothstep(0.01, 0.15, eye_dir.y);
    cloud *= horizon_fade;

    // ---- Cloud lighting ----
    // sun_toward points FROM scene TOWARD the sun
    float ndotl = saturate(sun_toward.y);

    // Directional shading: offset UV toward the sun to fake light penetration
    vec2 light_offset = sun_toward.xz * 0.002;
    float noise_lit = fbm(uv + light_offset, 3);
    float light_diff = saturate(noise - noise_lit);

    // Dark side (shadow) and lit side colors
    vec3 cloud_dark = vec3(0.45, 0.5, 0.55);
    vec3 cloud_lit = vec3(1.0, 1.0, 1.0);

    // At sunset, tint lit side with warm sun color
    vec3 sunset_tint = mix(vec3(1.0, 0.7, 0.45), vec3(1.0, 1.0, 1.0), sun_height_01);
    cloud_lit *= sunset_tint;

    // Also warm up the dark side slightly at sunset (scattered light)
    cloud_dark = mix(cloud_dark * vec3(1.0, 0.75, 0.6), cloud_dark, sun_height_01);

    // Combine lit and dark based on light difference and overall sun brightness
    float brightness = mix(0.3, 1.0, ndotl);
    vec3 cloud_color = mix(cloud_dark, cloud_lit, saturate(light_diff * 2.5 + 0.3)) * brightness;

    // Silver lining: bright edge when looking near the sun through thin cloud edges
    float view_sun = saturate(dot(eye_dir, sun_toward));
    float silver = pow(view_sun, 8.0) * 0.3 * (1.0 - cloud * 0.5);
    cloud_color += vec3_splat(silver);

    // Scale cloud color to match the scene's HDR range.
    // Derive approximate scene brightness from sun height so clouds
    // stay consistent with the atmospheric scattering luminance.
    float scene_brightness = mix(0.4, 2.5, sun_height_01);
    cloud_color *= scene_brightness;

    // Blend clouds over the sky
    return mix(sky_color, cloud_color, saturate(cloud));
}

// ----------------------------- Main -----------------------------------------

void main()
{
	const vec3 u_ground_color = vec3(1.369, 0.349, 0.341);
	const float u_surface_height = 0.99;
	const int u_step_count = 8;

	vec3 eye_dir = normalize(v_weye_dir);
	vec3 eye_pos = vec3(0.0, u_surface_height, 0.0);

	// sun_toward: direction FROM the scene TOWARD the sun
	vec3 sun_toward = -normalize(u_light_direction);

	// Phase functions
	float cos_angle = clamp(dot(eye_dir, sun_toward), 0.0, 1.0);
	float rayleigh_phase_val = phase(cos_angle, -0.01) * u_rayleigh_brightness;
	float mie_phase_val = phase(cos_angle, u_mie_distribution) * u_mie_brightness;
	float spot_val = smoothstep(0.0, u_spot_distance, phase(cos_angle, 0.9995)) * u_spot_brightness;

	float eye_depth = atmospheric_depth(eye_pos, eye_dir);
	float step_len = eye_depth / float(u_step_count);
	float eye_ext = horizon_extinction(eye_pos, eye_dir, u_surface_height - 0.05);

	// ---- Physically-based scattering coefficients (Beer-Lambert) ----
	//
	// u_kr contains Rayleigh wavelength ratios following the lambda^-4 law.
	// We scale them into proper extinction coefficients for the unit-sphere
	// atmosphere (where zenith path length ≈ 0.01).
	//
	// u_scatter_strength controls overall atmospheric density (varies with
	// turbidity from ~0.078 for clear to ~0.15 for hazy).
	//
	// With Beer-Lambert, attenuation = exp(-beta * distance), which naturally
	// produces correct sunset colors: blue is scattered away first, then green,
	// leaving red — without any post-process hacks.

	float density = u_scatter_strength / 0.078; // normalized to clear-sky baseline
	vec3 beta_R = u_kr * 100.0 * density;       // Rayleigh scattering coefficient
	float beta_M_val = u_mie_strength * 15.0;   // Mie coefficient (wavelength-independent)
	vec3 beta_M = vec3_splat(beta_M_val);

	// Extinction uses a slightly boosted green channel to prevent the horizon
	// green tint. With only 3 wavelengths (RGB), green sits in a mathematical
	// sweet spot: it scatters enough to accumulate but survives long view paths
	// better than blue. In a continuous spectrum this averages out to white;
	// with discrete RGB we must nudge green extinction up to compensate.
	vec3 beta_ext = beta_R * vec3(1.0, 1.15, 1.0) + beta_M;

	vec3 rayleigh_sum = vec3_splat(0.0);
	vec3 mie_sum = vec3_splat(0.0);
	vec3 mie_raw_sum = vec3_splat(0.0); // without horizon shadow, for ground ambient

	for(int i = 0; i < u_step_count; ++i)
	{
		// Mid-point sampling: avoids the degenerate dist=0 case
		float sample_dist = step_len * (float(i) + 0.5);
		vec3 pos = eye_pos + eye_dir * sample_dist;

		// Earth shadow at this sample point
		float h_ext = horizon_extinction(pos, sun_toward, u_surface_height - 0.175);

		// Atmospheric depth from sun to this sample
		float sun_depth = atmospheric_depth(pos, sun_toward);

		// Beer-Lambert: sunlight attenuation (sun → sample)
		// At sunset sun_depth is large → blue/green are strongly absorbed → red survives
		vec3 sun_atten = exp(-beta_ext * sun_depth);

		// Beer-Lambert: scattered light attenuation (sample → viewer)
		vec3 view_atten = exp(-beta_ext * sample_dist);

		// Sunlight arriving at this sample point
		vec3 L_sun = vec3_splat(u_intensity) * sun_atten * h_ext;
		vec3 L_sun_raw = vec3_splat(u_intensity) * sun_atten; // no earth shadow

		// Accumulate Rayleigh scattering (proportional to beta_R per wavelength)
		rayleigh_sum += L_sun * beta_R * view_atten * step_len;

		// Accumulate Mie scattering (wavelength-independent)
		mie_sum += L_sun * beta_M * view_atten * step_len;

		// Mie without earth shadow (for ground fog ambient)
		mie_raw_sum += L_sun_raw * beta_M * view_atten * step_len;
	}

	// At noon the single-scattering model overestimates brightness (in reality
	// multi-scattering redistributes energy). At sunset it is more accurate.
	// Vary the scale with sun height to keep noon dimmer and sunset vivid.
	float sun_height = saturate(sun_toward.y);
	float scatter_scale = mix(1.0, 0.45, sun_height); // 1.0 at sunset, 0.45 at noon

	// Apply phase functions, eye-level horizon extinction, and scale
	vec3 rayleigh_color = rayleigh_sum * rayleigh_phase_val * eye_ext * scatter_scale;
	vec3 mie_color = mie_sum * mie_phase_val * eye_ext * scatter_scale;
	vec3 spot_color = mie_sum * spot_val * scatter_scale;
	vec3 mie_ground = mie_raw_sum * 0.1 * scatter_scale;

	vec3 color = rayleigh_color + mie_color;

	// ---- Ground blending ----
	float light_angle = dot(-sun_toward, eye_pos);
	float factor_ground = saturate(-light_angle) * 0.3;
	float mix_factor_spot = saturate(-eye_dir.y / 0.06 + 1.0);
	float mix_factor = saturate(-eye_dir.y / 0.05);
	float mix_factor_ground = pow(1.0 - saturate(light_angle), 10.0);

	vec3 ground_color = u_ground_color * factor_ground;
	ground_color = mix(ground_color, mie_ground, mix_factor_ground);

	color = mix(color, ground_color, mix_factor);

	spot_color = mix(spot_color, ground_color, mix_factor_spot);
	color += spot_color;

	// Stars (rendered over computed sky, only visible at night)
	color = render_stars(color, eye_dir, sun_toward);

	// Sun height factor for cloud sunset tinting [0 = sunset, 1 = high noon]
	float sun_height_01 = saturate(sun_toward.y * 2.0 + 0.5);

	// Procedural clouds
	color = render_clouds(color, eye_dir, sun_toward, sun_height_01);

	// Dithering to reduce color banding
	float r = n4rand_ss(v_screenPos);
	color += vec3(r, r, r) / 60.0;

	// Ensure no negative values reach the tonemapper
	color = max(color, vec3_splat(0.0));

	gl_FragColor = vec4(color, 1.0);
}
