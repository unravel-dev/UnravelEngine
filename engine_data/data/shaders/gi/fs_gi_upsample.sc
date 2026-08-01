$input v_texcoord0

/*
 * Joint bilateral upsample of the half-resolution gather to full resolution.
 *
 * A plain bilinear tap is wrong at every silhouette. A low-resolution texel that straddles an
 * edge holds a blend of foreground and background, and bilinear interpolation then spreads that
 * blend across both sides -- so indirect light from a bright floor bleeds onto a dark wall in a
 * one-or-two pixel fringe. Because the gather is noisiest exactly at edges, where reprojection
 * fails and the spatial filter rejects most of its taps, those fringes read as bright speckles
 * rather than as a soft halo.
 *
 * The fix is to weight the four candidate texels by whether they belong to the SAME SURFACE as
 * the pixel being shaded. The guide is exact rather than approximate: the gather sampled the
 * G-buffer at the low-resolution texel centres, so re-sampling the full-resolution G-buffer at
 * those same centres recovers precisely the surface each texel was computed for.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gi_input, 0);
SAMPLER2D(s_gi_depth, 1);
SAMPLER2D(s_gi_normal, 2);

/// xy = one texel of the LOW resolution buffer, zw = its dimensions.
uniform vec4 u_gi_upsample_texel;
/// x = normal exponent, y = plane tolerance as a fraction of view distance.
uniform vec4 u_gi_upsample_params;
#define u_gi_upsample_normal_pow u_gi_upsample_params.x
#define u_gi_upsample_plane_tol  u_gi_upsample_params.y

uniform vec4 u_gi_upsample_camera;

/// Reconstructs the world position behind a texel of the full-resolution depth buffer.
bool GiWorldAt(vec2 uv, out vec3 out_position)
{
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	out_position = vec3_splat(0.0);
	if(depth >= 1.0)
	{
		return false;
	}
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	out_position = clipToWorld(u_invViewProj, clip);
	return true;
}

void main()
{
	vec2 uv = v_texcoord0;
	vec3 center_position;
	if(!GiWorldAt(uv, center_position))
	{
		// Sky. The gather wrote zero weight here and there is nothing to reconstruct.
		gl_FragColor = texture2DLod(s_gi_input, uv, 0.0);
		return;
	}
	GBufferDataNormalMetalRoughness center_nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	vec3 center_normal = center_nd.world_normal;
	if(dot(center_normal, center_normal) < 0.5)
	{
		gl_FragColor = texture2DLod(s_gi_input, uv, 0.0);
		return;
	}
	center_normal = normalize(center_normal);
	float view_distance = max(length(center_position - u_gi_upsample_camera.xyz), 1e-4);
	float plane_tolerance = max(u_gi_upsample_plane_tol * view_distance, 1e-4);
	vec2 low_size = u_gi_upsample_texel.zw;
	// Half-texel shift puts the sample in the low-resolution texel's own coordinate frame, so the
	// four neighbours and their bilinear fractions come out right.
	vec2 sample_pos = uv * low_size - vec2_splat(0.5);
	vec2 base = floor(sample_pos);
	vec2 f = sample_pos - base;
	vec4 sum = vec4_splat(0.0);
	float weight_sum = 0.0;
	for(int j = 0; j < 2; ++j)
	{
		for(int i = 0; i < 2; ++i)
		{
			vec2 tap_texel = base + vec2(float(i), float(j)) + vec2_splat(0.5);
			vec2 tap_uv = tap_texel / low_size;
			if(any(lessThan(tap_uv, vec2_splat(0.0))) || any(greaterThan(tap_uv, vec2_splat(1.0))))
			{
				continue;
			}
			float bilinear = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);
			if(bilinear <= 0.0)
			{
				continue;
			}
			// The surface this low-resolution texel was computed for: the gather sampled the
			// G-buffer at exactly this uv, so this is an exact guide rather than an estimate.
			vec3 tap_position;
			if(!GiWorldAt(tap_uv, tap_position))
			{
				continue;
			}
			GBufferDataNormalMetalRoughness tap_nd =
			    DecodeGBufferNormalMetalRoughnessLod(tap_uv, s_gi_normal, 0.0);
			vec3 tap_normal = tap_nd.world_normal;
			if(dot(tap_normal, tap_normal) < 0.5)
			{
				continue;
			}
			tap_normal = normalize(tap_normal);
			float plane_distance = abs(dot(center_normal, tap_position - center_position));
			float depth_weight = exp(-plane_distance / plane_tolerance);
			float normal_weight = pow(max(dot(center_normal, tap_normal), 0.0), u_gi_upsample_normal_pow);
			float weight = bilinear * depth_weight * normal_weight;
			sum += texture2DLod(s_gi_input, tap_uv, 0.0) * weight;
			weight_sum += weight;
		}
	}
	// Every candidate rejected: a thin feature whose low-resolution neighbours all belong to other
	// surfaces. Falling back to the bilinear tap keeps a plausible value rather than a black hole,
	// and it is the same answer the old path always gave, so this can only be an improvement.
	if(weight_sum <= 1e-6)
	{
		gl_FragColor = texture2DLod(s_gi_input, uv, 0.0);
		return;
	}
	gl_FragColor = sum / weight_sum;
}
