$input v_texcoord0

#define DIRECTIONAL_LIGHT 1
#define SM_NOOP 1
#define PBR_INDIRECT 1

#include "fs_pbr_lighting.sh"

void main()
{
    gl_FragColor = pbr_indirect(v_texcoord0, gl_FragCoord.xy);
}
