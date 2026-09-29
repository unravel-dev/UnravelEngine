/*
 * GI probe PLACEMENT (adaptive gather): one thread per probe, computing the
 * G-buffer pixel, its world position and normal, the lifted trace origin and
 * the shortened-ray range - into the record buffer, BEFORE the trace dispatch.
 *
 * The anchor is a Halton-jittered position in the probe's tile - the surface point under it,
 * not its texel (GiScreenProbeContinuousAnchor) - re-jittered EVERY
 * frame: each frame's gather is a fresh, independent estimate and the per-frame
 * anchor variance is white noise the full-res temporal integrates. Anchors are
 * never held sticky for probe-space accumulation - amortizing in probe space turns
 * that white noise into probe-granular correlated drift no downstream filter can remove.
 *
 * Splitting placement from tracing is what makes per-probe ADAPTIVITY possible at all: a probe
 * can only judge whether its parents' plane predicts its own anchor after every anchor exists,
 * and groups of a single dispatch have no ordering. The classify pass reads the records,
 * judges odd-lattice probes against their even-lattice parents, and lists only the probes to
 * trace, so the 64-ray march is skipped wherever a parent blend answers
 * (cs_gi_screen_probe_interp reconstructs those tiles).
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
	// The shortened-ray range is the SAME at every camera distance (GI_SCREEN_PROBE_SHORT_RANGE;
	// mesh-exact over its first GI_MESH_SDF_TRACE_RANGE). A range tied to the covering
	// cascade's probe spacing would make a surface's rays establish their own visibility
	// through a fatter field and complete from a higher, coarser cage the farther the camera
	// stands from it, so the same floor would read differently from near and far. A surface's
	// lighting must not know where the camera is.
	b_gi_probes[record + uint(GI_PROBE_META)] = vec4(world_position, 1.0);
	b_gi_probes[record + uint(GI_PROBE_META2)] =
	    vec4(world_normal, length(world_position - u_gi_camera.xyz));
	b_gi_probes[record + uint(GI_PROBE_ORIGIN)] =
	    vec4(world_position + world_normal * lift, GI_SCREEN_PROBE_SHORT_RANGE);
	// ANCHOR.w is reserved; kept zero for layout stability.
	b_gi_probes[record + uint(GI_PROBE_ANCHOR)] = vec4(uv, depth, 0.0);
}

/// Below this cosine between the camera ray and the anchor texel's plane the plane is seen
/// edge-on and the ray's intersection is ill-conditioned: the texel's own point answers.
#define GI_ANCHOR_MIN_FACING 0.02
/// How far, in the texel's own neighbour spacings, the ray's hit may land from the texel's
/// point before it counts as a silhouette (the plane belongs to the texel, not to what lies past it).
#define GI_ANCHOR_MAX_REACH 1.5

/// World position (xyz) of full-resolution depth texel @p texel; w = 1 when it holds geometry.
vec4 GiAnchorDepthTexel(ivec2 texel, ivec2 size)
{
	ivec2 t = clamp(texel, ivec2(0, 0), size - ivec2(1, 1));
	float depth = texelFetch(s_hiz, t, 0).x;
	vec2 uv = (vec2(t) + vec2_splat(0.5)) / vec2(size);
	vec3 position = clipToWorld(u_invViewProj, clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth))));
	return vec4(position, depth < 1.0 ? 1.0 : 0.0);
}

/// The surface's slope along one axis: the smaller of the two one-sided differences whose far
/// texel holds geometry, so a neighbour across a depth edge is not used. w = 0 when neither does.
vec4 GiAnchorTangent(vec4 forward, vec4 center, vec4 backward)
{
	vec3 ahead = forward.xyz - center.xyz;
	vec3 behind = center.xyz - backward.xyz;
	if(forward.w < 0.5 && backward.w < 0.5)
	{
		return vec4_splat(0.0);
	}
	if(forward.w < 0.5)
	{
		return vec4(behind, 1.0);
	}
	if(backward.w < 0.5)
	{
		return vec4(ahead, 1.0);
	}
	return vec4(dot(ahead, ahead) < dot(behind, behind) ? ahead : behind, 1.0);
}

/**
 * The anchor at its EXACT lattice position @p uv rather than at a texel: the camera ray through
 * it meets the plane of the full-resolution depth texel it falls in. A texel anchor snaps as the
 * image moves, and on a grazing surface one texel spans metres - the probe would jump along the
 * floor, and its lighting with it, although the lattice follows the surface exactly. Silhouettes,
 * edge-on planes and texels without two usable tangents keep the texel's own point; a texel
 * without geometry keeps @p fallback.
 */
