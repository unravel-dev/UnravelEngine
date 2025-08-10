$input v_texcoord0, v_color0

/*
 * Copyright 2011-2025 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 texColor = texture2D(s_texColor, v_texcoord0);
    if(texColor.a < 0.1f)
    {
        discard;
    }
    gl_FragColor = v_color0;
}
