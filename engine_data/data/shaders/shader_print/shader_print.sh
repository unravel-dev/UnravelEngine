#ifndef __SHADER_PRINT_SH__
#define __SHADER_PRINT_SH__

/*
 * UE 5.8's ShaderPrint for compute shaders (ShaderPrintCommon.ush): text into the view's print buffer, which
 * shader_print draws over the finished image. The includer includes bgfx_compute.sh and defines SHADER_PRINT_STAGE,
 * the stage C++ binds the buffer at (shader_print::bind). Each function returns the advanced context:
 *
 *     ShaderPrintContext ctx = ShaderPrintBegin(vec2(0.1, 0.1));
 *     ctx = ShaderPrintSymbols(ctx, ivec4(_V_, _a_, _l_, _COLON_));
 *     ctx = ShaderPrintFloat(ctx, value);
 *     ctx = ShaderPrintNewline(ctx);
 *
 * Symbols are character codes (UE's _A_-style names below). A value takes UE's field of SHADER_PRINT_VALUE_FIELD
 * characters, as UE's columns do.
 */

BUFFER_RW(b_shader_print, uint, SHADER_PRINT_STAGE);

/// xy = the view's size in pixels, z = the symbols the buffer holds (shader_print::max_symbols).
uniform vec4 u_shader_print;

/// The buffer: SHADER_PRINT_HEADER words ([0] = the symbols printed), then per symbol its pen position in pixels
/// (float bits), its code and its colour (RGBA8).
#define SHADER_PRINT_HEADER 4
#define SHADER_PRINT_SYMBOL_STRIDE 4
/// UE's metrics at r.ShaderPrint.FontSize 8, FontSpacingX 0 and FontSpacingY 8, in pixels.
#define SHADER_PRINT_ADVANCE_X 8.0
#define SHADER_PRINT_ADVANCE_Y 16.0
/// UE MAX_DIGIT_COUNT and MAX_DECIMAL_COUNT.
#define SHADER_PRINT_VALUE_FIELD 12.0
#define SHADER_PRINT_MAX_DECIMALS 5

/// UE's character names (ShaderPrintCommon.ush).
#define _SPC_ 32
#define _EXCL_ 33
#define _PLUS_ 43
#define _COMMA_ 44
#define _MINUS_ 45
#define _DOT_ 46
#define _SLASH_ 47
#define _COLON_ 58
#define _EQUAL_ 61
#define _UNDERSCORE_ 95
#define _0_ 48
#define _1_ 49
#define _2_ 50
#define _3_ 51
#define _4_ 52
#define _5_ 53
#define _6_ 54
#define _7_ 55
#define _8_ 56
#define _9_ 57
#define _A_ 65
#define _B_ 66
#define _C_ 67
#define _D_ 68
#define _E_ 69
#define _F_ 70
#define _G_ 71
#define _H_ 72
#define _I_ 73
#define _J_ 74
#define _K_ 75
#define _L_ 76
#define _M_ 77
#define _N_ 78
#define _O_ 79
#define _P_ 80
#define _Q_ 81
#define _R_ 82
#define _S_ 83
#define _T_ 84
#define _U_ 85
#define _V_ 86
#define _W_ 87
#define _X_ 88
#define _Y_ 89
#define _Z_ 90
#define _a_ 97
#define _b_ 98
#define _c_ 99
#define _d_ 100
#define _e_ 101
#define _f_ 102
#define _g_ 103
#define _h_ 104
#define _i_ 105
#define _j_ 106
#define _k_ 107
#define _l_ 108
#define _m_ 109
#define _n_ 110
#define _o_ 111
#define _p_ 112
#define _q_ 113
#define _r_ 114
#define _s_ 115
#define _t_ 116
#define _u_ 117
#define _v_ 118
#define _w_ 119
#define _x_ 120
#define _y_ 121
#define _z_ 122

struct ShaderPrintContext
{
	///< Where lines start and the pen, in pixels from the view's top left.
	vec2 start;
	vec2 position;
	vec3 color;
};

/// A context printing white from @p start_uv (fractions of the view, UE InitShaderPrintContext).
ShaderPrintContext ShaderPrintBegin(vec2 start_uv)
{
	ShaderPrintContext ctx;
	ctx.start = start_uv * u_shader_print.xy;
	ctx.position = ctx.start;
	ctx.color = vec3_splat(1.0);
	return ctx;
}

