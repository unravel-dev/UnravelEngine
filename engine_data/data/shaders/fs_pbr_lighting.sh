#ifndef __PBRLIGHTING_SH__
#define __PBRLIGHTING_SH__

#include "common.sh"
#include "lighting.sh"
#include "shadowmaps/common_shadow.sh"

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
SAMPLER2D(s_tex5, 5);
SAMPLER2D(s_tex6, 6);

uniform vec4 u_light_position;
uniform vec4 u_light_direction;
uniform vec4 u_light_color_intensity;
uniform vec4 u_light_data;
/// Contact shadows: x = minimum occluder thickness (world units), y = max distance (0 = off),
/// z = opacity, w = temporal frame index for the dither (0 when no TAA integrates it).
uniform vec4 u_contact_shadow;
uniform vec4 u_camera_position;

#if DIRECTIONAL_LIGHT
// Cloud shadow map (atmospherics/fs_cloud_shadow.sc): sun transmittance of the cloud layer,
// one texel per entry point of the sun ray at the layer base. xy = map origin (world xz),
// z = 1 / extent, w = opacity.
uniform vec4 u_cloudShadow;
// x = enabled, y = layer base world y, z = border fade width (map space), w = unused.
uniform vec4 u_cloudShadow2;
SAMPLER2D(s_cloudShadow, 11);
#endif

#if PBR_INDIRECT
SAMPLER2D(s_irradiance, 7);
SAMPLER2D(s_ssil, 8);
// GTAO: rgb = world bent normal * 0.5 + 0.5, a = visibility (gtao_pass).
SAMPLER2D(s_gtao, 9);
#else
SAMPLER2D(s_shadowMap0, 7);
SAMPLER2D(s_shadowMap1, 8);
SAMPLER2D(s_shadowMap2, 9);
SAMPLER2D(s_shadowMap3, 10);
#endif

// x = GTAO bound (0/1), y = bent normal strength, z = intensity, w = multi-bounce (0/1).
uniform vec4 u_gtao_params;
uniform vec4 u_params0;
uniform vec4 u_params1;
// u_params2 (texel size, coverage) is declared by shadowmaps/common_shadow.sh.

uniform vec4 u_smSamplingParams;
uniform vec4 u_csmFarDistances;
/// x = slope-scaled bias in texels per unit slope, y = d(stored depth)/d(world depth) for ortho
/// and linear maps, z = the same numerator for perspective 1/z maps (divided by w^2 here),
/// w = z for the vertical tetrahedron faces of a stencil-packed point light.
uniform vec4 u_shadowBiasParams;
/// World size of one texel per cascade (directional lights).
uniform vec4 u_csmTexelWorld;
/// Spot / point: x = texel world size per unit distance along the light axis,
/// y / z = map width / height per unit distance.
uniform vec4 u_perspTexelParams;
/// World-space unit vectors along +u / +v of the map (zero = receiver-plane term off).
uniform vec4 u_shadowAxisU;
uniform vec4 u_shadowAxisV;

#if SM_OMNI
uniform vec4 u_tetraNormalGreen;
uniform vec4 u_tetraNormalYellow;
uniform vec4 u_tetraNormalBlue;
uniform vec4 u_tetraNormalRed;
#endif


uniform mat4 u_lightMtx;
uniform mat4 u_shadowMapMtx0;
uniform mat4 u_shadowMapMtx1;
uniform mat4 u_shadowMapMtx2;
uniform mat4 u_shadowMapMtx3;

#define u_numSplits   u_params0.w

#define u_shadowMapBias   u_params1.x
#define u_shadowMapOffset u_params1.y
#define u_shadowMapParam0 u_params1.z
#define u_shadowMapParam1 u_params1.w

#define u_shadowMapShowCoverage u_params2.y



// Pcf
#define u_shadowMapPcfMode     u_shadowMapParam0
#define u_shadowMapNoiseAmount u_shadowMapParam1

// Vsm
#define u_shadowMapMinVariance     u_shadowMapParam0
#define u_shadowMapDepthMultiplier u_shadowMapParam1

// Esm
#define u_shadowMapHardness        u_shadowMapParam0
#define u_shadowMapDepthMultiplier u_shadowMapParam1
#define u_shadowSlopeBias u_shadowBiasParams.x

// NdotL-scaled normal offset: returns the scale factor for the normal offset.
// Zero when the surface faces the light (preserving ground contact),
// maximum at grazing angles (preventing acne). Uses sin(lightAngle).
float calculateNormalOffsetScale(float _NdotL)
{
    return sqrt(1.0 - _NdotL * _NdotL);
}

// Bias model, all on the receiver side and all in shadow-map texels of the map that answers
// (the world size of one texel of the sampled cascade, or of the map at the receiver's
// distance for spot and point lights), so one set of values holds at every cascade, distance
// and resolution:
//   constant       u_shadowMapBias   * texel               along the light
//   slope-scaled   u_shadowSlopeBias * texel * tan(theta)  along the light, tan clamped
//   normal offset  u_shadowMapOffset * texel * sin(theta)  along the normal, before projecting
//   receiver plane every filter tap is compared against the receiver's own plane at that tap
// theta = angle between the surface normal and the light. Receiver-plane depth bias from
// ddx/ddy of the shadow coords is not usable here: deferred lighting runs as a fullscreen pass
// and derivatives are across unrelated screen neighbors; the plane comes from the G-buffer
// normal instead. The constant and slope terms only have to cover the sub-texel rasterization
// error; the plane term makes the kernel exact on planar receivers at any radius.
#define SHADOW_MAX_SLOPE 8.0
// The last cascade fades to unshadowed over the outer part of the shadow distance.
#define SHADOW_CASCADE_FADE_START 0.9
// Receiver position precision: relative probe step for the depth-buffer finite difference,
// the float32 relative ulp, and the ulps of margin granted.
#define SHADOW_DEPTH_PROBE_STEP 1.0e-6
#define SHADOW_DEPTH_FLOAT_ULP 1.2e-7
#define SHADOW_DEPTH_ULPS 2.0

