#ifndef __SPHERE_PROBE_SH__
#define __SPHERE_PROBE_SH__

// One sphere reflection probe at a receiver, shared by the probe layer pass (fs_sphere_reflection_probe) and
// the indirect pass that samples a view's only probe itself (PROBE_LAYER_INLINE in fs_pbr_lighting.sh).
// Needs lighting.sh (ComputeReflectionProbeSampleMip).

vec3 GetLookupVectorForSphereCapture(vec3 ReflectionVector, vec3 WorldPosition, vec4 SphereCapturePositionAndRadius, float NormalizedDistanceToCapture, vec3 LocalCaptureOffset, out float DistanceAlpha)
{
	vec3 ProjectedCaptureVector = ReflectionVector;
	float ProjectionSphereRadius = SphereCapturePositionAndRadius.w;
	float SphereRadiusSquared = ProjectionSphereRadius * ProjectionSphereRadius;

	vec3 LocalPosition = WorldPosition - SphereCapturePositionAndRadius.xyz;
	float LocalPositionSqr = dot(LocalPosition, LocalPosition);

	// Find the intersection between the ray along the reflection vector and the capture's sphere
	vec3 QuadraticCoef;
	QuadraticCoef.x = 1;
	QuadraticCoef.y = dot(ReflectionVector, LocalPosition);
	QuadraticCoef.z = LocalPositionSqr - SphereRadiusSquared;

	float Determinant = QuadraticCoef.y * QuadraticCoef.y - QuadraticCoef.z;

	// Only continue if the ray intersects the sphere
	if (Determinant >= 0.0f)
	{
		float FarIntersection = sqrt(Determinant) - QuadraticCoef.y;

		vec3 LocalIntersectionPosition = LocalPosition + FarIntersection * ReflectionVector;
		ProjectedCaptureVector = LocalIntersectionPosition - LocalCaptureOffset;
		// Note: some compilers don't handle smoothstep min > max (this was 1, .6)
		//DistanceAlpha = 1.0f - smoothstep(0.6f, 1.0f, NormalizedDistanceToCapture);

		float x = saturate( 2.5f * NormalizedDistanceToCapture - 1.5f );
		DistanceAlpha = 1.0f - x*x*(3.0f - 2.0f*x);
	}
	#if BGFX_SHADER_LANGUAGE_GLSL
		ProjectedCaptureVector.y = -ProjectedCaptureVector.y;
	#endif
	return ProjectedCaptureVector;
}

/// The probe at a receiver: its capture along the parallax-corrected reflection ray _R, at the mip for
/// _roughness, times _intensity in rgb, and its coverage (the fade towards its radius) in alpha; zero beyond
/// the radius. _positionAndRadius: the probe's position and influence radius; _mips: the cubemap's mip count;
/// _applyPrefilter: 1 when the mips hold the prefiltered lobes.
vec4 SampleSphereProbe(samplerCube _cube, vec3 _worldPosition, vec3 _R, float _roughness, vec4 _positionAndRadius,
                       float _mips, float _intensity, float _applyPrefilter)
{
	float DistanceAlpha = 0.0f;

	vec3 CaptureVector = _worldPosition - _positionAndRadius.xyz;
	float CaptureVectorLength = sqrt(dot(CaptureVector, CaptureVector));
	float NormalizedDistanceToCapture = saturate(CaptureVectorLength / _positionAndRadius.w);

	vec4 color = vec4(0.0f, 0.0f, 0.0f, 0.0f);
	BRANCH
	if (CaptureVectorLength < _positionAndRadius.w)
	{
		vec3 ProjectedCaptureVector = GetLookupVectorForSphereCapture(_R, _worldPosition, _positionAndRadius, NormalizedDistanceToCapture, vec3_splat(0.0), DistanceAlpha);
		float lod = ComputeReflectionProbeSampleMip(_roughness, _mips, _applyPrefilter);
		color.xyz = textureCubeLod(_cube, ProjectedCaptureVector, lod).xyz * _intensity;
	}
	color.w = DistanceAlpha;
	return color;
}

#endif // __SPHERE_PROBE_SH__
