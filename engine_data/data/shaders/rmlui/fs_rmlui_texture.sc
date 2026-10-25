$input v_texcoord0, v_color0, v_document_position

#include "../common.sh"
#include "clip.sh"
#include "texture_footprint.sh"

// The texture coordinate bounds of the draw's geometry: min u, min v, max u, max v.
uniform vec4 u_texture_rect;

void main()
{
    // Gradients and implicit-gradient samples stay out of flow control.
    vec2 stepX = dFdx(v_texcoord0);
    vec2 stepY = dFdy(v_texcoord0);
    vec4 texColor = premultiply(texture2D(s_tex, v_texcoord0));
    if(u_texture_size.z > 0.5)
    {
        texColor = sampleFootprint(v_texcoord0, stepX, stepY, u_texture_rect, true);
    }
    gl_FragColor = v_color0 * texColor * getClipCoverage(v_document_position);
}
