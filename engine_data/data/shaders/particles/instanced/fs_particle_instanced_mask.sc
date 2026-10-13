$input v_color0, v_texcoord0, v_texcoord1, v_particle

#include <bgfx_shader.sh>
#include "../../pre_exposure.sh"
#include "../../shaderlib.sh"

SAMPLER2D(s_texColor, 0);
// The view's opaque depth (device depth in .x), when u_particleParams.x is 1.
SAMPLER2D(s_sceneDepth, 1);
uniform vec4 u_particleParams;

/// The texel at @p uv, crossfaded into the next texture sheet frame's texel at @p nextUv by @p blend
/// (0 without frame blending). Both are sampled outside any branch: implicit-gradient samples must not
/// sit in per-pixel flow control.
vec4 sampleParticleTexture(vec2 uv, vec2 nextUv, float blend)
{
	return mix(texture2D(s_texColor, uv), texture2D(s_texColor, nextUv), blend);
}

/// Soft particles: 0 where the particle at @p fragCoord touches the scene behind it, 1 at
/// @p fadeDistance metres in front of it. @p particleDepth is the particle's view depth.
float softFade(vec4 fragCoord, float fadeDistance, float particleDepth)
{
	float fade = 1.0;
	if(u_particleParams.x > 0.5 && fadeDistance > 0.0)
	{
		vec2 uv = fragCoord.xy * u_viewTexel.xy;
		float sceneDepth = screenSpaceToViewSpaceDepth(texture2DLod(s_sceneDepth, uv, 0.0).x);
		fade = clamp((sceneDepth - particleDepth) / fadeDistance, 0.0, 1.0);
	}
	return fade;
}

void main()
{
    vec4 maskColor = sampleParticleTexture(v_texcoord0.xy, v_texcoord1.xy, v_particle.x);
    
    // Luminance (perceived brightness) as opacity
    float maskAlpha = dot(maskColor.rgb, vec3(0.299, 0.587, 0.114));
    
    // Mask RGB tints the particle, multiplied by particle color
    vec3 finalColor = maskColor.rgb * v_color0.rgb * u_pre_exposure_value;
    
    // Alpha combines particle alpha with mask brightness
    float finalAlpha = v_color0.a * maskAlpha * softFade(gl_FragCoord, v_particle.y, v_particle.z);
    
    gl_FragColor = vec4(finalColor, finalAlpha);
}

