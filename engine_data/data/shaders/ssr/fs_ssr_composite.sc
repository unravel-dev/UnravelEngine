$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

// Temporally filtered SSR result (rgb = filtered color, a = normalized weight)
SAMPLER2D(s_ssr_history, 0);
// Current frame SSR result (rgb = color, a = confidence)
SAMPLER2D(s_ssr_curr, 1);
// Normal buffer for Fresnel calculation
SAMPLER2D(s_normal, 2);
// Depth buffer
SAMPLER2D(s_depth, 3);

#define MAX_ROUGHNESS 0.6

float GetRoughnessFade(float roughness)
{
    return MAX_ROUGHNESS - min(roughness, MAX_ROUGHNESS);
}

float GetRoughnessVisibility(float roughness)
{
    //return 1.0 - smoothstep(0.35, 0.8, roughness);
    return GetRoughnessFade(roughness);
}


void main()
{
    vec2 uv = v_texcoord0;

    // Current hit confidence drives replacement of the probe fallback.
    vec4 curr_ssr = texture2D(s_ssr_curr, uv);
    float ssr_confidence = curr_ssr.a;

    // History stabilizes color only; it should not keep overwriting the probe when
    // the current frame has a weak/missing screen-space hit.
    vec4 ssr_history = texture2D(s_ssr_history, uv);
    float history_weight = saturate(ssr_history.a);
    vec3 ssr_color = mix(curr_ssr.rgb, ssr_history.rgb, history_weight);

    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(uv, s_normal);

    // SSR is reliable for smooth reflections and intentionally fades out as the lobe
    // gets too wide for screen-space tracing. Probe data remains the fallback there.
    float roughness_visibility = GetRoughnessVisibility(normal_data.roughness);
    float enhanced_confidence = saturate(ssr_confidence * roughness_visibility);
    
    // Output SSR color with confidence alpha for hardware blending with probe buffer
    gl_FragColor = vec4(ssr_color, enhanced_confidence);
} 