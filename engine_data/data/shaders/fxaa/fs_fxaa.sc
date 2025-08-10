$input v_texcoord0

#include "../common.sh"

/*============================================================================


                 NVIDIA FXAA 3.11 by TIMOTHY LOTTES
------------------------------------------------------------------------------
                     INTEGRATION - RGBL AND COLORSPACE
------------------------------------------------------------------------------
FXAA3 requires RGBL as input.

RGB should be LDR (low dynamic range).
Specifically do FXAA after tonemapping.

RGB data as returned by a texture fetch can be linear or non-linear.
Note an "sRGB format" texture counts as linear,
because the result of a texture fetch is linear data.
Regular "RGBA8" textures in the sRGB colorspace are non-linear.

Luma must be stored in the alpha channel prior to running FXAA.
This luma should be in a perceptual space (could be gamma 2.0).
Example pass before FXAA where output is gamma 2.0 encoded,

  color.rgb = ToneMap(color.rgb); // linear color output
  color.rgb = sqrt(color.rgb);    // gamma 2.0 color output
  return color;

To use FXAA,

  color.rgb = ToneMap(color.rgb);  // linear color output
  color.rgb = sqrt(color.rgb);     // gamma 2.0 color output
  color.a = dot(color.rgb, float3(0.299, 0.587, 0.114)); // compute luma
  return color;

Another example where output is linear encoded,
say for instance writing to an sRGB formated render target,
where the render target does the conversion back to sRGB after blending,

  color.rgb = ToneMap(color.rgb); // linear color output
  return color;

To use FXAA,

  color.rgb = ToneMap(color.rgb); // linear color output
  color.a = sqrt(dot(color.rgb, float3(0.299, 0.587, 0.114))); // compute luma
  return color;

Getting luma correct is required for the algorithm to work correctly.

------------------------------------------------------------------------------
                       BEING LINEARLY CORRECT?
------------------------------------------------------------------------------
Applying FXAA to a framebuffer with linear RGB color will look worse.
This is very counter intuitive, but happends to be true in this case.
The reason is because dithering artifacts will be more visiable
in a linear colorspace.
------------------------------------------------------------------------------
                            COMPLEX INTEGRATION
------------------------------------------------------------------------------
Q. What if the engine is blending into RGB before wanting to run FXAA?

A. In the last opaque pass prior to FXAA,
   have the pass write out luma into alpha.
   Then blend into RGB only.
   FXAA should be able to run ok
   assuming the blending pass did not any add aliasing.
   This should be the common case for particles and common blending passes.

============================================================================*/

//------------------------------------------------------
// 1) Choose your FXAA Quality Preset (10..15, 20..29, or 39).
//    Example: 12 is a good default.  15 is higher quality.
//------------------------------------------------------
#ifndef FXAA_QUALITY__PRESET
#define FXAA_QUALITY__PRESET 12
#endif

//------------------------------------------------------
// 2) Basic config defines
//    We default to a PC/GLSL approach with the "quality" pipeline.
//------------------------------------------------------
#define FXAA_PC       1 // we want the PC-quality code
#define FXAA_GLSL_130 1 // assume modern-ish GLSL

#ifndef FXAA_EARLY_EXIT
//
// Controls algorithm's early exit path.
// On PS3 turning this ON adds 2 cycles to the shader.
// On 360 turning this OFF adds 10ths of a millisecond to the shader.
// Turning this off on console will result in a more blurry image.
// So this defaults to on.
//
// 1 = On.
// 0 = Off.
//
#define FXAA_EARLY_EXIT 1
#endif
/*--------------------------------------------------------------------------*/
#ifndef FXAA_DISCARD
//
// Only valid for PC OpenGL currently.
// Probably will not work when FXAA_GREEN_AS_LUMA = 1.
//
// 1 = Use discard on pixels which don't need AA.
//     For APIs which enable concurrent TEX+ROP from same surface.
// 0 = Return unchanged color on pixels which don't need AA.
//
#define FXAA_DISCARD 0
#endif

/*============================================================================
                         FXAA QUALITY - TUNING KNOBS
============================================================================*/
/*--------------------------------------------------------------------------*/
#ifndef FXAA_QUALITY__SUBPIX
//
// Choose the amount of sub-pixel aliasing removal.
//
// 1.00 = maximum (softer), 0.75 = default, 0.50 = sharper, etc.
//
#define FXAA_QUALITY__SUBPIX 1.00
#endif

