$input v_texcoord0

/*
 * Pre-exposure pass: scales the HDR scene by the adapted exposure value
 * before bloom. This ensures bloom operates on perceptually meaningful
 * values regardless of absolute scene brightness.
 */

#include "../common.sh"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_exposure, 1);

void main()
{
    vec3 color = texture2D(s_scene, v_texcoord0).rgb;
    float exposure = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0).r;

    gl_FragColor = vec4(color * max(exposure, 1e-5), 1.0);
}
