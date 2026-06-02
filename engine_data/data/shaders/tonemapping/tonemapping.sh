#ifndef TONEMAPPING_SH_HEADER_GUARD
#define TONEMAPPING_SH_HEADER_GUARD

// Tonemapping method constants
#define TONEMAP_NONE 0
#define TONEMAP_EXPONENTIAL 1
#define TONEMAP_REINHARD 2
#define TONEMAP_REINHARD_LUM 3
#define TONEMAP_HABLE 4
#define TONEMAP_FILMIC 5
#define TONEMAP_ACES 6
#define TONEMAP_ACES_LUM 7
#define TONEMAP_REINHARD2 8
#define TONEMAP_UNREAL3 9
#define TONEMAP_LOTTES 10
#define TONEMAP_UCHIMURA 11
#define TONEMAP_NEUTRAL 12
#define TONEMAP_AGX 13
#define TONEMAP_AGX_GOLDEN 14
#define TONEMAP_AGX_PUNCHY 15


#if BGFX_SHADER_MATRIX_COLUMN_MAJOR
#define mtxFromRows3(_0, _1, _2) transpose(mat3(_0, _1, _2) )
#else
#define mtxFromRows3(_0, _1, _2) mat3(_0, _1, _2)
#endif

#if BGFX_SHADER_MATRIX_COLUMN_MAJOR
#define mtxFromRows4(_0, _1, _2, _3) transpose(mat4(_0, _1, _2, _3) )
#else
#define mtxFromRows4(_0, _1, _2, _3) mat4(_0, _1, _2, _3)
#endif

#if BGFX_SHADER_MATRIX_COLUMN_MAJOR
#define mtxFromCols3(_0, _1, _2) mat3(_0, _1, _2)
#else
#define mtxFromCols3(_0, _1, _2) transpose(mat3(_0, _1, _2) )
#endif

#if BGFX_SHADER_MATRIX_COLUMN_MAJOR
#define mtxFromCols4(_0, _1, _2, _3) mat4(_0, _1, _2, _3)
#else
#define mtxFromCols4(_0, _1, _2, _3) transpose(mat4(_0, _1, _2, _3) )
#endif

vec3 linear_to_srgb(vec3 color)
{
    vec3 x = color * 12.92f;
    vec3 y = 1.055f * pow(saturate(color), vec3_splat(1.0f / 2.4f)) - 0.055f;

    vec3 clr = color;
    clr.r = color.r < 0.0031308f ? x.r : y.r;
    clr.g = color.g < 0.0031308f ? x.g : y.g;
    clr.b = color.b < 0.0031308f ? x.b : y.b;

    return clr;
}

vec3 srgb_to_linear(vec3 color)
{
    vec3 lo = color / 12.92f;
    vec3 hi = pow((color + 0.055f) / 1.055f, vec3_splat(2.4f));
    
    vec3 result;
    result.r = color.r <= 0.04045f ? lo.r : hi.r;
    result.g = color.g <= 0.04045f ? lo.g : hi.g;
    result.b = color.b <= 0.04045f ? lo.b : hi.b;
    
    return result;
}

// relative luminance of linear RGB(!)
// BT.709 primaries
float luminance(vec3 RGB)
{
    return 0.2126 * RGB.r + 0.7152 * RGB.g + 0.0722 * RGB.b;
}

// Reinhard, Hable, Filmic taken from:
// http://filmicworlds.com/blog/filmic-tonemapping-operators/

// Exponential tone mapping
vec3 tonemap_exponential(vec3 color)
{
    return vec3_splat(1.0) - exp(-color);
}

// Reinhard et al
// http://www.cs.utah.edu/~reinhard/cdrom/tonemap.pdf
// simple version, desaturates colors

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + 1.0);
}

// Reinhard, luminance only
// possibly creates undesirable whites
// one alternative is to define a pure white point (ideally max luminance in the scene)
// see original paper and https://imdoingitwrong.wordpress.com/2010/08/19/why-reinhard-desaturates-my-blacks-3/

vec3 tonemap_reinhard_luminance(vec3 color)
{
    float lum = luminance(color);
    float nLum =  lum / (lum + 1.0);
    return color * (nLum / lum);
}

// Uncharted 2 filmic operator
// John Hable
// https://www.gdcvault.com/play/1012351/Uncharted-2-HDR
// https://www.slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting

