#ifndef ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD
#define ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD

// NOTE: every texture read here uses an explicit LOD of 0 (the noise has no mips): the field
// is sampled inside dynamic loops, and implicit-gradient reads would force the HLSL compiler
// to unroll them (X3511).

// Shared cloud model for the volumetric pre-pass (fs_cloud.sc), the flat single-sample path
// (fs_sky.sc) and the cloud shadow map (fs_cloud_shadow.sc). Every pass must shape and light
// a cloud sample the same way so the modes agree with each other and with the shadows they
// cast; everything that is not geometry-specific lives here.
//
// Uniform layout, packed by atmospheric_pass_perez.cpp:
//   u_cloudParams  = (coverage, base altitude, thickness, density)
//   u_cloudParams2 = (shadow strength, 1 / cloud size, softness, mode)
//   u_cloudParams3 = (detail erosion, macro variation, wind offset x, wind offset y)
//   u_cloudParams4 = (cloud time, brightness, 0, 0)
//   u_cloudCamera  = (camera world position xyz, layer base world y)
//
// Space: the layer is a spherical shell around a planet centre placed below the camera's
// xz (the scene is flat; the sky curves around the camera). Its base sits at
// u_cloud_base_altitude above a ground reference that is either world y = 0 (world-space
// altitude: the camera can fly into and above the layer) or the camera itself
// (camera-relative: the layer always floats above the camera); the CPU packs the resulting
// base world y into u_cloudCamera.w. The noise field is anchored in WORLD space horizontally
// (and to the layer base vertically): noise position = (x, y - base, z) / cloud size + wind
// offset (noise units, wrapped to CLOUD_NOISE_PERIOD on the CPU). That is what lets the
// shadow map, the ground and the sky agree on where a cloud is.
// The 3D texture holds the base shape (R) and the erosion Worley octaves (GBA); the 2D
// texture (same field, one slice) doubles as the weather map at CLOUD_MACRO_SCALE and as
// the flat-mode field.
//
// Units: the Perez tables feed u_sunLuminance as sun ILLUMINANCE and u_skyLuminance as
// zenith sky LUMINANCE in one consistent scale (the ratio ~13:1 matches the real sky), both
// still to be multiplied by u_exposition, exactly like the sky dome. A cloud sample then
// scatters E_sun * phase (single scattering, albedo 1) plus the sky ambient, with no extra
// brightness multipliers: the clouds and the dome behind them share one light scale.

uniform vec4 u_cloudParams;
uniform vec4 u_cloudParams2;
uniform vec4 u_cloudParams3;
uniform vec4 u_cloudParams4;
uniform vec4 u_cloudCamera;

#define u_cloud_coverage        u_cloudParams.x
#define u_cloud_base_altitude   u_cloudParams.y
#define u_cloud_thickness       u_cloudParams.z
#define u_cloud_density         u_cloudParams.w

#define u_cloud_shadow_strength u_cloudParams2.x
#define u_cloud_inv_size        u_cloudParams2.y
#define u_cloud_softness        u_cloudParams2.z
#define u_cloud_mode            u_cloudParams2.w

#define u_cloud_detail_erode    u_cloudParams3.x
#define u_cloud_macro_variation u_cloudParams3.y
#define u_cloud_wind_offset     u_cloudParams3.zw

#define u_cloud_time            u_cloudParams4.x
// User multiplier on the scattered cloud radiance (1 = the shared sky light scale); above 1
// pushes the clouds toward white through the tonemapper without touching the dome.
#define u_cloud_brightness      u_cloudParams4.y

#define u_cloud_camera_pos      u_cloudCamera.xyz
// World-space altitude of the layer base (packed by the CPU from the altitude mode).
#define u_cloud_layer_base_y    u_cloudCamera.w

#define CLOUD_MODE_NONE       0.0
#define CLOUD_MODE_FLAT       1.0
#define CLOUD_MODE_VOLUMETRIC 2.0

#define CLOUD_PI                    3.14159265

