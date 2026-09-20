#ifndef __GI_REFLECTION_SAMPLING_SH__
#define __GI_REFLECTION_SAMPLING_SH__

/*
 * Specular lobe sampling for the reflection tier, shared by the TRACE (which fires the ray)
 * and by its temporal RESOLVE (which has to know, per neighbouring texel, exactly which ray
 * was fired and at what density). Two copies of this maths would drift silently and the
 * resolve's weights would then describe a lobe nobody sampled - hence one header.
 *
 * SAMPLER: visible-normal GGX in the bounded spherical-cap form - the cap parameterisation
 * is Dupuy & Benyoub (HPG 2023) and the `k` bound is Eto & Tokuyoshi (Siggraph Asia 2023),
 * which is what UE 5.8 ships (MonteCarlo.ush:386-471, GGX_BOUNDED_VNDF_SAMPLING on by
 * default). `k` shrinks the sampled cap so a half-vector whose REFLECTION would fall below
 * the horizon is never drawn. The Heitz 2018 disk form this replaces had no such bound: at
 * grazing angles it produced below-horizon reflections that the caller discarded by keeping
 * the mirror direction, which piled probability mass on a single direction and biased
 * exactly the geometry where reflections matter most (floors, wet ground). The cap form is
 * also CHEAPER - one lerp, one sqrt, one sincos, against the disk remap plus its `s` blend.
 *
 * NOISE CONVENTION: u1 drives the azimuth, u2 the polar extent (u2 = 0 is the specular
 * peak), matching UE's E.x / E.y so a Lumen-style sampling bias on u2 stays portable.
 *
 * shaderc: NO `out` parameters in this file. They miscompile silently on the HLSL path
 * (tasks/lessons.md) - everything returns by value, structs included.
 */

#include "gi/gi_constants.sh"

#define GI_REFLECTION_TWO_PI 6.283185307
#define GI_REFLECTION_PI     3.141592653

/// Tangent frame with z = the shading normal. Built identically on both sides so the
/// resolve reproduces the trace's sample bit for bit.
struct GiReflectionBasis
{
	vec3 tangent;
	vec3 bitangent;
	vec3 normal;
};

GiReflectionBasis GiReflectionMakeBasis(vec3 normal)
{
	vec3 axis;
	if(abs(normal.z) < 0.999)
	{
		axis = vec3(0.0, 0.0, 1.0);
	}
	else
	{
		axis = vec3(1.0, 0.0, 0.0);
	}
	GiReflectionBasis basis;
	basis.tangent = normalize(cross(axis, normal));
	basis.bitangent = cross(normal, basis.tangent);
	basis.normal = normal;
	return basis;
}

vec3 GiReflectionToTangent(GiReflectionBasis basis, vec3 v)
{
	return vec3(dot(v, basis.tangent), dot(v, basis.bitangent), dot(v, basis.normal));
}

vec3 GiReflectionToWorld(GiReflectionBasis basis, vec3 v)
{
	return basis.tangent * v.x + basis.bitangent * v.y + basis.normal * v.z;
}

/// The spherical-cap bound (Eto & Tokuyoshi 2023, eq. 5). k = 1 restores the unbounded
/// Dupuy & Benyoub cap; the sampler and the density below must use the SAME k or the
/// resolve's weights are wrong.
float GiReflectionVndfCapBound(vec3 view_ts, float alpha)
{
	float a2 = alpha * alpha;
	float s = 1.0 + length(view_ts.xy);
	float s2 = s * s;
	return (s2 - a2 * s2) / max(s2 + a2 * view_ts.z * view_ts.z, 1e-8);
}

/// GGX (Trowbridge-Reitz) normal distribution, alpha = roughness^2.
float GiReflectionNdf(float alpha, float n_dot_h)
{
	float a2 = alpha * alpha;
	float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1.0;
	return a2 / max(GI_REFLECTION_PI * d * d, 1e-12);
}

/// One visible-normal sample; view and result in tangent space (z = normal).
vec3 GiReflectionSampleGgxVndf(vec3 view_ts, float alpha, float u1, float u2)
{
	vec3 vh = normalize(vec3(alpha * view_ts.x, alpha * view_ts.y, view_ts.z));
	float k = GiReflectionVndfCapBound(view_ts, alpha);
	float phi = GI_REFLECTION_TWO_PI * u1;
	float z = mix(1.0, -k * vh.z, u2);
	float sin_theta = sqrt(saturate(1.0 - z * z));
	vec3 h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), z) + vh;
	return normalize(vec3(alpha * h.x, alpha * h.y, max(1e-6, h.z)));
}

