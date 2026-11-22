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

// Math Constant node.
#define mg_const_pi 3.14159265359
#define mg_const_tau 6.28318530718
#define mg_const_half_pi 1.57079632679
#define mg_const_e 2.71828182846
#define mg_const_golden_ratio 1.61803398875
#define mg_const_sqrt2 1.41421356237

// per frame
uniform vec4 u_camera_wpos;
// x: seconds since the game started.
uniform vec4 u_material_time;

// What the graph's input nodes read of the pixel.
struct MaterialInputs
{
	// The mesh's first texture coordinates.
	vec2 uv0;
	// The mesh's second texture coordinates (a copy of the first where it has one set) and its vertex colour as the file
	// stores it (white where it has none): what Texture Coordinate's second set and Vertex Color read.
	vec2 uv1;
	vec4 color0;
	vec3 world_position;
	// The interpolated vertex normal in world space, unit length; turned toward the viewer on the back of a two-sided
	// material.
	vec3 vertex_normal;
	// The mesh's tangent and bitangent in world space, unit length: with the vertex normal, the tangent space the Normal
	// channel is in.
	vec3 tangent;
	vec3 bitangent;
	// 1 where the vertex normal faces the viewer, -1 on the back of a two-sided material (and on the faces Cull Front
	// Faces draws). A vertex reads 1.
	float two_sided_sign;
	// Unit vector from the pixel toward the camera.
	vec3 view_direction;
	vec3 camera_position;
	// The direction the camera looks along, unit length.
	vec3 camera_direction;
	// The pixel's position on the view, 0 to 1 from the top-left corner.
	vec2 screen_uv;
	// Seconds since the game started.
	float time;
	// Bias of every texture read: negative while TAA jitters the view.
	float mip_bias;
	// The object's pivot and axes in world space, each axis as long as the object's scale along it: the model matrix.
	// A skinned mesh's are those of the bone frame its vertex moves with. Pixels read them only when the graph has a
	// vertex shader that passes them (MATERIAL_OBJECT_DATA); otherwise they hold the world's own.
	vec3 object_position;
	vec3 object_axis_x;
	vec3 object_axis_y;
	vec3 object_axis_z;
#if MATERIAL_INTERPOLATORS
	// The Vertex Interpolators' values, computed per vertex and blended across the triangle.
	vec4 interpolator0;
	vec4 interpolator1;
	vec4 interpolator2;
	vec4 interpolator3;
#endif
	// Decals: the decal's fade, age in seconds, normalized age and lifetime (Decal Data); (1, 0, 0, 0) elsewhere.
	vec4 decal_data;
	// Decals: the pixel's place in the decal's box, -0.5 to 0.5 per axis (Decal Position), and how it and the world
	// position change to the next pixel on the same surface along x and y: what texture reads take their mips from.
	// Zero elsewhere.
	vec3 decal_position;
	vec3 decal_position_ddx;
	vec3 decal_position_ddy;
	vec3 world_position_ddx;
	vec3 world_position_ddy;
	// Post-process: the exposure the view is shown with (Exposure); 1 elsewhere.
	float exposure;
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
	// The translucency pass: the index of refraction of what the surface bends its view through (1: it bends nothing).
	float refraction;
	// Decal and post-process: how much of the output shows (Opacity), and a decal's opacities of its normal and of its
	// metallic and roughness.
	float opacity;
	float normal_opacity;
	float surface_opacity;
	// Post-process: the pixel's new colour (Color); main() fills in the scene's.
	vec3 color;
};

// What the vertex stage of the graph writes: the Surface Output's World Position Offset, and the values of its Vertex
// Interpolators.
struct MaterialVertex
{
	// World space, added to the vertex's position.
	vec3 world_position_offset;
#if MATERIAL_INTERPOLATORS
	vec4 interpolator0;
	vec4 interpolator1;
	vec4 interpolator2;
	vec4 interpolator3;
#endif
};

// Setting the object inputs of MaterialInputs _i. Macros, not functions: the mains fill MaterialInputs field by field,
// and FXC refuses a struct not yet whole as an inout argument.
// Of a vertex drawn with _model:
#define mg_set_object_inputs(_i, _model) \
	_i.object_position = mul(_model, vec4(0.0, 0.0, 0.0, 1.0)).xyz; \
	_i.object_axis_x = mul(_model, vec4(1.0, 0.0, 0.0, 0.0)).xyz; \
	_i.object_axis_y = mul(_model, vec4(0.0, 1.0, 0.0, 0.0)).xyz; \
	_i.object_axis_z = mul(_model, vec4(0.0, 0.0, 1.0, 0.0)).xyz
// Of a pixel, from the vertex shader's v_object0..2: each axis with one component of the position.
#define mg_set_object_inputs_packed(_i, _packed0, _packed1, _packed2) \
	_i.object_position = vec3((_packed0).w, (_packed1).w, (_packed2).w); \
	_i.object_axis_x = (_packed0).xyz; \
	_i.object_axis_y = (_packed1).xyz; \
	_i.object_axis_z = (_packed2).xyz