// Tile period of the generated noise in noise-space units. Keep in sync with
// cloud_noise_textures::tile_period.
#define CLOUD_NOISE_PERIOD          6.0

// Extinction per world unit at normalized density 1 and density knob 1. The layer is
// thousands of units thick, so even this makes a cloud opaque after a few percent of its
// thickness.
#define CLOUD_BASE_EXTINCTION       0.0032

// Dual-lobe Henyey-Greenstein: forward peak plus a weak back lobe.
#define CLOUD_HG_FORWARD            0.45
#define CLOUD_HG_BACK              -0.15
#define CLOUD_HG_BLEND              0.65

// Multiple scattering approximation (Wrenninge 2013, used by Frostbite / UE): octave i scales
// extinction by a^i, contribution by b^i and phase eccentricity by c^i. Cloud droplets have
// albedo ~0.99, so the higher octaves carry a lot of energy: a strong contribution factor and
// a fast-decaying extinction are what make thick clouds white instead of grey.
#define CLOUD_MS_OCTAVES            5
#define CLOUD_MS_EXTINCTION         0.4
#define CLOUD_MS_CONTRIBUTION       0.85
#define CLOUD_MS_PHASE_ATTEN        0.5

// The Perez tables put the sun at ~13.5x the zenith luminance; a measured clear sky is
// ~25-30x (100 klux over 3-4 kcd/m2). The dome is tuned around the table, so the cloud
// sun term carries the difference: without it sunlit faces barely beat the sky behind them.
#define CLOUD_SUN_ILLUMINANCE_SCALE 2.0

// Ambient in-scatter as a fraction of the zenith sky radiance, graded by height in the
// layer: the layer shadows its own base, so bases are darker than tops. The hemisphere a
// cloud sees (whiter horizon, ground bounce, neighbouring clouds) is brighter and less
// saturated than the zenith colour, so the top factor exceeds 1.
#define CLOUD_AMBIENT_TOP           1.2
#define CLOUD_AMBIENT_BOTTOM        0.6
#define CLOUD_AMBIENT_SATURATION    0.35

// Aerial perspective, extinction per (distance / base altitude). Distant (low elevation)
// clouds fade toward the sky behind them instead of being cut off by an angle mask.
#define CLOUD_AERIAL_EXTINCTION     0.03

// Flat path: rays below this elevation never reach the plane.
#define CLOUD_MIN_ELEVATION         0.01

// Planet shell: radius of the planet the layer wraps around (1 unit = 1 m). The centre sits
// below the camera's xz, CLOUD_PLANET_RADIUS under the ground reference.
#define CLOUD_PLANET_RADIUS         6360000.0

// Vertical profile of a cumulus layer: flat base (density rises over the lowest 8%),
// rounded tops (the coverage threshold rises quadratically with height so only the densest
// noise survives near the top -> domes, not slabs), erosion weaker at the base.
#define CLOUD_GRADIENT_BOTTOM_END   0.08
#define CLOUD_GRADIENT_TOP_START    0.70
#define CLOUD_TOP_TAPER             0.22
#define CLOUD_ERODE_BOTTOM          0.45

// Weather map: the 2D noise sampled at this scale relative to the cloud size (one weather
// tile spans CLOUD_NOISE_PERIOD / CLOUD_MACRO_SCALE = 20 cloud sizes) modulates the
// coverage threshold: clear patches and dense banks instead of a uniform sheet.
#define CLOUD_MACRO_SCALE           0.3
#define CLOUD_MACRO_OFFSET          vec2(3.7, 1.3)

// Erosion: the Worley octaves read at CLOUD_DETAIL_SCALE times the base frequency, plus a
// second read at CLOUD_DETAIL2_SCALE times that for the fine cauliflower structure.
#define CLOUD_DETAIL_SCALE          5.0
#define CLOUD_DETAIL_OFFSET         vec3(17.3, 41.7, 23.1)
#define CLOUD_DETAIL2_SCALE         3.0
#define CLOUD_DETAIL2_OFFSET        vec3(5.1, 13.7, 29.3)
#define CLOUD_DETAIL2_WEIGHT        0.35

