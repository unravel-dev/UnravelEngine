$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"
#include "atmospherics/clouds.sh"

// Cloud shadow map: sun transmittance of the cloud layer over a square of the world around
// the camera. Texel (u, v) is the point where a sun ray enters the layer base at world
// (origin.x + (u - 0.5) * extent, base, origin.y + (v - 0.5) * extent); the value is the
// transmittance along that ray through the layer. The lighting pass projects a surface point
// up the sun direction to the layer base and reads the texel there; the irradiance bake reads
// the lowest mip as the sky's cloud coverage.

uniform vec4 u_sunDirection;
// xy = map origin (world xz), z = extent (world units), w = unused.
uniform vec4 u_cloudShadowMap;

SAMPLER3D(s_cloudNoise, 0);
SAMPLER2D(s_cloudNoise2D, 1);

#define CLOUD_SHADOW_STEPS              12
#define CLOUD_SHADOW_MIN_SUN_ELEVATION  0.05
#define CLOUD_SHADOW_MAX_MARCH_THICKNESS 6.0

void main()
{
    // v_clipPos is NDC xy; map space keeps +y = +z world (the reader flips for the texture).
    vec2 map_uv = v_clipPos * 0.5 + 0.5;
    vec3 light_dir = normalize(u_sunDirection.xyz);

    float transmittance = 1.0;
    if(light_dir.y > CLOUD_SHADOW_MIN_SUN_ELEVATION && u_cloud_mode > CLOUD_MODE_NONE + 0.5)
    {
        float extent = u_cloudShadowMap.z;
        vec3 entry = vec3(u_cloudShadowMap.x + (map_uv.x - 0.5) * extent,
                          u_cloud_layer_base_y,
                          u_cloudShadowMap.y + (map_uv.y - 0.5) * extent);
        float optical_depth = 0.0;
        if(u_cloud_mode > CLOUD_MODE_FLAT + 0.5)
        {
            float march = min(u_cloud_thickness / light_dir.y, u_cloud_thickness * CLOUD_SHADOW_MAX_MARCH_THICKNESS);
            float step_size = march / float(CLOUD_SHADOW_STEPS);
            for(int i = 0; i < CLOUD_SHADOW_STEPS; i++)
            {
                vec3 sp;
                vec3 sample_pos = entry + light_dir * ((float(i) + 0.5) * step_size);
                // Flat projection: the map is a plane at the layer base, so the height is flat.
                float h = cloud_height_fraction(sample_pos);
                optical_depth += cloud_sample_shape(s_cloudNoise, s_cloudNoise2D, sample_pos, h, sp) * step_size;
            }
            optical_depth *= CLOUD_BASE_EXTINCTION * u_cloud_density;
        }
        else
        {
            vec2 sp = cloud_noise_pos(entry).xz;
            optical_depth = cloud_flat_density(s_cloudNoise2D, sp) * cloud_flat_extinction();
        }
        transmittance = exp(-optical_depth);
    }

    gl_FragColor = vec4(transmittance, 0.0, 0.0, 1.0);
}
