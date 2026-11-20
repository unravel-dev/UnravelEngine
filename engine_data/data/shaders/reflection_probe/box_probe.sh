#ifndef __BOX_PROBE_SH__
#define __BOX_PROBE_SH__

// One box reflection probe at a receiver, shared by the probe layer pass (fs_box_reflection_probe) and the
// translucency pass's surfaces (material_graph/mg_forward.sh). Needs lighting.sh (ComputeReflectionProbeSampleMip).

/** Computes distance from an AABB to a point in space. */
float ComputeDistanceFromBoxToPoint(vec3 Mins, vec3 Maxs, vec3 InPoint)
{
	vec3 DistancesToMin = min(abs(InPoint) - Mins, vec3(0.0f, 0.0f, 0.0f));
	vec3 DistancesToMax = max(abs(InPoint) - Maxs, vec3(0.0f, 0.0f, 0.0f));

	float Distance = dot(DistancesToMin, DistancesToMin);
	Distance += dot(DistancesToMax, DistancesToMax);
	return sqrt(Distance);
}

vec3 GetLookupVectorForBoxCapture(vec3 ReflectionVector, vec3 WorldPosition, vec4 BoxCapturePositionAndRadius, mat4 BoxTransform, vec4 BoxScales, vec3 LocalCaptureOffset, out float DistanceAlpha)
{
	// Transform the ray into the local space of the box, where it is an AABB with mins at -1 and maxs at 1
	vec3 LocalRayStart		= mul(BoxTransform, vec4(WorldPosition, 1)).xyz;
	vec3 LocalRayDirection	= mul(BoxTransform, vec4(ReflectionVector, 0)).xyz;

	vec3 InvRayDir = rcp(LocalRayDirection);

	//find the ray intersection with each of the 3 planes defined by the minimum extrema.
	vec3 FirstPlaneIntersections = -InvRayDir - LocalRayStart * InvRayDir;
	//find the ray intersection with each of the 3 planes defined by the maximum extrema.
	vec3 SecondPlaneIntersections = InvRayDir - LocalRayStart * InvRayDir;
	//get the furthest of these intersections along the ray
	vec3 FurthestPlaneIntersections = max(FirstPlaneIntersections, SecondPlaneIntersections);

	//clamp the intersections to be between RayOrigin and RayEnd on the ray
	float Intersection = min(FurthestPlaneIntersections.x, min(FurthestPlaneIntersections.y, FurthestPlaneIntersections.z));
	// Compute the reprojected vector
	vec3 IntersectPosition = WorldPosition + Intersection * ReflectionVector;
	vec3 ProjectedCaptureVector = IntersectPosition - (BoxCapturePositionAndRadius.xyz  + LocalCaptureOffset);

	// Compute the distance from the receiving pixel to the box for masking
	// Apply local to world scale to take scale into account without transforming back to world space
	// Shrink the box by the transition distance (BoxScales.w) so that the fade happens inside the box influence area
	float BoxDistance = ComputeDistanceFromBoxToPoint(-(BoxScales.xyz - 0.5f * BoxScales.w), BoxScales.xyz - 0.5f * BoxScales.w, LocalRayStart * BoxScales.xyz);

	// Setup a fade based on receiver distance to the box, hides the box influence shape
	DistanceAlpha = 1.0f - smoothstep(0.0f, 0.7f * BoxScales.w, BoxDistance);

	#if BGFX_SHADER_LANGUAGE_GLSL
		ProjectedCaptureVector.y = -ProjectedCaptureVector.y;
	#endif

	return ProjectedCaptureVector;
}

/// The probe at a receiver, as SampleSphereProbe gives a sphere probe: its capture along the box-corrected reflection
/// ray _R at the mip for _roughness times _intensity in rgb, its coverage in alpha; zero beyond the influence radius.
/// _invWorld: world to the box's -1..1 space; _extents: the box's half extents (xyz) and transition distance (w).
vec4 SampleBoxProbe(samplerCube _cube, vec3 _worldPosition, vec3 _R, float _roughness, vec4 _positionAndRadius, mat4 _invWorld,
                    vec4 _extents, float _mips, float _intensity, float _applyPrefilter)
{
	vec3 CaptureVector = _worldPosition - _positionAndRadius.xyz;
	vec4 color = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	BRANCH
	if(dot(CaptureVector, CaptureVector) < _positionAndRadius.w * _positionAndRadius.w)
	{
		float DistanceAlpha = 0.0f;
		vec3 ProjectedCaptureVector = GetLookupVectorForBoxCapture(_R, _worldPosition, _positionAndRadius, _invWorld, _extents, vec3_splat(0.0), DistanceAlpha);
		if(DistanceAlpha >= 0.0f)
		{
			float lod = ComputeReflectionProbeSampleMip(_roughness, _mips, _applyPrefilter);
			color.xyz = textureCubeLod(_cube, ProjectedCaptureVector, lod).xyz * _intensity;
		}
		color.w = max(DistanceAlpha, 0.0);
	}
	return color;
}

#endif // __BOX_PROBE_SH__
