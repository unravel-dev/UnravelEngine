$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"

uniform vec4 	u_parameters; // x - sun size, y - sun bloom, z - exposition, w - time
uniform vec4 	u_sunDirection;
uniform vec4 	u_sunLuminance;
uniform vec4 	u_cloudParams;  // x - coverage, y - base_altitude, z - time, w - density
uniform vec4 	u_cloudParams2; // x - absorption, y - light_absorption, z - top_altitude, w - cloud_mode

#define u_sun_size u_parameters.x
#define u_sun_bloom u_parameters.y
#define u_exposition u_parameters.z
#define u_time u_parameters.w

#define u_cloud_coverage         u_cloudParams.x
#define u_cloud_base_altitude    u_cloudParams.y
#define u_cloud_time             u_cloudParams.z
#define u_cloud_density          u_cloudParams.w

#define u_cloud_absorption       u_cloudParams2.x
#define u_cloud_light_absorption u_cloudParams2.y
#define u_cloud_top_altitude     u_cloudParams2.z
#define u_cloud_mode             u_cloudParams2.w

#define CLOUD_MODE_NONE       0.0
#define CLOUD_MODE_FLAT       1.0
#define CLOUD_MODE_VOLUMETRIC 2.0

SAMPLER2D(s_cloudTex, 0);
SAMPLER2D(s_cloudNoise2D, 1);

#define CLOUD_FLAT_PI            3.14159265
#define CLOUD_FLAT_NOISE_PERIOD  6.0
#define CLOUD_FLAT_WIND_SPEED    0.3
#define CLOUD_FLAT_BASE_DENSITY  0.04
#define CLOUD_FLAT_THICKNESS     1200.0
#define CLOUD_FLAT_HG_FORWARD    0.45
#define CLOUD_FLAT_HG_BACK      -0.15
#define CLOUD_FLAT_HG_BLEND      0.65
#define CLOUD_FLAT_AMBIENT       0.22
#define CLOUD_FLAT_SUN_INTENSITY 16.0
#define CLOUD_FLAT_HORIZON_FADE  0.05
#define CLOUD_FLAT_DOME_EPS      0.2
#define CLOUD_FLAT_UV_SCALE      0.00008

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

// ----------------------------- Flat cloud scattering model -------------------
// Adapted from the volumetric path (fs_cloud.sc) to run on a single projected
// plane. Same phase function, extinction, and color model so both paths produce
// visually consistent results at identical parameter values.

float cloud_hg(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * CLOUD_FLAT_PI * denom * sqrt(denom));
}

float cloud_dual_lobe_phase(float cos_theta)
{
    float hg_fwd  = cloud_hg(cos_theta, CLOUD_FLAT_HG_FORWARD);
    float hg_back = cloud_hg(cos_theta, CLOUD_FLAT_HG_BACK);
    return mix(hg_back, hg_fwd, CLOUD_FLAT_HG_BLEND);
}

float cloud_powder(float density, float cos_theta)
{
    float powder = 1.0 - exp(-density * 2.0);
    float backlit = saturate(-cos_theta * 0.5 + 0.5);
    return mix(1.0, powder * 2.0, backlit * 0.3);
}

