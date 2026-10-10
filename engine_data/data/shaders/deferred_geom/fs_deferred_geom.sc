$input v_wpos, v_pos, v_wnormal, v_wtangent, v_wbitangent, v_texcoord0, v_lod_params

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_tex_color,  0);
SAMPLER2D(s_tex_normal, 1);
SAMPLER2D(s_tex_roughness, 2);
SAMPLER2D(s_tex_metalness, 3);
SAMPLER2D(s_tex_ao, 4);
SAMPLER2D(s_tex_emissive, 5);

// per frame
uniform vec4 u_camera_wpos;
uniform vec4 u_camera_clip_planes; //.x = near, .y = far, .z = material texture mip bias

// per instance
uniform vec4 u_base_color;
uniform vec4 u_subsurface_color;
uniform vec4 u_emissive_color;
uniform vec4 u_surface_data;
uniform vec4 u_surface_data2;

uniform vec4 u_tiling;
uniform vec4 u_dither_threshold; //.x = alpha threshold .y = distance threshold

#define u_surface_roughness u_surface_data.x
#define u_surface_metalness u_surface_data.y
#define u_surface_bumpiness u_surface_data.z
#define u_surface_alpha_test_value u_surface_data.w
#define u_surface_metalness_roughness_combined u_surface_data2.x
#define u_surface_normal_reconstruct_z u_surface_data2.y
#define u_surface_two_sided (u_surface_data2.z > 0.5f)
#define u_surface_alpha_cutout (u_surface_data2.w > 0.5f)

#define u_camear_near u_camera_clip_planes.x
#define u_camear_far u_camera_clip_planes.y
// Negative while TAA jitters the view: the jitter integrates the sharper mip's extra detail.
#define u_material_mip_bias u_camera_clip_planes.z

#define u_dither_alpha_threshold u_dither_threshold.x
#define u_dither_distance_threshold u_dither_threshold.y

void main()
{
	vec2 texcoords = v_texcoord0.xy * u_tiling.xy;

	vec4 metalness_val = texture2DBias(s_tex_metalness, texcoords, u_material_mip_bias);
	float metalness =  u_surface_metalness;
	
	float roughness = u_surface_roughness;
	if(u_surface_metalness_roughness_combined > 0.5f)
	{
	
		roughness *= metalness_val.g;
		metalness *= metalness_val.b;
	}
	else
	{
		roughness *= texture2DBias(s_tex_roughness, texcoords, u_material_mip_bias).r;
		metalness *= metalness_val.r;
	}
	
	// The authored roughness: direct lighting applies its own floor (GetLightingRoughness in lighting.sh).
	roughness = saturate(roughness);
	
	float ambient_occlusion = texture2DBias(s_tex_ao, texcoords, u_material_mip_bias).r;
	vec3 emissive = texture2DBias(s_tex_emissive, texcoords, u_material_mip_bias).rgb;

	float bumpiness = u_surface_bumpiness;
	float alpha_test_value = u_surface_alpha_test_value;

	vec3 view_direction = u_camera_wpos.xyz - v_wpos;
	vec3 tangent_space_normal = getTangentSpaceNormal( s_tex_normal, texcoords, bumpiness, u_surface_normal_reconstruct_z, u_material_mip_bias );

	// A two-sided material's back face turns its normal toward the viewer (a sign flip of the tangent frame's
	// normal axis). The triangle's normal from the position derivatives tells which side the viewer is on and which
	// side the vertex normals face; both products use it, so each backend's screen-axis convention cancels. A card
	// capture places u_camera_wpos far along its capture direction, which makes the same test hold there.
	// A uniform branch: fxc evaluates both sides of &&, which put the derivatives on every material's pixels.
	vec3 vertex_normal = v_wnormal;
	BRANCH
	if(u_surface_two_sided)
	{
		vec3 face_normal = cross(dFdx(v_wpos), dFdy(v_wpos));
		if(dot(face_normal, view_direction) * dot(face_normal, v_wnormal) < 0.0f)
		{
			vertex_normal = -v_wnormal;
		}
	}

	//mat3 tangent_to_world_space = computeTangentToWorldSpaceMatrix(normalize(v_wnormal), normalize(view_direction), texcoords.xy);
	mat3 tangent_to_world_space = constructTangentToWorldSpaceMatrix(normalize(v_wtangent), normalize(v_wbitangent), normalize(vertex_normal));

	vec3 wnormal = normalize( mul( tangent_to_world_space, tangent_space_normal ).xyz );
	vec4 albedo_color = texture2DBias(s_tex_color, texcoords, u_material_mip_bias) * u_base_color;

	float distance = length(view_direction) - u_camear_near * 2.0f;
	float distance_factor = saturate(distance / u_dither_distance_threshold);
	float dither = ign16x16(gl_FragCoord.xy);

	// LOD transition using optimized single-component branchless approach
	// v_lod_params.x: positive = current LOD fading out, negative = target LOD fading in
	// Positive: discard upper portion (dither > threshold)
	// Negative: discard lower portion (dither < 1-threshold)
	float lod_param = v_lod_params.x;
	float abs_param = abs(lod_param);
	float is_positive = step(0.0f, lod_param);  // 1.0 if positive/zero, 0.0 if negative
	float threshold = mix(1.0f - abs_param, abs_param, is_positive);  // Positive: abs_param, Negative: 1-abs_param
	bool lod_discard = (dither - threshold) * sign(lod_param) > 0.0f;

	bool alpha_discard = albedo_color.a + 0.01f + (dither * (1.0f - alpha_test_value)) < 1.0f;
#ifdef DEFERRED_GEOM_CARD_CAPTURE
	// A card capture keeps every texel of a masked material (fs_deferred_geom_card_capture.sc); an opaque material's
	// dithered alpha covers what the view shows.
	alpha_discard = alpha_discard && !u_surface_alpha_cutout;
#endif
	if(alpha_discard || (distance_factor + dither < 1.0f) || lod_discard)
	{
		discard;
	}

	// Not named "buffer": that is a reserved word in GLSL 4.30, the OpenGL shader baseline.
	GBufferData gbuffer;
	gbuffer.base_color = albedo_color.rgb;
	gbuffer.ambient_occlusion = ambient_occlusion;
	gbuffer.world_normal = wnormal;
	gbuffer.roughness = roughness;
	gbuffer.emissive_color = emissive * u_emissive_color.rgb;
	gbuffer.metalness = metalness;
	gbuffer.subsurface_color = u_subsurface_color.rgb;
	gbuffer.subsurface_opacity = u_subsurface_color.w;

	vec4 result[4];
    EncodeGBuffer(gbuffer, result);

	gl_FragData[0] = result[0];
	gl_FragData[1] = result[1];
	gl_FragData[2] = result[2];
	gl_FragData[3] = result[3];
}