struct ShadowReceiver
{
    float sinT;      // sin(theta): normal offset scale
    float tanT;      // tan(theta), clamped: slope bias scale
    float invNdotL;  // 1 / NdotL, clamped: receiver-plane gradient scale
    float nU;        // N . map axis u
    float nV;        // N . map axis v
    float positionError; // receiver position uncertainty along its normal, world units (GLSL reserves 'precision')
};

// precisionNormal: how far the reconstructed receiver position can be off along its normal
// (the depth-buffer quantization projected on N, see pbr_light). A plane compare only feels
// that component, amplified by 1 / NdotL like every depth term.
ShadowReceiver makeShadowReceiver(vec3 N, vec3 L, float precisionNormal)
{
    ShadowReceiver r;
    float NdotL = saturate(dot(N, L));
    r.sinT = calculateNormalOffsetScale(NdotL);
    r.invNdotL = 1.0 / max(NdotL, 1.0 / SHADOW_MAX_SLOPE);
    r.tanT = min(r.sinT * r.invNdotL, SHADOW_MAX_SLOPE);
    r.nU = dot(N, u_shadowAxisU.xyz);
    r.nV = dot(N, u_shadowAxisV.xyz);
    r.positionError = precisionNormal;
    return r;
}

// Constant + slope bias (texels) plus the position-precision term (world), in stored depth;
// depthScale = d(stored depth)/d(world depth). The precision term is what lets a receiver far
// from the camera sample a near cascade's fine map (the smallest crop that contains it) without
// acne: it does not depend on the texel, only on how well the receiver itself is known.
float shadowDepthBias(ShadowReceiver r, float texelWorld, float depthScale)
{
    return ((u_shadowMapBias + u_shadowSlopeBias * r.tanT) * texelWorld + r.positionError * r.invNdotL) * depthScale;
}

// Change of stored depth per unit of map UV along the receiver plane; mapExtent = world size
// of the map along u and v at the receiver. A tap displaced by t on the plane sits
// (N . t) / (N . L) farther from the light.
vec2 shadowPlaneGradient(ShadowReceiver r, vec2 mapExtent, float depthScale)
{
    return vec2(r.nU, r.nV) * mapExtent * (r.invNdotL * depthScale);
}

float computeVisibility(sampler2D _sampler
                      , vec4 _shadowCoord
                      , float _bias
                      , vec2 _planeGrad
                      , vec4 _samplingParams
                      , vec2 _texelSize
                      , float _depthMultiplier
                      , float _minVariance
                      , float _hardness
                      , vec2 _fragCoord
                      , float _cascadeScale
                      )
{
    float visibility = 1.0f;
    float minRadiusUV = SHADOW_MIN_FILTER_RADIUS_TEXELS * u_shadowMapTexelSize;

#if SM_LINEAR
    vec4 shadowcoord = vec4(_shadowCoord.xy / _shadowCoord.w, _shadowCoord.z, 1.0);
#else
    vec4 shadowcoord = _shadowCoord;
#endif

#if SM_HARD
    visibility = hardShadow(_sampler, shadowcoord, _bias, _planeGrad);
#elif SM_PCF
    visibility = PCF(_sampler, shadowcoord, _bias, _planeGrad, _samplingParams, _texelSize, _fragCoord, _cascadeScale, minRadiusUV);
#elif SM_PCSS
    visibility = PCSS(_sampler, shadowcoord, _bias, _planeGrad, _samplingParams, _texelSize, _fragCoord, _cascadeScale, minRadiusUV);
#elif SM_VSM
    visibility = VSM(_sampler, shadowcoord, _bias, _depthMultiplier, _minVariance);
#elif SM_ESM
    visibility = ESM(_sampler, shadowcoord, _bias, _depthMultiplier * _hardness);
#endif

    return visibility;
}

#if SM_CSM
// One cascade: offset the receiver along its normal by this cascade's texel, project, bias, filter.
float csmCascadeVisibility(sampler2D _sampler, mat4 _mtx, float _texelWorld, float _cascadeScale, vec2 _texelSize,
                           vec3 _worldPos, vec3 _N, ShadowReceiver _r, vec2 _fragCoord)
{
    vec4 wpos = vec4(_worldPos + _N * (u_shadowMapOffset * _texelWorld * _r.sinT), 1.0);
    vec4 shadowcoord = mul(_mtx, wpos);
#if SM_LINEAR
    shadowcoord.z += 0.5;
#endif
    float depthScale = u_shadowBiasParams.y;
    float mapExtent = _texelWorld / u_shadowMapTexelSize;
    float bias = shadowDepthBias(_r, _texelWorld, depthScale);
    vec2 planeGrad = shadowPlaneGradient(_r, vec2_splat(mapExtent), depthScale);
    return computeVisibility(_sampler
                           , shadowcoord
                           , bias
                           , planeGrad
                           , u_smSamplingParams
                           , _texelSize
                           , u_shadowMapDepthMultiplier
                           , u_shadowMapMinVariance
                           , u_shadowMapHardness
                           , _fragCoord
                           , _cascadeScale
                           );
}
#endif