// The inputs of the other domains, for a main of a domain without them: a surface reads no decal and no exposure.
#define mg_set_domain_input_defaults(_i) \
	_i.decal_data = vec4(1.0, 0.0, 0.0, 0.0); \
	_i.decal_position = vec3_splat(0.0); \
	_i.decal_position_ddx = vec3_splat(0.0); \
	_i.decal_position_ddy = vec3_splat(0.0); \
	_i.world_position_ddx = vec3_splat(0.0); \
	_i.world_position_ddy = vec3_splat(0.0); \
	_i.exposure = 1.0
// Of a pixel, from the graph's own vertex shader where its pixels read them (MATERIAL_UV1: v_texcoord1,
// MATERIAL_VERTEX_COLOR: v_color0); the first texture coordinates and white otherwise.
#if MATERIAL_UV1
#define MG_PIXEL_UV1 v_texcoord1
#else
#define MG_PIXEL_UV1 v_texcoord0
#endif
#if MATERIAL_VERTEX_COLOR
#define MG_PIXEL_COLOR0 v_color0
#else
#define MG_PIXEL_COLOR0 vec4_splat(1.0)
#endif
#define mg_set_vertex_attribute_inputs(_i) \
	_i.uv1 = MG_PIXEL_UV1; \
	_i.color0 = MG_PIXEL_COLOR0
// Vertex Color by its Color Space: the stored colour as it is, or decoded from sRGB to linear with its alpha as it is.
#define mg_vertex_color_linear(_color) (_color)
vec4 mg_vertex_color_srgb(vec4 _color)
{
	return vec4(toLinearAccurate(_color.rgb), _color.a);
}
// Where no vertex shader passes them: the world's own frame.
#define mg_set_world_object_inputs(_i) \
	_i.object_position = vec3_splat(0.0); \
	_i.object_axis_x = vec3(1.0, 0.0, 0.0); \
	_i.object_axis_y = vec3(0.0, 1.0, 0.0); \
	_i.object_axis_z = vec3(0.0, 0.0, 1.0)

// A texture node's read. A vertex has no derivatives to pick a mip by, and GLSL rejects biased reads outside the
// fragment stage even in a function nobody calls: the vertex stage reads the top mip.
#if BGFX_SHADER_TYPE_FRAGMENT
#define mg_texture2D(_sampler, _coord, _bias) texture2DBias(_sampler, _coord, _bias)
#define mg_textureCube(_sampler, _coord, _bias) textureCubeBias(_sampler, _coord, _bias)
#else
#define mg_texture2D(_sampler, _coord, _bias) texture2DLod(_sampler, _coord, 0.0)
#define mg_textureCube(_sampler, _coord, _bias) textureCubeLod(_sampler, _coord, 0.0)
#endif
// A read of the mip a sample's Mip input gives (its Mip setting: Level), the same in every stage.
#define mg_texture2D_level(_sampler, _coord, _level) texture2DLod(_sampler, _coord, _level)
#define mg_textureCube_level(_sampler, _coord, _level) textureCubeLod(_sampler, _coord, _level)

// The channel a node reads a height or mask from (its Channel setting), as a mask to dot with the texel.
#define mg_channel_r vec4(1.0, 0.0, 0.0, 0.0)
#define mg_channel_g vec4(0.0, 1.0, 0.0, 0.0)
#define mg_channel_b vec4(0.0, 0.0, 1.0, 0.0)
#define mg_channel_a vec4(0.0, 0.0, 0.0, 1.0)

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
	s.refraction = 1.0;
	s.opacity = 1.0;
	s.normal_opacity = 1.0;
	s.surface_opacity = 1.0;
	s.color = vec3_splat(0.0);
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

// The Two Sided Sign input: 1 where the vertex normal faces the viewer, -1 where it faces away, by the same test.
float mg_two_sided_sign(vec3 world_position, vec3 vertex_normal, vec3 view_direction)
{
	vec3 face_normal = cross(dFdx(world_position), dFdy(world_position));
	return dot(face_normal, view_direction) * dot(face_normal, vertex_normal) < 0.0 ? -1.0 : 1.0;
}
#endif

// The direction the camera of the view looks along, in world space: the view matrix's forward axis.
vec3 mg_camera_direction()
{
	return normalize(mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz);
}

// Two unit vectors at right angles to the unit vector n and to each other: a tangent space where the mesh's is not at
// hand (a vertex of the velocity and picking draws).
void mg_basis_from_normal(vec3 n, out vec3 tangent, out vec3 bitangent)
{
	vec3 helper = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	tangent = normalize(cross(helper, n));
	bitangent = cross(n, tangent);
}

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

// Make Material Attributes node: a surface of these channels; the rest (Opacity Mask Clip, Camera Fade Distance) are
// the Surface Output's.
MaterialSurface mg_make_attributes(vec3 base_color,
                                   float metallic,
                                   float roughness,
                                   vec3 specular,
                                   float glossiness,
                                   vec3 normal,
                                   vec3 emissive,
                                   float ambient_occlusion,
                                   float opacity_mask,
                                   vec3 subsurface_color,
                                   float subsurface_opacity)
{
	MaterialSurface s = mg_default_surface();
	s.base_color = base_color;
	s.metallic = metallic;
	s.roughness = roughness;
	s.specular = specular;
	s.glossiness = glossiness;
	s.normal = normal;
	s.emissive = emissive;
	s.ambient_occlusion = ambient_occlusion;
	s.opacity_mask = opacity_mask;
	s.subsurface_color = subsurface_color;
	s.subsurface_opacity = subsurface_opacity;
	return s;
}