#ifndef FXAA_QUALITY__EDGE_THRESHOLD
//
// The minimum amount of local contrast required to apply algorithm.
//
// 0.333 = faster, 0.166 = default, 0.125 = high quality, 0.063 = overkill
//
#define FXAA_QUALITY__EDGE_THRESHOLD 0.166
#endif
/*--------------------------------------------------------------------------*/
#ifndef FXAA_QUALITY__EDGE_THRESHOLD_MIN
//
// Trims the algorithm from processing darks.
//
// 0.0833 = default (start of visible unfiltered edges), 0.0625 = faster
//
#define FXAA_QUALITY__EDGE_THRESHOLD_MIN 0.0833
#endif

//------------------------------------------------------
// 3) Sampler for the scene color
//------------------------------------------------------
SAMPLER2D(s_input, 0);

//------------------------------------------------------
// 4) The original big chunk of FXAA code from "Fxaa3_11.h",
//    with only the PC/Quality branch retained. We'll paste
//    the relevant macros & the final "FxaaPixelShader" here.
//------------------------------------------------------
vec4 calc_luma_in_a(vec4 color)
{
    color.a = luma(color).x;
    return color;
}
// ---------------------------------------------------------------------
// These macros define how we sample textures in GLSL with LOD=0:
#if(FXAA_GLSL_120 == 1) || (FXAA_GLSL_130 == 1)
#define FxaaBool    bool
#define FxaaDiscard discard
#define FxaaFloat   float
#define FxaaFloat2  vec2
#define FxaaFloat3  vec3
#define FxaaFloat4  vec4
#define FxaaHalf    float
#define FxaaHalf2   vec2
#define FxaaHalf3   vec3
#define FxaaHalf4   vec4
#define FxaaInt2    ivec2
#define FxaaSat(x)  clamp(x, 0.0, 1.0)
#define FxaaTex     sampler2D

#define FxaaTexTop(t, p) calc_luma_in_a(texture2DLod(t, p, 0.0))
// we typically won't use offsets in modern GL unless we want integer-based fetch
#define FxaaTexOff(t, p, o, r) calc_luma_in_a(texture2DLod(t, p + (o * r), 0.0))
#else
// for older HLSL 3 or console, etc. - not used here.
#error "This example is for GLSL 130 or newer."
#endif

// ---------------------------------------------------------------------
// A small helper to get luma from alpha:
FxaaFloat FxaaLuma(FxaaFloat4 rgba)
{
    return rgba.w;
}

// ---------------------------------------------------------------------
// A block of #defines for the different "FXAA_QUALITY__PRESET" values.
// For more explanation, see the original Fxaa3_11.h.

/*============================================================================

                     FXAA QUALITY - PRESETS

============================================================================*/

/*============================================================================
                     FXAA QUALITY - MEDIUM DITHER PRESETS
============================================================================*/
#if(FXAA_QUALITY__PRESET == 10)
#define FXAA_QUALITY__PS 3
#define FXAA_QUALITY__P0 1.5
#define FXAA_QUALITY__P1 3.0
#define FXAA_QUALITY__P2 12.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 11)
#define FXAA_QUALITY__PS 4
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 3.0
#define FXAA_QUALITY__P3 12.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 12)
#define FXAA_QUALITY__PS 5
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 4.0
#define FXAA_QUALITY__P4 12.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 13)
#define FXAA_QUALITY__PS 6
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 4.0
#define FXAA_QUALITY__P5 12.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 14)
#define FXAA_QUALITY__PS 7
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 4.0
#define FXAA_QUALITY__P6 12.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 15)
#define FXAA_QUALITY__PS 8
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 2.0
#define FXAA_QUALITY__P6 4.0
#define FXAA_QUALITY__P7 12.0
#endif

