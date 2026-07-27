/*
 * Seed a material page from PBR base_color * color_map.
 * Card UV is not mesh UV — we tile the map across the page so textured red
 * awnings/floors contribute chroma even before G-buffer refine.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

SAMPLER2D(s_color_map, 0);
IMAGE2D_RW(i_material, rgba16f, 1);
IMAGE2D_RW(i_emissive, rgba16f, 2);

uniform vec4 u_fill_params0; // xy = page origin, z = page size
uniform vec4 u_fill_albedo;  // rgb tint * coverage in a
uniform vec4 u_fill_emissive;

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    int page_size = int(u_fill_params0.z);
    if(any(greaterThanEqual(local, ivec2(page_size, page_size))))
    {
        return;
    }
    ivec2 coord = ivec2(int(u_fill_params0.x), int(u_fill_params0.y)) + local;
    vec2 uv = (vec2(local) + 0.5) / float(page_size);
    vec3 tex = texture2DLod(s_color_map, uv, 0.0).rgb;
    vec3 albedo = saturate(tex * u_fill_albedo.rgb);
    // Prefer map chroma when tint is near-white (typical glTF: white factor * red map).
    vec3 luma_w = vec3(0.2126, 0.7152, 0.0722);
    float tint_lum = dot(u_fill_albedo.rgb, luma_w);
    float map_lum = dot(tex, luma_w);
    float tint_chroma = length(u_fill_albedo.rgb - vec3(tint_lum, tint_lum, tint_lum));
    float map_chroma = length(tex - vec3(map_lum, map_lum, map_lum));
    if(tint_chroma < 0.05 && map_chroma > 0.05)
    {
        albedo = saturate(tex);
    }
    imageStore(i_material, coord, vec4(albedo, u_fill_albedo.a));
    imageStore(i_emissive, coord, vec4(u_fill_emissive.rgb, 1.0));
}
