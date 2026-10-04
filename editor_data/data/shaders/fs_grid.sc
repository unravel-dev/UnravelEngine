$input v_near_point
$input v_far_point

#include "common.sh"

SAMPLER2D(s_depth, 0);

uniform vec4 u_params;
uniform vec4 u_depth_size;
uniform mat4 u_depth_view_proj;

#define u_grid_height   u_params.x
#define u_depth_test    u_params.y
#define u_camera_far    u_params.z
#define u_grid_opacity  u_params.w

// Depth margin the grid wins by over a coplanar surface, shaped like a hardware polygon
// offset: eight float32 ulps of a depth in [0.5, 1) for rounding, plus an eighth of a pixel
// of the grid's own depth slope, because the rasterizer places the scene's plane a few
// hundredths of a pixel off the analytic one. A constant offset in post-projection depth
// would instead grow with the square of the view distance.
#define GRID_DEPTH_ULP (1.0 / 16777216.0)
#define GRID_DEPTH_MARGIN_ULPS 8.0
#define GRID_DEPTH_MARGIN_PIXELS 0.125

vec4 grid(vec3 frag_pos, float scale, float grid_alpha, float axis_alpha)
{
	vec2 coord = frag_pos.xz / scale;
	vec2 derivative = fwidth(coord);

	vec2 grid_aa = abs(fract(coord - vec2_splat(0.5)) - vec2_splat(0.5)) / derivative;
	float ln = min(grid_aa.x, grid_aa.y);
	float line_mask = smoothstep(1.0, 0.0, ln);

	float min_dz = min(derivative.y, 1.0);
	float min_dx = min(derivative.x, 1.0);
	float axis_width = 1.5 * scale;

	float z_near_zero = 1.0 - smoothstep(0.0, axis_width * min_dz, abs(frag_pos.z));
	float x_near_zero = 1.0 - smoothstep(0.0, axis_width * min_dx, abs(frag_pos.x));

	vec4 color = vec4(1.0, 1.0, 1.0, grid_alpha * line_mask);

	// X axis (red, along z=0)
	float x_axis_str = z_near_zero * axis_alpha;
	color.rgb = mix(color.rgb, vec3(1.0, 0.15, 0.15), x_axis_str);
	color.a = max(color.a, x_axis_str);

	// Z axis (green, along x=0)
	float z_axis_str = x_near_zero * axis_alpha;
	color.rgb = mix(color.rgb, vec3(0.15, 1.0, 0.15), z_axis_str);
	color.a = max(color.a, z_axis_str);

	return color;
}

// Bilinear depth over the four nearest texels: the depth of a plane is affine in screen
// space, so this reconstructs it exactly. At the border the footprint stays inside the
// texture and the weights extrapolate.
float sample_scene_depth(vec2 uv)
{
	vec2 texel_pos = uv * u_depth_size.xy - vec2_splat(0.5);
	vec2 base = clamp(floor(texel_pos), vec2_splat(0.0), u_depth_size.xy - vec2_splat(2.0));
	vec2 weight = texel_pos - base;
	ivec2 p00 = ivec2(base);
	ivec2 p11 = p00 + ivec2(1, 1);
	float d00 = texelFetch(s_depth, p00, 0).r;
	float d10 = texelFetch(s_depth, ivec2(p11.x, p00.y), 0).r;
	float d01 = texelFetch(s_depth, ivec2(p00.x, p11.y), 0).r;
	float d11 = texelFetch(s_depth, p11, 0).r;
	return mix(mix(d00, d10, weight.x), mix(d01, d11, weight.x), weight.y);
}

// 1 where the scene depth does not occlude the grid point. The grid is drawn with the
// unjittered camera so its lines hold still under TAA; the scene depth was rasterized with
// the jittered one, so the point is located there through the jittered view-projection.
float compute_depth_visibility(vec3 frag_pos)
{
	vec4 clip_pos = mul(u_depth_view_proj, vec4(frag_pos, 1.0));
	vec3 ndc = clip_pos.xyz / clip_pos.w;
	vec2 uv = clipToUv(ndc.xy * 0.5 + vec2_splat(0.5));
	float grid_depth = toDepthTextureZ(ndc.z);
	float scene_depth = sample_scene_depth(uv);
	float depth_slope = max(abs(dFdx(grid_depth)), abs(dFdy(grid_depth)));
	float margin = GRID_DEPTH_MARGIN_ULPS * GRID_DEPTH_ULP + GRID_DEPTH_MARGIN_PIXELS * depth_slope;
	return step(grid_depth - margin, scene_depth);
}

void main()
{
	float denom = v_far_point.y - v_near_point.y;
	float t = (u_grid_height - v_near_point.y) / denom;
	vec3 frag_pos = v_near_point + t * (v_far_point - v_near_point);

	float view_depth = abs(mul(u_view, vec4(frag_pos, 1.0)).z);

	// Smooth distance fade (smoothstep avoids the hard cutoff that caused visible edge)
	float fading = 1.0 - smoothstep(0.1, 0.5, view_depth / u_camera_far);

	// Grazing angle fade: when camera looks nearly parallel to the grid,
	// the vertical component of the ray approaches zero, producing extreme
	// intersection distances that cause flickering and bright bands.
	float grazing = abs(denom) / length(v_far_point - v_near_point);
	fading *= smoothstep(0.0, 0.05, grazing);

	// Branchless zero-out for behind-camera fragments
	fading *= step(0.0, t);

	fading *= mix(1.0, compute_depth_visibility(frag_pos), u_depth_test);

	// Base 1-unit grid
	vec4 color = grid(frag_pos, 1.0, 0.3, 1.0);

	// Multi-scale grids (10, 100, 1000 units)
	for (int i = 1; i <= 3; ++i)
	{
		float range = pow(10.0, float(i));
		float dist = length(frag_pos.xz);
		float scale_fade = 1.0 - smoothstep(range * 1.0, range * 25.0, dist);

		vec4 sg = grid(frag_pos, range, 0.5, 1.0);
		float a = sg.a * scale_fade;

		// Over-operator compositing instead of additive blending
		color.rgb = mix(color.rgb, sg.rgb, a);
		color.a = color.a + a * (1.0 - color.a);
	}

	color.a *= fading * u_grid_opacity;
	gl_FragColor = color;
}
