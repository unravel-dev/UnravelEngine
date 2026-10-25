$input v_color0, v_texcoord0, v_texcoord1, v_particle

/*
 * Mask textures (particle_fragment.sh, particle_shade_mask).
 */

#include <bgfx_shader.sh>
// Unlit particles read the volumetric fog's froxel column (particle_fragment.sh).
#define FOG_VOLUME_SAMPLER s_particle_fog_volume
#include "particle_fragment.sh"

void main()
{
	vec4 color;
	vec4 reactive;
	particle_shade_mask(v_color0, v_texcoord0, v_texcoord1, v_particle, gl_FragCoord, vec3_splat(1.0), color, reactive);
	gl_FragData[0] = color;
	gl_FragData[1] = reactive;
}
