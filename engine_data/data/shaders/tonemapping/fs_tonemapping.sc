$input v_texcoord0

#include "../common.sh"
#include "tonemapping.sh"

uniform vec4 u_tonemapping;

SAMPLER2D(s_input, 0);

#define u_tonemappingExposure u_tonemapping.x
#define u_tonemappingMode int(u_tonemapping.y)

void main()
{
    vec3 color = texture2D(s_input, v_texcoord0).rgb;

    // Apply tonemapping using the centralized function
    color = apply_tonemapping(color, u_tonemappingMode, u_tonemappingExposure);

    gl_FragColor = vec4(color, 1.0f);
}
