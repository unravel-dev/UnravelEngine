$input v_texcoord0, v_weye_dir

#include "../common.sh"
#include "../lighting.sh"
#include "sphere_probe.sh"
#include "../pre_exposure.sh"

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
SAMPLERCUBE(s_tex_cube, 5);

uniform vec4 u_data0;
uniform vec4 u_data1;
uniform vec4 u_capture;

#define u_probe_position_and_radius u_data0
#define u_cube_mips u_data1.x
// The CPU scaled the intensity by its share of the pre-exposure; the GPU's share joins it here.
#define u_intensity (u_data1.y * u_pre_exposure_gpu_scale)
#define u_is_global_fallback u_data1.z
#define u_source_validity u_data1.w
#define u_apply_prefilter u_capture.x

void main()
{
    GBufferData data = DecodeGBuffer(v_texcoord0, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
	
	vec3 clip = vec3(v_texcoord0 * 2.0 - 1.0, data.depth);
	clip = clipTransform(clip);
	vec3 world_position = clipToWorld(u_invViewProj, clip);

	vec3 N = data.world_normal;
	vec3 V = -normalize(v_weye_dir);
	vec3 R = reflect(-V, N);
	
	vec4 color = SampleSphereProbe(s_tex_cube, world_position, R, data.roughness, u_probe_position_and_radius,
	                               u_cube_mips, u_intensity, u_apply_prefilter);
	color.a *= u_source_validity;

	// Unoccluded: this is the probe layer (PBUFFER), which the GI reflection trace reads as the
	// open sky and the indirect pass occludes (ComposeIndirectSpecular).
	gl_FragColor = color;
}
