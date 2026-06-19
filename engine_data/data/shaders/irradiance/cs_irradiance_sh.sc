/**
 * Irradiance SH Compute Shader
 * Projects sky/sun radiance to spherical harmonics (L0-L2, 9 coeffs per channel).
 * Output: 9x3 R32F texture (x=coeff index 0..8, y=channel R,G,B).
 * Mode 0: uniform - write L0 only from ambient color * intensity.
 * Mode 1: perez - sample Perez sky, project to SH.
 * Mode 2: environment cubemap - sample textureCube(s_env, L) instead of Perez.
 */

#include "../bgfx_compute.sh"


IMAGE2D_WO(i_output, r32f, 0);

// Input environment cubemap (mode 2)
SAMPLERCUBE(s_env, 1);

// x=mode (0=uniform, 1=perez, 2=env cubemap), y=sun_weight (applied in shader for all modes)
uniform vec4 u_mode;
uniform vec4 u_irradiance_tint_intensity;
uniform vec4 u_sun_direction;
uniform vec4 u_sky_luminance;
uniform vec4 u_sun_luminance;
uniform vec4 u_sky_luminance_xyz;
uniform vec4 u_exposition;
uniform vec4 u_perez_coeff[5];

#define IRRADIANCE_SH_SAMPLES 64
#define IRRADIANCE_PI 3.14159265

// Inlined convertXYZ2RGB - avoids shaderlib (toLinear/toLinearAccurate use pow/mix which fail on cs_5_0)
vec3 irradiance_convertXYZ2RGB(vec3 _xyz)
{
    return vec3(
        dot(vec3( 3.2404542, -1.5371385, -0.4985314), _xyz),
        dot(vec3(-0.9692660,  1.8760108,  0.0415560), _xyz),
        dot(vec3( 0.0556434, -0.2040259,  1.0572252), _xyz)
    );
}

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
    float phi = 2.0 * IRRADIANCE_PI * E.x;
    float cos_theta = 1.0 - 2.0 * E.y;
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    return vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
}

// Map Hammersley (u,v) to uniform solid-angle direction on upper hemisphere (y >= 0).
vec3 HammersleyToHemisphere(vec2 E)
{
    float phi = 2.0 * IRRADIANCE_PI * E.x;
    float cos_theta = 1.0 - E.y;
    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    return vec3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
}


vec3 irradiance_lerp3(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
float irradiance_lerp1(float a, float b, float t) { return a + (b - a) * t; }

vec3 Perez(vec3 A, vec3 B, vec3 C, vec3 D, vec3 E, float costeta, float cosgamma)
{
    float inv_costeta = 1.0 / max(costeta, 0.01);
    float cos2gamma = cosgamma * cosgamma;
    vec3 f = (vec3_splat(1.0) + A * exp(B * inv_costeta))
           * (vec3_splat(1.0) + C * exp(D * acos(cosgamma)) + E * cos2gamma);
    return f;
}

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
    float luma = dot(irradiance, vec3(0.299, 0.587, 0.114));
    float zenith_factor = max(dir.y, 0.0);
    float saturation = irradiance_lerp1(1.15, 1.45, zenith_factor);
    irradiance = irradiance_lerp3(vec3_splat(luma), irradiance, saturation);
    return irradiance;
}

float sh_basis_k(vec3 n, int k)
{
    float x = n.x, y = n.y, z = n.z;
    if(k == 0) return 0.282095;
    if(k == 1) return 0.488603 * y;
    if(k == 2) return 0.488603 * z;
    if(k == 3) return 0.488603 * x;
    if(k == 4) return 1.092548 * x * z;
    if(k == 5) return 1.092548 * y * z;
    if(k == 6) return 1.092548 * x * y;
    if(k == 7) return 0.315392 * (3.0 * z * z - 1.0);
    float diff = x - y, sum_xy = x + y;
    return 0.546274 * diff * sum_xy;
}