vec3 GiScreenProbeContinuousAnchor(vec2 uv, vec3 fallback)
{
	ivec2 size = ivec2(textureSize(s_hiz, 0));
	ivec2 texel = ivec2(uv * vec2(size));
	vec4 center = GiAnchorDepthTexel(texel, size);
	if(center.w < 0.5)
	{
		return fallback;
	}
	vec4 tangent_x = GiAnchorTangent(GiAnchorDepthTexel(texel + ivec2(1, 0), size), center,
	                                 GiAnchorDepthTexel(texel - ivec2(1, 0), size));
	vec4 tangent_y = GiAnchorTangent(GiAnchorDepthTexel(texel + ivec2(0, 1), size), center,
	                                 GiAnchorDepthTexel(texel - ivec2(0, 1), size));
	vec3 normal = cross(tangent_x.xyz, tangent_y.xyz);
	if(tangent_x.w < 0.5 || tangent_y.w < 0.5 || dot(normal, normal) < 1e-16)
	{
		return center.xyz;
	}
	normal = normalize(normal);
	vec3 far_point = clipToWorld(u_invViewProj, clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(0.5))));
	vec3 ray = normalize(far_point - u_gi_camera.xyz);
	float facing = dot(ray, normal);
	if(abs(facing) < GI_ANCHOR_MIN_FACING)
	{
		return center.xyz;
	}
	vec3 hit = u_gi_camera.xyz + ray * (dot(center.xyz - u_gi_camera.xyz, normal) / facing);
	float reach = GI_ANCHOR_MAX_REACH * max(length(tangent_x.xyz), length(tangent_y.xyz));
	return length(hit - center.xyz) <= reach ? hit : center.xyz;
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
	// The lattice follows the camera's rotation (gi_probe_common.sh), so its tiles sit at any
	// offset against the screen and are slightly warped: a tile wholly off screen holds no probe,
	// and a partly visible one keeps its anchor in the visible part of its bounds.
	vec2 corner00 = GiProbeLatticePixel(vec2(probe.xy));
	vec2 corner10 = GiProbeLatticePixel(vec2(probe.xy) + vec2(1.0, 0.0));
	vec2 corner01 = GiProbeLatticePixel(vec2(probe.xy) + vec2(0.0, 1.0));
	vec2 corner11 = GiProbeLatticePixel(vec2(probe.xy) + vec2_splat(1.0));
	vec2 tile_min = min(min(corner00, corner10), min(corner01, corner11));
	vec2 tile_max = max(max(corner00, corner10), max(corner01, corner11));
	vec2 visible_min = max(tile_min, vec2_splat(0.0));
	vec2 visible_max = min(tile_max, u_gi_probe_screen.xy) - vec2_splat(1.0);
	bool visible = all(greaterThanEqual(visible_max, visible_min));
	vec2 pixel = clamp(GiProbeLatticePixel(vec2(probe.xy) + jitter), visible_min,
	                   max(visible_max, visible_min) + vec2_splat(0.999));
	vec2 uv = (floor(pixel) + vec2_splat(0.5)) * u_gi_probe_screen.zw;
	float depth = visible ? texture2DLod(s_hiz, uv, 0.0).x : 1.0;
	if(depth < 1.0)
	{
		vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
		vec3 world_position = clipToWorld(u_invViewProj, clip);
		GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
		if(dot(nd.world_normal, nd.world_normal) >= 0.5)
		{
			// The Hi-Z tier keeps the texel as its screen-space origin: the march compares against
			// the depth the texel stores, which a sub-texel point on a slanted surface does not lie on.
			vec3 anchor = GiScreenProbeContinuousAnchor(pixel * u_gi_probe_screen.zw, world_position);
			GiCommitScreenProbe(record, anchor, normalize(nd.world_normal), uv, depth);
			return;
		}
	}
	b_gi_probes[record + uint(GI_PROBE_META)] = vec4_splat(0.0);
	b_gi_probes[record + uint(GI_PROBE_META2)] = vec4_splat(0.0);
}
