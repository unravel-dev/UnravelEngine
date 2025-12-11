$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

void main()
{
    vec4 maskColor = texture2D(s_texColor, v_texcoord0.xy);
    
    // Luminance (perceived brightness) as opacity
    float maskAlpha = dot(maskColor.rgb, vec3(0.299, 0.587, 0.114));
    
    // Mask RGB tints the particle, multiplied by particle color
    vec3 finalColor = maskColor.rgb * v_color0.rgb;
    
    // Alpha combines particle alpha with mask brightness
    float finalAlpha = v_color0.a * maskAlpha;
    
    gl_FragColor = vec4(finalColor, finalAlpha);
}

