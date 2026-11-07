/*
 * Copyright 2011-2016 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx#license-bsd-2-clause
 */

#ifndef __SHADERLIB_SH__
#define __SHADERLIB_SH__

#if BGFX_SHADER_LANGUAGE_GLSL
#	define CONST_ARRAY_BEGIN(_type, _name, _count) CONST(_type) _name[_count] = _type[](
#else
#	define CONST_ARRAY_BEGIN(_type, _name, _count) CONST(_type) _name[_count] = {
#endif // BGFX_SHADER_LANGUAGE_GLSL

#define NORMAL_MAP_Y_UP 1

vec4 encodeRE8(float _r)
{
	float exponent = ceil(log2(_r) );
	return vec4(_r / exp2(exponent)
			, 0.0
			, 0.0
			, (exponent + 128.0) / 255.0
			);
}

float decodeRE8(vec4 _re8)
{
	float exponent = _re8.w * 255.0 - 128.0;
	return _re8.x * exp2(exponent);
}

vec4 encodeRGBE8(vec3 _rgb)
{
	vec4 rgbe8;
	float maxComponent = max(max(_rgb.x, _rgb.y), _rgb.z);
	float exponent = ceil(log2(maxComponent) );
	rgbe8.xyz = _rgb / exp2(exponent);
	rgbe8.w = (exponent + 128.0) / 255.0;
	return rgbe8;
}

vec3 decodeRGBE8(vec4 _rgbe8)
{
	float exponent = _rgbe8.w * 255.0 - 128.0;
	vec3 rgb = _rgbe8.xyz * exp2(exponent);
	return rgb;
}


vec3 encodeNormalUint(vec3 _normal)
{
	return _normal * 0.5 + 0.5;
}

vec3 decodeNormalUint(vec3 _encodedNormal)
{
	return _encodedNormal * 2.0 - 1.0;
}

vec2 octahedronWrap(vec2 _val)
{
	// Octahedral normal encoding: folds a lower-hemisphere point onto the square's outer triangles.
	return (1.0 - abs(_val.yx) )
		 * mix(vec2_splat(-1.0), vec2_splat(1.0), vec2(greaterThanEqual(_val.xy, vec2_splat(0.0) ) ) );
}

vec2 encodeNormalOctahedron(vec3 _normal)
{
	_normal /= abs(_normal.x) + abs(_normal.y) + abs(_normal.z);
	_normal.xy = _normal.z >= 0.0 ? _normal.xy : octahedronWrap(_normal.xy);
	_normal.xy = _normal.xy * 0.5 + 0.5;
	return _normal.xy;
}

vec3 decodeNormalOctahedron(vec2 _encodedNormal)
{
	_encodedNormal = _encodedNormal * 2.0 - 1.0;

	vec3 normal;
	normal.z  = 1.0 - abs(_encodedNormal.x) - abs(_encodedNormal.y);
	normal.xy = normal.z >= 0.0 ? _encodedNormal.xy : octahedronWrap(_encodedNormal.xy);
	return normalize(normal);
}

vec3 convertRGB2XYZ(vec3 _rgb)
{
	// Linear sRGB (D65 white) to CIE XYZ.
	vec3 xyz;
	xyz.x = dot(vec3(0.4124564, 0.3575761, 0.1804375), _rgb);
	xyz.y = dot(vec3(0.2126729, 0.7151522, 0.0721750), _rgb);
	xyz.z = dot(vec3(0.0193339, 0.1191920, 0.9503041), _rgb);
	return xyz;
}

vec3 convertXYZ2RGB(vec3 _xyz)
{
	vec3 rgb;
	rgb.x = dot(vec3( 3.2404542, -1.5371385, -0.4985314), _xyz);
	rgb.y = dot(vec3(-0.9692660,  1.8760108,  0.0415560), _xyz);
	rgb.z = dot(vec3( 0.0556434, -0.2040259,  1.0572252), _xyz);
	return rgb;
}

vec3 convertXYZ2Yxy(vec3 _xyz)
{
	// CIE XYZ to Yxy: luminance Y, then the chromaticity x, y.
	float inv = 1.0/dot(_xyz, vec3(1.0, 1.0, 1.0) );
	return vec3(_xyz.y, _xyz.x*inv, _xyz.y*inv);
}

