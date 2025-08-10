$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_ssr_history, 0);  // Temporally filtered SSR result (rgb = filtered color, a = normalized weight)
SAMPLER2D(s_ssr_curr, 1);     // Current frame SSR result (rgb = color, a = confidence)
SAMPLER2D(s_normal, 2);       // Normal buffer for Fresnel calculation
SAMPLER2D(s_depth, 3);        // Depth buffer

#define MAX_ROUGHNESS 0.6

float GetRoughnessFade(float roughness)
{
    return MAX_ROUGHNESS - min(roughness, MAX_ROUGHNESS);
}

void main()
{
    vec2 uv = v_texcoord0;
    
    // Sample current frame confidence (this drives the blending)
    vec4 curr_ssr = texture2D(s_ssr_curr, uv);
    float ssr_confidence = curr_ssr.a;

    
    // Sample temporally filtered SSR color
    vec4 ssr_history = texture2D(s_ssr_history, uv);
    vec3 ssr_color = ssr_history.rgb;
	
	float stability = ssr_history.a;                  // already 0-1
	float alpha = clamp(ssr_confidence + (1.0 - ssr_confidence) * stability, 0.0, 1.0);
	//float alpha     = mix(ssr_confidence,           // current frame trust
    //                  1.0,                      // fall back to history
    //                  stability * (1.0 - ssr_confidence));
	//return;
    // Sample G-buffer data for enhanced blending
    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(uv, s_normal);

    // Roughness-based blending: smoother surfaces prefer SSR
    float roughness_factor = GetRoughnessFade(normal_data.roughness);
    
    // Metallic surfaces get more SSR contribution
    float metallic_factor = normal_data.metalness;
    
    // Combine all factors for final blend weight
    float enhanced_confidence = alpha * 
                               mix(0.5, 1.0, roughness_factor) * // Roughness preference  
                               mix(0.8, 1.0, metallic_factor);   // Metallic preference
    
    enhanced_confidence = clamp(enhanced_confidence, 0.0, 1.0);
    
    // Output SSR color with confidence alpha for hardware blending with probe buffer
    gl_FragColor = vec4(ssr_color, enhanced_confidence);
} 