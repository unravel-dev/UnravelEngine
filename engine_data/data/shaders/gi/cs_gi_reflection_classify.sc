/*
 * Classify + compact for the GI reflection trace (the fragment form's early-out tiers,
 * lifted into their own cheap pass so the expensive kernel never launches for them).
 *
 * Per trace-target texel, exactly the decisions fs_gi_reflection.sc made at its top -
 * KEEP THE TWO IN STEP:
 *   - sky, or a degenerate G-buffer normal: answer 0 directly.
 *   - roughness at/past GI_REFLECTION_ROUGH_CUTOFF: the wide-lobe limit - last frame's
 *     resolved gather (or the SH along the mirror direction before one exists) at full
 *     coverage, no ray.
 *   - everything sharper TRACES: the texel is appended to the compacted list the indirect
 *     args pass sizes the trace launch from, so every 64-lane trace group is dense with
 *     rays. In the fragment form one tracing pixel dragged its whole wave through the
 *     march, and the kernel's worst-case register footprint throttled every pixel.
 *
 * List layout (raw uint, so no typed-UAV float canonicalisation concerns; keep in step
 * with cs_gi_reflection_args.sc, which owns the full picture):
 *   [0] = the append cursor, atomically bumped here, RESET by the args pass for the next
 *         frame (this pass is the frame's first writer, so it cannot reset it itself).
 *   [1] = the staged trace count the kernel bounds-checks against.
 *   [2 .. 2 + GI_REFLECTION_MEAN_SLOTS*3) = the texture means the args pass stages for the
 *         trace kernel's albedo remodulation (this pass neither reads nor writes them).
 *   [2 + GI_REFLECTION_MEAN_SLOTS*3 + i] = packed texel coords, y in the high 16 bits.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "gi/gi_constants.sh"

SAMPLER2D(s_hiz, 0);
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_gi_diffuse, 2);
SAMPLER2D(s_gi_env_sh, 3);
IMAGE2D_WO(s_gi_refl_out, rgba16f, 4);
BUFFER_RW(b_gi_refl_list, uint, 5);

/// xyz = camera position, w > 0 when s_gi_diffuse holds last frame's resolve.
uniform vec4 u_gi_reflection_camera;
/// xy = R2 offset (unused here), zw unused.
uniform vec4 u_gi_reflection_jitter;
/// xy = one texel of the trace target, zw = its dimensions.
uniform vec4 u_gi_reflection_texel;

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(u_gi_reflection_texel.zw);
	if(pixel.x >= size.x || pixel.y >= size.y)
	{
		return;
	}
	vec2 uv = (vec2(pixel) + vec2_splat(0.5)) * u_gi_reflection_texel.xy;
	float depth = texture2DLod(s_hiz, uv, 0.0).x;
	if(depth >= 1.0)
	{
		imageStore(s_gi_refl_out, pixel, vec4_splat(0.0));
		return;
	}
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	if(dot(nd.world_normal, nd.world_normal) < 0.5)
	{
		imageStore(s_gi_refl_out, pixel, vec4_splat(0.0));
		return;
	}
	// RAW authored roughness, exactly as the kernel tiers (MakeRoughnessSafe floors it, and
	// a floored mirror leaked a fraction of the coarse world tier through the fade).
	float roughness = nd.roughness;
	BRANCH
	if(roughness >= GI_REFLECTION_ROUGH_CUTOFF)
	{
		// The wide-lobe limit at full coverage - the kernel's rough fast path verbatim.
		vec3 rough_value;
		BRANCH
		if(u_gi_reflection_camera.w > 0.5)
		{
			rough_value = texture2DLod(s_gi_diffuse, uv, 0.0).xyz;
		}
		else
		{
			vec3 normal = normalize(nd.world_normal);
			vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
			vec3 world_position = clipToWorld(u_invViewProj, clip);
			vec3 view = normalize(u_gi_reflection_camera.xyz - world_position);
			vec3 reflected = normalize(reflect(-view, normal));
			rough_value = eval_radiance_sh(s_gi_env_sh, reflected);
		}
		imageStore(s_gi_refl_out, pixel, vec4(rough_value, 1.0));
		return;
	}
	uint slot;
	atomicFetchAndAdd(b_gi_refl_list[0], 1u, slot);
	b_gi_refl_list[2u + uint(GI_REFLECTION_MEAN_SLOTS) * 3u + slot] = (uint(pixel.y) << 16u) | uint(pixel.x);
}
