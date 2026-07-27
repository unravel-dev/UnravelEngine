/*
 * Stamp surface-cache cards as thin opacity shells into the clipmap.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

SAMPLER2D(s_cards, 0);
IMAGE3D_RW(i_opacity, rgba16f, 1);

uniform vec4 u_opacity_params0; // origin.xyz, voxel_size
uniform vec4 u_opacity_params1; // dims.xyz, card_count
uniform vec4 u_opacity_params2; // thickness, unused...

#define u_origin       u_opacity_params0.xyz
#define u_voxel_size   u_opacity_params0.w
#define u_dims         u_opacity_params1.xyz
#define u_card_count   u_opacity_params1.w
#define u_thickness    u_opacity_params2.x

NUM_THREADS(64, 1, 1)
void main()
{
    uint card_index = gl_GlobalInvocationID.x;
    if(card_index >= uint(u_card_count))
    {
        return;
    }
    vec4 t0 = texelFetch(s_cards, ivec2(0, int(card_index)), 0);
    vec4 t1 = texelFetch(s_cards, ivec2(1, int(card_index)), 0);
    vec4 t2 = texelFetch(s_cards, ivec2(2, int(card_index)), 0);
    vec4 t3 = texelFetch(s_cards, ivec2(3, int(card_index)), 0);
    vec3 origin = t0.xyz;
    float half_u = max(t0.w, 1e-3);
    vec3 normal = normalize(t1.xyz);
    float half_v = max(t1.w, 1e-3);
    vec3 tangent = t2.xyz;
    vec3 bitangent = t3.xyz;
    // Never stamp +Y floors — a solid floor plane blocks every gather to lit
    // floor emitters and kills red bounce. Still stamp ceilings (-Y) so roof
    // junctions occlude sunlit top pages from leaking into undersides.
    if(normal.y > 0.75)
    {
        return;
    }
    // Thicker shells on ceilings/walls reduce roof-junction light leaks.
    float thick_scale = (normal.y < -0.55) ? 1.75 : 1.35;
    float thick = max(u_thickness * thick_scale, u_voxel_size * 1.25);
    int steps_u = clamp(int(ceil(half_u * 2.0 / max(u_voxel_size, 1e-3))), 1, 24);
    int steps_v = clamp(int(ceil(half_v * 2.0 / max(u_voxel_size, 1e-3))), 1, 24);
    int dim = int(u_dims.x + 0.5);
    for(int iv = 0; iv <= steps_v; ++iv)
    {
        float fv = (float(iv) / float(steps_v) - 0.5) * 2.0 * half_v;
        for(int iu = 0; iu <= steps_u; ++iu)
        {
            float fu = (float(iu) / float(steps_u) - 0.5) * 2.0 * half_u;
            vec3 p = origin + tangent * fu + bitangent * fv;
            // Stamp along normal thickness so thin curtains still block.
            for(int iz = -1; iz <= 1; ++iz)
            {
                vec3 wp = p + normal * (float(iz) * thick * 0.45);
                vec3 local = (wp - u_origin) / max(u_voxel_size, 1e-4);
                ivec3 coord = ivec3(floor(local));
                if(any(lessThan(coord, ivec3(0, 0, 0))) || any(greaterThanEqual(coord, ivec3(dim, dim, dim))))
                {
                    continue;
                }
                imageStore(i_opacity, coord, vec4(1.0, 0.0, 0.0, 0.0));
            }
        }
    }
}
