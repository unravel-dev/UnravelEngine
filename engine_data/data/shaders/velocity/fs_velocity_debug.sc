$input v_texcoord0

#include "../common.sh"

SAMPLER2D(s_velocity, 0);

uniform vec4 u_params;
// Pixels of motion mapped to full brightness.
#define u_debug_range_px u_params.x

vec3 velocity_debug_hsv_to_rgb(vec3 hsv)
{
    vec3 k = mod(vec3(5.0, 3.0, 1.0) + vec3_splat(hsv.x * 6.0), vec3_splat(6.0));
    vec3 f = clamp(min(k, vec3(4.0, 4.0, 4.0) - k), vec3_splat(0.0), vec3_splat(1.0));
    return hsv.z * mix(vec3_splat(1.0), f, hsv.y);
}

// Velocity visualization: hue = direction of motion, brightness = magnitude in pixels
// (1 - exp(-px / range)), black = still, magenta = NaN/inf (a broken prev transform).
void main()
{
    vec2 vel = texture2DLod(s_velocity, v_texcoord0, 0.0).xy;

    // NaN fails every comparison, inf fails the upper bound: both land in the ! branch.
    if(!(dot(vel, vel) < 1.0e12))
    {
        gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);
        return;
    }

    vec2 vel_px = vel * u_viewRect.zw;
    float mag = length(vel_px);
    float brightness = 1.0 - exp(-mag / max(u_debug_range_px, 1.0e-3));
    float hue = atan2(vel_px.y, vel_px.x) / (2.0 * 3.14159265) + 0.5;

    vec3 color = velocity_debug_hsv_to_rgb(vec3(hue, 1.0, brightness));
    gl_FragColor = vec4(color, 1.0);
}
