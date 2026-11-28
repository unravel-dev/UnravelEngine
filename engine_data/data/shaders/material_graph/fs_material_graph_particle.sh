#ifndef FS_MATERIAL_GRAPH_PARTICLE_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_PARTICLE_SH_HEADER_GUARD

/*
 * main() of a material graph's particle variants (MATERIAL_PARTICLE): a Surface material with Particle Use, drawn by a
 * particle emitter's sprites, trails and ribbons in the translucency pass, sorted with every other particle batch.
 * particles/vs_particle.eshader's graph variants write what the graph reads of the particle; the surface from the
 * graph's evaluate_material is lit as the emitter's Lighting says (particles/particle_shading.sh), faded, fogged and
 * written as the particle blend states take it (u_particleParams.y, the material's Blend Mode):
 *   Translucent    rgb = (light x Base Color + Emissive) x coverage, alpha = coverage; ONE, INV_SRC_ALPHA
 *   Premultiplied  rgb = (light x Base Color + Emissive) x the fades, alpha = coverage; ONE, INV_SRC_ALPHA
 *   Additive       rgb = (light x Base Color + Emissive) x coverage; ONE, ONE
 *   Modulate       rgb = Emissive faded to white by the coverage; DST_COLOR, ZERO
 * The coverage is the Opacity times the fades: the view's (camera fade, draw distance, a width below a pixel, a trail's
 * taper) and the soft fade against the scene's depth. An unlit material (MATERIAL_UNLIT) writes its Emissive alone; a
 * lit one on an emitter whose Lighting is Unlit (PARTICLE_UNLIT) adds its Base Color as light the particle gives off.
 * A lit one whose graph sets its Roughness or Metallic (MATERIAL_PARTICLE_SPECULAR) also reflects light off its
 * surface: the sun's highlight where its emitter's Lighting is Surface, and the light around it. Its Metallic takes
 * its Base Color from the diffuse light to that. What it reflects fades with its Opacity in every blend mode.
 * The second target is the TAA reactive mask (MAX blend): the coverage times the emitter's TAA Response.
 * A refracting material (MATERIAL_REFRACTION) shows the opaque scene behind it, bent, where it bends its view.
 * The velocity variant (PARTICLE_VELOCITY) writes the particle's motion where its coverage reaches the emitter's
 * Velocity Alpha Threshold (velocity_encoding.sh).
 */

#include "mg_particle.sh"

// per frame
uniform vec4 u_camera_clip_planes; //.x = near, .y = far, .z = material texture mip bias

#if PARTICLE_VELOCITY
vec2 mg_particle_clip_to_uv(vec4 clip)
{
	return clipTransform(clip.xyz / clip.w).xy * 0.5 + 0.5;
}
#endif