float CalculateSurfaceShadow(vec3 world_position, vec3 world_normal, vec3 light_dir, float precisionNormal, vec2 fragCoord, out vec3 colorCoverage)
{
    float visibility = 1.0f;
    colorCoverage = vec3(0.0f, 0.0f, 0.0f);

#if SM_NOOP
    // No operation
#else
    ShadowReceiver receiver = makeShadowReceiver(world_normal, light_dir, precisionNormal);
    vec4 wpos = vec4(world_position, 1.0);

#if SM_CSM
    vec2 texelSize = vec2_splat(u_shadowMapTexelSize);

    // The cascade is chosen from the unoffset position (the normal offset is texels and would
    // not change the choice); the chosen cascade then offsets and projects on its own.
    vec4 v_texcoord1 = mul(u_shadowMapMtx0, wpos);
    vec4 v_texcoord2 = mul(u_shadowMapMtx1, wpos);
    vec4 v_texcoord3 = mul(u_shadowMapMtx2, wpos);
    vec4 v_texcoord4 = mul(u_shadowMapMtx3, wpos);

    vec2 texcoord1 = v_texcoord1.xy/v_texcoord1.w;
    vec2 texcoord2 = v_texcoord2.xy/v_texcoord2.w;
    vec2 texcoord3 = v_texcoord3.xy/v_texcoord3.w;
    vec2 texcoord4 = v_texcoord4.xy/v_texcoord4.w;

    // The smallest cascade whose crop contains the receiver answers (xy inside the crop, depth
    // inside the map's range). This is the contract the shadow pass's nested-cascade caster
    // culling relies on (shadow.cpp): whatever can shadow a receiver of crop j lies in the same
    // light-space column and is drawn into map j. The crops are as deep as the shadow range, so
    // a near cascade's crop also answers for ground far from the camera at low sun - with the
    // nearest map's full resolution; what that receiver needs is a bias for its own position
    // precision (ShadowReceiver.positionError), not a coarser cascade.
    float viewDepth = mul(u_view, wpos).z;
#if SM_LINEAR
    const float zBase = 0.5;
#else
    const float zBase = 0.0;
#endif
    float z1 = v_texcoord1.z + zBase;
    float z2 = v_texcoord2.z + zBase;
    float z3 = v_texcoord3.z + zBase;
    float z4 = v_texcoord4.z + zBase;

	bool selection0 = all(lessThan(texcoord1, vec2_splat(0.99))) && all(greaterThan(texcoord1, vec2_splat(0.01))) && z1 > 0.0 && z1 < 1.0;
	bool selection1 = all(lessThan(texcoord2, vec2_splat(0.99))) && all(greaterThan(texcoord2, vec2_splat(0.01))) && z2 > 0.0 && z2 < 1.0;
	bool selection2 = all(lessThan(texcoord3, vec2_splat(0.99))) && all(greaterThan(texcoord3, vec2_splat(0.01))) && z3 > 0.0 && z3 < 1.0;
	bool selection3 = all(lessThan(texcoord4, vec2_splat(0.99))) && all(greaterThan(texcoord4, vec2_splat(0.01))) && z4 > 0.0 && z4 < 1.0;

    if (selection0)
    {
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.x, 0.001);

        float coverage = texcoordInRange(texcoord1) * 0.4;
        colorCoverage = vec3(-coverage, coverage, -coverage);
        visibility = csmCascadeVisibility(s_shadowMap0, u_shadowMapMtx0, u_csmTexelWorld.x, cascadeScale, texelSize,
                                          world_position, world_normal, receiver, fragCoord);
    }
    else if (selection1 && u_numSplits > 1)
    {
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.y, 0.001);

        float coverage = texcoordInRange(texcoord2) * 0.4;
        colorCoverage = vec3(coverage, coverage, -coverage);
        visibility = csmCascadeVisibility(s_shadowMap1, u_shadowMapMtx1, u_csmTexelWorld.y, cascadeScale, texelSize/2.0,
                                          world_position, world_normal, receiver, fragCoord);
    }
    else if (selection2 && u_numSplits > 2)
    {
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.z, 0.001);

        float coverage = texcoordInRange(texcoord3) * 0.4;
        colorCoverage = vec3(-coverage, -coverage, coverage);
        visibility = csmCascadeVisibility(s_shadowMap2, u_shadowMapMtx2, u_csmTexelWorld.z, cascadeScale, texelSize/3.0,
                                          world_position, world_normal, receiver, fragCoord);
    }
    else if (selection3 && u_numSplits > 3)
    {
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.w, 0.001);

        float coverage = texcoordInRange(texcoord4) * 0.4;
        colorCoverage = vec3(coverage, -coverage, -coverage);
        visibility = csmCascadeVisibility(s_shadowMap3, u_shadowMapMtx3, u_csmTexelWorld.w, cascadeScale, texelSize/4.0,
                                          world_position, world_normal, receiver, fragCoord);
    }

    // Shadow distance: the last cascade fades out over its outer part instead of ending on the
    // edge of its crop.
    float farLast = u_csmFarDistances.x;
    if (u_numSplits > 1) farLast = u_csmFarDistances.y;
    if (u_numSplits > 2) farLast = u_csmFarDistances.z;
    if (u_numSplits > 3) farLast = u_csmFarDistances.w;
    float distanceFade = smoothstep(farLast * SHADOW_CASCADE_FADE_START, farLast, viewDepth);
    visibility = mix(visibility, 1.0, distanceFade);