vec3 convertYxy2XYZ(vec3 _Yxy)
{
	// Yxy (luminance, chromaticity) back to CIE XYZ.
	vec3 xyz;
	xyz.x = _Yxy.x*_Yxy.y/_Yxy.z;
	xyz.y = _Yxy.x;
	xyz.z = _Yxy.x*(1.0 - _Yxy.y - _Yxy.z)/_Yxy.z;
	return xyz;
}

vec3 convertRGB2Yxy(vec3 _rgb)
{
	return convertXYZ2Yxy(convertRGB2XYZ(_rgb) );
}

vec3 convertYxy2RGB(vec3 _Yxy)
{
	return convertXYZ2RGB(convertYxy2XYZ(_Yxy) );
}

vec3 convertRGB2Yuv(vec3 _rgb)
{
	vec3 yuv;
	yuv.x = dot(_rgb, vec3(0.299, 0.587, 0.114) );
	yuv.y = (_rgb.x - yuv.x)*0.713 + 0.5;
	yuv.z = (_rgb.z - yuv.x)*0.564 + 0.5;
	return yuv;
}

vec3 convertYuv2RGB(vec3 _yuv)
{
	vec3 rgb;
	rgb.x = _yuv.x + 1.403*(_yuv.y-0.5);
	rgb.y = _yuv.x - 0.344*(_yuv.y-0.5) - 0.714*(_yuv.z-0.5);
	rgb.z = _yuv.x + 1.773*(_yuv.z-0.5);
	return rgb;
}

vec3 convertRGB2YIQ(vec3 _rgb)
{
	vec3 yiq;
	yiq.x = dot(vec3(0.299,     0.587,     0.114   ), _rgb);
	yiq.y = dot(vec3(0.595716, -0.274453, -0.321263), _rgb);
	yiq.z = dot(vec3(0.211456, -0.522591,  0.311135), _rgb);
	return yiq;
}

vec3 convertYIQ2RGB(vec3 _yiq)
{
	vec3 rgb;
	rgb.x = dot(vec3(1.0,  0.9563,  0.6210), _yiq);
	rgb.y = dot(vec3(1.0, -0.2721, -0.6474), _yiq);
	rgb.z = dot(vec3(1.0, -1.1070,  1.7046), _yiq);
	return rgb;
}

vec3 toLinear(vec3 _rgb)
{
	return pow(abs(_rgb), vec3_splat(2.2) );
}

vec4 toLinear(vec4 _rgba)
{
	return vec4(toLinear(_rgba.xyz), _rgba.w);
}

vec3 toLinearAccurate(vec3 _rgb)
{
	vec3 lo = _rgb / 12.92;
	vec3 hi = pow( (_rgb + 0.055) / 1.055, vec3_splat(2.4) );
	vec3 rgb = mix(hi, lo, vec3(lessThanEqual(_rgb, vec3_splat(0.04045) ) ) );
	return rgb;
}

vec4 toLinearAccurate(vec4 _rgba)
{
	return vec4(toLinearAccurate(_rgba.xyz), _rgba.w);
}

float toGamma(float _r)
{
	return pow(abs(_r), 1.0/2.2);
}

vec3 toGamma(vec3 _rgb)
{
	return pow(abs(_rgb), vec3_splat(1.0/2.2) );
}

vec4 toGamma(vec4 _rgba)
{
	return vec4(toGamma(_rgba.xyz), _rgba.w);
}

vec3 toGammaAccurate(vec3 _rgb)
{
	vec3 lo  = _rgb * 12.92;
	vec3 hi  = pow(abs(_rgb), vec3_splat(1.0/2.4) ) * 1.055 - 0.055;
	vec3 rgb = mix(hi, lo, vec3(lessThanEqual(_rgb, vec3_splat(0.0031308) ) ) );
	return rgb;
}

vec4 toGammaAccurate(vec4 _rgba)
{
	return vec4(toGammaAccurate(_rgba.xyz), _rgba.w);
}

vec3 toReinhard(vec3 _rgb)
{
	return toGamma(_rgb/(_rgb+vec3_splat(1.0) ) );
}

vec4 toReinhard(vec4 _rgba)
{
	return vec4(toReinhard(_rgba.xyz), _rgba.w);
}

vec3 toFilmic(vec3 _rgb)
{
	_rgb = max(vec3_splat(0.0), _rgb - 0.004);
	_rgb = (_rgb*(6.2*_rgb + 0.5) ) / (_rgb*(6.2*_rgb + 1.7) + 0.06);
	return _rgb;
}