/*============================================================================
                     FXAA QUALITY - LOW DITHER PRESETS
============================================================================*/
#if(FXAA_QUALITY__PRESET == 20)
#define FXAA_QUALITY__PS 3
#define FXAA_QUALITY__P0 1.5
#define FXAA_QUALITY__P1 2.0
#define FXAA_QUALITY__P2 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 21)
#define FXAA_QUALITY__PS 4
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 22)
#define FXAA_QUALITY__PS 5
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 23)
#define FXAA_QUALITY__PS 6
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 24)
#define FXAA_QUALITY__PS 7
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 3.0
#define FXAA_QUALITY__P6 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 25)
#define FXAA_QUALITY__PS 8
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 2.0
#define FXAA_QUALITY__P6 4.0
#define FXAA_QUALITY__P7 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 26)
#define FXAA_QUALITY__PS 9
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 2.0
#define FXAA_QUALITY__P6 2.0
#define FXAA_QUALITY__P7 4.0
#define FXAA_QUALITY__P8 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 27)
#define FXAA_QUALITY__PS 10
#define FXAA_QUALITY__P0 1.0
#define FXAA_QUALITY__P1 1.5
#define FXAA_QUALITY__P2 2.0
#define FXAA_QUALITY__P3 2.0
#define FXAA_QUALITY__P4 2.0
#define FXAA_QUALITY__P5 2.0
#define FXAA_QUALITY__P6 2.0
#define FXAA_QUALITY__P7 2.0
#define FXAA_QUALITY__P8 4.0
#define FXAA_QUALITY__P9 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 28)
#define FXAA_QUALITY__PS  11
#define FXAA_QUALITY__P0  1.0
#define FXAA_QUALITY__P1  1.5
#define FXAA_QUALITY__P2  2.0
#define FXAA_QUALITY__P3  2.0
#define FXAA_QUALITY__P4  2.0
#define FXAA_QUALITY__P5  2.0
#define FXAA_QUALITY__P6  2.0
#define FXAA_QUALITY__P7  2.0
#define FXAA_QUALITY__P8  2.0
#define FXAA_QUALITY__P9  4.0
#define FXAA_QUALITY__P10 8.0
#endif
/*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PRESET == 29)
#define FXAA_QUALITY__PS  12
#define FXAA_QUALITY__P0  1.0
#define FXAA_QUALITY__P1  1.5
#define FXAA_QUALITY__P2  2.0
#define FXAA_QUALITY__P3  2.0
#define FXAA_QUALITY__P4  2.0
#define FXAA_QUALITY__P5  2.0
#define FXAA_QUALITY__P6  2.0
#define FXAA_QUALITY__P7  2.0
#define FXAA_QUALITY__P8  2.0
#define FXAA_QUALITY__P9  2.0
#define FXAA_QUALITY__P10 4.0
#define FXAA_QUALITY__P11 8.0
#endif

/*============================================================================
                     FXAA QUALITY - EXTREME QUALITY
============================================================================*/
#if(FXAA_QUALITY__PRESET == 39)
#define FXAA_QUALITY__PS  12
#define FXAA_QUALITY__P0  1.0
#define FXAA_QUALITY__P1  1.0
#define FXAA_QUALITY__P2  1.0
#define FXAA_QUALITY__P3  1.0
#define FXAA_QUALITY__P4  1.0
#define FXAA_QUALITY__P5  1.5
#define FXAA_QUALITY__P6  2.0
#define FXAA_QUALITY__P7  2.0
#define FXAA_QUALITY__P8  2.0
#define FXAA_QUALITY__P9  2.0
#define FXAA_QUALITY__P10 4.0
#define FXAA_QUALITY__P11 8.0
#endif

//------------------------------------------------------
// 5) The primary function: FxaaPixelShader (Quality / PC).
//    We rely on some adjustable parameters.
//    Typically, you'd set them as constants or uniforms.
//    For simplicity, we put them in #defines below.
//------------------------------------------------------

// If you want dynamic uniforms, replace these #defines with uniform variables:

// -----------------------------------------------------------------------------
//  **ORIGINAL CODE** from the snippet, verbatim, inside #if (FXAA_PC == 1).
//  Do NOT omit anything. Just updated to GLSL syntax for bgfx.
// -----------------------------------------------------------------------------

