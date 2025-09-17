$input v_texcoord0, v_color0

#include "../common.sh"

uniform vec4 u_value;      // .x = time value
uniform vec4 u_dimensions; // .xy = dimensions

void main()
{
    float t = u_value.x;
    vec2 dimensions = u_dimensions.xy;
    vec3 c;
    float l;
    
    for (int i = 0; i < 3; i++) {
        vec2 p = v_texcoord0;
        vec2 uv = p;
        p -= 0.5;
        p.x *= dimensions.x / dimensions.y;
        float z = t + float(i) * 0.07;
        l = length(p);
        uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));
        c[i] = 0.01 / length(mod(uv, 1.0) - 0.5);
    }
    
    gl_FragColor = vec4(c / l, v_color0.a);
}
