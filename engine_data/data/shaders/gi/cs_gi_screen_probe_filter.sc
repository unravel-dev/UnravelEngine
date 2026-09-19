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
 * SKIPPED REGIONS FILTER AT THE PARENT LATTICE. Where the adaptive gather interpolated probes
 * (mode 2) the surface was SAMPLED at the even-lattice parents - one traced tile per 2x2 - while
 * a stride-1 kernel reached half the footprint that sample density needs, and most of what it
 * reached were blends of the centre's own rays. The radiance-only passes therefore tap at
 * GI_FILTER_PARENT_STRIDE wherever the centre probe or one of its eight direct neighbours is
 * interpolated; the final pass stays at stride 1 and closes the a-trous gaps. Measured
 * 2026-09-19 (Sponza, adaptive probes, 3 passes; Indirect rest std p95 | rest frame delta p95 |
 * turn 2 deg/f delta | residual that moves under a 3 degree turn): spacing 8 0.91 | 0.69 | 1.22
 * | 5.5 -> 0.61 | 0.51 | 1.05 | 4.2 (adaptive off: 0.64 | 0.43 | 0.92 | 2.8); spacing 16 with the
 * spatial denoise 0.90 | 0.61 | 1.17 | 7.8 -> 0.72 | 0.50 | 1.11 | 6.5 (off: 0.75 | 0.49 | 0.98 |
 * 6.1), for +0.005 ms. The price is the look: soft lighting on flat surfaces blurs to about what
 * the parent spacing's own filter does (converged on-vs-off difference 1.3-2.7 -> 2.8-4.4 levels
 * at spacing 8). Striding the final pass too measured no quieter and a further 20% off the look.
 *
 * Then projects the filtered sphere onto third-order spherical harmonics (nine solid-angle weighted
 * coefficients per probe) and evaluates the clamped-cosine convolution from them into the probe's 8x8
 * octahedral IRRADIANCE tile (E(n)/pi), which is what integration samples at each pixel's own normal - Lumen's
 * irradiance path (ScreenProbeConvertToIrradiance). Against the exact per-direction cosine sum it replaced:
 * per-frame noise unchanged, lit images 1-8% brighter (tasks/lumen_parity_log.md).
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
/// The filtered RADIANCE (rgb, w = the probe's own hitT lane verbatim, so the next pass's
/// hit-angle test still has it), written by every pass but the last - each pass reads the
/// previous one's output through s_probe_radiance (gi_resolve_pass ping-pongs two atlases).
IMAGE2D_WO(s_probe_filtered_out, rgba16f, 3);
/// x = 1 for a radiance-only pass (write s_probe_filtered_out and stop), 0 for the final
/// pass that convolves to irradiance and writes the importance mip. y = 1 while the adaptive
/// gather skips probes (settings::adaptive_probes): only then can a record hold mode 2, so the
/// stride test below costs nothing otherwise.
uniform vec4 u_gi_probe_filter;

/// Tap stride in skipped regions: the adaptive gather's parent lattice (every second probe).
#define GI_FILTER_PARENT_STRIDE 2

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
/// This group's tap stride (GiFilterTapStride), published by thread 0 for the filter phase.
SHARED int s_tap_stride;
/// Every thread's decoded direction, for the SH3 projection below: 64 threads re-decoding all
/// 64 directions ran GiOctDecode (a normalize among other things) 4096 times per probe for
/// 64 distinct values each thread already computed once.
SHARED vec3 s_dir[GI_PROBE_DIR_COUNT];
/// Every texel's solid angle (GiOctTexelSolidAngle): the octahedral map is not equal-area.
SHARED float s_omega[GI_PROBE_DIR_COUNT];
/// The probe's filtered radiance in nine real SH coefficients (rgb, w = the measured lane), projected once per
/// probe by thread 0 and evaluated by every thread.
SHARED vec4 s_sh[9];

/// Real spherical-harmonic basis normalisations for bands 0-2, and the clamped-cosine convolution's band scales
/// in the E/pi convention (A_l / pi = 1, 2/3, 1/4 [Ramamoorthi and Hanrahan 2001]).
#define GI_SH_BASIS_0        0.282095
#define GI_SH_BASIS_1        0.488603
#define GI_SH_BASIS_2_CROSS  1.092548
#define GI_SH_BASIS_2_ZZ     0.315392
#define GI_SH_BASIS_2_XX_YY  0.546274
#define GI_SH_COSINE_BAND_1  (2.0 / 3.0)
#define GI_SH_COSINE_BAND_2  0.25