#if(FXAA_PC == 1)
/*--------------------------------------------------------------------------*/
FxaaFloat4 FxaaPixelShader(
    // The center of the current pixel (e.g. v_texcoord0).
    // Use noperspective interpolation if possible.
    FxaaFloat2 pos,

    // The scene color texture.
    // {rgb_} = color in linear or perceptual color space
    // {___a} = luma in perceptual space
    FxaaTex tex,

    // From a uniform or constant:
    // {x} = 1.0 / screenWidth
    // {y} = 1.0 / screenHeight
    FxaaFloat2 fxaaQualityRcpFrame,

    // The sub-pixel amount. Controls how much aliasing to remove in sub-pixels.
    // 1.00 = maximum (softer), 0.75 = default, 0.50 = sharper, etc.
    FxaaFloat fxaaQualitySubpix,

    // Edge detection threshold. The minimum local contrast to apply the algorithm.
    // 0.333 = faster, 0.166 = default, 0.125 = high quality, 0.063 = overkill
    FxaaFloat fxaaQualityEdgeThreshold,

    // The dark edge threshold to ignore. Trims the algorithm from dark areas.
    // 0.0833 = default (start of visible unfiltered edges), 0.0625 = faster
    FxaaFloat fxaaQualityEdgeThresholdMin)
{
    //--------------------------------------------------------------------------
    FxaaFloat2 posM = pos;
#if(FXAA_GATHER4_ALPHA == 1)
#if(FXAA_DISCARD == 0)
    FxaaFloat4 rgbyM = FxaaTexTop(tex, posM);
    float lumaM = FxaaLuma(rgbyM);
#endif
    FxaaFloat4 luma4A = FxaaTexAlpha4(tex, posM);
    FxaaFloat4 luma4B = FxaaTexOffAlpha4(tex, posM, FxaaInt2(-1, -1));

#if(FXAA_DISCARD == 1)
#define lumaM luma4A.w
#endif
#define lumaE  luma4A.z
#define lumaS  luma4A.x
#define lumaSE luma4A.y
#define lumaNW luma4B.w
#define lumaN  luma4B.z
#define lumaW  luma4B.x
#else
    // Gather4 path is off or not used; sample center + neighbors
    FxaaFloat4 rgbyM = FxaaTexTop(tex, posM);
    float lumaM = FxaaLuma(rgbyM);
    FxaaFloat lumaS = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(0, 1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, 0), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaN = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(0, -1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 0), fxaaQualityRcpFrame.xy));
#endif
    //--------------------------------------------------------------------------
    FxaaFloat maxSM = max(lumaS, lumaM);
    FxaaFloat minSM = min(lumaS, lumaM);
    FxaaFloat maxESM = max(lumaE, maxSM);
    FxaaFloat minESM = min(lumaE, minSM);
    FxaaFloat maxWN = max(lumaN, lumaW);
    FxaaFloat minWN = min(lumaN, lumaW);
    FxaaFloat rangeMax = max(maxWN, maxESM);
    FxaaFloat rangeMin = min(minWN, minESM);
    FxaaFloat range = rangeMax - rangeMin;

    FxaaFloat rangeMaxScaled = rangeMax * fxaaQualityEdgeThreshold;
    FxaaFloat rangeMaxClamped = max(fxaaQualityEdgeThresholdMin, rangeMaxScaled);
    FxaaBool earlyExit = (range < rangeMaxClamped);

    if(earlyExit)
    {
#if(FXAA_DISCARD == 1)
        FxaaDiscard;
#else
        return rgbyM;
#endif
    }
    //--------------------------------------------------------------------------
#if(FXAA_GATHER4_ALPHA == 0)
    // Diagonal samples
    FxaaFloat lumaNW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, -1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaSE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, 1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaNE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, -1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaSW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 1), fxaaQualityRcpFrame.xy));
#else
    // If gather4 is used, lumaNE/lumaSW might come differently, but we skip details here.
    FxaaFloat lumaNE = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(1, -1), fxaaQualityRcpFrame.xy));
    FxaaFloat lumaSW = FxaaLuma(FxaaTexOff(tex, posM, FxaaInt2(-1, 1), fxaaQualityRcpFrame.xy));
