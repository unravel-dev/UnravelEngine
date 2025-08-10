/*
 * Prefilter Compute Shader
 * Generates prefiltered environment maps using importance sampling
 * Processes cubemap faces and mip levels in parallel
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

#define MAX_SAMPLES 1024

// Input environment cubemap
SAMPLERCUBE(s_env, 0);

// Output prefiltered cubemap as 2D array (6 faces)
IMAGE2D_ARRAY_WO(i_output, rgba16f, 1);

// Uniforms: x=mipIdx, y=faceIdx, z=cubeSize, w=numMips
uniform vec4 u_data;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(int i, int N)
{
    return vec2(float(i)/float(N), RadicalInverse_VdC(uint(i)));
}

vec3 ImportanceSampleGGX(vec2 E, vec3 N, float a2)
{
    float phi = 2.0 * PI * E.x;
    float cosT = sqrt((1.0 - E.y) / (1.0 + (a2 - 1.0) * E.y));
    float sinT = sqrt(1.0 - cosT*cosT);
    vec3 H;
    H.x = cos(phi) * sinT;
    H.y = sin(phi) * sinT;
    H.z = cosT;
    vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitan = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

// Map 2D uv to cubemap vector for face index
vec3 GetCubemapVector(vec2 uv, int face)
{
    vec3 dir;
    if (face == 0)      dir = vec3(+1,  uv.y, -uv.x);
    else if (face == 1) dir = vec3(-1,  uv.y,  uv.x);
    else if (face == 2) dir = vec3( uv.x, +1,   -uv.y);
    else if (face == 3) dir = vec3( uv.x, -1,    uv.y);
    else if (face == 4) dir = vec3( uv.x,  uv.y, +1);
    else                dir = vec3(-uv.x,  uv.y, -1);
    return dir;
}

// Optimized workgroup size for cubemap processing
NUM_THREADS(8, 8, 6)
void main()
{
    ivec3 coord = ivec3(gl_GlobalInvocationID.xyz);
    
    float mipIdx = u_data.x;
    int cubeSize = int(u_data.z + 0.5);
    float numMips = u_data.w;
    
    // Face index from Z coordinate
    int face = coord.z;
    
    // Calculate mip level size
    int mipSize = cubeSize >> int(mipIdx);
    
    // Check bounds for current mip level and face
    if (any(greaterThanEqual(coord.xy, ivec2(mipSize, mipSize))) || face >= 6)
        return;
    
    float lastMipLevel = numMips - 1.0;
    float roughness = ComputeReflectionCaptureRoughnessFromMip(mipIdx, lastMipLevel);
    
    // Compute UV in [-1,+1] range
    vec2 uv = vec2(
        (float(coord.x) + 0.5) / float(mipSize) * 2.0 - 1.0,
        -((float(coord.y) + 0.5) / float(mipSize) * 2.0 - 1.0)
    );
    
    vec3 N = normalize(GetCubemapVector(uv, face));
    vec3 V = N;
    
    // Base lookup for very low roughness
    if (roughness < 0.01)
    {
        vec3 c = textureCubeLod(s_env, N, 0.0).rgb;
        imageStore(i_output, ivec3(coord.xy, face), vec4(max(c, vec3_splat(0.0)), 1.0));
        return;
    }
    
    // Determine sample count based on roughness
    int samples = (roughness < 0.1) ? 32 : 64;
    
    float SolidAngleTexel = 4.0 * PI / (6.0 * float(cubeSize * cubeSize));
    vec3 result = vec3_splat(0.0);
    float totalWeight = 0.0;
    
    float a = roughness * roughness;
    float a2 = a * a;
    
    // Monte Carlo integration using importance sampling
    for (int i = 0; i < samples; ++i)
    {
        vec2 E = Hammersley(i, samples);
        vec3 H = ImportanceSampleGGX(E, N, a2);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoH = max(dot(N, H), 0.0);
        float PDF = D_GGX(roughness, NoH) * NoH * 0.25;
        float SolidAngleSample = 1.0 / (float(samples) * PDF);
        float mip = 0.5 * log2(SolidAngleSample / SolidAngleTexel);
        mip = clamp(mip, 0.0, lastMipLevel);
        float NoL = max(dot(N, L), 0.0);
        
        if (NoL > 0.0)
        {
            vec3 c = textureCubeLod(s_env, L, mip).rgb;
            result += c * NoL;
            totalWeight += NoL;
        }
    }
    
    result /= totalWeight;
    
    // Store result in output cubemap array
    imageStore(i_output, ivec3(coord.xy, face), vec4(result, 1.0));
} 