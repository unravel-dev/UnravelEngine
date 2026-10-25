$input a_position, a_color0, a_texcoord0
$output v_texcoord0, v_color0, v_document_position

#include "../common.sh"

uniform vec4 u_translate;
uniform mat4 u_transform;
uniform mat4 u_element_transform;
// x, y = the document's size in pixels; z = screen pixels its outline grows by, 0 to draw it as it is.
uniform vec4 u_document_edge;

// Document pixels closer than this to the outline count as on it.
#define OUTLINE_EPSILON 0.01
// Guards the division for a document seen exactly edge-on.
#define MIN_SCREEN_SPAN 1e-4

vec2 toScreenPixels(vec4 clipPos)
{
    return clipPos.xy / clipPos.w * 0.5 * u_viewRect.zw;
}

// A document drawn straight into a 3D view has a single sample per pixel: a vertex on its outline
// moves outward by u_document_edge.z screen pixels, so the pixels across the outline are drawn and the
// clip coverage fades them. Its texture coordinates stay, stretching the outermost texels.
vec2 growOutline(vec2 position, vec2 documentPosition)
{
    vec2 outward = vec2(documentPosition.x <= OUTLINE_EPSILON ? -1.0 : (documentPosition.x >= u_document_edge.x - OUTLINE_EPSILON ? 1.0 : 0.0),
                        documentPosition.y <= OUTLINE_EPSILON ? -1.0 : (documentPosition.y >= u_document_edge.y - OUTLINE_EPSILON ? 1.0 : 0.0));
    vec4 clipPos = mul(u_transform, vec4(position, 0.0, 1.0));
    if(u_document_edge.z <= 0.0 || (outward.x == 0.0 && outward.y == 0.0) || clipPos.w <= 0.0)
    {
        return position;
    }
    vec2 screenPos = toScreenPixels(clipPos);
    vec2 spanX = toScreenPixels(mul(u_transform, vec4(position + vec2(1.0, 0.0), 0.0, 1.0))) - screenPos;
    vec2 spanY = toScreenPixels(mul(u_transform, vec4(position + vec2(0.0, 1.0), 0.0, 1.0))) - screenPos;
    vec2 screenPixelsPerDocumentPixel = max(vec2(length(spanX), length(spanY)), vec2_splat(MIN_SCREEN_SPAN));
    return position + outward * u_document_edge.z / screenPixelsPerDocumentPixel;
}

void main()
{
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;

    vec2 translatedPos = a_position.xy + u_translate.xy;
    vec4 documentPos = mul(u_element_transform, vec4(translatedPos, 0.0, 1.0));
    translatedPos = growOutline(translatedPos, documentPos.xy / documentPos.w);
    vec4 outPos = mul(u_transform, vec4(translatedPos, 0.0, 1.0));
    // Where the vertex lands in the document after its element's transform: the clip rectangle is
    // in these pixels.
    v_document_position = mul(u_element_transform, vec4(translatedPos, 0.0, 1.0));

    gl_Position = outPos;
}
