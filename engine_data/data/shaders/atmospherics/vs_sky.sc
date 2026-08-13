$input a_position
$output v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"

uniform vec4 u_sunDirection;
uniform vec4 u_skyLuminanceXYZ;
uniform vec4 u_parameters;
uniform vec4 u_perezCoeff[5];

#define u_sun_size u_parameters.x
#define u_sun_bloom u_parameters.y
#define u_exposition u_parameters.z
#define u_time u_parameters.w

vec3 Perez(vec3 A,vec3 B,vec3 C,vec3 D, vec3 E,float costeta, float cosgamma)
{
	float _1_costeta = 1.0 / costeta;
	float cos2gamma = cosgamma * cosgamma;
	float gamma = acos(cosgamma);
	vec3 f = (vec3_splat(1.0) + A * exp(B * _1_costeta))
		   * (vec3_splat(1.0) + C * exp(D * gamma) + E * cos2gamma);
	return f;
}

void main()
{
	v_clipPos = a_position.xy;

	vec4 rayStart = mul(u_invViewProj, vec4(vec3(a_position.xy, -1.0), 1.0));
	vec4 rayEnd = mul(u_invViewProj, vec4(vec3(a_position.xy, 1.0), 1.0));

	rayStart = rayStart / rayStart.w;
	rayEnd = rayEnd / rayEnd.w;

	v_viewDir = normalize(rayEnd.xyz - rayStart.xyz);
    //v_viewDir.y = abs(v_viewDir.y);

	gl_Position = vec4(a_position.xy, 1.0, 1.0);

	vec3 lightDir = normalize(u_sunDirection.xyz);
	vec3 skyDir = vec3(0.0, 1.0, 0.0);

	// Perez coefficients.
	vec3 A = u_perezCoeff[0].xyz;
	vec3 B = u_perezCoeff[1].xyz;
	vec3 C = u_perezCoeff[2].xyz;
	vec3 D = u_perezCoeff[3].xyz;
	vec3 E = u_perezCoeff[4].xyz;

	float costeta = max(dot(v_viewDir, skyDir), 0.01);
	float cosgamma = clamp(dot(v_viewDir, lightDir), -0.9999, 0.9999);
	float cosgammas = clamp(dot(skyDir, lightDir), -0.9999, 0.9999);

	vec3 P = Perez(A,B,C,D,E, costeta, cosgamma);
	vec3 P0 = Perez(A,B,C,D,E, 1.0, cosgammas);

	// Clamp the P/P0 ratio to prevent luminance blow-up at low sun angles.
	// The Perez model produces extreme values in the circumsolar region
	// near the horizon. Allow higher values (18) for more vibrant sunsets.
	vec3 ratio = P / max(P0, vec3_splat(0.0001));

	float denom = u_skyLuminanceXYZ.x + u_skyLuminanceXYZ.y + u_skyLuminanceXYZ.z;
	denom = max(denom, 0.0001);
	vec3 skyColorxyY = vec3(
		  u_skyLuminanceXYZ.x / denom
		, u_skyLuminanceXYZ.y / denom
		, u_skyLuminanceXYZ.y
		);

	vec3 Yp = skyColorxyY * ratio;

	// Guard against division by near-zero y chromaticity
	float yp_y_safe = max(Yp.y, 0.0001);
	vec3 skyColorXYZ = vec3(Yp.x * Yp.z / yp_y_safe, Yp.z, (1.0 - Yp.x - Yp.y) * Yp.z / yp_y_safe);

	v_skyColor = max(convertXYZ2RGB(skyColorXYZ * u_exposition), vec3_splat(0.0));

	// Saturation boost: stronger at zenith (deep blue overhead), tapers toward horizon
	// Matches HDR cubemap look with vibrant blue at zenith. Rec.709 luma, and MUST
	// stay identical to the same boost in cs_irradiance_sh.sc (dome/ambient parity).
	float luma = dot(v_skyColor, vec3(0.2126, 0.7152, 0.0722));
	float zenith_factor = max(v_viewDir.y, 0.0);  // 1 at zenith, 0 at horizon
	float saturation = mix(1.15, 1.45, zenith_factor);
	v_skyColor = mix(vec3_splat(luma), v_skyColor, saturation);
}
