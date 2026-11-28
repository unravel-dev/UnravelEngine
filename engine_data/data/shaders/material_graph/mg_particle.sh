#ifndef MG_PARTICLE_SH_HEADER_GUARD
#define MG_PARTICLE_SH_HEADER_GUARD

/*
 * What a material drawn by a particle emitter reads besides its graph (fs_material_graph_particle.sh): what every
 * particle fragment shader shares (particles/particle_shading.sh: the sky's irradiance SH with the GPU's share of the
 * pre-exposure in its texel 9 at stage 12, the view's opaque depth at stage 15, the fog, and for an unlit material the
 * fog's froxel column at stage 8; a lit one's vertices read the sun's cascades at stages 8-11), and the copy of the
 * lit scene behind the pass (Scene Texture Scene Color, Refraction). The graph's own textures take the stages those
 * and the particle data leave (get_material_graph_particle_texture_stage).
 */

// The draws that light nothing per vertex: an unlit material's, and a lit one's on an emitter whose Lighting is Unlit
// (PARTICLE_UNLIT). They hold the fog's froxel column where the lit ones hold the sun's cascades.
#if MATERIAL_UNLIT || PARTICLE_UNLIT
#define MG_PARTICLE_UNLIT 1
#define FOG_VOLUME_SAMPLER s_particle_fog_volume
#else
#define MG_PARTICLE_UNLIT 0
#endif
#include "../particles/particle_shading.sh"

#if (MG_READS_SCENE_COLOR || MG_READS_SCENE_DEPTH) && BGFX_SHADER_TYPE_FRAGMENT
// A read at @p uv kept half a texel inside the view: no read lands on its edge.
vec2 mg_particle_clamp_uv(vec2 uv)
{
	vec2 half_texel = u_viewTexel.xy * 0.5;
	return clamp(uv, half_texel, vec2_splat(1.0) - half_texel);
}

#if MG_READS_SCENE_COLOR
// The copy of the lit scene the translucency pass takes (pipeline::prepare_forward_scene_reads), on the last stage a
// particle material's own textures could take (get_material_graph_particle_scene_color_stage).
#if MATERIAL_UNLIT
#define PARTICLE_SCENE_COLOR_STAGE 11
#else
#define PARTICLE_SCENE_COLOR_STAGE 6
#endif
SAMPLER2D(s_particle_scene_color, PARTICLE_SCENE_COLOR_STAGE);
/// x: 1 when s_particle_scene_color holds the copy of the lit scene, 0 when the view took none (it reads black then).
uniform vec4 u_particle_scene;

// The light of the opaque scene at @p uv, as the scene has it (the pre-exposure taken out).
vec4 mg_scene_texture_scene_color(vec2 uv)
{
	vec3 stored = texture2DLod(s_particle_scene_color, mg_particle_clamp_uv(uv), 0.0).rgb;
	return vec4(stored / max(u_pre_exposure_value, 0.000001), 1.0);
}
#endif

#if MG_READS_SCENE_DEPTH
// How far the opaque scene at @p uv lies from the camera along its direction, in world units; far away in a view
// that bound no depth (u_particleParams.x), so Depth Fade passes the Opacity.
vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	if(u_particleParams.x < 0.5)
	{
		return vec4_splat(1.0e6);
	}
	float device_depth = texture2DLod(s_sceneDepth, mg_particle_clamp_uv(uv), 0.0).x;
	return vec4_splat(abs(screenSpaceToViewSpaceDepth(device_depth)));
}
#endif

#if MATERIAL_REFRACTION
#define MG_SCENE_CLAMP_UV(_uv) mg_particle_clamp_uv(_uv)
#include "mg_refraction.sh"
#endif
#endif // scene reads

#endif // MG_PARTICLE_SH_HEADER_GUARD
