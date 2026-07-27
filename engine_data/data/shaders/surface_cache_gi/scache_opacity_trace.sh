/*
 * Short software opacity march for surface-cache GI (compute-safe: imageLoad).
 * Callers must declare: IMAGE3D_RO(i_opacity, rgba16f, <reg>);
 *
 * Soft transmittance avoids knife-cut occlusion planes. Near-field bypass
 * keeps local floor→wall bounce alive.
 */

#ifndef SCACHE_OPACITY_TRACE_SH
#define SCACHE_OPACITY_TRACE_SH

#ifndef SCACHE_OPACITY_STEPS
#define SCACHE_OPACITY_STEPS 12
#endif

#ifndef SCACHE_OPACITY_NEAR_BYPASS
#define SCACHE_OPACITY_NEAR_BYPASS 5.0
#endif

float scache_sample_opacity(vec3 world_pos,
                            vec3 origin,
                            float voxel_size,
                            vec3 dims)
{
    vec3 local = (world_pos - origin) / max(voxel_size, 1e-4);
    ivec3 coord = ivec3(floor(local));
    ivec3 idim = ivec3(int(dims.x + 0.5), int(dims.y + 0.5), int(dims.z + 0.5));
    if(any(lessThan(coord, ivec3(0, 0, 0))) || any(greaterThanEqual(coord, idim)))
    {
        return 0.0;
    }
    return imageLoad(i_opacity, coord).r;
}

/**
 * @brief March from a to b. Returns soft visibility in [0,1].
 */
float scache_visibility(vec3 a,
                        vec3 b,
                        vec3 origin,
                        float voxel_size,
                        vec3 dims,
                        float enabled)
{
    if(enabled < 0.5)
    {
        return 1.0;
    }
    vec3 delta = b - a;
    float dist = length(delta);
    if(dist < 1e-3)
    {
        return 1.0;
    }
    // Local bounce (red floor → nearby wall) must not die on emitter shells.
    if(dist < SCACHE_OPACITY_NEAR_BYPASS)
    {
        return 1.0;
    }
    vec3 dir = delta / dist;
    float start_t = max(voxel_size * 2.0, dist * 0.12);
    float end_t = dist - max(voxel_size * 3.0, dist * 0.28);
    if(end_t <= start_t)
    {
        return 1.0;
    }
    float step_t = (end_t - start_t) / float(SCACHE_OPACITY_STEPS);
    float t = start_t;
    float vis = 1.0;
    LOOP for(int i = 0; i < SCACHE_OPACITY_STEPS; ++i)
    {
        vec3 p = a + dir * t;
        float op = scache_sample_opacity(p, origin, voxel_size, dims);
        // Soft beer-law attenuation — binary 0/1 made vertical knife cuts.
        vis *= exp(-op * 2.2);
        if(vis < 0.04)
        {
            return 0.0;
        }
        t += step_t;
    }
    return vis;
}

#endif // SCACHE_OPACITY_TRACE_SH
