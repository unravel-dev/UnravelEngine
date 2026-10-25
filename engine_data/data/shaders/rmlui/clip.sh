#ifndef RMLUI_CLIP_SH
#define RMLUI_CLIP_SH

// The region of the document a draw keeps, in document pixels: left, top, right, bottom. A draw into
// a layer of its own clips with the hardware scissor and gets a region that keeps everything; a draw
// straight into a 3D view has no screen-space rectangle to scissor with and clips here.
uniform vec4 u_clip_rect;

// Guards the division where the document position does not change across the pixel.
#define MIN_DOCUMENT_FOOTPRINT 1e-6

// The share of this pixel inside the clip region: 1 inside, 0 outside, with a one-pixel ramp across
// the border so a clip edge seen at an angle does not stair-step.
float getClipCoverage(vec4 documentPosition)
{
    vec2 position = documentPosition.xy / documentPosition.w;
    vec2 footprint = vec2(length(vec2(dFdx(position.x), dFdy(position.x))), length(vec2(dFdx(position.y), dFdy(position.y))));
    vec2 inside = min(position - u_clip_rect.xy, u_clip_rect.zw - position) / max(footprint, vec2_splat(MIN_DOCUMENT_FOOTPRINT));
    return saturate(min(inside.x, inside.y) + 0.5);
}

#endif // RMLUI_CLIP_SH
