#ifndef MG_POST_SH_HEADER_GUARD
#define MG_POST_SH_HEADER_GUARD

/*
 * What a post-process material's generated shader reads before its code (the graph's evaluate_material): the view's
 * colour at its Location, depth, G-buffer and velocity, and the Scene Texture reads of them. main() is
 * fs_material_graph_post.sh.
 *
 * Stages: the graph's textures take 0-6 (MATERIAL_GRAPH_MAX_POST_PROCESS_TEXTURES); the scene's colour 7, the custom
 * depth 8, the depth 9, the G-buffer 10 to 12, the velocity 13, the custom mask 14 and the pre-exposure 15. The custom
 * mask and depth (mask_pass.cpp) are declared only for a material that reads them (MG_READS_CUSTOM_*).
 *
 * Scene Color is linear: in the scene-referred Locations the light as the scene has it (the pre-exposure taken out on
 * read and put back on write), at After Tonemapping (POST_PROCESS_DISPLAY) the display colour, decoded from sRGB.
 */

#include "../pre_exposure.sh"

SAMPLER2D(s_post_color, 7);
SAMPLER2D(s_post_depth, 9);
SAMPLER2D(s_post_gbuffer0, 10);
SAMPLER2D(s_post_gbuffer1, 11);
SAMPLER2D(s_post_gbuffer2, 12);
SAMPLER2D(s_post_velocity, 13);
#if MG_READS_CUSTOM_DEPTH
SAMPLER2D(s_post_custom_depth, 8);
#endif
#if MG_READS_CUSTOM_MASK
SAMPLER2D(s_post_custom_mask, 14);
#endif

// The camera-relative inverse view-projection: a pixel's offset from the camera, kept precise anywhere in the world.
uniform mat4 u_post_inv_view_proj;
// x: the weight of the entry the material runs for (its Opacity is scaled by it).
uniform vec4 u_post_params;

#if BGFX_SHADER_TYPE_FRAGMENT

// A read at @p uv kept half a texel inside the view: no read lands on its edge.
vec2 mg_post_clamp_uv(vec2 uv)
{
	vec2 half_texel = u_viewTexel.xy * 0.5;
	return clamp(uv, half_texel, vec2_splat(1.0) - half_texel);
}

// The multiplier between the scene's light and what the colour target holds.
float mg_post_pre_exposure()
{
#if POST_PROCESS_DISPLAY
	return 1.0;
#else
	return max(u_pre_exposure_value, 0.000001);
#endif
}

vec3 mg_post_decode_color(vec3 stored)
{
#if POST_PROCESS_DISPLAY
	return toLinear(stored);
#else
	return stored / mg_post_pre_exposure();
#endif
}

vec3 mg_post_encode_color(vec3 color)
{
#if POST_PROCESS_DISPLAY
	return toGamma(saturate(color));
#else
	return max(color, vec3_splat(0.0)) * mg_post_pre_exposure();
#endif
}

// The world position of the opaque surface at screen position @p uv; the far plane where there is none (the sky).
vec3 mg_post_world_position(vec2 uv)
{
	float device_depth = texture2DLod(s_post_depth, uv, 0.0).x;
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(device_depth)));
	return u_camera_wpos.xyz + clipToWorld(u_post_inv_view_proj, clip);
}

vec4 mg_scene_texture_scene_color(vec2 uv)
{
	vec4 stored = texture2DLod(s_post_color, mg_post_clamp_uv(uv), 0.0);
	return vec4(mg_post_decode_color(stored.rgb), 1.0);
}

vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	vec3 position = mg_post_world_position(mg_post_clamp_uv(uv));
	return vec4_splat(dot(position - u_camera_wpos.xyz, mg_camera_direction()));
}

vec4 mg_scene_texture_world_normal(vec2 uv)
{
	return vec4(decodeNormalOctahedron(texture2DLod(s_post_gbuffer1, mg_post_clamp_uv(uv), 0.0).xy), 0.0);
}

vec4 mg_scene_texture_base_color(vec2 uv)
{
	return vec4(texture2DLod(s_post_gbuffer0, mg_post_clamp_uv(uv), 0.0).rgb, 1.0);
}

vec4 mg_scene_texture_metallic(vec2 uv)
{
	return vec4_splat(texture2DLod(s_post_gbuffer1, mg_post_clamp_uv(uv), 0.0).z);
}

vec4 mg_scene_texture_roughness(vec2 uv)
{
	return vec4_splat(texture2DLod(s_post_gbuffer1, mg_post_clamp_uv(uv), 0.0).w);
}

vec4 mg_scene_texture_ambient_occlusion(vec2 uv)
{
	return vec4_splat(texture2DLod(s_post_gbuffer0, mg_post_clamp_uv(uv), 0.0).a);
}

vec4 mg_scene_texture_emissive(vec2 uv)
{
	return vec4(texture2DLod(s_post_gbuffer2, mg_post_clamp_uv(uv), 0.0).rgb, 1.0);
}

// The screen-space motion of the pixel since the last frame, in screen position units (velocity_encoding.sh's RG).
vec4 mg_scene_texture_velocity(vec2 uv)
{
	return vec4(texture2DLod(s_post_velocity, mg_post_clamp_uv(uv), 0.0).rg, 0.0, 0.0);
}

#if MG_READS_CUSTOM_MASK
// The Post Process Mask of the nearest masked renderer at @p uv, 0 to 255; 0 where none is. Point-sampled: a value
// between two masks means nothing.
float mg_post_custom_mask_at(vec2 uv)
{
	vec2 size = vec2(textureSize(s_post_custom_mask, 0));
	ivec2 texel = ivec2(clamp(floor(uv * size), vec2_splat(0.0), size - vec2_splat(1.0)));
	return floor(texelFetch(s_post_custom_mask, texel, 0).x * 255.0 + 0.5);
}

vec4 mg_scene_texture_custom_mask(vec2 uv)
{
	return vec4_splat(mg_post_custom_mask_at(uv));
}
#endif

#if MG_READS_CUSTOM_DEPTH
// The depth of the nearest masked renderer at @p uv, hidden or not, as Scene Depth measures it; the far plane where
// none is.
vec4 mg_scene_texture_custom_depth(vec2 uv)
{
	vec2 clamped = mg_post_clamp_uv(uv);
	float device_depth = texture2DLod(s_post_custom_depth, clamped, 0.0).x;
	vec3 clip = clipTransform(vec3(clamped * 2.0 - 1.0, toClipSpaceDepth(device_depth)));
	vec3 offset = clipToWorld(u_post_inv_view_proj, clip);
	return vec4_splat(dot(offset, mg_camera_direction()));
}
#endif

#endif // BGFX_SHADER_TYPE_FRAGMENT

#endif // MG_POST_SH_HEADER_GUARD
