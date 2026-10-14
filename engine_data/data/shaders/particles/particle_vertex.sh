#ifndef PARTICLE_VERTEX_SH_HEADER_GUARD
#define PARTICLE_VERTEX_SH_HEADER_GUARD

/*
 * One particle quad corner per vertex, shared by the particle vertex shaders. The instance picks the
 * particle from the draw's range of the sorted list (stage 4: interleaved key / slot pairs), and the
 * particle is evaluated at the emitter's current time from its birth record (particle_common.sh) - no
 * per-particle data is written per frame. The including shader declares the buffers of
 * particle_common.sh and s_particle_sorted at stage 4, then includes particle_common.sh.
 */

/// Each GPU batch's live count (cs_particle_draw_args.sc): a ribbon reads its neighbours within it. The vertex
/// shaders' buffers stay below stage 8 (particle_gpu_schema.h).
BUFFER_RO(s_particle_batch_counts, uint, 5);
/// Cutout tables (particle_cutout.h): per sheet frame 4 vec4, two polygon points (texture coordinates) each.
BUFFER_RO(s_particle_cutouts, vec4, 6);

// View matrix (glm column-major upload). Billboard uses camera axes from the view matrix.
uniform mat4 u_viewCamera;
/// xyz = the camera's velocity (m/s), for particles that stretch against it.
uniform vec4 u_particle_camera_velocity;
/// x = the draw's first entry in the sorted list, y = its GPU batch, z = a CPU list's count (-1 on the GPU path).
uniform vec4 u_particle_draw;

/// Below this squared length a direction is too short to orient a quad.
#define PARTICLE_MIN_DIRECTION_SQ 1e-12
/// Thresholds between the ribbon texture modes stored as floats (ribbon_texture_mode: age 0, stretch 1, tile 2).
#define PARTICLE_RIBBON_TEXTURE_STRETCH 0.5
#define PARTICLE_RIBBON_TEXTURE_TILE 1.5

/// The normal of a quad with axes @p axis_x and @p axis_y, on the side of @p toward_camera (the frame a lit particle
/// is shaded in stays orthonormal in every render mode).
vec3 particle_facing_normal(vec3 axis_x, vec3 axis_y, vec3 toward_camera)
{
	vec3 normal = cross(axis_x, axis_y);
	float length_squared = dot(normal, normal);
	if(length_squared < PARTICLE_MIN_DIRECTION_SQ)
	{
		return toward_camera;
	}
	normal *= inversesqrt(length_squared);
	return dot(normal, toward_camera) < 0.0 ? -normal : normal;
}

/// The share of a particle's opacity left at @p distance from the camera: the camera fade in and the fade out
/// before the max draw distance.
float particle_distance_fade(uint emitter, uint flags, float distance)
{
	float fade = 1.0;
	if((flags & PARTICLE_FLAG_CAMERA_FADE) != 0u)
	{
		vec4 view_shaping = particle_static(emitter, PARTICLE_STATIC_VIEW_SHAPING);
		fade *= clamp((distance - view_shaping.y) / max(view_shaping.z - view_shaping.y, 1e-4), 0.0, 1.0);
	}
	if((flags & PARTICLE_FLAG_DRAW_DISTANCE) != 0u)
	{
		vec2 draw_distance = particle_static(emitter, PARTICLE_STATIC_DISTANCE).xy;
		fade *= clamp((draw_distance.x - distance) / max(draw_distance.y, 1e-4), 0.0, 1.0);
	}
	return fade;
}

/// World size of one pixel at @p distance from a perspective camera: 2 d / (proj[1][1] h).
float particle_pixel_size(float distance)
{
	return 2.0 * distance / (u_proj[1][1] * u_viewRect.w);
}

vec3 quat_rotate(vec4 q, vec3 v)
{
	vec3 uv = cross(q.xyz, v);
	vec3 uuv = cross(q.xyz, uv);
	return v + ((uv * q.w) + uuv) * 2.0;
}

