/**
 * Irradiance SH Compute Shader
 * Projects sky/sun radiance to spherical harmonics (L0-L2, 9 coeffs per channel).
 * Output: 9x3 R32F texture (x=coeff index 0..8, y=channel R,G,B).
 * Mode 0: uniform - write L0 only from ambient color * intensity.
 * Mode 1: perez - sample Perez sky, project to SH.
 * Mode 2: environment cubemap - sample textureCube(s_env, L) instead of Perez.
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

IMAGE2D_WO(i_output, r32f, 0);

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
// Perez: sky luminance RGB at zenith
uniform vec4 u_sky_luminance;
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
    vec3 ratio = min(P * P0_inv, vec3_splat(12.0));
    vec3 Yp = sky_color_xyY * ratio;
    float yp_y_safe = max(Yp.y, 0.0001);
    vec3 sky_color_xyz = vec3(Yp.x * Yp.z / yp_y_safe, Yp.z, (1.0 - Yp.x - Yp.y) * Yp.z / yp_y_safe);
    vec3 sky_color = max(irradiance_convertXYZ2RGB(sky_color_xyz), vec3_splat(0.0));
    float sun_cos = dot(dir, light_dir);
    float sun_disc = exp(-2.0 * (1.0 - sun_cos) / 0.02);
    vec3 sun_color = u_sun_luminance.xyz * u_exposition.x * sun_disc;
    return (sky_color + sun_color) * u_exposition.x;
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

// Project L(omega) to SH (radiance coefficients). Store raw coeffs; Lambert convolution
// is applied when evaluating (cosine lobe in fragment shader).
// Uniform solid-angle sampling over full sphere: d_omega = 4*PI / num_samples.
NUM_THREADS(1, 1, 1)
void main()
{
    int mode = int(u_mode.x);
    float sun_weight = u_mode.y;
    ivec2 size = imageSize(i_output);
    if(mode == 0)
    {
        // Uniform mode: E(N) = indirect_tint * intensity * sun_weight * exposition. SH has only L0.
        // exposition scales raw luminance to display range (matches atmospheric sky)
        vec3 color = u_irradiance_tint_intensity.xyz;
        float intensity = u_irradiance_tint_intensity.w * sun_weight * u_exposition.x;
        const float PI = 3.14159265;
        float l0 = intensity / (PI * 0.282095);
        imageStore(i_output, ivec2(0, 0), vec4(l0 * color.r, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(0, 1), vec4(l0 * color.g, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(0, 2), vec4(l0 * color.b, 0.0, 0.0, 0.0));
        for(int i = 1; i < 9; i++)
        {
            for(int ch = 0; ch < 3; ch++)
            {
                imageStore(i_output, ivec2(i, ch), vec4(0.0, 0.0, 0.0, 0.0));
            }
        }
        return;
    }
    // Perez (mode 1) or environment cubemap (mode 2): project radiance L(omega) to SH.
    // Monte Carlo with Hammersley low-discrepancy sampling.
    int num_samples = 64;
    const float PI = 3.14159265;
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;

    vec3 light_dir;
    vec3 P0_inv;
    vec3 sky_color_xyY;
    if(mode == 1)
    {
        light_dir = normalize(u_sun_direction.xyz);
        vec3 sky_dir = vec3(0.0, 1.0, 0.0);
        float cosgammas = clamp(dot(sky_dir, light_dir), -0.9999, 0.9999);
        vec3 P0 = Perez(u_perez_coeff[0].xyz, u_perez_coeff[1].xyz, u_perez_coeff[2].xyz,
                        u_perez_coeff[3].xyz, u_perez_coeff[4].xyz, 1.0, cosgammas);
        P0_inv = vec3_splat(1.0) / max(P0, vec3_splat(0.0001));
        float denom = max(dot(u_sky_luminance_xyz.xyz, vec3_splat(1.0)), 0.0001);
        sky_color_xyY = vec3(u_sky_luminance_xyz.x / denom, u_sky_luminance_xyz.y / denom, u_sky_luminance_xyz.y);
    }

    // Perez: sky dome only, zero radiance below horizon. Use hemisphere sampling.
    // Cubemap: typically sky-only; use hemisphere for consistency (full sphere if map has ground).
    const bool use_hemisphere = (mode == 1);
    const float d_omega = use_hemisphere
        ? (2.0 * PI) / float(num_samples)   // hemisphere solid angle
        : (4.0 * PI) / float(num_samples); // full sphere

    vec3 sh_coeff[9];
    for(int k = 0; k < 9; k++)
        sh_coeff[k] = vec3_splat(0.0);

    float basis[9];
    for(int i = 0; i < num_samples; i++)
    {
        vec2 E = Hammersley(i, num_samples);
        vec3 dir = use_hemisphere ? HammersleyToHemisphere(E) : HammersleyToSphere(E);
        vec3 radiance;
        if(mode == 1)
            radiance = sample_perez_sky(dir, P0_inv, sky_color_xyY, light_dir);
        else
            radiance = textureCubeLod(s_env, dir, 0.0).rgb;
        vec3 L = radiance * tint_scale;
        sh_basis(dir, basis);
        for(int k = 0; k < 9; k++)
            sh_coeff[k] += L * basis[k] * d_omega;
    }
    for(int k = 0; k < 9; k++)
    {
        imageStore(i_output, ivec2(k, 0), vec4(sh_coeff[k].r, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(k, 1), vec4(sh_coeff[k].g, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(k, 2), vec4(sh_coeff[k].b, 0.0, 0.0, 0.0));
    }
}
