/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#ifndef BGFX_UTILS_H_HEADER_GUARD
#define BGFX_UTILS_H_HEADER_GUARD

#include <bgfx/bgfx.h>
#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bimg/decode.h>

#include <bx/bounds.h>
#include <bx/pixelformat.h>
#include <bx/string.h>
#include <bx/filepath.h>

#include <tinystl/allocator.h>
#include <tinystl/vector.h>
namespace stl = tinystl;

bool saveToFile(bgfx::ViewId viewId, const bx::FilePath& _filePath, bgfx::FrameBufferHandle fbo, uint32_t width, uint32_t height);
///
void* load(const bx::FilePath& _filePath, uint32_t* _size = NULL);

///
void unload(void* _ptr);

///
bgfx::ShaderHandle loadShader(const bx::StringView& _name);
bgfx::ShaderHandle loadShader(const bx::FilePath& _filePath);
///
bgfx::ProgramHandle loadProgram(const bx::StringView& _vsName, const bx::StringView& _fsName);

///
bgfx::TextureHandle loadTexture(const bx::FilePath& _filePath,
                                uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                uint8_t _skip = 0,
                                bgfx::TextureInfo* _info = NULL,
                                bimg::Orientation::Enum* _orientation = NULL,
                                bx::Error* _err = NULL);

/// Load a texture from memory (DDS, KTX, PNG, etc.). @a _name is optional debug label.
bgfx::TextureHandle loadTexture(const void* _data,
                                uint32_t _size,
                                uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                uint8_t _skip = 0,
                                bgfx::TextureInfo* _info = NULL,
                                bimg::Orientation::Enum* _orientation = NULL,
                                const char* _name = NULL,
                                bx::Error* _err = NULL);

///
bimg::ImageContainer* imageLoad(const void* data, uint32_t size, bgfx::TextureFormat::Enum _dstFormat = bgfx::TextureFormat::Count);
bimg::ImageContainer* imageLoad(const bx::FilePath& _filePath, bgfx::TextureFormat::Enum _dstFormat = bgfx::TextureFormat::Count);

/// Parse image metadata without decoding pixel data.
bool imageParseInfo(const bx::FilePath& _filePath, bimg::ImageContainer& _info, bx::Error* _err = NULL);
bool imageParseInfo(const void* _data, uint32_t _size, bimg::ImageContainer& _info, bx::Error* _err = NULL);

bool imageSave(const char* saveAs, bimg::ImageContainer* image);

/// Flip tangent-space normal map Y (tangent-space +Y / green channel via bimg unpack/pack).
/// Preserves the container format for uncompressed sources. Compressed inputs are
/// decoded to RGBA8 and left as RGBA8 (no lossy re-encode back to BC).
bool imageFlipTangentSpaceNormalY(bimg::ImageContainer*& _image);

/// Prepare a flipped normal map for PNG bake export (RGBA8, opaque alpha when unused).
bool imagePrepareNormalMapBakePng(bimg::ImageContainer*& _image);

///
void calcTangents(void* _vertices,
                  uint16_t _numVertices,
                  bgfx::VertexLayout _layout,
                  const uint16_t* _indices,
                  uint32_t _numIndices);

/// Returns true if both internal transient index and vertex buffer have
/// enough space.
///
/// @param[in] _numVertices Number of vertices.
/// @param[in] _layout Vertex layout.
/// @param[in] _numIndices Number of indices.
///
inline bool checkAvailTransientBuffers(uint32_t _numVertices, const bgfx::VertexLayout& _layout, uint32_t _numIndices)
{
    return _numVertices == bgfx::getAvailTransientVertexBuffer(_numVertices, _layout) &&
           (0 == _numIndices || _numIndices == bgfx::getAvailTransientIndexBuffer(_numIndices));
}

///
inline uint32_t encodeNormalRgba8(float _x, float _y = 0.0f, float _z = 0.0f, float _w = 0.0f)
{
    const float src[] = {
        _x * 0.5f + 0.5f,
        _y * 0.5f + 0.5f,
        _z * 0.5f + 0.5f,
        _w * 0.5f + 0.5f,
    };
    uint32_t dst;
    bx::packRgba8(&dst, src);
    return dst;
}

#endif // BGFX_UTILS_H_HEADER_GUARD
