$input v_texcoord0

#include "../common.sh"

SAMPLER2D(s_tex, 0);
uniform mat4 u_color_matrix;

void main()
{
    // The general case uses a 4x5 color matrix for full rgba transformation, plus a constant term with the last column.
    // However, we only consider the case of rgb transformations. Thus, we could in principle use a 3x4 matrix, but we
    // keep the alpha row for simplicity.
    // In the general case we should do the matrix transformation in non-premultiplied space. However, without alpha
    // transformations, we can do it directly in premultiplied space to avoid the extra division and multiplication
    // steps. In this space, the constant term needs to be multiplied by the alpha value, instead of unity.
    vec4 texColor = texture2D(s_tex, v_texcoord0);
    vec3 transformedColor = mul(u_color_matrix, texColor).xyz;
    gl_FragColor = vec4(transformedColor, texColor.a);
}