// Flat clouds: one projected plane, so the vertical integration the volumetric path gets
// for free is approximated. Softer edge ramp (no height gradient to feather the mask), a
// fraction of the layer thickness as the optical path, and a lower coverage threshold: a
// single slice of the 3D field passes the threshold less often than a whole column does.
#define CLOUD_FLAT_DOME_EPS            0.2
#define CLOUD_FLAT_EDGE_SCALE          2.5
#define CLOUD_FLAT_THICKNESS_FRACTION  0.05
#define CLOUD_FLAT_SHADOW_OFFSET       0.0015
#define CLOUD_FLAT_WARP_STRENGTH       0.08
#define CLOUD_FLAT_HEIGHT_FRACTION     0.5
#define CLOUD_FLAT_COVERAGE_BIAS       0.12
#define CLOUD_FLAT_ERODE_SCALE         0.5

#define CLOUD_DENSITY_EPS           0.001

float cloud_hg(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * CLOUD_PI * denom * sqrt(denom));
}

// Dual-lobe phase; g_scale attenuates both lobes toward isotropic for the higher
// multi-scattering octaves.
float cloud_phase(float cos_theta, float g_scale)
{
    float hg_fwd = cloud_hg(cos_theta, CLOUD_HG_FORWARD * g_scale);
    float hg_bk = cloud_hg(cos_theta, CLOUD_HG_BACK * g_scale);
    return mix(hg_bk, hg_fwd, CLOUD_HG_BLEND);
}

// Sun in-scatter factor for a sample with optical depth `od_sun` toward the sun, summed
// over the multi-scattering octaves. Multiply by the sun radiance.
float cloud_sun_scatter(float od_sun, float cos_theta)
{
    float result = 0.0;
    float a = 1.0;
    float b = 1.0;
    float c = 1.0;
    for(int i = 0; i < CLOUD_MS_OCTAVES; i++)
    {
        result += b * exp(-od_sun * a) * cloud_phase(cos_theta, c);
        a *= CLOUD_MS_EXTINCTION;
        b *= CLOUD_MS_CONTRIBUTION;
        c *= CLOUD_MS_PHASE_ATTEN;
    }
    return result;
}

// Sun irradiance reaching the layer, in the dome's units. The Perez table is already zero
// at night, and keeps its warm chroma at low sun: never clamp it.
vec3 cloud_sun_radiance(vec3 sun_luminance, float exposition)
{
    return max(sun_luminance, vec3_splat(0.0)) *
           (exposition * CLOUD_SUN_ILLUMINANCE_SCALE * u_cloud_brightness);
}

// Sky ambient reaching a sample at height fraction h (0 = base, 1 = top).
vec3 cloud_ambient_radiance(vec3 sky_luminance, float exposition, float height_fraction)
{
    float occlusion = mix(CLOUD_AMBIENT_BOTTOM, CLOUD_AMBIENT_TOP, saturate(height_fraction));
    vec3 sky = max(sky_luminance, vec3_splat(0.0));
    float luma = dot(sky, vec3(0.2126, 0.7152, 0.0722));
    vec3 ambient = mix(vec3_splat(luma), sky, CLOUD_AMBIENT_SATURATION);
    return ambient * (exposition * occlusion * u_cloud_brightness);
}

float cloud_aerial_transmittance(float distance, float base_altitude)
{
    return exp(-CLOUD_AERIAL_EXTINCTION * distance / max(base_altitude, 1.0));
}

float cloud_height_gradient(float height_fraction)
{
    float bottom = smoothstep(0.0, CLOUD_GRADIENT_BOTTOM_END, height_fraction);
    float top = smoothstep(1.0, CLOUD_GRADIENT_TOP_START, height_fraction);
    return bottom * top;
}