#elif SM_OMNI
    vec2 texelSize = vec2_splat(u_shadowMapTexelSize/4.0);

    vec4 v_shadowcoord = mul(u_lightMtx, wpos);

    vec4 faceSelection;
	vec3 pos = v_shadowcoord.xyz;
    faceSelection.x = dot(u_tetraNormalGreen.xyz,  pos);
    faceSelection.y = dot(u_tetraNormalYellow.xyz, pos);
    faceSelection.z = dot(u_tetraNormalBlue.xyz,   pos);
    faceSelection.w = dot(u_tetraNormalRed.xyz,    pos);

    mat4 faceMtx = u_shadowMapMtx0;
    float depthNumerator = u_shadowBiasParams.z;
    vec3 coverageSign = vec3(-1.0, 1.0, -1.0);
    float faceMax = max(max(faceSelection.x, faceSelection.y), max(faceSelection.z, faceSelection.w));
    if (faceSelection.y == faceMax)
    {
        faceMtx = u_shadowMapMtx1;
        coverageSign = vec3(1.0, 1.0, -1.0);
    }
    else if (faceSelection.z == faceMax)
    {
        faceMtx = u_shadowMapMtx2;
        depthNumerator = u_shadowBiasParams.w;
        coverageSign = vec3(-1.0, -1.0, 1.0);
    }
    else if (faceSelection.w == faceMax)
    {
        faceMtx = u_shadowMapMtx3;
        depthNumerator = u_shadowBiasParams.w;
        coverageSign = vec3(1.0, -1.0, -1.0);
    }

    // Texel size at the receiver's distance along the face axis, from the unoffset projection.
    vec4 unoffset = mul(faceMtx, v_shadowcoord);
    float texelWorld = unoffset.w * u_perspTexelParams.x;
    vec4 wposOffset = vec4(world_position + world_normal * (u_shadowMapOffset * texelWorld * receiver.sinT), 1.0);
    vec4 shadowcoord = mul(faceMtx, mul(u_lightMtx, wposOffset));
#if SM_LINEAR
    shadowcoord.z += 0.5;
    float depthScale = u_shadowBiasParams.y;
#else
    float depthScale = depthNumerator / max(shadowcoord.w * shadowcoord.w, 1e-6);
#endif
    float bias = shadowDepthBias(receiver, texelWorld, depthScale);
    // Receiver-plane term is off for point lights (the map axes are zero per face).
    vec2 planeGrad = vec2_splat(0.0);

    float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
    colorCoverage = coverageSign * coverage;

    visibility = computeVisibility(s_shadowMap0
                    , shadowcoord
                    , bias
                    , planeGrad
                    , u_smSamplingParams
                    , texelSize
                    , u_shadowMapDepthMultiplier
                    , u_shadowMapMinVariance
                    , u_shadowMapHardness
                    , fragCoord
                    , 1.0
                    );
#else
    vec2 texelSize = vec2_splat(u_shadowMapTexelSize);

    // Texel size at the receiver's distance along the light axis, from the unoffset projection.
    vec4 unoffset = mul(u_lightMtx, wpos);
    float texelWorld = unoffset.w * u_perspTexelParams.x;
    vec4 wposOffset = vec4(world_position + world_normal * (u_shadowMapOffset * texelWorld * receiver.sinT), 1.0);
    vec4 shadowcoord = mul(u_lightMtx, wposOffset);
#if SM_LINEAR
    shadowcoord.z += 0.5;
    float depthScale = u_shadowBiasParams.y;
#else
    float depthScale = u_shadowBiasParams.z / max(shadowcoord.w * shadowcoord.w, 1e-6);
#endif
    float bias = shadowDepthBias(receiver, texelWorld, depthScale);
    vec2 planeGrad = shadowPlaneGradient(receiver, shadowcoord.w * u_perspTexelParams.yz, depthScale);

    float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
    colorCoverage = vec3(coverage, -coverage, -coverage);

    visibility = computeVisibility(s_shadowMap0
                    , shadowcoord
                    , bias
                    , planeGrad
                    , u_smSamplingParams
                    , texelSize
                    , u_shadowMapDepthMultiplier
                    , u_shadowMapMinVariance
                    , u_shadowMapHardness
                    , fragCoord
                    , 1.0
                    );
#endif
#endif

    return visibility;
}

// ---- Contact shadows ----------------------------------------------------------------------
// Screen-space march along the light through the depth buffer, for the occluders the shadow
// map cannot resolve (object bases, crevices, fine detail). Same family as Unreal's
// ShadowRayCast / HDRP's ContactShadows.compute (perspective-correct march in uv + device
// depth, point-sampled depth, binary hit with a tolerance tied to the per-step depth travel,
// border vignette, dither integrated by TAA), plus a receiver-plane test that neither has: the
// receiver's own plane is rebuilt from its depth neighbours, so a planar receiver never
// shadows itself at any light elevation, view angle or distance (no N.L mask, no view-
// dependent bias), and only geometry on the light side of that plane can occlude.
//
// Step count: one step per CONTACT_SHADOW_PIXELS_PER_STEP projected pixels, clamped; rays under
// a pixel skip; near the camera the ray is shortened so its projection stays within
// CONTACT_SHADOW_MAX_PIXELS (the stride stays bounded without a second, screen-space length).
#define CONTACT_SHADOW_MIN_STEPS 4
#define CONTACT_SHADOW_MAX_STEPS 16
#define CONTACT_SHADOW_PIXELS_PER_STEP 2.0
#define CONTACT_SHADOW_MAX_PIXELS 64.0
// A depth-buffer surface is never thinner than this many steps of the ray's own depth travel,
// so a ray cannot step through a surface it entered (the user thickness is the minimum
// occluder thickness for rays that run sideways to the view).
#define CONTACT_SHADOW_STEP_THICKNESS 2.0
// Depth-buffer ulps granted to the plane test (on top of one per pixel travelled, the tilt
// error of a normal reconstructed from one-pixel differences) and to the ray-in-front test.
#define CONTACT_SHADOW_PLANE_ULPS 4.0
#define CONTACT_SHADOW_RAY_ULPS 2.0
// Curvature allowance of the plane test: tan of the tilt a neighbouring facet may have toward
// the camera before it can register as an occluder (faceted terrain, creases).
#define CONTACT_SHADOW_PLANE_TOLERANCE_TAN 0.05
// A candidate occluder's point-sampled depth is uncertain by the depth its surface spans across
// one pixel; the ray must be behind it by more than this many pixels of that span to count as
// inside it. Surfaces seen at grazing angles (the top of a raised edge beside a receiver in a
// depression) cannot register from their silhouette; faces, legs and overhangs are unaffected.
#define CONTACT_SHADOW_OCCLUDER_SPAN_PX 1.0
// The hit is released over the last part of the ray (the shadow map takes over) ...
#define CONTACT_SHADOW_FADE_START 0.75
// ... and over the outer part of the maximum distance.
#define CONTACT_SHADOW_DISTANCE_FADE 0.2
// The march is skipped where the shadow map already leaves less than this.
#define CONTACT_SHADOW_SKIP_BELOW 0.001

