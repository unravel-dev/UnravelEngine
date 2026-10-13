$input v_texcoord0

#include "common.sh"

SAMPLER2D(s_tex, 0);

// Guards the division for a quad seen exactly edge-on.
#define MIN_TEXCOORD_FOOTPRINT 1e-6
// Sharpens minified documents: trilinear filtering blurs text more than its screen footprint needs.
#define DOCUMENT_MIP_BIAS -0.5

// Signed screen-pixel distance from this fragment to the nearer quad border along one texcoord
// axis: positive inside the quad, negative in the border the vertex shader grew.
float getBorderDistancePixels(float texcoord)
{
    float footprint = length(vec2(dFdx(texcoord), dFdy(texcoord)));
    return min(texcoord, 1.0 - texcoord) / max(footprint, MIN_TEXCOORD_FOOTPRINT);
}

void main()
{
    // The quad is drawn after anti-aliasing has resolved the scene and has a single sample per
    // pixel, so its silhouette would stair-step: fade each pixel by the share of it the quad covers.
    float coverage = saturate(min(getBorderDistancePixels(v_texcoord0.x), getBorderDistancePixels(v_texcoord0.y)) + 0.5);
    // The document is rendered with premultiplied alpha, so coverage scales every channel.
    vec4 texColor = texture2DBias(s_tex, v_texcoord0, DOCUMENT_MIP_BIAS) * coverage;
    if (texColor.a < 0.01)
        discard;
    gl_FragColor = texColor;
}
