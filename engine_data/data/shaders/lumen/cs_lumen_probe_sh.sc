/*
 * Lumen screen probe gather, SH3 projection (UE 5.8 ScreenProbeConvertToIrradianceCS,
 * LumenScreenProbeFiltering.usf:597-998, IrradianceFormat 0 at Epic). One 8x8 group per probe:
 * the 64 filtered radiance texels, at their texel-centre directions (equal solid angle each), project
 * onto the nine SH3 basis functions with uniform 1/64 weights - 1/(4 pi) of the true projection, which
 * the integrate's 4 pi undoes.
 *
 * Writes LUMEN_SH_TEXELS_PER_PROBE texels per probe along x: [0] = (c0 rgb, 0), then per colour
 * channel c: [1 + 2c] = (c1, c2, c3, c4), [2 + 2c] = (c5, c6, c7, c8). Unlit probes write zeros.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"

SAMPLER2D(s_lumen_probe_filtered, 0);
SAMPLER2D(s_lumen_probe_records, 1);
IMAGE2D_WO(s_lumen_probe_sh, rgba16f, 2);

#define LUMEN_PROBE_TEXELS (LUMEN_PROBE_TRACE_RES * LUMEN_PROBE_TRACE_RES)
#define LUMEN_SH3_COEFFICIENTS 9
#define LUMEN_SH3_CHANNEL_COEFFICIENTS (3 * LUMEN_SH3_COEFFICIENTS)

SHARED vec3 s_radiance[LUMEN_PROBE_TEXELS];
SHARED float s_basis[LUMEN_PROBE_TEXELS * LUMEN_SH3_COEFFICIENTS];
SHARED float s_coefficients[LUMEN_SH3_CHANNEL_COEFFICIENTS];

/// Stores the nine basis values of one texel's direction in s_basis.
void LumenStoreBasis(int texel_index, vec3 direction)
{
	LumenSH3 basis = LumenSHBasis3(direction);
	int base = texel_index * LUMEN_SH3_COEFFICIENTS;
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

float LumenChannel(vec3 value, int channel)
{
	return channel == 0 ? value.x : (channel == 1 ? value.y : value.z);
}

/// One channel's coefficient @p k projected over the 64 texels, with the uniform 1/64 weight.
float LumenProjectCoefficient(int channel, int k)
{
	float sum = 0.0;
	for(int t = 0; t < LUMEN_PROBE_TEXELS; ++t)
	{
		sum += LumenChannel(s_radiance[t], channel) * s_basis[t * LUMEN_SH3_COEFFICIENTS + k];
	}
	return sum * (1.0 / float(LUMEN_PROBE_TEXELS));
}

/// Storage texel @p slot of the probe (see the header for the layout).
vec4 LumenPackShTexel(int slot)
{
	if(slot == 0)
	{
		return vec4(s_coefficients[0], s_coefficients[LUMEN_SH3_COEFFICIENTS], s_coefficients[2 * LUMEN_SH3_COEFFICIENTS], 0.0);
	}
	int channel = (slot - 1) / 2;
	int first = channel * LUMEN_SH3_COEFFICIENTS + ((slot - 1) % 2 == 0 ? 1 : 5);
	return vec4(s_coefficients[first], s_coefficients[first + 1], s_coefficients[first + 2], s_coefficients[first + 3]);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 tile = ivec2(gl_WorkGroupID.xy);
	ivec2 texel = ivec2(gl_LocalInvocationID.xy);
	int index = texel.y * LUMEN_PROBE_TRACE_RES + texel.x;
	// Barriers stay in uniform flow control: an invalid probe projects zeros instead of returning.
	vec4 record = texelFetch(s_lumen_probe_records, tile, 0);
	bool valid = record.x > 0.0;
	vec3 radiance = texelFetch(s_lumen_probe_filtered, tile * LUMEN_PROBE_TRACE_RES + texel, 0).xyz;
	s_radiance[index] = valid ? radiance : vec3_splat(0.0);
	LumenStoreBasis(index, LumenEquiAreaSphericalMapping((vec2(texel) + 0.5) / float(LUMEN_PROBE_TRACE_RES)));
	barrier();
	if(index < LUMEN_SH3_CHANNEL_COEFFICIENTS)
	{
		s_coefficients[index] = LumenProjectCoefficient(index / LUMEN_SH3_COEFFICIENTS, index % LUMEN_SH3_COEFFICIENTS);
	}
	barrier();
	if(index < LUMEN_SH_TEXELS_PER_PROBE)
	{
		imageStore(s_lumen_probe_sh, ivec2(tile.x * LUMEN_SH_TEXELS_PER_PROBE + index, tile.y), LumenPackShTexel(index));
	}
}