// uv + device depth of a view-space point, the space the march is affine in.
vec3 contactProjectToScreen(vec3 vs_pos)
{
    vec4 clip = mul(u_proj, vec4(vs_pos, 1.0));
    vec3 ss = clipTransform(clip.xyz / clip.w);
    ss.xy = ss.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_GLSL
    ss.z = ss.z * 0.5 + 0.5;
#endif
    return ss;
}

// Linear depth of a depth-buffer texel, clamped into the texture.
float contactTexelDepth(sampler2D depthTex, ivec2 texel, ivec2 max_texel)
{
    return screenSpaceToViewSpaceDepth(texelFetch(depthTex, clamp(texel, ivec2(0, 0), max_texel), 0).x);
}

// Tangent along one axis of the receiver surface from the two texel neighbours: the one
// closer in depth to the centre (the side more likely on the same surface), the other side
// when the first is off the texture.
vec3 contactPlaneAxis(vec3 p_center, float z_center, vec3 d_minus, float z_minus, bool has_minus,
                      vec3 d_plus, float z_plus, bool has_plus)
{
    bool use_plus = has_plus && (!has_minus || abs(z_plus - z_center) <= abs(z_minus - z_center));
    return use_plus ? (d_plus * z_plus - p_center) : (p_center - d_minus * z_minus);
}