/// Solid-angle density of the sampler above, evaluated at `half_ts`. The resolve divides a
/// neighbour's radiance by this to re-weight its sample under the CENTRE pixel's lobe.
float GiReflectionVndfPdf(vec3 view_ts, vec3 half_ts, float alpha)
{
	float a2 = alpha * alpha;
	float n_dot_v = view_ts.z;
	float v_dot_h = max(dot(view_ts, half_ts), 0.0);
	float d = GiReflectionNdf(alpha, half_ts.z);
	float k = GiReflectionVndfCapBound(view_ts, alpha);
	float denom = k * n_dot_v + sqrt(max(n_dot_v * (n_dot_v - n_dot_v * a2) + a2, 0.0));
	return (2.0 * v_dot_h * d) / max(denom, 1e-8);
}

struct GiReflectionRay
{
	vec3 direction;
	float pdf;
};

/*
 * The ray one texel fires, and its density. `roughness` is the RAW authored value (the
 * mirror gate keys on the encoder floor - see the trace kernel), `view` points from the
 * surface to the camera, `xi` is the texel's jitter pair.
 *
 * At or below GI_REFLECTION_MIRROR_ROUGHNESS the ray is deterministic (the determinism
 * gate the temporal also keys on) and the density is the peak of a lobe floored at
 * GI_REFLECTION_MIN_LOBE_ALPHA - large but finite, so a mirror neighbour correctly carries
 * almost no weight in a glossy pixel's resolve instead of dividing by zero.
 */
GiReflectionRay GiReflectionMakeRay(vec3 normal, vec3 view, float roughness, vec2 xi)
{
	GiReflectionRay ray;
	ray.direction = normalize(reflect(-view, normal));

	float alpha = max(roughness * roughness, GI_REFLECTION_MIN_LOBE_ALPHA);
	GiReflectionBasis basis = GiReflectionMakeBasis(normal);
	vec3 view_ts = GiReflectionToTangent(basis, view);
	// The half-vector the returned direction actually corresponds to; (0,0,1) is the mirror
	// case, whose half-vector IS the normal.
	vec3 half_ts = vec3(0.0, 0.0, 1.0);

	// A normal-mapped texel can face away from the view; the cap is undefined there and the
	// stretched frame degenerates. Keep the mirror answer rather than emit a NaN.
	BRANCH
	if(roughness > GI_REFLECTION_MIRROR_ROUGHNESS && view_ts.z > 1e-4)
	{
		vec3 sampled_ts = GiReflectionSampleGgxVndf(view_ts, alpha, xi.x, xi.y);
		vec3 half_ws = normalize(GiReflectionToWorld(basis, sampled_ts));
		vec3 jittered = reflect(-view, half_ws);
		// The cap bound makes this practically unreachable; it stays as a NaN guard, not as
		// the rejection strategy it used to be.
		if(dot(jittered, normal) > 1e-3)
		{
			ray.direction = normalize(jittered);
			half_ts = sampled_ts;
		}
	}

	ray.pdf = max(GiReflectionVndfPdf(view_ts, half_ts, alpha), 1e-8);
	return ray;
}

/// BRDF-over-pdf weight of a sample that arrives along `direction` at a surface whose lobe
/// is (normal, alpha), given the density `pdf` the sample was actually drawn from. This is
/// Lumen's resolve weight (LumenReflectionResolve.usf:58-71): it is what turns the
/// pre-temporal 3x3 from a blur into a reuse of neighbouring rays under this pixel's lobe.
float GiReflectionSampleWeight(vec3 normal, vec3 view, float alpha, vec3 direction, float pdf)
{
	vec3 half_vector = view + direction;
	float half_len = length(half_vector);
	if(half_len < 1e-4)
	{
		return 0.0;
	}
	float n_dot_h = dot(normal, half_vector / half_len);
	return GiReflectionNdf(alpha, n_dot_h) / max(pdf, 1e-8);
}

#endif // __GI_REFLECTION_SAMPLING_SH__