// Blend Material Attributes node: every channel of a and b blended by alpha, the normal turned between the two.
MaterialSurface mg_blend_attributes(MaterialSurface a, MaterialSurface b, float alpha)
{
	float t = saturate(alpha);
	MaterialSurface s = a;
	s.base_color = mix(a.base_color, b.base_color, t);
	s.metallic = mix(a.metallic, b.metallic, t);
	s.roughness = mix(a.roughness, b.roughness, t);
	s.specular = mix(a.specular, b.specular, t);
	s.glossiness = mix(a.glossiness, b.glossiness, t);
	s.normal = normalize(mix(a.normal, b.normal, t) + vec3(0.0, 0.0, 0.00001));
	s.emissive = mix(a.emissive, b.emissive, t);
	s.ambient_occlusion = mix(a.ambient_occlusion, b.ambient_occlusion, t);
	s.opacity_mask = mix(a.opacity_mask, b.opacity_mask, t);
	s.subsurface_color = mix(a.subsurface_color, b.subsurface_color, t);
	s.subsurface_opacity = mix(a.subsurface_opacity, b.subsurface_opacity, t);
	return s;
}

// Normal From Texture node: the tangent-space normal of the height in channel of tex, from its slopes offset UV units
// apart, times strength.
vec3 mg_normal_from_texture(sampler2D tex, vec2 uv, float offset, float strength, vec4 channel, float bias)
{
	float step_size = max(offset, 0.000001);
	float center = dot(mg_texture2D(tex, uv, bias), channel);
	float right = dot(mg_texture2D(tex, uv + vec2(step_size, 0.0), bias), channel);
	float up = dot(mg_texture2D(tex, uv + vec2(0.0, step_size), bias), channel);
	vec2 slope = vec2(center - right, center - up) * strength;
	return normalize(vec3(slope, step_size * 4.0));
}

#if BGFX_SHADER_TYPE_FRAGMENT
// Parallax Occlusion node: the UV where the view ray, entering the surface at uv, first meets the height field of
// channel of tex, the field rising height_scale UV units with reference_plane at the surface. Steps from min_steps
// (facing the view) to max_steps (grazing), at most 64, reading with the pixel's own gradients, then a linear refine.
vec2 mg_parallax_occlusion(sampler2D tex, vec2 uv, vec3 tangent_view, float height_scale, float min_steps, float max_steps, float reference_plane, vec4 channel)
{
	vec2 dx = dFdx(uv);
	vec2 dy = dFdy(uv);
	vec3 view = normalize(tangent_view);
	float steps = clamp(mix(max_steps, min_steps, saturate(view.z)), 1.0, 64.0);
	float layer = 1.0 / steps;
	// The UV move over the whole depth of the field: along the view, longer at grazing angles.
	vec2 travel = -view.xy / max(view.z, 0.05) * height_scale;
	vec2 start = uv - travel * (1.0 - reference_plane);
	vec2 step_uv = travel * layer;
	float ray_height = 1.0;
	vec2 current = start;
	float field = dot(texture2DGrad(tex, current, dx, dy), channel);
	float previous_field = field;
	float previous_ray = ray_height;
	for(int i = 0; i < 64; ++i)
	{
		if(float(i) >= steps || field >= ray_height)
		{
			break;
		}
		previous_field = field;
		previous_ray = ray_height;
		current += step_uv;
		ray_height -= layer;
		field = dot(texture2DGrad(tex, current, dx, dy), channel);
	}
	float after = field - ray_height;
	float before = previous_ray - previous_field;
	float weight = after / max(after + before, 0.00001);
	return mix(current, current - step_uv, weight);
}
#endif

// The direction a preview swatch shows a cube texture along at uv: longitude across, latitude down.
vec3 mg_preview_direction(vec2 uv)
{
	float longitude = (uv.x - 0.5) * mg_const_tau;
	float latitude = (0.5 - uv.y) * mg_const_pi;
	return vec3(cos(latitude) * sin(longitude), sin(latitude), cos(latitude) * cos(longitude));
}

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

// A Color Ramp's eased blend between two stops (Interpolation: Smooth).
float mg_smooth_weight(float x)
{
	return x * x * (3.0 - 2.0 * x);
}

// Transform node: a direction, a position or a normal from one space to another, through world space. Tangent space has
// no position of its own: a position there is a direction. The view space is the camera's (x right, y up, z forward).
vec3 mg_object_rows_x(MaterialInputs i) { return cross(i.object_axis_y, i.object_axis_z); }
vec3 mg_object_rows_y(MaterialInputs i) { return cross(i.object_axis_z, i.object_axis_x); }
vec3 mg_object_rows_z(MaterialInputs i) { return cross(i.object_axis_x, i.object_axis_y); }
float mg_object_determinant(MaterialInputs i)
{
	float d = dot(i.object_axis_x, mg_object_rows_x(i));
	return abs(d) < 1e-12 ? 1e-12 : d;
}

