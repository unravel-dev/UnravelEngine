/*
 * GI probe-space filter + irradiance convolution (plan 3.4) - one thread group per probe.
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
#define GI_FILTER_PLANE_TOLERANCE 0.05

SHARED vec4 s_filtered[GI_PROBE_DIR_COUNT];
/// The 3x3 neighbourhood's metas and plane weights are per-GROUP quantities: staged once by
/// nine threads instead of being re-derived by all 64 (that was 704 buffer loads per probe
/// where 11 carry information).
SHARED vec4 s_nb_meta[9];
SHARED float s_nb_weight[9];
/// Anchor-to-anchor distance, the parallax baseline of the adaptive angle test below.
SHARED float s_nb_baseline[9];
/// Every thread's decoded direction, for the convolution below: 64 threads re-decoding all
/// 64 directions ran GiOctDecode (a normalize among other things) 4096 times per probe for
/// 64 distinct values each thread already computed once.
SHARED vec3 s_dir[GI_PROBE_DIR_COUNT];
/// Every texel's solid angle (GiOctTexelSolidAngle): the octahedral map is not equal-area.
SHARED float s_omega[GI_PROBE_DIR_COUNT];

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
	s_dir[dir_index] = dir;
	s_omega[dir_index] = GiOctTexelSolidAngle(local, GI_PROBE_DIR_EDGE);
	if(dir_index < 9)
	{
		int ox = dir_index % 3 - 1;
		int oy = dir_index / 3 - 1;
		int nx = probe.x + ox;
		int ny = probe.y + oy;
		float plane_weight = 0.0;
		vec4 neighbour_meta = vec4_splat(0.0);
		if(center_valid && nx >= 0 && ny >= 0 && nx < u_gi_probe_count_x && ny < u_gi_probe_count_y)
		{
			uint neighbour_base =
			    (GiProbeRecord(nx, ny, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
			neighbour_meta = b_gi_probes[neighbour_base + uint(GI_PROBE_META)];
			if(neighbour_meta.w > 0.5)
			{
				float plane_tolerance = GI_FILTER_PLANE_TOLERANCE * max(center_meta2.w, 0.1);
				float plane = abs(dot(neighbour_meta.xyz - center_meta.xyz, center_meta2.xyz));
				plane_weight = saturate(1.0 - plane / plane_tolerance);
			}
		}
		s_nb_meta[dir_index] = neighbour_meta;
		s_nb_weight[dir_index] = plane_weight;
		s_nb_baseline[dir_index] = length(neighbour_meta.xyz - center_meta.xyz);
	}
	barrier();
	vec4 filtered = vec4_splat(0.0);
	if(center_valid)
	{
		vec4 center_texel =
		    texelFetch(s_probe_radiance, GiProbeAtlasBase(probe.x, probe.y, 0) + local, 0);
		float own_hit_t = center_texel.w;
		float weight_sum = 0.0;
		// Same neighbour order as the old oy-outer / ox-inner walk, so the summation order -
		// and with it the floating-point result - is unchanged.
		for(int n = 0; n < 9; ++n)
		{
			float weight = s_nb_weight[n];
			if(weight <= 1e-3)
			{
				continue;
			}
			vec4 neighbour_meta = s_nb_meta[n];
			int nx = probe.x + n % 3 - 1;
			int ny = probe.y + n / 3 - 1;
			vec4 neighbour_value =
			    texelFetch(s_probe_radiance, GiProbeAtlasBase(nx, ny, 0) + local, 0);
			// The hitT-clamped reprojection test, only when both probes actually HIT (a
			// completed/sky texel has no parallax to test and shares freely). The limit is
			// PARALLAX-ADAPTIVE: a co-planar neighbour's hit reprojects with an error of
			// about baseline/hitT purely from geometry, so a fixed pi/50 rejected ALL
			// sharing for hits within ~16 baselines - exactly the voxel-read band, whose
			// per-probe sampling bias then stood unfiltered as wall blotches. The accepted
			// angle is GI_FILTER_PARALLAX_SCALE x that intrinsic error, capped
			// (GI_FILTER_ANGLE_RELAX_MAX - contact scale stays the screen trace's), floored
			// by the published pi/50 for the far field (min of cosines = wider angle wins).
			if(own_hit_t > 0.0 && neighbour_value.w > 0.0)
			{
				float clamped_t = min(neighbour_value.w, own_hit_t);
				vec3 neighbour_hit = neighbour_meta.xyz + dir * clamped_t;
				vec3 reprojected = neighbour_hit - center_meta.xyz;
				float reprojected_length = max(length(reprojected), 1e-4);
				float parallax = GI_FILTER_PARALLAX_SCALE * s_nb_baseline[n] /
				                 max(clamped_t, 1e-3);
				float limit_cos = min(GI_FILTER_ANGLE_LIMIT_COS,
				                      cos(min(parallax, GI_FILTER_ANGLE_RELAX_MAX)));
				if(dot(reprojected / reprojected_length, dir) < limit_cos)
				{
					continue;
				}
			}
			filtered.xyz += neighbour_value.xyz * weight;
			weight_sum += weight;
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
				luminance_sum += dot(radiance_texel, vec3(0.2126, 0.7152, 0.0722));
			}
			block_luminance[b] = luminance_sum * 0.25;
		}
		b_gi_probes[base + uint(local.x)] =
		    vec4(block_luminance[0], block_luminance[1], block_luminance[2], block_luminance[3]);
	}
	// Cosine convolution to irradiance at THIS texel's normal direction, each sample weighted
	// by its texel's SOLID ANGLE (the octahedral map is not equal-area - see
	// GiOctTexelSolidAngle) and normalised by sum(cos x omega), so a uniform radiance field
	// integrates to exactly E/pi = L (the equal-weight sum over N/4 under-counted by ~8% and
	// biased individual directions by up to 47%). The sample directions come from shared
	// memory: each is the decode some thread already did.
	vec3 normal = dir;
	vec4 irradiance = vec4_splat(0.0);
	if(center_valid)
	{
		float cos_omega_sum = 0.0;
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			float weight = max(dot(normal, s_dir[d]), 0.0) * s_omega[d];
			irradiance.xyz += s_filtered[d].xyz * weight;
			irradiance.w += s_filtered[d].w * weight;
			cos_omega_sum += weight;
		}
		float norm = max(cos_omega_sum, 1e-6);
		irradiance.xyz /= norm;
		// w becomes the measured fraction of the cosine lobe: texels the trace refused (the
		// below-tangent cap of an invalid neighbour set) hand their share to integration's
		// weighting rather than reading as darkness.
		irradiance.w /= norm;
	}
	imageStore(s_probe_irradiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, irradiance);
}
