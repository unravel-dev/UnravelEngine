$output v_color0

/*
 * UE 5.8 r.Lumen.ScreenProbeGather.Debug.ProbePlacement (LumenScreenProbeDebug.usf ScreenProbeGatherDebugCS) as its
 * ShaderPrint lines, four per probe atlas tile: modes 1 and 2 outline a square two pixels across at the probe's pixel
 * (DrawPoint: AddQuadSS), in front of everything; mode 3 draws a LUMEN_PLACEMENT_CROSS_SIZE cross at the probe and a
 * LUMEN_PLACEMENT_NORMAL_LENGTH line along the normal, an adaptive probe along its uniform tile probe's normal as UE
 * reads it. Uniform probes yellow (not in mode 2), adaptive ones magenta. UE's text is not drawn.
 */

#include "../common.sh"
#include "lumen/lumen_common.sh"

SAMPLER2D(s_lumen_probe_records, 0);

/// x = the mode (1: every probe, 2: the adaptive probes, 3: every probe's cross and normal).
uniform vec4 u_lumen_visualize_placement;

#define LUMEN_PLACEMENT_VERTICES_PER_PROBE 8
#define LUMEN_PLACEMENT_MODE_ADAPTIVE 2
#define LUMEN_PLACEMENT_MODE_NORMALS 3
/// UE AddCrossTWS(Position, 2) and the 10 cm normal line.
#define LUMEN_PLACEMENT_CROSS_SIZE 0.02
#define LUMEN_PLACEMENT_NORMAL_LENGTH 0.1

/// Corner @p corner (0-3, around the square) of the square UE's DrawPoint outlines at @p pixel, in pixels.
vec2 LumenPlacementSquareCorner(vec2 pixel, int corner)
{
	return pixel + vec2(corner == 1 || corner == 2 ? 1.0 : -1.0, corner >= 2 ? 1.0 : -1.0);
}

void main()
{
	int probe = gl_VertexID / LUMEN_PLACEMENT_VERTICES_PER_PROBE;
	int local = gl_VertexID - probe * LUMEN_PLACEMENT_VERTICES_PER_PROBE;
	int segment = local / 2;
	int end = local - segment * 2;
	int row = probe / u_lumen_probe_count.x;
	ivec2 tile = ivec2(probe - row * u_lumen_probe_count.x, row);
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	/// A uniform probe: the atlas rows above the adaptive ones (shaderc's SPIR-V path reads any name holding "uniform" as
	/// a declaration).
	bool is_lattice_probe = tile.y < u_lumen_probe_count.y;
	int mode = int(u_lumen_visualize_placement.x + 0.5);
	bool is_drawn = record.x > 0.0 && (!is_lattice_probe || mode != LUMEN_PLACEMENT_MODE_ADAPTIVE);
	// Outside the clip volume unless drawn.
	vec4 position = vec4(2.0, 2.0, 2.0, 1.0);
	if(is_drawn && mode == LUMEN_PLACEMENT_MODE_NORMALS)
	{
		vec3 world = LumenProbePosition(record);
		vec4 normal_record = is_lattice_probe ? record : texelFetch(s_lumen_probe_records, LumenProbeScreenTile(tile, record), 0);
		vec3 axis = vec3(segment == 0 ? 1.0 : 0.0, segment == 1 ? 1.0 : 0.0, segment == 2 ? 1.0 : 0.0);
		vec3 start = segment < 3 ? world - axis * LUMEN_PLACEMENT_CROSS_SIZE : world;
		vec3 finish = segment < 3 ? world + axis * LUMEN_PLACEMENT_CROSS_SIZE
		                       : world + LumenProbeNormal(normal_record) * LUMEN_PLACEMENT_NORMAL_LENGTH;
		position = mul(u_viewProj, vec4(end == 0 ? start : finish, 1.0));
	}
	else if(is_drawn)
	{
		int corner = segment + end;
		vec2 pixel = LumenPlacementSquareCorner(vec2(LumenProbeRecordPixel(record)), corner == 4 ? 0 : corner);
		vec2 ndc = vec2(pixel.x * u_lumen_view_texel.x * 2.0 - 1.0, 1.0 - pixel.y * u_lumen_view_texel.y * 2.0);
		// At the near plane: screen-space lines are never behind the scene.
		position = vec4(ndc, toClipSpaceDepth(0.0), 1.0);
	}
	gl_Position = position;
	v_color0 = is_lattice_probe ? vec4(1.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 1.0, 1.0);
}
