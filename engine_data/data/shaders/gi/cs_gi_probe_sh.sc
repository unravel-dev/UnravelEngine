/*
 * Screen probe gather, SH3 projection. One group per probe of one thread per texel: the N^2 =
 * GI_PROBE_TRACE_RES^2 filtered radiance texels, at their texel-centre directions (equal solid angle each),
 * project onto the nine SH3 basis functions with uniform 1/N^2 weights - 1/(4 pi) of the true projection, which the
 * integrate's 4 pi undoes.
 *
 * Writes GI_SH_TEXELS_PER_PROBE texels per probe along x: [0] = (c0 rgb, the probe's moving fraction), then per
 * colour channel c: [1 + 2c] = (c1, c2, c3, c4), [2 + 2c] = (c5, c6, c7, c8). Unlit probes write zero coefficients.
 * The moving fraction (cs_gi_probe_composite.sc) rides along for the integrate, which reads the four texels [0] of
 * its probes anyway.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"

SAMPLER2D(s_gi_probe_filtered, 0);
SAMPLER2D(s_gi_probe_records, 1);
IMAGE2D_WO(i_gi_probe_sh, rgba16f, 2);
/// One texel per probe: the fraction of its rays that hit a moving surface.
SAMPLER2D(s_gi_probe_moving, 3);

#define GI_PROBE_TEXELS (GI_PROBE_TRACE_RES * GI_PROBE_TRACE_RES)
#define GI_SH3_COEFFICIENTS 9
#define GI_SH3_CHANNEL_COEFFICIENTS (3 * GI_SH3_COEFFICIENTS)

SHARED vec3 s_radiance[GI_PROBE_TEXELS];
SHARED float s_basis[GI_PROBE_TEXELS * GI_SH3_COEFFICIENTS];
SHARED float s_coefficients[GI_SH3_CHANNEL_COEFFICIENTS];

/// Stores the nine basis values of one texel's direction in s_basis.
void GiStoreBasis(int texel_index, vec3 direction)
{
	GiSH3 basis = GiSHBasis3(direction);
	int base = texel_index * GI_SH3_COEFFICIENTS;
	s_basis[base + 0] = basis.v0.x;
	s_basis[base + 1] = basis.v0.y;
	s_basis[base + 2] = basis.v0.z;
	s_basis[base + 3] = basis.v0.w;
	s_basis[base + 4] = basis.v1.x;
	s_basis[base + 5] = basis.v1.y;
	s_basis[base + 6] = basis.v1.z;
	s_basis[base + 7] = basis.v1.w;
	s_basis[base + 8] = basis.v2;
}

float GiChannel(vec3 value, int channel)
{
	return channel == 0 ? value.x : (channel == 1 ? value.y : value.z);
}

/// One channel's coefficient @p k projected over the probe's texels, with the uniform 1/N^2 weight.
float GiProjectCoefficient(int channel, int k)
{
	float sum = 0.0;
	for(int t = 0; t < GI_PROBE_TEXELS; ++t)
	{
		sum += GiChannel(s_radiance[t], channel) * s_basis[t * GI_SH3_COEFFICIENTS + k];
	}
	return sum * (1.0 / float(GI_PROBE_TEXELS));
}

/// Storage texel @p slot of the probe at @p tile (see the header for the layout).
vec4 GiPackShTexel(int slot, ivec2 tile)
{
	if(slot == 0)
	{
		return vec4(s_coefficients[0],
		            s_coefficients[GI_SH3_COEFFICIENTS],
		            s_coefficients[2 * GI_SH3_COEFFICIENTS],
		            texelFetch(s_gi_probe_moving, tile, 0).x);
	}
	int channel = (slot - 1) / 2;
	int first = channel * GI_SH3_COEFFICIENTS + ((slot - 1) % 2 == 0 ? 1 : 5);
	return vec4(s_coefficients[first], s_coefficients[first + 1], s_coefficients[first + 2], s_coefficients[first + 3]);
}

NUM_THREADS(GI_PROBE_TRACE_RES, GI_PROBE_TRACE_RES, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	int index = texel.y * GI_PROBE_TRACE_RES + texel.x;
	// Barriers stay in uniform flow control: an invalid probe projects zeros instead of returning.
	vec4 record = texelFetch(s_gi_probe_records, tile, 0);
	bool valid = record.x > 0.0;
	vec3 radiance = texelFetch(s_gi_probe_filtered, tile * GI_PROBE_TRACE_RES + texel, 0).xyz;
	s_radiance[index] = valid ? radiance : vec3_splat(0.0);
	GiStoreBasis(index, GiEquiAreaSphericalMapping((vec2(texel) + 0.5) / float(GI_PROBE_TRACE_RES)));
	barrier();
	// 27 coefficients: a 4 x 4 group projects up to two per thread.
	for(int coefficient = index; coefficient < GI_SH3_CHANNEL_COEFFICIENTS; coefficient += GI_PROBE_TEXELS)
	{
		s_coefficients[coefficient] =
		    GiProjectCoefficient(coefficient / GI_SH3_COEFFICIENTS, coefficient % GI_SH3_COEFFICIENTS);
	}
	barrier();
	if(index < GI_SH_TEXELS_PER_PROBE)
	{
		imageStore(i_gi_probe_sh, ivec2(tile.x * GI_SH_TEXELS_PER_PROBE + index, tile.y), GiPackShTexel(index, tile));
	}
}
