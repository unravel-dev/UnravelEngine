$input v_color0, v_texcoord0, v_texcoord1, v_particle, v_light_ambient, v_light_sun, v_sun_local, v_quad_uv

/*
 * Lit particles (vs_particle_lit.sc): the particle's color is its albedo, lit per particle and, for surface
 * lighting, by the sun per pixel.
 */

#include <bgfx_shader.sh>
#include "particle_fragment.sh"

void main()
{
	vec3 light = particle_pixel_light(v_light_ambient, v_light_sun, v_sun_local, v_quad_uv, v_texcoord0.xy, v_texcoord1.xy, v_particle.x);
	vec4 color;
	vec4 reactive;
	particle_shade_mask(v_color0, v_texcoord0, v_texcoord1, v_particle, gl_FragCoord, light, color, reactive);
	gl_FragData[0] = color;
	gl_FragData[1] = reactive;
}
