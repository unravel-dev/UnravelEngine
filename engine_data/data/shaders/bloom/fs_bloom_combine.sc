$input v_texcoord0

/*
 * Bloom combine pass.
 * Reference: BGFX 38-bloom (https://github.com/bkaradzic/bgfx/tree/master/examples/38-bloom)
 * Adds bloom to the original HDR scene. Tonemapping is applied separately.
 */

#include "../common.sh"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);
SAMPLER2D(s_dirt, 2);

uniform vec4 u_combineParams;
// Assembled-pyramid tint * weight (mip0_tint.rgb * mip0_tint.a): the upsample
// cascade indexes tints by SOURCE mip (1..N-1), so the half-res band has no hop
// of its own -- its tint applies here, to the assembled pyramid.
uniform vec4 u_combineTint0;
#define u_bloom_intensity u_combineParams.x
#define u_dirt_intensity  u_combineParams.z

void main()
{
    vec3 scene_color = texture2D(s_scene, v_texcoord0).rgb;
    vec3 bloom_color = texture2D(s_bloom, v_texcoord0).rgb * u_combineTint0.rgb;

    // One combine for both modes; they differ only in what the pyramid holds
    // (scatter: energy-normalized blur of the whole scene; legacy: thresholded
    // highlights). Additive keeps the base image SHARP at any intensity -- a
    // mix() formulation was tried and rejected: it couples halo strength to
    // full-screen blur, so halos vanish at low intensity and the image frosts
    // at high. Auto-exposure meters BEFORE this pass, so scatter intensity is
    // a small global lift on top of the locked exposure (part of the look).
    vec3 hdr_color = scene_color + bloom_color * u_bloom_intensity;

    // Lens dirt: bloom modulated by the screen-space mask, added on top. A black
    // (unassigned) mask makes this an exact no-op.
    hdr_color += bloom_color * texture2D(s_dirt, v_texcoord0).rgb * u_dirt_intensity;

    hdr_color = min(max(hdr_color, vec3_splat(0.0)), vec3_splat(65504.0));

    gl_FragColor = vec4(hdr_color, 1.0);
}
