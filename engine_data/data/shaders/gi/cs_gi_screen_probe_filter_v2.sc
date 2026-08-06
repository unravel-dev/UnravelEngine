/*
 * GI v2 probe-space filter + irradiance convolution (plan 3.4) - one thread group per probe.
 *
 * Filters each direction across the 3x3 probe neighbourhood - a 3-probe kernel in probe space
 * is a ~48-pixel kernel in screen space [S21 s57] - with the two guards that matter:
 *  - plane agreement between probe anchors (no borrowing across a depth break), and
 *  - the CONTACT-SHADOW-PRESERVING angle test [S21 s61]: the neighbour's hit, with its distance
 *    CLAMPED to our own, reprojected toward this probe; if the direction disagrees by more than
 *    GI_FILTER_ANGLE_LIMIT_COS the neighbour saw meaningfully different visibility. The clamp
 *    is the fix for the naive test's failure - distant hits have no parallax, always pass, and
 *    leak over local shadowing.
 *
 * Then convolves the filtered sphere into the probe's 8x8 octahedral IRRADIANCE tile
 * (E(n)/pi = sum(L cos) / (N/4)), which is what integration samples at each pixel's own normal.
 */

#include "bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"

SAMPLER2D(s_probe_radiance, 0);
/// RW: this pass also writes the probe's 4x4 importance mip into record slots 0-3 (the slots
/// the retired SH design left spare), which next frame's trace reads - through the buffer it
/// already binds - to supersample bright cones. No new bindings anywhere.
BUFFER_RW(b_gi_probes, vec4, 7);
IMAGE2D_WO(s_probe_irradiance_out, rgba16f, 2);

/// Plane tolerance as a fraction of view distance - the adaptive spatial-error rule every
/// screen-space consumer shares (GI-1.0's cell_size heuristic, here in its simplest form).
#define GI_V2_FILTER_PLANE_TOLERANCE 0.05

SHARED vec4 s_filtered[GI_PROBE_DIR_COUNT];

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	uint base = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec4 center_meta = b_gi_probes[base + uint(GI_PROBE_META)];
	vec4 center_meta2 = b_gi_probes[base + uint(GI_PROBE_META2)];
	bool center_valid = center_meta.w > 0.5;
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	vec2 dir_uv = (vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE);
	vec3 dir = GiOctDecode(dir_uv);
	vec4 filtered = vec4_splat(0.0);
	if(center_valid)
	{
		vec4 center_texel =
		    texelFetch(s_probe_radiance, GiProbeAtlasBase(probe.x, probe.y, 0) + local, 0);
		float own_hit_t = center_texel.w;
		float plane_tolerance = GI_V2_FILTER_PLANE_TOLERANCE * max(center_meta2.w, 0.1);
		float weight_sum = 0.0;
		for(int oy = -1; oy <= 1; ++oy)
		{
			for(int ox = -1; ox <= 1; ++ox)
			{
				int nx = probe.x + ox;
				int ny = probe.y + oy;
				if(nx < 0 || ny < 0 || nx >= u_gi_probe_count_x || ny >= u_gi_probe_count_y)
				{
					continue;
				}
				uint neighbour_base =
				    (GiProbeRecord(nx, ny, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
				vec4 neighbour_meta = b_gi_probes[neighbour_base + uint(GI_PROBE_META)];
				if(neighbour_meta.w < 0.5)
				{
					continue;
				}
				float plane = abs(dot(neighbour_meta.xyz - center_meta.xyz, center_meta2.xyz));
				float plane_weight = saturate(1.0 - plane / plane_tolerance);
				if(plane_weight <= 1e-3)
				{
					continue;
				}
				vec4 neighbour_value =
				    texelFetch(s_probe_radiance, GiProbeAtlasBase(nx, ny, 0) + local, 0);
				float weight = plane_weight;
				// The hitT-clamped reprojection test, only when both probes actually HIT (a
				// completed/sky texel has no parallax to test and shares freely).
				if(own_hit_t > 0.0 && neighbour_value.w > 0.0)
				{
					float clamped_t = min(neighbour_value.w, own_hit_t);
					vec3 neighbour_hit = neighbour_meta.xyz + dir * clamped_t;
					vec3 reprojected = neighbour_hit - center_meta.xyz;
					float reprojected_length = max(length(reprojected), 1e-4);
					if(dot(reprojected / reprojected_length, dir) < GI_FILTER_ANGLE_LIMIT_COS)
					{
						continue;
					}
				}
				filtered.xyz += neighbour_value.xyz * weight;
				weight_sum += weight;
			}
		}
		filtered.xyz = weight_sum > 1e-4 ? filtered.xyz / weight_sum : center_texel.xyz;
		filtered.w = 1.0;
	}
	s_filtered[dir_index] = filtered;
	barrier();
	// IMPORTANCE MIP for next frame's ray allocation: 16 blocks of 2x2 texels, each block's
	// filtered luminance, packed four blocks per record vec4 in slots 0-3. Threads 0-3 write
	// one vec4 each; the luminances come straight from shared memory, so this is free next to
	// the convolution below.
	if(center_valid && local.y == 0 && local.x < 4)
	{
		// Explicit components rather than a dynamically indexed vec4 write, which does not
		// survive every backend translation (see lessons on HLSL-only failures).
		float block_luminance[4];
		for(int b = 0; b < 4; ++b)
		{
			int block = local.x * 4 + b;
			ivec2 block_base = ivec2((block % 4) * 2, (block / 4) * 2);
			float luminance_sum = 0.0;
			for(int t = 0; t < 4; ++t)
			{
				ivec2 texel_in_block = block_base + ivec2(t % 2, t / 2);
				vec3 radiance_texel =
				    s_filtered[texel_in_block.y * GI_PROBE_DIR_EDGE + texel_in_block.x].xyz;
				luminance_sum += dot(radiance_texel, vec3(0.299, 0.587, 0.114));
			}
			block_luminance[b] = luminance_sum * 0.25;
		}
		b_gi_probes[base + uint(local.x)] =
		    vec4(block_luminance[0], block_luminance[1], block_luminance[2], block_luminance[3]);
	}
	// Cosine convolution to irradiance at THIS texel's normal direction - equal-solid-angle
	// octahedral texels make the plain cosine-weighted sum exact up to quadrature.
	vec3 normal = GiOctDecode(dir_uv);
	vec4 irradiance = vec4_splat(0.0);
	if(center_valid)
	{
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			vec2 sample_uv = (vec2(float(d % GI_PROBE_DIR_EDGE), float(d / GI_PROBE_DIR_EDGE)) +
			                  vec2_splat(0.5)) /
			                 float(GI_PROBE_DIR_EDGE);
			float cosine = max(dot(normal, GiOctDecode(sample_uv)), 0.0);
			irradiance.xyz += s_filtered[d].xyz * cosine;
			irradiance.w += s_filtered[d].w * cosine;
		}
		float norm = float(GI_PROBE_DIR_COUNT) * 0.25;
		irradiance.xyz /= norm;
		// w becomes the measured fraction of the cosine lobe: texels the trace refused (the
		// below-tangent cap of an invalid neighbour set) hand their share to integration's
		// weighting rather than reading as darkness.
		irradiance.w /= norm;
	}
	imageStore(s_probe_irradiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, irradiance);
}
