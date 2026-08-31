$input v_texcoord0

/*
 * Intensity-normalised scene-color snapshot for the GI feedback loop.
 *
 * The gather's screen-tier hits and far-field fallback read LAST frame's composited scene
 * color - that is what closes the infinite-bounce loop, and its convergence rests on every
 * cycle multiplying by albedo < 1. The artistic GI intensity multiplier breaks that contract
 * when it rides the loop: the composite carries intensity x bounce, the gather re-ingests it,
 * and the effective cycle gain becomes intensity x albedo x view-factor - supercritical FIRST
 * at creases and in enclosed rooms, where opposing faces see each other at view factors near
 * one (measured: glowing crease lines that brighten with the intensity slider, and a sealed
 * room running away to its clamps).
 *
 * This pass rebuilds the snapshot with the bounce term UNSCALED: the GI resolve's rgb carries
 * intensity x G and its alpha the weight it replaced the environment with, so the composite's
 * scaled indirect diffuse is diffuse_color x gi.rgb x gi.a (x ambient occlusion, which the
 * shading applied). Subtracting the intensity excess restores outgoing = albedo x E/pi in the
 * history - display keeps the artistic scale, transport stays physical. For intensity < 1 the
 * correction ADDS the deficit back for the same reason. The specular share of the GI layer is
 * not intensity-scaled and needs no correction.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_gi_resolve, 1);
SAMPLER2D(s_tex0, 2);
SAMPLER2D(s_tex1, 3);
SAMPLER2D(s_tex2, 4);
SAMPLER2D(s_tex3, 5);
SAMPLER2D(s_tex4, 6);

/// x = the GI intensity the resolve applied this frame; yzw unused.
uniform vec4 u_gi_feedback_params;

void main()
{
	vec3 scene = texture2D(s_scene, v_texcoord0).xyz;
	vec4 gi = texture2D(s_gi_resolve, v_texcoord0);
	float intensity = max(u_gi_feedback_params.x, 1e-3);
	GBufferData data = DecodeGBuffer(v_texcoord0, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
	// gi.rgb already carries the intensity factor, so the unscaled bounce is gi.rgb / I and
	// the excess in the composite is diffuse x gi.rgb x gi.a x ao x (1 - 1/I).
	vec3 excess = data.diffuse_color * gi.xyz * gi.w * data.ambient_occlusion *
	              (1.0 - 1.0 / intensity);
	gl_FragColor = vec4(max(scene - excess, vec3_splat(0.0)), 1.0);
}
