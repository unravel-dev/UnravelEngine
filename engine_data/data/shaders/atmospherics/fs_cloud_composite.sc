$input v_skyColor, v_clipPos, v_viewDir

#include "../common.sh"

// Composites the half-resolution volumetric cloud pre-pass over the full-resolution frame
// (sky and geometry alike): out = cloud.rgb + frame * cloud.transmittance, done with the
// ONE / SRC_ALPHA blend state. The upsample is depth-aware: of the four half-res texels
// around the pixel, those whose scene distance (aux.g, km) disagrees with this pixel's
// distance are down-weighted, so clouds do not bleed over foreground geometry edges.

// xy = 1 / half-res size, zw = half-res size.
uniform vec4 u_cloudComposite;

SAMPLER2D(s_cloudTex, 0);
SAMPLER2D(s_cloudAux, 1);
SAMPLER2D(s_depth, 2);

#define CLOUD_COMPOSITE_DISTANCE_SCALE 0.001
// Same sky sentinel as the pre-pass (fs_cloud.sc).
#define CLOUD_COMPOSITE_SKY_DISTANCE   1.0e7
#define CLOUD_COMPOSITE_SKY_DEPTH      0.99999
// Relative distance difference at which a half-res texel loses most of its weight.
#define CLOUD_COMPOSITE_DEPTH_TOLERANCE 0.1

void main()
{
    vec2 uv = clipToUv(v_clipPos * 0.5 + 0.5);
    float depth = texture2D(s_depth, uv).r;
    float pixel_distance = depth >= CLOUD_COMPOSITE_SKY_DEPTH ? CLOUD_COMPOSITE_SKY_DISTANCE : length(computeViewSpacePosition(uv, depth));
    float pixel_km = pixel_distance * CLOUD_COMPOSITE_DISTANCE_SCALE;

    vec2 texel = u_cloudComposite.xy;
    vec2 pos = uv * u_cloudComposite.zw - 0.5;
    vec2 f = fract(pos);
    vec2 base = (floor(pos) + 0.5) * texel;

    vec2 offsets[4];
    offsets[0] = vec2(0.0, 0.0);
    offsets[1] = vec2(1.0, 0.0);
    offsets[2] = vec2(0.0, 1.0);
    offsets[3] = vec2(1.0, 1.0);
    float bilinear[4];
    bilinear[0] = (1.0 - f.x) * (1.0 - f.y);
    bilinear[1] = f.x * (1.0 - f.y);
    bilinear[2] = (1.0 - f.x) * f.y;
    bilinear[3] = f.x * f.y;

    vec4 accum = vec4_splat(0.0);
    float weight_sum = 0.0;
    for(int i = 0; i < 4; i++)
    {
        vec2 sample_uv = clamp(base + offsets[i] * texel, texel * 0.5, vec2_splat(1.0) - texel * 0.5);
        float sample_km = texture2DLod(s_cloudAux, sample_uv, 0.0).g;
        float relative = abs(sample_km - pixel_km) / max(pixel_km, 1e-3);
        float depth_weight = 1.0 / (1.0 + relative / CLOUD_COMPOSITE_DEPTH_TOLERANCE);
        float weight = bilinear[i] * depth_weight + 1e-5;
        accum += texture2DLod(s_cloudTex, sample_uv, 0.0) * weight;
        weight_sum += weight;
    }
    vec4 cloud = accum / weight_sum;

    gl_FragColor = vec4(max(cloud.rgb, vec3_splat(0.0)), saturate(cloud.a));
}
