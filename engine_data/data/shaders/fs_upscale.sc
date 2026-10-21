$input v_texcoord0

/*
 * The upscale of a scene rendered at a fraction of its view's size: a 5-tap Catmull-Rom filter, the
 * bicubic's sharpness from five bilinear taps (the corners of its 4x4 footprint, which weigh little, left
 * out), then the film grain and dither the tone curve left for the output's resolution.
 */

#include "common.sh"
#include "tonemapping/output_noise.sh"

SAMPLER2D(s_input, 0);

// xy: the input's size in texels, zw: its reciprocal.
uniform vec4 u_upscale_input_size;
uniform vec4 u_output_noise;
#define u_grain_amount u_output_noise.x
#define u_grain_seed   u_output_noise.y
#define u_dithering    u_output_noise.z

vec3 sample_catmull_rom(vec2 uv)
{
	vec2 position = uv * u_upscale_input_size.xy;
	vec2 center = floor(position - 0.5) + 0.5;
	vec2 f = position - center;
	vec2 f2 = f * f;
	vec2 f3 = f2 * f;
	vec2 w0 = f2 - 0.5 * (f3 + f);
	vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
	vec2 w3 = 0.5 * (f3 - f2);
	vec2 w2 = 1.0 - w0 - w1 - w3;
	// The two middle taps merge into one bilinear tap placed between them by their weights.
	vec2 w12 = w1 + w2;
	vec2 texel = u_upscale_input_size.zw;
	vec2 uv0 = (center - 1.0) * texel;
	vec2 uv12 = (center + w2 / w12) * texel;
	vec2 uv3 = (center + 2.0) * texel;
	vec3 color = texture2DLod(s_input, vec2(uv12.x, uv0.y), 0.0).rgb * (w12.x * w0.y)
	           + texture2DLod(s_input, vec2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y)
	           + texture2DLod(s_input, uv12, 0.0).rgb * (w12.x * w12.y)
	           + texture2DLod(s_input, vec2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y)
	           + texture2DLod(s_input, vec2(uv12.x, uv3.y), 0.0).rgb * (w12.x * w3.y);
	float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
	return color / weight;
}

void main()
{
	// Catmull-Rom overshoots at hard edges: the display-encoded result stays in range.
	vec3 color = saturate(sample_catmull_rom(v_texcoord0.xy));
	gl_FragColor = vec4(apply_output_noise(color, gl_FragCoord.xy, u_grain_amount, u_grain_seed, u_dithering), 1.0);
}
