$input v_texcoord0

#include "../common.sh"

SAMPLER2D(s_tex, 0);
uniform vec4 u_texCoordMin;  // Minimum texture coordinates
uniform vec4 u_texCoordMax;  // Maximum texture coordinates
uniform vec4 u_color;        // Shadow color

void main()
{
    vec2 in_region = step(u_texCoordMin.xy, v_texcoord0) * step(v_texcoord0, u_texCoordMax.xy);
    float alpha = texture2D(s_tex, v_texcoord0).a;
    gl_FragColor = alpha * in_region.x * in_region.y * u_color;
}
