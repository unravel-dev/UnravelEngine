#ifndef PRE_EXPOSURE_SH_HEADER_GUARD
#define PRE_EXPOSURE_SH_HEADER_GUARD

// The view's scene-color pre-exposure (engine/engine/rendering/pipeline/pre_exposure.h).
// Scene lighting written into the frame's HDR buffers is multiplied by it; history written
// under last frame's scale is multiplied by the history correction when read. It is the CPU's
// share (the manual exposure) times the GPU's share (the adapted exposure), each in this layout:
// x = share, y = 1 / share, z = share / previous share, w = previous share.
uniform vec4 u_pre_exposure;

// The environment SH's texel that holds the GPU's share, after the nine coefficients
// (pre_exposure_state::gpu_texel, cs_pre_exposure.sc writes it).
#define PRE_EXPOSURE_GPU_TEXEL 9

#ifdef PRE_EXPOSURE_ENVIRONMENT
// A shader that samples the environment SH anyway reads the share there, from the sampler named here,
// and needs no stage of its own for it.
#define u_pre_exposure_gpu texelFetch(PRE_EXPOSURE_ENVIRONMENT, ivec2(PRE_EXPOSURE_GPU_TEXEL, 0), 0)
#else
// The environment SH at the stage the passes bind it at (pre_exposure_state::bind); a shader that uses
// stage 15 sets its own before including this header.
#ifndef PRE_EXPOSURE_STAGE
#define PRE_EXPOSURE_STAGE 15
#endif
SAMPLER2D(s_pre_exposure, PRE_EXPOSURE_STAGE);
#define u_pre_exposure_gpu texelFetch(s_pre_exposure, ivec2(PRE_EXPOSURE_GPU_TEXEL, 0), 0)
#endif

// The pre-exposure from a share the shader fetched once (u_pre_exposure_gpu into a local), for a
// register-bound shader where a fetch per use costs more than the fetch.
#define pre_exposure_from_gpu(gpu)        (u_pre_exposure.x * (gpu).x)
#define u_pre_exposure_value              pre_exposure_from_gpu(u_pre_exposure_gpu)
#define u_pre_exposure_inverse            (u_pre_exposure.y * u_pre_exposure_gpu.y)
#define u_history_pre_exposure_correction (u_pre_exposure.z * u_pre_exposure_gpu.z)
// The GPU's share alone, for a value the CPU scaled by its share already (a light's intensity, the sky's
// exposition).
#define u_pre_exposure_gpu_scale          u_pre_exposure_gpu.x

#endif // PRE_EXPOSURE_SH_HEADER_GUARD
