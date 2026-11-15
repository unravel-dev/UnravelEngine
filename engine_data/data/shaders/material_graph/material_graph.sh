#ifndef MATERIAL_GRAPH_SH_HEADER_GUARD
#define MATERIAL_GRAPH_SH_HEADER_GUARD

/*
 * What a material graph's generated shader reads and writes, and the functions its nodes call
 * (engine/engine/rendering/material_graph/). The generated shader includes common.sh, lighting.sh and this, defines
 * evaluate_material(MaterialInputs i, inout MaterialSurface s) from the graph, then includes the main() of its variant:
 * fs_material_graph_gbuffer.sh, or fs_material_graph_shadow.sh for the shadow casters of a graph whose Opacity Mask
 * clips or whose World Position Offset moves its vertices. Such a graph has a vertex shader too, which includes this,
 * defines evaluate_vertex(MaterialInputs i, inout MaterialVertex s) and includes vs_material_graph.sh.
 */

// per frame
uniform vec4 u_camera_wpos;
// x: seconds since the game started.
uniform vec4 u_material_time;

// What the graph's input nodes read of the pixel.
struct MaterialInputs
{
	// The mesh's first texture coordinates.
	vec2 uv0;
	vec3 world_position;
	// The interpolated vertex normal in world space, unit length; turned toward the viewer on the back of a two-sided
	// material.
	vec3 vertex_normal;
	// Unit vector from the pixel toward the camera.
	vec3 view_direction;
	vec3 camera_position;
	// The pixel's position on the view, 0 to 1 from the top-left corner.
	vec2 screen_uv;
	// Seconds since the game started.
	float time;
	// Bias of every texture read: negative while TAA jitters the view.
	float mip_bias;
#ifdef MATERIAL_GRAPH_INSTANCE_MATERIAL
	// The instance's own values of the first parameter registers (the instance_material variant): the varyings are
	// main()'s alone, so the graph's code reads them here.
	vec4 instance_register0;
	vec4 instance_register1;
	vec4 instance_register2;
	vec4 instance_register3;
#endif
};

// The material's channels: what the Surface Output node writes. main() fills in the defaults first.
struct MaterialSurface
{
	// The diffuse color in the specular workflow (MATERIAL_SPECULAR_WORKFLOW).
	vec3 base_color;
	float metallic;
	float roughness;
	// The specular workflow's specular color and glossiness, which replace metallic and roughness there.
	vec3 specular;
	float glossiness;
	// Tangent space: (0, 0, 1) is the vertex normal.
	vec3 normal;
	vec3 emissive;
	float ambient_occlusion;
	float opacity_mask;
	// The Opacity Mask below which a masked material's pixel is cut out.
	float opacity_mask_clip;
	vec3 subsurface_color;
	float subsurface_opacity;
	// Within this distance of the camera the surface dithers out; 0: it does not.
	float camera_fade_distance;
};

// What the vertex stage of the graph writes: the Surface Output's World Position Offset.
struct MaterialVertex
{
	// World space, added to the vertex's position.
	vec3 world_position_offset;
};

// A texture node's read. A vertex has no derivatives to pick a mip by, and GLSL rejects biased reads outside the
// fragment stage even in a function nobody calls: the vertex stage reads the top mip.
#if BGFX_SHADER_TYPE_FRAGMENT
#define mg_texture2D(_sampler, _coord, _bias) texture2DBias(_sampler, _coord, _bias)
#else
#define mg_texture2D(_sampler, _coord, _bias) texture2DLod(_sampler, _coord, 0.0)
#endif

// The channels of a surface the graph leaves alone.
MaterialSurface mg_default_surface()
{
	MaterialSurface s;
	s.base_color = vec3_splat(0.8);
	s.metallic = 0.0;
	s.roughness = 0.5;
	s.specular = vec3_splat(0.0);
	s.glossiness = 0.5;
	s.normal = vec3(0.0, 0.0, 1.0);
	s.emissive = vec3_splat(0.0);
	s.ambient_occlusion = 1.0;
	s.opacity_mask = 1.0;
	s.opacity_mask_clip = 0.5;
	s.subsurface_color = vec3_splat(0.0);
	s.subsurface_opacity = 0.0;
	s.camera_fade_distance = 0.0;
	return s;
}

// A Discard node's test: the pixel is not drawn where it holds. A GI card capture keeps every texel, as it keeps a
// masked material's (fs_material_graph_gbuffer.sh).
#if DEFERRED_GEOM_CARD_CAPTURE
#define mg_discard_if(condition)
#else
#define mg_discard_if(condition) if(condition) { discard; }
#endif

// The vertex normal turned toward the viewer on the back of a two-sided material (MATERIAL_TWO_SIDED): the triangle's
// normal from the position derivatives tells which side the viewer is on and which side the vertex normals face; both
// products use it, so each backend's screen-axis convention cancels. Fragment stage only: it reads derivatives.
#if BGFX_SHADER_TYPE_FRAGMENT
vec3 mg_facing_vertex_normal(vec3 world_position, vec3 vertex_normal, vec3 view_direction)
{
#if MATERIAL_TWO_SIDED
	vec3 face_normal = cross(dFdx(world_position), dFdy(world_position));
	if(dot(face_normal, view_direction) * dot(face_normal, vertex_normal) < 0.0)
	{
		return -vertex_normal;
	}
#endif
	return vertex_normal;
}
#endif

