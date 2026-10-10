/*
 * The radiance cache's update counters as ShaderPrint text - its priority histogram, the bucket the trace budget
 * ran out in, what the update spent, the probes and tiles traced, and the probe atlas's occupancy (red when full:
 * probes then go unallocated and their screen probes trace farther). The counters are this frame's
 * (lumen_radiance_cache_common.sh); red / yellow mark a cost over twice / once the budget.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_radiance_cache_common.sh"

BUFFER_RO(b_lumen_rc_counters, uint, 0);
#define SHADER_PRINT_STAGE 1
#include "shader_print/shader_print.sh"

/// x = the trace cost budget (u_lumen_rc_params.y), y unused, z = the lines the text starts below (other overlays'
/// text above it), w unused.
uniform vec4 u_lumen_rc_stats;

/// Red at or above @p red, yellow at or above @p yellow, white otherwise.
vec3 LumenRcStatsColor(uint value, uint red, uint yellow)
{
	return value > red ? vec3(1.0, 0.0, 0.0) : (value > yellow ? vec3(1.0, 1.0, 0.0) : vec3_splat(1.0));
}

NUM_THREADS(1, 1, 1)
void main()
{
	uint budget = uint(u_lumen_rc_stats.x);
	uint spent = b_lumen_rc_counters[LUMEN_RC_COUNTER_SPENT];
	uint new_probe_cost = b_lumen_rc_counters[LUMEN_RC_COUNTER_NEW_PROBE_COST];
	uint tiles = b_lumen_rc_counters[LUMEN_RC_COUNTER_TILES];
	uint probes_in_atlas = b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES] - b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST];
	ShaderPrintContext text = ShaderPrintBegin(vec2(0.1, 0.1));
	for(int skipped = 0; skipped < int(u_lumen_rc_stats.z); ++skipped)
	{
		text = ShaderPrintNewline(text);
	}
	text = ShaderPrintNewline(text);
	// The priority histogram: the trace cost of the probes in each bucket, 8 to a line.
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 0]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 1]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 2]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 3]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 4]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 5]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 6]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 7]);
	text = ShaderPrintNewline(text);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 8]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 9]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 10]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 11]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 12]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 13]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 14]);
	text = ShaderPrintSymbol(text, _SPC_);
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_HISTOGRAM + 15]);
	text = ShaderPrintNewline(text);
	text = ShaderPrintNewline(text);
	// "MaxBucket "
	text = ShaderPrintSymbols(text, ivec4(_M_, _a_, _x_, _B_));
	text = ShaderPrintSymbols(text, ivec4(_u_, _c_, _k_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _SPC_, 0, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_MAX_BUCKET]);
	text = ShaderPrintNewline(text);
	// "MaxTracesFromMaxUpdateBucket "
	text = ShaderPrintSymbols(text, ivec4(_M_, _a_, _x_, _T_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _a_, _c_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _F_, _r_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_m_, _M_, _a_, _x_));
	text = ShaderPrintSymbols(text, ivec4(_U_, _p_, _d_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _e_, _B_, _u_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _k_, _e_, _t_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_REMAINDER]);
	text = ShaderPrintNewline(text);
	// "Out "
	text = ShaderPrintSymbols(text, ivec4(_O_, _u_, _t_, _SPC_));
	text = ShaderPrintNewline(text);
	// " TraceCost "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _T_, _r_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _e_, _C_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _t_, _SPC_, 0));
	text.color = LumenRcStatsColor(spent, 2u * budget, budget);
	text = ShaderPrintUint(text, spent);
	text.color = vec3_splat(1.0);
	text = ShaderPrintNewline(text);
	// " TraceCostFromMaxUpdateBucket: "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _T_, _r_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _e_, _C_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _t_, _F_, _r_));
	text = ShaderPrintSymbols(text, ivec4(_o_, _m_, _M_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_x_, _U_, _p_, _d_));
	text = ShaderPrintSymbols(text, ivec4(_a_, _t_, _e_, _B_));
	text = ShaderPrintSymbols(text, ivec4(_u_, _c_, _k_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _COLON_, _SPC_, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_SPENT_LAST]);
	text = ShaderPrintNewline(text);
	// " Min New Probe Trace Cost:     "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _M_, _i_, _n_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _N_, _e_, _w_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _P_, _r_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_b_, _e_, _SPC_, _T_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _a_, _c_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _C_, _o_, _s_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _COLON_, _SPC_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _SPC_, _SPC_, 0));
	text.color = LumenRcStatsColor(new_probe_cost, budget / 2u, 0xFFFFFFFFu);
	text = ShaderPrintUint(text, new_probe_cost);
	text.color = vec3_splat(1.0);
	text = ShaderPrintNewline(text);
	// " Traced Probes:  "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _T_, _r_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _e_, _d_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_P_, _r_, _o_, _b_));
	text = ShaderPrintSymbols(text, ivec4(_e_, _s_, _COLON_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_TRACE_COUNT]);
	text = ShaderPrintNewline(text);
	// " Traced tiles/4: "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _T_, _r_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _e_, _d_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _i_, _l_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _SLASH_, _4_, _COLON_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text.color = LumenRcStatsColor(tiles, budget, 0xFFFFFFFFu);
	text = ShaderPrintUint(text, tiles / 4u);
	text.color = vec3_splat(1.0);
	text = ShaderPrintNewline(text);
	// " Traced tiles:   "
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _T_, _r_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _e_, _d_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _i_, _l_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _COLON_, _SPC_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, tiles);
	text = ShaderPrintNewline(text);
	// "MaxProbesInAtlas "
	text = ShaderPrintSymbols(text, ivec4(_M_, _a_, _x_, _P_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _o_, _b_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _I_, _n_, _A_));
	text = ShaderPrintSymbols(text, ivec4(_t_, _l_, _a_, _s_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, uint(LUMEN_RADIANCE_CACHE_MAX_PROBES));
	text = ShaderPrintNewline(text);
	// "ProbesInAtlas "
	text = ShaderPrintSymbols(text, ivec4(_P_, _r_, _o_, _b_));
	text = ShaderPrintSymbols(text, ivec4(_e_, _s_, _I_, _n_));
	text = ShaderPrintSymbols(text, ivec4(_A_, _t_, _l_, _a_));
	text = ShaderPrintSymbols(text, ivec4(_s_, _SPC_, 0, 0));
	text.color = LumenRcStatsColor(probes_in_atlas, uint(LUMEN_RADIANCE_CACHE_MAX_PROBES) - 2u, 0xFFFFFFFFu);
	text = ShaderPrintUint(text, probes_in_atlas);
	text.color = vec3_splat(1.0);
	text = ShaderPrintNewline(text);
	// "Allocator "
	text = ShaderPrintSymbols(text, ivec4(_A_, _l_, _l_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _a_, _t_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _SPC_, 0, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_PROBES]);
	text = ShaderPrintNewline(text);
	// "FreeList "
	text = ShaderPrintSymbols(text, ivec4(_F_, _r_, _e_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_L_, _i_, _s_, _t_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, b_lumen_rc_counters[LUMEN_RC_COUNTER_FREE_LIST]);
}
