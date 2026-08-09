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
#define ROUGHNESS_FADE_START 0.3

float GetRoughnessVisibility(float roughness)
{
    // FLAT-TOP fade: full strength wherever SSR is confident, handing off to the layer
    // beneath (the GI world-reflection tier) only as the lobe grows too wide for
    // screen-space tracing. The old form returned the UNNORMALIZED leftover
    // (MAX_ROUGHNESS - roughness), so even a perfect mirror composited at 0.6 alpha and a
    // 0.2-rough surface at 0.4 - confident screen hits could never win over the fallback,
    // which read as the GI layer "overpowering" SSR and as SSR-only reflections at 40
    // percent of their true brightness.
    return 1.0 - smoothstep(ROUGHNESS_FADE_START, MAX_ROUGHNESS, roughness);
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