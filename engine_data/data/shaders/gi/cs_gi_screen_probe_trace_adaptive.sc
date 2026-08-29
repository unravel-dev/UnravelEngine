/*
 * ADAPTIVE-RAY screen-probe trace (settings::adaptive_rays): four probes per 64-lane
 * group, 16 lanes each, rays allocated by reprojected importance - bright 2x2 blocks at
 * full per-texel detail, dim blocks as one splatted cone (16 + 3K rays instead of 64).
 * Blend-free and per-frame-complete, exactly like the full 8x8 sibling
 * (cs_gi_screen_probe_trace_full.sc). Body: gi_screen_probe_trace_kernel.sh.
 */

#define GI_SCREEN_PROBE_TRACE_ADAPTIVE
#include "gi/gi_screen_probe_trace_kernel.sh"
