/*
 * The far-field pass of the screen probe trace (cs_gi_probe_trace.sc): the distance field, the radiance cache and the
 * sky over the rays the screen pass left, one thread each.
 */

#define GI_TRACE_FAR_FIELD 1
#include "gi/cs_gi_probe_trace.sc"
