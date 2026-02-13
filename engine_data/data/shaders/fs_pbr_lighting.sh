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
uniform vec4 u_camera_position;


SAMPLER2D(s_shadowMap0, 7);
SAMPLER2D(s_shadowMap1, 8);
SAMPLER2D(s_shadowMap2, 9);
SAMPLER2D(s_shadowMap3, 10);

uniform vec4 u_params0;
uniform vec4 u_params1;
uniform vec4 u_params2;

uniform vec4 u_smSamplingParams;
uniform vec4 u_csmFarDistances;

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
#define u_shadowMapTexelSize    u_params2.z



// Pcf
#define u_shadowMapPcfMode     u_shadowMapParam0
#define u_shadowMapNoiseAmount u_shadowMapParam1

// Vsm
#define u_shadowMapMinVariance     u_shadowMapParam0
#define u_shadowMapDepthMultiplier u_shadowMapParam1

// Esm
#define u_shadowMapHardness        u_shadowMapParam0
#define u_shadowMapDepthMultiplier u_shadowMapParam1
float calculateDistanceBias(float _bias, float _distanceFromCamera)
{
    // Use power function (exponent 0.65) for non-linear scaling - starts very gentle
    // at close distances, scales more aggressively at far distances. This reduces
    // shadow floating when close while maintaining good bias at distance.
    float shadowMapDistanceBiasScale = 0.02f;
    float shadowMapDistanceBiasExponent = 0.65f;
    float distanceScale = pow(_distanceFromCamera, shadowMapDistanceBiasExponent) * shadowMapDistanceBiasScale;
    return _bias * (1.0 + distanceScale);
}


// NdotL-scaled normal offset: returns the scale factor for the normal offset.
// Zero when the surface faces the light (preserving ground contact),
// maximum at grazing angles (preventing acne). Uses sin(lightAngle).
float calculateNormalOffsetScale(float _NdotL)
{
    return sqrt(1.0 - _NdotL * _NdotL);
}

// Receiver plane depth bias (Render Diagrams: https://renderdiagrams.org/2024/12/18/shadowmap-bias/)
// Assumes the receiving surface is planar within the shadow texel. Computes dz/dUV via screen-space
// derivatives and the chain rule, then adds bias based on distance from pixel to texel center.
// Returns a value to ADD to shadowCoord.z; clamped to <= 0 so we only move out of shadow.
// Caveat: ddx/ddy assume flat surface across 2x2 block; discontinuities can cause artifacts.
float computeReceiverPlaneDepthBias(vec4 _shadowCoord, float _texelSize)
{
    vec2 uv = _shadowCoord.xy / _shadowCoord.w;
    float w = _shadowCoord.w;
    vec2 dU_dXY = (w * dFdx(_shadowCoord.xy) - _shadowCoord.xy * dFdx(w)) / (w * w);
    vec2 dV_dXY = (w * dFdy(_shadowCoord.xy) - _shadowCoord.xy * dFdy(w)) / (w * w);
    float dZ_dX = (w * dFdx(_shadowCoord.z) - _shadowCoord.z * dFdx(w)) / (w * w);
    float dZ_dY = (w * dFdy(_shadowCoord.z) - _shadowCoord.z * dFdy(w)) / (w * w);
    vec2 dZ_dXY = vec2(dZ_dX, dZ_dY);
    float detJ = dU_dXY.x * dV_dXY.y - dV_dXY.x * dU_dXY.y;
    vec2 dz_dUV = vec2_splat(0.0);
    if (abs(detJ) > 1e-7)
    {
        dz_dUV.x = (dZ_dXY.x * dV_dXY.y - dZ_dXY.y * dV_dXY.x) / detJ;
        dz_dUV.y = (-dZ_dXY.x * dU_dXY.y + dZ_dXY.y * dU_dXY.x) / detJ;
    }
    float shadowMapSize = 1.0 / _texelSize;
    vec2 distanceToTexelCenterUV = (vec2_splat(0.5) - fract(uv * shadowMapSize)) * _texelSize;
    float slopeBias = dot(distanceToTexelCenterUV, dz_dUV);
    return min(slopeBias, 0.0);
}

