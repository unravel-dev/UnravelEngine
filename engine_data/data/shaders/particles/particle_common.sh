#ifndef PARTICLE_COMMON_SH_HEADER_GUARD
#define PARTICLE_COMMON_SH_HEADER_GUARD

/*
 * Analytic particles: what a particle is at any age, evaluated in closed form from the record written
 * once at its birth. Shared by the per-view cull / sort-key pass and the vertex shader, and mirrored on
 * the CPU by engine/engine/rendering/particles/ps/particle_eval.cpp - keep the two in step.
 *
 * The including shader declares, at these stages:
 *   0 s_particle_records  uint  PARTICLE_RECORD_UINTS per particle
 *   1 s_particle_static   vec4  PARTICLE_STATIC_VEC4S per emitter (changes with the emitter's settings)
 *   2 s_particle_dynamic  uint  PARTICLE_DYNAMIC_UINTS per emitter (rewritten every frame)
 *   3 s_particle_luts     vec4  PARTICLE_LUT_ROWS x PARTICLE_LUT_SIZE per curve set
 * and binds the turbulence noise volume this file declares at stage 7 (s_particle_noise).
 */

#define PARTICLE_RECORD_UINTS 16u
#define PARTICLE_STATIC_VEC4S 15u
#define PARTICLE_DYNAMIC_UINTS 28u
#define PARTICLE_LUT_SIZE 256u
#define PARTICLE_LUT_ROWS 7u
#define PARTICLE_TICKS_PER_SECOND 8192.0

/// Slots of the emitter static row (particle_record.h, static_slot).
#define PARTICLE_STATIC_APPEARANCE 0u
#define PARTICLE_STATIC_SIZE_AXES 1u
#define PARTICLE_STATIC_SIZE_BY_SPEED 2u
#define PARTICLE_STATIC_RENDER 3u
#define PARTICLE_STATIC_SHEET 4u
#define PARTICLE_STATIC_ROTATION 5u
#define PARTICLE_STATIC_SPIN 6u
#define PARTICLE_STATIC_VIEW_SHAPING 7u
#define PARTICLE_STATIC_STRETCH 8u
#define PARTICLE_STATIC_TURBULENCE 9u
#define PARTICLE_STATIC_NOISE 10u
#define PARTICLE_STATIC_DISTANCE 11u
#define PARTICLE_STATIC_TRAIL 12u
#define PARTICLE_STATIC_FACING 13u
#define PARTICLE_STATIC_SPEED_BOUND 14u

/// Slots of the emitter dynamic row (particle_record.h, dynamic_slot).
#define PARTICLE_DYNAMIC_LOCAL_TO_WORLD 0u
#define PARTICLE_DYNAMIC_ROTATION 12u
#define PARTICLE_DYNAMIC_ACCELERATION 16u
#define PARTICLE_DYNAMIC_DRAG 19u
#define PARTICLE_DYNAMIC_NOW 20u
#define PARTICLE_DYNAMIC_SYSTEM_SCALE 21u
#define PARTICLE_DYNAMIC_FRAME_TIME 22u
#define PARTICLE_DYNAMIC_ORIGIN_MOTION 23u

#define PARTICLE_LUT_ROW_COLOR 0u
#define PARTICLE_LUT_ROW_LIFE 1u
#define PARTICLE_LUT_ROW_COLOR_BY_SPEED 2u
#define PARTICLE_LUT_ROW_SPEED 3u
#define PARTICLE_LUT_ROW_SHAPE 4u
#define PARTICLE_LUT_ROW_COLOR_B 5u
#define PARTICLE_LUT_ROW_TURBULENCE_SIZE_Z 6u

