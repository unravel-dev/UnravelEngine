$input v_texcoord0

#include <bgfx_shader.sh>

// We reuse your existing uniform.
uniform vec4 u_imageLodEnabled;

// x = LOD (mip level), y = enabled
#define u_imageLod     u_imageLodEnabled.x
#define u_imageEnabled u_imageLodEnabled.y

// Change to a CUBEmap sampler.
SAMPLER_CUBE(s_texCube, 0);

void main()
{
    // v_texcoord0 should be in the range [0..6] on X, [0..1] on Y.
    // Each integer step in X is one face of the cubemap.
    float x = v_texcoord0.x;
    float y = v_texcoord0.y;

    // Face index from 0..5
    int faceIndex = int(floor(x));

    // Local UV within that face, mapped to [-1..1].
    float localX = fract(x) * 2.0 - 1.0;
    float localY = y * 2.0 - 1.0;

    // Convert (localX, localY) to a direction vector for each face.
    vec3 dir;
    if(faceIndex == 0)
    {
        // +X
        dir = vec3(1.0, -localY, -localX);
    }
    else if(faceIndex == 1)
    {
        // -X
        dir = vec3(-1.0, -localY, localX);
    }
    else if(faceIndex == 2)
    {
        // +Y
        dir = vec3(localX, 1.0, localY);
    }
    else if(faceIndex == 3)
    {
        // -Y
        dir = vec3(localX, -1.0, -localY);
    }
    else if(faceIndex == 4)
    {
        // +Z
        dir = vec3(localX, -localY, 1.0);
    }
    else
    {
        // -Z (faceIndex == 5)
        dir = vec3(-localX, -localY, -1.0);
    }

    // Normalize for proper texture sampling.
    dir = normalize(dir);

    // Sample the cube with optional LOD (mip level).
    vec3 color = textureCubeLod(s_texCube, dir, u_imageLod).rgb;

    // Your existing alpha logic:
    float alpha = 0.2 + 0.8 * u_imageEnabled;

    gl_FragColor = vec4(color, alpha);
}