float computeVisibility(sampler2D _sampler
                      , vec4 _shadowCoord
                      , float _bias
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

#if SM_LINEAR
    vec4 shadowcoord = vec4(_shadowCoord.xy / _shadowCoord.w, _shadowCoord.z, 1.0);
#else
    vec4 shadowcoord = _shadowCoord;
#endif

#if SM_HARD
    visibility = hardShadow(_sampler, shadowcoord, _bias);
#elif SM_PCF
    visibility = PCF(_sampler, shadowcoord, _bias, _samplingParams, _texelSize, _fragCoord, _cascadeScale);
#elif SM_PCSS
    visibility = PCSS(_sampler, shadowcoord, _bias, _samplingParams, _texelSize, _fragCoord, _cascadeScale);
#elif SM_VSM
    visibility = VSM(_sampler, shadowcoord, _bias, _depthMultiplier, _minVariance);
#elif SM_ESM
    visibility = ESM(_sampler, shadowcoord, _bias, _depthMultiplier * _hardness);
#endif

    return visibility;
}


float CalculateSurfaceShadow(vec3 world_position, vec3 world_normal, vec3 light_dir, vec2 fragCoord, out vec3 colorCoverage)
{
    float visibility = 1.0f;
    colorCoverage = vec3(0.0f, 0.0f, 0.0f);

#if SM_NOOP
    // No operation
#else
    float NdotL = saturate(dot(world_normal, light_dir));
    float normalOffsetScale = calculateNormalOffsetScale(NdotL);
    vec4 wpos = vec4(world_position.xyz + world_normal.xyz * u_shadowMapOffset * normalOffsetScale, 1.0);
#if SM_CSM
    vec4 v_shadowcoord = wpos;
#else
    vec4 v_shadowcoord = mul(u_lightMtx, wpos);
#endif

#if SM_CSM
    vec4 v_texcoord1 = mul(u_shadowMapMtx0, v_shadowcoord);
    vec4 v_texcoord2 = mul(u_shadowMapMtx1, v_shadowcoord);
    vec4 v_texcoord3 = mul(u_shadowMapMtx2, v_shadowcoord);
    vec4 v_texcoord4 = mul(u_shadowMapMtx3, v_shadowcoord);
#elif SM_OMNI
    vec4 v_texcoord1 = mul(u_shadowMapMtx0, v_shadowcoord);
    vec4 v_texcoord2 = mul(u_shadowMapMtx1, v_shadowcoord);
    vec4 v_texcoord3 = mul(u_shadowMapMtx2, v_shadowcoord);
    vec4 v_texcoord4 = mul(u_shadowMapMtx3, v_shadowcoord);
#endif

#if SM_LINEAR
#if SM_CSM
    v_texcoord1.z += 0.5;
    v_texcoord2.z += 0.5;
    v_texcoord3.z += 0.5;
    v_texcoord4.z += 0.5;
#elif SM_OMNI
    v_texcoord1.z += 0.5;
    v_texcoord2.z += 0.5;
    v_texcoord3.z += 0.5;
    v_texcoord4.z += 0.5;
#else
    v_shadowcoord.z += 0.5;
#endif
#endif

    float distanceFromCamera = length(u_camera_position.xyz - world_position);
    float adjustedBias = calculateDistanceBias(u_shadowMapBias, distanceFromCamera);

#if SM_CSM
    vec2 texelSize = vec2_splat(u_shadowMapTexelSize);

    vec2 texcoord1 = v_texcoord1.xy/v_texcoord1.w;
    vec2 texcoord2 = v_texcoord2.xy/v_texcoord2.w;
    vec2 texcoord3 = v_texcoord3.xy/v_texcoord3.w;
    vec2 texcoord4 = v_texcoord4.xy/v_texcoord4.w;

	bool selection0 = all(lessThan(texcoord1, vec2_splat(0.99))) && all(greaterThan(texcoord1, vec2_splat(0.01)));
	bool selection1 = all(lessThan(texcoord2, vec2_splat(0.99))) && all(greaterThan(texcoord2, vec2_splat(0.01)));
	bool selection2 = all(lessThan(texcoord3, vec2_splat(0.99))) && all(greaterThan(texcoord3, vec2_splat(0.01)));
	bool selection3 = all(lessThan(texcoord4, vec2_splat(0.99))) && all(greaterThan(texcoord4, vec2_splat(0.01)));

    if (selection0)
    {
        vec4 shadowcoord = v_texcoord1;
        shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.x, 0.001);

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.4;
        colorCoverage = vec3(-coverage, coverage, -coverage);
        visibility = computeVisibility(s_shadowMap0
                        , shadowcoord
                        , adjustedBias
                        , u_smSamplingParams
                        , texelSize
                        , u_shadowMapDepthMultiplier
                        , u_shadowMapMinVariance
                        , u_shadowMapHardness
                        , fragCoord
                        , cascadeScale
                        );

    }
    else if (selection1 && u_numSplits > 1)
    {
        vec4 shadowcoord = v_texcoord2;
        shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.y, 0.001);

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.4;
        colorCoverage = vec3(coverage, coverage, -coverage);
        visibility = computeVisibility(s_shadowMap1
                        , shadowcoord
                        , adjustedBias
                        , u_smSamplingParams
                        , texelSize/2.0
                        , u_shadowMapDepthMultiplier
                        , u_shadowMapMinVariance
                        , u_shadowMapHardness
                        , fragCoord
                        , cascadeScale
                        );
    }
    else if (selection2 && u_numSplits > 2)
    {
        vec4 shadowcoord = v_texcoord3;
        shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.z, 0.001);

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.4;
        colorCoverage = vec3(-coverage, -coverage, coverage);
        visibility = computeVisibility(s_shadowMap2
                        , shadowcoord
                        , adjustedBias
                        , u_smSamplingParams
                        , texelSize/3.0
                        , u_shadowMapDepthMultiplier
                        , u_shadowMapMinVariance
                        , u_shadowMapHardness
                        , fragCoord
                        , cascadeScale
                        );
    }
    else if (selection3 && u_numSplits > 3) // selection3
    {
        vec4 shadowcoord = v_texcoord4;
        shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);
        float cascadeScale = u_csmFarDistances.x / max(u_csmFarDistances.w, 0.001);

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.4;
        colorCoverage = vec3(coverage, -coverage, -coverage);
        visibility = computeVisibility(s_shadowMap3
                        , shadowcoord
                        , adjustedBias
                        , u_smSamplingParams
                        , texelSize/4.0
                        , u_shadowMapDepthMultiplier
                        , u_shadowMapMinVariance
                        , u_shadowMapHardness
                        , fragCoord
                        , cascadeScale
                        );
    }