ShaderPrintContext ShaderPrintSymbol(ShaderPrintContext ctx, int code)
{
	uint index;
	atomicFetchAndAdd(b_shader_print[0], 1u, index);
	if(index < uint(u_shader_print.z))
	{
		uint base = uint(SHADER_PRINT_HEADER) + index * uint(SHADER_PRINT_SYMBOL_STRIDE);
		uvec3 rgb = uvec3(saturate(ctx.color) * 255.0 + 0.5);
		b_shader_print[base] = floatBitsToUint(ctx.position.x);
		b_shader_print[base + 1u] = floatBitsToUint(ctx.position.y);
		b_shader_print[base + 2u] = uint(code);
		b_shader_print[base + 3u] = rgb.x | (rgb.y << 8u) | (rgb.z << 16u) | (255u << 24u);
	}
	ctx.position.x += SHADER_PRINT_ADVANCE_X;
	return ctx;
}

/// Up to four symbols; a code of 0 prints nothing.
ShaderPrintContext ShaderPrintSymbols(ShaderPrintContext ctx, ivec4 codes)
{
	for(int i = 0; i < 4; ++i)
	{
		int code = i == 0 ? codes.x : (i == 1 ? codes.y : (i == 2 ? codes.z : codes.w));
		if(code > 0)
		{
			ctx = ShaderPrintSymbol(ctx, code);
		}
	}
	return ctx;
}

ShaderPrintContext ShaderPrintNewline(ShaderPrintContext ctx)
{
	ctx.position = vec2(ctx.start.x, ctx.position.y + SHADER_PRINT_ADVANCE_Y);
	return ctx;
}

/// UE AddUIntSymbols: the digits of @p value.
ShaderPrintContext ShaderPrintDigits(ShaderPrintContext ctx, uint value)
{
	uint count = 1u;
	uint divisor = 1u;
	uint rest = value;
	for(int i = 0; i < 9; ++i)
	{
		rest /= 10u;
		if(rest > 0u)
		{
			++count;
			divisor *= 10u;
		}
	}
	for(uint i = 0u; i < count; ++i)
	{
		uint digit = value / divisor;
		ctx = ShaderPrintSymbol(ctx, _0_ + int(digit));
		value -= digit * divisor;
		divisor /= 10u;
	}
	return ctx;
}

/// The pen after a value printed from @p field: UE gives every value SHADER_PRINT_VALUE_FIELD characters.
ShaderPrintContext ShaderPrintEndValue(ShaderPrintContext ctx, vec2 field)
{
	ctx.position = field + vec2(SHADER_PRINT_VALUE_FIELD * SHADER_PRINT_ADVANCE_X, 0.0);
	return ctx;
}

ShaderPrintContext ShaderPrintUint(ShaderPrintContext ctx, uint value)
{
	vec2 field = ctx.position;
	ctx = ShaderPrintDigits(ctx, value);
	return ShaderPrintEndValue(ctx, field);
}

ShaderPrintContext ShaderPrintInt(ShaderPrintContext ctx, int value)
{
	vec2 field = ctx.position;
	if(value < 0)
	{
		ctx = ShaderPrintSymbol(ctx, _MINUS_);
		value = -value;
	}
	ctx = ShaderPrintDigits(ctx, uint(value));
	return ShaderPrintEndValue(ctx, field);
}

/// UE AddFloatSymbols: INF and NAN spelled out, else the sign, the integer part, the point and the decimals up to
/// the last non-zero one, at most SHADER_PRINT_MAX_DECIMALS.
ShaderPrintContext ShaderPrintFloat(ShaderPrintContext ctx, float value)
{
	vec2 field = ctx.position;
	if(isinf(value))
	{
		ctx = ShaderPrintSymbols(ctx, ivec4(_I_, _N_, _F_, 0));
	}
	else if(isnan(value))
	{
		ctx = ShaderPrintSymbols(ctx, ivec4(_N_, _A_, _N_, 0));
	}
	else
	{
		if(value < 0.0)
		{
			ctx = ShaderPrintSymbol(ctx, _MINUS_);
			value = -value;
		}
		ctx = ShaderPrintDigits(ctx, uint(int(value)));
		ctx = ShaderPrintSymbol(ctx, _DOT_);
		for(int i = 0; i < SHADER_PRINT_MAX_DECIMALS; ++i)
		{
			value = fract(value);
			if(value > 0.0)
			{
				value *= 10.0;
				ctx = ShaderPrintSymbol(ctx, _0_ + int(value));
			}
		}
	}
	return ShaderPrintEndValue(ctx, field);
}

#endif // __SHADER_PRINT_SH__
