$input v_texcoord0

#include "../common.sh"
#include "tonemapping.sh"

uniform vec4 u_tonemapping;

SAMPLER2D(s_input, 0);
SAMPLER2D(s_exposure, 1);

#define u_tonemappingExposure u_tonemapping.x
#define u_tonemappingMode     int(u_tonemapping.y)

void main()
{
    vec3 color = texture2D(s_input, v_texcoord0).rgb;

    float exposure = u_tonemappingExposure;
    float adapted = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0).r;
    exposure *= max(adapted, 1e-5);

    color = apply_tonemapping(color, u_tonemappingMode, exposure);

    gl_FragColor = vec4(color, 1.0f);
}
