$input v_texcoord0, v_color0, v_document_position, v_edge_distance, v_texture_rect, v_shape_kind

#include "../common.sh"
#include "clip.sh"
#include "edge.sh"
#include "texture_footprint.sh"

void main()
{
    // Each shape keeps its taps in its own texture coordinates: its edge coverage, not the texture,
    // fades its outline.
    vec4 texColor = sampleFootprint(v_texcoord0, dFdx(v_texcoord0), dFdy(v_texcoord0), v_texture_rect, false);
    gl_FragColor = v_color0 * texColor * getShapeCoverage(v_edge_distance, v_shape_kind) * getClipCoverage(v_document_position);
}
