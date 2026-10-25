$input a_position, a_color0, a_texcoord0, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord4, a_texcoord5, a_texcoord6
$output v_texcoord0, v_color0, v_document_position, v_edge_distance, v_texture_rect, v_shape_kind

#include "../common.sh"

uniform vec4 u_translate;
uniform mat4 u_transform;
uniform mat4 u_element_transform;
// x, y = the document's size in pixels; z = screen pixels outline sides and edges grow by.
uniform vec4 u_document_edge;

// The distance to a side or an edge the mesh continues across: it never fades.
#define INTERIOR_DISTANCE 1.0e5
// Guards the divisions for a document seen exactly edge-on, and two outline edges that run parallel.
#define MIN_DETERMINANT 1e-8
// How many times its own growth a fringe corner may move at a sharp angle between its outline edges.
#define MAX_MITER_SCALE 4.0
// The shape kind (RmlUi_WorldShapeVertex): a rectangle below TRIANGLE_KIND, then a triangle, a fringe
// from FRINGE_KIND and a strip triangle from STRIP_KIND.
#define TRIANGLE_KIND 0.5
#define FRINGE_KIND 1.5
#define STRIP_KIND 2.5
// Screen pixels a side or an edge reaches under a later shape of the mesh: past the half pixel the later
// shape's fade reaches inside, with a margin for the fragment shader's estimate of the pixel footprint.
#define UNDERLAY_PIXELS 0.75

vec2 toScreenPixels(vec4 clipPos)
{
    return clipPos.xy / clipPos.w * 0.5 * u_viewRect.zw;
}

// Screen pixels per mesh pixel along x (xy) and along y (zw) at meshPos: zero where they are unusable.
vec4 getScreenAxes(vec2 meshPos)
{
    vec4 clipPos = mul(u_transform, vec4(meshPos, 0.0, 1.0));
    if(clipPos.w <= 0.0)
    {
        return vec4_splat(0.0);
    }
    vec2 screenPos = toScreenPixels(clipPos);
    vec2 alongX = toScreenPixels(mul(u_transform, vec4(meshPos + vec2(1.0, 0.0), 0.0, 1.0))) - screenPos;
    vec2 alongY = toScreenPixels(mul(u_transform, vec4(meshPos + vec2(0.0, 1.0), 0.0, 1.0))) - screenPos;
    return vec4(alongX, alongY);
}

// How far (in mesh pixels) a line with the given normal moves to move by one screen pixel: the length of
// the screen gradient of the distance to it.
float getMeshPixelsPerScreenPixel(vec4 screenAxes, vec2 normal)
{
    float determinant = screenAxes.x * screenAxes.w - screenAxes.y * screenAxes.z;
    if(abs(determinant) < MIN_DETERMINANT)
    {
        return 0.0;
    }
    return length(vec2(screenAxes.w * normal.x - screenAxes.y * normal.y, -screenAxes.z * normal.x + screenAxes.x * normal.y)) / abs(determinant);
}

// A rectangle corner moves out across each side that fades (sides = 1) or reaches under a later shape
// (sides = 2, no further than underlayLimits in mesh pixels), and in across each side a later draw covers
// (sides = 3); texture coordinates follow the move. meshRect: the rectangle in mesh pixels;
// texCoordGradient: d(u, v)/dx, d(u, v)/dy. Sets the side distances (left, top, right, bottom), positive
// inside.
vec2 growRectangle(vec2 position, vec4 screenAxes, vec4 meshRect, vec4 sides, vec4 texCoordGradient,
                   vec4 underlayLimits, inout vec2 texCoord, out vec4 edgeDistance)
{
    vec4 rect = meshRect + u_translate.xyxy;
    vec4 pulledSides = step(2.5, sides);
    vec4 underlaySides = step(1.5, sides) - pulledSides;
    vec4 fadingSides = step(0.5, sides) - underlaySides - pulledSides;
    vec2 meshPixels = vec2(getMeshPixelsPerScreenPixel(screenAxes, vec2(1.0, 0.0)), getMeshPixelsPerScreenPixel(screenAxes, vec2(0.0, 1.0)));
    vec4 sideMeshPixels = meshPixels.xyxy;
    vec4 sideMoves = (fadingSides - pulledSides) * u_document_edge.z * sideMeshPixels +
                     underlaySides * min(UNDERLAY_PIXELS * sideMeshPixels, underlayLimits);
    vec2 center = 0.5 * (rect.xy + rect.zw);
    vec2 outward = vec2(position.x < center.x ? -1.0 : 1.0, position.y < center.y ? -1.0 : 1.0);
    vec2 moves = vec2(outward.x < 0.0 ? sideMoves.x : sideMoves.z, outward.y < 0.0 ? sideMoves.y : sideMoves.w);
    vec2 offset = outward * moves;
    position += offset;
    texCoord += texCoordGradient.xy * offset.x + texCoordGradient.zw * offset.y;
    edgeDistance = mix(vec4_splat(INTERIOR_DISTANCE), vec4(position - rect.xy, rect.zw - position), fadingSides);
    return position;
}