#endif
    //--------------------------------------------------------------------------
    FxaaFloat lumaNS = lumaN + lumaS;
    FxaaFloat lumaWE = lumaW + lumaE;
    FxaaFloat subpixRcpRange = 1.0 / range;
    FxaaFloat subpixNSWE = lumaNS + lumaWE;

    FxaaFloat edgeHorz1 = (-2.0 * lumaM) + (lumaN + lumaS);
    FxaaFloat edgeVert1 = (-2.0 * lumaM) + (lumaW + lumaE);

    FxaaFloat lumaNESE = lumaNE + lumaSE;
    FxaaFloat lumaNWNE = lumaNW + lumaNE;
    FxaaFloat edgeHorz2 = (-2.0 * lumaE) + lumaNESE;
    FxaaFloat edgeVert2 = (-2.0 * lumaN) + lumaNWNE;

    FxaaFloat lumaNWSW = lumaNW + lumaSW;
    FxaaFloat lumaSWSE = lumaSW + lumaSE;
    FxaaFloat edgeHorz4 = (abs(edgeHorz1) * 2.0) + abs(edgeHorz2);
    FxaaFloat edgeVert4 = (abs(edgeVert1) * 2.0) + abs(edgeVert2);
    FxaaFloat edgeHorz3 = (-2.0 * lumaW) + lumaNWSW;
    FxaaFloat edgeVert3 = (-2.0 * lumaS) + lumaSWSE;
    FxaaFloat edgeHorz = abs(edgeHorz3) + edgeHorz4;
    FxaaFloat edgeVert = abs(edgeVert3) + edgeVert4;

    FxaaFloat subpixNWSWNESE = lumaNWSW + lumaNESE;
    FxaaFloat lengthSign = fxaaQualityRcpFrame.x;
    FxaaBool horzSpan = (edgeHorz >= edgeVert);
    FxaaFloat subpixA = (subpixNSWE * 2.0) + subpixNWSWNESE;

    if(!horzSpan)
        lumaN = lumaW;
    if(!horzSpan)
        lumaS = lumaE;
    if(horzSpan)
        lengthSign = fxaaQualityRcpFrame.y;

    FxaaFloat subpixB = (subpixA * (1.0 / 12.0)) - lumaM;

    FxaaFloat gradientN = (lumaN - lumaM);
    FxaaFloat gradientS = (lumaS - lumaM);
    FxaaFloat lumaNN = (lumaN + lumaM);
    FxaaFloat lumaSS = (lumaS + lumaM);
    FxaaBool pairN = abs(gradientN) >= abs(gradientS);
    FxaaFloat gradient = max(abs(gradientN), abs(gradientS));
    if(pairN)
        lengthSign = -lengthSign;

    FxaaFloat subpixC = FxaaSat(abs(subpixB) * subpixRcpRange);

    // offset 0.5 in the direction
    FxaaFloat2 posB = posM;
    if(!horzSpan)
        posB.x += (lengthSign * 0.5);
    else
        posB.y += (lengthSign * 0.5);

    // Step #1
    FxaaFloat2 offNP;
    offNP.x = (horzSpan) ? 0.0 : fxaaQualityRcpFrame.x;
    offNP.y = (horzSpan) ? fxaaQualityRcpFrame.y : 0.0;

    FxaaFloat2 posN = posB - offNP * FXAA_QUALITY__P0;
    FxaaFloat2 posP = posB + offNP * FXAA_QUALITY__P0;

    FxaaFloat subpixD = ((-2.0) * subpixC) + 3.0;
    FxaaFloat lumaEndN = FxaaLuma(FxaaTexTop(tex, posN));
    FxaaFloat subpixE = (subpixC * subpixC);
    FxaaFloat lumaEndP = FxaaLuma(FxaaTexTop(tex, posP));

    if(!pairN)
        lumaNN = lumaSS;
    FxaaFloat gradientScaled = (gradient * (1.0 / 4.0));
    FxaaFloat lumaMM = lumaM - (lumaNN * 0.5);
    FxaaFloat subpixF = subpixD * subpixE;
    FxaaBool lumaMLTZero = (lumaMM < 0.0);

    lumaEndN -= (lumaNN * 0.5);
    lumaEndP -= (lumaNN * 0.5);
    FxaaBool doneN = (abs(lumaEndN) >= gradientScaled);
    FxaaBool doneP = (abs(lumaEndP) >= gradientScaled);
    if(!doneN)
        posN.x -= offNP.x * FXAA_QUALITY__P1;
    if(!doneN)
        posN.y -= offNP.y * FXAA_QUALITY__P1;
    FxaaBool doneNP = (!doneN) || (!doneP);
    if(!doneP)
        posP.x += offNP.x * FXAA_QUALITY__P1;
    if(!doneP)
        posP.y += offNP.y * FXAA_QUALITY__P1;
    /*--------------------------------------------------------------------------*/
    if(doneNP)
    {
        if(!doneN)
            lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
        if(!doneP)
            lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
        if(!doneN)
            lumaEndN = lumaEndN - lumaNN * 0.5;
        if(!doneP)
            lumaEndP = lumaEndP - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if(!doneN)
            posN.x -= offNP.x * FXAA_QUALITY__P2;
        if(!doneN)
            posN.y -= offNP.y * FXAA_QUALITY__P2;
        doneNP = (!doneN) || (!doneP);
        if(!doneP)
            posP.x += offNP.x * FXAA_QUALITY__P2;
        if(!doneP)
            posP.y += offNP.y * FXAA_QUALITY__P2;
            /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 3)
        if(doneNP)
        {
            if(!doneN)
                lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
            if(!doneP)
                lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
            if(!doneN)
                lumaEndN = lumaEndN - lumaNN * 0.5;
            if(!doneP)
                lumaEndP = lumaEndP - lumaNN * 0.5;
            doneN = abs(lumaEndN) >= gradientScaled;
            doneP = abs(lumaEndP) >= gradientScaled;
            if(!doneN)
                posN.x -= offNP.x * FXAA_QUALITY__P3;
            if(!doneN)
                posN.y -= offNP.y * FXAA_QUALITY__P3;
            doneNP = (!doneN) || (!doneP);
            if(!doneP)
                posP.x += offNP.x * FXAA_QUALITY__P3;
            if(!doneP)
                posP.y += offNP.y * FXAA_QUALITY__P3;
                /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 4)
            if(doneNP)
            {
                if(!doneN)
                    lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                if(!doneP)
                    lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                if(!doneN)
                    lumaEndN = lumaEndN - lumaNN * 0.5;
                if(!doneP)
                    lumaEndP = lumaEndP - lumaNN * 0.5;
                doneN = abs(lumaEndN) >= gradientScaled;
                doneP = abs(lumaEndP) >= gradientScaled;
                if(!doneN)
                    posN.x -= offNP.x * FXAA_QUALITY__P4;
                if(!doneN)
                    posN.y -= offNP.y * FXAA_QUALITY__P4;
                doneNP = (!doneN) || (!doneP);
                if(!doneP)
                    posP.x += offNP.x * FXAA_QUALITY__P4;
                if(!doneP)
                    posP.y += offNP.y * FXAA_QUALITY__P4;
                    /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 5)
                if(doneNP)
                {
                    if(!doneN)
                        lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                    if(!doneP)
                        lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                    if(!doneN)
                        lumaEndN = lumaEndN - lumaNN * 0.5;
                    if(!doneP)
                        lumaEndP = lumaEndP - lumaNN * 0.5;
                    doneN = abs(lumaEndN) >= gradientScaled;
                    doneP = abs(lumaEndP) >= gradientScaled;
                    if(!doneN)
                        posN.x -= offNP.x * FXAA_QUALITY__P5;
                    if(!doneN)
                        posN.y -= offNP.y * FXAA_QUALITY__P5;
                    doneNP = (!doneN) || (!doneP);
                    if(!doneP)
                        posP.x += offNP.x * FXAA_QUALITY__P5;
                    if(!doneP)
                        posP.y += offNP.y * FXAA_QUALITY__P5;
                        /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 6)
                    if(doneNP)
                    {
                        if(!doneN)
                            lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                        if(!doneP)
                            lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                        if(!doneN)
                            lumaEndN = lumaEndN - lumaNN * 0.5;
                        if(!doneP)
                            lumaEndP = lumaEndP - lumaNN * 0.5;
                        doneN = abs(lumaEndN) >= gradientScaled;
                        doneP = abs(lumaEndP) >= gradientScaled;
                        if(!doneN)
                            posN.x -= offNP.x * FXAA_QUALITY__P6;
                        if(!doneN)
                            posN.y -= offNP.y * FXAA_QUALITY__P6;
                        doneNP = (!doneN) || (!doneP);
                        if(!doneP)
                            posP.x += offNP.x * FXAA_QUALITY__P6;
                        if(!doneP)
                            posP.y += offNP.y * FXAA_QUALITY__P6;
                            /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 7)
                        if(doneNP)
                        {
                            if(!doneN)
                                lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                            if(!doneP)
                                lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                            if(!doneN)
                                lumaEndN = lumaEndN - lumaNN * 0.5;
                            if(!doneP)
                                lumaEndP = lumaEndP - lumaNN * 0.5;
                            doneN = abs(lumaEndN) >= gradientScaled;
                            doneP = abs(lumaEndP) >= gradientScaled;
                            if(!doneN)
                                posN.x -= offNP.x * FXAA_QUALITY__P7;
                            if(!doneN)
                                posN.y -= offNP.y * FXAA_QUALITY__P7;
                            doneNP = (!doneN) || (!doneP);
                            if(!doneP)
                                posP.x += offNP.x * FXAA_QUALITY__P7;
                            if(!doneP)
                                posP.y += offNP.y * FXAA_QUALITY__P7;
                                /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 8)
                            if(doneNP)
                            {
                                if(!doneN)
                                    lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                if(!doneP)
                                    lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                if(!doneN)
                                    lumaEndN = lumaEndN - lumaNN * 0.5;
                                if(!doneP)
                                    lumaEndP = lumaEndP - lumaNN * 0.5;
                                doneN = abs(lumaEndN) >= gradientScaled;
                                doneP = abs(lumaEndP) >= gradientScaled;
                                if(!doneN)
                                    posN.x -= offNP.x * FXAA_QUALITY__P8;
                                if(!doneN)
                                    posN.y -= offNP.y * FXAA_QUALITY__P8;
                                doneNP = (!doneN) || (!doneP);
                                if(!doneP)
                                    posP.x += offNP.x * FXAA_QUALITY__P8;
                                if(!doneP)
                                    posP.y += offNP.y * FXAA_QUALITY__P8;
                                    /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 9)
                                if(doneNP)
                                {
                                    if(!doneN)
                                        lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                    if(!doneP)
                                        lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                    if(!doneN)
                                        lumaEndN = lumaEndN - lumaNN * 0.5;
                                    if(!doneP)
                                        lumaEndP = lumaEndP - lumaNN * 0.5;
                                    doneN = abs(lumaEndN) >= gradientScaled;
                                    doneP = abs(lumaEndP) >= gradientScaled;
                                    if(!doneN)
                                        posN.x -= offNP.x * FXAA_QUALITY__P9;
                                    if(!doneN)
                                        posN.y -= offNP.y * FXAA_QUALITY__P9;
                                    doneNP = (!doneN) || (!doneP);
                                    if(!doneP)
                                        posP.x += offNP.x * FXAA_QUALITY__P9;
                                    if(!doneP)
                                        posP.y += offNP.y * FXAA_QUALITY__P9;
                                        /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 10)
                                    if(doneNP)
                                    {
                                        if(!doneN)
                                            lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                        if(!doneP)
                                            lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                        if(!doneN)
                                            lumaEndN = lumaEndN - lumaNN * 0.5;
                                        if(!doneP)
                                            lumaEndP = lumaEndP - lumaNN * 0.5;
                                        doneN = abs(lumaEndN) >= gradientScaled;
                                        doneP = abs(lumaEndP) >= gradientScaled;
                                        if(!doneN)
                                            posN.x -= offNP.x * FXAA_QUALITY__P10;
                                        if(!doneN)
                                            posN.y -= offNP.y * FXAA_QUALITY__P10;
                                        doneNP = (!doneN) || (!doneP);
                                        if(!doneP)
                                            posP.x += offNP.x * FXAA_QUALITY__P10;
                                        if(!doneP)
                                            posP.y += offNP.y * FXAA_QUALITY__P10;
                                            /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 11)
                                        if(doneNP)
                                        {
                                            if(!doneN)
                                                lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                            if(!doneP)
                                                lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                            if(!doneN)
                                                lumaEndN = lumaEndN - lumaNN * 0.5;
                                            if(!doneP)
                                                lumaEndP = lumaEndP - lumaNN * 0.5;
                                            doneN = abs(lumaEndN) >= gradientScaled;
                                            doneP = abs(lumaEndP) >= gradientScaled;
                                            if(!doneN)
                                                posN.x -= offNP.x * FXAA_QUALITY__P11;
                                            if(!doneN)
                                                posN.y -= offNP.y * FXAA_QUALITY__P11;
                                            doneNP = (!doneN) || (!doneP);
                                            if(!doneP)
                                                posP.x += offNP.x * FXAA_QUALITY__P11;
                                            if(!doneP)
                                                posP.y += offNP.y * FXAA_QUALITY__P11;
                                                /*--------------------------------------------------------------------------*/
#if(FXAA_QUALITY__PS > 12)
                                            if(doneNP)
                                            {
                                                if(!doneN)
                                                    lumaEndN = FxaaLuma(FxaaTexTop(tex, posN.xy));
                                                if(!doneP)
                                                    lumaEndP = FxaaLuma(FxaaTexTop(tex, posP.xy));
                                                if(!doneN)
                                                    lumaEndN = lumaEndN - lumaNN * 0.5;
                                                if(!doneP)
                                                    lumaEndP = lumaEndP - lumaNN * 0.5;
                                                doneN = abs(lumaEndN) >= gradientScaled;
                                                doneP = abs(lumaEndP) >= gradientScaled;
                                                if(!doneN)
                                                    posN.x -= offNP.x * FXAA_QUALITY__P12;
                                                if(!doneN)
                                                    posN.y -= offNP.y * FXAA_QUALITY__P12;
                                                doneNP = (!doneN) || (!doneP);
                                                if(!doneP)
                                                    posP.x += offNP.x * FXAA_QUALITY__P12;
                                                if(!doneP)
                                                    posP.y += offNP.y * FXAA_QUALITY__P12;
                                                /*--------------------------------------------------------------------------*/
                                            }
#endif
                                            /*--------------------------------------------------------------------------*/
                                        }