vec3 mg_object_to_world_direction(MaterialInputs i, vec3 v) { return i.object_axis_x * v.x + i.object_axis_y * v.y + i.object_axis_z * v.z; }
vec3 mg_object_to_world_position(MaterialInputs i, vec3 v) { return mg_object_to_world_direction(i, v) + i.object_position; }
vec3 mg_object_to_world_normal(MaterialInputs i, vec3 v)
{
	return normalize(mg_object_rows_x(i) * v.x + mg_object_rows_y(i) * v.y + mg_object_rows_z(i) * v.z);
}
vec3 mg_world_to_object_direction(MaterialInputs i, vec3 v)
{
	return vec3(dot(mg_object_rows_x(i), v), dot(mg_object_rows_y(i), v), dot(mg_object_rows_z(i), v)) / mg_object_determinant(i);
}
vec3 mg_world_to_object_position(MaterialInputs i, vec3 v) { return mg_world_to_object_direction(i, v - i.object_position); }
vec3 mg_world_to_object_normal(MaterialInputs i, vec3 v)
{
	return normalize(vec3(dot(i.object_axis_x, v), dot(i.object_axis_y, v), dot(i.object_axis_z, v)));
}

vec3 mg_tangent_to_world_direction(MaterialInputs i, vec3 v) { return i.tangent * v.x + i.bitangent * v.y + i.vertex_normal * v.z; }
vec3 mg_tangent_to_world_position(MaterialInputs i, vec3 v) { return mg_tangent_to_world_direction(i, v); }
vec3 mg_tangent_to_world_normal(MaterialInputs i, vec3 v) { return normalize(mg_tangent_to_world_direction(i, v)); }
vec3 mg_world_to_tangent_direction(MaterialInputs i, vec3 v) { return vec3(dot(v, i.tangent), dot(v, i.bitangent), dot(v, i.vertex_normal)); }
vec3 mg_world_to_tangent_position(MaterialInputs i, vec3 v) { return mg_world_to_tangent_direction(i, v); }
vec3 mg_world_to_tangent_normal(MaterialInputs i, vec3 v) { return normalize(mg_world_to_tangent_direction(i, v)); }

vec3 mg_view_to_world_direction(MaterialInputs i, vec3 v) { return mul(u_invView, vec4(v, 0.0)).xyz; }
vec3 mg_view_to_world_position(MaterialInputs i, vec3 v) { return mul(u_invView, vec4(v, 1.0)).xyz; }
vec3 mg_view_to_world_normal(MaterialInputs i, vec3 v) { return normalize(mg_view_to_world_direction(i, v)); }
vec3 mg_world_to_view_direction(MaterialInputs i, vec3 v) { return mul(u_view, vec4(v, 0.0)).xyz; }
vec3 mg_world_to_view_position(MaterialInputs i, vec3 v) { return mul(u_view, vec4(v, 1.0)).xyz; }
vec3 mg_world_to_view_normal(MaterialInputs i, vec3 v) { return normalize(mg_world_to_view_direction(i, v)); }

vec3 mg_world_to_world_direction(MaterialInputs i, vec3 v) { return v; }
vec3 mg_world_to_world_position(MaterialInputs i, vec3 v) { return v; }
vec3 mg_world_to_world_normal(MaterialInputs i, vec3 v) { return v; }

// The Object Scale input: the length of each axis.
vec3 mg_object_scale(MaterialInputs i)
{
	return vec3(length(i.object_axis_x), length(i.object_axis_y), length(i.object_axis_z));
}

// Hashes of a point to 0..1 (Dave Hoskins' hash without sine), for each width; mg_hash12 is above.
float mg_hash11(float p)
{
	p = fract(p * 0.1031);
	p *= p + 33.33;
	p *= p + p;
	return fract(p);
}

float mg_hash13(vec3 p3)
{
	p3 = fract(p3 * 0.1031);
	p3 += dot(p3, p3.zyx + 31.32);
	return fract((p3.x + p3.y) * p3.z);
}

float mg_hash14(vec4 p4)
{
	p4 = fract(p4 * vec4(0.1031, 0.1030, 0.0973, 0.1099));
	p4 += dot(p4, p4.wzxy + 33.33);
	return fract((p4.x + p4.y) * (p4.z + p4.w));
}

vec2 mg_hash22(vec2 p)
{
	vec3 p3 = fract(vec3(p.x, p.y, p.x) * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 mg_hash33(vec3 p3)
{
	p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973));
	p3 += dot(p3, p3.yxz + 33.33);
	return fract((p3.xxy + p3.yxx) * p3.zyx);
}

// A unit gradient for a lattice point.
vec2 mg_gradient2(vec2 cell)
{
	float angle = mg_hash12(cell) * mg_const_tau;
	return vec2(cos(angle), sin(angle));
}

vec3 mg_gradient3(vec3 cell)
{
	return normalize(mg_hash33(cell) * 2.0 - 1.0 + vec3_splat(0.0001));
}

// Value noise in 3D, 0..1, smooth between lattice points one unit apart.
float mg_value_noise3(vec3 p)
{
	vec3 cell = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	float a = mg_hash13(cell);
	float b = mg_hash13(cell + vec3(1.0, 0.0, 0.0));
	float c = mg_hash13(cell + vec3(0.0, 1.0, 0.0));
	float d = mg_hash13(cell + vec3(1.0, 1.0, 0.0));
	float e = mg_hash13(cell + vec3(0.0, 0.0, 1.0));
	float f1 = mg_hash13(cell + vec3(1.0, 0.0, 1.0));
	float g = mg_hash13(cell + vec3(0.0, 1.0, 1.0));
	float h = mg_hash13(cell + vec3(1.0, 1.0, 1.0));
	float near = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
	float far = mix(mix(e, f1, u.x), mix(g, h, u.x), u.y);
	return mix(near, far, u.z);
}

