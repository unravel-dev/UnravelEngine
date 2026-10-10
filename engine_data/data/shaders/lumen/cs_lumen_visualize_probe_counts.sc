/*
 * Screen probe gather debug: the gather's screen probe counts as ShaderPrint text - every probe, the uniform ones and
 * the adaptive ones placed this frame (at most the adaptive capacity). The text starts at (0.1, 0.1) of the view,
 * clear of the editor's navigation cube in the top right.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "lumen/lumen_common.sh"
#include "lumen/lumen_adaptive_probes.sh"

/// The adaptive probe state (lumen_adaptive_probes.sh).
BUFFER_RO(b_lumen_adaptive, uint, 0);
#define SHADER_PRINT_STAGE 1
#include "shader_print/shader_print.sh"

/// x = the probes of the screen's lattice, y = the adaptive probe capacity.
uniform vec4 u_lumen_visualize_counts;

NUM_THREADS(1, 1, 1)
void main()
{
	uint lattice_probes = uint(u_lumen_visualize_counts.x);
	uint adaptive_probes = min(b_lumen_adaptive[LUMEN_ADAPTIVE_COUNTER], uint(u_lumen_visualize_counts.y));
	ShaderPrintContext text = ShaderPrintBegin(vec2(0.1, 0.1));
	// "NumScreenProbes:         "
	text = ShaderPrintSymbols(text, ivec4(_N_, _u_, _m_, _S_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _r_, _e_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_n_, _P_, _r_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_b_, _e_, _s_, _COLON_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _SPC_, _SPC_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, _SPC_, _SPC_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, lattice_probes + adaptive_probes);
	text = ShaderPrintNewline(text);
	// "NumUniformScreenProbes:  "
	text = ShaderPrintSymbols(text, ivec4(_N_, _u_, _m_, _U_));
	text = ShaderPrintSymbols(text, ivec4(_n_, _i_, _f_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _m_, _S_, _c_));
	text = ShaderPrintSymbols(text, ivec4(_r_, _e_, _e_, _n_));
	text = ShaderPrintSymbols(text, ivec4(_P_, _r_, _o_, _b_));
	text = ShaderPrintSymbols(text, ivec4(_e_, _s_, _COLON_, _SPC_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, lattice_probes);
	text = ShaderPrintNewline(text);
	// "NumAdaptiveScreenProbes: "
	text = ShaderPrintSymbols(text, ivec4(_N_, _u_, _m_, _A_));
	text = ShaderPrintSymbols(text, ivec4(_d_, _a_, _p_, _t_));
	text = ShaderPrintSymbols(text, ivec4(_i_, _v_, _e_, _S_));
	text = ShaderPrintSymbols(text, ivec4(_c_, _r_, _e_, _e_));
	text = ShaderPrintSymbols(text, ivec4(_n_, _P_, _r_, _o_));
	text = ShaderPrintSymbols(text, ivec4(_b_, _e_, _s_, _COLON_));
	text = ShaderPrintSymbols(text, ivec4(_SPC_, 0, 0, 0));
	text = ShaderPrintUint(text, adaptive_probes);
}
