#ifndef PRE_EXPOSURE_SH_HEADER_GUARD
#define PRE_EXPOSURE_SH_HEADER_GUARD

// The view's scene-color pre-exposure (UE View.PreExposure; engine/engine/rendering/pipeline/
// pre_exposure.h). Scene lighting written into the frame's HDR buffers is multiplied by it;
// history written under last frame's scale is multiplied by the history correction when read.
// x = pre-exposure, y = 1 / pre-exposure, z = pre-exposure / previous pre-exposure,
// w = previous pre-exposure.
uniform vec4 u_pre_exposure;

#define u_pre_exposure_value              u_pre_exposure.x
#define u_pre_exposure_inverse            u_pre_exposure.y
#define u_history_pre_exposure_correction u_pre_exposure.z

#endif // PRE_EXPOSURE_SH_HEADER_GUARD
