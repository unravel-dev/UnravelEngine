/*
 * Filters each probe's radiance across its 3x3 probe neighbourhood and convolves it into a
 * cosine-weighted octahedral IRRADIANCE tile, one probe per thread group.
 *
 * Filtering happens IN PROBE SPACE -- the same octahedral texel across neighbouring probes is
 * the same world-space direction, so averaging them denoises the radiance BEFORE any pixel
 * integrates it, with none of the edge-stopping machinery a screen-space filter needs. The
 * plane and facing weights keep a probe from borrowing radiance across a depth or orientation
 * break, which is the probe-space form of the silhouette-leak guard the integration also has.
 *
 * The output is an 8x8 octahedral IRRADIANCE map per probe -- each texel holds the full
 * cosine-convolved integral for the normal direction that texel represents -- rather than the
 * SH2 projection this used to produce. SH2 was the probe path's largest QUALITY loss: nine
 * coefficients can only express near-uniform variation over the sphere, so the directional
 * contrast the 64 traced cones actually captured (a bright opening on one side, a dark interior
 * on the other) was mathematically smeared flat. The convolution preserves it, integration gets
 * CHEAPER (one small filtered fetch per probe instead of nine buffer reads), and the resolved
 * fraction convolves alongside in alpha so the per-pixel weight stays directional.
 */

#include "bgfx_compute.sh"

#define GI_CACHE_READ_ONLY
// GiHashUint / GiHashCombine live here; the probe header uses them for jitter.
#include "gi/radiance_cache.sh"
#include "gi/gi_probe_common.sh"

SAMPLER2D(s_probe_radiance, 0);
BUFFER_RW(b_gi_probes, vec4, 1);
/// The convolved output: rgb = irradiance / PI for the texel's normal direction, a = the
/// cosine-weighted resolved fraction around it.
IMAGE2D_WO(s_probe_irradiance_out, rgba16f, 2);

/// How far off the centre probe's plane a neighbour may anchor before it stops contributing,
/// as a fraction of the centre's view distance. The probe-space analogue of the integration's
/// plane weight, and the reason a probe on a wall does not borrow the floor's radiance.
#define GI_PROBE_FILTER_PLANE_TOLERANCE 0.05

SHARED vec4 s_filtered[GI_PROBE_DIR_COUNT];

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_WorkGroupID.xy);
	int layer = int(gl_WorkGroupID.z);
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	// This frame's half of the double-buffered probe state, always: the filter consumes what the
	// trace just wrote and never touches the history half. Layers filter independently -- a
	// minority-surface probe must not borrow a majority neighbour's radiance.
	uint base =
	    (GiProbeRecord(probe.x, probe.y, layer) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec4 center_meta = b_gi_probes[base + uint(GI_PROBE_META)];
	vec4 center_meta2 = b_gi_probes[base + uint(GI_PROBE_META2)];
	bool center_valid = center_meta.w > 0.5;
	int dir_index = local.y * GI_PROBE_DIR_EDGE + local.x;
	// Each thread filters ITS direction across the neighbourhood.
	vec4 filtered = vec4_splat(0.0);
	if(center_valid)
	{
		float weight_sum = 0.0;
		float plane_tolerance = GI_PROBE_FILTER_PLANE_TOLERANCE * max(center_meta2.w, 0.1);
		// The centre probe's own reading of this direction, for the per-texel proximity gate
		// below.
		vec4 center_texel =
		    texelFetch(s_probe_radiance, GiProbeAtlasBase(probe.x, probe.y, layer) + local, 0);
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
				    (GiProbeRecord(nx, ny, layer) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
				vec4 neighbour_meta = b_gi_probes[neighbour_base + uint(GI_PROBE_META)];
				if(neighbour_meta.w < 0.5)
				{
					continue;
				}
				vec4 neighbour_meta2 = b_gi_probes[neighbour_base + uint(GI_PROBE_META2)];
				// Same-surface tests, both scaled to the centre probe: off-plane distance and
				// facing agreement. A neighbour across a silhouette fails one of them.
				float plane = abs(dot(neighbour_meta.xyz - center_meta.xyz, center_meta2.xyz));
				float plane_weight = saturate(1.0 - plane / plane_tolerance);
				float facing = saturate(dot(neighbour_meta2.xyz, center_meta2.xyz));
				float weight = plane_weight * facing * facing;
				if(weight <= 1e-4)
				{
					continue;
				}
				ivec2 texel = GiProbeAtlasBase(nx, ny, layer) + local;
				vec4 neighbour_value = texelFetch(s_probe_radiance, texel, 0);
				// Per-texel PROXIMITY gate, on top of the per-probe plane and facing tests.
				// Those tests compare the probes' ANCHORS, and both anchors sitting on the same
				// wall says nothing about what the two cones in this direction actually saw: a
				// probe under an overhang reads the occluder half a metre up while its same-plane
				// neighbour reads open sky, and averaging them erases exactly the contact
				// occlusion the trace measured -- the light leak under every awning. The encoded
				// proximity in alpha is the per-direction visibility the anchor tests lack: cones
				// that saw comparably distant things share, sky and near-hit never mix, and a
				// texel that measured nothing (alpha 0) contributes nothing rather than diluting
				// everyone with black -- unless the centre itself measured nothing, in which case
				// it takes what its neighbours offer.
				float gate = 1.0;
				if(neighbour_value.w <= 0.0)
				{
					gate = 0.0;
				}
				else if(center_texel.w > 0.0)
				{
					float ratio = min(center_texel.w, neighbour_value.w) /
					              max(center_texel.w, neighbour_value.w);
					gate = ratio * ratio;
				}
				weight *= gate;
				if(weight <= 1e-4)
				{
					continue;
				}
				filtered += neighbour_value * weight;
				weight_sum += weight;
			}
		}
		filtered = weight_sum > 1e-4 ? filtered / weight_sum : vec4_splat(0.0);
	}
	s_filtered[dir_index] = filtered;
	barrier();
	// Cosine convolution, perfectly parallel: THIS thread's texel represents one candidate
	// NORMAL direction, and it integrates the whole filtered sphere against that normal's
	// clamped-cosine lobe. Every octahedral texel subtends approximately the same solid angle,
	// so E(n) / PI = sum(L_d * max(dot(n, w_d), 0)) * (4pi / N) / pi = sum(...) / (N / 4).
	vec3 texel_normal = GiOctDecode((vec2(local.xy) + vec2_splat(0.5)) / float(GI_PROBE_DIR_EDGE));
	vec4 irradiance = vec4_splat(0.0);
	if(center_valid)
	{
		for(int d = 0; d < GI_PROBE_DIR_COUNT; ++d)
		{
			vec2 dir_uv = (vec2(float(d % GI_PROBE_DIR_EDGE), float(d / GI_PROBE_DIR_EDGE)) +
			               vec2_splat(0.5)) /
			              float(GI_PROBE_DIR_EDGE);
			float cosine = max(dot(texel_normal, GiOctDecode(dir_uv)), 0.0);
			irradiance.xyz += s_filtered[d].xyz * cosine;
			// Alpha holds encoded proximity, so the RESOLVED fraction the integration weighs by
			// is recovered here: measured (any non-zero proximity, sky included) versus not.
			irradiance.w += (s_filtered[d].w > 0.0 ? 1.0 : 0.0) * cosine;
		}
		irradiance /= float(GI_PROBE_DIR_COUNT) * 0.25;
	}
	imageStore(s_probe_irradiance_out, GiProbeAtlasBase(probe.x, probe.y, layer) + local, irradiance);
}
