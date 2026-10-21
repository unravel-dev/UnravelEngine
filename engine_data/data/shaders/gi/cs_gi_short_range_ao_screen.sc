/*
 * The screen AO the lighting reads (s_screen_ao: rgb = the bent normal * 0.5 + 0.5, a = the visibility), unpacked
 * from this frame's accumulation (cs_gi_integrate_ao.sc), in its own dispatch: the integrate gives the screen AO's
 * image stage to the pre-exposure.
 * Sky pixels are left as they are, like the integrate leaves them.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "gi/gi_common.sh"
#include "gi/gi_short_range_ao.sh"

SAMPLER2D(s_gi_depth, 0);
SAMPLER2D(s_gi_normal, 1);
/// This frame's accumulation, packed (GiPackShortRangeAO).
USAMPLER2D(s_gi_short_range_ao_accumulation, 2);
// Images take i_ names, never a sampler's: the OpenGL backend uploads every registered uniform
// a program declares, so an image named like a sampler is rebound to that sampler's stage.
IMAGE2D_WO(i_gi_short_range_ao_screen, rgba8, 3);

/// Below this visibility the packed bent normal carries no direction: the surface normal stands in.
#define GI_SHORT_RANGE_AO_SCREEN_MIN_VISIBILITY 1e-4

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if(pixel.x >= int(u_gi_view_size.x) || pixel.y >= int(u_gi_view_size.y))
	{
		return;
	}
	if(texelFetch(s_gi_depth, pixel, 0).x >= 1.0)
	{
		return;
	}
	vec4 ao = GiUnpackShortRangeAO(texelFetch(s_gi_short_range_ao_accumulation, pixel, 0).x);
	vec3 bent = ao.xyz;
	if(ao.w <= GI_SHORT_RANGE_AO_SCREEN_MIN_VISIBILITY)
	{
		bent = decodeNormalOctahedron(texelFetch(s_gi_normal, pixel, 0).xy);
	}
	imageStore(i_gi_short_range_ao_screen, pixel, vec4(bent * 0.5 + 0.5, saturate(ao.w)));
}
