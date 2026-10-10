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
#define TONEMAP_FILM 16


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

// Shared sRGB <-> linear conversion (exact piecewise curve). Guarded so files
// that also include lighting.sh get exactly one definition.
#ifndef SRGB_CONVERSION_SH_GUARD
#define SRGB_CONVERSION_SH_GUARD

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

#endif // SRGB_CONVERSION_SH_GUARD

// relative luminance of linear RGB(!)
// BT.709 primaries
float luminance(vec3 RGB)
{
    return 0.2126 * RGB.r + 0.7152 * RGB.g + 0.0722 * RGB.b;
}

// Exponential tone mapping
vec3 tonemap_exponential(vec3 color)
{
    return vec3_splat(1.0) - exp(-color);
}

// Reinhard
// simple version, desaturates colors

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + 1.0);
}

// Reinhard, luminance only
// possibly creates undesirable whites
// one alternative is to define a pure white point (ideally max luminance in the scene)

vec3 tonemap_reinhard_luminance(vec3 color)
{
    float lum = luminance(color);
    float nLum =  lum / (lum + 1.0);
    // max() guards the 0/0 at pure black (lum == 0 -> NaN written to the target).
    return color * (nLum / max(lum, 1e-5));
}

// Hable filmic operator: toe, linear and shoulder segments in one rational curve

vec3 hable_map(vec3 x)
{
    // a second value in a trailing comment belongs to the alternative parameter set
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
    // whiteScale = hable_map(W) with W = 11.2 evaluated with the constants
    // used in hable_map above (A=0.22, B=0.30, E=0.01). The alternative set
    // (A=0.15, B=0.50, E=0.02) yields 0.72513 -- using that value with these
    // constants over-brightens by x1.196 and clips whites early.
    const float whiteScale = 0.86730;
    const float ExposureBias = 2.0;
    return hable_map(ExposureBias * color) / whiteScale;
}

// Filmic / Hejl-Burgess-Dawson
// Rational fit of a photographic film response curve (pow 1/2.2 baked in).
// The output is display-encoded; do NOT linearize or re-apply linear_to_srgb.
vec3 tonemap_filmic(vec3 color)
{
    vec3 x = max(color - 0.004, 0.0);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

// Polynomial fit of ACES
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

    // The input is pre-scaled by 1.8 so the fit matches full ACES brightness.
    // This is internal to ACES, not a remap onto AgX -- other operators keep
    // their native mid-gray.
    color *= 1.8;
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
vec3 tonemap_aces_luminance(vec3 color)
{
    float lum = luminance(color);
    float mapped_lum = aces_filmic_curve(lum);
    return color * (mapped_lum / max(lum, 1e-5));
}

// Reinhard Extended (with white point)
vec3 tonemap_reinhard2(vec3 color)
{
    const float L_white = 4.0;
    return (color * (1.0 + color / (L_white * L_white))) / (1.0 + color);
}

// Single rational filmic curve
// Gamma 2.2 correction is baked in, do not apply linear_to_srgb
vec3 tonemap_unreal3(vec3 color)
{
    return color / (color + 0.155) * 1.019;
}

// Lottes curve: contrast a, shoulder d, solved so midIn maps to midOut and hdrMax to 1
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


// Uchimura curve: toe, linear section and shoulder (P peak, a contrast, m / l linear start / length,
// c black tightness, b pedestal)
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


// AgX tonemapping
// Polynomial fit of the default AgX contrast sigmoid (log2-encoded input in [0, 1])
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

    // The sigmoid output is sRGB-display-encoded; return linear so the caller's
    // linear_to_srgb is an exact inverse. Using pow(2.2) here while the caller
    // encodes with the piecewise sRGB curve shifts the darks.
    val = srgb_to_linear(saturate(val));
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

// ============================================================================
// FILM: the default SDR tone curve ("Filmic"): the film curve with the default film settings
// inside the SDR colour path: gamut expansion, blue correction, the curve in ACEScg (AP1) and
// back to Rec.709, evaluated per pixel. Neutral grading makes the colour correction an identity,
// so it is left out. The matrices are composed for the sRGB working colour space.
// ============================================================================
#define FILM_SLOPE 0.88
#define FILM_TOE 0.55
#define FILM_SHOULDER 0.26
#define FILM_BLACK_CLIP 0.0
#define FILM_WHITE_CLIP 0.04
#define FILM_BLUE_CORRECTION 0.6
#define FILM_EXPAND_GAMUT 1.0
// log10(x) = log2(x) * FILM_LOG10_OF_2.
#define FILM_LOG10_OF_2 0.30102999566

vec3 film_ap1_luma_weights()
{
    return vec3(0.2722287168, 0.6740817658, 0.0536895174);
}

// ACES RRT helpers.
float film_rgb_to_saturation(vec3 rgb)
{
    float min_rgb = min(min(rgb.r, rgb.g), rgb.b);
    float max_rgb = max(max(rgb.r, rgb.g), rgb.b);
    return (max(max_rgb, 1e-10) - max(min_rgb, 1e-10)) / max(max_rgb, 1e-2);
}

float film_rgb_to_yc(vec3 rgb)
{
    const float yc_radius_weight = 1.75;
    float chroma = sqrt(max(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b), 0.0));
    return (rgb.b + rgb.g + rgb.r + yc_radius_weight * chroma) / 3.0;
}

