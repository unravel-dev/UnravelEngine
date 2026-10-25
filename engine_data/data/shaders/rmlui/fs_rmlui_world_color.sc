$input v_texcoord0, v_color0, v_document_position, v_edge_distance, v_texture_rect, v_shape_kind

#include "../common.sh"
#include "clip.sh"
#include "edge.sh"

void main()
{
    gl_FragColor = v_color0 * getShapeCoverage(v_edge_distance, v_shape_kind) * getClipCoverage(v_document_position);
}
