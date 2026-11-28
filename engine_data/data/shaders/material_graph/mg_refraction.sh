#ifndef MG_REFRACTION_SH_HEADER_GUARD
#define MG_REFRACTION_SH_HEADER_GUARD

/*
 * Where a refracting surface of the translucency pass reads the opaque scene behind it (the Surface Output's
 * Refraction), for the mains whose headers read that scene: a mesh's (mg_forward.sh) and a particle's
 * (mg_particle.sh). The includer defines MG_SCENE_CLAMP_UV(uv), which keeps a read inside the view, and
 * mg_scene_texture_scene_depth before it.
 */

/// The farthest, in world units, the opaque scene behind a refracting surface bends its view as if it lay.
#define MG_REFRACTION_MAX_GAP 1.0

/// @p clip as a position on the view, 0 to 1 from the top as gl_FragCoord runs.
vec2 mg_clip_to_view_uv(vec4 clip)
{
	vec2 ndc = clip.xy / max(clip.w, 1e-6);
	return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
}

/**
 * Where on the view a surface of index of refraction @p ior at @p position (an offset from the view origin; the view
 * @p V and the normal @p N in world space, @p uv its pixel) shows the opaque scene: the view ray bent by the surface,
 * carried to the depth of the scene behind (at most MG_REFRACTION_MAX_GAP further). A read that would land on
 * something in front of the surface stays at the unbent pixel, so nothing nearer smears into it.
 */
vec2 mg_refracted_uv(vec3 position, vec3 N, vec3 V, float ior, vec2 uv)
{
	float pixel_depth = dot(position - mg_camera_offset(), mg_camera_direction());
	float gap = clamp(mg_scene_texture_scene_depth(uv).x - pixel_depth, 0.0, MG_REFRACTION_MAX_GAP);
	vec3 bent = refract(-V, N, 1.0 / max(ior, 1.0));
	vec2 shift = mg_clip_to_view_uv(mul(u_viewProj, vec4(position + bent * gap, 1.0))) -
	             mg_clip_to_view_uv(mul(u_viewProj, vec4(position, 1.0)));
	vec2 bent_uv = MG_SCENE_CLAMP_UV(uv + shift);
	if(mg_scene_texture_scene_depth(bent_uv).x < pixel_depth)
	{
		bent_uv = uv;
	}
	return bent_uv;
}

#endif // MG_REFRACTION_SH_HEADER_GUARD
