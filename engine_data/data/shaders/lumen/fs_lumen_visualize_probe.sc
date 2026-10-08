$input v_sphere, v_world_position, v_probe

/*
 * The probe spheres: the camera ray against the sphere, shaded at the hit by the sphere normal and written as radiance
 * into the pre-exposed scene colour.
 *  mode 0 (UE VisualizeRadiosityProbesPS): the probe's irradiance SH as a white diffuse surface
 *         (max(SH . the diffuse transfer of the normal, 0) / pi); an invalid probe pink.
 *  mode 1 (UE VisualizeRadianceCachePS, VISUALIZE_MODE_RADIANCE): the probe's radiance along the normal.
 */

#include "../common.sh"
#include "../sampling.sh"

SAMPLER2D(s_lumen_radiosity_sh_r, 3);
SAMPLER2D(s_lumen_radiosity_sh_g, 4);
SAMPLER2D(s_lumen_radiosity_sh_b, 5);
/// The radiance cache's bordered radiance atlas (cached lighting), mode 1.
SAMPLER2D(s_lumen_rc_final, 7);

#include "lumen/lumen_radiosity_common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_radiance_cache_common.sh"

/// x = mode (vs_lumen_visualize_probe.sc).
uniform vec4 u_lumen_visualize_probe;

#define LUMEN_VISUALIZE_MODE_RADIANCE_CACHE 1

/// x = the scene colour's pre-exposure.
uniform vec4 u_lumen_visualize_probe2;

#define LUMEN_PI 3.14159265

void main()
{
	vec3 eye = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 direction = normalize(v_world_position - eye);
	vec3 offset = eye - v_sphere.xyz;
	float b = dot(offset, direction);
	float c = dot(offset, offset) - v_sphere.w * v_sphere.w;
	float h = b * b - c;
	if(h < 0.0)
	{
		discard;
	}
	float t = -b - sqrt(h);
	if(t < 0.0)
	{
		discard;
	}
	vec3 normal = normalize(eye + direction * t - v_sphere.xyz);
	vec3 lighting = vec3(1.0, 0.0, 1.0);
	BRANCH
	if(int(u_lumen_visualize_probe.x + 0.5) == LUMEN_VISUALIZE_MODE_RADIANCE_CACHE)
	{
		uint probe = uint(v_probe.x + 0.5);
		vec2 uv = LumenInverseEquiAreaSphericalMapping(normal);
		vec2 final_texel = vec2(LumenRcProbeTileOrigin(probe, u_lumen_rc_final_res)) + 1.0 +
		                   uv * float(u_lumen_rc_probe_res);
		// Cached lighting is absolute radiance here (GI_CACHED_LIGHTING_PRE_EXPOSURE 1).
		lighting = texture2DLod(s_lumen_rc_final, final_texel / vec2(textureSize(s_lumen_rc_final, 0)), 0.0).xyz;
	}
	else if(v_probe.z > 0.5)
	{
		// Rounded: the interpolated constant can land a hair below the integer.
		ivec2 probe = ivec2(v_probe.xy + 0.5);
		vec4 transfer = LumenSH2DiffuseTransfer(normal);
		vec3 irradiance = vec3(dot(texelFetch(s_lumen_radiosity_sh_r, probe, 0), transfer),
		                       dot(texelFetch(s_lumen_radiosity_sh_g, probe, 0), transfer),
		                       dot(texelFetch(s_lumen_radiosity_sh_b, probe, 0), transfer));
		lighting = max(irradiance, vec3_splat(0.0)) / LUMEN_PI;
	}
	gl_FragColor = vec4(lighting * u_lumen_visualize_probe2.x, 1.0);
}