vec3 hable_map(vec3 x)
{
    // values used are directly from the presentation
    // comments have the values taken from the website above
    const float A = 0.22; // shoulder strength // 0.15
    const float B = 0.30; // linear strength // 0.50
    const float C = 0.10; // linear angle
    const float D = 0.20; // toe strength
    const float E = 0.01; // toe numerator // 0.02
    const float F = 0.30; // toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tonemap_hable(vec3 color)
{
    //const float W = 11.2; // linear white point
    //vec3 whiteScale = hable_map(vec3_splat(W));
    const float whiteScale = 0.72513;
    const float ExposureBias = 2.0;
    return hable_map(ExposureBias * color) / whiteScale;
}

// Filmic / Hejl-Burgess-Dawson
// Mimics response curve of Kodak film, Haarm-Pieter Duiker
// approximation by Hejl/Burgess-Dawson (pow 1/2.2 baked in)
vec3 tonemap_filmic(vec3 color)
{
    vec3 x = max(color - 0.004, 0.0);
    vec3 result = (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
    return pow(result, vec3_splat(2.2));
}

// Polynomial fit of ACES
// Stephen Hill
// Taken from:
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 tonemap_aces(vec3 color)
{
    // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
    // sRGB refers to gamut, not display transform
    CONST(mat3) ACESInputMat = mtxFromCols3(
        vec3(0.59719, 0.07600, 0.02840),
        vec3(0.35458, 0.90834, 0.13383),
        vec3(0.04823, 0.01566, 0.83777)
    );

    // ODT_SAT => XYZ => D60_2_D65 => sRGB
    CONST(mat3) ACESOutputMat = mtxFromCols3(
        vec3(1.60475, -0.10208, -0.00327),
        vec3(-0.53108, 1.10813, -0.07276),
        vec3(-0.07367, -0.00605, 1.07602)
    );

    // colors in this code are premultiplied by 1.8
    // https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ToneMapping.hlsl
    //color *= 1.8;
    vec3 result = mul(ACESInputMat, color);

    // RRT and ODT
    vec3 v = result;
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    color = a / b;

    result = mul(ACESOutputMat, color);
    return saturate(result);
}

float aces_filmic_curve(float x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Luminance-preserving ACES fit. The curve is evaluated on luminance,
// then applied as a scalar to RGB so hue ratios survive the tone map.
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 tonemap_aces_luminance(vec3 color)
{
    float lum = luminance(color);
    float mapped_lum = aces_filmic_curve(lum);
    return color * (mapped_lum / max(lum, 1e-5));
}

// Reinhard Extended (with white point)
// https://github.com/dmnsgn/glsl-tone-map
vec3 tonemap_reinhard2(vec3 color)
{
    const float L_white = 4.0;
    return (color * (1.0 + color / (L_white * L_white))) / (1.0 + color);
}

// Unreal 3, Documentation: "Color Grading"
// Gamma 2.2 correction is baked in, do not apply linear_to_srgb
vec3 tonemap_unreal3(vec3 color)
{
    return color / (color + 0.155) * 1.019;
}

// Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
vec3 tonemap_lottes(vec3 color)
{
    vec3 a = vec3_splat(1.6);
    vec3 d = vec3_splat(0.977);
    vec3 hdrMax = vec3_splat(8.0);
    vec3 midIn = vec3_splat(0.18);
    vec3 midOut = vec3_splat(0.267);
    vec3 b = (-pow(midIn, a) + pow(hdrMax, a) * midOut) / ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    vec3 c = (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) / ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(color, a) / (pow(color, a * d) * b + c);
}


// Uchimura 2017, "HDR theory and practice"
vec3 tonemap_uchimura(vec3 x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    vec3 w0 = vec3_splat(1.0) - smoothstep(0.0, m, x);
    vec3 w2 = step(m + l0, x);
    vec3 w1 = vec3_splat(1.0) - w0 - w2;
    vec3 T = m * pow(x / m, vec3_splat(c)) + b;
    vec3 S = P - (P - S1) * exp(CP * (x - S0));
    vec3 L = m + a * (x - m);
    return T * w0 + L * w1 + S * w2;
}

vec3 tonemap_uchimura(vec3 color)
{
    return tonemap_uchimura(color, 1.0, 1.0, 0.22, 0.4, 1.33, 0.0);
}


// AgX tonemapping (Missing Deadlines / Benjamin Wrensch)
// https://iolite-engine.com/blog_posts/minimal_agx_implementation
// https://github.com/sobotka/AgX
// Matrices from Troy Sobotka's AgX OCIO config
vec3 agx_default_contrast_approx(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5 * x4 * x2
           - 40.14 * x4 * x
           + 31.96 * x4
           - 6.868 * x2 * x
           + 0.4298 * x2
           + 0.1191 * x
           - 0.00232;
}



vec3 agx_core(vec3 val, vec3 slope, vec3 offset, vec3 power, float saturation)
{
    // AgX inset matrix (Rec709/sRGB)
    CONST(mat3) agx_mat = mtxFromRows3(
        vec3(0.842479062253094, 0.0784335999999992, 0.0792237451477643),
        vec3(0.0423282422610123, 0.878468636469772, 0.0791661274605434),
        vec3(0.0423756549057051, 0.0784336, 0.879142973793104)
    );
    // AgX outset matrix (inverse)
    CONST(mat3) agx_mat_inv = mtxFromRows3(
        vec3(1.19687900512017, -0.0980208811401368, -0.0990297440797205),
        vec3(-0.0528968517574562, 1.15190312990417, -0.0989611768448433),
        vec3(-0.0529716355144438, -0.0980434501171241, 1.15107367264116)
    );

    const float min_ev = -12.47393;
    const float max_ev = 4.026069;
    const vec3 lw = vec3(0.2126, 0.7152, 0.0722);

    // Input transform (inset)
    val = mul(agx_mat, val);
    val = max(val, vec3_splat(1e-10));

    // Log2 space encoding
    val = clamp(log2(val), min_ev, max_ev);
    val = (val - min_ev) / (max_ev - min_ev);
    //val = clamp(val, 0.0, 1.0);

    // Sigmoid (contrast) approximation
    val = agx_default_contrast_approx(val);

    // ASC CDL look
    val = pow(val * slope + offset, power);
    float luma = dot(val, lw);
    val = luma + saturation * (val - luma);

    // Inverse input transform (outset)
    val = mul(agx_mat_inv, val);

    // sRGB EOTF (output linear for display)
    val = pow(max(val, vec3_splat(0.0)), vec3_splat(2.2));
    return saturate(val);
}

vec3 tonemap_agx(vec3 color)
{
    return agx_core(color, vec3_splat(1.0), vec3_splat(0.0), vec3_splat(1.0), 1.0);
}

vec3 tonemap_agx_golden(vec3 color)
{
    return agx_core(color, vec3(1.0, 0.9, 0.5), vec3_splat(0.0), vec3_splat(0.8), 0.8);
}

vec3 tonemap_agx_punchy(vec3 color)
{
    return agx_core(color, vec3_splat(1.0), vec3_splat(0.0), vec3_splat(1.35), 1.4);
}

// Khronos PBR Neutral Tone Mapper
vec3 tonemap_neutral(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(min(color.r, color.g), color.b);
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(max(color.r, color.g), color.b);
    if(peak < startCompression)
        return color;
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, vec3_splat(newPeak), g);
}

// ============================================================================
// MAIN TONEMAPPING FUNCTIONS
// ============================================================================

// Apply tonemapping with exposure and method
vec3 apply_tonemapping(vec3 color, int method, float exposure)
{
    // Apply exposure first
    color *= exposure;
    
    vec3 tonemapped_color;
    
	BRANCH
    if(method == TONEMAP_NONE)
    {
        tonemapped_color = saturate(color);
    }
    else if(method == TONEMAP_EXPONENTIAL)
    {
        tonemapped_color = linear_to_srgb(tonemap_exponential(color));
    }
    else if(method == TONEMAP_REINHARD)
    {
        tonemapped_color = linear_to_srgb(tonemap_reinhard(color));
    }
    else if(method == TONEMAP_REINHARD_LUM)
    {
        tonemapped_color = linear_to_srgb(tonemap_reinhard_luminance(color));
    }
    else if(method == TONEMAP_HABLE)
    {
        tonemapped_color = linear_to_srgb(tonemap_hable(color));
    }
    else if(method == TONEMAP_FILMIC)
    {
        // tonemap_filmic bakes gamma (~2.2); output is already display-encoded for UNORM8.
        tonemapped_color = saturate(tonemap_filmic(color));
    }
    else if(method == TONEMAP_ACES)
    {
        tonemapped_color = linear_to_srgb(tonemap_aces(color));
    }
    else if(method == TONEMAP_ACES_LUM)
    {
        tonemapped_color = linear_to_srgb(tonemap_aces_luminance(color));
    }
    else if(method == TONEMAP_REINHARD2)
    {
        tonemapped_color = linear_to_srgb(tonemap_reinhard2(color));
    }
    else if(method == TONEMAP_UNREAL3)
    {
        tonemapped_color = saturate(tonemap_unreal3(color));
    }
    else if(method == TONEMAP_LOTTES)
    {
        tonemapped_color = linear_to_srgb(saturate(tonemap_lottes(color)));
    }
    else if(method == TONEMAP_UCHIMURA)
    {
        tonemapped_color = linear_to_srgb(saturate(tonemap_uchimura(color)));
    }
    else if(method == TONEMAP_NEUTRAL)
    {
        tonemapped_color = linear_to_srgb(saturate(tonemap_neutral(color)));
    }
    else if(method == TONEMAP_AGX)
    {
        tonemapped_color = linear_to_srgb(tonemap_agx(color));
    }
    else if(method == TONEMAP_AGX_GOLDEN)
    {
        tonemapped_color = linear_to_srgb(tonemap_agx_golden(color));
    }
    else if(method == TONEMAP_AGX_PUNCHY)
    {
        tonemapped_color = linear_to_srgb(tonemap_agx_punchy(color));
    }
    else
    {
        // Fallback to saturate for unknown methods
        tonemapped_color = saturate(color);
    }

    return tonemapped_color;
}

#endif // TONEMAPPING_SH_HEADER_GUARD