// Gradient (Perlin) noise, 0..1: unit gradients at the lattice points, a quintic blend between them.
float mg_gradient_noise(vec2 p)
{
	vec2 cell = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = dot(mg_gradient2(cell), f);
	float b = dot(mg_gradient2(cell + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));
	float c = dot(mg_gradient2(cell + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));
	float d = dot(mg_gradient2(cell + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.7071 + 0.5;
}

float mg_gradient_noise3(vec3 p)
{
	vec3 cell = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a = dot(mg_gradient3(cell), f);
	float b = dot(mg_gradient3(cell + vec3(1.0, 0.0, 0.0)), f - vec3(1.0, 0.0, 0.0));
	float c = dot(mg_gradient3(cell + vec3(0.0, 1.0, 0.0)), f - vec3(0.0, 1.0, 0.0));
	float d = dot(mg_gradient3(cell + vec3(1.0, 1.0, 0.0)), f - vec3(1.0, 1.0, 0.0));
	float e = dot(mg_gradient3(cell + vec3(0.0, 0.0, 1.0)), f - vec3(0.0, 0.0, 1.0));
	float f1 = dot(mg_gradient3(cell + vec3(1.0, 0.0, 1.0)), f - vec3(1.0, 0.0, 1.0));
	float g = dot(mg_gradient3(cell + vec3(0.0, 1.0, 1.0)), f - vec3(0.0, 1.0, 1.0));
	float h = dot(mg_gradient3(cell + vec3(1.0, 1.0, 1.0)), f - vec3(1.0, 1.0, 1.0));
	float near = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
	float far = mix(mix(e, f1, u.x), mix(g, h, u.x), u.y);
	return mix(near, far, u.z) * 0.5774 + 0.5;
}

// Simplex noise, 0..1: gradients on a triangle lattice, cheaper than gradient noise and free of its square grid.
float mg_simplex_noise(vec2 p)
{
	const float k1 = 0.366025404;
	const float k2 = 0.211324865;
	vec2 cell = floor(p + (p.x + p.y) * k1);
	vec2 a = p - cell + (cell.x + cell.y) * k2;
	float m = step(a.y, a.x);
	vec2 o = vec2(m, 1.0 - m);
	vec2 b = a - o + k2;
	vec2 c = a - 1.0 + 2.0 * k2;
	vec3 h = max(vec3_splat(0.5) - vec3(dot(a, a), dot(b, b), dot(c, c)), vec3_splat(0.0));
	vec3 n = h * h * h * h * vec3(dot(a, mg_gradient2(cell)), dot(b, mg_gradient2(cell + o)), dot(c, mg_gradient2(cell + 1.0)));
	return dot(n, vec3_splat(70.0)) * 0.5 + 0.5;
}

float mg_simplex_noise3(vec3 p)
{
	const float f3 = 0.333333333;
	const float g3 = 0.166666667;
	vec3 s = floor(p + dot(p, vec3_splat(f3)));
	vec3 x = p - s + dot(s, vec3_splat(g3));
	vec3 e = step(vec3_splat(0.0), x - x.yzx);
	vec3 i1 = e * (1.0 - e.zxy);
	vec3 i2 = 1.0 - e.zxy * (1.0 - e);
	vec3 x1 = x - i1 + g3;
	vec3 x2 = x - i2 + 2.0 * g3;
	vec3 x3 = x - 1.0 + 3.0 * g3;
	vec4 w = vec4(dot(x, x), dot(x1, x1), dot(x2, x2), dot(x3, x3));
	w = max(vec4_splat(0.6) - w, vec4_splat(0.0));
	vec4 d = vec4(dot(x, mg_hash33(s) - 0.5),
	              dot(x1, mg_hash33(s + i1) - 0.5),
	              dot(x2, mg_hash33(s + i2) - 0.5),
	              dot(x3, mg_hash33(s + 1.0) - 0.5));
	w *= w;
	w *= w;
	return clamp(dot(d, w) * 52.0 * 0.5 + 0.5, 0.0, 1.0);
}

// Noise nodes: octaves of a noise, each Lacunarity times finer and Gain times weaker than the one before, averaged by
// their weights so the result stays 0..1. One octave is the noise itself; a fraction of an octave counts as a whole.
#define MG_DEFINE_FRACTAL_NOISE(_name, _type, _noise) \
float _name(_type p, float octaves, float lacunarity, float gain) \
{ \
	float sum = 0.0; \
	float weight = 0.0; \
	float amplitude = 1.0; \
	float count = max(octaves, 1.0); \
	for(int octave = 0; octave < 8; ++octave) \
	{ \
		if(float(octave) >= count) \
		{ \
			break; \
		} \
		sum += _noise(p) * amplitude; \
		weight += amplitude; \
		amplitude *= gain; \
		p *= lacunarity; \
	} \
	return sum / max(weight, 0.0001); \
}

MG_DEFINE_FRACTAL_NOISE(mg_noise_value2, vec2, mg_value_noise)
MG_DEFINE_FRACTAL_NOISE(mg_noise_gradient2, vec2, mg_gradient_noise)
MG_DEFINE_FRACTAL_NOISE(mg_noise_simplex2, vec2, mg_simplex_noise)
MG_DEFINE_FRACTAL_NOISE(mg_noise_value3, vec3, mg_value_noise3)
MG_DEFINE_FRACTAL_NOISE(mg_noise_gradient3, vec3, mg_gradient_noise3)
MG_DEFINE_FRACTAL_NOISE(mg_noise_simplex3, vec3, mg_simplex_noise3)

// Voronoi node: cells around points scattered by randomness (0: a square grid). x: the distance to the nearest point,
// y: the distance to the nearest border between cells, z: a random value per cell, 0..1.
vec3 mg_voronoi(vec2 p, float randomness)
{
	vec2 cell = floor(p);
	vec2 f = fract(p);
	vec2 nearest_offset = vec2_splat(0.0);
	vec2 nearest_cell = vec2_splat(0.0);
	float nearest = 8.0;
	for(int y = -1; y <= 1; ++y)
	{
		for(int x = -1; x <= 1; ++x)
		{
			vec2 g = vec2(float(x), float(y));
			vec2 r = g + mg_hash22(cell + g) * randomness - f;
			float d = dot(r, r);
			if(d < nearest)
			{
				nearest = d;
				nearest_offset = r;
				nearest_cell = g;
			}
		}
	}
	float border = 8.0;
	// Loop variables of their own: HLSL keeps a loop's variable in the enclosing scope.
	for(int by = -1; by <= 1; ++by)
	{
		for(int bx = -1; bx <= 1; ++bx)
		{
			vec2 g = nearest_cell + vec2(float(bx), float(by));
			vec2 r = g + mg_hash22(cell + g) * randomness - f;
			vec2 between = r - nearest_offset;
			if(dot(between, between) > 0.00001)
			{
				border = min(border, dot(0.5 * (nearest_offset + r), normalize(between)));
			}
		}
	}
	return vec3(sqrt(nearest), border, mg_hash12(cell + nearest_cell));
}

vec3 mg_voronoi3(vec3 p, float randomness)
{
	vec3 cell = floor(p);
	vec3 f = fract(p);
	vec3 nearest_offset = vec3_splat(0.0);
	vec3 nearest_cell = vec3_splat(0.0);
	float nearest = 8.0;
	for(int z = -1; z <= 1; ++z)
	{
		for(int y = -1; y <= 1; ++y)
		{
			for(int x = -1; x <= 1; ++x)
			{
				vec3 g = vec3(float(x), float(y), float(z));
				vec3 r = g + mg_hash33(cell + g) * randomness - f;
				float d = dot(r, r);
				if(d < nearest)
				{
					nearest = d;
					nearest_offset = r;
					nearest_cell = g;
				}
			}
		}
	}
	float border = 8.0;
	for(int bz = -1; bz <= 1; ++bz)
	{
		for(int by = -1; by <= 1; ++by)
		{
			for(int bx = -1; bx <= 1; ++bx)
			{
				vec3 g = nearest_cell + vec3(float(bx), float(by), float(bz));
				vec3 r = g + mg_hash33(cell + g) * randomness - f;
				vec3 between = r - nearest_offset;
				if(dot(between, between) > 0.00001)
				{
					border = min(border, dot(0.5 * (nearest_offset + r), normalize(between)));
				}
			}
		}
	}
	return vec3(sqrt(nearest), border, mg_hash13(cell + nearest_cell));
}

// Wave node shapes, 0..1 over each whole number of x: a sine, a square (0 then 1), a triangle and a sawtooth.
#define MG_DEFINE_WAVE(_name, _body) \
float _name(float x) { return _body; } \
vec2 _name(vec2 x) { return _body; } \
vec3 _name(vec3 x) { return _body; } \
vec4 _name(vec4 x) { return _body; }

MG_DEFINE_WAVE(mg_wave_sine, 0.5 - 0.5 * cos(x * mg_const_tau))
MG_DEFINE_WAVE(mg_wave_square, step(0.5, fract(x)))
MG_DEFINE_WAVE(mg_wave_triangle, 1.0 - abs(fract(x) * 2.0 - 1.0))
MG_DEFINE_WAVE(mg_wave_sawtooth, fract(x))

// Rotate About Axis node: position turned by angle radians around the axis through pivot, and the move that makes.
vec3 mg_rotate_about_axis_position(vec3 position, vec3 axis, float angle, vec3 pivot)
{
	vec3 a = normalize(axis);
	vec3 p = position - pivot;
	float s = sin(angle);
	float c = cos(angle);
	return pivot + p * c + cross(a, p) * s + a * dot(a, p) * (1.0 - c);
}

vec3 mg_rotate_about_axis_offset(vec3 position, vec3 axis, float angle, vec3 pivot)
{
	return mg_rotate_about_axis_position(position, axis, angle, pivot) - position;
}

// Polar Coordinates node: x the distance from center (1 at half a UV unit) times radial_scale, y the angle around it,
// 0..1 from straight down, times angle_scale.
vec2 mg_polar_coordinates(vec2 uv, vec2 center, float radial_scale, float angle_scale)
{
	vec2 d = uv - center;
	float radius = length(d) * 2.0 * radial_scale;
	float angle = (atan2(d.x, d.y) / mg_const_tau + 0.5) * angle_scale;
	return vec2(radius, angle);
}

// Twirl node: uv turned around center by strength radians per UV unit of distance, then moved by offset.
vec2 mg_twirl(vec2 uv, vec2 center, float strength, vec2 offset)
{
	vec2 d = uv - center;
	float angle = strength * length(d);
	float s = sin(angle);
	float c = cos(angle);
	return vec2(c * d.x - s * d.y, s * d.x + c * d.y) + center + offset;
}

// Spherize node: uv bulged around center as if wrapped on a sphere, by strength, then moved by offset.
vec2 mg_spherize(vec2 uv, vec2 center, float strength, vec2 offset)
{
	vec2 d = uv - center;
	float d2 = dot(d, d);
	return uv + d * (d2 * d2 * strength) + offset;
}

#define mg_flipbook_from_top 1.0
#define mg_flipbook_from_bottom 0.0

// Flipbook node: the uv of one frame of a sheet of columns x rows frames, read left to right, from the top row down
// (from_top 1) or from the bottom row up (0). The frame wraps around the sheet.
vec2 mg_flipbook(vec2 uv, float columns, float rows, float frame, float from_top)
{
	float cols = max(floor(columns), 1.0);
	float rws = max(floor(rows), 1.0);
	float count = cols * rws;
	float index = floor(frame - count * floor(frame / count));
	float column = index - cols * floor(index / cols);
	float row = floor(index / cols);
	row = from_top > 0.5 ? row : rws - 1.0 - row;
	return (fract(uv) + vec2(column, row)) / vec2(cols, rws);
}

// Sphere Mask node: 1 within radius of the center, falling to 0 at radius, over the outer part hardness leaves soft.
float mg_sphere_mask(float distance_to_center, float radius, float hardness)
{
	float r = max(radius, 0.00001);
	float softness = max(1.0 - saturate(hardness), 0.00001);
	return saturate((1.0 - distance_to_center / r) / softness);
}

// The view direction in the tangent space of i: x along the tangent, y along the bitangent, z along the normal.
vec3 mg_tangent_view_direction(vec3 view_direction, vec3 tangent, vec3 bitangent, vec3 normal)
{
	return vec3(dot(view_direction, tangent), dot(view_direction, bitangent), dot(view_direction, normal));
}

// Bump Offset node: uv moved along the view by height above the reference plane times ratio: a cheap parallax.
vec2 mg_bump_offset(vec2 uv, float height, float ratio, float reference_plane, vec3 tangent_view)
{
	return uv + tangent_view.xy * ((height - reference_plane) * ratio);
}

// Blend Normals node: detail normal b laid over base normal a, both tangent space.
vec3 mg_blend_normals_reoriented(vec3 a, vec3 b)
{
	vec3 t = a + vec3(0.0, 0.0, 1.0);
	vec3 u = b * vec3(-1.0, -1.0, 1.0);
	return normalize(t * dot(t, u) / max(t.z, 0.00001) - u);
}

vec3 mg_blend_normals_whiteout(vec3 a, vec3 b)
{
	return normalize(vec3(a.xy + b.xy, a.z * b.z));
}

vec3 mg_blend_normals_linear(vec3 a, vec3 b)
{
	return normalize(a + b);
}

// Normal Strength node: a tangent-space normal's slope scaled by strength (0: flat).
vec3 mg_normal_strength(vec3 normal, float strength)
{
	return normalize(vec3(normal.xy * strength, mix(1.0, normal.z, saturate(strength))));
}

// Normal Reconstruct Z node: the z of a unit tangent-space normal from its x and y, as two-channel normal maps store it.
vec3 mg_normal_reconstruct_z(vec2 xy)
{
	return vec3(xy, sqrt(saturate(1.0 - dot(xy, xy))));
}

#if BGFX_SHADER_TYPE_FRAGMENT
// Normal From Height node: the tangent-space normal of a surface raised by height (world units times strength),
// from the height's and the position's screen derivatives (surface gradient). Fragment stage only.
vec3 mg_normal_from_height(float height, float strength, vec3 world_position, vec3 normal, vec3 tangent, vec3 bitangent)
{
	vec3 dpdx = dFdx(world_position);
	vec3 dpdy = dFdy(world_position);
	float dhdx = dFdx(height) * strength;
	float dhdy = dFdy(height) * strength;
	vec3 r1 = cross(dpdy, normal);
	vec3 r2 = cross(normal, dpdx);
	float det = dot(dpdx, r1);
	vec3 gradient = (r1 * dhdx + r2 * dhdy) * sign(det);
	vec3 world_normal = normalize(abs(det) * normal - gradient);
	return normalize(vec3(dot(world_normal, tangent), dot(world_normal, bitangent), dot(world_normal, normal)));
}

// Shape nodes: 1 inside the shape centred on the UV square, 0 outside, the edge smoothed over one pixel.
float mg_ellipse(vec2 uv, float width, float height)
{
	float d = length((uv * 2.0 - 1.0) / vec2(max(width, 0.00001), max(height, 0.00001)));
	return saturate((1.0 - d) / max(fwidth(d), 0.00001));
}

float mg_rectangle(vec2 uv, float width, float height)
{
	vec2 d = abs(uv * 2.0 - 1.0) - vec2(width, height);
	d = 1.0 - d / max(fwidth(d), vec2_splat(0.00001));
	return saturate(min(d.x, d.y));
}

float mg_rounded_rectangle(vec2 uv, float width, float height, float radius)
{
	float r = max(min(min(abs(radius * 2.0), abs(width)), abs(height)), 0.00001);
	vec2 p = abs(uv * 2.0 - 1.0) - vec2(width, height) + r;
	float d = length(max(p, vec2_splat(0.0))) / r;
	return saturate((1.0 - d) / max(fwidth(d), 0.00001));
}

float mg_polygon(vec2 uv, float sides, float width, float height)
{
	float n = max(floor(sides), 3.0);
	float corner = cos(mg_const_pi / n);
	vec2 p = (uv * 2.0 - 1.0) / vec2(max(width * corner, 0.00001), max(height * corner, 0.00001));
	p.y = -p.y;
	float angle = atan2(p.x, p.y);
	float r = mg_const_tau / n;
	float d = cos(floor(0.5 + angle / r) * r - angle) * length(p);
	return saturate((1.0 - d) / max(fwidth(d), 0.00001));
}
#endif

// Color nodes.
vec3 mg_rgb_to_hsv(vec3 c)
{
	vec4 k = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = c.g < c.b ? vec4(c.bg, k.wz) : vec4(c.gb, k.xy);
	vec4 q = c.r < p.x ? vec4(p.xyw, c.r) : vec4(c.r, p.yzx);
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 mg_hsv_to_rgb(vec3 c)
{
	vec3 p = abs(fract(vec3_splat(c.x) + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
	return c.z * mix(vec3_splat(1.0), saturate(p - 1.0), c.y);
}

// Hue Saturation Value node: the hue turned by hue_shift (1: a whole turn), the saturation and the value scaled.
vec3 mg_hsv_adjust(vec3 color, float hue_shift, float saturation, float value)
{
	vec3 hsv = mg_rgb_to_hsv(max(color, vec3_splat(0.0)));
	hsv.x = fract(hsv.x + hue_shift);
	hsv.y = saturate(hsv.y * saturation);
	hsv.z *= value;
	return mg_hsv_to_rgb(hsv);
}

float mg_luminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// The sRGB transfer curves, for colours that went through a texture or a picker in the other space.
vec3 mg_srgb_to_linear(vec3 color)
{
	vec3 c = max(color, vec3_splat(0.0));
	vec3 low = c / 12.92;
	vec3 high = pow((c + 0.055) / 1.055, vec3_splat(2.4));
	return mix(low, high, step(vec3_splat(0.04045), c));
}

vec3 mg_linear_to_srgb(vec3 color)
{
	vec3 c = max(color, vec3_splat(0.0));
	vec3 low = c * 12.92;
	vec3 high = 1.055 * pow(c, vec3_splat(1.0 / 2.4)) - 0.055;
	return mix(low, high, step(vec3_splat(0.0031308), c));
}

// Blend node modes: base under blend, as image editors combine layers.
vec3 mg_blend_normal(vec3 base, vec3 blend) { return blend; }
vec3 mg_blend_multiply(vec3 base, vec3 blend) { return base * blend; }
vec3 mg_blend_screen(vec3 base, vec3 blend) { return 1.0 - (1.0 - base) * (1.0 - blend); }
vec3 mg_blend_add(vec3 base, vec3 blend) { return base + blend; }
vec3 mg_blend_subtract(vec3 base, vec3 blend) { return base - blend; }
vec3 mg_blend_difference(vec3 base, vec3 blend) { return abs(base - blend); }
vec3 mg_blend_darken(vec3 base, vec3 blend) { return min(base, blend); }
vec3 mg_blend_lighten(vec3 base, vec3 blend) { return max(base, blend); }
vec3 mg_blend_overlay(vec3 base, vec3 blend)
{
	vec3 low = 2.0 * base * blend;
	vec3 high = 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
	return mix(low, high, step(vec3_splat(0.5), base));
}
vec3 mg_blend_hard_light(vec3 base, vec3 blend)
{
	return mg_blend_overlay(blend, base);
}
vec3 mg_blend_soft_light(vec3 base, vec3 blend)
{
	vec3 low = 2.0 * base * blend + base * base * (1.0 - 2.0 * blend);
	vec3 high = sqrt(max(base, vec3_splat(0.0))) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend);
	return mix(low, high, step(vec3_splat(0.5), blend));
}
vec3 mg_blend_color_dodge(vec3 base, vec3 blend) { return base / max(1.0 - blend, vec3_splat(0.00001)); }
vec3 mg_blend_color_burn(vec3 base, vec3 blend) { return 1.0 - (1.0 - base) / max(blend, vec3_splat(0.00001)); }
vec3 mg_blend_linear_light(vec3 base, vec3 blend) { return base + 2.0 * blend - 1.0; }

#endif // MATERIAL_GRAPH_SH_HEADER_GUARD