vec3 render_clouds(vec3 sky_color, vec3 eye_dir, vec3 light_dir)
{
    if(eye_dir.y < 0.01)
    {
        return sky_color;
    }

    // Dome-projected UV (soft floor on y prevents horizon blow-up)
    float y_dome = sqrt(eye_dir.y * eye_dir.y + CLOUD_FLAT_DOME_EPS * CLOUD_FLAT_DOME_EPS);
    float t_hit  = u_cloud_base_altitude / y_dome;
    vec2  cloud_world = eye_dir.xz * t_hit;

    // World-to-noise UV (same scale factor as volumetric: 0.00008 / period)
    vec2 uv = cloud_world * CLOUD_FLAT_UV_SCALE / CLOUD_FLAT_NOISE_PERIOD;

    // Wind animation: mirrors volumetric cloud_sample_pos() exactly.
    // Vol: sp += time*speed*dir, then uvw = sp/period  →  net = time*speed*dir/period
    // Flat: uv is already /period, so we must also divide the offset by period.
    uv.x += u_cloud_time * CLOUD_FLAT_WIND_SPEED * 10.0 / CLOUD_FLAT_NOISE_PERIOD;
    uv.y += u_cloud_time * CLOUD_FLAT_WIND_SPEED *  4.0 / CLOUD_FLAT_NOISE_PERIOD;

    // ---- Base shape from 2D noise texture (R channel) ----
    float base_noise = texture2D(s_cloudNoise2D, uv).r;
    float threshold  = 1.0 - u_cloud_coverage;
    float density    = smoothstep(threshold, threshold + 0.4, base_noise);

    if(density < 0.001)
    {
        return sky_color;
    }

    // ---- Detail erosion (same algorithm as volumetric sample_cloud_density_full) ----
    vec2 detail_uv = uv * 5.0 + vec2(17.3, 41.7) / CLOUD_FLAT_NOISE_PERIOD;
    vec4 dns       = texture2D(s_cloudNoise2D, detail_uv);
    float detail   = dns.g * 0.625 + dns.b * 0.25 + dns.a * 0.125;

    float edge_factor = 1.0 - density * density;
    density = max(0.0, density - detail * 0.35 * edge_factor);
    density *= u_cloud_density;

    if(density < 0.001)
    {
        return sky_color;
    }

    // ---- Fake light march: sample at sun-offset UV ----
    // Same formula as volumetric: exp(-density * DENSITY * path * LIGHT_ABSORPT)
    vec2  sun_uv      = uv + light_dir.xz * 0.0015;
    float lit_noise   = texture2D(s_cloudNoise2D, sun_uv).r;
    float lit_density = smoothstep(threshold, threshold + 0.4, lit_noise);
    float shadow_od   = lit_density * CLOUD_FLAT_BASE_DENSITY * CLOUD_FLAT_THICKNESS * u_cloud_light_absorption;
    float shadow      = exp(-shadow_od);
    float ms          = exp(-shadow_od * 0.2) * 0.35;
    shadow            = max(shadow, ms);

    // ---- Phase function (identical to volumetric) ----
    float cos_theta = dot(eye_dir, light_dir);
    float phase     = cloud_dual_lobe_phase(cos_theta);

    // Beer-Lambert: same formula as volumetric per-step extinction
    // exp(-density * DENSITY_SCALE * effective_thickness * ABSORPT)
    float optical_depth = density * CLOUD_FLAT_BASE_DENSITY * CLOUD_FLAT_THICKNESS * u_cloud_absorption;
    float beer          = exp(-optical_depth);
    float alpha         = 1.0 - beer;

    // Powder effect
    float powder = cloud_powder(density, cos_theta);

    // ---- Sun / ambient color (same model as volumetric) ----
    float sun_height   = saturate(light_dir.y * 2.0 + 0.5);
    vec3  sunset_tint  = mix(vec3(1.0, 0.7, 0.45), vec3(1.0, 1.0, 1.0), sun_height);
    float night_factor = saturate(-light_dir.y * 3.0 - 0.2);
    vec3  sun_color    = sunset_tint * (1.0 - night_factor * 0.9);
    vec3  ambient      = vec3(0.45, 0.55, 0.75) * CLOUD_FLAT_AMBIENT * (1.0 - night_factor * 0.8);

    vec3 cloud_color = sun_color * shadow * phase * CLOUD_FLAT_SUN_INTENSITY * powder + ambient;
    cloud_color     *= u_exposition * 8.0;

    // Horizon fade
    float horizon = smoothstep(CLOUD_FLAT_HORIZON_FADE, 0.4, eye_dir.y);
    alpha *= horizon;

    return mix(sky_color, cloud_color, saturate(alpha));
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

    // Composite clouds based on cloud_mode
    if(u_cloud_mode > CLOUD_MODE_NONE + 0.5)
    {
        if(u_cloud_mode > CLOUD_MODE_FLAT + 0.5)
        {
            // Volumetric: composite half-res pre-pass result
            vec2 cloud_uv = clipToUv(v_clipPos * 0.5 + 0.5);
            vec4 cloud_data = texture2D(s_cloudTex, cloud_uv);
            color = color * cloud_data.a + cloud_data.rgb;
        }
        else
        {
            // Flat: single-sample scattering on projected dome
            color = render_clouds(color, viewDir, lightDir);
        }
    }

    // Zenith gradient: slightly darker/deeper blue toward zenith (matches clear-sky reference)
    float zenith_darken = 1.0 - 0.08 * max(viewDir.y, 0.0);
    color *= zenith_darken;

    // Ground color blending
    const vec3 u_ground_color = vec3(0.63, 0.6, 0.57);
    float light_angle = dot(-lightDir, vec3(0.0, 1.0, 0.0));
    vec3 ground_color = (u_ground_color + vec3(1.0, 1.0, 1.0)) * saturate(-light_angle) * 0.1;
    float ground_mask = saturate(-viewDir.y / 0.06 + 0.4);
    color = mix(color, ground_color, ground_mask);

    // Dithering to reduce color banding
    float r = n4rand_ss(v_clipPos);
    color += vec3(r, r, r) / 60.0;

    // Ensure no negative values reach the tonemapper
    color = max(color, vec3_splat(0.0));

    gl_FragColor = vec4(color, 1.0);
}
