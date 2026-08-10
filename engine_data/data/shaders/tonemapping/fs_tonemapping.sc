$input v_texcoord0

#include "../common.sh"
#include "tonemapping.sh"

uniform vec4 u_tonemapping;
uniform vec4 u_grading;
uniform vec4 u_wb_lms;

SAMPLER2D(s_input, 0);
SAMPLER2D(s_exposure, 1);

#define u_tonemappingExposure u_tonemapping.x
#define u_tonemappingMode     int(u_tonemapping.y)
#define u_dithering           u_tonemapping.z
#define u_contrast            u_grading.x
#define u_saturation          u_grading.y

// Color grading in LINEAR space, on post-exposure values (the contrast pivot
// only means "18% mid-gray" after exposure has normalized the scene).
vec3 apply_color_grading(vec3 color)
{
    // White balance: von Kries scaling in CAT02 LMS. u_wb_lms is (1,1,1) at
    // neutral settings, computed on the CPU from temperature/tint.
    CONST(mat3) lin2lms = mtxFromRows3(
        vec3(3.90405e-1, 5.49941e-1, 8.92632e-3),
        vec3(7.08416e-2, 9.63172e-1, 1.35775e-3),
        vec3(2.31082e-2, 1.28021e-1, 9.36245e-1));
    CONST(mat3) lms2lin = mtxFromRows3(
        vec3( 2.85847e+0, -1.62879e+0, -2.48910e-2),
        vec3(-2.10182e-1,  1.15820e+0,  3.24281e-4),
        vec3(-4.18120e-2, -1.18169e-1,  1.06867e+0));
    vec3 lms = mul(lin2lms, color);
    lms *= u_wb_lms.xyz;
    color = max(mul(lms2lin, lms), vec3_splat(0.0));

    // Log-space contrast around 18% gray: mids keep their exposure while the
    // stops above/below expand (>1) or compress (<1).
    const float mid_gray = 0.18;
    vec3 log_c = log2(max(color, vec3_splat(1e-6)) / mid_gray);
    color = exp2(log_c * u_contrast) * mid_gray;

    // Saturation around Rec.709 luma, still in linear.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(vec3_splat(luma) + (color - luma) * u_saturation, vec3_splat(0.0));

    return color;
}

void main()
{
    vec3 color = texture2D(s_input, v_texcoord0).rgb;

    float exposure = u_tonemappingExposure;
    float adapted = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0).r;
    if ((adapted != adapted) || adapted <= 0.0 || adapted >= 1.0e10)
    {
        adapted = 1.0;
    }
    exposure *= max(adapted, 1e-5);

    // Exposure first so grading operates in normalized scene-referred space,
    // then the tone curve (apply_tonemapping gets a neutral exposure of 1).
    color *= exposure;
    color = apply_color_grading(color);
    color = apply_tonemapping(color, u_tonemappingMode, 1.0);

    // Triangular-PDF dither: +-1 LSB of noise decorrelates quantization error,
    // removing banding in smooth gradients at zero visible cost. Applied to the
    // display-encoded value right before the UNORM8 write rounds it.
    if (u_dithering > 0.5)
    {
        float n1 = ign16x16(gl_FragCoord.xy);
        float n2 = ign16x16(gl_FragCoord.xy + vec2(5.588238, 5.588238));
        float tpdf = (n1 + n2) - 1.0;
        color += vec3_splat(tpdf * (1.0 / 255.0));
    }

    gl_FragColor = vec4(color, 1.0f);
}
