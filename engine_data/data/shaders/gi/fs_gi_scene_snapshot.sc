$input v_texcoord0

/*
 * SCENE HISTORY SNAPSHOT: last frame's composited, scene-referred HDR color for the feedback
 * readers (the GI gather's screen tier and far field, SSR), with each pixel's VIEW DEPTH in
 * alpha.
 *
 * The readers reproject a world position into this image and take the color they find, which
 * is only that surface's color while the pixel still shows that surface. A blit could not say;
 * this pass stores the depth the reprojection must agree with - the clip w the readers compute
 * from the same (TAA-unjittered) view-projection next frame - so a disoccluded reprojection is
 * declined instead of read (GiReadHistory). A depth-history sampler was not an option: the
 * gather's trace already binds every stage it has.
 *
 * Stored as view depth rather than device depth: the alpha is a 16-bit float, whose relative
 * precision is uniform across the range, while device depth crowds toward 1 and would lose
 * every distant surface to quantisation.
 */

#include "../common.sh"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_depth, 1);

void main()
{
	vec3 scene = texture2DLod(s_scene, v_texcoord0, 0.0).xyz;
	float depth = texture2DLod(s_depth, v_texcoord0, 0.0).x;
	vec3 clip = clipTransform(vec3(v_texcoord0 * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	float view_depth = mul(u_viewProj, vec4(world_position, 1.0)).w;
	gl_FragColor = vec4(scene, view_depth);
}
