#ifndef __CLOUD_SHADOW_SH__
#define __CLOUD_SHADOW_SH__

/*
 * The sun's transmittance through the cloud layer (atmospherics/fs_cloud_shadow.sc renders the map): one texel per
 * entry point of the sun ray at the layer base. The deferred directional light and the GI's card direct lighting
 * read it the same way (a compute shader at the map's top mip, having no derivatives). The includer declares
 * s_cloudShadow (sampler2D).
 */

/// xy = map origin (world xz), z = 1 / extent, w = opacity.
uniform vec4 u_cloudShadow;
/// x = enabled, y = layer base world y, z = border fade width (map space), w = unused.
uniform vec4 u_cloudShadow2;

/// Sun transmittance through the cloud layer above world_position (L points toward the sun):
/// the surface point is projected up the sun direction to the layer base and the shadow map is
/// read there. Fades to unshadowed toward the map border.
float CloudShadow(vec3 world_position, vec3 L)
{
    if(u_cloudShadow2.x < 0.5 || L.y < 0.05)
    {
        return 1.0;
    }
    float t = (u_cloudShadow2.y - world_position.y) / L.y;
    if(t <= 0.0)
    {
        return 1.0;
    }
    vec3 entry = world_position + L * t;
    vec2 map_pos = (entry.xz - u_cloudShadow.xy) * u_cloudShadow.z + 0.5;
#if BGFX_SHADER_TYPE_COMPUTE
    float transmittance = texture2DLod(s_cloudShadow, clipToUv(map_pos), 0.0).r;
#else
    float transmittance = texture2D(s_cloudShadow, clipToUv(map_pos)).r;
#endif
    vec2 d = abs(map_pos - vec2_splat(0.5));
    float border = saturate((0.5 - max(d.x, d.y)) / max(u_cloudShadow2.z, 1e-4));
    return mix(1.0, transmittance, u_cloudShadow.w * border);
}

#endif // __CLOUD_SHADOW_SH__
