$output v_color0

/*
 * The world-space lines UE's Lumen visualizations draw with ShaderPrint (AddLineWS), two vertices per line of a buffer
 * in lumen_visualize.sh's layout: the start, the vector to the end and a colour. The colour is radiance tone mapped
 * as the image is (UE VisualizeTonemap: r.Lumen.ScreenProbeGather.VisualizeTraces) or a colour taken as it is (the
 * line colours of r.Lumen.Reflections.VisualizeTraces). A zero-length line draws nothing.
 */

#include "../common.sh"
#include "../tonemapping/tonemapping.sh"
#include "lumen/lumen_visualize.sh"
#include <bgfx_compute.sh>

/// The lines (cs_lumen_probe_trace_visualize.sc, cs_lumen_visualize_reflection_trace.sc).
BUFFER_RO(b_lumen_visualize_lines, vec4, 0);

/// x = the image's tone mapping operator, y > 0.5 when the colours are taken as they are.
uniform vec4 u_lumen_visualize_lines;

void main()
{
	int segment = gl_VertexID / 2;
	int base = segment * LUMEN_VISUALIZE_TRACE_STRIDE;
	vec3 start = b_lumen_visualize_lines[base].xyz;
	vec3 delta = b_lumen_visualize_lines[base + 1].xyz;
	vec3 position = gl_VertexID - segment * 2 == 1 ? start + delta : start;
	// Outside the clip volume when the line has no length.
	gl_Position = dot(delta, delta) > 0.0 ? mul(u_viewProj, vec4(position, 1.0)) : vec4(2.0, 2.0, 2.0, 1.0);
	vec3 color = b_lumen_visualize_lines[base + 2].xyz;
	v_color0 = vec4(u_lumen_visualize_lines.y > 0.5 ? color
	                                                : apply_tonemapping(color, int(u_lumen_visualize_lines.x), 1.0),
	                1.0);
}