// How far (in mesh pixels) a corner moves for both edges through it (inward normals in outlineNormals)
// to move out by screenPixels but no more than maxMeshPixels, limited at sharp angles. Zero normals keep
// the corner in place.
vec2 getCornerOffset(vec4 screenAxes, vec4 outlineNormals, float screenPixels, float maxMeshPixels)
{
    vec2 normalA = outlineNormals.xy;
    vec2 normalB = outlineNormals.zw;
    float growA = min(screenPixels * getMeshPixelsPerScreenPixel(screenAxes, normalA), maxMeshPixels);
    float growB = min(screenPixels * getMeshPixelsPerScreenPixel(screenAxes, normalB), maxMeshPixels);
    float determinant = normalA.x * normalB.y - normalA.y * normalB.x;
    // normalA . offset = -growA and normalB . offset = -growB; along one normal where they run parallel.
    vec2 offset = abs(determinant) > MIN_DETERMINANT
                      ? vec2(normalA.y * growB - normalB.y * growA, normalB.x * growA - normalA.x * growB) / determinant
                      : -normalA * growA;
    float limit = MAX_MITER_SCALE * max(growA, growB);
    return offset * min(1.0, limit / max(length(offset), MIN_DETERMINANT));
}

// The distance from a position to an edge line (inward normal, offset), positive inside. The lines are
// in mesh pixels before the translation.
float getEdgeDistance(vec4 edge, vec2 position)
{
    return dot(edge.xy, position - u_translate.xy) + edge.z;
}

// The distances to the edges a triangle fades across (xyz, positive inside), which fade it on the inside.
vec4 getTriangleDistances(vec2 position, vec4 edge0, vec4 edge1, vec4 edge2)
{
    vec3 distances = vec3(getEdgeDistance(edge0, position), getEdgeDistance(edge1, position), getEdgeDistance(edge2, position));
    vec3 isOutline = vec3(edge0.w, edge1.w, edge2.w);
    return vec4(mix(vec3_splat(INTERIOR_DISTANCE), distances, isOutline), INTERIOR_DISTANCE);
}

// A triangle keeps its corners, so the mesh still tiles exactly, except where a later draw covers its
// edges: a corner on them pulls in by u_document_edge.z screen pixels (insetNormals, the same in every
// triangle that shares the corner), so the shape stays hidden under the later draw's fading edge.
vec2 pullInTriangle(vec2 position, vec4 screenAxes, vec4 insetNormals)
{
    return position - getCornerOffset(screenAxes, insetNormals, u_document_edge.z, INTERIOR_DISTANCE);
}

// A fringe lies across an edge of a triangle: its outer corners move out by the normals of the edges of
// its kind there, the same in the fringes on either side, so neighbouring fringes still tile. A fringe
// that fades reaches u_document_edge.z screen pixels out and sets the distance to the edge (x, negative
// outside) and to the other long edge of a strip it belongs to (y, oppositeEdge); one that reaches under
// a later shape (underlay.x = 1) goes UNDERLAY_PIXELS, no further than underlay.y mesh pixels, and never
// fades. texCoordGradient: d(u, v)/dx, d(u, v)/dy of the triangle.
vec2 growFringe(vec2 position, vec4 screenAxes, vec4 edge, vec4 oppositeEdge, vec4 texCoordGradient,
                vec4 outlineNormals, vec2 underlay, inout vec2 texCoord, out vec4 edgeDistance)
{
    float isUnderlay = underlay.x;
    vec2 offset = getCornerOffset(screenAxes,
                                  outlineNormals,
                                  mix(u_document_edge.z, UNDERLAY_PIXELS, isUnderlay),
                                  mix(INTERIOR_DISTANCE, underlay.y, isUnderlay));
    position += offset;
    texCoord += texCoordGradient.xy * offset.x + texCoordGradient.zw * offset.y;
    float distance = mix(getEdgeDistance(edge, position), INTERIOR_DISTANCE, isUnderlay);
    float oppositeDistance = mix(INTERIOR_DISTANCE, getEdgeDistance(oppositeEdge, position), oppositeEdge.w * (1.0 - isUnderlay));
    edgeDistance = vec4(distance, oppositeDistance, INTERIOR_DISTANCE, INTERIOR_DISTANCE);
    return position;
}

void main()
{
    // The sides and edges a shape (RmlUi_WorldShapeVertex) fades across reach out by u_document_edge.z
    // screen pixels, so even a shape thinner than a pixel is drawn and the pixels across them are shaded;
    // the fragment shader fades them by their coverage. A rectangle moves its own corners out, a triangle
    // leaves that to the fringes along its edges.
    vec2 position = a_position.xy + u_translate.xy;
    vec4 screenAxes = getScreenAxes(position);
    vec2 texCoord = a_texcoord0;
    vec4 edgeDistance;
    if(a_texcoord6.x < TRIANGLE_KIND)
    {
        position = growRectangle(position, screenAxes, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord5, texCoord, edgeDistance);
    }
    else if(a_texcoord6.x < FRINGE_KIND || a_texcoord6.x > STRIP_KIND)
    {
        position = pullInTriangle(position, screenAxes, a_texcoord5);
        edgeDistance = getTriangleDistances(position, a_texcoord1, a_texcoord2, a_texcoord3);
    }
    else
    {
        position = growFringe(position, screenAxes, a_texcoord1, a_texcoord2, a_texcoord3, a_texcoord5, a_texcoord6.yz, texCoord, edgeDistance);
    }

    v_texcoord0 = texCoord;
    v_color0 = a_color0;
    v_edge_distance = edgeDistance;
    v_texture_rect = a_texcoord4;
    v_shape_kind = a_texcoord6.x;

    vec4 meshPos = vec4(position, 0.0, 1.0);
    gl_Position = mul(u_transform, meshPos);
    // Where the vertex lands in the document after its element's transform: the clip rectangle is
    // in these pixels.
    v_document_position = mul(u_element_transform, meshPos);
}
