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


#include <cstdint>
#include <functional>
#include <memory>
#include <vector>


/// Invoked after the image container has been parsed (with the texture info computed from its
/// header) and right before the GPU texture is created. Return false to skip the GPU allocation.
using TexturePreCreateFn = std::function<bool(const bgfx::TextureInfo& _info)>;

/// Pixels of a texture read back to the CPU: RGBA8, rows of width * 4 bytes.
struct ReadbackImage
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

/// Receives a readback on the main thread, inside gfx::frame() (or gfx::shutdown()).
using ReadbackFn = std::function<void(ReadbackImage&& _image)>;

/// Reads mip 0 of @p _texture (non-MSAA RGBA8, @p width x @p height) back to the CPU. The pixels
/// come back through a GPU readback that lands a couple of frames later, so this only issues the
/// copy in @p viewId (which must run after the texture is final); gfx::frame() hands them to
/// @p _onLanded once they are there. Returns false when the copy could not be issued.
bool readTexture(bgfx::ViewId viewId, bgfx::TextureHandle _texture, uint32_t width, uint32_t height, ReadbackFn _onLanded);

/// Saves the first color attachment of @p fbo (non-MSAA RGBA8, @p width x @p height) as a PNG at
/// @p _filePath through readTexture: gfx::frame() writes the file once the readback has landed.
/// Returns false when the copy could not be issued. Window framebuffers have no attachment; use
/// bgfx::requestScreenShot for those.
bool saveToFile(bgfx::ViewId viewId, const bx::FilePath& _filePath, bgfx::FrameBufferHandle fbo, uint32_t width, uint32_t height);

/// Hands the readbacks that have landed by @p _renderFrame to their callbacks and releases their
/// readback textures. Called by gfx::frame() after every bgfx::frame().
void deliverLandedReadbacks(uint32_t _renderFrame);

/// Advances bgfx frames until every pending readback has landed, then delivers them. Called by
/// gfx::shutdown() before bgfx shuts down, so no copy is left targeting freed memory.
void finishReadbacks();

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
                                bx::Error* _err = NULL,
                                const TexturePreCreateFn& _preCreate = nullptr);

/// Load a texture from memory (DDS, KTX, PNG, etc.). @a _name is optional debug label.
/// @a _preCreate is invoked after the image header has been parsed, right before the GPU texture
/// is created. @a _skip drops that many top mips (at most all but the last); @a _info describes the
/// texture created.
bgfx::TextureHandle loadTexture(const void* _data,
                                uint32_t _size,
                                uint64_t _flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                uint8_t _skip = 0,
                                bgfx::TextureInfo* _info = NULL,
                                bimg::Orientation::Enum* _orientation = NULL,
                                const char* _name = NULL,
                                bx::Error* _err = NULL,
                                const TexturePreCreateFn& _preCreate = nullptr);

/// A parsed image, shared by the texture that keeps it and the bgfx memory that refers to it.
using ImagePtr = std::shared_ptr<bimg::ImageContainer>;

/// Parses an image file in memory (KTX2, KTX, DDS, PNG, ...) into the layout bgfx uploads: each layer
/// or cube face in turn, its mips largest first. ETC1 data reads as ETC2. Null when it does not parse.
ImagePtr imageParseShared(const void* _data, uint32_t _size, bx::Error* _err = NULL);

/// The creation flags of a texture made from @p _image: @p _flags, plus BGFX_TEXTURE_SRGB when the
/// container is tagged sRGB and the backend has an sRGB view of its format and shape. Warns when it
/// has none, and once per format and shape when the backend decodes the format.
uint64_t imageGetTextureFlags(const bimg::ImageContainer& _image, uint64_t _flags);

/// Fills @p _info with the texture @p _image makes without its @p _skip top mips. False when
/// @p _skip leaves no mip.
bool imageGetTextureInfo(const bimg::ImageContainer& _image, uint8_t _skip, bgfx::TextureInfo& _info);

/// Bytes of the mips from @p _skip down, of every layer or face of @p _image: the size of the memory
/// a texture made at that skip uploads. 0 when @p _skip leaves no mip.
uint32_t imageGetSkippedSize(const bimg::ImageContainer& _image, uint8_t _skip);

/// The one range of @p _image holding the mips from @p _skip down, when there is one: a single layer
/// or face in bgfx's layout, or every side at skip 0. Such a texture is made without a copy.
bool imageGetSkippedRange(const bimg::ImageContainer& _image, uint8_t _skip, uint32_t& _offset, uint32_t& _size);

/// Copies the mips from @p _skip down of every layer or face of @p _image into @p _dst, side after
/// side, as bgfx reads them. Returns the bytes written: 0 when @p _dstSize is short or @p _skip
/// leaves no mip.
uint32_t imageCopySkipped(const bimg::ImageContainer& _image, uint8_t _skip, void* _dst, uint32_t _dstSize);

/// Creates a texture from @p _image without its @p _skip top mips, with @p _flags as given (see
/// imageGetTextureFlags). One range is referenced, not copied (imageGetSkippedRange): the reference
/// keeps @p _image alive until bgfx has uploaded it. Invalid when @p _skip leaves no mip or the
/// backend cannot create the texture.
bgfx::TextureHandle createTextureFromImage(const ImagePtr& _image, uint64_t _flags, uint8_t _skip);

///
bimg::ImageContainer* imageLoad(const void* data, uint32_t size, bgfx::TextureFormat::Enum _dstFormat = bgfx::TextureFormat::Count);
bimg::ImageContainer* imageLoad(const bx::FilePath& _filePath, bgfx::TextureFormat::Enum _dstFormat = bgfx::TextureFormat::Count);

/// Parse image metadata without decoding pixel data.
bool imageParseInfo(const bx::FilePath& _filePath, bimg::ImageContainer& _info, bx::Error* _err = NULL);
bool imageParseInfo(const void* _data, uint32_t _size, bimg::ImageContainer& _info, bx::Error* _err = NULL);

/// Write `image` to `saveAs`. Format is detected from `format_hint` when non-null
/// (e.g. the final ".dds" destination path while writing a ".temp" file); otherwise
/// from `saveAs`.
bool imageSave(const char* saveAs, bimg::ImageContainer* image, const char* format_hint = nullptr);

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