float film_sigmoid_shaper(float x)
{
    float t = max(1.0 - abs(0.5 * x), 0.0);
    float y = 1.0 + sign(x) * (1.0 - t * t);
    return 0.5 * y;
}

float film_glow_forward(float yc_in, float glow_gain, float glow_mid)
{
    if(yc_in <= 2.0 / 3.0 * glow_mid)
    {
        return glow_gain;
    }
    if(yc_in >= 2.0 * glow_mid)
    {
        return 0.0;
    }
    return glow_gain * (glow_mid / yc_in - 0.5);
}

float film_rgb_to_hue(vec3 rgb)
{
    float hue = 0.0;
    if(rgb.r != rgb.g || rgb.g != rgb.b)
    {
        hue = (180.0 / 3.14159265359) * atan2(sqrt(3.0) * (rgb.g - rgb.b), 2.0 * rgb.r - rgb.g - rgb.b);
    }
    if(hue < 0.0)
    {
        hue += 360.0;
    }
    return clamp(hue, 0.0, 360.0);
}

float film_center_hue(float hue, float center)
{
    float centered = hue - center;
    if(centered < -180.0)
    {
        centered += 360.0;
    }
    else if(centered > 180.0)
    {
        centered -= 360.0;
    }
    return centered;
}

// The film curve: AP1 in, AP1 out (0.18 maps to 0.18, the shoulder tops out at 1 + white clip).
vec3 film_tone_map(vec3 color_ap1)
{
    CONST(mat3) ap1_to_ap0 = mtxFromRows3(
        vec3(0.6954522413, 0.1406786965, 0.1638690622),
        vec3(0.0447945634, 0.8596711184, 0.0955343182),
        vec3(-0.0055258826, 0.0040252103, 1.0015006722));
    CONST(mat3) ap0_to_ap1 = mtxFromRows3(
        vec3(1.4514393161, -0.2365107469, -0.2149285693),
        vec3(-0.0765537734, 1.1762296998, -0.0996759264),
        vec3(0.0083161484, -0.0060324498, 0.9977163014));
    vec3 color_ap0 = mul(ap1_to_ap0, color_ap1);
    // RRT glow module.
    const float glow_gain = 0.05;
    const float glow_mid = 0.08;
    float saturation = film_rgb_to_saturation(color_ap0);
    float yc_in = film_rgb_to_yc(color_ap0);
    float glow_shape = film_sigmoid_shaper((saturation - 0.4) / 0.2);
    color_ap0 *= 1.0 + film_glow_forward(yc_in, glow_gain * glow_shape, glow_mid);
    // RRT red modifier.
    const float red_scale = 0.82;
    const float red_pivot = 0.03;
    const float red_hue = 0.0;
    const float red_width = 135.0;
    float centered_hue = film_center_hue(film_rgb_to_hue(color_ap0), red_hue);
    float hue_weight = smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centered_hue / red_width));
    hue_weight *= hue_weight;
    color_ap0.r += hue_weight * saturation * (red_pivot - color_ap0.r) * (1.0 - red_scale);
    vec3 working = max(mul(ap0_to_ap1, color_ap0), vec3_splat(0.0));
    // Pre desaturate.
    working = mix(vec3_splat(dot(working, film_ap1_luma_weights())), working, 0.96);
    const float toe_scale = 1.0 + FILM_BLACK_CLIP - FILM_TOE;
    const float shoulder_scale = 1.0 + FILM_WHITE_CLIP - FILM_SHOULDER;
    const float in_match = 0.18;
    const float out_match = 0.18;
    // FILM_TOE <= 0.8: 0.18 sits on the toe segment; solve the toe so that 0.18 maps to 0.18.
    const float toe_bt = (out_match + FILM_BLACK_CLIP) / toe_scale - 1.0;
    float toe_match = log2(in_match) * FILM_LOG10_OF_2 - 0.5 * log((1.0 + toe_bt) / (1.0 - toe_bt)) * (toe_scale / FILM_SLOPE);
    float straight_match = (1.0 - FILM_TOE) / FILM_SLOPE - toe_match;
    float shoulder_match = FILM_SHOULDER / FILM_SLOPE - straight_match;
    vec3 log_color = log2(max(working, vec3_splat(1e-10))) * FILM_LOG10_OF_2;
    vec3 straight_color = FILM_SLOPE * (log_color + straight_match);
    vec3 toe_color = -FILM_BLACK_CLIP + (2.0 * toe_scale) /
                     (1.0 + exp((-2.0 * FILM_SLOPE / toe_scale) * (log_color - toe_match)));
    vec3 shoulder_color = (1.0 + FILM_WHITE_CLIP) - (2.0 * shoulder_scale) /
                          (1.0 + exp((2.0 * FILM_SLOPE / shoulder_scale) * (log_color - shoulder_match)));
    toe_color = mix(toe_color, straight_color, step(vec3_splat(toe_match), log_color));
    shoulder_color = mix(shoulder_color, straight_color, step(log_color, vec3_splat(shoulder_match)));
    vec3 t = saturate((log_color - toe_match) / (shoulder_match - toe_match));
    t = shoulder_match < toe_match ? vec3_splat(1.0) - t : t;
    t = (3.0 - 2.0 * t) * t * t;
    vec3 tone_color = mix(toe_color, shoulder_color, t);
    // Post desaturate.
    tone_color = mix(vec3_splat(dot(tone_color, film_ap1_luma_weights())), tone_color, 0.93);
    return max(tone_color, vec3_splat(0.0));
}