#define PARTICLE_FLAG_ALIGN 1u
#define PARTICLE_FLAG_TEXSHEET 2u
#define PARTICLE_FLAG_COLOR_BY_SPEED 4u
#define PARTICLE_FLAG_SIZE_BY_SPEED 8u
#define PARTICLE_FLAG_LOCAL_SPACE 16u
#define PARTICLE_FLAG_EASE 32u
#define PARTICLE_FLAG_SPEED_CURVE 64u
#define PARTICLE_FLAG_FRAME_BLEND 128u
#define PARTICLE_FLAG_TEX_RANDOMIZE 256u
#define PARTICLE_FLAG_TEX_SINGLE_PASS 512u
#define PARTICLE_FLAG_RANDOM_SPIN_DIR 1024u
#define PARTICLE_FLAG_EROSION 2048u
#define PARTICLE_FLAG_SUBPIXEL_FADE 4096u
#define PARTICLE_FLAG_CAMERA_FADE 8192u
#define PARTICLE_FLAG_SIZE_AXIS_CURVE 16384u
#define PARTICLE_FLAG_SPIN_CURVE 32768u
#define PARTICLE_FLAG_INTENSITY_CURVE 65536u
#define PARTICLE_FLAG_TWO_GRADIENTS 131072u
#define PARTICLE_FLAG_FRAME_CURVE 262144u
#define PARTICLE_FLAG_RANDOM_ROW 524288u
#define PARTICLE_FLAG_TURBULENCE 1048576u
#define PARTICLE_FLAG_DRAW_DISTANCE 2097152u
#define PARTICLE_FLAG_LIT_VOLUMETRIC 4194304u
#define PARTICLE_FLAG_LIT_SURFACE 8388608u

#define PARTICLE_RENDER_MODE_CAMERA_FACING 3.0
#define PARTICLE_RENDER_MODE_STRETCHED 4.0
#define PARTICLE_RENDER_MODE_TRAIL 5.0
#define PARTICLE_RENDER_MODE_RIBBON 6.0
#define PARTICLE_RENDER_MODE_FIXED_AXIS 8.0

/// Salts of the per-particle random picks drawn from the particle's seed.
#define PARTICLE_SALT_ROTATION 1u
#define PARTICLE_SALT_SPIN 2u
#define PARTICLE_SALT_SPIN_DIRECTION 3u
#define PARTICLE_SALT_SHEET 4u
#define PARTICLE_SALT_GRADIENT 5u
#define PARTICLE_SALT_SHEET_ROW 6u

/// Below this drag x time the exponential forms lose precision (the gravity term divides a small difference by
/// drag squared); their series are used instead, exact to float precision up to it.
#define PARTICLE_SMALL_DRAG_TIME 0.1

struct particle_record
{
	vec3 start;
	uint birth;
	vec3 velocity;
	uint lifespan;
	uint seed;
	vec2 size_picks;
	float size_factor;
	float size_offset;
	vec4 tint;
	uint emitter;
	uint motion_offset;
};

struct particle_state
{
	/// World-space position and velocity (metres, metres per second).
	vec3 position;
	vec3 velocity;
	/// Age in seconds and the particle's life progress 0..1.
	float age;
	float life;
	/// Seconds into its life the particle's current path segment began (its last bounce; 0 before any).
	float segment_age;
	bool is_alive;
};

/// Two half floats packed in a uint (low half first). bgfx_shader.sh has no unpackHalf2x16 for SPIR-V,
/// which compiles through the HLSL front end, so the HLSL intrinsic is used there.
vec2 particle_unpack_half2(uint packed)
{
#if BGFX_SHADER_LANGUAGE_GLSL
	return unpackHalf2x16(packed);
#else
	return vec2(f16tof32(packed & 0xffffu), f16tof32(packed >> 16u));
#endif
}

uint particle_hash(uint value)
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

/// A number in [0, 1) drawn from the particle's seed; each salt gives an independent pick.
float particle_random(uint seed, uint salt)
{
	return float(particle_hash(seed ^ (salt * 2654435769u)) >> 8u) * (1.0 / 16777216.0);
}

vec3 particle_load_vec3(uint base)
{
	return uintBitsToFloat(uvec3(s_particle_records[base], s_particle_records[base + 1u], s_particle_records[base + 2u]));
}

