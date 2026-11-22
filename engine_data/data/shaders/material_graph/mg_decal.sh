#ifndef MG_DECAL_SH_HEADER_GUARD
#define MG_DECAL_SH_HEADER_GUARD

/*
 * What a decal material's generated shader reads before its code (the graph's evaluate_material): the depth copy and
 * the G-buffer of the surfaces the decal lies on, the Scene Texture reads, the texture reads with the gradients of the
 * surface (not of the screen's 2x2 quads, which straddle depth edges), and the inputs of a neighbouring pixel the
 * compiler evaluates a texture's UV at. main() is fs_material_graph_decal.sh.
 *
 * Stages: the graph's textures take 0-6 and 8-12 (MATERIAL_GRAPH_MAX_DECAL_TEXTURES); the depth copy 7, the G-buffer
 * 13 and 14, the receivers' layers 15.
 */

SAMPLER2D(s_decal_depth, 7);
#if DECAL_READS_SURFACE || MG_READS_GBUFFER0
SAMPLER2D(s_decal_gbuffer0, 13);
#endif
#if DECAL_READS_SURFACE || MG_READS_GBUFFER1
SAMPLER2D(s_decal_gbuffer1, 14);
#endif

// The camera-relative inverse view-projection: a pixel's offset from the camera, kept precise anywhere in the world.
uniform mat4 u_decal_inv_view_proj;
// x: the mip bias of the view (negative while TAA jitters it); y: 1 while the G-buffer's base colour is readable (the
// decal buffers' path), 0 while it is a target; z: 1 while the receivers' layers are drawn (s_decal_layers).
uniform vec4 u_decal_view;
// The layers of the receiver each pixel shows, a byte per channel, R the lowest (decal_pass.cpp, draw_layers).
SAMPLER2D(s_decal_layers, 15);

#if BGFX_SHADER_TYPE_FRAGMENT

// The world position of the opaque surface at texel @p texel of the view, from the depth copy.
vec3 mg_decal_texel_position(ivec2 texel)
{
	vec2 size = vec2(textureSize(s_decal_depth, 0));
	ivec2 clamped = clamp(texel, ivec2(0, 0), ivec2(size) - ivec2(1, 1));
	float device_depth = texelFetch(s_decal_depth, clamped, 0).x;
	vec2 uv = (vec2(clamped) + vec2_splat(0.5)) / size;
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, device_depth));
	return u_camera_wpos.xyz + clipToWorld(u_decal_inv_view_proj, clip);
}

// Whether two bytes, 0 to 255 as whole numbers, share a set bit. Arithmetic, as every shading language has it.
float mg_decal_bytes_overlap(float a, float b)
{
	float overlap = 0.0;
	for(int bit = 0; bit < 8; ++bit)
	{
		overlap = max(overlap, step(1.0, mod(a, 2.0)) * step(1.0, mod(b, 2.0)));
		a = floor(a * 0.5);
		b = floor(b * 0.5);
	}
	return overlap;
}

// Whether the receiver at @p texel is on one of the decal's Receiver Layers (@p layers_low and @p layers_high: the low
// and high 16 bits). Every receiver is while the layers are not drawn, and for a decal of every layer, whose receivers
// the layer mask leaves out (decal_pass.cpp, draw_layers).
bool mg_decal_receives_layers(ivec2 texel, float layers_low, float layers_high)
{
	if(u_decal_view.z < 0.5 || (layers_low > 65534.5 && layers_high > 65534.5))
	{
		return true;
	}
	vec4 bytes = floor(texelFetch(s_decal_layers, texel, 0) * 255.0 + 0.5);
	vec4 mask = vec4(mod(layers_low, 256.0), floor(layers_low / 256.0), mod(layers_high, 256.0), floor(layers_high / 256.0));
	float overlap = mg_decal_bytes_overlap(bytes.x, mask.x) + mg_decal_bytes_overlap(bytes.y, mask.y) +
					mg_decal_bytes_overlap(bytes.z, mask.z) + mg_decal_bytes_overlap(bytes.w, mask.w);
	return overlap > 0.0;
}

// The device depth at @p texel, clamped to the view.
float mg_decal_texel_depth(ivec2 texel)
{
	ivec2 size = textureSize(s_decal_depth, 0);
	return texelFetch(s_decal_depth, clamp(texel, ivec2(0, 0), size - ivec2(1, 1)), 0).x;
}

// How the surface's position changes to the next pixel along @p axis (1, 0) or (0, 1), measured on the side of the
// pixel that lies on its surface: two depths on each side, each side's line extrapolated to the centre, the side
// that meets it closer wins. A plane's device depth is linear across the screen, so this is exact on flat ground and
// never mixes two surfaces at a depth edge, where the screen's own derivatives do.
vec3 mg_decal_surface_step(ivec2 texel, ivec2 axis, float depth_center, vec3 position_center)
{
	float before1 = mg_decal_texel_depth(texel - axis);
	float before2 = mg_decal_texel_depth(texel - axis * 2);
	float after1 = mg_decal_texel_depth(texel + axis);
	float after2 = mg_decal_texel_depth(texel + axis * 2);
	float error_before = abs(2.0 * before1 - before2 - depth_center);
	float error_after = abs(2.0 * after1 - after2 - depth_center);
	if(error_after <= error_before)
	{
		return mg_decal_texel_position(texel + axis) - position_center;
	}
	return position_center - mg_decal_texel_position(texel - axis);
}

