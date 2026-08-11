/**
 * Irradiance SH Compute Shader
 * Projects sky/sun radiance to spherical harmonics (L0-L2, 9 coeffs per channel).
 * Output: 9x1 RGBA32F texture (x=coeff index 0..8, rgb=channel R,G,B; a unused).
 * Mode 0: uniform - write L0 only from ambient color * intensity.
 * Mode 1: perez - sample Perez sky, project to SH.
 * Mode 2: environment cubemap - sample textureCube(s_env, L), full L0-L2 SH.
 * Mode 3: environment cubemap flat - average cubemap into L0 only (no normal variation).
 * Mode 4: tint gradient - hemisphere gradient from tint color only (no sky), L0 + vertical L1.
 * Mode 5: perez flat - the SAME Perez integration as mode 1, kept only in L0 (the exact
 *         analog of mode 3 for mode 2). The flat sky ambient therefore equals the average
 *         of the directional one BY CONSTRUCTION - this replaced a hand-calibrated
 *         CPU-side collapse of the sky to one color.
 */

#include "../bgfx_compute.sh"

// Inlined convertXYZ2RGB - avoids shaderlib (toLinear/toLinearAccurate use pow/mix which fail on cs_5_0)
vec3 irradiance_convertXYZ2RGB(vec3 _xyz)
{
    return vec3(
        dot(vec3( 3.2404542, -1.5371385, -0.4985314), _xyz),
        dot(vec3(-0.9692660,  1.8760108,  0.0415560), _xyz),
        dot(vec3( 0.0556434, -0.2040259,  1.0572252), _xyz)
    );
}

IMAGE2D_WO(i_output, rgba32f, 0);

// Input environment cubemap (mode 2)
SAMPLERCUBE(s_env, 1);

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(int i, int N)
{
    return vec2(float(i) / float(N), RadicalInverse_VdC(uint(i)));
}

// Map Hammersley (u,v) in [0,1]^2 to uniform solid-angle direction on sphere
vec3 HammersleyToSphere(vec2 E)
{
    float phi = 2.0 * 3.14159265 * E.x;
    float cos_theta = 1.0 - 2.0 * E.y;
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    return vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
}

// Map Hammersley (u,v) to uniform solid-angle direction on upper hemisphere (y >= 0).
// Perez sky and skybox have zero/very low radiance below horizon; full-sphere sampling
// wastes half the samples and yields ~2x darker irradiance than uniform/skybox.
vec3 HammersleyToHemisphere(vec2 E)
{
    float phi = 2.0 * 3.14159265 * E.x;
    float cos_theta = 1.0 - E.y;  // zenith (1) to horizon (0)
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    return vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
}

// x=mode (0=uniform, 1=perez, 2=env cubemap), y=sun_weight (applied in shader for all modes)
uniform vec4 u_mode;
// rgb=irradiance tint, w=intensity. Applied in both modes (uniform: baked into L0; perez: multiplied on top of sky)
uniform vec4 u_irradiance_tint_intensity;
// For Perez: sun direction (points toward sun)
uniform vec4 u_sun_direction;
// Perez: sun luminance RGB
uniform vec4 u_sun_luminance;
// Perez: sky luminance XYZ (for Perez formula)
uniform vec4 u_sky_luminance_xyz;
// Perez: exposition factor
uniform vec4 u_exposition;
// Perez coefficients [5] -> 5 * vec4
uniform vec4 u_perez_coeff[5];

vec3 Perez(vec3 A, vec3 B, vec3 C, vec3 D, vec3 E, float costeta, float cosgamma)
{
    float _1_costeta = 1.0 / max(costeta, 0.01);
    float cos2gamma = cosgamma * cosgamma;
    vec3 f = (vec3_splat(1.0) + A * exp(B * _1_costeta))
           * (vec3_splat(1.0) + C * exp(D * acos(cosgamma)) + E * cos2gamma);
    return f;
}