particle_record particle_load(uint slot)
{
	uint base = slot * PARTICLE_RECORD_UINTS;
	particle_record record;
	record.start = particle_load_vec3(base);
	record.birth = s_particle_records[base + 3u];
	record.velocity = particle_load_vec3(base + 4u);
	record.lifespan = s_particle_records[base + 7u];
	record.seed = s_particle_records[base + 8u];
	uint picks = s_particle_records[base + 9u];
	record.size_picks = vec2(float(picks & 0xffffu), float(picks >> 16u)) * (1.0 / 65535.0);
	record.size_factor = uintBitsToFloat(s_particle_records[base + 10u]);
	record.size_offset = uintBitsToFloat(s_particle_records[base + 11u]);
	record.tint = vec4(particle_unpack_half2(s_particle_records[base + 12u]), particle_unpack_half2(s_particle_records[base + 13u]));
	record.emitter = s_particle_records[base + 14u] & 0xffffu;
	record.motion_offset = s_particle_records[base + 15u];
	return record;
}

vec4 particle_static(uint emitter, uint index)
{
	return s_particle_static[emitter * PARTICLE_STATIC_VEC4S + index];
}

float particle_dynamic_float(uint emitter, uint index)
{
	return uintBitsToFloat(s_particle_dynamic[emitter * PARTICLE_DYNAMIC_UINTS + index]);
}

uint particle_dynamic_uint(uint emitter, uint index)
{
	return s_particle_dynamic[emitter * PARTICLE_DYNAMIC_UINTS + index];
}

vec4 particle_dynamic_vec4(uint emitter, uint index)
{
	return vec4(particle_dynamic_float(emitter, index),
	            particle_dynamic_float(emitter, index + 1u),
	            particle_dynamic_float(emitter, index + 2u),
	            particle_dynamic_float(emitter, index + 3u));
}

/// The emitter's acceleration this frame (gravity and its own), in simulation space.
vec3 particle_acceleration(uint emitter)
{
	return vec3(particle_dynamic_float(emitter, PARTICLE_DYNAMIC_ACCELERATION),
	            particle_dynamic_float(emitter, PARTICLE_DYNAMIC_ACCELERATION + 1u),
	            particle_dynamic_float(emitter, PARTICLE_DYNAMIC_ACCELERATION + 2u));
}

uint particle_flags(uint emitter)
{
	return uint(particle_static(emitter, PARTICLE_STATIC_ROTATION).y);
}

/// Row @p row of the emitter's curve set at @p t (0..1), linearly between its two nearest samples.
vec4 particle_lut(uint emitter, uint row, float t)
{
	uint base = uint(particle_static(emitter, PARTICLE_STATIC_SIZE_AXES).w) + row * PARTICLE_LUT_SIZE;
	float position = clamp(t, 0.0, 1.0) * float(PARTICLE_LUT_SIZE - 1u);
	uint index = min(uint(position), PARTICLE_LUT_SIZE - 2u);
	return mix(s_particle_luts[base + index], s_particle_luts[base + index + 1u], position - float(index));
}

vec3 particle_local_to_world_point(uint emitter, vec3 position)
{
	vec4 row0 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD);
	vec4 row1 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD + 4u);
	vec4 row2 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD + 8u);
	vec4 homogeneous = vec4(position, 1.0);
	return vec3(dot(row0, homogeneous), dot(row1, homogeneous), dot(row2, homogeneous));
}

vec3 particle_local_to_world_vector(uint emitter, vec3 direction)
{
	vec3 row0 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD).xyz;
	vec3 row1 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD + 4u).xyz;
	vec3 row2 = particle_dynamic_vec4(emitter, PARTICLE_DYNAMIC_LOCAL_TO_WORLD + 8u).xyz;
	return vec3(dot(row0, direction), dot(row1, direction), dot(row2, direction));
}