vec4 toFilmic(vec4 _rgba)
{
	return vec4(toFilmic(_rgba.xyz), _rgba.w);
}

vec3 toAcesFilmic(vec3 _rgb)
{
	// ACES filmic tone mapping curve (a rational fit), clamped to [0, 1].
	float aa = 2.51f;
	float bb = 0.03f;
	float cc = 2.43f;
	float dd = 0.59f;
	float ee = 0.14f;
	return saturate( (_rgb*(aa*_rgb + bb) )/(_rgb*(cc*_rgb + dd) + ee) );
}

vec4 toAcesFilmic(vec4 _rgba)
{
	return vec4(toAcesFilmic(_rgba.xyz), _rgba.w);
}

vec3 luma(vec3 _rgb)
{
	float yy = dot(vec3(0.2126729, 0.7151522, 0.0721750), _rgb);
	return vec3_splat(yy);
}

vec4 luma(vec4 _rgba)
{
	return vec4(luma(_rgba.xyz), _rgba.w);
}

vec3 conSatBri(vec3 _rgb, vec3 _csb)
{
	vec3 rgb = _rgb * _csb.z;
	rgb = mix(luma(rgb), rgb, _csb.y);
	rgb = mix(vec3_splat(0.5), rgb, _csb.x);
	return rgb;
}

vec4 conSatBri(vec4 _rgba, vec3 _csb)
{
	return vec4(conSatBri(_rgba.xyz, _csb), _rgba.w);
}

vec3 posterize(vec3 _rgb, float _numColors)
{
	return floor(_rgb*_numColors) / _numColors;
}

vec4 posterize(vec4 _rgba, float _numColors)
{
	return vec4(posterize(_rgba.xyz, _numColors), _rgba.w);
}

vec3 sepia(vec3 _rgb)
{
	vec3 color;
	color.x = dot(_rgb, vec3(0.393, 0.769, 0.189) );
	color.y = dot(_rgb, vec3(0.349, 0.686, 0.168) );
	color.z = dot(_rgb, vec3(0.272, 0.534, 0.131) );
	return color;
}

vec4 sepia(vec4 _rgba)
{
	return vec4(sepia(_rgba.xyz), _rgba.w);
}

vec3 blendOverlay(vec3 _base, vec3 _blend)
{
	vec3 lt = 2.0 * _base * _blend;
	vec3 gte = 1.0 - 2.0 * (1.0 - _base) * (1.0 - _blend);
	return mix(lt, gte, step(vec3_splat(0.5), _base) );
}

vec4 blendOverlay(vec4 _base, vec4 _blend)
{
	return vec4(blendOverlay(_base.xyz, _blend.xyz), _base.w);
}

vec3 adjustHue(vec3 _rgb, float _hue)
{
	vec3 yiq = convertRGB2YIQ(_rgb);
	float angle = _hue + atan2(yiq.z, yiq.y);
	float len = length(yiq.yz);
	return convertYIQ2RGB(vec3(yiq.x, len*cos(angle), len*sin(angle) ) );
}

