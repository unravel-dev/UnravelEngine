$input a_position, a_normal, a_tangent, a_bitangent, a_texcoord0
$output v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params

#include "../common.sh"

uniform vec4 u_lod_params;

#if DEFERRED_GEOM_CARD_CAPTURE
/// The surface cache card capture (vs_deferred_geom_card_capture.sc): each draw's own world -> clip transform, which
/// places the capture in its tile of the capture atlas, so one pass draws every capture.
uniform mat4 u_card_capture_view_proj;
#define DEFERRED_GEOM_VIEW_PROJ u_card_capture_view_proj
#else
#define DEFERRED_GEOM_VIEW_PROJ u_viewProj
#endif

void main()
{
    vec4 wpos = mul(u_world[0], vec4(a_position, 1.0) );
    gl_Position = mul(DEFERRED_GEOM_VIEW_PROJ, wpos );

	vec4 normal = a_normal * 2.0 - 1.0;
	vec4 tangent = a_tangent * 2.0 - 1.0;
	vec4 bitangent = a_bitangent * 2.0 - 1.0;

    mat3 modelIT = calculateInverseTranspose(u_world[0]);
	
	vec3 wnormal = normalize(mul(modelIT, normal.xyz ));
	vec3 wtangent = normalize(mul(modelIT, tangent.xyz ));
	vec3 wbitangent = normalize(mul(modelIT, bitangent.xyz ));
	
	v_wpos = wpos.xyz;
	v_pos = gl_Position.xyz/gl_Position.w;

	v_wnormal   = wnormal;
	v_wtangent   = wtangent;
	v_wbitangent = wbitangent;

	v_texcoord0 = a_texcoord0;
	v_lod_params = u_lod_params.xy;

}