/// Returns 1.0 when lit, 0.0 when the march found an occluder (scaled by opacity and fades).
/// origin_texel / origin_device_depth: the receiver's G-buffer texel; depth_ulp: view-space span
/// of one depth-buffer ulp at the receiver (pbr_light); light_distance: distance to a spot /
/// point light (the ray stops there), a large value for directional lights.
float ContactShadow(sampler2D depthTex, ivec2 origin_texel, float origin_device_depth, float depth_ulp,
                    vec3 world_light_dir, float ray_length, float light_distance, vec2 screen_pos)
{
    // Perspective cameras only: the plane evaluation assumes camera rays through the origin.
    // u_proj[3][3] (0 perspective, 1 orthographic) is a diagonal element and reads the same on
    // every backend.
    if(u_proj[3][3] != 0.0) return 1.0;

    ivec2 depth_size = textureSize(depthTex, 0);
    ivec2 max_texel = depth_size - ivec2(1, 1);
    vec2 depth_size_f = vec2(depth_size);
    vec2 texel_uv = 1.0 / depth_size_f;
    vec2 origin_uv = (vec2(origin_texel) + vec2(0.5, 0.5)) * texel_uv;
    vec3 vs_origin = computeViewSpacePosition(origin_uv, origin_device_depth);
    float z0 = vs_origin.z;

    float distance_fade = 1.0;
    if(u_contact_shadow.y > 0.0)
    {
        distance_fade = 1.0 - smoothstep(u_contact_shadow.y * (1.0 - CONTACT_SHADOW_DISTANCE_FADE), u_contact_shadow.y, z0);
        if(distance_fade <= 0.0) return 1.0;
    }

    // The ray: along the light, no farther than the light itself, clipped before the near plane
    // (a projected end behind the camera would flip the screen-space segment).
    vec3 vs_light_dir = normalize(mul(u_view, vec4(world_light_dir, 0.0)).xyz);
    float ray_len = min(ray_length, light_distance);
    if(vs_light_dir.z < 0.0)
    {
        float z_near = screenSpaceToViewSpaceDepth(0.0);
        ray_len = min(ray_len, (z0 - z_near) * 0.9 / -vs_light_dir.z);
    }
    if(ray_len <= 0.0) return 1.0;

    vec3 ss_origin = vec3(origin_uv, origin_device_depth);
    vec3 ss_end = contactProjectToScreen(vs_origin + vs_light_dir * ray_len);
    float ray_px = length((ss_end.xy - ss_origin.xy) * depth_size_f);
    if(ray_px < 1.0) return 1.0;
    if(ray_px > CONTACT_SHADOW_MAX_PIXELS)
    {
        ray_len *= CONTACT_SHADOW_MAX_PIXELS / ray_px;
        ss_end = contactProjectToScreen(vs_origin + vs_light_dir * ray_len);
        ray_px = length((ss_end.xy - ss_origin.xy) * depth_size_f);
    }
    vec3 ss_ray = ss_end - ss_origin;
    int steps = int(clamp(ceil(ray_px / CONTACT_SHADOW_PIXELS_PER_STEP), float(CONTACT_SHADOW_MIN_STEPS), float(CONTACT_SHADOW_MAX_STEPS)));
    float inv_steps = 1.0 / float(steps);

    // Receiver plane from the depth buffer. d(texel) is the view-space direction with z = 1
    // through a texel centre: affine in the texel index, with per-texel derivatives from the
    // projection's diagonal (safe to index on every backend); the y sign follows clipTransform.
    vec3 d0 = vs_origin / z0;
    float dd_dx = 2.0 / u_proj[0][0] * texel_uv.x;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV
    float dd_dy = -2.0 / u_proj[1][1] * texel_uv.y;
#else
    float dd_dy = 2.0 / u_proj[1][1] * texel_uv.y;
#endif
    vec3 d_left = d0 - vec3(dd_dx, 0.0, 0.0);
    vec3 d_right = d0 + vec3(dd_dx, 0.0, 0.0);
    vec3 d_down = d0 - vec3(0.0, dd_dy, 0.0);
    vec3 d_up = d0 + vec3(0.0, dd_dy, 0.0);
    float z_left = contactTexelDepth(depthTex, origin_texel + ivec2(-1, 0), max_texel);
    float z_right = contactTexelDepth(depthTex, origin_texel + ivec2(1, 0), max_texel);
    float z_down = contactTexelDepth(depthTex, origin_texel + ivec2(0, -1), max_texel);
    float z_up = contactTexelDepth(depthTex, origin_texel + ivec2(0, 1), max_texel);
    vec3 axis_x = contactPlaneAxis(vs_origin, z0, d_left, z_left, origin_texel.x > 0, d_right, z_right, origin_texel.x < max_texel.x);
    vec3 axis_y = contactPlaneAxis(vs_origin, z0, d_down, z_down, origin_texel.y > 0, d_up, z_up, origin_texel.y < max_texel.y);
    vec3 plane_n = cross(axis_x, axis_y);
    float plane_n_len = length(plane_n);
    if(plane_n_len <= 0.0) return 1.0;
    plane_n /= plane_n_len;
    // Facing the camera (at the origin): the camera side of the plane is the only side an
    // occluder of the receiver can be on.
    if(dot(plane_n, d0) > 0.0) plane_n = -plane_n;
    float plane_c = dot(plane_n, vs_origin);

    float px_world_scale = max(abs(dd_dx), abs(dd_dy));
    float inv_z0_sq = 1.0 / (z0 * z0);
    float thickness_min = max(u_contact_shadow.x, 0.0);
    float dither = interleavedGradientNoise(screen_pos + u_contact_shadow.w * vec2(47.17, 17.31)) - 0.5;
    float z_prev = z0;
    float hit_t = -1.0;
    LOOP for(int i = 0; i < steps; i++)
    {
        float t = (float(i) + 1.0 + dither) * inv_steps;
        vec3 ss_pos = ss_origin + ss_ray * t;
        if(any(lessThan(ss_pos.xy, vec2_splat(0.0))) || any(greaterThan(ss_pos.xy, vec2_splat(1.0)))) break;
        ivec2 texel = min(ivec2(floor(ss_pos.xy * depth_size_f)), max_texel);
        float z_ray = screenSpaceToViewSpaceDepth(ss_pos.z);
        float z_scene = contactTexelDepth(depthTex, texel, max_texel);
        float ulp_scene = depth_ulp * (z_scene * z_scene) * inv_z0_sq;
        float thickness = max(thickness_min, CONTACT_SHADOW_STEP_THICKNESS * abs(z_ray - z_prev));
        z_prev = z_ray;
        // The visible surface must be in front of the ray point (by more than its precision) and
        // no farther in front than the occluder thickness ...
        float behind = z_ray - z_scene;
        if(behind > CONTACT_SHADOW_RAY_ULPS * ulp_scene && behind <= thickness)
        {
            // ... and on the camera side of the receiver plane at that texel's centre, by more
            // than the plane's precision (depth ulps, growing one per pixel travelled, converted to
            // height along the normal) and the curvature allowance. A planar receiver's own texels
            // sit on the plane and fail this exactly.
            vec2 dpx = vec2(texel - origin_texel);
            vec3 d_scene = d0 + vec3(dpx.x * dd_dx, dpx.y * dd_dy, 0.0);
            float n_dot_d = dot(plane_n, d_scene);
            float height = n_dot_d * z_scene - plane_c;
            float travelled_px = length(dpx);
            float eps_height = (CONTACT_SHADOW_PLANE_ULPS + travelled_px) * ulp_scene * abs(n_dot_d)
                             + travelled_px * z_scene * px_world_scale * CONTACT_SHADOW_PLANE_TOLERANCE_TAN;
            if(height > eps_height)
            {
                // ... and behind the occluder by more than its own per-pixel depth span (the
                // smaller difference per axis, so a thin occluder is measured against itself and
                // not across its silhouette).
                float z_l = contactTexelDepth(depthTex, texel + ivec2(-1, 0), max_texel);
                float z_r = contactTexelDepth(depthTex, texel + ivec2(1, 0), max_texel);
                float z_d = contactTexelDepth(depthTex, texel + ivec2(0, -1), max_texel);
                float z_u = contactTexelDepth(depthTex, texel + ivec2(0, 1), max_texel);
                float span_x = min(abs(z_r - z_scene), abs(z_scene - z_l));
                float span_y = min(abs(z_u - z_scene), abs(z_scene - z_d));
                if(behind > CONTACT_SHADOW_OCCLUDER_SPAN_PX * max(span_x, span_y))
                {
                    hit_t = t;
                    break;
                }
            }
        }
    }
    if(hit_t < 0.0) return 1.0;

    float occlusion = 1.0 - smoothstep(CONTACT_SHADOW_FADE_START, 1.0, hit_t);
    // Screen-border vignette on the hit position (Unreal's).
    vec2 hit_ndc = (ss_origin.xy + ss_ray.xy * hit_t) * 2.0 - 1.0;
    vec2 vignette = max(6.0 * abs(hit_ndc) - 5.0, vec2_splat(0.0));
    occlusion *= saturate(1.0 - dot(vignette, vignette));
    occlusion *= distance_fade * saturate(u_contact_shadow.z);
    return 1.0 - occlusion;
}

