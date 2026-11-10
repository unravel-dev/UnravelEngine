#ifndef SHADER_BUFFER_SH_HEADER_GUARD
#define SHADER_BUFFER_SH_HEADER_GUARD

/*
 * Shader buffers (shader_buffer.h): data a shader reads by index, from a storage buffer, or with
 * SHADER_BUFFER_TEXTURES from a texture of the same elements in rows of SHADER_BUFFER_TEXTURE_WIDTH texels, for
 * devices whose vertex shaders read no storage buffers, as WebGL 2. The texture variant of a shader defines
 * SHADER_BUFFER_TEXTURES and is named after the variant it stands in for: "textures" for the default variant,
 * "<variant>_textures" for the others (get_shader_buffer_variant).
 *
 * Declare a buffer with the macro of its element and read it with the matching macro:
 *   SHADER_BUFFER_FLOAT4(name, stage)  shader_buffer_float4(name, index)       vec4 (rgba32f texels)
 *   SHADER_BUFFER_UINT4(name, stage)   shader_buffer_uint4(name, index)        uvec4 (rgba32u texels)
 *                                      shader_buffer_uint4_word(name, word)    uint word of the uvec4s
 *   SHADER_BUFFER_UINT(name, stage)    shader_buffer_uint(name, index)         uint (r32u texels)
 * A uint4 buffer is a buffer of uints, four per element. The index arguments are evaluated more than once.
 */

/// Texels per row of a shader buffer's texture (shader_buffer.h, SHADER_BUFFER_TEXTURE_WIDTH).
#define SHADER_BUFFER_TEXTURE_WIDTH 2048u

#if SHADER_BUFFER_TEXTURES

/// The texel of element @p index, counted along the rows.
ivec2 shader_buffer_coord(uint index)
{
	return ivec2(int(index % SHADER_BUFFER_TEXTURE_WIDTH), int(index / SHADER_BUFFER_TEXTURE_WIDTH));
}

#define SHADER_BUFFER_FLOAT4(_name, _stage) SAMPLER2D(_name, _stage)
#define SHADER_BUFFER_UINT4(_name, _stage) USAMPLER2D(_name, _stage)
#define SHADER_BUFFER_UINT(_name, _stage) USAMPLER2D(_name, _stage)
#define shader_buffer_float4(_name, _index) texelFetch(_name, shader_buffer_coord(_index), 0)
#define shader_buffer_uint4(_name, _index) texelFetch(_name, shader_buffer_coord(_index), 0)
#define shader_buffer_uint4_word(_name, _word) texelFetch(_name, shader_buffer_coord((_word) / 4u), 0)[int((_word) % 4u)]
#define shader_buffer_uint(_name, _index) texelFetch(_name, shader_buffer_coord(_index), 0).x

#else

#include <bgfx_compute.sh>

#define SHADER_BUFFER_FLOAT4(_name, _stage) BUFFER_RO(_name, vec4, _stage)
#define SHADER_BUFFER_UINT4(_name, _stage) BUFFER_RO(_name, uint, _stage)
#define SHADER_BUFFER_UINT(_name, _stage) BUFFER_RO(_name, uint, _stage)
#define shader_buffer_float4(_name, _index) _name[_index]
#define shader_buffer_uint4(_name, _index) uvec4(_name[(_index) * 4u], _name[(_index) * 4u + 1u], _name[(_index) * 4u + 2u], _name[(_index) * 4u + 3u])
#define shader_buffer_uint4_word(_name, _word) _name[_word]
#define shader_buffer_uint(_name, _index) _name[_index]

#endif

#endif // SHADER_BUFFER_SH_HEADER_GUARD
