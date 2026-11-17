#ifndef MG_PREVIEW_VIEW_SH_HEADER_GUARD
#define MG_PREVIEW_VIEW_SH_HEADER_GUARD

/*
 * The view a node swatch of a decal or post-process material reads through Scene Texture nodes
 * (fs_material_graph_preview.sh): a small made-up scene over the tile, a sky over a checkered floor, so an effect on
 * the scene's colour, depth or normals shows on the swatch.
 */

#if BGFX_SHADER_TYPE_FRAGMENT

float mg_preview_view_floor(vec2 uv)
{
	return step(0.5, uv.y);
}

vec4 mg_scene_texture_scene_color(vec2 uv)
{
	vec3 sky = mix(vec3(0.55, 0.7, 0.95), vec3(0.2, 0.35, 0.7), uv.y * 2.0);
	vec2 cell = floor(vec2(uv.x * 8.0, uv.y * 16.0));
	float checker = mod(cell.x + cell.y, 2.0);
	vec3 ground = mix(vec3(0.35, 0.3, 0.25), vec3(0.75, 0.7, 0.6), checker);
	return vec4(mix(sky, ground, mg_preview_view_floor(uv)), 1.0);
}

vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	return vec4_splat(mix(1000.0, mix(20.0, 1.0, (uv.y - 0.5) * 2.0), mg_preview_view_floor(uv)));
}

vec4 mg_scene_texture_world_normal(vec2 uv)
{
	return vec4(0.0, mg_preview_view_floor(uv), 0.0, 0.0);
}

vec4 mg_scene_texture_base_color(vec2 uv)
{
	return vec4(mg_scene_texture_scene_color(uv).rgb * mg_preview_view_floor(uv), 1.0);
}

vec4 mg_scene_texture_metallic(vec2 uv)
{
	return vec4_splat(0.0);
}

vec4 mg_scene_texture_roughness(vec2 uv)
{
	return vec4_splat(0.5);
}

vec4 mg_scene_texture_ambient_occlusion(vec2 uv)
{
	return vec4_splat(1.0);
}

vec4 mg_scene_texture_emissive(vec2 uv)
{
	return vec4(0.0, 0.0, 0.0, 1.0);
}

vec4 mg_scene_texture_velocity(vec2 uv)
{
	return vec4_splat(0.0);
}

#endif // BGFX_SHADER_TYPE_FRAGMENT

#endif // MG_PREVIEW_VIEW_SH_HEADER_GUARD