// Per-sample Perez: dir, P0_inv (1/max(P0,0.0001)), sky_color_xyY, light_dir. P0 is loop-invariant.
vec3 sample_perez_sky(vec3 dir, vec3 P0_inv, vec3 sky_color_xyY, vec3 light_dir)
{
    vec3 sky_dir = vec3(0.0, 1.0, 0.0);
    vec3 A = u_perez_coeff[0].xyz;
    vec3 B = u_perez_coeff[1].xyz;
    vec3 C = u_perez_coeff[2].xyz;
    vec3 D = u_perez_coeff[3].xyz;
    vec3 E = u_perez_coeff[4].xyz;
    float costeta = max(dot(dir, sky_dir), 0.01);
    float cosgamma = clamp(dot(dir, light_dir), -0.9999, 0.9999);
    vec3 P = Perez(A, B, C, D, E, costeta, cosgamma);
    vec3 ratio = P * P0_inv;
    vec3 Yp = sky_color_xyY * ratio;
    float yp_y_safe = max(Yp.y, 0.0001);
    vec3 sky_color_xyz = vec3(Yp.x * Yp.z / yp_y_safe, Yp.z, (1.0 - Yp.x - Yp.y) * Yp.z / yp_y_safe);
    vec3 sky_color = max(irradiance_convertXYZ2RGB(sky_color_xyz), vec3_splat(0.0));
    float sun_cos = dot(dir, light_dir);
    float sun_disc = exp(-2.0 * (1.0 - sun_cos) / 0.02);
    vec3 sun_color = u_sun_luminance.xyz * u_exposition.x * sun_disc;
    vec3 irradiance = (sky_color + sun_color) * u_exposition.x;
    // NON-PHYSICAL art-directed saturation boost: pushes color away from luma to better
    // match the perceived vividness of the visible sky (1.15 at horizon, 1.45 at zenith).
    // This intentionally diverges from true radiometric irradiance.
    float luma = dot(irradiance, vec3(0.299, 0.587, 0.114));
    float zenith_factor = max(dir.y, 0.0);
    float saturation = mix(1.15, 1.45, zenith_factor);
    irradiance = mix(vec3_splat(luma), irradiance, saturation);
    return irradiance;
}

// SH basis for L0-L2 evaluated at direction n
// Constants from real spherical harmonics: Y00, Y1-1..Y11, Y2-2..Y22
void sh_basis(vec3 n, inout float c[9])
{
    float x = n.x;
    float y = n.y;
    float z = n.z;
    c[0] = 0.282095;
    c[1] = 0.488603 * y;
    c[2] = 0.488603 * z;
    c[3] = 0.488603 * x;
    c[4] = 1.092548 * x * z;
    c[5] = 1.092548 * y * z;
    c[6] = 1.092548 * x * y;
    c[7] = 0.315392 * (3.0 * z * z - 1.0);
    // Y22: 0.546274 * (x*x - y*y). Use (x-y)*(x+y) to avoid D3D double-precision warning X4122
    float diff = x - y;
    float sum_xy = x + y;
    c[8] = 0.546274 * diff * sum_xy;
}

// Output is 9x1 RGBA32F: one texel per coefficient holding all 3 channels in rgb.
void store_sh_coeffs(vec3 sh_coeff[9])
{
    for(int k = 0; k < 9; k++)
        imageStore(i_output, ivec2(k, 0), vec4(sh_coeff[k], 0.0));
}

void accumulate_sh_sample(vec3 dir, vec3 L, float d_omega, inout vec3 sh_coeff[9])
{
    float basis[9];
    sh_basis(dir, basis);
    for(int k = 0; k < 9; k++)
        sh_coeff[k] += L * basis[k] * d_omega;
}

// Mode 0: uniform - write L0 only from ambient color * intensity.
void process_uniform_mode(float sun_weight)
{
    // ARTIST calibration: "intensity" is the diffuse level a white wall should DISPLAY. The
    // composite applies albedo * E/pi (the energy-audit contract in fs_pbr_lighting.sh), so
    // the bake targets eval = E = pi * intensity - the pi here and the 1/pi there cancel and
    // a uniform-ambient scene renders the number the author typed. The physically-sourced
    // modes (Perez, env) project raw radiance instead and stay radiometric.
    // exposition scales raw luminance to display range (matches atmospheric sky)
    vec3 color = u_irradiance_tint_intensity.xyz;
    float intensity = u_irradiance_tint_intensity.w * sun_weight * u_exposition.x;
    float l0 = intensity / 0.282095;
    imageStore(i_output, ivec2(0, 0), vec4(l0 * color, 0.0));
    for(int i = 1; i < 9; i++)
        imageStore(i_output, ivec2(i, 0), vec4(0.0, 0.0, 0.0, 0.0));
}

