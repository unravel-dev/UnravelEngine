/*
 * Copyright 2013-2014 Dario Manesku. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "../common.sh"

#if BGFX_SHADER_LANGUAGE_GLSL
#	define CONST_ARRAY_BEGIN(_type, _name, _count) CONST(_type) _name[_count] = _type[](
#else
#	define CONST_ARRAY_BEGIN(_type, _name, _count) CONST(_type) _name[_count] = {
#endif // BGFX_SHADER_LANGUAGE_GLSL


#define BLOCKER_SEARCH_NUM_SAMPLES 16
#define PCF_LOD_OFFSET_NUM_SAMPLES 16


vec2 sampleVogelDisk(int index, int sampleCount)
{
    const float goldenAngle = 2.39996323; // radians
    float i = float(index);
    float r = sqrt((i + 0.5) / float(sampleCount));
    float a = i * goldenAngle;
    return vec2(cos(a), sin(a)) * r;
}

vec2 samplePoisson(int index)
{
	//return sampleVogelDisk(index, 10);
CONST_ARRAY_BEGIN(vec2, PoissonDistribution, 64)
    vec2(-0.94201624, -0.39906216),
    vec2(0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543, 0.27676845),
    vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554),
    vec2(0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507),
    vec2(-0.81409955, 0.91437590),
    vec2(0.19984126, 0.78641367),
    vec2(0.14383161, -0.14100790),
    vec2(-0.413923, -0.439757),
    vec2(-0.979153, -0.201245),
    vec2(-0.865579, -0.288695),
    vec2(-0.243704, -0.186378),
    vec2(-0.294920, -0.055748),
    vec2(-0.604452, -0.544251),
    vec2(-0.418056, -0.587679),
    vec2(-0.549156, -0.415877),
    vec2(-0.238080, -0.611761),
    vec2(-0.267004, -0.459702),
    vec2(-0.100006, -0.229116),
    vec2(-0.101928, -0.380382),
    vec2(-0.681467, -0.700773),
    vec2(-0.763488, -0.543386),
    vec2(-0.549030, -0.750749),
    vec2(-0.809045, -0.408738),
    vec2(-0.388134, -0.773448),
    vec2(-0.429392, -0.894892),
    vec2(-0.131597, 0.065058),
    vec2(-0.275002, 0.102922),
    vec2(-0.106117, -0.068327),
    vec2(-0.294586, -0.891515),
    vec2(-0.629418, 0.379387),
    vec2(-0.407257, 0.339748),
    vec2(0.071650, -0.384284),
    vec2(0.022018, -0.263793),
    vec2(0.003879, -0.136073),
    vec2(-0.137533, -0.767844),
    vec2(-0.050874, -0.906068),
    vec2(0.114133, -0.070053),
    vec2(0.163314, -0.217231),
    vec2(-0.100262, -0.587992),
    vec2(-0.004942, 0.125368),
    vec2(0.035302, -0.619310),
    vec2(0.195646, -0.459022),
    vec2(0.303969, -0.346362),
    vec2(-0.678118, 0.685099),
    vec2(-0.628418, 0.507978),
    vec2(-0.508473, 0.458753),
    vec2(0.032134, -0.782030),
    vec2(0.122595, 0.280353),
    vec2(-0.043643, 0.312119),
    vec2(0.132993, 0.085170),
    vec2(-0.192106, 0.285848),
    vec2(0.183621, -0.713242),
    vec2(0.265220, -0.596716),
    vec2(-0.009628, -0.483058),
    vec2(-0.018516, 0.435703)
ARRAY_END();


    return PoissonDistribution[int(mod(index,64))];
}

// Rotate a 2D sample by a precomputed sin/cos pair.
// _sincos = vec2(sin(angle), cos(angle))
vec2 rotateSample(vec2 _sample, vec2 _sincos)
{
    return vec2(_sample.x * _sincos.y - _sample.y * _sincos.x,
                _sample.x * _sincos.x + _sample.y * _sincos.y);
}

// Interleaved gradient noise for per-pixel Poisson disk rotation.
// Produces well-distributed noise that avoids the clustering artifacts of
// traditional fract(sin(...)) hashes. Converts structured Poisson banding
// into smooth, perceptually-uniform noise.
// Source: "Next Generation Post Processing in Call of Duty: AW" (Jimenez 2014)
float interleavedGradientNoise(vec2 _screenPos)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(_screenPos, magic.xy)));
}

float texcoordInRange(vec2 _texcoord)
{
    bool inRange = all(greaterThan(_texcoord, vec2_splat(0.0)))
                && all(lessThan   (_texcoord, vec2_splat(1.0)))
                 ;

    return float(inRange);
}

float hardShadowLod(sampler2D _sampler, float lod, vec4 _shadowCoord, float _bias)
{
    vec2 texCoord = _shadowCoord.xy/_shadowCoord.w;

    bool outside = any(greaterThan(texCoord, vec2_splat(1.0)))
                || any(lessThan   (texCoord, vec2_splat(0.0)))
                 ;

    if (outside)
    {
        return 1.0;
    }

    float receiver = (_shadowCoord.z-_bias)/_shadowCoord.w;
    float occluder = unpackRgbaToFloat(texture2DLod(_sampler, texCoord, lod) );

    float visibility = step(receiver, occluder);
    return visibility;
}

float hardShadow(sampler2D _sampler, vec4 _shadowCoord, float _bias)
{
    return hardShadowLod(_sampler, 0.0, _shadowCoord, _bias);
}


// _diskRotation = vec2(sin(angle), cos(angle)) for per-pixel Poisson disk rotation.
// Pass vec2(0.0, 1.0) for no rotation (identity).
float PCFLodOffset(sampler2D _sampler, float lod, vec2 offset, vec4 _shadowCoord, float _bias, vec4 _pcfParams, vec2 _texelSize, vec2 _diskRotation)
{
    float result = 0.0;

    for ( int i = 0; i < PCF_LOD_OFFSET_NUM_SAMPLES; ++i )
    {
        vec2 jitteredOffset = rotateSample(samplePoisson(i), _diskRotation) * offset;
        result += hardShadowLod(_sampler, lod, _shadowCoord + vec4(jitteredOffset, 0.0, 0.0), _bias);
    }
    return result / float(PCF_LOD_OFFSET_NUM_SAMPLES);
}

float PCFLod(sampler2D _sampler, float lod, vec2 filterRadius, vec4 _shadowCoord, float _bias, vec4 _pcfParams, vec2 _texelSize, vec2 _diskRotation)
{
    vec2 offset = filterRadius * _pcfParams.zw * _texelSize * _shadowCoord.w;

    return PCFLodOffset(_sampler, lod, offset, _shadowCoord, _bias, _pcfParams, _texelSize, _diskRotation);
}

float PCF(sampler2D _sampler, vec4 _shadowCoord, float _bias, vec4 _pcfParams, vec2 _texelSize, vec2 fragCoord)
{
    // Per-pixel Poisson disk rotation from shadow map texel coordinates
    vec2 noiseCoord = fragCoord;//(_shadowCoord.xy / _shadowCoord.w) * (1.0 / _texelSize.x);
    float angle = interleavedGradientNoise(noiseCoord) * 6.283185;
    vec2 diskRotation = vec2(sin(angle), cos(angle));
    return PCFLod(_sampler, 0.0, vec2(2.0, 2.0), _shadowCoord, _bias, _pcfParams, _texelSize, diskRotation);
}

float VSM(sampler2D _sampler, vec4 _shadowCoord, float _bias, float _depthMultiplier, float _minVariance)
{
    vec2 texCoord = _shadowCoord.xy/_shadowCoord.w;

    bool outside = any(greaterThan(texCoord, vec2_splat(1.0)))
                || any(lessThan   (texCoord, vec2_splat(0.0)))
                 ;

    if (outside)
    {
        return 1.0;
    }

    float receiver = (_shadowCoord.z-_bias)/_shadowCoord.w * _depthMultiplier;
    vec4 rgba = texture2D(_sampler, texCoord);
    vec2 occluder = vec2(unpackHalfFloat(rgba.rg), unpackHalfFloat(rgba.ba)) * _depthMultiplier;

    if (receiver < occluder.x)
    {
        return 1.0;
    }

    float variance = max(occluder.y - (occluder.x*occluder.x), _minVariance);
    float d = receiver - occluder.x;

    float visibility = variance / (variance + d*d);

    return visibility;
}

float ESM(sampler2D _sampler, vec4 _shadowCoord, float _bias, float _depthMultiplier)
{
    vec2 texCoord = _shadowCoord.xy/_shadowCoord.w;

    bool outside = any(greaterThan(texCoord, vec2_splat(1.0)))
                || any(lessThan   (texCoord, vec2_splat(0.0)))
                 ;

    if (outside)
    {
        return 1.0;
    }

    float receiver = (_shadowCoord.z-_bias)/_shadowCoord.w;
    float occluder = unpackRgbaToFloat(texture2D(_sampler, texCoord) );

    float visibility = clamp(exp(_depthMultiplier * (occluder-receiver) ), 0.0, 1.0);

    return visibility;
}


vec4 blur9(sampler2D _sampler, vec2 _uv0, vec4 _uv1, vec4 _uv2, vec4 _uv3, vec4 _uv4)
{
#define _BLUR9_WEIGHT_0 1.0
#define _BLUR9_WEIGHT_1 0.9
#define _BLUR9_WEIGHT_2 0.55
#define _BLUR9_WEIGHT_3 0.18
#define _BLUR9_WEIGHT_4 0.1
#define _BLUR9_NORMALIZE (_BLUR9_WEIGHT_0+2.0*(_BLUR9_WEIGHT_1+_BLUR9_WEIGHT_2+_BLUR9_WEIGHT_3+_BLUR9_WEIGHT_4) )
#define BLUR9_WEIGHT(_x) (_BLUR9_WEIGHT_##_x/_BLUR9_NORMALIZE)

    float blur;
    blur  = unpackRgbaToFloat(texture2D(_sampler, _uv0)    * BLUR9_WEIGHT(0));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv1.xy) * BLUR9_WEIGHT(1));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv1.zw) * BLUR9_WEIGHT(1));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv2.xy) * BLUR9_WEIGHT(2));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv2.zw) * BLUR9_WEIGHT(2));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv3.xy) * BLUR9_WEIGHT(3));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv3.zw) * BLUR9_WEIGHT(3));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv4.xy) * BLUR9_WEIGHT(4));
    blur += unpackRgbaToFloat(texture2D(_sampler, _uv4.zw) * BLUR9_WEIGHT(4));
    return packFloatToRgba(blur);
}

vec4 blur9VSM(sampler2D _sampler, vec2 _uv0, vec4 _uv1, vec4 _uv2, vec4 _uv3, vec4 _uv4)
{
#define _BLUR9_WEIGHT_0 1.0
#define _BLUR9_WEIGHT_1 0.9
#define _BLUR9_WEIGHT_2 0.55
#define _BLUR9_WEIGHT_3 0.18
#define _BLUR9_WEIGHT_4 0.1
#define _BLUR9_NORMALIZE (_BLUR9_WEIGHT_0+2.0*(_BLUR9_WEIGHT_1+_BLUR9_WEIGHT_2+_BLUR9_WEIGHT_3+_BLUR9_WEIGHT_4) )
#define BLUR9_WEIGHT(_x) (_BLUR9_WEIGHT_##_x/_BLUR9_NORMALIZE)

    vec2 blur;
    vec4 val;
    val = texture2D(_sampler, _uv0) * BLUR9_WEIGHT(0);
    blur = vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv1.xy) * BLUR9_WEIGHT(1);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv1.zw) * BLUR9_WEIGHT(1);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv2.xy) * BLUR9_WEIGHT(2);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv2.zw) * BLUR9_WEIGHT(2);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv3.xy) * BLUR9_WEIGHT(3);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv3.zw) * BLUR9_WEIGHT(3);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv4.xy) * BLUR9_WEIGHT(4);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));
    val = texture2D(_sampler, _uv4.zw) * BLUR9_WEIGHT(4);
    blur += vec2(unpackHalfFloat(val.rg), unpackHalfFloat(val.ba));

    return vec4(packHalfFloat(blur.x), packHalfFloat(blur.y));
}


// Returns vec3(avgBlockerDepth, closestBlockerDepth, blockerRatio)
//   avgBlockerDepth:     mean depth of all blockers found (for general penumbra)
//   closestBlockerDepth: maximum depth among blockers (nearest to receiver, for contact hardening)
//   blockerRatio:        fraction of search samples that found a blocker [0..1]
// _diskRotation = vec2(sin(angle), cos(angle)) for per-pixel Poisson disk rotation
vec3 findBlocker(sampler2D _sampler, vec4 _shadowCoord, vec2 _searchSize, float _bias, vec2 _diskRotation)
{
    int blockerCount = 0;
    float avgBlockerDepth = 0.0;
    float closestBlockerDepth = 0.0;
    vec2 texCoord = _shadowCoord.xy / _shadowCoord.w;
    float receiverDepth = (_shadowCoord.z / _shadowCoord.w) - _bias;

    // Search around the shadow coordinate to find blockers
    for( int i = 0; i < BLOCKER_SEARCH_NUM_SAMPLES; ++i )
    {
        vec2 offset = rotateSample(samplePoisson(i), _diskRotation) * _searchSize;
        float shadowMapDepth = unpackRgbaToFloat(texture2D(_sampler, texCoord + offset));
        if (shadowMapDepth < receiverDepth)
        {
            avgBlockerDepth += shadowMapDepth;
            closestBlockerDepth = max(closestBlockerDepth, shadowMapDepth);
            blockerCount++;
        }
    }

    // Calculate average blocker depth
    if (blockerCount > 0)
    {
        avgBlockerDepth /= float(blockerCount);
    }
    else
    {
        avgBlockerDepth = -1.0; // No blockers found
    }

    float blockerRatio = float(blockerCount) / float(BLOCKER_SEARCH_NUM_SAMPLES);
    return vec3(avgBlockerDepth, closestBlockerDepth, blockerRatio);
}



float PCSS(sampler2D _sampler, vec4 _shadowCoord, float _bias, vec4 _pcfParams, vec2 _texelSize, vec2 fragCoord)
{
    // -----------------------------------------------------------------------
    // PCSS Parameters
    // -----------------------------------------------------------------------

    // Blocker search radius in UV space (~10 texels on 1024 map).
    float searchRadiusUV = 0.01;

    // Penumbra scale. Amplifies the squared depth ratio into a UV-space
    // filter radius. Higher = softer shadows at distance.
    float penumbraScale = 4.0;

    // Maximum filter radius in UV space (~50 texels on 1024 map).
    float maxFilterRadius = 0.05;

    // -----------------------------------------------------------------------
    // Per-pixel Poisson disk rotation (Interleaved Gradient Noise)
    // -----------------------------------------------------------------------
    vec2 noiseCoord = fragCoord;//(_shadowCoord.xy / _shadowCoord.w) * (1.0 / _texelSize.x);
    float noise = interleavedGradientNoise(noiseCoord);
    float rotationAngle = noise * 6.283185;
    vec2 diskRotation = vec2(sin(rotationAngle), cos(rotationAngle));

    // Receiver depth in normalized shadow map space
    float receiverDepth = (_shadowCoord.z / _shadowCoord.w) - _bias;

    // -----------------------------------------------------------------------
    // Step 1: Blocker Search
    // -----------------------------------------------------------------------
    vec3 blockerResult = findBlocker(_sampler, _shadowCoord, vec2(searchRadiusUV, searchRadiusUV), _bias, diskRotation);
    float avgBlockerDepth = blockerResult.x;
    float blockerRatio = blockerResult.z;

    if (avgBlockerDepth < -0.99)
    {
        return 1.0;
    }

    // -----------------------------------------------------------------------
    // Step 2: Penumbra Estimation (standard PCSS, Fernando 2005)
    //
    //   penumbraWidth = lightSize * (d_receiver - d_blocker) / d_blocker
    //
    // The depth gap at contact equals the object's thickness along the
    // light direction. For curved objects (spheres, characters), the gap
    // varies across the shadow giving the contact-hardening gradient.
    // For flat objects (cubes), the gap is constant → uniform penumbra.
    // -----------------------------------------------------------------------
    float penumbraWidth = penumbraScale * max(0.0, receiverDepth - avgBlockerDepth) / avgBlockerDepth;
    float filterRadiusUV = clamp(penumbraWidth, 0.0, maxFilterRadius);

    // -----------------------------------------------------------------------
    // Step 3: Percentage-Closer Filtering
    // -----------------------------------------------------------------------
    float visibility = PCFLodOffset(_sampler, 0.0, vec2(filterRadiusUV, filterRadiusUV), _shadowCoord, _bias, _pcfParams, _texelSize, diskRotation);

    // -----------------------------------------------------------------------
    // Step 4: Edge fade based on blocker ratio
    // -----------------------------------------------------------------------
    float edgeFade = smoothstep(0.0, 0.25, blockerRatio);
    visibility = mix(1.0, visibility, edgeFade);

    return visibility;
}
