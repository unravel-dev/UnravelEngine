$input v_texcoord0

/*
 * Composites the half-resolution particles over the scene: color + scene x transmittance, and their
 * reactive coverage into the full-resolution one. Where the four low-resolution texels around a pixel
 * hold its depth the target is filtered bilinearly; across a depth edge the texel nearest in depth
 * stands alone, so particles behind a foreground object do not bleed onto it.
 */

#include "../common.sh"

// Premultiplied particle light in rgb, the transmittance in alpha.
SAMPLER2D(s_half_color, 0);
SAMPLER2D(s_half_reactive, 1);
// The half-resolution device depth the particles were tested against (fs_particle_half_depth.sc).
SAMPLER2D(s_half_depth, 2);
// The view's full-resolution device depth.
SAMPLER2D(s_depth, 3);

// Largest view-depth difference, relative to the pixel's depth, at which a low-resolution texel still
// counts as the pixel's surface.
#define PARTICLE_UPSAMPLE_DEPTH_TOLERANCE 0.05
// Transmittance from which four low-resolution texels without particle light count as empty: the scene they leave
// is darkened by less than 0.1%.
#define PARTICLE_EMPTY_TRANSMITTANCE 0.999

float particle_view_depth(float device_depth)
{
	return abs(screenSpaceToViewSpaceDepth(device_depth));
}

void main()
{
	ivec2 full_last = textureSize(s_depth, 0) - ivec2(1, 1);
	ivec2 half_size = textureSize(s_half_depth, 0);
	ivec2 half_last = half_size - ivec2(1, 1);
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	// The pixel centre in low-resolution texel units, and the 2x2 texels a bilinear fetch blends there.
	vec2 half_position = (vec2(pixel) + vec2_splat(0.5)) * vec2(half_size) / vec2(full_last + ivec2(1, 1));
	vec2 uv = half_position / vec2(half_size);
	vec4 filtered_color = texture2DLod(s_half_color, uv, 0.0);
	// No particle in the four texels: the blend would leave the scene and the coverage as they are. Most of the
	// screen, so the depth-aware upsample below runs only where particles are.
	if(filtered_color.a >= PARTICLE_EMPTY_TRANSMITTANCE && all(equal(filtered_color.rgb, vec3_splat(0.0))))
	{
		discard;
	}
	float depth = particle_view_depth(texelFetch(s_depth, min(pixel, full_last), 0).x);
	ivec2 base = ivec2(floor(half_position - vec2_splat(0.5)));
	float largest_difference = 0.0;
	float nearest_difference = 1e30;
	ivec2 nearest = clamp(base, ivec2(0, 0), half_last);
	for(int y = 0; y < 2; ++y)
	{
		for(int x = 0; x < 2; ++x)
		{
			ivec2 texel = clamp(base + ivec2(x, y), ivec2(0, 0), half_last);
			float difference = abs(particle_view_depth(texelFetch(s_half_depth, texel, 0).x) - depth);
			largest_difference = max(largest_difference, difference);
			if(difference < nearest_difference)
			{
				nearest_difference = difference;
				nearest = texel;
			}
		}
	}
	float filtered_reactive = texture2DLod(s_half_reactive, uv, 0.0).x;
	vec4 nearest_color = texelFetch(s_half_color, nearest, 0);
	float nearest_reactive = texelFetch(s_half_reactive, nearest, 0).x;
	bool is_same_surface = largest_difference <= PARTICLE_UPSAMPLE_DEPTH_TOLERANCE * depth;
	gl_FragData[0] = is_same_surface ? filtered_color : nearest_color;
	gl_FragData[1] = vec4(is_same_surface ? filtered_reactive : nearest_reactive, 0.0, 0.0, 0.0);
}
