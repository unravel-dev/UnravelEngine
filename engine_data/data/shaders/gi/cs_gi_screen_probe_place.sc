/*
 * GI probe PLACEMENT (adaptive gather): one thread per probe, computing the
 * G-buffer pixel, its world position and normal, the lifted trace origin and
 * the shortened-ray range - into the record buffer, BEFORE the trace dispatch.
 *
 * The anchor is a Halton-jittered pixel of the probe's tile, re-jittered EVERY
 * frame: each frame's gather is a fresh, independent estimate and the per-frame
 * anchor variance is white noise the full-res temporal integrates (the
 * probe-space temporal that once kept anchors sticky for windowed accumulation
 * is REMOVED - amortizing in probe space turned that white noise into
 * probe-granular correlated drift no downstream filter could remove).
 *
 * Splitting placement from tracing is what makes per-probe ADAPTIVITY possible at all: a probe
 * can only judge whether its parents' plane predicts its own anchor after every anchor exists,
 * and groups of a single dispatch have no ordering. The trace reads records instead of
 * recomputing, classifies odd-lattice probes against their even-lattice parents, and skips the
 * 64-ray march wherever a parent blend answers (cs_gi_screen_probe_interp reconstructs
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
uniform vec4 u_gi_camera;

void GiCommitScreenProbe(uint record, vec3 world_position, vec3 world_normal, vec2 uv, float depth)
{
	float voxel;
	float d = SdfSampleClipmapEx(world_position, voxel);
	voxel = max(voxel, 0.01);
	float lift = max(0.0, -d) + GI_PROBE_TRACE_SURFACE_BIAS * voxel;
	float blend;
	float answered_voxel;
	int level = SdfFindClipmapLevel(world_position, blend, answered_voxel);
	int clamped = level >= SDF_CLIPMAP_LEVEL_COUNT ? SDF_CLIPMAP_LEVEL_COUNT - 1 : level;
	// The shortened-ray range follows the cascade's own cross-fade: at a level face it used to
	// jump from 2 x 2 m to 2 x 4 m outright, moving the voxel/probe energy split of every ray
	// at a knife edge the camera drags across the scene. Blending the spacing over the field's
	// blend band makes the range continuous in the anchor's position.
	float spacing = GiWorldProbeSpacing(clamped);
	if(blend > 0.0 && clamped + 1 < SDF_CLIPMAP_LEVEL_COUNT)
	{
		spacing = mix(spacing, GiWorldProbeSpacing(clamped + 1), blend);
	}
	b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 1.0);
	b_gi_probes[record + uint(GI_PROBE_META2)] =
	    vec4(world_normal, length(world_position - u_gi_camera.xyz));
	b_gi_probes[record + uint(GI_PROBE_ORIGIN)] =
	    vec4(world_position + world_normal * lift, 2.0 * spacing);
	// ANCHOR.w is reserved (the removed probe-space temporal's walk flag); kept zero for
	// layout stability.
	b_gi_probes[record + uint(GI_PROBE_ANCHOR)] = vec4(uv, depth, 0.0);
}

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
	vec2 jitter = GiHalton8(uint(u_gi_camera.w));
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
			GiCommitScreenProbe(record, world_position, normalize(nd.world_normal), uv, depth);
			return;
		}
	}
	b_gi_probes[record + uint(GI_PROBE_META)] = vec4_splat(0.0);
	b_gi_probes[record + uint(GI_PROBE_META2)] = vec4_splat(0.0);
}
