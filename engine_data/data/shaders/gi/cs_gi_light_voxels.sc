/*
 * Lights the surface voxels (gi_rewrite_plan.md 3.2): one thread per surface-list entry, direct
 * lighting with traced shadows per EXPOSED FACE, written straight into the light volume.
 *
 * BUDGETED BY CONSTRUCTION: only listed surface voxels are processed, and each is re-lit every
 * GI_LIGHT_VOXEL_UPDATE_DENOM frames (entry index + frame phase), so the per-frame cost is a
 * quarter of the resident surface set regardless of scene size - the property the old 524k-slot
 * cache sweep lacked.
 *
 * NO temporal accumulation here, deliberately. Direct lighting with traced shadows is
 * deterministic - there is no variance to average - so the volume just holds the latest answer
 * and a light change propagates in at most one full rotation (4 frames). The stochastic
 * machinery lives where the stochastic rays are: the world probes (Phase 3). When the bounce
 * term arrives (Phase 4) it reads the probes' FILTERED irradiance, which is equally
 * deterministic per frame, so this stays a plain write.
 *
 * The dispatch covers every level's full segment and early-outs beyond each level's count; the
 * counts live on the GPU, so a tighter launch needs indirect dispatch args - a measured
 * optimisation for later, not a correctness matter (an early-out thread costs one buffer read).
 */

#include "bgfx_compute.sh"
#include "gi/sdf_common.sh"
#include "gi/gpu_lights.sh"
#include "gi/gi_lighting.sh"
#include "gi/gi_light_voxels.sh"
// The bounce term (gi_rewrite_plan.md Phase 4): last frame's world-probe irradiance closes the
// infinite-bounce loop - probes read voxels, voxels read probes, gain bounded by GI_MAX_ALBEDO.
#define GI_WORLD_PROBE_READ
#include "gi/gi_world_probes.sh"

/// Surface-voxel list + per-level counts, written by cs_gi_clipmap_attributes. The count sits
/// at a high stage ON PURPOSE: OpenGL guarantees only eight image units (bindings 0-7), so the
/// light-volume IMAGE below must live in that range while buffers and samplers tolerate the
/// high stages.
BUFFER_RO(b_surface_list, uint, 6);
BUFFER_RO(b_surface_count, uint, 10);
/// Attribute volumes: what the surface looks like.
SAMPLER3D(s_attr_albedo, 8);
SAMPLER3D(s_attr_emissive, 9);
/// The light volume this pass owns.
IMAGE3D_WO(s_light_voxels_out, rgba16f, 7);

/// Defined locally rather than taken from lighting.sh, which this shader does not include. The
/// D3D backend happens to supply one anyway, so relying on it compiles there and fails on GLSL.
#define GI_PI 3.1415926535897932

/// xyz = camera position - the world-probe windows are centred on it, and the cascade chooser
/// needs the same centre the trace pass used.
uniform vec4 u_gi_light_voxel_camera;

/*
 * CAVITY visibility for the bounce term - distance-field cone occlusion, the [DFAO] role:
 * sample the composed field at doubling distances along the face; wherever the field reads
 * less than the distance travelled, geometry encroaches on the face's hemisphere. This
 * measures EXACTLY the band the world probes cannot: from one attribute voxel (below which
 * the voxel's own surface dominates the field) out to about the probe spacing (beyond which
 * the probes' own Chebyshev visibility already handles occlusion). Without it, a voxel inside
 * a sub-spacing cavity - an awning's underside, a window reveal, a doorway - receives the
 * OPEN ambient of the probe cage around it and glows in exactly the places that should be
 * darkest; every gather ray that hits the cavity then reads that false brightness back.
 */
float GiBounceCavityVisibility(vec3 position, vec3 direction, float attr_voxel)
{
	float occlusion = 0.0;
	float weight = 1.0;
	float weight_sum = 0.0;
	float t = attr_voxel;
	LOOP for(int i = 0; i < GI_BOUNCE_AO_STEPS; ++i)
	{
		// Field >= travel distance: the cone is clear at this scale, no contribution. Field
		// negative (inside geometry): fully occluded. The weights halve so near encroachment
		// - the strongest visibility signal - dominates.
		float d = SdfSampleClipmap(position + direction * t);
		occlusion += weight * saturate(1.0 - d / t);
		weight_sum += weight;
		weight *= 0.5;
		t *= 2.0;
	}
	return saturate(1.0 - occlusion / weight_sum);
}

