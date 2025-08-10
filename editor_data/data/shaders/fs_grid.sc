$input v_near_point
$input v_far_point

#include <bgfx_shader.sh>

uniform vec4 u_params;

#define u_grid_height   u_params.x
#define u_camera_near   u_params.y
#define u_camera_far    u_params.z
#define u_grid_opacity  u_params.w

vec4 grid (vec3 frag_position_3d, float scale, float thickness, float axis_alpha)
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
	vec2 derivative = fwidth (coord);
    vec2 fr = fract(coord);
    vec2 frabs = abs(fr);
	vec2 gr = frabs / derivative;
	float ln = min(gr.x, gr.y);
	float minimum_z = min(derivative.y, 1.0f);
	float minimum_x = min(derivative.x, 1.0f);
    
    float opacity = 0.3f;
    float axisLineThreshold = thickness * scale;
	vec4 color = vec4(1.0f, 1.0f, 1.0f, thickness - min(ln, thickness));

	// z axis color
	if (frag_position_3d.x > -axisLineThreshold * minimum_x
		&& frag_position_3d.x < axisLineThreshold * minimum_x)
	{
        color.r = 0.1f;
        color.g = 1.0f;
        color.b = 0.3f;
        color.a = axis_alpha;
	}

	// x axis color
	if (frag_position_3d.z > -axisLineThreshold * minimum_z
		&& frag_position_3d.z < axisLineThreshold * minimum_z)
	{
		color.r = 1.0f;
        color.g = 0.35f;
        color.b = 0.3f;
        color.a = axis_alpha;
	}
	
	color *= opacity;

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

	vec4 color = grid (frag_position_3d, 1, 3.0f * fading, 0.7f);

	for(int i = 1; i <= 3; ++i)
	{
		float range = float(pow(10, float(i)));
		float opacity = depth / range;
		color += grid (frag_position_3d, range, 3.0f*fading, 1.0f);
	}

	// Branchless version
	fading *= step(0.0f, t);
	
    color.a *= fading * u_grid_opacity;
    float depthBias = 0.00005 * (1.0 + abs(dot(normalize(frag_position_3d), vec3(0, 1, 0))));
    gl_FragDepth -= depthBias;
	gl_FragColor = color;
}
