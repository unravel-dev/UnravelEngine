/*
 * The hit pass of the screen probe trace (cs_lumen_probe_trace.sc): the surface cache at the distance-field hits the
 * far-field pass stored, one thread per far ray (its dispatch).
 */

#define LUMEN_TRACE_HIT_SHADE 1
#include "lumen/cs_lumen_probe_trace.sc"