// The view depth along the camera's direction of the surface at screen position @p uv.
float mg_decal_view_depth(vec2 uv)
{
	ivec2 size = textureSize(s_decal_depth, 0);
	vec3 position = mg_decal_texel_position(ivec2(uv * vec2(size)));
	return dot(position - u_camera_wpos.xyz, mg_camera_direction());
}

// Scene Texture reads of the surface the decal lies on. A read of a target the path draws into reads black.
vec4 mg_scene_texture_scene_depth(vec2 uv)
{
	return vec4_splat(mg_decal_view_depth(uv));
}

#if DECAL_READS_SURFACE || MG_READS_GBUFFER0
vec4 mg_decal_gbuffer0(vec2 uv)
{
	return texture2DLod(s_decal_gbuffer0, uv, 0.0) * u_decal_view.y;
}
#endif

#if DECAL_READS_SURFACE || MG_READS_GBUFFER1
vec4 mg_decal_gbuffer1(vec2 uv)
{
	return texture2DLod(s_decal_gbuffer1, uv, 0.0);
}
#endif

#if MG_READS_GBUFFER0
vec4 mg_scene_texture_base_color(vec2 uv)
{
	return vec4(mg_decal_gbuffer0(uv).rgb, 1.0);
}

vec4 mg_scene_texture_ambient_occlusion(vec2 uv)
{
	return vec4_splat(mg_decal_gbuffer0(uv).a);
}
#endif

#if MG_READS_GBUFFER1
vec4 mg_scene_texture_world_normal(vec2 uv)
{
	return vec4(decodeNormalOctahedron(mg_decal_gbuffer1(uv).xy), 0.0);
}

vec4 mg_scene_texture_metallic(vec2 uv)
{
	return vec4_splat(mg_decal_gbuffer1(uv).z);
}

vec4 mg_scene_texture_roughness(vec2 uv)
{
	return vec4_splat(mg_decal_gbuffer1(uv).w);
}
#endif

// The decal UV's gradients along x and y: the decal's box space changes with v running down its height.
vec2 mg_decal_uv_ddx(MaterialInputs i)
{
	return vec2(i.decal_position_ddx.x, -i.decal_position_ddx.y);
}

vec2 mg_decal_uv_ddy(MaterialInputs i)
{
	return vec2(i.decal_position_ddy.x, -i.decal_position_ddy.y);
}

// The inputs of the next pixel on the same surface along x or y: what a texture's UV is evaluated at again to take
// its gradients (material_graph_compiler.cpp, emit_decal_uv_function).
MaterialInputs mg_decal_neighbour_x(MaterialInputs i)
{
	i.uv0 += mg_decal_uv_ddx(i);
	i.world_position += i.world_position_ddx;
	i.decal_position += i.decal_position_ddx;
	i.screen_uv.x += u_viewTexel.x;
	return i;
}

MaterialInputs mg_decal_neighbour_y(MaterialInputs i)
{
	i.uv0 += mg_decal_uv_ddy(i);
	i.world_position += i.world_position_ddy;
	i.decal_position += i.decal_position_ddy;
	i.screen_uv.y += u_viewTexel.y;
	return i;
}

// A texture read at the decal's own UV, its mip from the surface's gradients.
#define mg_decal_texture2D(_sampler, _coord, _i) \
	texture2DGrad(_sampler, _coord, mg_decal_uv_ddx(_i) * exp2((_i).mip_bias), mg_decal_uv_ddy(_i) * exp2((_i).mip_bias))
// A texture read at a UV the graph computes: the gradients are that UV at the neighbours less it here.
#define mg_decal_texture2D_at(_sampler, _coord, _coord_x, _coord_y, _i) \
	texture2DGrad(_sampler, _coord, ((_coord_x) - (_coord)) * exp2((_i).mip_bias), ((_coord_y) - (_coord)) * exp2((_i).mip_bias))

// getTangentSpaceNormal (shaderlib.sh) with the read's gradients given.
vec3 mg_decal_tangent_space_normal(sampler2D bump_texture, vec2 coord, vec2 coord_ddx, vec2 coord_ddy, float bumpiness, float channels)
{
	vec4 texel = texture2DGrad(bump_texture, coord, coord_ddx, coord_ddy);
	vec3 normal = vec3(channels > 1.5 ? texel.ra : texel.rg, texel.b) * 2.0 - 1.0;
#if NORMAL_MAP_Y_UP
	normal.y = -normal.y;
#endif
	normal.xy *= bumpiness;
	if(channels > 0.5)
	{
		normal.z = sqrt(max(0.0, 1.0 - dot(normal.xy, normal.xy)));
	}
	return normalize(normal);
}

#endif // BGFX_SHADER_TYPE_FRAGMENT

#endif // MG_DECAL_SH_HEADER_GUARD