/// The quad's right and up axes for @p render_mode before spin and alignment.
void particle_axes(vec3 camera_right,
                   vec3 camera_up,
                   vec3 view_direction,
                   vec3 velocity,
                   float render_mode,
                   vec4 emitter_quat,
                   vec3 facing_axis,
                   out vec3 out_right,
                   out vec3 out_up)
{
	vec3 emitter_right = normalize(quat_rotate(emitter_quat, vec3(1.0, 0.0, 0.0)));
	vec3 emitter_up = normalize(quat_rotate(emitter_quat, vec3(0.0, 1.0, 0.0)));
	vec3 emitter_forward = normalize(quat_rotate(emitter_quat, vec3(0.0, 0.0, 1.0)));
	out_right = normalize(camera_right);
	out_up = normalize(camera_up);
	if(render_mode > 0.5 && render_mode < 1.5)
	{
		// Horizontal: quad lies in emitter local XZ plane.
		out_right = emitter_right;
		out_up = emitter_forward;
	}
	else if((render_mode > 1.5 && render_mode < 2.5) || abs(render_mode - PARTICLE_RENDER_MODE_FIXED_AXIS) < 0.5)
	{
		// Vertical / fixed axis: stay upright along the emitter's local Y (or facing_axis), yawing toward the camera.
		vec3 axis = emitter_up;
		if(render_mode > 2.5)
		{
			vec3 turned = quat_rotate(emitter_quat, facing_axis);
			axis = dot(turned, turned) > PARTICLE_MIN_DIRECTION_SQ ? normalize(turned) : emitter_up;
		}
		vec3 projected = camera_right - axis * dot(camera_right, axis);
		float projected_length = length(projected);
		vec3 fallback = cross(axis, view_direction);
		fallback = dot(fallback, fallback) > PARTICLE_MIN_DIRECTION_SQ ? normalize(fallback) : emitter_right;
		out_right = projected_length > 0.0001 ? projected * (1.0 / projected_length) : fallback;
		out_up = axis;
	}
	else if(abs(render_mode - PARTICLE_RENDER_MODE_CAMERA_FACING) < 0.5)
	{
		// Facing the camera's position, rolled to stay level with the camera.
		vec3 right = cross(camera_up, view_direction);
		if(dot(right, right) > PARTICLE_MIN_DIRECTION_SQ)
		{
			out_right = normalize(right);
			out_up = cross(view_direction, out_right);
		}
	}
	else if(abs(render_mode - PARTICLE_RENDER_MODE_STRETCHED) < 0.5)
	{
		// Up along the motion, width across it facing the camera.
		vec3 right = cross(velocity, view_direction);
		if(dot(velocity, velocity) > PARTICLE_MIN_DIRECTION_SQ && dot(right, right) > PARTICLE_MIN_DIRECTION_SQ)
		{
			out_right = normalize(right);
			out_up = normalize(velocity);
		}
	}
}

/// A particle quad corner and what the fragment shaders read of it.
struct particle_vertex
{
	vec4 clip;
	/// The corner and the particle's centre (after the camera offset), in world space.
	vec3 world_position;
	vec3 position;
	/// The particle's world velocity.
	vec3 velocity;
	/// World directions of the unit quad's x and y (spin and alignment applied) and the quad's normal (toward
	/// the camera).
	vec3 axis_x;
	vec3 axis_y;
	vec3 normal;
	/// This sheet frame's uv and the next one's (frame blending).
	vec2 texcoord0;
	vec2 texcoord1;
	vec4 color;
	/// Frame blend, soft fade distance, view depth, erosion threshold.
	vec4 particle;
	uint emitter;
	/// The unit quad's corner (-0.5..0.5) this vertex stands at, on the cutout once it moved there.
	vec2 quad_corner;
};

/**
 * A trail mode corner of @p vertex: @p corner.x across the trail (-0.5..0.5), corner.y along it (0 at the
 * particle, 1 at the tail trail_length seconds back along its path, clamped to its birth). The trail faces the
 * camera, narrows to trail_width_end of @p width and fades out toward the tail.
 */