/// The distance an initial velocity carries a particle in @p time seconds under drag @p drag: the
/// integral of exp(-drag t). Its acceleration counterpart is (time - lambda) / drag.
float particle_drag_lambda(float drag, float time)
{
	float x = drag * time;
	if(abs(x) < PARTICLE_SMALL_DRAG_TIME)
	{
		return time * (1.0 - x * (1.0 / 2.0 - x * (1.0 / 6.0 - x * (1.0 / 24.0 - x * (1.0 / 120.0)))));
	}
	return (1.0 - exp(-x)) / drag;
}

float particle_drag_gamma(float drag, float time, float lambda)
{
	float x = drag * time;
	if(abs(x) < PARTICLE_SMALL_DRAG_TIME)
	{
		return time * time * (1.0 / 2.0 - x * (1.0 / 6.0 - x * (1.0 / 24.0 - x * (1.0 / 120.0 - x * (1.0 / 720.0)))));
	}
	return (time - lambda) / drag;
}

/**
 * The particle's age, life and motion at the emitter's current time. Motion is the closed form of a
 * constant acceleration under linear drag (exact at any age); position easing and the speed curve
 * reshape it through the curve set.
 */
particle_state particle_evaluate_at(particle_record record, uint now)
{
	uint emitter = record.emitter;
	uint flags = particle_flags(emitter);
	particle_state state;
	uint age_ticks = now - record.birth;
	state.is_alive = age_ticks < record.lifespan;
	float lifespan = max(float(record.lifespan), 1.0) / PARTICLE_TICKS_PER_SECOND;
	state.age = float(age_ticks) / PARTICLE_TICKS_PER_SECOND;
	state.life = clamp(state.age / lifespan, 0.0, 1.0);
	// A bounce starts a new segment of the path motion_offset ticks into the life; the curves go on from there.
	float segment_age = float(min(record.motion_offset, age_ticks)) / PARTICLE_TICKS_PER_SECOND;
	state.segment_age = segment_age;
	vec3 acceleration = particle_acceleration(emitter);
	float drag = particle_dynamic_float(emitter, PARTICLE_DYNAMIC_DRAG);
	float time = state.age - segment_age;
	float time_rate = 1.0;
	float progress = state.life;
	float segment_progress = clamp(segment_age / lifespan, 0.0, 1.0);
	if((flags & PARTICLE_FLAG_EASE) != 0u)
	{
		vec4 life_curves = particle_lut(emitter, PARTICLE_LUT_ROW_LIFE, progress);
		float segment_eased = particle_lut(emitter, PARTICLE_LUT_ROW_LIFE, segment_progress).x;
		time = (life_curves.x - segment_eased) * lifespan;
		time_rate = life_curves.y;
		progress = life_curves.x;
		segment_progress = segment_eased;
	}
	float lambda = particle_drag_lambda(drag, time);
	float gamma = particle_drag_gamma(drag, time, lambda);
	vec3 displacement;
	vec3 velocity;
	if((flags & PARTICLE_FLAG_SPEED_CURVE) != 0u)
	{
		vec4 speed = particle_lut(emitter, PARTICLE_LUT_ROW_SPEED, progress);
		float segment_integral = particle_lut(emitter, PARTICLE_LUT_ROW_SPEED, segment_progress).x;
		displacement = record.velocity * ((speed.x - segment_integral) * lifespan);
		velocity = record.velocity * speed.y;
	}
	else
	{
		displacement = record.velocity * lambda;
		velocity = record.velocity * exp(-drag * time);
	}
	vec3 position = record.start + displacement + acceleration * gamma;
	velocity = (velocity + acceleration * lambda) * time_rate;
	if((flags & PARTICLE_FLAG_LOCAL_SPACE) != 0u)
	{
		position = particle_local_to_world_point(emitter, position);
		velocity = particle_local_to_world_vector(emitter, velocity);
	}
	state.position = position;
	state.velocity = velocity;
	return state;
}

