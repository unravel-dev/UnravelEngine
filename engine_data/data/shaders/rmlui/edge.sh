#ifndef RMLUI_EDGE_SH
#define RMLUI_EDGE_SH

// Guards the division where an edge distance does not change across the pixel.
#define MIN_EDGE_FOOTPRINT 1e-6
// v_shape_kind below TRIANGLE_SHAPE_KIND is a rectangle, then a triangle, and from FRINGE_SHAPE_KIND on a
// fringe or a strip triangle.
#define TRIANGLE_SHAPE_KIND 0.5
#define FRINGE_SHAPE_KIND 1.5

// The share of this pixel inside its shape, each outline side or edge ramping across one screen pixel.
// edgeDistance holds the distances to the shape's sides (positive inside, any unit):
// - a rectangle (shapeKind 0): left, top, right, bottom; opposite sides combine per axis, so a rectangle
//   thinner than a pixel covers as little as it should;
// - a triangle (shapeKind 1): its three edges, the fourth unused;
// - a fringe (shapeKind 2): the edge it lies across and the other long edge of a strip it belongs to;
// - a strip triangle (shapeKind 3): the strip's two long edges, which combine the way a rectangle's
//   opposite sides do, then its edge across the strip.
// A side the mesh continues across is far away and never fades.
float getShapeCoverage(vec4 edgeDistance, float shapeKind)
{
    vec4 footprint = vec4(length(vec2(dFdx(edgeDistance.x), dFdy(edgeDistance.x))),
                          length(vec2(dFdx(edgeDistance.y), dFdy(edgeDistance.y))),
                          length(vec2(dFdx(edgeDistance.z), dFdy(edgeDistance.z))),
                          length(vec2(dFdx(edgeDistance.w), dFdy(edgeDistance.w))));
    vec4 sideCoverage = saturate(edgeDistance / max(footprint, vec4_splat(MIN_EDGE_FOOTPRINT)) + 0.5);
    float rectangleCoverage = saturate(sideCoverage.x + sideCoverage.z - 1.0) * saturate(sideCoverage.y + sideCoverage.w - 1.0);
    float triangleCoverage = sideCoverage.x * sideCoverage.y * sideCoverage.z;
    float stripCoverage = saturate(sideCoverage.x + sideCoverage.y - 1.0) * sideCoverage.z;
    return shapeKind < TRIANGLE_SHAPE_KIND ? rectangleCoverage : (shapeKind < FRINGE_SHAPE_KIND ? triangleCoverage : stripCoverage);
}

#endif // RMLUI_EDGE_SH
