/*
 * Short-range ambient occlusion by a horizon search: the occlusion detail below the screen probe lattice, which the
 * probes cannot resolve.
 *
 * One thread per texel of the tier's layout (lumen_short_range_ao.sh: every pixel at Epic, half resolution at High)
 * searches from its pixel. Along LUMEN_SHORT_RANGE_AO_SLICE_COUNT screen slices it finds the highest
 * occluding horizon on both sides within LUMEN_SHORT_RANGE_AO_RADIUS_PROBE_TILES probe tiles, from
 * LUMEN_SHORT_RANGE_AO_STEPS_PER_SLICE depth samples spaced quadratically; a sample further in front of the pixel
 * than LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_DISTANCE of its depth fades out of the horizon, shaped by the tier's
 * reject power. The cosine-weighted visibility between the horizons and the bent normal are integrated analytically
 * per slice (the GTAO integrals), normalized by the projected normal's full-visibility integral.
 *
 * Everything is in world space: a slice's direction is toward the point one pixel along it at the pixel's depth,
 * made orthogonal to the view vector, so the horizon math matches the screen samples whatever the view
 * convention. Depth comes from the full-resolution depth buffer, not a Hi-Z mip.
 *
 * Writes the unit bent normal (world) and the visibility packed in one uint (LumenPackShortRangeAO); the sky writes
 * +z at visibility 1.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_short_range_ao.sh"

SAMPLER2D(s_lumen_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 1);
/// The search, packed (LumenPackShortRangeAO).
UIMAGE2D_WO(s_lumen_short_range_ao_out, r32ui, 2);

/// @p horizon_cos raised to the sample at @p sample_uv: the cosine between the view vector and the direction to the
/// sample, faded toward @p low_horizon_cos as the sample lies further in front.
float LumenUpdateHorizon(vec2 sample_uv,
                         vec3 position,
                         vec3 view_vector,
                         float scene_depth,
                         float inv_foreground_distance,
                         float low_horizon_cos,
                         float horizon_cos)
{
	if(any(lessThan(sample_uv, vec2_splat(0.0))) || any(greaterThanEqual(sample_uv, vec2_splat(1.0))))
	{
		return horizon_cos;
	}
	ivec2 texel = min(ivec2(sample_uv * u_lumen_view_size), ivec2(u_lumen_view_size) - ivec2(1, 1));
	float sample_depth01 = texelFetch(s_lumen_depth, texel, 0).x;
	if(sample_depth01 >= 1.0)
	{
		return horizon_cos;
	}
	vec3 delta = LumenWorldFromDepth(sample_uv, sample_depth01) - position;
	float sample_distance = length(delta);
	if(sample_distance <= 0.0)
	{
		return horizon_cos;
	}
	float new_cos = dot(delta / sample_distance, view_vector);
	float depth_delta = abs(LumenLinearDepth(sample_depth01) - scene_depth);
	new_cos = mix(new_cos,
	              low_horizon_cos,
	              saturate(pow(depth_delta * inv_foreground_distance, u_lumen_short_range_ao_reject_power)));
	return max(horizon_cos, new_cos);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	int factor = u_lumen_short_range_ao_downsample_factor;
	ivec2 search_size = (ivec2(u_lumen_view_size) + ivec2(factor - 1, factor - 1)) / factor;
	if(texel.x >= search_size.x || texel.y >= search_size.y)
	{
		return;
	}
	ivec2 pixel = LumenShortRangeAOPixel(texel);
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		imageStore(s_lumen_short_range_ao_out, texel, uvec4(LumenPackShortRangeAO(vec4(0.0, 0.0, 1.0, 1.0)), 0u, 0u, 0u));
		return;
	}
	vec2 uv = LumenPixelUv(pixel);
	vec3 position = LumenWorldFromDepth(uv, depth01);
	float scene_depth = LumenLinearDepth(depth01);
	vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 view_vector = normalize(camera - position);
	vec3 normal = decodeNormalOctahedron(texelFetch(s_lumen_normal, pixel, 0).xy);
	float inv_foreground_distance = 1.0 / (scene_depth * LUMEN_SHORT_RANGE_AO_FOREGROUND_REJECT_DISTANCE);
	vec2 noise = LumenShortRangeAONoise(pixel, 0.0);
	float radius_pixels = LUMEN_SHORT_RANGE_AO_RADIUS_PROBE_TILES * u_lumen_downsample;
	float visibility = 0.0;
	float correction = 0.0;
	vec3 bent_normal = vec3_splat(0.0);
	for(int slice = 0; slice < LUMEN_SHORT_RANGE_AO_SLICE_COUNT; ++slice)
	{
		float phi = (float(slice) + noise.x) / float(LUMEN_SHORT_RANGE_AO_SLICE_COUNT) * LUMEN_PI;
		vec2 direction = vec2(cos(phi), sin(phi));
		vec3 along = LumenWorldFromDepth(uv + direction * u_lumen_view_texel, depth01) - position;
		vec3 slice_direction = along - dot(along, view_vector) * view_vector;
		float slice_length = length(slice_direction);
		if(slice_length <= 1e-8)
		{
			continue;
		}
		slice_direction /= slice_length;
		vec3 axis = normalize(cross(slice_direction, view_vector));
		vec3 projected_normal = normal - axis * dot(normal, axis);
		float projected_length = length(projected_normal);
		if(projected_length <= 1e-6)
		{
			continue;
		}
		float sign_normal = dot(slice_direction, projected_normal) >= 0.0 ? 1.0 : -1.0;
		float cos_normal = saturate(dot(projected_normal, view_vector) / projected_length);
		float normal_angle = sign_normal * acos(cos_normal);
		float sin_normal = sin(normal_angle);
		float low_horizon_cos0 = cos(normal_angle + 0.5 * LUMEN_PI);
		float low_horizon_cos1 = cos(normal_angle - 0.5 * LUMEN_PI);
		float horizon_cos0 = low_horizon_cos0;
		float horizon_cos1 = low_horizon_cos1;
		for(int step = 0; step < LUMEN_SHORT_RANGE_AO_STEPS_PER_SLICE; ++step)
		{
			float fraction = (float(step) + noise.y) / float(LUMEN_SHORT_RANGE_AO_STEPS_PER_SLICE);
			// More samples near the pixel; one pixel of offset keeps the pixel from occluding itself.
			vec2 offset = direction * (fraction * fraction * radius_pixels + 1.0) * u_lumen_view_texel;
			horizon_cos0 = LumenUpdateHorizon(uv + offset, position, view_vector, scene_depth, inv_foreground_distance,
			                                  low_horizon_cos0, horizon_cos0);
			horizon_cos1 = LumenUpdateHorizon(uv - offset, position, view_vector, scene_depth, inv_foreground_distance,
			                                  low_horizon_cos1, horizon_cos1);
		}
		float h0 = -acos(clamp(horizon_cos1, -1.0, 1.0));
		float h1 = acos(clamp(horizon_cos0, -1.0, 1.0));
		float arc0 = (cos_normal + 2.0 * h0 * sin_normal - cos(2.0 * h0 - normal_angle)) / 4.0;
		float arc1 = (cos_normal + 2.0 * h1 * sin_normal - cos(2.0 * h1 - normal_angle)) / 4.0;
		visibility += projected_length * (arc0 + arc1);
		float t0 = (6.0 * sin(h0 - normal_angle) - sin(3.0 * h0 - normal_angle) + 6.0 * sin(h1 - normal_angle) -
		            sin(3.0 * h1 - normal_angle) + 16.0 * sin_normal -
		            3.0 * (sin(h0 + normal_angle) + sin(h1 + normal_angle))) / 12.0;
		float t1 = (-cos(3.0 * h0 - normal_angle) - cos(3.0 * h1 - normal_angle) + 8.0 * cos(normal_angle) -
		            3.0 * (cos(h0 + normal_angle) + cos(h1 + normal_angle))) / 12.0;
		bent_normal += (slice_direction * t0 + view_vector * t1) * projected_length;
		// The projected normal's integral over the full hemisphere: without it a surface at a grazing angle to
		// the view never reaches full visibility.
		correction += projected_length * (normal_angle * sin_normal + cos_normal);
	}
	visibility = correction > 0.0 ? max(visibility / correction, LUMEN_SHORT_RANGE_AO_MIN_VISIBILITY) : 1.0;
	float bent_length = length(bent_normal);
	vec3 bent = bent_length > 1e-6 ? bent_normal / bent_length : normal;
	imageStore(s_lumen_short_range_ao_out, texel, uvec4(LumenPackShortRangeAO(vec4(bent, visibility)), 0u, 0u, 0u));
}
