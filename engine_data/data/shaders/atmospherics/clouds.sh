#ifndef ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD
#define ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD

// Shared cloud model for the volumetric pre-pass (fs_cloud.sc) and the flat single-sample
// path (fs_sky.sc). Both paths must shape and light a cloud sample the same way so switching
// modes keeps the look; everything that is not geometry-specific lives here.
//
// Uniform layout, packed by atmospheric_pass_perez.cpp:
//   u_cloudParams  = (coverage, base altitude, thickness, density)
//   u_cloudParams2 = (shadow strength, 1 / cloud size, softness, mode)
//   u_cloudParams3 = (detail erosion, macro variation, wind offset x, wind offset y)
//   u_cloudParams4 = (cloud time, 0, 0, 0)
//
// Noise space: world position / cloud size, plus the wind offset (noise units, wrapped to
// CLOUD_NOISE_PERIOD on the CPU). The 3D texture holds the base shape (R) and the erosion
// Worley octaves (GBA); the 2D texture (same field, one slice) doubles as the weather map at
// CLOUD_MACRO_SCALE.
//
// Units: the Perez tables feed u_sunLuminance as sun ILLUMINANCE and u_skyLuminance as
// zenith sky LUMINANCE in one consistent scale (the ratio ~13:1 matches the real sky), both
// still to be multiplied by u_exposition, exactly like the sky dome. A cloud sample then
// scatters E_sun * phase (single scattering, albedo 1) plus the sky ambient, with no extra
// brightness multipliers: the clouds and the dome behind them share one light scale.

#define CLOUD_PI                    3.14159265

// Tile period of the generated noise in noise-space units. Keep in sync with
// cloud_noise_textures::tile_period.
#define CLOUD_NOISE_PERIOD          6.0

// Extinction per world unit at normalized density 1 and density knob 1. The layer is
// thousands of units thick, so even this makes a cloud opaque after a few percent of its
// thickness (view optical depth across the default 9000-unit layer ~ 40).
#define CLOUD_BASE_EXTINCTION       0.0032

// Dual-lobe Henyey-Greenstein: forward peak plus a weak back lobe.
#define CLOUD_HG_FORWARD            0.45
#define CLOUD_HG_BACK              -0.15
#define CLOUD_HG_BLEND              0.65

// Multiple scattering approximation (Wrenninge 2013, used by Frostbite / UE): octave i scales
// extinction by a^i, contribution by b^i and phase eccentricity by c^i.
#define CLOUD_MS_OCTAVES            4
#define CLOUD_MS_EXTINCTION         0.5
#define CLOUD_MS_CONTRIBUTION       0.7
#define CLOUD_MS_PHASE_ATTEN        0.5

// The Perez tables put the sun at ~13.5x the zenith luminance; a measured clear sky is
// ~25-30x (100 klux over 3-4 kcd/m2). The dome is tuned around the table, so the cloud
// sun term carries the difference: without it sunlit faces barely beat the sky behind them.
#define CLOUD_SUN_ILLUMINANCE_SCALE 2.0

// Ambient in-scatter as a fraction of the zenith sky radiance, graded by height in the
// layer: the layer shadows its own base, so bases are darker than tops. The hemisphere a
// cloud sees (whiter horizon, ground bounce) is less saturated than the zenith colour.
#define CLOUD_AMBIENT_TOP           1.0
#define CLOUD_AMBIENT_BOTTOM        0.4
#define CLOUD_AMBIENT_SATURATION    0.5

// Aerial perspective, extinction per (distance / base altitude). Distant (low elevation)
// clouds fade toward the sky behind them instead of being cut off by an angle mask.
#define CLOUD_AERIAL_EXTINCTION     0.03

// Rays below this elevation never reach the layer (the layer is above the camera).
#define CLOUD_MIN_ELEVATION         0.01

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
    return max(sun_luminance, vec3_splat(0.0)) * (exposition * CLOUD_SUN_ILLUMINANCE_SCALE);
}

// Sky ambient reaching a sample at height fraction h (0 = base, 1 = top).
vec3 cloud_ambient_radiance(vec3 sky_luminance, float exposition, float height_fraction)
{
    float occlusion = mix(CLOUD_AMBIENT_BOTTOM, CLOUD_AMBIENT_TOP, saturate(height_fraction));
    vec3 sky = max(sky_luminance, vec3_splat(0.0));
    float luma = dot(sky, vec3(0.2126, 0.7152, 0.0722));
    vec3 ambient = mix(vec3_splat(luma), sky, CLOUD_AMBIENT_SATURATION);
    return ambient * exposition * occlusion;
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

// UV of the weather lookup for a noise-space xz position.
vec2 cloud_macro_uv(vec2 sp_xz)
{
    return (sp_xz * CLOUD_MACRO_SCALE + CLOUD_MACRO_OFFSET) / CLOUD_NOISE_PERIOD;
}

// Coverage threshold: lowered by the weather map where it is dense, raised where it is
// clear, and raised with height so the tops taper into domes.
float cloud_threshold(float coverage, float macro_noise, float macro_variation, float height_fraction)
{
    float weather = (macro_noise - 0.5) * macro_variation;
    float taper = CLOUD_TOP_TAPER * height_fraction * height_fraction;
    return clamp(1.0 - coverage - weather + taper, 0.02, 0.98);
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

#endif // ATMOSPHERICS_CLOUDS_SH_HEADER_GUARD
