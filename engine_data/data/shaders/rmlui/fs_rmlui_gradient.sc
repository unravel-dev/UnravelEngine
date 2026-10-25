$input v_texcoord0, v_color0, v_document_position

#include "../common.sh"
#include "clip.sh"
#include "gradient.sh"

void main()
{
    gl_FragColor = v_color0 * getGradientColor(v_texcoord0) * getClipCoverage(v_document_position);
}
