/*
 * COMPACTED screen-probe trace: 16 threads per live probe, one per this frame's
 * Bayer stratum. Selected by gi_resolve_pass while windowed probe-space temporal
 * is on; each ray blends into the history tile the previous pass copied.
 * The 8x8 A/B-off sibling is cs_gi_screen_probe_trace_full.sc. Shared body:
 * gi_screen_probe_trace_kernel.sh.
 */

#define GI_SCREEN_PROBE_TRACE_COMPACT
#include "gi/gi_screen_probe_trace_kernel.sh"
