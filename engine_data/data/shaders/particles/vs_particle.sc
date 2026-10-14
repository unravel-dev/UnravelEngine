$input a_position, a_texcoord0
$output v_texcoord0, v_texcoord1, v_color0, v_particle

/*
 * The particle quads of the color passes (particle_vertex.sh).
 */

#include <bgfx_shader.sh>
#include <bgfx_compute.sh>

BUFFER_RO(s_particle_records, uint, 0);
BUFFER_RO(s_particle_static, vec4, 1);
BUFFER_RO(s_particle_dynamic, uint, 2);
BUFFER_RO(s_particle_luts, vec4, 3);
BUFFER_RO(s_particle_sorted, uint, 4);

#include "particle_common.sh"
#include "particle_vertex.sh"

void main()
{
	particle_vertex vertex = particle_build_vertex(a_position, a_texcoord0, uint(gl_InstanceID));
	gl_Position = vertex.clip;
	// z: the emitter's TAA response, the scale of its reactive coverage.
	v_texcoord0 = vec3(vertex.texcoord0, particle_static(vertex.emitter, PARTICLE_STATIC_STRETCH).z);
	v_texcoord1 = vertex.texcoord1;
	v_particle = vertex.particle;
	v_color0 = vertex.color;
}
