$input v_texcoord0

#define SPOT_LIGHT 1
#define SM_SPOT 1
#define SM_PCSS 1

#include "fs_pbr_lighting.sh"

void main()
{
    vec4 shadowed, unshadowed;
    pbr_light(v_texcoord0, gl_FragCoord.xy, shadowed, unshadowed);
    gl_FragData[0] = shadowed;
    gl_FragData[1] = unshadowed;
}