#endif
                                        /*--------------------------------------------------------------------------*/
                                    }
#endif
                                    /*--------------------------------------------------------------------------*/
                                }
#endif
                                /*--------------------------------------------------------------------------*/
                            }
#endif
                            /*--------------------------------------------------------------------------*/
                        }
#endif
                        /*--------------------------------------------------------------------------*/
                    }
#endif
                    /*--------------------------------------------------------------------------*/
                }
#endif
                /*--------------------------------------------------------------------------*/
            }
#endif
            /*--------------------------------------------------------------------------*/
        }
#endif
        /*--------------------------------------------------------------------------*/
    }
    /*--------------------------------------------------------------------------*/

    // Finally we compute offset:
    FxaaFloat dstN = (horzSpan ? (posM.y - posN.y) : (posM.x - posN.x));
    FxaaFloat dstP = (horzSpan ? (posP.y - posM.y) : (posP.x - posM.x));
    FxaaBool goodSpanN = ((lumaEndN < 0.0) != lumaMLTZero);
    FxaaBool goodSpanP = ((lumaEndP < 0.0) != lumaMLTZero);
    FxaaFloat spanLength = (dstP + dstN);
    FxaaFloat spanLengthRcp = 1.0 / spanLength;

    FxaaBool directionN = (dstN < dstP);
    FxaaFloat dst = min(dstN, dstP);
    FxaaBool goodSpan = (directionN ? goodSpanN : goodSpanP);
    FxaaFloat subpixG = (subpixF * subpixF);
    FxaaFloat pixelOffset = ((dst * (-spanLengthRcp)) + 0.5);
    FxaaFloat subpixH = (subpixG * fxaaQualitySubpix);

    FxaaFloat pixelOffsetGood = (goodSpan ? pixelOffset : 0.0);
    FxaaFloat pixelOffsetSubpix = max(pixelOffsetGood, subpixH);

    if(!horzSpan)
        posM.x += (pixelOffsetSubpix * lengthSign);
    else
        posM.y += (pixelOffsetSubpix * lengthSign);