/// The particle at its emitter's current time.
particle_state particle_evaluate(particle_record record)
{
	return particle_evaluate_at(record, particle_dynamic_uint(record.emitter, PARTICLE_DYNAMIC_NOW));
}

/// How far the size pick has drifted from its birth place toward its death place at @p life,
/// weighted by the range widths (particle_scale_curve.cpp, get_scale_pick_blend).
float particle_size_pick_blend(float life, float width_start, float width_end)
{
	float start_weight = (1.0 - life) * width_start;
	float end_weight = life * width_end;
	float total = start_weight + end_weight;
	return total > 1e-6 ? end_weight / total : life;
}

float particle_speed_factor(float speed, float minimum, float inverse_span)
{
	return clamp((speed - minimum) * inverse_span, 0.0, 1.0);
}

/// The particle's size (before the emitter's 3D scale) at its state.
float particle_size(particle_record record, particle_state state)
{
	uint emitter = record.emitter;
	vec4 widths = particle_static(emitter, PARTICLE_STATIC_APPEARANCE);
	vec4 life_curves = particle_lut(emitter, PARTICLE_LUT_ROW_LIFE, state.life);
	float place = mix(record.size_picks.x, record.size_picks.y, particle_size_pick_blend(state.life, widths.z, widths.w));
	float size = (mix(life_curves.z, life_curves.w, place) * record.size_factor + record.size_offset) * particle_dynamic_float(emitter, PARTICLE_DYNAMIC_SYSTEM_SCALE);
	if((particle_flags(emitter) & PARTICLE_FLAG_SIZE_BY_SPEED) != 0u)
	{
		vec4 size_by_speed = particle_static(emitter, PARTICLE_STATIC_SIZE_BY_SPEED);
		float factor = particle_speed_factor(length(state.velocity), size_by_speed.w, size_by_speed.z);
		size *= mix(size_by_speed.x, size_by_speed.y, factor);
	}
	return max(size, 0.0);
}

/// The particle's color (linear, alpha not premultiplied) at its state.
vec4 particle_color(particle_record record, particle_state state)
{
	uint emitter = record.emitter;
	uint flags = particle_flags(emitter);
	vec4 color = particle_lut(emitter, PARTICLE_LUT_ROW_COLOR, state.life);
	if((flags & PARTICLE_FLAG_TWO_GRADIENTS) != 0u)
	{
		color = mix(color, particle_lut(emitter, PARTICLE_LUT_ROW_COLOR_B, state.life), particle_random(record.seed, PARTICLE_SALT_GRADIENT));
	}
	if((flags & PARTICLE_FLAG_COLOR_BY_SPEED) != 0u)
	{
		vec4 color_by_speed = particle_static(emitter, PARTICLE_STATIC_RENDER);
		color *= particle_lut(emitter, PARTICLE_LUT_ROW_COLOR_BY_SPEED, particle_speed_factor(length(state.velocity), color_by_speed.y, color_by_speed.x));
	}
	color *= record.tint;
	vec4 strength = particle_static(emitter, PARTICLE_STATIC_APPEARANCE);
	float intensity = strength.y;
	if((flags & PARTICLE_FLAG_INTENSITY_CURVE) != 0u)
	{
		intensity *= particle_lut(emitter, PARTICLE_LUT_ROW_SHAPE, state.life).w;
	}
	color.a *= strength.x;
	color.rgb *= intensity;
	return color;
}