// Mode 1: Perez sky - hemisphere sampling, zero radiance below horizon.
void process_perez_mode(float sun_weight)
{
    const int num_samples = 64;
    const float PI = 3.14159265;
    const float d_omega = (2.0 * PI) / float(num_samples);
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;

    vec3 light_dir = normalize(u_sun_direction.xyz);
    vec3 sky_dir = vec3(0.0, 1.0, 0.0);
    float cosgammas = clamp(dot(sky_dir, light_dir), -0.9999, 0.9999);
    vec3 P0 = Perez(u_perez_coeff[0].xyz, u_perez_coeff[1].xyz, u_perez_coeff[2].xyz,
                    u_perez_coeff[3].xyz, u_perez_coeff[4].xyz, 1.0, cosgammas);
    vec3 P0_inv = vec3_splat(1.0) / max(P0, vec3_splat(0.0001));
    float denom = max(dot(u_sky_luminance_xyz.xyz, vec3_splat(1.0)), 0.0001);
    vec3 sky_color_xyY = vec3(u_sky_luminance_xyz.x / denom, u_sky_luminance_xyz.y / denom, u_sky_luminance_xyz.y);

    vec3 sh_coeff[9];
    for(int k = 0; k < 9; k++)
        sh_coeff[k] = vec3_splat(0.0);

    for(int i = 0; i < num_samples; i++)
    {
        vec2 E = Hammersley(i, num_samples);
        vec3 dir = HammersleyToHemisphere(E);
        vec3 radiance = sample_perez_sky(dir, P0_inv, sky_color_xyY, light_dir);
        accumulate_sh_sample(dir, radiance * tint_scale, d_omega, sh_coeff);
    }
    store_sh_coeffs(sh_coeff);
}

// Mode 5: Perez sky, flat - the SAME hemisphere integration as mode 1, truncated to the
// constant band. L0 = integral(L * Y0) with Y0 = 0.282095, exactly what mode 1's first
// coefficient converges to, so switching irradiance quality can never shift the average
// energy - only remove the directional variation.
void process_perez_flat_mode(float sun_weight)
{
    const int num_samples = 64;
    const float PI = 3.14159265;
    const float d_omega = (2.0 * PI) / float(num_samples);
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;

    vec3 light_dir = normalize(u_sun_direction.xyz);
    vec3 sky_dir = vec3(0.0, 1.0, 0.0);
    float cosgammas = clamp(dot(sky_dir, light_dir), -0.9999, 0.9999);
    vec3 P0 = Perez(u_perez_coeff[0].xyz, u_perez_coeff[1].xyz, u_perez_coeff[2].xyz,
                    u_perez_coeff[3].xyz, u_perez_coeff[4].xyz, 1.0, cosgammas);
    vec3 P0_inv = vec3_splat(1.0) / max(P0, vec3_splat(0.0001));
    float denom = max(dot(u_sky_luminance_xyz.xyz, vec3_splat(1.0)), 0.0001);
    vec3 sky_color_xyY = vec3(u_sky_luminance_xyz.x / denom, u_sky_luminance_xyz.y / denom, u_sky_luminance_xyz.y);

    vec3 l0 = vec3_splat(0.0);
    for(int i = 0; i < num_samples; i++)
    {
        vec2 E = Hammersley(i, num_samples);
        vec3 dir = HammersleyToHemisphere(E);
        vec3 radiance = sample_perez_sky(dir, P0_inv, sky_color_xyY, light_dir);
        l0 += radiance * tint_scale * 0.282095 * d_omega;
    }
    imageStore(i_output, ivec2(0, 0), vec4(l0, 0.0));
    for(int i = 1; i < 9; i++)
        imageStore(i_output, ivec2(i, 0), vec4(0.0, 0.0, 0.0, 0.0));
}

