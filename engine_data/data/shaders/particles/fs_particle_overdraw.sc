$input v_color0, v_texcoord0, v_texcoord1, v_particle

/*
 * Particle overdraw (the debug view): every fragment a particle rasterizes counts, including the texels
 * its texture leaves transparent - that is the fill its draw pays. The draw's blend factor weights the
 * count (a half-resolution particle shades a quarter of the pixels it covers).
 */

#include <bgfx_shader.sh>

void main()
{
	gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