/// 1, or GI_FILTER_PARENT_STRIDE on a radiance-only pass where the probe or a direct neighbour
/// was interpolated (see the header). Group-uniform: a function of the records alone.
int GiFilterTapStride(ivec2 probe)
{
	if(u_gi_probe_filter.x < 0.5 || u_gi_probe_filter.y < 0.5)
	{
		return 1;
	}
	bool skipped_region = false;
	for(int m = 0; m < 9; ++m)
	{
		int mx = clamp(probe.x + m % 3 - 1, 0, u_gi_probe_count_x - 1);
		int my = clamp(probe.y + m / 3 - 1, 0, u_gi_probe_count_y - 1);
		uint record = (GiProbeRecord(mx, my, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
		float mode = b_gi_probes[record + uint(GI_PROBE_META)].w;
		skipped_region = skipped_region || (mode > 1.5 && mode < 2.5);
	}
	return skipped_region ? GI_FILTER_PARENT_STRIDE : 1;
}

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
		// The nine staging threads each derive the stride (it addresses their neighbour);
		// thread 0 publishes it for the filter phase behind the barrier.
		int staging_stride = center_valid ? GiFilterTapStride(probe) : 1;
		if(dir_index == 0)
		{
			s_tap_stride = staging_stride;
		}
		int ox = (dir_index % 3 - 1) * staging_stride;
		int oy = (dir_index / 3 - 1) * staging_stride;
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
	float own_hit_t = 0.0;
	if(center_valid)
	{
		vec4 center_texel =
		    texelFetch(s_probe_radiance, GiProbeAtlasBase(probe.x, probe.y, 0) + local, 0);
		own_hit_t = center_texel.w;
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
			int nx = probe.x + (n % 3 - 1) * s_tap_stride;
			int ny = probe.y + (n / 3 - 1) * s_tap_stride;
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
	// RADIANCE-ONLY PASS: hand the filtered sphere to the next pass and stop. Group-uniform,
	// so no thread reaches the barriers below alone; an invalid probe writes zeros so the
	// derived atlas is fully rewritten like the trace atlas.
	if(u_gi_probe_filter.x > 0.5)
	{
		imageStore(s_probe_filtered_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local,
		           vec4(filtered.xyz, own_hit_t));
		return;
	}
	s_filtered[dir_index] = filtered;
	barrier();
	// SH3 PROJECTION of the filtered sphere, once per probe: each direction weighted by its texel's SOLID ANGLE
	// (the octahedral map is not equal-area - GiOctTexelSolidAngle), so a uniform field L projects to exactly
	// E/pi = L below. The directions and solid angles come from shared memory.
	if(center_valid && local.x == 0 && local.y == 0)
	{
		for(int c = 0; c < 9; ++c)
		{
			s_sh[c] = vec4_splat(0.0);
		}
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			vec3 w = s_dir[d];
			vec4 weighted = s_filtered[d] * s_omega[d];
			s_sh[0] += weighted * GI_SH_BASIS_0;
			s_sh[1] += weighted * (GI_SH_BASIS_1 * w.y);
			s_sh[2] += weighted * (GI_SH_BASIS_1 * w.z);
			s_sh[3] += weighted * (GI_SH_BASIS_1 * w.x);
			s_sh[4] += weighted * (GI_SH_BASIS_2_CROSS * w.x * w.y);
			s_sh[5] += weighted * (GI_SH_BASIS_2_CROSS * w.y * w.z);
			s_sh[6] += weighted * (GI_SH_BASIS_2_ZZ * (3.0 * w.z * w.z - 1.0));
			s_sh[7] += weighted * (GI_SH_BASIS_2_CROSS * w.x * w.z);
			s_sh[8] += weighted * (GI_SH_BASIS_2_XX_YY * (w.x * w.x - w.y * w.y));
		}
	}
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
	// Irradiance at THIS texel's normal direction from the SH3 coefficients: the clamped-cosine convolution's band
	// scales (GI_SH_COSINE_BAND_*), clamped at zero - band-limited SH rings slightly negative behind a bright lobe.
	// w is the measured lane evaluated the same way: texels the trace refused hand their share to integration's
	// weighting rather than reading as darkness.
	vec3 normal = dir;
	vec4 irradiance = vec4_splat(0.0);
	if(center_valid)
	{
		vec4 value = s_sh[0] * GI_SH_BASIS_0;
		value += (s_sh[1] * normal.y + s_sh[2] * normal.z + s_sh[3] * normal.x) * (GI_SH_BASIS_1 * GI_SH_COSINE_BAND_1);
		value += (s_sh[4] * (GI_SH_BASIS_2_CROSS * normal.x * normal.y) +
		          s_sh[5] * (GI_SH_BASIS_2_CROSS * normal.y * normal.z) +
		          s_sh[6] * (GI_SH_BASIS_2_ZZ * (3.0 * normal.z * normal.z - 1.0)) +
		          s_sh[7] * (GI_SH_BASIS_2_CROSS * normal.x * normal.z) +
		          s_sh[8] * (GI_SH_BASIS_2_XX_YY * (normal.x * normal.x - normal.y * normal.y))) *
		         GI_SH_COSINE_BAND_2;
		irradiance = vec4(max(value.xyz, vec3_splat(0.0)), saturate(value.w));
	}
	imageStore(s_probe_irradiance_out, GiProbeAtlasBase(probe.x, probe.y, 0) + local, irradiance);
}
