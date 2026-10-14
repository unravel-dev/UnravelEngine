$input a_position, a_texcoord0
$output v_texcoord0, v_texcoord1, v_color0, v_particle, v_curr_clip, v_prev_clip, v_prev_static_clip

/*
 * The particle quads of the velocity pass: the color pass's corners (particle_vertex.sh) with their
 * motion since the last frame. The clip positions are interpolated and divided per pixel
 * (fs_particle_velocity.sc), as the scene's velocity pass does.
 */

#include <bgfx_shader.sh>
#include <bgfx_compute.sh>

BUFFER_RO(s_particle_records, uint, 0);
BUFFER_RO(s_particle_static, vec4, 1);
BUFFER_RO(s_particle_dynamic, uint, 2);
BUFFER_RO(s_particle_luts, vec4, 3);
BUFFER_RO(s_particle_sorted, uint, 4);

#include "../shaderlib.sh"
#include "particle_common.sh"
#include "particle_vertex.sh"

// Previous frame's view-projection, jitter removed (as the velocity pass binds it).
uniform mat4 u_prev_view_proj;

void main()
{
	particle_vertex vertex = particle_build_vertex(a_position, a_texcoord0, uint(gl_InstanceID));
	gl_Position = vertex.clip;
	// z: the opacity from which a texel writes its motion.
	v_texcoord0 = vec3(vertex.texcoord0, particle_static(vertex.emitter, PARTICLE_STATIC_STRETCH).w);
	v_texcoord1 = vertex.texcoord1;
	v_particle = vertex.particle;
	v_color0 = vertex.color;
	// Last frame's corner: the particle's analytic motion over the emitter's frame time, plus a local-space
	// emitter's own move. The quad keeps its current shape and facing.
	float frame_time = particle_dynamic_float(vertex.emitter, PARTICLE_DYNAMIC_FRAME_TIME);
	vec3 origin_motion = vec3(particle_dynamic_float(vertex.emitter, PARTICLE_DYNAMIC_ORIGIN_MOTION),
	                          particle_dynamic_float(vertex.emitter, PARTICLE_DYNAMIC_ORIGIN_MOTION + 1u),
	                          particle_dynamic_float(vertex.emitter, PARTICLE_DYNAMIC_ORIGIN_MOTION + 2u));
	vec3 previous_world = vertex.world_position - vertex.velocity * frame_time - origin_motion;
	v_curr_clip = vertex.clip;
	v_prev_clip = mul(u_prev_view_proj, vec4(previous_world, 1.0));
	// The current corner through last frame's camera: the camera-only part of the motion.
	v_prev_static_clip = mul(u_prev_view_proj, vec4(vertex.world_position, 1.0));
}