/// The particle's angle in its quad's plane, radians.
float particle_angle(particle_record record, particle_state state)
{
	uint emitter = record.emitter;
	vec4 rotation = particle_static(emitter, PARTICLE_STATIC_ROTATION);
	vec4 spin = particle_static(emitter, PARTICLE_STATIC_SPIN);
	float speed = mix(spin.x, spin.y, particle_random(record.seed, PARTICLE_SALT_SPIN));
	if((particle_flags(emitter) & PARTICLE_FLAG_RANDOM_SPIN_DIR) != 0u && particle_random(record.seed, PARTICLE_SALT_SPIN_DIRECTION) < 0.5)
	{
		speed = -speed;
	}
	// A spin curve turns by the curve's integral over the life lived (in lifetimes) times the lifetime.
	float spun_time = state.age;
	if((particle_flags(emitter) & PARTICLE_FLAG_SPIN_CURVE) != 0u)
	{
		float lifespan = max(float(record.lifespan), 1.0) / PARTICLE_TICKS_PER_SECOND;
		spun_time = particle_lut(emitter, PARTICLE_LUT_ROW_SHAPE, state.life).z * lifespan;
	}
	return mix(rotation.z, rotation.w, particle_random(record.seed, PARTICLE_SALT_ROTATION)) + speed * spun_time;
}

/// Texture sheet frame of the particle: xy = uv offset, zw = the next frame's uv offset; the blend
/// amount toward the next frame in @p blend (0 without frame blending).
vec4 particle_sheet(particle_record record, particle_state state, out vec2 uv_scale, out float blend)
{
	uint emitter = record.emitter;
	uint flags = particle_flags(emitter);
	vec4 sheet = particle_static(emitter, PARTICLE_STATIC_SHEET);
	vec2 tiles = max(sheet.zw, vec2_splat(1.0));
	uv_scale = vec2_splat(1.0) / tiles;
	blend = 0.0;
	if((flags & PARTICLE_FLAG_TEXSHEET) == 0u)
	{
		return vec4_splat(0.0);
	}
	uint tiles_x = uint(tiles.x);
	uint tiles_y = uint(tiles.y);
	// A random row plays its own frames along the row; otherwise the frames run through the whole sheet.
	bool is_random_row = (flags & PARTICLE_FLAG_RANDOM_ROW) != 0u;
	uint total = is_random_row ? max(tiles_x, 1u) : max(tiles_x * tiles_y, 1u);
	uint first = is_random_row ? min(uint(particle_random(record.seed, PARTICLE_SALT_SHEET_ROW) * float(tiles_y)), tiles_y - 1u) * tiles_x : 0u;
	float cycles = particle_static(emitter, PARTICLE_STATIC_ROTATION).x;
	bool is_frame_curve = (flags & PARTICLE_FLAG_FRAME_CURVE) != 0u;
	float progress = is_frame_curve ? particle_lut(emitter, PARTICLE_LUT_ROW_SPEED, state.life).w : state.life * cycles;
	if((flags & PARTICLE_FLAG_TEX_RANDOMIZE) != 0u)
	{
		progress += particle_random(record.seed, PARTICLE_SALT_SHEET);
	}
	bool is_single_pass = (flags & PARTICLE_FLAG_TEX_SINGLE_PASS) != 0u;
	// A sheet played once stops on its last frame at death instead of wrapping to the first.
	progress = is_single_pass ? clamp(progress, 0.0, 0.99999) : fract(progress);
	float frame_position = progress * float(total);
	uint frame = min(uint(frame_position), total - 1u);
	uint next = is_single_pass ? min(frame + 1u, total - 1u) : (frame + 1u) % total;
	if((flags & PARTICLE_FLAG_FRAME_BLEND) != 0u && (cycles > 0.0 || is_frame_curve))
	{
		blend = fract(frame_position);
	}
	else
	{
		next = frame;
	}
	frame += first;
	next += first;
	return vec4(float(frame % tiles_x), float(frame / tiles_x), float(next % tiles_x), float(next / tiles_x)) * uv_scale.xyxy;
}

/// The turbulence noise volume (particle_noise.h): a tileable curl field with lengths up to 1.
SAMPLER3D(s_particle_noise, 7);

/// Seconds between the two noise samples the turbulence's velocity is taken from (turbulence_velocity_step).
#define PARTICLE_TURBULENCE_STEP (1.0 / 30.0)
/// The direction the noise scrolls along as it evolves (turbulence_scroll_axis).
#define PARTICLE_TURBULENCE_SCROLL 0.57735027

