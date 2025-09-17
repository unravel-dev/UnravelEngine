$input v_texcoord0, v_color0

#include "../common.sh"

#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265
#define MAX_NUM_STOPS 16

uniform vec4 u_gradient_func;     // .x = function type (int)
uniform vec4 u_gradient_p;        // .xy = starting point/center
uniform vec4 u_gradient_v;        // .xy = vector/curvature/angle
uniform vec4 u_gradient_stops[MAX_NUM_STOPS];      // stop colors
uniform vec4 u_gradient_positions[MAX_NUM_STOPS/4]; // stop positions (packed)
uniform vec4 u_gradient_num_stops; // .x = number of stops

vec4 mix_stop_colors(float t) 
{
    vec4 color = u_gradient_stops[0];
    int num_stops = int(u_gradient_num_stops.x);
    
    for (int i = 1; i < MAX_NUM_STOPS && i < num_stops; i++)
    {
        float prev_pos = u_gradient_positions[max(0, (i-1)/4)][max(0, (i-1)%4)];
        float curr_pos = u_gradient_positions[i/4][i%4];
        color = mix(color, u_gradient_stops[i], smoothstep(prev_pos, curr_pos, t));
    }
    
    return color;
}

void main()
{
    float t = 0.0;
    int func = int(u_gradient_func.x);
    vec2 p = u_gradient_p.xy;
    vec2 v = u_gradient_v.xy;
    
    if (func == LINEAR || func == REPEATING_LINEAR)
    {
        float dist_square = dot(v, v);
        vec2 V = v_texcoord0 - p;
        t = dot(v, V) / dist_square;
    }
    else if (func == RADIAL || func == REPEATING_RADIAL)
    {
        vec2 V = v_texcoord0 - p;
        t = length(v * V);
    }
    else if (func == CONIC || func == REPEATING_CONIC)
    {
        mat2 R = mat2(v.x, -v.y, v.y, v.x);
        vec2 V = mul(R, (v_texcoord0 - p));
        t = 0.5 + atan2(-V.x, V.y) / (2.0 * PI);
    }
    
    if (func == REPEATING_LINEAR || func == REPEATING_RADIAL || func == REPEATING_CONIC)
    {
        float t0 = u_gradient_positions[0].x;
        int num_stops = int(u_gradient_num_stops.x);
        float t1 = u_gradient_positions[max(0, (num_stops-1)/4)][max(0, (num_stops-1)%4)];
        t = t0 + mod(t - t0, t1 - t0);
    }
    
    gl_FragColor = v_color0 * mix_stop_colors(t);
}
