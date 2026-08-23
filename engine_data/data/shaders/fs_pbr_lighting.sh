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
/// x: hit thickness (view-space, 0 = auto from ray length). y,z: N·L smoothstep edges. w: normal-facing reject (-1 = off).
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
#else
SAMPLER2D(s_shadowMap0, 7);
SAMPLER2D(s_shadowMap1, 8);
SAMPLER2D(s_shadowMap2, 9);
SAMPLER2D(s_shadowMap3, 10);
#endif

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

/// Screen-space contact shadow. Marches a short ray toward the light in
/// screen space and tests the depth buffer for occluders that shadow maps
/// cannot resolve (object contact points, small crevices, fine detail).
/// Returns 1.0 when fully lit, 0.0 when fully shadowed.
float ContactShadow(sampler2D depthTex, vec2 origin_uv, float origin_device_depth,
                    vec3 world_light_dir, float ray_length, vec2 screen_pos, vec3 world_shading_normal)
{
    float vs_depth = abs(screenSpaceToViewSpaceDepth(origin_device_depth));
    float fade_end = max(ray_length * 80.0, 20.0);
    float distance_fade = 1.0 - smoothstep(fade_end * 0.6, fade_end, vs_depth);
    if(distance_fade <= 0.0) return 1.0;

    vec3 vs_origin = computeViewSpacePosition(origin_uv, origin_device_depth);
    vec3 vs_light_dir = normalize(mul(u_view, vec4(world_light_dir, 0.0)).xyz);
    vec3 vs_end = vs_origin + vs_light_dir * ray_length;
    vec4 end_proj = mul(u_proj, vec4(vs_end, 1.0));
    vec3 ss_end = end_proj.xyz / end_proj.w;
    ss_end = clipTransform(ss_end);
    ss_end.xy = ss_end.xy * 0.5 + 0.5;
#if BGFX_SHADER_LANGUAGE_GLSL
    ss_end.z = ss_end.z * 0.5 + 0.5;
#endif
    vec3 ss_origin = vec3(origin_uv, origin_device_depth);
    vec3 ss_ray = ss_end - ss_origin;
    const int CONTACT_SHADOW_STEPS = 16;
    float inv_steps = 1.0 / float(CONTACT_SHADOW_STEPS);
    float thickness = u_contact_shadow.x > 0.0 ? u_contact_shadow.x : (ray_length * 0.15);
    float n_dot_l = saturate(dot(world_shading_normal, world_light_dir));
    float n_dot_l_hi = max(u_contact_shadow.z, u_contact_shadow.y + 1e-4);
    float n_dot_l_mask = smoothstep(u_contact_shadow.y, n_dot_l_hi, n_dot_l);
    if(n_dot_l_mask <= 0.0) return 1.0;

    float vs_origin_z = vs_origin.z;
    float vs_end_z = vs_end.z;
    vec3 vs_normal = normalize(mul(u_view, vec4(world_shading_normal, 0.0)).xyz);
    float n_dot_v = max(abs(dot(vs_normal, normalize(-vs_origin))), 0.05);
    float self_shadow_bias = ray_length * 0.07 / n_dot_v;
    float frame_index = u_camera_position.w;
    float dither = interleavedGradientNoise(screen_pos + frame_index * vec2(47.17, 17.31)) * inv_steps;
    LOOP for(int i = 0; i < CONTACT_SHADOW_STEPS; i++)
    {
        float t = (float(i) + 1.0) * inv_steps + dither;
        if(t > 1.0) break;
        vec3 ss_pos = ss_origin + ss_ray * t;
        if(any(greaterThan(abs(ss_pos.xy - 0.5), vec2_splat(0.5))))
            break;
        float scene_depth = texture2DLod(depthTex, ss_pos.xy, 0.0).x;
        float vs_ray_depth = mix(vs_origin_z, vs_end_z, t);
        float vs_scene_depth = screenSpaceToViewSpaceDepth(scene_depth);
        float depth_diff = vs_ray_depth - vs_scene_depth;
        float step_thickness = thickness * (1.0 - t * 0.7);
        if(depth_diff > self_shadow_bias && depth_diff < step_thickness)
        {
            if(u_contact_shadow.w >= 0.0)
            {
                vec2 texel_uv = 1.0 / vec2(textureSize(depthTex, 0));
                vec2 duvx = vec2(texel_uv.x, 0.0);
                vec2 duvy = vec2(0.0, texel_uv.y);
                float zx = texture2DLod(depthTex, ss_pos.xy + duvx, 0.0).x;
                float zy = texture2DLod(depthTex, ss_pos.xy + duvy, 0.0).x;
                vec3 p0 = computeViewSpacePosition(ss_pos.xy, scene_depth);
                vec3 px = computeViewSpacePosition(ss_pos.xy + duvx, zx);
                vec3 py = computeViewSpacePosition(ss_pos.xy + duvy, zy);
                vec3 n_vs = cross(px - p0, py - p0);
                float n_len = length(n_vs);
                if(n_len > 1e-5)
                {
                    n_vs *= 1.0 / n_len;
                    if(dot(n_vs, normalize(-p0)) < 0.0)
                        n_vs = -n_vs;
                    vec3 n_ws = normalize(mul(u_invView, vec4(n_vs, 0.0)).xyz);
                    if(dot(n_ws, world_light_dir) > u_contact_shadow.w)
                        continue;
                }
            }
            float occ = smoothstep(0.0, 1.0, t);
            return mix(1.0, occ, n_dot_l_mask * distance_fade);
        }
    }
    return 1.0;
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
    float contact_shadow_length = u_light_data.w;
    BRANCH
    if(contact_shadow_length > 0.0)
    {
        float contact = ContactShadow(s_tex4, texcoord0, data.depth01, L, contact_shadow_length, fragCoord, N);
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

    vec3 irradiance = eval_irradiance_sh(s_irradiance, N);
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
    vec3 indirect_diffuse = mix(irradiance * RECIP_PI, ssil_sample.rgb, ssil_sample.a);

    float indirect_filtered_roughness = GeometricSpecularAA(N, data.roughness);
    float lighting_visibility = saturate(sqrt(Luminance(indirect_diffuse)));

    vec3 indirect_lighting = StandardShadingIndirect(data.diffuse_color, indirect_diffuse, data.specular_color, indirect_specular, s_tex6, indirect_filtered_roughness, data.ambient_occlusion, lighting_visibility, V, N);
    return vec4(indirect_lighting + data.emissive_color, 1.0f);
}
#endif

#endif // __PBRLIGHTING_SH__
