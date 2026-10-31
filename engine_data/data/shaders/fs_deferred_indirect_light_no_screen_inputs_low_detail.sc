$input v_texcoord0

#define DIRECTIONAL_LIGHT 1
#define SM_NOOP 1
#define PBR_INDIRECT 1
// For a view without screen-space AO, a GI or SSIL estimate, or traced reflections (fs_pbr_lighting.sh).
#define PBR_INDIRECT_SCREEN_INPUTS 0
// Two SH bands and no background pixels, for a run whose limits leave the detail out (lighting.sh).
#define PBR_INDIRECT_DETAIL 0

#include "fs_pbr_lighting.sh"

void main()
{
    gl_FragColor = pbr_indirect(v_texcoord0, gl_FragCoord.xy);
}
