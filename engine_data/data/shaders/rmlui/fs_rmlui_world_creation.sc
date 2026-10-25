$input v_texcoord0, v_color0, v_document_position, v_edge_distance, v_texture_rect, v_shape_kind

#include "../common.sh"
#include "clip.sh"
#include "edge.sh"
#include "creation.sh"

// Taps per screen axis at most when averaging the effect over a pixel's footprint.
#define MAX_CREATION_TAPS 4

// The effect averaged over the pixel's footprint, spanned by the texture coordinate steps to the next
// pixel: one tap per document pixel across it. Seen smaller than its document pixels the effect would
// otherwise sparkle, where a document drawn into a texture is filtered by the texture's mips.
vec4 getFilteredCreationColor(vec2 texCoord, float alpha)
{
    vec2 stepX = dFdx(texCoord);
    vec2 stepY = dFdy(texCoord);
    vec2 pixelsAcross = vec2(length(stepX * u_dimensions.xy), length(stepY * u_dimensions.xy));
    vec2 taps = clamp(ceil(pixelsAcross), vec2_splat(1.0), vec2_splat(float(MAX_CREATION_TAPS)));
    vec4 sum = vec4_splat(0.0);
    for(int y = 0; y < MAX_CREATION_TAPS; ++y)
    {
        if(float(y) >= taps.y)
        {
            break;
        }
        for(int x = 0; x < MAX_CREATION_TAPS; ++x)
        {
            if(float(x) >= taps.x)
            {
                break;
            }
            vec2 tap = texCoord + ((float(x) + 0.5) / taps.x - 0.5) * stepX + ((float(y) + 0.5) / taps.y - 0.5) * stepY;
            sum += getCreationColor(tap, alpha);
        }
    }
    return sum / (taps.x * taps.y);
}

void main()
{
    gl_FragColor = getFilteredCreationColor(v_texcoord0, v_color0.a) * getShapeCoverage(v_edge_distance, v_shape_kind) *
                   getClipCoverage(v_document_position);
}
