$input v_texcoord0

/*
 * The particle overdraw view: the layers of particles each pixel shaded (fs_particle_overdraw.sc), as a
 * heat ramp blended over the darkened scene. The colors are the legend's (visualization_modes.cpp).
 */

#include "../common.sh"

// Particle layers per pixel; a half-resolution particle counts a quarter.
SAMPLER2D(s_overdraw, 0);

// Opacity of the black laid over the pixels no particle shaded: the scene stays readable behind the ramp.
#define PARTICLE_OVERDRAW_DARKEN 0.8
// Fewest layers the view shows: below it a pixel counts as empty.
#define PARTICLE_OVERDRAW_MIN_LAYERS 0.01
// log2 of the layers at the ramp's first key (a quarter: one half-resolution particle) and its last (32).
#define PARTICLE_OVERDRAW_FIRST_KEY -2.0
#define PARTICLE_OVERDRAW_LAST_KEY 5.0

// The ramp's keys at 1/4, 1, 2, 4, 8, 16 and 32 layers.
vec3 particle_overdraw_key(int index)
{
	if(index <= 0)
	{
		return vec3(0.0, 0.05, 0.3);
	}
	if(index == 1)
	{
		return vec3(0.0, 0.25, 1.0);
	}
	if(index == 2)
	{
		return vec3(0.0, 0.8, 0.8);
	}
	if(index == 3)
	{
		return vec3(0.2, 0.9, 0.1);
	}
	if(index == 4)
	{
		return vec3(1.0, 0.85, 0.0);
	}
	if(index == 5)
	{
		return vec3(1.0, 0.15, 0.0);
	}
	return vec3(1.0, 1.0, 1.0);
}

vec3 particle_overdraw_color(float layers)
{
	float key = clamp(log2(layers), PARTICLE_OVERDRAW_FIRST_KEY, PARTICLE_OVERDRAW_LAST_KEY);
	// The first key sits two octaves below one layer, then one key per octave.
	float position = key < 0.0 ? (key - PARTICLE_OVERDRAW_FIRST_KEY) * 0.5 : key + 1.0;
	int lower = int(floor(position));
	return mix(particle_overdraw_key(lower), particle_overdraw_key(lower + 1), position - float(lower));
}

void main()
{
	float layers = texture2DLod(s_overdraw, v_texcoord0, 0.0).x;
	if(layers < PARTICLE_OVERDRAW_MIN_LAYERS)
	{
		gl_FragColor = vec4(0.0, 0.0, 0.0, PARTICLE_OVERDRAW_DARKEN);
		return;
	}
	gl_FragColor = vec4(particle_overdraw_color(layers), 1.0);
}