#if DIRECTIONAL_LIGHT
/// Sun transmittance through the cloud layer above world_position (L points toward the sun):
/// the surface point is projected up the sun direction to the layer base and the shadow map is
/// read there. Fades to unshadowed toward the map border.
float CloudShadow(vec3 world_position, vec3 L)
{
    if(u_cloudShadow2.x < 0.5 || L.y < 0.05)
    {
        return 1.0;
    }
    float t = (u_cloudShadow2.y - world_position.y) / L.y;
    if(t <= 0.0)
    {
        return 1.0;
    }
    vec3 entry = world_position + L * t;
    vec2 map_pos = (entry.xz - u_cloudShadow.xy) * u_cloudShadow.z + 0.5;
    float transmittance = texture2D(s_cloudShadow, clipToUv(map_pos)).r;
    vec2 d = abs(map_pos - vec2_splat(0.5));
    float border = saturate((0.5 - max(d.x, d.y)) / max(u_cloudShadow2.z, 1e-4));
    return mix(1.0, transmittance, u_cloudShadow.w * border);
}
#endif

vec4 pbr_light(vec2 texcoord0, vec2 fragCoord)
{
    ivec2 gbuf_texel = GBufferTexelFromFragCoord(fragCoord, s_tex4);
    GBufferData data = DecodeGBufferTexel(gbuf_texel, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
    vec3 clip = ReconstructClipFromGBufferTexel(gbuf_texel, data.depth, s_tex4);
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    float filtered_roughness = GeometricSpecularAA(data.world_normal, data.roughness);
    vec3 lobe_roughness = vec3(0.0f, filtered_roughness, 1.0f);
    vec3 light_color = u_light_color_intensity.xyz;
    float intensity = u_light_color_intensity.w;
    vec3 specular_color = data.specular_color;
    vec3 diffuse_color = data.diffuse_color;


#if DIRECTIONAL_LIGHT
    vec3 vector_to_light = -u_light_direction.xyz;
#else
    vec3 vector_to_light = u_light_position.xyz - world_position;
#endif
    float distance_sqr = dot( vector_to_light, vector_to_light );
    vec3 N = data.world_normal;
    vec3 V = normalize(u_camera_position.xyz - world_position);
    vec3 L = vector_to_light / sqrt( distance_sqr );

#if POINT_LIGHT
    vec3 vector_to_light_over_radius = vector_to_light / u_light_data.x;
    float light_radius_mask = RadialAttenuation(vector_to_light_over_radius, u_light_data.y);
    float light_falloff = 1.0f;
#elif SPOT_LIGHT
    vec3 vector_to_light_over_radius = vector_to_light / u_light_data.x;
    float light_radius_mask = RadialAttenuation(vector_to_light_over_radius, 1.0f);
    float light_falloff = SpotAttenuation( vector_to_light_over_radius, normalize(u_light_direction.xyz), vec2(u_light_data.z, 1.0f / (u_light_data.y - u_light_data.z )));
#else
    float light_radius_mask = 1.0f;
    float light_falloff = 1.0f;
#endif

    float NoL = saturate(dot(N, L));

    vec3 colorCoverage = vec3(0.0f, 0.0f, 0.0f);
    // Receiver position uncertainty: the view-space distance one float ulp of the stored depth
    // spans here (finite difference over a small relative step), a couple of ulps of margin,
    // projected on the normal - the only component a planar shadow compare feels.
    float viewDepthHere = screenSpaceToViewSpaceDepth(data.depth01);
    float viewDepthProbe = screenSpaceToViewSpaceDepth(data.depth01 * (1.0 + SHADOW_DEPTH_PROBE_STEP));
    float depthUlp = abs(viewDepthProbe - viewDepthHere) * (SHADOW_DEPTH_FLOAT_ULP / SHADOW_DEPTH_PROBE_STEP);
    float precisionNormal = SHADOW_DEPTH_ULPS * depthUlp * abs(dot(N, V));
    float surface_shadow = CalculateSurfaceShadow(world_position, N, L, precisionNormal, fragCoord, colorCoverage);
    // Contact shadows: skipped where they cannot change the result (unlit, already fully
    // shadowed, sky).
    float contact_shadow_length = u_light_data.w;
    BRANCH
    if(contact_shadow_length > 0.0 && NoL > 0.0 && surface_shadow > CONTACT_SHADOW_SKIP_BELOW && data.depth01 < 1.0)
    {
#if DIRECTIONAL_LIGHT
        const float light_distance = 1.0e6;
#else
        float light_distance = sqrt(distance_sqr);
#endif
        float contact = ContactShadow(s_tex4, gbuf_texel, data.depth01, depthUlp, L, contact_shadow_length, light_distance, fragCoord);
        surface_shadow = min(surface_shadow, contact);
    }
#if DIRECTIONAL_LIGHT
    surface_shadow *= CloudShadow(world_position, L);
#endif
    float subsurface_shadow = 1.0f;
    float base_attenuation = intensity * light_radius_mask * light_falloff;
    float surface_attenuation = base_attenuation * surface_shadow;
    float subsurface_attenuation = base_attenuation * subsurface_shadow;

    vec3 energy = AreaLightSpecular(0.0f, 0.0f, normalize(vector_to_light), lobe_roughness, vector_to_light, L, V, N);

    vec3 direct_surface_lighting = StandardShadingDirect(diffuse_color, specular_color, lobe_roughness, energy, L, V, N, data.ambient_occlusion);
    vec3 subsurface_lighting = SubsurfaceShading(data.subsurface_color, data.subsurface_opacity, data.ambient_occlusion, L, V, N);
    vec3 subsurface_multiplier = (light_color * subsurface_attenuation);

    vec3 surface_multiplier = light_color * (NoL * surface_attenuation);
    vec3 lighting = surface_multiplier * direct_surface_lighting
                  + subsurface_lighting * subsurface_multiplier
                  + colorCoverage * u_shadowMapShowCoverage;

    // vec3 surface_multiplier_unshadowed = light_color * (NoL * base_attenuation);
    // vec3 lighting_unshadowed = surface_multiplier_unshadowed * direct_surface_lighting
    //                          + subsurface_lighting * subsurface_multiplier
    //                          + colorCoverage * u_shadowMapShowCoverage;

    return vec4(lighting, 1.0f);
}

#if PBR_INDIRECT
// Separate indirect lighting + emissive pass (rendered once as a fullscreen quad, not per-light)
vec4 pbr_indirect(vec2 texcoord0, vec2 fragCoord)
{
    ivec2 gbuf_texel = GBufferTexelFromFragCoord(fragCoord, s_tex4);
    GBufferData data = DecodeGBufferTexel(gbuf_texel, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
    vec3 indirect_specular = texture2D(s_tex5, texcoord0).xyz;
    vec3 clip = ReconstructClipFromGBufferTexel(gbuf_texel, data.depth, s_tex4);
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    vec3 indirect_color = u_light_data.xyz;
    float indirect_intensity = u_light_data.w;

    vec3 N = normalize(data.world_normal);
    vec3 V = normalize(u_camera_position.xyz - world_position);

    // GTAO: its visibility occludes the indirect terms (the GI resolve already reads its
    // probes at the bent normal - the mean unoccluded direction - and so does the SH lookup
    // below; the visibility itself is applied here, at full resolution, after the GI's
    // denoise chain). The diffuse takes the MULTI-BOUNCE form (per channel: light albedos
    // give part of the occluded energy back through interreflection); the specular
    // occlusion keeps the plain visibility. Direct light is untouched by design.
    float screen_ao = 1.0;
    vec3 diffuse_normal = N;
    if(u_gtao_params.x > 0.5)
    {
        vec4 gtao = texture2D(s_gtao, texcoord0);
        screen_ao = mix(1.0, gtao.a, u_gtao_params.z);
        vec3 bent_normal = normalize(gtao.xyz * 2.0 - 1.0);
        diffuse_normal = normalize(mix(N, bent_normal, u_gtao_params.y));
    }
    vec3 diffuse_screen_ao = u_gtao_params.w > 0.5 ? MultiBounceAO(screen_ao, data.diffuse_color)
                                                   : vec3_splat(screen_ao);
    float combined_ao = data.ambient_occlusion * screen_ao;

    vec3 irradiance = eval_irradiance_sh(s_irradiance, diffuse_normal);
    // THE INDIRECT DIFFUSE CONTRACT (energy audit): this slot carries E/pi - the
    // cosine-weighted MEAN incoming radiance - because StandardShadingIndirect multiplies it
    // by plain DiffuseColor. Lambert's BRDF is albedo/pi and the direct path pays its pi in
    // Diffuse_Lambert; folding the other pi here keeps ONE convention on both paths:
    // outgoing = albedo * E/pi. SSIL and the GI resolve already PRODUCE E/pi natively (a
    // cosine-importance mean of radiance IS E/pi), so they feed the slot unscaled - the old
    // "* PI" here was the single factor that made every indirect bounce pi times hotter than
    // the same light arriving directly. eval_irradiance_sh returns TRUE irradiance E (raw
    // cosine-convolution constants), so the SH branch divides here.
    //
    // SSIL/GI REPLACES the SH probe by its alpha rather than adding to it -- adding would
    // double-count the environment. Alpha is the blend weight: per-frame trace coverage when
    // temporal is off, accumulated screen-hit evidence when temporal is on. When both are
    // disabled the bound fallback has alpha 0 -> pure SH.
    vec4 ssil_sample = texture2D(s_ssil, texcoord0);
    vec3 indirect_diffuse = mix(irradiance * RECIP_PI, ssil_sample.rgb, ssil_sample.a) * diffuse_screen_ao;

    float indirect_filtered_roughness = GeometricSpecularAA(N, data.roughness);
    float lighting_visibility = saturate(sqrt(Luminance(indirect_diffuse)));

    // Diffuse: the material AO (the screen term is already folded into indirect_diffuse per
    // channel); specular occlusion: material x plain screen visibility.
    vec3 indirect_lighting = StandardShadingIndirectAO(data.diffuse_color, indirect_diffuse, data.specular_color, indirect_specular, s_tex6, indirect_filtered_roughness, data.ambient_occlusion, combined_ao, lighting_visibility, V, N);
    return vec4(indirect_lighting + data.emissive_color, 1.0f);
}
#endif

#endif // __PBRLIGHTING_SH__