#elif SM_OMNI
    vec2 texelSize = vec2_splat(u_shadowMapTexelSize/4.0);

    vec4 faceSelection;
	vec3 pos = v_shadowcoord.xyz;
    faceSelection.x = dot(u_tetraNormalGreen.xyz,  pos);
    faceSelection.y = dot(u_tetraNormalYellow.xyz, pos);
    faceSelection.z = dot(u_tetraNormalBlue.xyz,   pos);
    faceSelection.w = dot(u_tetraNormalRed.xyz,    pos);

    vec4 shadowcoord;
    float faceMax = max(max(faceSelection.x, faceSelection.y), max(faceSelection.z, faceSelection.w));
    if (faceSelection.x == faceMax)
    {
        shadowcoord = v_texcoord1;

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
        colorCoverage = vec3(-coverage, coverage, -coverage);
    }
    else if (faceSelection.y == faceMax)
    {
        shadowcoord = v_texcoord2;

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
        colorCoverage = vec3(coverage, coverage, -coverage);
    }
    else if (faceSelection.z == faceMax)
    {
        shadowcoord = v_texcoord3;

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
        colorCoverage = vec3(-coverage, -coverage, coverage);
    }
    else // (faceSelection.w == faceMax)
    {
        shadowcoord = v_texcoord4;

        float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
        colorCoverage = vec3(coverage, -coverage, -coverage);
    }

    shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);

    visibility = computeVisibility(s_shadowMap0
                    , shadowcoord
                    , adjustedBias
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

    vec4 shadowcoord = v_shadowcoord;
    shadowcoord.z += computeReceiverPlaneDepthBias(shadowcoord, texelSize.x);

    float coverage = texcoordInRange(shadowcoord.xy/shadowcoord.w) * 0.3;
    colorCoverage = vec3(coverage, -coverage, -coverage);

    visibility = computeVisibility(s_shadowMap0
                    , shadowcoord
                    , adjustedBias
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

vec4 pbr_light(vec2 texcoord0, vec2 fragCoord)
{
    GBufferData data = DecodeGBuffer(texcoord0, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
    vec3 clip = vec3(texcoord0 * 2.0 - 1.0, data.depth);
    clip = clipTransform(clip);
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    vec3 lobe_roughness = vec3(0.0f, data.roughness, 1.0f);
    vec3 light_color = u_light_color_intensity.xyz;
    float intensity = u_light_color_intensity.w;
    // Use unmodified colors for direct lighting — AO is applied only to indirect terms inside StandardShading
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
    float NoL = saturate( dot(N, L) );

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


    vec3 colorCoverage = vec3(0.0f, 0.0f, 0.0f);
    float surface_shadow = CalculateSurfaceShadow(world_position, N, L, fragCoord,colorCoverage);
    float subsurface_shadow = 1.0f;
    float surface_attenuation = (intensity * light_radius_mask * light_falloff) * surface_shadow;
    float subsurface_attenuation = (intensity * light_radius_mask * light_falloff) * subsurface_shadow;

    vec3 energy = AreaLightSpecular(0.0f, 0.0f, normalize(vector_to_light), lobe_roughness, vector_to_light, L, V, N);
    vec3 direct_surface_lighting = StandardShadingDirect(diffuse_color, specular_color, lobe_roughness, energy, L, V, N);
    //vec3 subsurface_lighting = SubsurfaceShadingTwoSided(data.subsurface_color, L, V, N);
    vec3 subsurface_lighting = SubsurfaceShading(data.subsurface_color, data.subsurface_opacity, data.ambient_occlusion, L, V, N);
    vec3 surface_multiplier = light_color * (NoL * surface_attenuation);
    vec3 subsurface_multiplier = (light_color * subsurface_attenuation);

    // Only direct lighting — indirect and emissive are handled in a separate fullscreen pass
    vec3 lighting = surface_multiplier * direct_surface_lighting
                  + subsurface_lighting * subsurface_multiplier
                  + colorCoverage * u_shadowMapShowCoverage;

    vec4 result;
    result.xyz = lighting;
    result.w = 1.0f;
    return result;
}

// Separate indirect lighting + emissive pass (rendered once as a fullscreen quad, not per-light)
vec4 pbr_indirect(vec2 texcoord0)
{
    GBufferData data = DecodeGBuffer(texcoord0, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
    vec3 indirect_specular = texture2D(s_tex5, texcoord0).xyz;
    vec3 clip = vec3(texcoord0 * 2.0 - 1.0, data.depth);
    clip = clipTransform(clip);
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    float indirect_intensity = u_light_data.w;

    vec3 N = data.world_normal;
    vec3 V = normalize(u_camera_position.xyz - world_position);

    // Indirect diffuse carries only irradiance — StandardShadingIndirect applies DiffuseColor and AO
    vec3 indirect_diffuse = vec3_splat(indirect_intensity);

    vec3 indirect_lighting = StandardShadingIndirect(data.diffuse_color, indirect_diffuse, data.specular_color, indirect_specular, s_tex6, data.roughness, data.ambient_occlusion, V, N);

    vec4 result;
    result.xyz = indirect_lighting + data.emissive_color;
    result.w = 1.0f;
    return result;
}

#endif // __PBRLIGHTING_SH__
