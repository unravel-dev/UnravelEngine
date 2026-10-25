#ifndef RMLUI_TEXTURE_FOOTPRINT_SH
#define RMLUI_TEXTURE_FOOTPRINT_SH

SAMPLER2D(s_tex, 0);

uniform vec4 u_tex_requires_premultiplication;
// x, y = the bound texture's size in texels; z = 1 when the draw filters its minified footprint itself.
uniform vec4 u_texture_size;

// Taps per screen axis at most when filtering a footprint; a footprint wider than that many texels is
// read from a coarser mip.
#define MAX_FOOTPRINT_TAPS 4

vec4 premultiply(vec4 texColor)
{
    if(u_tex_requires_premultiplication.x > 0.5)
    {
        texColor.rgb *= texColor.a;
    }
    return texColor;
}

// A document drawn straight into a 3D view is often seen far smaller than its texels, where the
// texture's own mips would blend in the neighbours of a sprite in an atlas. Averages taps across the
// pixel's footprint (spanned by the texture coordinate steps to the next pixel) at full resolution,
// each kept inside the texture coordinates of rect (min u, min v, max u, max v).
// featherEdges: up to one texel per pixel a tap may then blend half a texel past the bounds, as plain
// bilinear filtering does, which feathers an edge that has no coverage of its own; the taps move half a
// texel inside by two texels per pixel, where a neighbour in an atlas would start to show. Without it
// the geometry fades its own edges: taps keep half a texel inside, and only the taps that land inside
// the bounds count, since the pixel's coverage already weighs what lies outside.
vec4 sampleFootprint(vec2 texCoord, vec2 stepX, vec2 stepY, vec4 rect, bool featherEdges)
{
    vec2 texelsAcross = vec2(length(stepX * u_texture_size.xy), length(stepY * u_texture_size.xy));
    vec2 taps = clamp(ceil(texelsAcross), vec2_splat(1.0), vec2_splat(float(MAX_FOOTPRINT_TAPS)));
    // A texel of the level read may span at most half the bounds, so even a tap at their centre reads
    // nothing outside them: a thin piece of an atlas sprite is filtered no coarser than it is wide.
    vec2 rectTexels = (rect.zw - rect.xy) * u_texture_size.xy;
    float maxLod = max(floor(log2(max(min(rectTexels.x, rectTexels.y), 1.0) * 0.5)), 0.0);
    float lod = min(max(log2(max(texelsAcross.x / taps.x, texelsAcross.y / taps.y)), 0.0), maxLod);
    float insetWeight = featherEdges ? saturate(max(texelsAcross.x, texelsAcross.y) - 1.0) : 1.0;
    vec2 inset = insetWeight * min(0.5 * exp2(lod) / u_texture_size.xy, 0.5 * (rect.zw - rect.xy));
    vec2 low = rect.xy + inset;
    vec2 high = rect.zw - inset;
    vec4 sum = vec4_splat(0.0);
    float weight = 0.0;
    for(int y = 0; y < MAX_FOOTPRINT_TAPS; ++y)
    {
        if(float(y) >= taps.y)
        {
            break;
        }
        for(int x = 0; x < MAX_FOOTPRINT_TAPS; ++x)
        {
            if(float(x) >= taps.x)
            {
                break;
            }
            vec2 tap = texCoord + ((float(x) + 0.5) / taps.x - 0.5) * stepX + ((float(y) + 0.5) / taps.y - 0.5) * stepY;
            vec2 isInside = step(rect.xy, tap) * step(tap, rect.zw);
            float tapWeight = featherEdges ? 1.0 : isInside.x * isInside.y;
            sum += tapWeight * premultiply(texture2DLod(s_tex, clamp(tap, low, high), lod));
            weight += tapWeight;
        }
    }
    // A pixel whose footprint misses the bounds entirely reads the nearest texel inside them.
    return weight > 0.0 ? sum / weight : premultiply(texture2DLod(s_tex, clamp(texCoord, low, high), lod));
}

#endif // RMLUI_TEXTURE_FOOTPRINT_SH
