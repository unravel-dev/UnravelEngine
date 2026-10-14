$input v_color0, v_texcoord0, v_texcoord1, v_particle

#include <bgfx_shader.sh>
#include "particle_fragment.sh"

void main()
{
	vec4 color;
	vec4 reactive;
	particle_shade_multi_channel(v_color0, v_texcoord0, v_texcoord1, v_particle, gl_FragCoord, vec3_splat(1.0), color, reactive);
	gl_FragData[0] = color;
	gl_FragData[1] = reactive;
}
