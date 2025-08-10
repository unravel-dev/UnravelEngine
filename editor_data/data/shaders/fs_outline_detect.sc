$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// (1.0/width, 1.0/height)
uniform vec4 u_data;
uniform vec4 u_outline_color;


void main()
{
    vec2 uv = v_texcoord0;
	float c0 = texture2D(s_tex, uv).r;
	if (c0 < 0.5)
	{
	    discard;
	}

    vec2 offset = u_data.xy;

    // Check neighbors from 1..T
	for(int i = 1; i <= 2; ++i)
	{
	    vec2 off = vec2(offset.x * i, offset.y * i);
		if (texture2D(s_tex, uv + vec2(-off.x,  0.0    )).r < 0.5 ||
		    texture2D(s_tex, uv + vec2( off.x,  0.0    )).r < 0.5 ||
			texture2D(s_tex, uv + vec2( 0.0,    -off.y )).r < 0.5 ||
			texture2D(s_tex, uv + vec2( 0.0,     off.y )).r < 0.5
			)
		{
		    gl_FragColor = u_outline_color;
			return;
			}
		}
	discard;
}