void particle_trail_corner(particle_record record, vec2 corner, float width, vec3 camera_position, inout particle_vertex vertex)
{
	uint emitter = record.emitter;
	vec4 trail = particle_static(emitter, PARTICLE_STATIC_TRAIL);
	uint now = particle_dynamic_uint(emitter, PARTICLE_DYNAMIC_NOW);
	uint back = uint(corner.y * trail.x * PARTICLE_TICKS_PER_SECOND);
	particle_state earlier = particle_place_at(record, now - min(back, now - record.birth));
	vec3 toward_tail = -earlier.velocity;
	if(dot(toward_tail, toward_tail) < PARTICLE_MIN_DIRECTION_SQ)
	{
		toward_tail = vertex.axis_y;
	}
	vec3 side = cross(toward_tail, earlier.position - camera_position);
	side = dot(side, side) > PARTICLE_MIN_DIRECTION_SQ ? normalize(side) : vertex.axis_x;
	vertex.world_position = earlier.position + side * (corner.x * width * mix(1.0, trail.y, corner.y));
	vertex.clip = mul(u_modelViewProj, vec4(vertex.world_position, 1.0));
	vertex.texcoord0 = vec2(corner.x + 0.5, corner.y);
	vertex.texcoord1 = vertex.texcoord0;
	vertex.color.a *= 1.0 - corner.y;
	vertex.particle.z = vertex.clip.w;
	// A lit trail shades as a tube: across it in its own plane, never along it.
	vertex.axis_x = side;
	vertex.axis_y = normalize(-toward_tail);
	vertex.normal = particle_facing_normal(vertex.axis_x, vertex.axis_y, normalize(camera_position - earlier.position));
	vertex.quad_corner = vec2(corner.x, 0.0);
}

/// The live particles the draw's list holds (a CPU list's count, or its GPU batch's).
uint particle_draw_count()
{
	return u_particle_draw.z >= 0.0 ? uint(u_particle_draw.z) : s_particle_batch_counts[uint(u_particle_draw.y)];
}

/// The particle of the draw's list entry @p index (its first entry is 0), and its position.
particle_state particle_ribbon_point(uint index, out particle_record record)
{
	uint entry = uint(u_particle_draw.x) + index;
	record = particle_load(s_particle_sorted[entry * 2u + 1u]);
	return particle_place(record);
}

/**
 * A ribbon segment's corner of @p vertex: segment @p segment joins the list's particles segment (corner.y < 0) and
 * segment + 1 (oldest first), each end as wide as its particle, facing the camera across the ribbon's direction
 * there. The last segment has no far end and collapses.
 */
void particle_ribbon_corner(uint segment, vec2 corner, vec3 camera_position, inout particle_vertex vertex)
{
	uint count = particle_draw_count();
	bool is_far = corner.y > 0.0;
	uint index = min(segment + (is_far ? 1u : 0u), max(count, 1u) - 1u);
	particle_record record;
	particle_state anchor = particle_ribbon_point(index, record);
	particle_record before_record;
	particle_record after_record;
	particle_state before = particle_ribbon_point(index > 0u ? index - 1u : index, before_record);
	particle_state after = particle_ribbon_point(min(index + 1u, max(count, 1u) - 1u), after_record);
	vec3 along = after.position - before.position;
	if(dot(along, along) < PARTICLE_MIN_DIRECTION_SQ)
	{
		along = vertex.axis_y;
	}
	vec3 side = cross(along, anchor.position - camera_position);
	side = dot(side, side) > PARTICLE_MIN_DIRECTION_SQ ? normalize(side) : vertex.axis_x;
	float width = particle_quad(record, anchor).x * particle_static(record.emitter, PARTICLE_STATIC_SIZE_AXES).x;
	if(segment + 1u >= count)
	{
		width = 0.0;
	}
	// The anchor's own fades: its distance, and a width below a pixel keeping one pixel and fading by the rest.
	uint flags = particle_flags(record.emitter);
	float distance = max(length(anchor.position - camera_position), 1e-4);
	float fade = particle_distance_fade(record.emitter, flags, distance);
	float pixel_size = particle_pixel_size(distance);
	if((flags & PARTICLE_FLAG_SUBPIXEL_FADE) != 0u && width > 0.0 && width < pixel_size)
	{
		fade *= width / pixel_size;
		width = pixel_size;
	}
	vertex.world_position = anchor.position + side * (corner.x * width);
	vertex.clip = mul(u_modelViewProj, vec4(vertex.world_position, 1.0));
	// Along the ribbon (ribbon_texture_mode in z): the particle's life, its place from the head (the youngest,
	// last in the list) to the tail, or its age in texture repeats (w per second).
	vec4 texture_mode = particle_static(record.emitter, PARTICLE_STATIC_TRAIL);
	float along_ribbon = anchor.life;
	if(texture_mode.z > PARTICLE_RIBBON_TEXTURE_TILE)
	{
		along_ribbon = anchor.age * texture_mode.w;
	}
	else if(texture_mode.z > PARTICLE_RIBBON_TEXTURE_STRETCH)
	{
		along_ribbon = float(max(count, 1u) - 1u - index) / float(max(count, 2u) - 1u);
	}
	vertex.texcoord0 = vec2(corner.x + 0.5, along_ribbon);
	vertex.texcoord1 = vertex.texcoord0;
	vertex.color = particle_color(record, anchor);
	vertex.color.a *= fade;
	vertex.particle.z = vertex.clip.w;
	vertex.particle.w = particle_erosion(record, anchor);
	// A lit ribbon shades as a tube across it.
	vertex.position = anchor.position;
	vertex.axis_x = side;
	vertex.axis_y = normalize(along);
	vertex.normal = particle_facing_normal(vertex.axis_x, vertex.axis_y, normalize(camera_position - anchor.position));
	vertex.quad_corner = vec2(corner.x, 0.0);
}

