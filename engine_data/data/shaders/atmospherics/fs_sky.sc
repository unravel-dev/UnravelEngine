$input v_skyColor, v_screenPos, v_viewDir

#include "../common.sh"

uniform vec4 	u_parameters; // x - sun size, y - sun bloom, z - exposition, w - time
uniform vec4 	u_sunDirection;
uniform vec4 	u_sunLuminance;
uniform vec4 	u_cloudParams; // x - coverage, y - altitude, z - speed, w - density

#define u_sun_size u_parameters.x
#define u_sun_bloom u_parameters.y
#define u_exposition u_parameters.z
#define u_time u_parameters.w

#define u_cloud_coverage u_cloudParams.x
#define u_cloud_altitude u_cloudParams.y
#define u_cloud_time u_cloudParams.z  // accumulated seconds, pre-scaled by speed on CPU
#define u_cloud_density u_cloudParams.w

// ----------------------------- Dithering helpers -----------------------------

float nrand(in vec2 n)
{
    return fract(sin(dot(n.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

float n4rand_ss(in vec2 n)
{
    float nrnd0 = nrand(n + 0.07 * fract(u_time));
    float nrnd1 = nrand(n + 0.11 * fract(u_time + 0.573953));
    return 0.23 * sqrt(-log(nrnd0 + 0.00001)) * cos(2.0 * 3.141592 * nrnd1) + 0.5;
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

// ----------------------------- Cloud rendering ------------------------------

vec3 render_clouds(vec3 sky_color, vec3 eye_dir, vec3 light_dir, float sun_height_01)
{
    // Only render clouds above the horizon
    if(eye_dir.y < 0.01)
    {
        return sky_color;
    }

    // Ray-plane intersection: find where the view ray hits the cloud layer
    float t = u_cloud_altitude / eye_dir.y;
    vec2 cloud_uv = eye_dir.xz * t;

    // Animate with wind (slow drift) using accumulated real time from CPU
    float wind_time = u_cloud_time * 0.4;
    cloud_uv += vec2(wind_time * 12.0, wind_time * 5.0);

    // Scale UVs for cloud size
    vec2 uv = cloud_uv * 0.0004;

    // Multi-octave noise for cloud shape
    float noise = fbm(uv, 5);

    // Shape the clouds: coverage controls the threshold
    // Higher coverage = lower threshold = more clouds
    float threshold = 1.0 - u_cloud_coverage;
    float cloud = smoothstep(threshold, threshold + 0.25, noise);
    cloud *= u_cloud_density;

    // Fade clouds near the horizon to avoid a hard seam and
    // simulate atmospheric haze obscuring distant clouds
    float horizon_fade = smoothstep(0.01, 0.2, eye_dir.y);
    cloud *= horizon_fade;

    // ---- Cloud lighting ----

    // Base brightness from sun angle (higher sun = brighter clouds)
    float ndotl = saturate(dot(vec3(0.0, 1.0, 0.0), light_dir));

    // Directional shading: clouds facing the sun are brighter
    // Use a slightly offset UV to fake light penetration
    vec2 light_offset = light_dir.xz * 0.002;
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
    float view_sun = saturate(dot(eye_dir, light_dir));
    float silver = pow(view_sun, 8.0) * 0.3 * (1.0 - cloud * 0.5);
    cloud_color += vec3_splat(silver);

    // Scale cloud color by exposition for HDR consistency
    cloud_color *= u_exposition * 8.0;

    // Blend clouds over the sky
    return mix(sky_color, cloud_color, saturate(cloud));
}

// ----------------------------- Main -----------------------------------------

void main()
{
    float size2 = u_sun_size * u_sun_size;

    vec3 lightDir = normalize(u_sunDirection.xyz);
    vec3 viewDir = normalize(v_viewDir);
    float cosAngle = dot(viewDir, lightDir);
    float distance = 2.0 * (1.0 - cosAngle);

    // Inner sun disc: bright, tight core
    float sun = exp(-distance / u_sun_bloom / size2) + step(distance, size2);
    float sun2 = min(sun * sun, 1.0);

    // Mie scattering halo
    float mie_falloff = 0.08;
    float mie = exp(-distance / mie_falloff);
    float sun_height = saturate(lightDir.y * 2.0 + 0.5);
    vec3 mie_tint = mix(vec3(1.0, 0.6, 0.3), vec3(1.0, 0.95, 0.9), sun_height);
    float mie_strength = mix(0.3, 0.02, sun_height);
    vec3 mie_color = mie_tint * mie * mie_strength;

    // Combine sun disc and Mie halo
    vec3 sun_color = u_sunLuminance.xyz * u_exposition * (sun2 + mie_color);
    vec3 color = v_skyColor + sun_color;

    // Procedural clouds
    color = render_clouds(color, viewDir, lightDir, sun_height);

    // Ground color blending
    const vec3 u_ground_color = vec3(0.63, 0.6, 0.57);
    float light_angle = dot(-lightDir, vec3(0.0, 1.0, 0.0));
    vec3 ground_color = (u_ground_color + vec3(1.0, 1.0, 1.0)) * saturate(-light_angle) * 0.1;
    float ground_mask = saturate(-viewDir.y / 0.06 + 0.4);
    color = mix(color, ground_color, ground_mask);

    // Dithering to reduce color banding
    float r = n4rand_ss(v_screenPos);
    color += vec3(r, r, r) / 60.0;

    // Ensure no negative values reach the tonemapper
    color = max(color, vec3_splat(0.0));

    gl_FragColor = vec4(color, 1.0);
}