void store_sh_coeffs(vec3 sh_coeff[9])
{
    LOOP
    for(int k = 0; k < 9; k++)
    {
        imageStore(i_output, ivec2(k, 0), vec4(sh_coeff[k].r, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(k, 1), vec4(sh_coeff[k].g, 0.0, 0.0, 0.0));
        imageStore(i_output, ivec2(k, 2), vec4(sh_coeff[k].b, 0.0, 0.0, 0.0));
    }
}

void clear_sh_coeffs()
{
    LOOP
    for(int i = 0; i < 9; i++)
    {
        LOOP
        for(int ch = 0; ch < 3; ch++)
        {
            imageStore(i_output, ivec2(i, ch), vec4(0.0, 0.0, 0.0, 0.0));
        }
    }
}

void mode_uniform(float sun_weight)
{
    vec3 color = u_irradiance_tint_intensity.xyz;
    float intensity = u_irradiance_tint_intensity.w * sun_weight * u_exposition.x;
    float l0 = intensity / (IRRADIANCE_PI * 0.282095);
    imageStore(i_output, ivec2(0, 0), vec4(l0 * color.r, 0.0, 0.0, 0.0));
    imageStore(i_output, ivec2(0, 1), vec4(l0 * color.g, 0.0, 0.0, 0.0));
    imageStore(i_output, ivec2(0, 2), vec4(l0 * color.b, 0.0, 0.0, 0.0));

    LOOP
    for(int i = 1; i < 9; i++)
    {
        LOOP
        for(int ch = 0; ch < 3; ch++)
        {
            imageStore(i_output, ivec2(i, ch), vec4(0.0, 0.0, 0.0, 0.0));
        }
    }
}

void mode_perez(float sun_weight)
{
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;
    vec3 light_dir = normalize(u_sun_direction.xyz);
    vec3 sky_dir = vec3(0.0, 1.0, 0.0);
    float cosgammas = clamp(dot(sky_dir, light_dir), -0.9999, 0.9999);
    vec3 P0 = Perez(u_perez_coeff[0].xyz, u_perez_coeff[1].xyz, u_perez_coeff[2].xyz,
                    u_perez_coeff[3].xyz, u_perez_coeff[4].xyz, 1.0, cosgammas);
    vec3 P0_inv = vec3_splat(1.0) / max(P0, vec3_splat(0.0001));
    float denom = max(dot(u_sky_luminance_xyz.xyz, vec3_splat(1.0)), 0.0001);
    vec3 sky_color_xyY = vec3(u_sky_luminance_xyz.x / denom, u_sky_luminance_xyz.y / denom, u_sky_luminance_xyz.y);
    float d_omega = (2.0 * IRRADIANCE_PI) / float(IRRADIANCE_SH_SAMPLES);
    vec3 sh_coeff[9];
    LOOP
    for(int k = 0; k < 9; k++)
    {
        sh_coeff[k] = vec3_splat(0.0);
    }
    
    LOOP
    for(int i = 0; i < IRRADIANCE_SH_SAMPLES; i++)
    {
        vec2 E = Hammersley(i, IRRADIANCE_SH_SAMPLES);
        vec3 dir = HammersleyToHemisphere(E);
        vec3 L = sample_perez_sky(dir, P0_inv, sky_color_xyY, light_dir) * tint_scale;

        LOOP
        for(int kk = 0; kk < 9; kk++)
        {
            sh_coeff[kk] += L * sh_basis_k(dir, kk) * d_omega;
        }
    }
    store_sh_coeffs(sh_coeff);
}

void mode_cubemap(float sun_weight)
{
    vec3 tint_scale = u_irradiance_tint_intensity.xyz * u_irradiance_tint_intensity.w * sun_weight;
    float d_omega = (4.0 * IRRADIANCE_PI) / float(IRRADIANCE_SH_SAMPLES);
    vec3 sh_coeff[9];

    LOOP
    for(int k = 0; k < 9; k++)
    {
        sh_coeff[k] = vec3_splat(0.0);
    }

    LOOP
    for(int i = 0; i < IRRADIANCE_SH_SAMPLES; i++)
    {
        vec2 E = Hammersley(i, IRRADIANCE_SH_SAMPLES);
        vec3 dir = HammersleyToSphere(E);
        vec3 L = textureCubeLod(s_env, dir, 0.0).rgb * tint_scale;
        LOOP
        for(int kk = 0; kk < 9; kk++)
        {
            sh_coeff[kk] += L * sh_basis_k(dir, kk) * d_omega;
        }
    }
    store_sh_coeffs(sh_coeff);
}

NUM_THREADS(1, 1, 1)
void main()
{
    int mode = int(floor(u_mode.x + 0.5));
    float sun_weight = u_mode.y;
    if(mode == 0)
        mode_uniform(sun_weight);
    else if(mode == 1)
        mode_perez(sun_weight);
    else if(mode == 2)
        mode_cubemap(sun_weight);
    else
        clear_sh_coeffs();
}
