$input v_near_point
$input v_far_point

#include <bgfx_shader.sh>

uniform vec4 u_params;

#define u_grid_height   u_params.x
#define u_camera_near   u_params.y
#define u_camera_far    u_params.z
#define u_grid_opacity  u_params.w

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

float compute_ndc_depth(vec3 position, in mat4 viewProj)
{
	vec4 clip_pos = mul(viewProj, vec4(position.xyz, 1.0));
	float ndc_depth = clip_pos.z / clip_pos.w;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_SPIRV
	return ndc_depth;
#else
	return (ndc_depth + 1.0) * 0.5;
#endif
}

float compute_linear_depth(vec3 position, in mat4 viewProj)
{
	float near_val = u_camera_near;
	float far_val = u_camera_far;
	vec4 clip_pos = mul(viewProj, vec4(position.xyz, 1.0));
	float ndc_z = (clip_pos.z / clip_pos.w) * 2.0 - 1.0;
	float linear_z = (2.0 * near_val * far_val) / (far_val + near_val - ndc_z * (far_val - near_val));
	return linear_z / far_val;
}

void main()
{
	float denom = v_far_point.y - v_near_point.y;
	float t = (u_grid_height - v_near_point.y) / denom;
	vec3 frag_pos = v_near_point + t * (v_far_point - v_near_point);

	gl_FragDepth = compute_ndc_depth(frag_pos, u_viewProj) - 0.00008;

	float linear_depth = compute_linear_depth(frag_pos, u_viewProj);

	// Smooth distance fade (smoothstep avoids the hard cutoff that caused visible edge)
	float fading = 1.0 - smoothstep(0.1, 0.5, linear_depth);

	// Grazing angle fade: when camera looks nearly parallel to the grid,
	// the vertical component of the ray approaches zero, producing extreme
	// intersection distances that cause flickering and bright bands.
	float grazing = abs(denom) / length(v_far_point - v_near_point);
	fading *= smoothstep(0.0, 0.05, grazing);

	// Branchless zero-out for behind-camera fragments
	fading *= step(0.0, t);

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
