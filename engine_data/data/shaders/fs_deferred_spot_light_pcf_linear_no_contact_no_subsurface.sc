$input v_texcoord0

#define SPOT_LIGHT 1
#define SM_SPOT 1
#define SM_PCF 1
#define SM_LINEAR 1
// Without the contact shadow code, for a light without contact shadows (deferred::get_light_program).
#define PBR_CONTACT_SHADOWS 0
// Nor the subsurface code, for a run whose G-buffer holds no subsurface colour (deferred::get_light_program).
#define PBR_SUBSURFACE 0

#include "fs_pbr_lighting.sh"

void main()
{
    gl_FragColor = pbr_light(v_texcoord0, gl_FragCoord.xy);
}
