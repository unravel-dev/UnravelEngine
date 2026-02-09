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

// ----------------------------- Stars ----------------------------------------

// 2D hash returning vec2 (for star sub-cell position)
vec2 hash2(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 render_stars(vec3 sky_color, vec3 view_dir, vec3 light_dir)
{
    // Stars only above the horizon
    if(view_dir.y < 0.0)
    {
        return sky_color;
    }

    // ---- Night visibility ----
    // Stars fade in as the sun drops below the horizon.
    // lightDir.y < 0 means sun is below horizon (night).
    float night_factor = saturate(-light_dir.y * 4.0 - 0.1);
    if(night_factor < 0.001)
    {
        return sky_color;
    }

    // ---- Sky sphere grid ----
    // Project view direction onto a stable 2D coordinate using
    // spherical mapping. We use (atan2, acos) for a uniform grid.
    float phi = atan2(view_dir.z, view_dir.x);         // -PI to PI
    float theta = acos(clamp(view_dir.y, -1.0, 1.0));  // 0 to PI

    // Grid density: higher = more potential star cells = denser star field
    float grid_scale = 20.0;
    vec2 grid_uv = vec2(phi, theta) * grid_scale;
    vec2 cell_id = floor(grid_uv);
    vec2 cell_fract = fract(grid_uv);

    vec3 star_contribution = vec3_splat(0.0);

    // Check the current cell and its 8 neighbors so stars near
    // cell borders aren't clipped
    for(int ox = -1; ox <= 1; ox++)
    {
        for(int oy = -1; oy <= 1; oy++)
        {
            vec2 neighbor = vec2(float(ox), float(oy));
            vec2 current_cell = cell_id + neighbor;

            // Hash the cell to decide if it has a star (~8% chance)
            float star_chance = hash(current_cell * 1.73);
            if(star_chance > 0.08)
            {
                continue;
            }

            // Sub-cell position: where exactly the star sits within the cell
            vec2 star_pos = hash2(current_cell * 2.41) * 0.8 + 0.1; // keep away from edges
            vec2 diff = (neighbor + star_pos) - cell_fract;

            // Star size: tiny bright point
            float dist_sq = dot(diff, diff);
            float star_radius = 0.06;
            float star_point = exp(-dist_sq / (star_radius * star_radius));

            // ---- Brightness distribution (power law) ----
            // Most stars are dim, few are bright. Mimics real magnitude distribution.
            float mag_rand = hash(current_cell * 3.17);
            float brightness = pow(mag_rand, 5.0) * 0.8 + 0.05; // range ~0.05 to 0.85

            // A few very bright stars
            if(mag_rand > 0.97)
            {
                brightness = 1.2;
                star_radius = 0.08; // slightly larger
                star_point = exp(-dist_sq / (star_radius * star_radius));
            }

            // ---- Star color (spectral class) ----
            // Hash determines color temperature:
            // Blue-white (O/B), White (A/F), Yellow (G), Orange-Red (K/M)
            float color_rand = hash(current_cell * 4.93);
            vec3 star_color;
            if(color_rand < 0.15)
            {
                star_color = vec3(0.7, 0.8, 1.0);  // Blue-white (hot stars)
            }
            else if(color_rand < 0.55)
            {
                star_color = vec3(1.0, 1.0, 1.0);  // White
            }
            else if(color_rand < 0.80)
            {
                star_color = vec3(1.0, 0.95, 0.8); // Yellow-white (sun-like)
            }
            else if(color_rand < 0.92)
            {
                star_color = vec3(1.0, 0.85, 0.6); // Orange
            }
            else
            {
                star_color = vec3(1.0, 0.7, 0.5);  // Red-orange (cool stars)
            }

            // ---- Twinkling (atmospheric scintillation) ----
            // Slow, subtle brightness variation
            float twinkle = value_noise(current_cell * 0.5 + vec2(u_cloud_time * 0.8, u_cloud_time * 0.6));
            twinkle = mix(0.7, 1.0, twinkle); // range 0.7-1.0, subtle

            star_contribution += star_color * star_point * brightness * twinkle;
        }
    }

    // ---- Atmospheric extinction ----
    // Stars near the horizon are dimmed by thicker atmosphere
    float extinction = smoothstep(0.0, 0.25, view_dir.y);

    // ---- Final star brightness ----
    // Scale for visibility, modulated by night, extinction, and exposition
    star_contribution *= night_factor * extinction * u_exposition * 3.0;

    return sky_color + star_contribution;
}

// ----------------------------- Cloud rendering ------------------------------

vec3 render_clouds(vec3 sky_color, vec3 eye_dir, vec3 light_dir, float sun_height_01)
{
    // Only render clouds above the horizon
    if(eye_dir.y < 0.01)
    {
        return sky_color;
    }

    // ---- Cloud UV with dome curvature ----
    // Standard flat-plane intersection divides by eye_dir.y, which goes to
    // infinity at the horizon. Adding a small curvature term bends the flat
    // plane into a dome: at zenith (y=1) the effect is negligible, but near
    // the horizon (y→0) it prevents infinite stretching and makes clouds
    // appear to curve away naturally.
    // Smooth dome curvature: sqrt(y^2 + eps^2) acts as a soft floor on y.
    // At zenith (y=1): sqrt(1+0.01) ≈ 1.005 → virtually unchanged.
    // At 45° (y=0.7): sqrt(0.49+0.01) ≈ 0.707 → virtually unchanged.
    // At horizon (y→0): sqrt(0+0.01) = 0.1 → finite instead of infinity.
    // Only the last few degrees near the horizon are affected.
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

    // Base brightness from sun angle
    float ndotl = saturate(dot(vec3(0.0, 1.0, 0.0), light_dir));

    // Directional shading: offset UV toward the sun to fake light penetration
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

    // Stars (rendered behind sun and clouds, only visible at night)
    vec3 color = render_stars(v_skyColor, viewDir, lightDir);

    // Combine sun disc and Mie halo
    vec3 sun_color = u_sunLuminance.xyz * u_exposition * (sun2 + mie_color);
    color += sun_color;

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
