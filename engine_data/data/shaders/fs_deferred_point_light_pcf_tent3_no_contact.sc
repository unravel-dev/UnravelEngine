$input v_texcoord0

#define POINT_LIGHT 1
#define SM_OMNI 1
#define SM_PCF 1
#define PCF_TENT_SIZE 3
// Without the contact shadow code, for a light without contact shadows (deferred::get_light_program).
#define PBR_CONTACT_SHADOWS 0

#include "fs_pbr_lighting.sh"

void main()
{
    gl_FragColor = pbr_light(v_texcoord0, gl_FragCoord.xy);
}
