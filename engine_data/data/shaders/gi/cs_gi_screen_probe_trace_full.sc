/*
 * FULL 8x8 screen-probe trace: one thread per octahedral texel. Selected by
 * gi_resolve_pass for the A/B-off path and the first untrusted frame, so all 64
 * rays stay parallel. Compacted sibling is cs_gi_screen_probe_trace.sc. Shared
 * body: gi_screen_probe_trace_kernel.sh.
 */

#include "gi/gi_screen_probe_trace_kernel.sh"