/// Adds the turbulence to @p state: the field at the particle's start, scrolled from its birth by its age (so a
/// stream from one point sways over time); the displacement is how far the scroll has moved it since its segment
/// began, the velocity that displacement's rate (particle_eval.cpp, evaluate_particle).
void particle_apply_turbulence(particle_record record, inout particle_state state)
{
	uint emitter = record.emitter;
	vec4 turbulence = particle_static(emitter, PARTICLE_STATIC_TURBULENCE);
	float strength = turbulence.x * particle_lut(emitter, PARTICLE_LUT_ROW_TURBULENCE_SIZE_Z, state.life).x;
	vec3 scroll = vec3_splat(PARTICLE_TURBULENCE_SCROLL * turbulence.z);
	// The noise tiles, so only the coordinates' fractions matter.
	float birth_time = float(record.birth) / PARTICLE_TICKS_PER_SECOND;
	vec3 base = fract(record.start * turbulence.y + particle_static(emitter, PARTICLE_STATIC_NOISE).xyz) + fract(scroll * birth_time);
	vec3 origin = texture3DLod(s_particle_noise, base + scroll * state.segment_age, 0.0).xyz;
	vec3 now = texture3DLod(s_particle_noise, base + scroll * state.age, 0.0).xyz;
	vec3 later = texture3DLod(s_particle_noise, base + scroll * (state.age + PARTICLE_TURBULENCE_STEP), 0.0).xyz;
	vec3 displacement = (now - origin) * strength;
	vec3 velocity = (later - now) * (strength / PARTICLE_TURBULENCE_STEP);
	if((particle_flags(emitter) & PARTICLE_FLAG_LOCAL_SPACE) != 0u)
	{
		displacement = particle_local_to_world_vector(emitter, displacement);
		velocity = particle_local_to_world_vector(emitter, velocity);
	}
	state.position += displacement;
	state.velocity += velocity;
}

/// The particle at emitter tick @p now where it draws: its motion plus the turbulence's push.
particle_state particle_place_at(particle_record record, uint now)
{
	particle_state state = particle_evaluate_at(record, now);
	if((particle_flags(record.emitter) & PARTICLE_FLAG_TURBULENCE) != 0u)
	{
		particle_apply_turbulence(record, state);
	}
	return state;
}

/// The particle now where it draws (particle_place_at at the emitter's current tick).
particle_state particle_place(particle_record record)
{
	return particle_place_at(record, particle_dynamic_uint(record.emitter, PARTICLE_DYNAMIC_NOW));
}

/// The particle's quad, width and height, before the emitter's 3D scale: square, or stretched along
/// @p stretch_velocity in stretched mode (the particle's velocity, less a share of the camera's when it draws).
vec2 particle_quad_stretched(particle_record record, particle_state state, vec3 stretch_velocity)
{
	float size = particle_size(record, state);
	uint emitter = record.emitter;
	vec2 quad = vec2(size, size);
	float render_mode = particle_static(emitter, PARTICLE_STATIC_RENDER).w;
	if(abs(render_mode - PARTICLE_RENDER_MODE_STRETCHED) < 0.5)
	{
		vec4 stretch = particle_static(emitter, PARTICLE_STATIC_STRETCH);
		quad.y = size * stretch.x + length(stretch_velocity) * stretch.y;
	}
	else if(abs(render_mode - PARTICLE_RENDER_MODE_TRAIL) < 0.5)
	{
		// The trail reaches back along the path at most the fastest the particle moved over its length (its launch
		// at the speed curve's peak plus what the acceleration added, at the easing's fastest rate: the speed bound slot), plus
		// the turbulence's push on its past points (particle_eval.cpp, evaluate_particle_quad).
		float trail_length = particle_static(emitter, PARTICLE_STATIC_TRAIL).x;
		vec2 speed_bound = particle_static(emitter, PARTICLE_STATIC_SPEED_BOUND).xy;
		vec3 acceleration = particle_acceleration(emitter);
		vec3 launch = record.velocity;
		if((particle_flags(emitter) & PARTICLE_FLAG_LOCAL_SPACE) != 0u)
		{
			launch = particle_local_to_world_vector(emitter, launch);
			acceleration = particle_local_to_world_vector(emitter, acceleration);
		}
		float top_speed = length(launch) * speed_bound.x + length(acceleration) * state.age * speed_bound.y;
		quad.y = size + 2.0 * (top_speed * trail_length + particle_static(emitter, PARTICLE_STATIC_TURBULENCE).w);
	}
	if((particle_flags(emitter) & PARTICLE_FLAG_SIZE_AXIS_CURVE) != 0u)
	{
		quad *= particle_lut(emitter, PARTICLE_LUT_ROW_SHAPE, state.life).xy;
	}
	return quad;
}

