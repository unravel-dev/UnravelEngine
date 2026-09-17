$input v_texcoord0

/*
 * Exposure readback: one bit per occlusion query.
 *
 * bgfx has no non-blocking texture readback, but occlusion queries resolve asynchronously on
 * every backend. auto_exposure_pass draws this shader into a 1x1 target once per query; the
 * pixel survives (the query reads visible) exactly when the requested bit is set. Tag bits
 * come straight from the CPU; code bits encode log2(exposure x average local exposure) from
 * AUTO_EXPOSURE on a fixed range, so the CPU reassembles the value a few frames later - the
 * async eye adaptation readback UE builds its pre-exposure from.
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_exposure, 0);

// x = 1 for a code bit, 0 for a tag bit; y = bit index; z = the tag bit's value.
uniform vec4 u_readback_bit;
// x = log2 of the lowest encodable value, y = codes per stop, z = highest code.
uniform vec4 u_readback_range;

#define u_is_code_bit     u_readback_bit.x
#define u_bit_index       u_readback_bit.y
#define u_tag_bit_value   u_readback_bit.z
#define u_min_log2        u_readback_range.x
#define u_codes_per_stop  u_readback_range.y
#define u_max_code        u_readback_range.z

void main()
{
    float bit_value = u_tag_bit_value;
    if (u_is_code_bit > 0.5)
    {
        vec4 exposure = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0);
        float value = max(exposure.r, 1e-30) * max(exposure.a, 1e-30);
        float code = clamp(floor((log2(value) - u_min_log2) * u_codes_per_stop + 0.5), 0.0, u_max_code);
        bit_value = mod(floor(code / pow(2.0, u_bit_index)), 2.0);
    }
    if (bit_value < 0.5)
    {
        discard;
    }
    gl_FragColor = vec4_splat(1.0);
}