// Linear Rec.709 in, linear display Rec.709 out (up to 1 + FILM_WHITE_CLIP; the caller encodes).
vec3 tonemap_film(vec3 color)
{
    CONST(mat3) srgb_to_ap1 = mtxFromRows3(
        vec3(0.6130974024, 0.3395231461, 0.0473794514),
        vec3(0.0701937225, 0.9163538791, 0.0134523986),
        vec3(0.0206155929, 0.1095697729, 0.8698146341));
    CONST(mat3) ap1_to_srgb = mtxFromRows3(
        vec3(1.7050509926, -0.6217921205, -0.0832588722),
        vec3(-0.1302564175, 1.1408047365, -0.0105483190),
        vec3(-0.0240033568, -0.1289689761, 1.1529723328));
    // Bright saturated colours pushed out toward a gamut between P3 and AP1.
    CONST(mat3) expand_gamut = mtxFromRows3(
        vec3(1.3704123718, -0.3292921877, -0.0636831194),
        vec3(-0.0834334917, 1.0970927480, -0.0108613795),
        vec3(-0.0257933209, -0.0986257988, 1.2036949526));
    CONST(mat3) blue_correct = mtxFromRows3(
        vec3(0.9386393778, 0.0, 0.0613606221),
        vec3(0.0, 0.8307941329, 0.1692058671),
        vec3(0.0, 0.0, 1.0));
    CONST(mat3) blue_correct_inverse = mtxFromRows3(
        vec3(1.0653748754, 0.0000014468, -0.0653710053),
        vec3(-0.0000003455, 1.2036635244, -0.2036677199),
        vec3(0.0000000199, 0.0000000212, 0.9999996001));
    vec3 color_ap1 = mul(srgb_to_ap1, color);
    float luma_ap1 = dot(color_ap1, film_ap1_luma_weights());
    if(luma_ap1 > 0.0)
    {
        vec3 chroma_ap1 = color_ap1 / luma_ap1;
        float chroma_distance_sq = dot(chroma_ap1 - vec3_splat(1.0), chroma_ap1 - vec3_splat(1.0));
        float expand_amount = (1.0 - exp2(-4.0 * chroma_distance_sq)) *
                              (1.0 - exp2(-4.0 * FILM_EXPAND_GAMUT * luma_ap1 * luma_ap1));
        color_ap1 = mix(color_ap1, mul(expand_gamut, color_ap1), expand_amount);
    }
    color_ap1 = mix(color_ap1, mul(blue_correct, color_ap1), FILM_BLUE_CORRECTION);
    color_ap1 = film_tone_map(color_ap1);
    color_ap1 = mix(color_ap1, mul(blue_correct_inverse, color_ap1), FILM_BLUE_CORRECTION);
    return max(mul(ap1_to_srgb, color_ap1), vec3_splat(0.0));
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

// Apply tonemapping. The exposure argument is an extra input scale; AE and the
// exposure slider are already applied by the caller, which passes 1.0 here so
// each operator keeps its native mid-gray (no remap onto AgX).
vec3 apply_tonemapping(vec3 color, int method, float exposure)
{
    // Reject tiny lighting negatives: Lottes/Uchimura pow() would NaN them.
    color = max(color, vec3_splat(0.0));
    color *= exposure;
    vec3 tonemapped_color;
    
	BRANCH
    if(method == TONEMAP_NONE)
    {
        // "None" skips the tone curve, not the display encode: the target is
        // UNORM8 read as sRGB by the display, so linear values still need encoding.
        tonemapped_color = linear_to_srgb(saturate(color));
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
    else if(method == TONEMAP_FILM)
    {
        // The shoulder reaches 1 + white clip; saturate clips it for the 8/10-bit display encode.
        tonemapped_color = linear_to_srgb(saturate(tonemap_film(color)));
    }
    else
    {
        // Fallback to saturate for unknown methods
        tonemapped_color = saturate(color);
    }

    return tonemapped_color;
}

#endif // TONEMAPPING_SH_HEADER_GUARD
