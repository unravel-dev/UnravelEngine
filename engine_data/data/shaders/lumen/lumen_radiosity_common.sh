#ifndef __LUMEN_RADIOSITY_COMMON_SH__
#define __LUMEN_RADIOSITY_COMMON_SH__

/*
 * Lumen surface cache radiosity, shared layout (UE 5.8 LumenRadiosity.ush / LumenRadiosity.usf, Epic):
 * probes every LUMEN_RADIOSITY_PROBE_SPACING card texels, each at its cell's texel offset by a jitter that
 * cycles over four updates of the page (a Latin square), tracing LUMEN_RADIOSITY_RAYS_PER_AXIS^2 stratified,
 * noise-jittered uniform hemisphere rays. With the spacing equal to the rays per axis, a probe's traces fill
 * exactly its own cell of an atlas the size of the physical atlas.
 *
 * Work is scheduled in card tiles of 8 x 8 texels (2 x 2 probes); a tile record is 3 vec4: (tile origin in
 * the atlas xy, card index, the page's update index), the page's card UV rectangle, (page atlas origin xy,
 * page size xy).
 */

#define LUMEN_RADIOSITY_PROBE_SPACING 4
#define LUMEN_RADIOSITY_RAYS_PER_AXIS 4
#define LUMEN_RADIOSITY_RAY_COUNT 16
#define LUMEN_RADIOSITY_TILE_STRIDE 3
/// MaxFramesAccumulated with r.LumenScene.Radiosity.Temporal.
#define LUMEN_RADIOSITY_MAX_FRAMES 4.0
/// Ray start offsets: SurfaceBias 5 cm along the normal and the ray, MinTraceDistance 10 cm.
#define LUMEN_RADIOSITY_SURFACE_BIAS 0.05
#define LUMEN_RADIOSITY_MIN_TRACE_DISTANCE 0.1
/// Lumen's MaxTraceDistance (200 m).
#define LUMEN_RADIOSITY_MAX_TRACE_DISTANCE 200.0
/// MaxRayIntensity, in pre-exposed units.
#define LUMEN_RADIOSITY_MAX_RAY_INTENSITY 40.0
/// The plane test between a probe and a texel or neighbour: exp2(-100 rel^2) > 0.01.
#define LUMEN_RADIOSITY_PLANE_REJECT 0.01
#define LUMEN_RADIOSITY_PLANE_SCALE 100.0
#define LUMEN_RADIOSITY_PLANE_MIN_REL 0.1
#define LUMEN_TWO_PI 6.28318531

/// The probe jitter of update @p index: Hammersley16(i % 4, 4, (0x4ae4, 0x9bdb)) x 4.
ivec2 LumenRadiosityJitter(float index)
{
	int i = int(mod(index, 4.0));
	ivec2 jitter = ivec2(0, 1);
	if(i == 0)
	{
		jitter = ivec2(1, 2);
	}
	else if(i == 1)
	{
		jitter = ivec2(2, 0);
	}
	else if(i == 2)
	{
		jitter = ivec2(3, 3);
	}
	return jitter;
}

/// UE UniformSampleHemisphere: z = cos theta uniform in [0, 1).
vec3 LumenUniformSampleHemisphere(vec2 e)
{
	float phi = LUMEN_TWO_PI * e.x;
	float cos_theta = e.y;
	float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
	return vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
}

/// Frisvad's basis around @p n applied to a +z-hemisphere vector.
vec3 LumenTangentToWorld(vec3 n, vec3 v)
{
	vec3 tangent_x = vec3(0.0, -1.0, 0.0);
	vec3 tangent_y = vec3(-1.0, 0.0, 0.0);
	if(n.z >= -0.9999999)
	{
		float a = 1.0 / (1.0 + n.z);
		float b = -n.x * n.y * a;
		tangent_x = vec3(1.0 - n.x * n.x * a, b, -n.x);
		tangent_y = vec3(b, 1.0 - n.y * n.y * a, -n.y);
	}
	return tangent_x * v.x + tangent_y * v.y + n * v.z;
}

/// The direction of trace @p trace_texel of the probe at atlas cell @p probe_cell, update @p index.
vec3 LumenRadiosityRayDirection(vec3 normal, ivec2 probe_cell, ivec2 trace_texel, float index)
{
	vec2 noise = SpatioTemporalNoise2D(vec2(probe_cell), index);
	vec2 e = (vec2(trace_texel) + noise) / float(LUMEN_RADIOSITY_RAYS_PER_AXIS);
	return LumenTangentToWorld(normal, LumenUniformSampleHemisphere(e));
}

/// True when @p other lies within ~15 degrees of the tangent plane at @p position.
bool LumenRadiosityPlaneTest(vec3 position, vec3 normal, vec3 other)
{
	vec3 delta = other - position;
	float rel = max(abs(dot(normal, delta)) / (length(delta) + 0.0001), LUMEN_RADIOSITY_PLANE_MIN_REL);
	return exp2(-LUMEN_RADIOSITY_PLANE_SCALE * rel * rel) > LUMEN_RADIOSITY_PLANE_REJECT;
}

/// UE SHBasisFunction, two bands.
vec4 LumenSH2Basis(vec3 d)
{
	return vec4(0.282095, -0.488603 * d.y, 0.488603 * d.z, -0.488603 * d.x);
}

/// UE CalcDiffuseTransferSH(N, 1): the basis scaled by the cosine lobe's band factors (pi, 2pi/3).
vec4 LumenSH2DiffuseTransfer(vec3 n)
{
	return LumenSH2Basis(n) * vec4(3.14159265, 2.09439510, 2.09439510, 2.09439510);
}

#endif // __LUMEN_RADIOSITY_COMMON_SH__