void main()
{
	// The particle's positions are offsets from the view origin (view_origin.sh); the graph's nodes read absolute ones.
	vec3 view_direction = mg_camera_offset() - v_particle_wpos;
	vec3 quad_normal = normalize(v_particle_normal);
	vec3 quad_tangent = normalize(v_particle_tangent.xyz);
	vec3 quad_bitangent = cross(quad_normal, quad_tangent) * v_particle_tangent.w;

	MaterialInputs i;
	i.uv0 = v_particle_uv.xy;
	i.uv1 = v_particle_uv.xy;
	i.color0 = v_particle_color;
	i.world_position = viewOffsetToWorld(v_particle_wpos);
	i.vertex_normal = quad_normal;
	i.tangent = quad_tangent;
	i.bitangent = quad_bitangent;
	i.two_sided_sign = 1.0;
	i.view_direction = normalize(view_direction);
	i.camera_position = u_camera_wpos.xyz;
	i.camera_direction = mg_camera_direction();
	i.screen_uv = gl_FragCoord.xy * u_viewTexel.xy;
	i.time = u_material_time.x;
	i.mip_bias = u_camera_clip_planes.z;
	mg_set_domain_input_defaults(i);
	i.particle_data = v_particle_data;
	i.particle_custom = vec4(v_particle_custom.xy, v_particle_next.zw);
	i.particle_sheet_uv = vec4(v_particle_uv.zw, v_particle_next.xy);
	// The object is the particle: its centre, and its quad's axes as long as the quad is wide and high.
	i.object_position = viewOffsetToWorld(v_particle_center);
	i.object_axis_x = quad_tangent * v_particle_custom.z;
	i.object_axis_y = quad_bitangent * v_particle_custom.w;
	i.object_axis_z = quad_normal;
#if MATERIAL_INTERPOLATORS
	// A particle has no vertex stage of the graph's: its Vertex Interpolators read 0.
	i.interpolator0 = vec4_splat(0.0);
	i.interpolator1 = vec4_splat(0.0);
	i.interpolator2 = vec4_splat(0.0);
	i.interpolator3 = vec4_splat(0.0);
#endif

	MaterialSurface s = mg_default_surface();
	evaluate_material(i, s);

	float fade = v_particle_fade.x * particle_soft_fade(gl_FragCoord, v_particle_fade.y, v_particle_fade.z);
	float coverage = saturate(s.opacity_mask) * fade;

#if PARTICLE_VELOCITY
	if(coverage < v_particle_fade.w)
	{
		discard;
	}
	// Behind either camera plane there is no meaningful previous place: still, as the scene's pass writes it.
	if(v_curr_clip.w <= 0.0 || v_prev_clip.w <= 0.0 || v_prev_static_clip.w <= 0.0)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec2 previous_uv = mg_particle_clip_to_uv(v_prev_clip);
	float object_pixels = length((mg_particle_clip_to_uv(v_prev_static_clip) - previous_uv) * u_viewRect.zw);
	gl_FragColor = vec4(mg_particle_clip_to_uv(v_curr_clip) - previous_uv, object_pixels, v_prev_clip.w);
#else
	float pre_exposure = u_pre_exposure_value;
	vec3 emissive = max(s.emissive, vec3_splat(0.0));
	// The scene light the particle gives off and passes on, pre-exposed.
	vec3 radiance = emissive * pre_exposure;
#if !MG_PARTICLE_UNLIT
	// A particle's colour may be brighter than white (Intensity Over Life), so its Base Color is not kept below 1.
	// The Normal channel is in the quad's frame, as the particle lighting takes it: x along the quad's width, y up it,
	// z toward the camera. Left alone, the quad shades as the sphere it stands for (a trail or ribbon as a tube).
#if MATERIAL_NORMAL_LINKED
	vec3 surface_normal = normalize(s.normal);
#else
	float radius_squared = min(dot(v_quad_uv, v_quad_uv), 1.0);
	vec3 surface_normal = vec3(v_quad_uv, sqrt(1.0 - radius_squared));
#endif
	vec3 light = particle_surface_light(v_light_ambient, v_light_sun, v_sun_local, surface_normal);
#if MATERIAL_PARTICLE_SPECULAR
#if MATERIAL_SPECULAR_WORKFLOW
	// The workflow's solve takes colours up to white: a particle's brighter one keeps its brightness around it.
	float over_white = max(max(s.base_color.r, s.base_color.g), max(s.base_color.b, 1.0));
	mg_specular_to_metallic(s.base_color / over_white, s.specular, s.glossiness, s.base_color, s.metallic, s.roughness);
	s.base_color *= over_white;
#endif
	vec3 base_color = max(s.base_color, vec3_splat(0.0));
	float metallic = saturate(s.metallic);
	// What the surface reflects is worked out in the pixel's own frame: the quad's tangent, the bitangent square to it
	// and the quad's normal. The normal there is unit and faces the camera a little even in the quad's corners.
	vec3 reflecting_normal = normalize(vec3(surface_normal.xy, max(surface_normal.z, PARTICLE_MIN_REFLECTING_FACING)));
	vec3 toward_sun = -u_particle_sun_direction.xyz;
	vec3 sun_frame = vec3(dot(toward_sun, quad_tangent), dot(toward_sun, quad_bitangent), dot(toward_sun, quad_normal));
	vec3 view_frame = vec3(dot(i.view_direction, quad_tangent), dot(i.view_direction, quad_bitangent), dot(i.view_direction, quad_normal));
	// A sphere a few pixels wide turns through every normal: its highlight is widened to what a pixel can hold.
	float reflecting_roughness = GeometricSpecularAA(reflecting_normal, saturate(s.roughness));
	vec3 reflected = particle_reflected_light(v_light_ambient,
	                                          v_light_sun,
	                                          sun_frame,
	                                          reflecting_normal,
	                                          view_frame,
	                                          ComputeF0(0.5, saturate(base_color), metallic),
	                                          reflecting_roughness);
	radiance += ComputeDiffuseColor(base_color, metallic) * light * saturate(s.ambient_occlusion);
	// The graph's colour of a Premultiplied surface carries its Opacity and the light it reflects does not: the
	// Opacity takes that down too, as the coverage does in the other blend modes.
	radiance += reflected * (saturate(s.ambient_occlusion) * (particle_is_premultiplied() ? saturate(s.opacity_mask) : 1.0));
#else
	radiance += max(s.base_color, vec3_splat(0.0)) * light * saturate(s.ambient_occlusion);
#endif
#elif !MATERIAL_UNLIT
	// A lit material on an emitter whose Lighting is Unlit: its Base Color is light the particle gives off.
	radiance += max(s.base_color, vec3_splat(0.0)) * pre_exposure;
#endif

	vec4 color;
	if(particle_is_multiply())
	{
		// A unitless filter, never scaled by the pre-exposure.
		color = vec4(mix(vec3_splat(1.0), emissive, coverage), 1.0);
	}
	else if(particle_is_premultiplied())
	{
		color = vec4(radiance * fade, coverage);
	}
	else
	{
		color = vec4(radiance * coverage, coverage);
	}
	color = particle_apply_fog(color, gl_FragCoord, v_particle_fade.z);
#if MATERIAL_REFRACTION
	// The opaque scene behind, bent, takes the share of the pixel the particle leaves, as far as the particle bends its
	// view there (a full share from one pixel of shift on): where it bends nothing it blends as any particle, over
	// the particles and see-through surfaces already drawn behind it, which the scene read does not hold.
	if(u_particle_scene.x > 0.5 && !particle_is_multiply())
	{
		// By the Normal channel, whatever lights the particle: without one linked nothing bends.
		vec3 bend_normal = normalize(s.normal);
		vec3 world_normal = normalize(quad_tangent * bend_normal.x + quad_bitangent * bend_normal.y + quad_normal * bend_normal.z);
		vec2 bent_uv = mg_refracted_uv(v_particle_wpos, world_normal, i.view_direction, s.refraction, i.screen_uv);
		float bend = saturate(length((bent_uv - i.screen_uv) * u_viewRect.zw)) * fade;
		float share = bend * (1.0 - color.a);
		color.rgb += texture2DLod(s_particle_scene_color, bent_uv, 0.0).rgb * share;
		color.a += share;
	}
#endif
	gl_FragData[0] = color;
	gl_FragData[1] = particle_reactive(coverage, v_particle_fade.w);
#endif // PARTICLE_VELOCITY
}

#endif // FS_MATERIAL_GRAPH_PARTICLE_SH_HEADER_GUARD