// The BT.601 luminance the specular solve weighs colors by.
float mg_perceived_brightness(vec3 color)
{
	return sqrt(dot(vec3(0.299, 0.587, 0.114), color * color));
}

// The specular workflow's surface as the metal / roughness G-buffer stores it (MATERIAL_SPECULAR_WORKFLOW): the solve
// the importer baked specular / gloss textures with, and convert_specular_to_metallic, its C++ twin in
// standard_material.cpp. It is calibrated in sRGB-encoded space, so the linear diffuse and specular are encoded first
// and the base color is decoded again. Fragment stage only: lighting.sh holds the sRGB curves.
#if BGFX_SHADER_TYPE_FRAGMENT
void mg_specular_to_metallic(vec3 diffuse, vec3 specular, float glossiness, out vec3 base_color, out float metallic, out float roughness)
{
	const float dielectric_f0 = 0.04;
	vec3 diffuse_srgb = linear_to_srgb(saturate(diffuse));
	vec3 specular_srgb = linear_to_srgb(saturate(specular));
	float perceived_diffuse = mg_perceived_brightness(diffuse_srgb);
	float perceived_specular = mg_perceived_brightness(specular_srgb);
	float one_minus_specular_strength = 1.0 - max(specular_srgb.x, max(specular_srgb.y, specular_srgb.z));
	float b = perceived_diffuse * one_minus_specular_strength / (1.0 - dielectric_f0) + perceived_specular - 2.0 * dielectric_f0;
	float c = dielectric_f0 - perceived_specular;
	float discriminant = max(b * b - 4.0 * dielectric_f0 * c, 0.0);
	float solved = clamp((-b + sqrt(discriminant)) / (2.0 * dielectric_f0), 0.0, 1.0);
	metallic = perceived_specular < dielectric_f0 ? 0.0 : solved;
	// Base color from the diffuse identity and from the specular one, blended by metallic squared.
	vec3 from_diffuse = diffuse_srgb * (1.0 - dielectric_f0) / max(1.0 - metallic * dielectric_f0, 1e-6);
	vec3 from_specular = specular_srgb - vec3_splat(dielectric_f0 * (1.0 - metallic));
	base_color = srgb_to_linear(saturate(mix(from_diffuse, from_specular, metallic * metallic)));
	roughness = saturate(1.0 - glossiness);
}
#endif

// Checker node: 0 and 1 alternating over scale squares per UV unit.
float mg_checker(vec2 uv, vec2 scale)
{
	vec2 cell = floor(uv * scale);
	float sum = cell.x + cell.y;
	return sum - 2.0 * floor(sum * 0.5);
}

// A hash of a 2D point to 0..1 (Dave Hoskins' hash12): no sine, so every GPU gives the same pattern.
float mg_hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

// Noise node: value noise, smooth between lattice points one UV unit apart.
float mg_value_noise(vec2 uv)
{
	vec2 cell = floor(uv);
	vec2 f = fract(uv);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float a = mg_hash12(cell);
	float b = mg_hash12(cell + vec2(1.0, 0.0));
	float c = mg_hash12(cell + vec2(0.0, 1.0));
	float d = mg_hash12(cell + vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fresnel node: 0 where the surface faces the viewer, 1 at grazing angles.
float mg_fresnel(vec3 normal, vec3 view_direction, float power)
{
	float facing = saturate(dot(normalize(normal), view_direction));
	return pow(max(1.0 - facing, 0.0001), power);
}

// Rotate UV node: uv turned around center by angle radians.
vec2 mg_rotate_uv(vec2 uv, vec2 center, float angle)
{
	float s = sin(angle);
	float c = cos(angle);
	vec2 d = uv - center;
	return vec2(d.x * c - d.y * s, d.x * s + d.y * c) + center;
}

// Desaturate node: the colour moved toward its luminance (Rec. 709) by amount.
vec3 mg_desaturate(vec3 color, float amount)
{
	float grey = dot(color, vec3(0.2126, 0.7152, 0.0722));
	return mix(color, vec3_splat(grey), amount);
}

// Triplanar Sample node: the texture read on the three planes the axes face, at position, blended by how much the
// normal faces each axis, raised to sharpness.
vec4 mg_triplanar(sampler2D tex, vec3 position, vec3 normal, float sharpness, float bias)
{
	vec3 weights = pow(abs(normalize(normal)), vec3_splat(max(sharpness, 0.0001)));
	weights /= max(weights.x + weights.y + weights.z, 0.0001);
	vec4 along_x = mg_texture2D(tex, position.zy, bias);
	vec4 along_y = mg_texture2D(tex, position.xz, bias);
	vec4 along_z = mg_texture2D(tex, position.xy, bias);
	return along_x * weights.x + along_y * weights.y + along_z * weights.z;
}

#endif // MATERIAL_GRAPH_SH_HEADER_GUARD