NUM_THREADS(64, 1, 1)
void main()
{
	uint capacity = uint(u_light_voxel_resolution * u_light_voxel_resolution * u_light_voxel_resolution);
	uint id = gl_GlobalInvocationID.x;
	uint level = id / capacity;
	uint entry = id % capacity;
	if(level >= uint(SDF_CLIPMAP_LEVEL_COUNT))
	{
		return;
	}
	if(entry >= b_surface_count[level])
	{
		return;
	}
	// Interleaved update, keyed by entry + frame so the work spreads evenly across the rotation
	// instead of pulsing.
	if(((entry + u_light_voxel_frame) % uint(GI_LIGHT_VOXEL_UPDATE_DENOM)) != 0u)
	{
		return;
	}
	// packed_slot, not `packed`: that word is a GLSL layout-qualifier keyword and a variable
	// named after it fails the OpenGL backend outright.
	uint packed_slot = b_surface_list[level * capacity + entry];
	ivec3 slot = ivec3(int(packed_slot & 0xFFu),
	                   int((packed_slot >> 8u) & 0xFFu),
	                   int((packed_slot >> 16u) & 0xFFu));
	vec4 level_data = u_sdf_clipmap_levels[level];
	float attr_voxel = level_data.w * 2.0;
	// Toroidal reconstruction: the slot's world cell under the current window (the same math the
	// attribute composer used to place it - the origin is attr-voxel aligned by the snap).
	int attr_res = u_light_voxel_resolution;
	ivec3 window_base = ivec3(floor(level_data.xyz / attr_voxel + vec3_splat(0.5)));
	ivec3 base_slot = GiLightVoxelSlot(window_base);
	ivec3 offset = ivec3((slot.x - base_slot.x + attr_res) % attr_res,
	                     (slot.y - base_slot.y + attr_res) % attr_res,
	                     (slot.z - base_slot.z + attr_res) % attr_res);
	ivec3 cell = window_base + offset;
	vec3 center = (vec3(cell) + vec3_splat(0.5)) * attr_voxel;
	ivec3 attr_texel = ivec3(slot.x, slot.y, slot.z + int(level) * attr_res);
	vec4 albedo = texelFetch(s_attr_albedo, attr_texel, 0);
	vec3 emissive = texelFetch(s_attr_emissive, attr_texel, 0).xyz;
	if(albedo.a <= 0.0)
	{
		// De-listed between compose and this slice's turn: nothing to light.
		return;
	}
	// The gain clamp that closes the (future) bounce recursion below 1 lives at the one place
	// radiance is produced, exactly as the old cache update kept it.
	vec3 bounded_albedo = min(albedo.xyz, vec3_splat(GI_MAX_ALBEDO));
	float d_center = SdfSampleClipmapLevel(int(level), center);
	// Mesh-exact shadow detail fades with level, like every near-field consumer: level 0 sees
	// full contact shadowing, level 1 half range, beyond that the cascade alone answers.
	float near_scale = level == 0u ? 1.0 : (level == 1u ? 0.5 : 0.0);
	for(int face = 0; face < 6; ++face)
	{
		vec3 direction = GiLightVoxelFaceDirection(face);
		ivec3 texel = GiLightVoxelTexel(slot, int(level), face);
		// Launch point clear of the surface: out by however deep the centre sits, plus half an
		// attribute voxel - in the units of the thing being cleared.
		float lift = max(0.0, -d_center) + 0.5 * attr_voxel;
		vec3 position = center + direction * lift;
		// TUNNEL GUARD: walking out of your OWN surface along the face rises monotonically
		// (1-Lipschitz from inside the band); a lift whose midpoint reads DEEPER than the
		// centre crossed the slab core - it exited through the FAR side, and everything
		// measured from there (direct sun, exterior ambient) belongs to the wrong side of the
		// wall. Un-guarded, buried faces near walls were lit by the sunlit exterior and
		// stamped white into enclosed rooms. One field sample, only for deep lifts.
		if(lift > attr_voxel)
		{
			float d_mid = SdfSampleClipmap(center + direction * (0.5 * lift));
			if(d_mid < d_center - 0.25 * attr_voxel)
			{
				imageStore(s_light_voxels_out, texel, vec4_splat(0.0));
				continue;
			}
		}
		// A face is MEASURABLE when enough of its cavity cone escapes - the same multi-scale
		// visibility the ambient below is weighted by, computed once and shared. This replaced
		// a single-step field-rise test, which cannot see past a coarse level's blob plateau:
		// small geometry merges into blobs whose shell voxels sit a voxel or more deep, the
		// one-voxel step stays inside, and every face read as unexposed - whole objects went
		// black wherever only coarse levels covered them (the far-distance failure). The march
		// at 1/2/4 voxels from the LIFTED point sees past the plateau; a face pointing into
		// real interior still reads closed at every scale and stays dark, both sides of a thin
		// wall still measure open through their own slabs.
		float visibility = GiBounceCavityVisibility(position, direction, attr_voxel);
		if(visibility < GI_LIGHT_VOXEL_VISIBILITY_MIN)
		{
			imageStore(s_light_voxels_out, texel, vec4_splat(0.0));
			continue;
		}
		vec3 irradiance = GiEvalDirectLighting(position,
		                                       direction,
		                                       max(level_data.w, 0.01),
		                                       u_gi_shadow_near_field * near_scale);
		// Bounce: LAST frame's world-probe irradiance around this face (the probes traced after
		// this pass last frame, so the loop advances one bounce per frame). The probes' E/pi
		// convention converts back with pi so one albedo/pi below serves the whole sum. The
		// "view" direction of the self-shadow bias is the face itself - a voxel has no camera,
		// and biasing purely along the face normal is the direction that clears its own surface.
		vec3 probe_value;
		float sky_fraction;
		if(u_world_probe_ready &&
		   GiWorldProbeIrradianceCascade(position, direction, direction,
		                                 u_gi_light_voxel_camera.xyz, probe_value, sky_fraction))
		{
			// Attenuated by the face's own sub-probe-spacing visibility: the probes' ambient
			// is measured on a lattice that cannot see this cavity. The SAME value gated the
			// face above, so the gate costs nothing extra.
			irradiance += probe_value * GI_PI * visibility;
		}
		vec3 radiance = bounded_albedo * irradiance / GI_PI + emissive;
		imageStore(s_light_voxels_out, texel, vec4(radiance, 1.0));
	}
}
