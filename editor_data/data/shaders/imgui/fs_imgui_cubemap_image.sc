$input v_texcoord0

#include <bgfx_shader.sh>

// We reuse your existing uniform.
uniform vec4 u_imageLodEnabled;

// x = LOD (mip level), y = enabled
#define u_imageLod     u_imageLodEnabled.x
#define u_imageEnabled u_imageLodEnabled.y

// Change to a CUBEmap sampler.
SAMPLERCUBE(s_texCube, 0);

void main()
{
    // v_texcoord0 in [0..1, 0..1]
    float u = v_texcoord0.x;
    float v = v_texcoord0.y;

           // Determine which of the 4 columns (0..3) and 3 rows (0..2) we're in.
    float colF = floor(u * 4.0); // 0..3
    float rowF = floor(v * 3.0); // 0..2
    int col = int(colF);
    int row = int(rowF);

           // Default: no face -> paint black
    int faceIndex = -1;

           // Map (row,col) => faceIndex [0..5], if it belongs to a face
           // Row 0: (0,1) => face 0 => +Y
    if (row == 0 && col == 1) {
        faceIndex = 0; // +Y
    }
    // Row 1: (1,0) => face 1 => -X
    else if (row == 1 && col == 0) {
        faceIndex = 1; // -X
    }
    // Row 1: (1,1) => face 2 => +Z
    else if (row == 1 && col == 1) {
        faceIndex = 2; // +Z
    }
    // Row 1: (1,2) => face 3 => +X
    else if (row == 1 && col == 2) {
        faceIndex = 3; // +X
    }
    // Row 1: (1,3) => face 4 => -Z
    else if (row == 1 && col == 3) {
        faceIndex = 4; // -Z
    }
    // Row 2: (2,1) => face 5 => -Y
    else if (row == 2 && col == 1) {
        faceIndex = 5; // -Y
    }

           // Remap local UV to [-1..+1] within this cell
    float localU = fract(u * 4.0) * 2.0 - 1.0; //   [0..1] -> [-1..+1]
    float localV = fract(v * 3.0) * 2.0 - 1.0; //   [0..1] -> [-1..+1]

    vec3 color = vec3_splat(0.0);
    float alpha = 0.2 + 0.8 * u_imageEnabled;

    if (faceIndex >= 0)
    {
        // Convert (localU, localV) to a direction for each face
        vec3 dir;
        if (faceIndex == 0) {
            // +Y
            dir = vec3(localU,  1.0,  localV);
        }
        else if (faceIndex == 1) {
            // -X
            dir = vec3(-1.0, -localV, localU);
        }
        else if (faceIndex == 2) {
            // +Z
            dir = vec3(localU, -localV, 1.0);
        }
        else if (faceIndex == 3) {
            // +X
            dir = vec3(1.0, -localV, -localU);
        }
        else if (faceIndex == 4) {
            // -Z
            dir = vec3(-localU, -localV, -1.0);
        }
        else {
            // faceIndex == 5 => -Y
            dir = vec3(localU, -1.0, -localV);
        }

        dir = normalize(dir);

               // Sample the cubemap with optional LOD
        color = textureCubeLod(s_texCube, dir, u_imageLod).rgb;
    }

    gl_FragColor = vec4(color, alpha);
}

// void main()
// {
//     // v_texcoord0 in [0..1, 0..1]
//     float u = v_texcoord0.x; // maps to longitude
//     float v = v_texcoord0.y; // maps to latitude

//     // Convert (u, v) => (phi, theta)
//     // phi in [0..2π], theta in [0..π]
//     float phi = 2.0 * 3.141592653589793 * (u - 0.0);
//     float theta = v * 3.141592653589793;

//     // Spherical to Cartesian, Y up:
//     // sin(theta)*cos(phi), cos(theta), sin(theta)*sin(phi)
//     //   theta=0 => +Y
//     //   phi=0 => +Z
//     float sinTheta = sin(theta);
//     float cosTheta = cos(theta);
//     float sinPhi   = sin(phi);
//     float cosPhi   = cos(phi);

//     // direction vector
//     vec3 dir = normalize(vec3(
//         sinTheta * cosPhi,
//         cosTheta,
//         sinTheta * sinPhi
//         ));

//     // Sample the cubemap with optional LOD
//     vec3 color = textureCubeLod(s_texCube, dir, u_imageLod).rgb;

//     // Your alpha logic
//     float alpha = 0.2 + 0.8 * u_imageEnabled;

//     gl_FragColor = vec4(color, alpha);
// }