/// The corner at @p corner_position (the unit quad's, -0.5..0.5) with texture coordinate @p corner_uv of
/// the particle instance @p instance draws (gl_InstanceID, which only main sees on every backend).
particle_vertex particle_build_vertex(vec3 corner_position, vec2 corner_uv, uint instance)
{
	uint entry = uint(u_particle_draw.x) + instance;
	uint slot = s_particle_sorted[entry * 2u + 1u];
	particle_record record = particle_load(slot);
	particle_state state = particle_place(record);
	uint emitter = record.emitter;
	uint flags = particle_flags(emitter);
	// Stretched particles may stretch against the camera's motion (a share of it, the facing slot's w).
	vec4 facing = particle_static(emitter, PARTICLE_STATIC_FACING);
	vec3 stretch_velocity = state.velocity - u_particle_camera_velocity.xyz * facing.w;
	vec2 quad = particle_quad_stretched(record, state, stretch_velocity);
	vec4 color = particle_color(record, state);
	float angle = particle_angle(record, state);
	vec2 uv_scale;
	float frame_blend;
	vec4 sheet = particle_sheet(record, state, uv_scale, frame_blend);
	// The cutout primitive (corner z = its vertex + 1) moves onto its frame's polygon once the table is in
	// (the distance slot's w = its first vec4 + 1); until then it outlines the whole quad.
	float cutout_table = particle_static(emitter, PARTICLE_STATIC_DISTANCE).w;
	if(corner_position.z > 0.5 && cutout_table > 0.5)
	{
		uint tiles_x = uint(1.0 / uv_scale.x + 0.5);
		uint frame = uint(sheet.y / uv_scale.y + 0.5) * tiles_x + uint(sheet.x / uv_scale.x + 0.5);
		uint cutout_vertex = uint(corner_position.z - 0.5);
		vec4 points = s_particle_cutouts[uint(cutout_table - 0.5) + frame * 4u + cutout_vertex / 2u];
		corner_uv = (cutout_vertex % 2u) == 0u ? points.xy : points.zw;
		corner_position = vec3(corner_uv.x - 0.5, 0.5 - corner_uv.y, 0.0);
	}
	vec4 render = particle_static(emitter, PARTICLE_STATIC_RENDER);
	vec4 view_shaping = particle_static(emitter, PARTICLE_STATIC_VIEW_SHAPING);
	vec2 pivot = particle_static(emitter, PARTICLE_STATIC_SHEET).xy;
	vec4 emitter_quat = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_ROTATION);
	vec3 camera_position = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 to_camera = camera_position - state.position;
	float distance = max(length(to_camera), 1e-4);
	vec3 view_direction = -to_camera / distance;
	// Camera offset: toward the camera, never past its near distance.
	float offset = min(view_shaping.x, distance * 0.5);
	vec3 position = state.position - view_direction * offset;
	distance -= offset;
	float pixel_size = particle_pixel_size(distance);
	bool is_trail = abs(render.w - PARTICLE_RENDER_MODE_TRAIL) < 0.5;
	if(view_shaping.w > 0.0)
	{
		// A trail's length is its path, not its size: only its width is capped.
		float largest = view_shaping.w * u_viewRect.w * pixel_size;
		float extent = is_trail ? quad.x : max(quad.x, quad.y);
		quad *= min(1.0, largest / max(extent, 1e-6));
	}
	if((flags & PARTICLE_FLAG_SUBPIXEL_FADE) != 0u && min(quad.x, quad.y) < pixel_size)
	{
		// Each axis below a pixel keeps one pixel; the opacity falls by the area the widening added, so a thin
		// streak stays as long as it is and only fades.
		vec2 widened = max(quad, vec2_splat(pixel_size));
		color.a *= (quad.x * quad.y) / max(widened.x * widened.y, 1e-12);
		quad = widened;
	}
	color.a *= particle_distance_fade(emitter, flags, distance);
	vec3 camera_right = mtxGetColumn(u_viewCamera, 0).xyz;
	vec3 camera_up = mtxGetColumn(u_viewCamera, 1).xyz;
	vec3 right;
	vec3 up;
	particle_axes(camera_right, camera_up, view_direction, stretch_velocity, render.w, emitter_quat, facing.xyz, right, up);
	// align_to_direction: the quad turns in its own plane - the camera's, the emitter's XZ plane or the
	// upright one - so the texture's top edge follows the motion projected into that plane. A stretched
	// quad is aligned already.
	if((flags & PARTICLE_FLAG_ALIGN) != 0u && abs(render.w - PARTICLE_RENDER_MODE_STRETCHED) > 0.5)
	{
		vec2 in_plane = vec2(dot(state.velocity, right), dot(state.velocity, up));
		float in_plane_length_squared = dot(in_plane, in_plane);
		if(in_plane_length_squared > 1e-12)
		{
			in_plane *= inversesqrt(in_plane_length_squared);
			vec3 aligned_up = right * in_plane.x + up * in_plane.y;
			vec3 aligned_right = right * in_plane.y - up * in_plane.x;
			right = aligned_right;
			up = aligned_up;
		}
	}
	vec2 scale = particle_static(emitter, PARTICLE_STATIC_SIZE_AXES).xy * quad;
	// A dead particle (a stale slot past its life) collapses to a point.
	if(!state.is_alive)
	{
		scale = vec2_splat(0.0);
	}
	// The spin turns the corner around the particle position in the quad's own plane.
	vec2 corner = (corner_position.xy + (pivot - vec2(0.5, 0.5))) * scale;
	float spin_sin = sin(angle);
	float spin_cos = cos(angle);
	corner = vec2(corner.x * spin_cos - corner.y * spin_sin, corner.x * spin_sin + corner.y * spin_cos);
	particle_vertex vertex;
	vertex.emitter = emitter;
	vertex.quad_corner = corner_position.xy;
	vertex.position = position;
	vertex.velocity = state.velocity;
	vertex.world_position = position + right * corner.x + up * corner.y;
	vertex.axis_x = right * spin_cos + up * spin_sin;
	vertex.axis_y = up * spin_cos - right * spin_sin;
	vertex.normal = particle_facing_normal(vertex.axis_x, vertex.axis_y, -view_direction);
	vertex.clip = mul(u_modelViewProj, vec4(vertex.world_position, 1.0));
	vertex.texcoord0 = corner_uv * uv_scale + sheet.xy;
	vertex.texcoord1 = corner_uv * uv_scale + sheet.zw;
	// Clip w is the view depth for a perspective projection; the soft fade compares it with the scene's.
	vertex.particle = vec4(frame_blend, render.z, vertex.clip.w, particle_erosion(record, state));
	vertex.color = color;
	if(abs(render.w - PARTICLE_RENDER_MODE_TRAIL) < 0.5)
	{
		particle_trail_corner(record, corner_position.xy, state.is_alive ? scale.x : 0.0, camera_position, vertex);
	}
	else if(abs(render.w - PARTICLE_RENDER_MODE_RIBBON) < 0.5)
	{
		particle_ribbon_corner(instance, corner_position.xy, camera_position, vertex);
	}
	return vertex;
}

#endif // PARTICLE_VERTEX_SH_HEADER_GUARD
