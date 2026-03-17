$input v_texcoord0

/*
 * Bloom combine pass.
 * Reference: BGFX 38-bloom (https://github.com/bkaradzic/bgfx/tree/master/examples/38-bloom)
 * Adds bloom to the original HDR scene. Tonemapping is applied separately.
 */

#include "../common.sh"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);

void main()
{
    vec3 scene_color = texture2D(s_scene, v_texcoord0).rgb;
    vec3 bloom_color = texture2D(s_bloom, v_texcoord0).rgb;

    vec3 hdr_color = scene_color + bloom_color;
    hdr_color = min(max(hdr_color, vec3_splat(0.0)), vec3_splat(65504.0));

    gl_FragColor = vec4(hdr_color, 1.0);
}
