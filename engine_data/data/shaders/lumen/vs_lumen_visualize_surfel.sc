$input a_position, i_data0, i_data1, i_data2
$output v_color0

/*
 * UE 5.8's card generation surfels (LumenVisualize.cpp DrawSurfels): one disc per instance, its six corners around
 * the surfel as a fan from the first corner, LUMEN_SURFEL_BIAS off the surface along the normal. Built in world space
 * as UE builds them: the radius (u_lumen_visualize_surfel.x) is in metres whatever the placement's scale, and the
 * normal goes through the placement's inverse transpose. a_position.x is the corner (0-5).
 */

#include "../common.sh"

/// x = the discs' radius in metres (UE 2 cm x r.Lumen.Visualize.CardGenerationSurfelScale).
uniform vec4 u_lumen_visualize_surfel;

/// UE: the disc 0.5 cm off the surface.
#define LUMEN_SURFEL_BIAS 0.005
/// 2 pi over the disc's six sides.
#define LUMEN_SURFEL_ANGLE_STEP 1.0471975512

void main()
{
	float corner = floor(a_position.x + 0.5);
	vec3 center = mul(u_model[0], vec4(i_data0.xyz, 1.0)).xyz;
	vec3 axis_x = mul(u_model[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz;
	vec3 axis_y = mul(u_model[0], vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	vec3 axis_z = mul(u_model[0], vec4(0.0, 0.0, 1.0, 0.0)).xyz;
	// The inverse transpose by the placement's cofactors, with the determinant's sign.
	vec3 normal = i_data1.x * cross(axis_y, axis_z) + i_data1.y * cross(axis_z, axis_x) + i_data1.z * cross(axis_x, axis_y);
	normal = normalize(normal) * (dot(axis_x, cross(axis_y, axis_z)) < 0.0 ? -1.0 : 1.0);
	// UE FVector::FindBestAxisVectors.
	vec3 tangent = abs(normal.z) > abs(normal.x) && abs(normal.z) > abs(normal.y) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
	tangent = normalize(tangent - normal * dot(normal, tangent));
	vec3 bitangent = cross(tangent, normal);
	float angle = corner * LUMEN_SURFEL_ANGLE_STEP;
	vec3 world = center + normal * LUMEN_SURFEL_BIAS +
	             (tangent * cos(angle) + bitangent * sin(angle)) * u_lumen_visualize_surfel.x;
	gl_Position = mul(u_viewProj, vec4(world, 1.0));
	v_color0 = i_data2;
}
