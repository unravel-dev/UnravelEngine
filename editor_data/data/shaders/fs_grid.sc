$input v_near_point
$input v_far_point

#include <bgfx_shader.sh>

uniform vec4 u_params;

#define u_grid_height   u_params.x
#define u_camera_near   u_params.y
#define u_camera_far    u_params.z
#define u_grid_opacity  u_params.w

vec4 grid (vec3 frag_position_3d, float scale, float thickness, float grid_alpha, float axis_alpha)
{
	// dont want the grid to be infinite?
	// 	uncomment this bit, set your boundaries to whatever you want
	//if (frag_position_3d.x > 10.0f
	//	|| frag_position_3d.x < -10.0f
	//	|| frag_position_3d.z > 10.0f
	//	|| frag_position_3d.z < -10.0f)
	//{
	//	return vec4 (0.0f, 0.0f, 0.0f, 0.0f);
	//}

	vec2 coord = frag_position_3d.xz / scale;
	vec2 derivative = fwidth(coord);
    
    // Better anti-aliased grid calculation
    vec2 grid = abs(fract(coord - vec2_splat(0.5)) - vec2_splat(0.5)) / derivative;
    float ln = min(grid.x, grid.y);
    
    // Smooth the grid lines with proper thickness
    float grid_opacity = 1.0 - min(ln, 1.0);
    grid_opacity = smoothstep(0.0, thickness, grid_opacity);
    
	float minimum_z = min(derivative.y, 1.0f);
	float minimum_x = min(derivative.x, 1.0f);
    
    float axisLineThreshold = thickness * scale * 1.5; // Made thicker

	// Smooth axis line transitions with thicker lines
    float x_axis_factor = 1.0 - smoothstep(0.0, axisLineThreshold * minimum_x, abs(frag_position_3d.x));
    float z_axis_factor = 1.0 - smoothstep(0.0, axisLineThreshold * minimum_z, abs(frag_position_3d.z));
    
    // Start with grid color
	vec4 color = vec4(1.0f, 1.0f, 1.0f, grid_alpha * grid_opacity);
    
	// x axis color (bright red) - when z is close to 0
    vec3 x_axis_color = vec3(1.0f, 0.1f, 0.1f);
    float x_axis_strength = z_axis_factor * axis_alpha;

	// z axis color (bright green) - when x is close to 0
    vec3 z_axis_color = vec3(0.1f, 1.0f, 0.1f);
    float z_axis_strength = x_axis_factor * axis_alpha;
    
    // Apply axis colors with strong dominance
    if (x_axis_strength > 0.1) 
	{
        color.rgb = x_axis_color;
        color.a = max(color.a, x_axis_strength);
    }
    if (z_axis_strength > 0.1) 
	{
        color.rgb = z_axis_color;
        color.a = max(color.a, z_axis_strength);
    }

	return color;
}

float compute_ndc_depth (vec3 position, in mat4 viewProj)
{
	vec4 clip_space_position = mul(viewProj, vec4 (position.xyz, 1.0));
    
    float ndc_depth = clip_space_position.z / clip_space_position.w;
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_SPIRV
    return ndc_depth;
#else
    return (ndc_depth + 1.0f) * 0.5f;
#endif
}

float compute_depth (vec3 position, in mat4 viewProj)
{
	float near = u_camera_near;
	float far = u_camera_far;
	vec4 clip_space_position = mul(viewProj, vec4 (position.xyz, 1.0f));
	float clip_space_depth = (clip_space_position.z / clip_space_position.w) * 2.0f - 1.0f;
	float depth = (2.0f * near * far) / (far + near - clip_space_depth * (far - near));

	return depth;
}

float compute_linear_depth (vec3 position, in mat4 viewProj)
{
	float far = u_camera_far;
	float depth = compute_depth(position, viewProj);
    // normalize
	return depth / far;
}


void main()
{
	float t = (u_grid_height - v_near_point.y) / (v_far_point.y - v_near_point.y);
	vec3 frag_position_3d = v_near_point + t * (v_far_point - v_near_point);

	gl_FragDepth = compute_ndc_depth(frag_position_3d, u_viewProj);

	float linear_depth = compute_linear_depth (frag_position_3d, u_viewProj);
	float fading = max (0, (0.5 - linear_depth)) * 1.1f;


	float depth = compute_depth (frag_position_3d, u_viewProj);

	// Base grid with improved thickness scaling
	vec4 color = grid (frag_position_3d, 1.0, 2.0 * fading, 0.3f, 0.7f);

	// Multi-scale grids - maximum visibility
	for(int i = 1; i <= 3; ++i)
	{
		float range = pow(10.0, float(i));
		
		// Extremely generous distance-based fade
		float distance_factor = length(frag_position_3d.xz);
		float scale_fade = 1.0 - smoothstep(range * 0.05, range * 0.5, distance_factor);
		scale_fade = clamp(scale_fade, 0.7, 1.0); // Always show at least 70%
		
		// Even thicker lines for maximum visibility
		float scale_thickness = 4.0 * fading;
		
		vec4 scale_grid = grid(frag_position_3d, range, scale_thickness, 1.0f, 0.3);
		
		// Very strong additive blending
		color += scale_grid * scale_fade;
	}

	// Branchless version
	fading *= step(0.0f, t);
	
    color.a *= fading * u_grid_opacity;
    float depthBias = 0.00005 * (1.0 + abs(dot(normalize(frag_position_3d), vec3(0, 1, 0))));
    gl_FragDepth -= depthBias;
	gl_FragColor = color;
}