// Interleaved Gradient Noise (Jimenez 2014): per-pixel jitter that averages out under
// temporal accumulation.
float cloud_interleaved_gradient_noise(vec2 pixel)
{
    return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

// Noise-space position of a WORLD position (world anchoring horizontally, layer-relative
// vertically, plus wind).
vec3 cloud_noise_pos(vec3 world_pos)
{
    vec3 sp = vec3(world_pos.x, world_pos.y - u_cloud_layer_base_y, world_pos.z) * u_cloud_inv_size;
    sp.xz += u_cloud_wind_offset;
    return sp;
}

// Flat height fraction of a world position inside the layer (0 = base, 1 = top). The shadow
// map uses this (it is a flat projection); the volumetric march uses the radial version.
float cloud_height_fraction(vec3 world_pos)
{
    return saturate((world_pos.y - u_cloud_layer_base_y) / u_cloud_thickness);
}

// Planet centre in camera-relative space.
vec3 cloud_planet_center_rel()
{
    float ground_y = u_cloud_layer_base_y - u_cloud_base_altitude;
    return vec3(0.0, ground_y - u_cloud_camera_pos.y - CLOUD_PLANET_RADIUS, 0.0);
}

// Radial height fraction of a camera-relative position inside the shell.
float cloud_height_fraction_rel(vec3 rel_pos)
{
    float radius = length(rel_pos - cloud_planet_center_rel());
    return saturate((radius - (CLOUD_PLANET_RADIUS + u_cloud_base_altitude)) / u_cloud_thickness);
}

// Ray (origin, dir) against the sphere (center, radius): sorted roots, or (-1, -1) when the
// ray misses. Written for planet-scale radii in float: the constant term is the product of
// the radius difference and sum (no catastrophic cancellation), and the roots come from the
// stable quadratic form.
vec2 cloud_ray_sphere(vec3 origin, vec3 dir, vec3 center, float radius)
{
    vec3 oc = origin - center;
    float oc_len = length(oc);
    float b = dot(dir, oc);
    float c = (oc_len - radius) * (oc_len + radius);
    float disc = b * b - c;
    if(disc < 0.0)
    {
        return vec2(-1.0, -1.0);
    }
    float sq = sqrt(disc);
    float q = (b >= 0.0) ? -(b + sq) : -(b - sq);
    float t0 = q;
    float t1 = (abs(q) > 1e-6) ? c / q : q;
    return vec2(min(t0, t1), max(t0, t1));
}

// Interval [t_start, t_end] of the camera ray inside the layer shell, before any scene depth
// clip. Handles the camera below, inside and above the layer and the planet blocking rays
// that go down. Returns false when the ray never reaches the layer.
bool cloud_shell_interval(vec3 rd, out float o_start, out float o_end)
{
    vec3 center = cloud_planet_center_rel();
    float ground_radius = CLOUD_PLANET_RADIUS;
    float base_radius = CLOUD_PLANET_RADIUS + u_cloud_base_altitude;
    float top_radius = base_radius + u_cloud_thickness;
    float cam_radius = length(-center);
    vec3 up = -center / max(cam_radius, 1.0);
    vec3 origin = vec3_splat(0.0);

    vec2 inner = cloud_ray_sphere(origin, rd, center, base_radius);
    vec2 outer = cloud_ray_sphere(origin, rd, center, top_radius);
    o_start = 0.0;
    o_end = 0.0;

    // Planet: a ray that points into the ground never reaches the layer (camera on or below
    // the ground reference) or is cut where it hits it (camera above).
    float ground_limit = 1e30;
    if(cam_radius <= ground_radius + 1.0)
    {
        if(dot(rd, up) < 0.0)
        {
            return false;
        }
    }
    else
    {
        vec2 ground = cloud_ray_sphere(origin, rd, center, ground_radius);
        if(ground.x > 0.0)
        {
            ground_limit = ground.x;
        }
    }

    if(cam_radius < base_radius)
    {
        // Below the layer: enter at the inner sphere (far root), leave at the outer.
        if(inner.y <= 0.0 || outer.y <= 0.0)
        {
            return false;
        }
        o_start = inner.y;
        o_end = outer.y;
    }
    else if(cam_radius <= top_radius)
    {
        // Inside the layer: march from the camera to whichever boundary comes first.
        o_start = 0.0;
        o_end = outer.y > 0.0 ? outer.y : 1e30;
        if(inner.x > 0.0)
        {
            o_end = min(o_end, inner.x);
        }
    }
    else
    {
        // Above the layer: enter at the outer sphere (near root), leave at the inner sphere or
        // back out of the outer.
        if(outer.x <= 0.0)
        {
            return false;
        }
        o_start = outer.x;
        o_end = inner.x > 0.0 ? inner.x : outer.y;
    }
    o_end = min(o_end, ground_limit);
    return o_end > o_start;
}

// UV of the weather lookup for a noise-space xz position.
vec2 cloud_macro_uv(vec2 sp_xz)
{
    return (sp_xz * CLOUD_MACRO_SCALE + CLOUD_MACRO_OFFSET) / CLOUD_NOISE_PERIOD;
}

// Coverage threshold before clamping: lowered by the weather map where it is dense, raised
// where it is clear, and raised with height so the tops taper into domes. At or above 1 no
// noise value passes, which callers use to skip the 3D read in clear weather.
float cloud_threshold_raw(float coverage, float macro_noise, float macro_variation, float height_fraction)
{
    float weather = (macro_noise - 0.5) * macro_variation;
    float taper = CLOUD_TOP_TAPER * height_fraction * height_fraction;
    return 1.0 - coverage - weather + taper;
}

float cloud_threshold(float coverage, float macro_noise, float macro_variation, float height_fraction)
{
    return clamp(cloud_threshold_raw(coverage, macro_noise, macro_variation, height_fraction), 0.02, 0.98);
}

// Base density mask from the base noise, threshold and ramp width (softness).
float cloud_shape_mask(float base_noise, float threshold, float softness)
{
    return smoothstep(threshold, threshold + softness, base_noise);
}

// Combined erosion value from the two Worley reads (GBA = 1x / 2x / 4x at each scale).
float cloud_detail_value(vec3 worley_detail, vec3 worley_detail_fine)
{
    const vec3 octave_weights = vec3(0.625, 0.25, 0.125);
    float coarse = dot(worley_detail, octave_weights);
    float fine = dot(worley_detail_fine, octave_weights);
    return mix(coarse, fine, CLOUD_DETAIL2_WEIGHT);
}

// Detail erosion: carve the edges with the Worley octaves; stronger toward the top (billowy
// tops, flatter bases) and vanishing in the dense core (1 - d^2) so it never punches holes
// through thick cloud.
float cloud_erode(float density, float detail, float erode_strength, float height_fraction)
{
    float strength = erode_strength * mix(CLOUD_ERODE_BOTTOM, 1.0, saturate(height_fraction));
    float edge_factor = 1.0 - density * density;
    return max(0.0, density - detail * strength * edge_factor);
}

// Volumetric base shape (no detail) at a WORLD position with a given height fraction (radial
// for the volumetric march, flat for the shadow map): the height profile, the weather
// modulated threshold and the base noise. Used by the far light-march samples, the shadow
// map, and as the first stage of the full sample.
float cloud_sample_shape(sampler3D noise3d, sampler2D noise2d, vec3 world_pos, float height_fraction, out vec3 o_sp)
{
    o_sp = vec3_splat(0.0);
    float h_grad = cloud_height_gradient(height_fraction);
    if(h_grad < CLOUD_DENSITY_EPS)
    {
        return 0.0;
    }
    vec3 sp = cloud_noise_pos(world_pos);
    o_sp = sp;
    float macro = texture2DLod(noise2d, cloud_macro_uv(sp.xz), 0.0).r;
    float threshold_raw = cloud_threshold_raw(u_cloud_coverage, macro, u_cloud_macro_variation, height_fraction);
    if(threshold_raw >= 1.0)
    {
        // Clear weather here: no base noise value can pass, skip the 3D read.
        return 0.0;
    }
    float threshold = clamp(threshold_raw, 0.02, 0.98);
    float base_noise = texture3DLod(noise3d, sp / CLOUD_NOISE_PERIOD, 0.0).r;
    return cloud_shape_mask(base_noise, threshold, u_cloud_softness) * h_grad;
}

// Full normalized volumetric density [0,1]: shape eroded by the Worley detail octaves.
float cloud_sample_density(sampler3D noise3d, sampler2D noise2d, vec3 world_pos, float height_fraction)
{
    vec3 sp;
    float density = cloud_sample_shape(noise3d, noise2d, world_pos, height_fraction, sp);
    if(density < CLOUD_DENSITY_EPS)
    {
        return 0.0;
    }
    vec3 detail_sp = sp * CLOUD_DETAIL_SCALE + CLOUD_DETAIL_OFFSET;
    vec3 worley_detail = texture3DLod(noise3d, detail_sp / CLOUD_NOISE_PERIOD, 0.0).gba;
    vec3 worley_fine = texture3DLod(noise3d, (detail_sp * CLOUD_DETAIL2_SCALE + CLOUD_DETAIL2_OFFSET) / CLOUD_NOISE_PERIOD, 0.0).gba;
    float detail = cloud_detail_value(worley_detail, worley_fine);
    return cloud_erode(density, detail, u_cloud_detail_erode, height_fraction);
}

// Flat-mode density mask at a noise-space xz position (the plane at the layer base):
// weather-modulated threshold and a wider ramp to stand in for the vertical integration a
// single sample cannot do.
float cloud_flat_mask(sampler2D noise2d, vec2 sp)
{
    float macro = texture2DLod(noise2d, cloud_macro_uv(sp), 0.0).r;
    float base_noise = texture2DLod(noise2d, sp / CLOUD_NOISE_PERIOD, 0.0).r;
    float threshold = cloud_threshold(u_cloud_coverage, macro, u_cloud_macro_variation, CLOUD_FLAT_HEIGHT_FRACTION)
                    - CLOUD_FLAT_COVERAGE_BIAS;
    return cloud_shape_mask(base_noise, threshold, u_cloud_softness * CLOUD_FLAT_EDGE_SCALE);
}

// Flat-mode density with domain warp and erosion at a noise-space xz position.
float cloud_flat_density(sampler2D noise2d, vec2 sp)
{
    vec2 warp_uv = (sp * 1.3 + vec2(3.7, 7.1)) / CLOUD_NOISE_PERIOD;
    float warp_x = texture2DLod(noise2d, warp_uv, 0.0).r - 0.5;
    float warp_y = texture2DLod(noise2d, warp_uv + vec2(5.3, 2.9), 0.0).r - 0.5;
    sp += vec2(warp_x, warp_y) * CLOUD_FLAT_WARP_STRENGTH * CLOUD_NOISE_PERIOD;
    float density = cloud_flat_mask(noise2d, sp);
    if(density < CLOUD_DENSITY_EPS)
    {
        return 0.0;
    }
    vec2 detail_sp = sp * CLOUD_DETAIL_SCALE + CLOUD_DETAIL_OFFSET.xy;
    vec3 worley_detail = texture2DLod(noise2d, detail_sp / CLOUD_NOISE_PERIOD, 0.0).gba;
    vec3 worley_fine = texture2DLod(noise2d, (detail_sp * CLOUD_DETAIL2_SCALE + CLOUD_DETAIL2_OFFSET.xy) / CLOUD_NOISE_PERIOD, 0.0).gba;
    float detail = cloud_detail_value(worley_detail, worley_fine);
    return cloud_erode(density, detail, u_cloud_detail_erode * CLOUD_FLAT_ERODE_SCALE, CLOUD_FLAT_HEIGHT_FRACTION);
}

// Optical path of the flat plane (the same fraction of the layer the flat path composites with).
float cloud_flat_extinction()
{
    return CLOUD_BASE_EXTINCTION * u_cloud_density * u_cloud_thickness * CLOUD_FLAT_THICKNESS_FRACTION;
}

#endif // ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD
