#ifndef FS_MATERIAL_GRAPH_DECAL_SH_HEADER_GUARD
#define FS_MATERIAL_GRAPH_DECAL_SH_HEADER_GUARD

/*
 * main() of a decal material: one pixel of the surfaces a decal's box covers, drawn by decal/vs_decal.eshader after
 * the G-buffer and before any lighting reads it (decal_pass.cpp).
 *
 * The pixel's surface comes from the depth copy: its position, the decal's box space there (outside the box: no
 * pixel), and from its neighbours on the same surface the geometric normal and the gradients texture reads take
 * their mips from. The decal fades by the instance's fade (opacity, screen size, distance, lifetime), the angle
 * between the surface and the projection, and the ends of its depth (DECAL_EDGE_FADE).
 *
 * What the graph writes (DECAL_WRITES_*) goes out premultiplied by its opacity:
 *  - the colour path (no DECAL_FULL): RT0 the G-buffer's base colour (blend ONE / INV_SRC_ALPHA, its AO kept), RT1
 *    the G-buffer's emissive (alpha 0: it adds);
 *  - the full path (DECAL_FULL): RT0 DBUFFER_A (base colour), RT1 DBUFFER_B (world normal biased to 0..1), RT2
 *    DBUFFER_C (metallic, roughness), RT3 the G-buffer's emissive; every alpha is the share of the surface the decal
 *    covers, which the blend (ONE / INV_SRC_ALPHA, alpha ZERO / INV_SRC_ALPHA) turns into the share left. The decal
 *    resolve (fs_decal_resolve.eshader) applies them to the G-buffer.
 */

