$input v_texcoord0, v_color0, v_document_position

#include "../common.sh"
#include "clip.sh"
#include "creation.sh"

void main()
{
    gl_FragColor = getCreationColor(v_texcoord0, v_color0.a) * getClipCoverage(v_document_position);
}
