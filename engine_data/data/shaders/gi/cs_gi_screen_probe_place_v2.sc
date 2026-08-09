/*
 * GI v2 probe PLACEMENT (adaptive gather): one thread per probe, computing the anchor the
 * trace used to derive in its first thread - the Halton-jittered G-buffer pixel, its world
 * position and normal, the lifted trace origin and the shortened-ray range - into the record
 * buffer, BEFORE the trace dispatch runs.
 *
 * Splitting placement from tracing is what makes per-probe ADAPTIVITY possible at all: a probe
 * can only judge whether its parents' plane predicts its own anchor after every anchor exists,
 * and groups of a single dispatch have no ordering. The trace now reads records instead of
 * recomputing, classifies odd-lattice probes against their even-lattice parents, and skips the
 * 64-ray march wherever a parent blend answers (cs_gi_screen_probe_interp_v2 reconstructs
 * those tiles).
 *
 * Cost: one dispatch of probe-count threads doing a couple of texture reads and one clipmap
 * sample each - noise next to the trace it gates.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

#include "gi/sdf_common.sh"
// Only the unguarded helpers: u_gi_world_probe_params + GiWorldProbeSpacing (+ probe common).
#include "gi/gi_world_probes.sh"

/// The traced-probe list ([0] = count) the classify pass appends to next; this pass only
/// zeroes the cursor, which is safe cross-dispatch and saves a dedicated clear.
BUFFER_RW(b_gi_probe_traced, uint, 6);
BUFFER_RW(b_gi_probes, vec4, 7);
SAMPLER2D(s_hiz, 8);
SAMPLER2D(s_gi_normal, 9);

/// xyz = camera position, w = frame index.
uniform vec4 u_gi_v2_camera;

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 probe = ivec2(gl_GlobalInvocationID.xy);
	if(probe.x >= u_gi_probe_count_x || probe.y >= u_gi_probe_count_y)
	{
		return;
	}
	if(probe.x == 0 && probe.y == 0)
	{
		b_gi_probe_traced[0] = 0u;
	}
	uint record = (GiProbeRecord(probe.x, probe.y, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec2 jitter = GiHalton8(uint(u_gi_v2_camera.w));
	vec2 pixel = (vec2(probe.xy) + jitter) * u_gi_probe_spacing;
	pixel = min(pixel, u_gi_probe_screen.xy - vec2_splat(1.0));
	vec2 uv = (floor(pixel) + vec2_splat(0.5)) * u_gi_probe_screen.zw;
	float depth = texture2DLod(s_hiz, uv, 0.0).x;
	if(depth < 1.0)
	{
		vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
		vec3 world_position = clipToWorld(u_invViewProj, clip);
		GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
		if(dot(nd.world_normal, nd.world_normal) >= 0.5)
		{
			vec3 world_normal = normalize(nd.world_normal);
			// Lift off the composed isosurface by what THIS point needs: its own burial depth
			// plus the trace's acceptance, in the answering level's voxels.
			float voxel;
			float d = SdfSampleClipmapEx(world_position, voxel);
			voxel = max(voxel, 0.01);
			float lift = max(0.0, -d) + GI_PROBE_TRACE_SURFACE_BIAS * voxel;
			// Shortened-ray range: twice the local world-probe spacing covers the probe cage's
			// footprint plus the completion skip [S21 s71-72].
			float blend;
			float answered_voxel;
			int level = SdfFindClipmapLevel(world_position, blend, answered_voxel);
			int clamped = level >= SDF_CLIPMAP_LEVEL_COUNT ? SDF_CLIPMAP_LEVEL_COUNT - 1 : level;
			b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 1.0);
			b_gi_probes[record + uint(GI_PROBE_META2)] =
			    vec4(world_normal, length(world_position - u_gi_v2_camera.xyz));
			b_gi_probes[record + uint(GI_PROBE_ORIGIN)] =
			    vec4(world_position + world_normal * lift, 2.0 * GiWorldProbeSpacing(clamped));
			b_gi_probes[record + uint(GI_PROBE_ANCHOR)] = vec4(uv, depth, 0.0);
			return;
		}
	}
	b_gi_probes[record + uint(GI_PROBE_META)] = vec4_splat(0.0);
	b_gi_probes[record + uint(GI_PROBE_META2)] = vec4_splat(0.0);
}