/// The particle's quad stretched by its own velocity (particle_eval.cpp, evaluate_particle_quad).
vec2 particle_quad(particle_record record, particle_state state)
{
	return particle_quad_stretched(record, state, state.velocity);
}

/// How much further the particle's stretched quad may reach when the view's camera moves at @p camera_speed (m/s):
/// half the length the camera's motion adds, along its own y axis, pushed by the pivot (particle_eval.cpp,
/// get_camera_stretch_reach).
float particle_camera_stretch_reach(particle_record record, particle_state state, float camera_speed)
{
	uint emitter = record.emitter;
	if(abs(particle_static(emitter, PARTICLE_STATIC_RENDER).w - PARTICLE_RENDER_MODE_STRETCHED) > 0.5)
	{
		return 0.0;
	}
	float axis_y = particle_static(emitter, PARTICLE_STATIC_SIZE_AXES).y;
	if((particle_flags(emitter) & PARTICLE_FLAG_SIZE_AXIS_CURVE) != 0u)
	{
		axis_y *= abs(particle_lut(emitter, PARTICLE_LUT_ROW_SHAPE, state.life).y);
	}
	return 0.5 * camera_speed * particle_static(emitter, PARTICLE_STATIC_FACING).w * particle_static(emitter, PARTICLE_STATIC_STRETCH).y * axis_y *
	       particle_static(emitter, PARTICLE_STATIC_SPIN).z;
}

/// Radius of the sphere around the particle's position its quad stays inside, whatever its spin and
/// camera offset (particle_eval.cpp, get_particle_cull_radius).
float particle_cull_radius(particle_record record, particle_state state)
{
	uint emitter = record.emitter;
	vec2 quad = particle_quad(record, state) * particle_static(emitter, PARTICLE_STATIC_SIZE_AXES).xy;
	// Camera offset and the turbulence's reach (the cull pass evaluates without the turbulence).
	return 0.5 * length(quad) * particle_static(emitter, PARTICLE_STATIC_SPIN).z + particle_static(emitter, PARTICLE_STATIC_VIEW_SHAPING).x + particle_static(emitter, PARTICLE_STATIC_TURBULENCE).w;
}

/// False when the particle's cull sphere lies beyond its emitter's max draw distance from @p eye
/// (particle_eval.cpp, is_within_draw_distance).
bool particle_is_within_draw_distance(particle_record record, particle_state state, float radius, vec3 eye)
{
	if((particle_flags(record.emitter) & PARTICLE_FLAG_DRAW_DISTANCE) == 0u)
	{
		return true;
	}
	return distance(state.position, eye) - radius <= particle_static(record.emitter, PARTICLE_STATIC_DISTANCE).x;
}

/// Alpha erosion threshold at the particle's life (0 = none).
float particle_erosion(particle_record record, particle_state state)
{
	if((particle_flags(record.emitter) & PARTICLE_FLAG_EROSION) == 0u)
	{
		return 0.0;
	}
	return particle_lut(record.emitter, PARTICLE_LUT_ROW_SPEED, state.life).z;
}

#endif // PARTICLE_COMMON_SH_HEADER_GUARD