#if(FXAA_DISCARD == 1)
    return FxaaTexTop(tex, posM);
#else
    // Keep alpha = luma, or set alpha=1 if you prefer.
    return FxaaFloat4(FxaaTexTop(tex, posM).xyz, lumaM);
#endif
}
/*==========================================================================*/
#endif // (FXAA_PC == 1)

void main()
{
    // 1) The center of the pixel in [0..1] space:
    vec2 pos = v_texcoord0;

    // 2) Some of the arguments we can set to defaults or read from uniforms.
    //    For demonstration, we read from "u_viewTexel.xy" for (1/width, 1/height).
    //    Alternatively, if you prefer your own uniform, do that.
    vec2 fxaaQualityRcpFrame = u_viewTexel.xy;

    // 3) We'll read from our example uniforms for the "Quality" parameters:
    float fxaaQualitySubpix = FXAA_QUALITY__SUBPIX;
    float fxaaQualityEdgeThreshold = FXAA_QUALITY__EDGE_THRESHOLD;
    float fxaaQualityEdgeThresholdMin = FXAA_QUALITY__EDGE_THRESHOLD_MIN;

    // 4) Call FxaaPixelShader:
    vec4 fxaaColor = FxaaPixelShader(pos,
                                     s_input,
                                     fxaaQualityRcpFrame,
                                     fxaaQualitySubpix,
                                     fxaaQualityEdgeThreshold,
                                     fxaaQualityEdgeThresholdMin);
    fxaaColor.a = 1.0f;
    // Done
    gl_FragColor = fxaaColor;
}