// Mode 2: environment cubemap - full-sphere sampling (map may include ground).
void process_environment_mode(float sun_weight)
{
    const int num_samples = 64;
    const float PI = 3.14159265;
    const float d_omega = (4.0 * PI) / float(num_samples);
    // We only need the low-frequency (L0-L2) content, so sample a coarse prefiltered mip.
    // This averages many texels per tap, drastically reducing the variance/noise that
    // sampling mip 0 with just 64 taps would produce on a high-resolution cubemap.
    const float ENV_LOD = 4.0;
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;

    vec3 sh_coeff[9];
    for(int k = 0; k < 9; k++)
        sh_coeff[k] = vec3_splat(0.0);

    for(int i = 0; i < num_samples; i++)
    {
        vec2 E = Hammersley(i, num_samples);
        vec3 dir = HammersleyToSphere(E);
        vec3 radiance = textureCubeLod(s_env, dir, ENV_LOD).rgb;
        accumulate_sh_sample(dir, radiance * tint_scale, d_omega, sh_coeff);
    }
    store_sh_coeffs(sh_coeff);
}

// Mode 3: environment cubemap, flat - average the cubemap into L0 only (no normal variation).
void process_environment_flat_mode(float sun_weight)
{
    const int num_samples = 64;
    const float PI = 3.14159265;
    const float d_omega = (4.0 * PI) / float(num_samples);
    const float ENV_LOD = 4.0;
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;

    // Only the constant band L0 = integral(L * Y0) over the sphere, Y0 = 0.282095.
    // This is the same L0 the directional path produces, with L1-L2 discarded.
    vec3 l0 = vec3_splat(0.0);
    for(int i = 0; i < num_samples; i++)
    {
        vec2 E = Hammersley(i, num_samples);
        vec3 dir = HammersleyToSphere(E);
        vec3 radiance = textureCubeLod(s_env, dir, ENV_LOD).rgb;
        l0 += radiance * tint_scale * 0.282095 * d_omega;
    }
    imageStore(i_output, ivec2(0, 0), vec4(l0, 0.0));
    for(int i = 1; i < 9; i++)
        imageStore(i_output, ivec2(i, 0), vec4(0.0, 0.0, 0.0, 0.0));
}

// Mode 4: flat-tint hemisphere gradient (no sky contribution). Full tint for up-facing
// normals fading to a darkened tint for down-facing normals - gives directional shape from
// a single artist color. Encodes L0 + the vertical L1 band directly (no sampling needed).
void process_tint_gradient_mode(float sun_weight)
{
    const float PI = 3.14159265;
    vec3 color = u_irradiance_tint_intensity.xyz;
    float intensity = u_irradiance_tint_intensity.w * sun_weight * u_exposition.x;
    vec3 top = color * intensity;       // target irradiance at zenith (N.y = +1)
    const float ground_darken = 0.5;    // nadir is half as bright as zenith
    vec3 bottom = top * ground_darken;  // target irradiance at nadir (N.y = -1)
    // ARTIST calibration, same contract as the uniform mode: the composite divides by pi
    // (fs_pbr_lighting.sh energy audit), so the bake targets eval = pi * displayed level -
    // the lobes below are eval_irradiance_sh's constants (L0 = PI*0.282095, vertical
    // L1 = (2*PI/3)*0.488603*N.y) with that pi folded in, and a white wall shows `top`.
    const float LOBE0 = 0.282095;
    const float LOBE1 = (2.0 / 3.0) * 0.488603;
    vec3 c0 = (top + bottom) / (2.0 * LOBE0);
    vec3 c1 = (top - bottom) / (2.0 * LOBE1);
    imageStore(i_output, ivec2(0, 0), vec4(c0, 0.0));
    imageStore(i_output, ivec2(1, 0), vec4(c1, 0.0));
    for(int i = 2; i < 9; i++)
        imageStore(i_output, ivec2(i, 0), vec4(0.0, 0.0, 0.0, 0.0));
}

// Project L(omega) to SH (radiance coefficients). Store raw coeffs; Lambert convolution
// is applied when evaluating (cosine lobe in fragment shader).
NUM_THREADS(1, 1, 1)
void main()
{
    int mode = int(u_mode.x);
    float sun_weight = u_mode.y;

    if(mode == 0)
        process_uniform_mode(sun_weight);
    else if(mode == 1)
        process_perez_mode(sun_weight);
    else if(mode == 2)
        process_environment_mode(sun_weight);
    else if(mode == 3)
        process_environment_flat_mode(sun_weight);
    else if(mode == 5)
        process_perez_flat_mode(sun_weight);
    else
        process_tint_gradient_mode(sun_weight);
}