void main()
{
	ivec2 texel = ivec2(gl_FragCoord.xy);
	float depth_center = mg_decal_texel_depth(texel);
	// An offset from the view origin (view_origin.sh); the box's rotation and scale take the offset from its centre,
	// so no absolute position meets the matrix.
	vec3 world_position = mg_decal_texel_position(texel);
	vec3 decal_center = vec3(v_decal_local_to_world0.w, v_decal_local_to_world1.w, v_decal_local_to_world2.w);
	mat4 world_to_decal = mtxFromRows(v_decal_world_to_local0, v_decal_world_to_local1, v_decal_world_to_local2, vec4(0.0, 0.0, 0.0, 1.0));
	vec3 decal_position = mul(world_to_decal, vec4(world_position - worldToViewOffset(decal_center), 0.0)).xyz;
	if(any(greaterThan(abs(decal_position), vec3_splat(0.5))) || !mg_decal_receives_layers(texel, v_decal_angle.z, v_decal_angle.w))
	{
		discard;
	}

	vec3 world_ddx = mg_decal_surface_step(texel, ivec2(1, 0), depth_center, world_position);
	vec3 world_ddy = mg_decal_surface_step(texel, ivec2(0, 1), depth_center, world_position);
	vec3 decal_ddx = mul(world_to_decal, vec4(world_ddx, 0.0)).xyz;
	vec3 decal_ddy = mul(world_to_decal, vec4(world_ddy, 0.0)).xyz;

	vec3 view_direction = normalize(mg_camera_offset() - world_position);
	vec3 geometric_normal = normalize(cross(world_ddy, world_ddx));
	if(dot(geometric_normal, view_direction) < 0.0)
	{
		geometric_normal = -geometric_normal;
	}

	// The decal's axes in the world: the columns of its local-to-world transform.
	vec3 axis_x = vec3(v_decal_local_to_world0.x, v_decal_local_to_world1.x, v_decal_local_to_world2.x);
	vec3 axis_y = vec3(v_decal_local_to_world0.y, v_decal_local_to_world1.y, v_decal_local_to_world2.y);
	vec3 axis_z = vec3(v_decal_local_to_world0.z, v_decal_local_to_world1.z, v_decal_local_to_world2.z);
	vec3 projection_direction = normalize(axis_z);

	// Surfaces turned toward the projector receive; those turned away by more than the end angle do not.
	float facing = dot(geometric_normal, -projection_direction);
	float angle_fade = saturate((facing - v_decal_angle.y) / max(v_decal_angle.x - v_decal_angle.y, 0.0001));
	float edge_fade = DECAL_EDGE_FADE > 0.0 ? saturate((0.5 - abs(decal_position.z)) / DECAL_EDGE_FADE) : 1.0;
	float fade = v_decal_fade.x * angle_fade * edge_fade;
	if(fade <= 0.0)
	{
		discard;
	}

	// The decal's frame on the surface: its width and height axes laid onto the geometric normal; v runs down.
	vec3 tangent = normalize(axis_x - geometric_normal * dot(geometric_normal, axis_x));
	vec3 bitangent = normalize(cross(geometric_normal, tangent));
	if(dot(bitangent, axis_y) > 0.0)
	{
		bitangent = -bitangent;
	}

	MaterialInputs i;
	i.uv0 = vec2(decal_position.x + 0.5, 0.5 - decal_position.y);
	i.uv1 = i.uv0;
	i.color0 = vec4_splat(1.0);
	i.world_position = viewOffsetToWorld(world_position);
	i.vertex_normal = geometric_normal;
	i.tangent = tangent;
	i.bitangent = bitangent;
	i.two_sided_sign = 1.0;
	i.view_direction = view_direction;
	i.camera_position = u_camera_wpos.xyz;
	i.camera_direction = mg_camera_direction();
	i.screen_uv = gl_FragCoord.xy * u_viewTexel.xy;
	i.time = u_material_time.x;
	i.mip_bias = u_decal_view.x;
	mg_set_domain_input_defaults(i);
	i.decal_data = vec4(fade, v_decal_fade.yzw);
	i.decal_position = decal_position;
	i.decal_position_ddx = decal_ddx;
	i.decal_position_ddy = decal_ddy;
	i.world_position_ddx = world_ddx;
	i.world_position_ddy = world_ddy;
	i.object_position = vec3(v_decal_local_to_world0.w, v_decal_local_to_world1.w, v_decal_local_to_world2.w);
	i.object_axis_x = axis_x;
	i.object_axis_y = axis_y;
	i.object_axis_z = axis_z;
	i.instance_register0 = v_material0;
	i.instance_register1 = v_material1;
	i.instance_register2 = v_material2;
	i.instance_register3 = v_material3;

	MaterialSurface s = mg_default_surface();
	evaluate_material(i, s);

	float opacity = saturate(s.opacity) * fade;
	vec2 surface_uv = i.screen_uv;

#if DECAL_FULL
	vec4 decal_a = vec4_splat(0.0);
	vec4 decal_b = vec4_splat(0.0);
	vec4 decal_c = vec4_splat(0.0);
#if DECAL_WRITES_BASE_COLOR
	decal_a = vec4(saturate(s.base_color) * opacity, opacity);
#endif
#if DECAL_WRITES_NORMAL
	float normal_opacity = saturate(s.normal_opacity) * fade;
	vec3 decal_normal = normalize(s.normal);
#if DECAL_NORMAL_REPLACE
	vec3 world_normal = normalize(tangent * decal_normal.x + bitangent * decal_normal.y + geometric_normal * decal_normal.z);
#else
	// Detail: the decal's normal in the frame of the surface's own normal (normal maps included), so the surface's
	// bumps show through it.
	vec3 surface_normal = decodeNormalOctahedron(mg_decal_gbuffer1(surface_uv).xy);
	vec3 surface_tangent = normalize(tangent - surface_normal * dot(surface_normal, tangent));
	vec3 surface_bitangent = normalize(bitangent - surface_normal * dot(surface_normal, bitangent) - surface_tangent * dot(surface_tangent, bitangent));
	vec3 world_normal = normalize(surface_tangent * decal_normal.x + surface_bitangent * decal_normal.y + surface_normal * decal_normal.z);
#endif
	decal_b = vec4((world_normal * 0.5 + 0.5) * normal_opacity, normal_opacity);
#endif
#if DECAL_WRITES_SURFACE
	float surface_opacity = saturate(s.surface_opacity) * fade;
	vec4 surface = vec4_splat(0.0);
#if !DECAL_WRITES_METALLIC || !DECAL_WRITES_ROUGHNESS
	surface = mg_decal_gbuffer1(surface_uv);
#endif
#if DECAL_WRITES_METALLIC
	float metallic = saturate(s.metallic);
#else
	float metallic = surface.z;
#endif
#if DECAL_WRITES_ROUGHNESS
	float roughness = saturate(s.roughness);
#else
	float roughness = surface.w;
#endif
	decal_c = vec4(vec2(metallic, roughness) * surface_opacity, 0.0, surface_opacity);
#endif
	gl_FragData[0] = decal_a;
	gl_FragData[1] = decal_b;
	gl_FragData[2] = decal_c;
#if DECAL_WRITES_EMISSIVE
	gl_FragData[3] = vec4(max(s.emissive, vec3_splat(0.0)) * opacity, 0.0);
#else
	gl_FragData[3] = vec4_splat(0.0);
#endif
#else
#if DECAL_WRITES_BASE_COLOR
	gl_FragData[0] = vec4(saturate(s.base_color) * opacity, opacity);
#else
	gl_FragData[0] = vec4_splat(0.0);
#endif
#if DECAL_WRITES_EMISSIVE
	gl_FragData[1] = vec4(max(s.emissive, vec3_splat(0.0)) * opacity, 0.0);
#else
	gl_FragData[1] = vec4_splat(0.0);
#endif
#endif
}

#endif // FS_MATERIAL_GRAPH_DECAL_SH_HEADER_GUARD