vec4 packFloatToRgba(float _value)
{
	const vec4 shift = vec4(256 * 256 * 256, 256 * 256, 256, 1.0);
	const vec4 mask = vec4(0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
	vec4 comp = fract(_value * shift);
	comp -= comp.xxyz * mask;
	return comp;
}

float unpackRgbaToFloat(vec4 _rgba)
{
	const vec4 shift = vec4(1.0 / (256.0 * 256.0 * 256.0), 1.0 / (256.0 * 256.0), 1.0 / 256.0, 1.0);
	return dot(_rgba, shift);
}

vec2 packHalfFloat(float _value)
{
	const vec2 shift = vec2(256, 1.0);
	const vec2 mask = vec2(0, 1.0 / 256.0);
	vec2 comp = fract(_value * shift);
	comp -= comp.xx * mask;
	return comp;
}

float unpackHalfFloat(vec2 _rg)
{
	const vec2 shift = vec2(1.0 / 256.0, 1.0);
	return dot(_rg, shift);
}

float random(vec2 _uv)
{
	return fract(sin(dot(_uv.xy, vec2(12.9898, 78.233) ) ) * 43758.5453);
}

vec3 fixCubeLookup(vec3 _v, float _lod, float _topLevelCubeSize)
{
	// Seamless cube-map filtering: pulls the minor axes in so a face edge maps to the mip's edge texel centres.
	float ax = abs(_v.x);
	float ay = abs(_v.y);
	float az = abs(_v.z);
	float vmax = max(max(ax, ay), az);
	float scale = 1.0 - exp2(_lod) / _topLevelCubeSize;
	if (ax != vmax) { _v.x *= scale; }
	if (ay != vmax) { _v.y *= scale; }
	if (az != vmax) { _v.z *= scale; }
	return _v;
}

#if BGFX_SHADER_TYPE_FRAGMENT
// Fragment stage only: a mip bias needs the implicit derivatives, and GLSL rejects biased sampling in
// every other stage even in a function nobody calls.
vec3 getTangentSpaceNormal( sampler2D bumpTexture, vec2 texCoords, float bumpiness, float reconstructZ, float mipBias )
{
    vec3 normal = texture2DBias(bumpTexture, texCoords, mipBias).xyz;
  	normal = normal * 2.0f - 1.0f;

#if NORMAL_MAP_Y_UP
    normal.y = -normal.y;
#endif

	normal.xy *= bumpiness;

	BRANCH
	if(reconstructZ > 0.5f)
	{
  		normal.z = sqrt(max(0.0f, 1.0f - dot(normal.xy, normal.xy)));
	}

    return normalize(normal);
}
#endif // BGFX_SHADER_TYPE_FRAGMENT

float dither5x5(vec2 fragCoord)
{
float aa = 0.0f;
float Dither5 = fract( ( fragCoord.x + fragCoord.y * 2.0f - 1.5f + aa-2.5f ) / 5.0f );
float Noise = fract( dot( vec2( 171.0f, 231.0f ) / 71.0f, fragCoord.xy ) );
float Dither = ( Dither5 * 5.0f + Noise ) * (1.0f / 6.0f);
return Dither;
}

// The high 32 bits of the 32 x 32 bit product of a and b, from 16-bit halves: not every target has a 64-bit product.
uint mulHigh32(uint a, uint b)
{
	uint aLow = a & 0xFFFFu;
	uint aHigh = a >> 16u;
	uint bLow = b & 0xFFFFu;
	uint bHigh = b >> 16u;
	uint lowLow = aLow * bLow;
	uint middleHigh = aHigh * bLow;
	uint middleLow = aLow * bHigh;
	uint carry = ((lowLow >> 16u) + (middleHigh & 0xFFFFu) + (middleLow & 0xFFFFu)) >> 16u;
	return aHigh * bHigh + (middleHigh >> 16u) + (middleLow >> 16u) + carry;
}

// 16x16 tile of interleaved gradient noise (IGN) at integer pixel offsets, round(255 * IGN) / 256: repeats on screen
// like Bayer. In 32-bit fixed point, which every GPU computes alike: frac() is the wrap-around of an unsigned integer.
// Computed, not looked up: compiled through SPIR-V (Vulkan, OpenGL, WebGPU), a table indexed at run time
// becomes a local array every pixel fills.
float ign16x16(vec2 fragCoord)
{
	uvec2 tile = uvec2(fragCoord) & 15u;
	// frac(0.06711056 x + 0.00583715 y)
	uint gradient = tile.x * 288237660u + tile.y * 25070368u;
	// frac(52.9829189 gradient): 52 gradient, plus 0.9829189 gradient as a high product
	uint noise = gradient * 52u + mulHigh32(gradient, 4221604530u);
	// round(255 noise / 2^32), from the top 8 bits and the 24 below them
	uint rounded = ((noise >> 24u) * 255u + (((noise & 0xFFFFFFu) * 255u) >> 24u) + 128u) >> 8u;
	return float(rounded) / 256.0;
}


// bgfx_ndc (declared by bgfx_shader.sh for GLSL, baked into a constant by the GL backend)
// carries the backend's depth range and y origin, so these follow the OpenGL NDC
// convention bgfx is configured with instead of assuming it.
float toClipSpaceDepth(float _depthTextureZ)
{
#if BGFX_SHADER_LANGUAGE_GLSL
	return _depthTextureZ * bgfx_ndc.x - bgfx_ndc.y;
#else
	return _depthTextureZ;
#endif // BGFX_SHADER_LANGUAGE_GLSL
}

float toDepthTextureZ(float _clipSpaceZ)
{
#if BGFX_SHADER_LANGUAGE_GLSL
	return (_clipSpaceZ + bgfx_ndc.y) * bgfx_ndc.w;
#else
	return _clipSpaceZ;
#endif // BGFX_SHADER_LANGUAGE_GLSL
}

float toClipSpaceY(float _y)
{
#if BGFX_SHADER_LANGUAGE_GLSL
	return _y * bgfx_ndc.z;
#else
	return -_y;
#endif // BGFX_SHADER_LANGUAGE_GLSL
}

vec3 clipTransform(vec3 clip)
{
	clip.y = toClipSpaceY(clip.y);
	return clip;
}

// Uv of a render-target texel from a y-up [0, 1] screen position (clip * 0.5 + 0.5). The y flip
// is the clip-space one mirrored about the texture centre: where render targets keep their
// origin at the top left, uv.y runs opposite to clip y.
vec2 clipToUv(vec2 clip)
{
	clip.y = toClipSpaceY(clip.y - 0.5) + 0.5;
	return clip;
}

vec3 clipToWorld(mat4 _invViewProj, vec3 _clipPos)
{
	vec4 wpos = mul(_invViewProj, vec4(_clipPos, 1.0));
	return wpos.xyz / wpos.w;
}

mat3 invert_3x3( mat3 M )
{
	float det = dot( cross(M[0], M[1]), M[2] );
	mat3 T = transpose(M);
	return mat3( cross(T[1], T[2]), cross(T[2], T[0]), cross(T[0], T[1]) ) / det;
}

mat3 invert_3x3( mat4 M4 )
{
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_WGSL
	mat3 M = (mat3)M4;
#else
	mat3 M = mat3(M4);
#endif
	return invert_3x3(M);
}

mat3 constructTangentToWorldSpaceMatrix( vec3 T, vec3 B, vec3 N )
{
	return mtxFromCols(
			normalize(T),
			normalize(B),
			normalize(N)
			);
}

mat3 calculateInverseTranspose( mat4 m )
{
	return transpose(invert_3x3(m));
}

#if BGFX_SHADER_TYPE_FRAGMENT
mat3 computeTangentToWorldSpaceMatrix( vec3 N, vec3 p, vec2 uv )
{
	// Compute tangentToWorldSpace matrix
	vec3 dp1 = dFdx( p );
	vec3 dp2 = dFdy( p );
	vec2 duv1 = dFdx( uv );
	vec2 duv2 = dFdy( uv );

	/* solve the linear system */
	vec3 dp2perp = cross( dp2, N );
	vec3 dp1perp = cross( N, dp1 );
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	/* construct a scale-invariant frame */
	float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
	return mtxFromCols( T * invmax, B * invmax, N );
}
#endif

// Get view space position from screen space coordinates and depth
vec3 computeViewSpacePosition(vec2 uv, float z)
{
	// Convert screen space depth to clip space depth
    float clipDepth = toClipSpaceDepth(z);
	vec3 clip = clipTransform(vec3(uv.xy * 2.0 - 1.0, clipDepth));

    vec4 projected = mul(u_invProj, vec4(clip, 1.0));
    projected.xyz /= projected.w;

    return projected.xyz;
}

// Convert screen space depth to view space depth. A perspective projection gives
// Z_ndc = Mul + Add / Z_view (W_clip = Z_view), and toClipSpaceDepth turns the depth-buffer value
// back into Z_ndc for the backend's depth range, so no depth convention is assumed here.
float screenSpaceToViewSpaceDepth(float screenDepth)
{
#if BGFX_SHADER_LANGUAGE_HLSL || BGFX_SHADER_LANGUAGE_METAL || BGFX_SHADER_LANGUAGE_SPIRV || BGFX_SHADER_LANGUAGE_WGSL
    float depthLinearizeMul = u_proj[2][2];
    float depthLinearizeAdd = u_proj[2][3];
#else
    // GLSL indexes column-major (m[column][row]) and bgfx uploads the same memory untransposed,
    // so the elements the HLSL branch reads as [2][2] / [2][3] are [2][2] / [3][2] here.
    float depthLinearizeMul = u_proj[2][2];
    float depthLinearizeAdd = u_proj[3][2];
#endif
    return -depthLinearizeAdd / (depthLinearizeMul - toClipSpaceDepth(screenDepth));
}

#endif // __SHADERLIB_SH__
