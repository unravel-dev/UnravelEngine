/*
 * Copyright 2011-2023 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include "common.h"

#include <tinystl/allocator.h>
#include <tinystl/string.h>
#include <tinystl/vector.h>
namespace stl = tinystl;

#include "entry/entry.h"
#include <bgfx/bgfx.h>
#include <bx/commandline.h>
#include <bx/endian.h>
#include <bx/math.h>
#include <bx/readerwriter.h>
#include <bx/string.h>

#include "bgfx_utils.h"

#include "../graphics.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>

#include <bgfx/bgfx.h>
#include <bx/bx.h>
#include <bx/file.h>
#include <bx/sort.h>

#include <time.h>
namespace entry
{
namespace
{

bx::AllocatorI* getDefaultAllocator()
{
    static bx::DefaultAllocator allocator;
    return &allocator;
}

static bx::AllocatorI* g_allocator = getDefaultAllocator();
typedef bx::StringT<&g_allocator> String;

class FileReader : public bx::FileReader
{
    typedef bx::FileReader super;

public:
    virtual bool open(const bx::FilePath& _filePath, bx::Error* _err) override
    {
        return super::open(_filePath, _err);
    }
};

class FileWriter : public bx::FileWriter
{
    typedef bx::FileWriter super;

public:
    virtual bool open(const bx::FilePath& _filePath, bool _append, bx::Error* _err) override
    {
        return super::open(_filePath, _append, _err);
    }
};

bx::AllocatorI* getAllocator()
{
    return g_allocator;
}

} // namespace
} // namespace entry

void* load(bx::FileReaderI* _reader, bx::AllocatorI* _allocator, const bx::FilePath& _filePath, uint32_t* _size)
{
    if(bx::open(_reader, _filePath))
    {
        uint32_t size = (uint32_t)bx::getSize(_reader);
        void* data = bx::alloc(_allocator, size);
        bx::read(_reader, data, size, bx::ErrorAssert{});
        bx::close(_reader);
        if(NULL != _size)
        {
            *_size = size;
        }
        return data;
    }
    else
    {
        DBG("Failed to open: %s.", _filePath);
    }

    if(NULL != _size)
    {
        *_size = 0;
    }

    return NULL;
}

void* load(const bx::FilePath& _filePath, uint32_t* _size)
{
    entry::FileReader reader;
    return load(&reader, entry::getAllocator(), _filePath, _size);
}

void unload(void* _ptr)
{
    bx::free(entry::getAllocator(), _ptr);
}

static const bgfx::Memory* loadMem(bx::FileReaderI* _reader, const char* _filePath)
{
    if(bx::open(_reader, _filePath))
    {
        uint32_t size = (uint32_t)bx::getSize(_reader);
        const bgfx::Memory* mem = bgfx::alloc(size + 1);
        bx::read(_reader, mem->data, size, bx::ErrorAssert{});
        bx::close(_reader);
        mem->data[mem->size - 1] = '\0';
        return mem;
    }

    DBG("Failed to load %s.", _filePath);
    return NULL;
}

static void* loadMem(bx::FileReaderI* _reader, bx::AllocatorI* _allocator, const bx::FilePath& _filePath, uint32_t* _size)
{
    if(bx::open(_reader, _filePath))
    {
        uint32_t size = (uint32_t)bx::getSize(_reader);
        void* data = bx::alloc(_allocator, size);
        bx::read(_reader, data, size, bx::ErrorAssert{});
        bx::close(_reader);

        if(NULL != _size)
        {
            *_size = size;
        }
        return data;
    }

    DBG("Failed to load %s.", _filePath);
    return NULL;
}

static bgfx::ShaderHandle loadShader(bx::FileReaderI* _reader, const bx::StringView& _name)
{
	bx::FilePath filePath("shaders/");

	switch (bgfx::getRendererType() )
	{
	case bgfx::RendererType::Noop:
	case bgfx::RendererType::Direct3D11:
	case bgfx::RendererType::Direct3D12: filePath.join("dx11");  break;
	case bgfx::RendererType::Agc:
	case bgfx::RendererType::Gnm:        filePath.join("pssl");  break;
	case bgfx::RendererType::Metal:      filePath.join("metal"); break;
	case bgfx::RendererType::Nvn:        filePath.join("nvn");   break;
	case bgfx::RendererType::OpenGL:     filePath.join("glsl");  break;
	case bgfx::RendererType::OpenGLES:   filePath.join("essl");  break;
	case bgfx::RendererType::Vulkan:     filePath.join("spirv"); break;
	case bgfx::RendererType::WebGPU:     filePath.join("wgsl");  break;

	case bgfx::RendererType::Count:
		BX_ASSERT(false, "You should not be here!");
		break;
	}

	char fileName[512];
	bx::strCopy(fileName, BX_COUNTOF(fileName), _name);
	bx::strCat(fileName, BX_COUNTOF(fileName), ".bin");

	filePath.join(fileName);

	bgfx::ShaderHandle handle = bgfx::createShader(loadMem(_reader, filePath.getCPtr() ) );
	bgfx::setName(handle, _name.getPtr(), _name.getLength() );

	return handle;
}

bgfx::ShaderHandle loadShader(const bx::StringView& _name)
{
    entry::FileReader reader;
    return loadShader(&reader, _name);
}

bgfx::ShaderHandle loadShader(const bx::FilePath& _filePath)
{
    entry::FileReader reader;
    bgfx::ShaderHandle handle = bgfx::createShader(loadMem(&reader, _filePath.getCPtr() ) );
	bgfx::setName(handle, _filePath.getFileName().getPtr(), int32_t(_filePath.getFileName().getLength() ) );
    return handle;
}

bgfx::ProgramHandle loadProgram(bx::FileReaderI* _reader, const bx::StringView& _vsName, const bx::StringView& _fsName)
{
    bgfx::ShaderHandle vsh = loadShader(_reader, _vsName);
    bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
    if(NULL != _fsName)
    {
        fsh = loadShader(_reader, _fsName);
    }

    return bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);
}

bgfx::ProgramHandle loadProgram(const bx::StringView& _vsName, const bx::StringView& _fsName)
{
    entry::FileReader reader;
    return loadProgram(&reader, _vsName, _fsName);
}

static void imageReleaseCb(void* _ptr, void* _userData)
{
    BX_UNUSED(_ptr);
    bimg::ImageContainer* imageContainer = (bimg::ImageContainer*)_userData;
    bimg::imageFree(imageContainer);
}

// Containers written by the asset compiler (KTX2; also KTX and DX10-header DDS)
// carry an sRGB transfer tag. Decoded source images (PNG/JPG/...) do not reliably
// express intent, so their parse result is never trusted for this.
static bool containerCarriesSrgbTag(const bimg::ImageContainer* imageContainer)
{
    return imageContainer->m_srgb &&
           (imageContainer->m_parser == bimg::ImageParser::Ktx2 ||
            imageContainer->m_parser == bimg::ImageParser::Ktx ||
            imageContainer->m_parser == bimg::ImageParser::Dds);
}

static uint64_t applyContainerSrgbFlag(const bimg::ImageContainer* imageContainer, uint64_t _flags)
{
    if(!containerCarriesSrgbTag(imageContainer))
    {
        return _flags;
    }

    // Only sample through an sRGB view when this backend actually exposes one for
    // the format and texture dimension; otherwise fall back to raw sampling rather
    // than failing texture creation.
    const bgfx::Caps* caps = bgfx::getCaps();
    const uint32_t formatCaps = caps->formats[bgfx::TextureFormat::Enum(imageContainer->m_format)];
    uint32_t srgbCap = BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB;
    if(imageContainer->m_cubeMap)
    {
        srgbCap = BGFX_CAPS_FORMAT_TEXTURE_CUBE_SRGB;
    }
    else if(1 < imageContainer->m_depth)
    {
        srgbCap = BGFX_CAPS_FORMAT_TEXTURE_3D_SRGB;
    }

    if(0 != (formatCaps & srgbCap))
    {
        return _flags | BGFX_TEXTURE_SRGB;
    }

    // The texture is tagged sRGB but this backend has no sRGB view for the
    // format/dimension: it will sample GAMMA-ENCODED texels as if linear (whole
    // scene lights too bright). There is no shader-side fallback decode, so at
    // least make the cause diagnosable instead of failing silently.
    gfx::log("warning",
             std::string("sRGB-tagged texture sampled RAW: no sRGB view for format ") +
                 bimg::getName(bimg::TextureFormat::Enum(imageContainer->m_format)) +
                 " on this backend; colors from this texture will be too bright.",
             __FILE__,
             uint16_t(__LINE__));

    return _flags;
}

static bgfx::TextureHandle loadTextureFromContainer(bimg::ImageContainer* imageContainer,
                                                    uint64_t _flags,
                                                    bgfx::TextureInfo* _info,
                                                    const TexturePreCreateFn& _preCreate = nullptr)
{
    if(NULL == imageContainer)
    {
        return BGFX_INVALID_HANDLE;
    }

    _flags = applyContainerSrgbFlag(imageContainer, _flags);

    const bgfx::Memory* mem = bgfx::makeRef(
        imageContainer->m_data,
        imageContainer->m_size,
        imageReleaseCb,
        imageContainer);

    bgfx::TextureInfo info;
    bgfx::calcTextureSize(
        info,
        uint16_t(imageContainer->m_width),
        uint16_t(imageContainer->m_height),
        uint16_t(imageContainer->m_depth),
        imageContainer->m_cubeMap,
        1 < imageContainer->m_numMips,
        imageContainer->m_numLayers,
        bgfx::TextureFormat::Enum(imageContainer->m_format));

    if(NULL != _info)
    {
        *_info = info;
    }

    // The image is parsed and its exact GPU footprint is known; give the caller a chance to
    // reject the allocation before the create lands.
    if(_preCreate && !_preCreate(info))
    {
        bimg::imageFree(imageContainer);
        return BGFX_INVALID_HANDLE;
    }

    if(imageContainer->m_cubeMap)
    {
        return bgfx::createTextureCube(
            uint16_t(imageContainer->m_width),
            1 < imageContainer->m_numMips,
            imageContainer->m_numLayers,
            bgfx::TextureFormat::Enum(imageContainer->m_format),
            _flags,
            mem);
    }

    if(1 < imageContainer->m_depth)
    {
        return bgfx::createTexture3D(
            uint16_t(imageContainer->m_width),
            uint16_t(imageContainer->m_height),
            uint16_t(imageContainer->m_depth),
            1 < imageContainer->m_numMips,
            bgfx::TextureFormat::Enum(imageContainer->m_format),
            _flags,
            mem);
    }

    if(bgfx::isTextureValid(0, false, imageContainer->m_numLayers, bgfx::TextureFormat::Enum(imageContainer->m_format), _flags))
    {
        return bgfx::createTexture2D(
            uint16_t(imageContainer->m_width),
            uint16_t(imageContainer->m_height),
            1 < imageContainer->m_numMips,
            imageContainer->m_numLayers,
            bgfx::TextureFormat::Enum(imageContainer->m_format),
            _flags,
            mem);
    }

    return BGFX_INVALID_HANDLE;
}

namespace
{

auto can_flip_normal_y_format(bimg::TextureFormat::Enum _format) -> bool
{
    if(bimg::isCompressed(_format) || bimg::isFloat(_format))
    {
        return false;
    }

    return bimg::getUnpack(_format) != nullptr && bimg::getPack(_format) != nullptr;
}

auto flip_normal_y_mip_ldr(bimg::ImageMip& _mip, bimg::TextureFormat::Enum _format) -> bool
{
    const bimg::UnpackFn unpack = bimg::getUnpack(_format);
    const bimg::PackFn pack = bimg::getPack(_format);
    if(nullptr == unpack || nullptr == pack)
    {
        return false;
    }

    const uint32_t bpp = bimg::getBitsPerPixel(_format);
    if(0 == bpp || (bpp % 8) != 0)
    {
        return false;
    }

    const uint32_t bytes_per_pixel = bpp / 8;
    const uint32_t width = _mip.m_width;
    const uint32_t height = _mip.m_height;
    const uint32_t depth = bx::max<uint32_t>(1, _mip.m_depth);
    const uint32_t row_stride = width * bytes_per_pixel;
    const uint32_t slice_stride = row_stride * height;

    uint8_t* slice = const_cast<uint8_t*>(_mip.m_data);
    for(uint32_t zz = 0; zz < depth; ++zz)
    {
        uint8_t* row = slice + zz * slice_stride;
        for(uint32_t yy = 0; yy < height; ++yy)
        {
            uint8_t* pixel = row;
            for(uint32_t xx = 0; xx < width; ++xx)
            {
                float rgba[4];
                unpack(rgba, pixel);
                rgba[1] = 1.0f - rgba[1];
                pack(pixel, rgba);
                pixel += bytes_per_pixel;
            }
            row += row_stride;
        }
    }

    return true;
}

auto flip_normal_y_all_mips_ldr(bimg::ImageContainer* _image) -> bool
{
    if(!can_flip_normal_y_format(_image->m_format))
    {
        return false;
    }

    for(uint8_t mip = 0; mip < _image->m_numMips; ++mip)
    {
        for(uint16_t layer = 0; layer < _image->m_numLayers; ++layer)
        {
            bimg::ImageMip mipData;
            if(!bimg::imageGetRawData(*_image, layer, mip, _image->m_data, _image->m_size, mipData))
            {
                continue;
            }

            if(!flip_normal_y_mip_ldr(mipData, _image->m_format))
            {
                return false;
            }
        }
    }

    return true;
}

auto ensure_rgba8_opaque_alpha(bimg::ImageContainer* _image) -> void
{
    if(nullptr == _image)
    {
        return;
    }

    if(_image->m_format != bimg::TextureFormat::RGBA8)
    {
        return;
    }

    for(uint8_t mip = 0; mip < _image->m_numMips; ++mip)
    {
        for(uint16_t layer = 0; layer < _image->m_numLayers; ++layer)
        {
            bimg::ImageMip mipData;
            if(!bimg::imageGetRawData(*_image, layer, mip, _image->m_data, _image->m_size, mipData))
            {
                continue;
            }

            const uint32_t width = mipData.m_width;
            const uint32_t height = mipData.m_height;
            const uint32_t depth = bx::max<uint32_t>(1, mipData.m_depth);
            const uint32_t row_stride = width * 4;
            const uint32_t slice_stride = row_stride * height;

            uint8_t* slice = const_cast<uint8_t*>(mipData.m_data);
            for(uint32_t zz = 0; zz < depth; ++zz)
            {
                uint8_t* row = slice + zz * slice_stride;
                for(uint32_t yy = 0; yy < height; ++yy)
                {
                    uint8_t* pixel = row;
                    for(uint32_t xx = 0; xx < width; ++xx)
                    {
                        // PNG RGB sources often decode with alpha=0; make preview/texturec input opaque.
                        if(0 == pixel[3])
                        {
                            pixel[3] = 255;
                        }
                        pixel += 4;
                    }
                    row += row_stride;
                }
            }
        }
    }
}

} // namespace

bool imageFlipTangentSpaceNormalY(bimg::ImageContainer*& _image)
{
    if(NULL == _image)
    {
        return false;
    }

    if(bimg::isCompressed(_image->m_format))
    {
        bimg::ImageContainer* decoded =
            bimg::imageConvert(entry::getAllocator(), bimg::TextureFormat::RGBA8, *_image);
        if(NULL == decoded)
        {
            return false;
        }

        bimg::imageFree(_image);
        _image = decoded;
    }

    if(!flip_normal_y_all_mips_ldr(_image))
    {
        return false;
    }

    return true;
}

bool imagePrepareNormalMapBakePng(bimg::ImageContainer*& _image)
{
    if(nullptr == _image)
    {
        return false;
    }

    if(_image->m_format != bimg::TextureFormat::RGBA8)
    {
        bimg::ImageContainer* converted =
            bimg::imageConvert(entry::getAllocator(), bimg::TextureFormat::RGBA8, *_image);
        if(nullptr == converted)
        {
            return false;
        }

        bimg::imageFree(_image);
        _image = converted;
    }

    ensure_rgba8_opaque_alpha(_image);
    return true;
}

bgfx::TextureHandle loadTexture(const void* _data,
                                uint32_t _size,
                                uint64_t _flags,
                                uint8_t _skip,
                                bgfx::TextureInfo* _info,
                                bimg::Orientation::Enum* _orientation,
                                const char* _name,
                                bx::Error* _err,
                                const TexturePreCreateFn& _preCreate)
{
    BX_UNUSED(_skip);
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

    if (NULL != _info)
	{
		bx::memSet(_info, 0, sizeof(*_info) );
		_info->format = bgfx::TextureFormat::Unknown;
	}

	if (NULL != _orientation)
	{
		*_orientation = bimg::Orientation::R0;
	}

    bx::Error localErr;
    bx::Error* err = (NULL != _err) ? _err : &localErr;
    bimg::ImageContainer* imageContainer = bimg::imageParse(entry::getAllocator(), _data, _size, bimg::TextureFormat::Count, err);
    if(NULL != imageContainer)
    {
        if(NULL != _orientation)
        {
            *_orientation = imageContainer->m_orientation;
        }

        handle = loadTextureFromContainer(imageContainer, _flags, _info, _preCreate);

        if(bgfx::isValid(handle) && NULL != _name)
        {
            bgfx::setName(handle, _name, int32_t(bx::strLen(_name)));
        }
    }

    return handle;
}

bgfx::TextureHandle loadTexture(bx::FileReaderI* _reader,
                                const bx::FilePath& _filePath,
                                uint64_t _flags,
                                uint8_t _skip,
                                bgfx::TextureInfo* _info,
                                bimg::Orientation::Enum* _orientation,
                                bx::Error* _err,
                                const TexturePreCreateFn& _preCreate = nullptr)
{
    BX_UNUSED(_skip);
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

    if (NULL != _info)
	{
		bx::memSet(_info, 0, sizeof(*_info) );
		_info->format = bgfx::TextureFormat::Unknown;
	}

	if (NULL != _orientation)
	{
		*_orientation = bimg::Orientation::R0;
	}

    uint32_t size;
    void* data = load(_reader, entry::getAllocator(), _filePath, &size);
    if(NULL != data)
    {
        const bx::StringView name(_filePath);
        handle = loadTexture(data, size, _flags, _skip, _info, _orientation, name.getPtr(), _err, _preCreate);
        unload(data);
    }

    return handle;
}

bgfx::TextureHandle loadTexture(const bx::FilePath& _filePath,
                                uint64_t _flags,
                                uint8_t _skip,
                                bgfx::TextureInfo* _info,
                                bimg::Orientation::Enum* _orientation,
                                bx::Error* _err,
                                const TexturePreCreateFn& _preCreate)
{
    entry::FileReader reader;
    return loadTexture(&reader, _filePath, _flags, _skip, _info, _orientation, _err, _preCreate);
}

bimg::ImageContainer* imageLoad(const void* data, uint32_t size, bgfx::TextureFormat::Enum _dstFormat)
{
    return bimg::imageParse(entry::getAllocator(), data, size, bimg::TextureFormat::Enum(_dstFormat));
}

bimg::ImageContainer* imageLoad(const bx::FilePath& _filePath, bgfx::TextureFormat::Enum _dstFormat)
{
    entry::FileReader reader;

    uint32_t size = 0;
    void* data = loadMem(&reader, entry::getAllocator(), _filePath, &size);

    if(data == nullptr)
    {
        return nullptr;
    }

    return bimg::imageParse(entry::getAllocator(), data, size, bimg::TextureFormat::Enum(_dstFormat));
}

bool imageParseInfo(const void* _data, uint32_t _size, bimg::ImageContainer& _info, bx::Error* _err)
{
    if(_data == nullptr || _size == 0)
    {
        return false;
    }

    return bimg::imageParseInfo(entry::getAllocator(), _info, _data, _size, _err);
}

bool imageParseInfo(const bx::FilePath& _filePath, bimg::ImageContainer& _info, bx::Error* _err)
{
    uint32_t size = 0;
    entry::FileReader reader;
    void* data = load(&reader, entry::getAllocator(), _filePath, &size);
    if(data == nullptr)
    {
        return false;
    }

    const bool parsed = bimg::imageParseInfo(entry::getAllocator(), _info, data, size, _err);
    unload(data);
    return parsed;
}

void calcTangents(void* _vertices,
                  uint16_t _numVertices,
                  bgfx::VertexLayout _layout,
                  const uint16_t* _indices,
                  uint32_t _numIndices)
{
    struct PosTexcoord
    {
        float m_x;
        float m_y;
        float m_z;
        float m_pad0;
        float m_u;
        float m_v;
        float m_pad1;
        float m_pad2;
    };

    float* tangents = new float[6 * _numVertices];
    bx::memSet(tangents, 0, 6 * _numVertices * sizeof(float));

    PosTexcoord v0;
    PosTexcoord v1;
    PosTexcoord v2;

    for(uint32_t ii = 0, num = _numIndices / 3; ii < num; ++ii)
    {
        const uint16_t* indices = &_indices[ii * 3];
        uint32_t i0 = indices[0];
        uint32_t i1 = indices[1];
        uint32_t i2 = indices[2];

        bgfx::vertexUnpack(&v0.m_x, bgfx::Attrib::Position, _layout, _vertices, i0);
        bgfx::vertexUnpack(&v0.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i0);

        bgfx::vertexUnpack(&v1.m_x, bgfx::Attrib::Position, _layout, _vertices, i1);
        bgfx::vertexUnpack(&v1.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i1);

        bgfx::vertexUnpack(&v2.m_x, bgfx::Attrib::Position, _layout, _vertices, i2);
        bgfx::vertexUnpack(&v2.m_u, bgfx::Attrib::TexCoord0, _layout, _vertices, i2);

        const float bax = v1.m_x - v0.m_x;
        const float bay = v1.m_y - v0.m_y;
        const float baz = v1.m_z - v0.m_z;
        const float bau = v1.m_u - v0.m_u;
        const float bav = v1.m_v - v0.m_v;

        const float cax = v2.m_x - v0.m_x;
        const float cay = v2.m_y - v0.m_y;
        const float caz = v2.m_z - v0.m_z;
        const float cau = v2.m_u - v0.m_u;
        const float cav = v2.m_v - v0.m_v;

        const float det = (bau * cav - bav * cau);
        const float invDet = 1.0f / det;

        const float tx = (bax * cav - cax * bav) * invDet;
        const float ty = (bay * cav - cay * bav) * invDet;
        const float tz = (baz * cav - caz * bav) * invDet;

        const float bx = (cax * bau - bax * cau) * invDet;
        const float by = (cay * bau - bay * cau) * invDet;
        const float bz = (caz * bau - baz * cau) * invDet;

        for(uint32_t jj = 0; jj < 3; ++jj)
        {
            float* tanu = &tangents[indices[jj] * 6];
            float* tanv = &tanu[3];
            tanu[0] += tx;
            tanu[1] += ty;
            tanu[2] += tz;

            tanv[0] += bx;
            tanv[1] += by;
            tanv[2] += bz;
        }
    }

    for(uint32_t ii = 0; ii < _numVertices; ++ii)
    {
        const bx::Vec3 tanu = bx::load<bx::Vec3>(&tangents[ii * 6]);
        const bx::Vec3 tanv = bx::load<bx::Vec3>(&tangents[ii * 6 + 3]);

        float nxyzw[4];
        bgfx::vertexUnpack(nxyzw, bgfx::Attrib::Normal, _layout, _vertices, ii);

        const bx::Vec3 normal = bx::load<bx::Vec3>(nxyzw);
        const float ndt = bx::dot(normal, tanu);
        const bx::Vec3 nxt = bx::cross(normal, tanu);
        const bx::Vec3 tmp = bx::sub(tanu, bx::mul(normal, ndt));

        float tangent[4];
        bx::store(tangent, bx::normalize(tmp));
        tangent[3] = bx::dot(nxt, tanv) < 0.0f ? -1.0f : 1.0f;

        bgfx::vertexPack(tangent, true, bgfx::Attrib::Tangent, _layout, _vertices, ii);
    }

    delete[] tangents;
}

bool saveToFile(bgfx::ViewId viewId, const bx::FilePath& _filePath, bgfx::FrameBufferHandle fbo, uint32_t width, uint32_t height)
{

    auto input_tex = bgfx::getTexture(fbo);
    // formats have one to one mapping
    auto format = bgfx::TextureFormat::RGBA8;
    auto bimg_format = static_cast<bimg::TextureFormat::Enum>(format);

    bool result = false;

    uint64_t flags = 0 | BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK | BGFX_SAMPLER_U_CLAMP |
                     BGFX_SAMPLER_V_CLAMP;
    auto blit_tex = bgfx::createTexture2D(width, height, false, 1, format, flags, nullptr);

    bgfx::TextureInfo info;
    bgfx::calcTextureSize(info, width, height, 1, false, false, 1, format);

    // Blit and read
    bgfx::touch(viewId);
    bgfx::blit(viewId, blit_tex, 0, 0, input_tex);

    // Allocate memory for the texture data

    tinystl::vector<uint8_t> input(info.storageSize);

    // Read the frame buffer data
    uint32_t frameNumber = bgfx::readTexture(blit_tex, input.data());


    // Wait until the data is available (frameNumber indicates when)
    while(bgfx::frame() != frameNumber)
    {
      // You can perform other tasks here if needed
        break;
    }

    bx::FilePath filePath(_filePath);

    if(bx::makeAll(filePath.getPath()))
    {
        bx::FileWriter writer;
        if(bx::open(&writer, filePath))
        {
            bx::Error err;
            bimg::imageWritePng(&writer,
                                info.width,
                                info.height,
                                info.width * (info.bitsPerPixel / 8),
                                input.data(),
                                bimg_format,
                                false,
                                &err);
            result = true;
            bx::close(&writer);
        }
    }

    return result;
}

bool imageSave(const char* saveAs, bimg::ImageContainer* image, const char* format_hint)
{
    if (!image || !saveAs)
    {
        return false;
    }
    // Format dispatch key: explicit hint (final destination path/ext) or save path.
    const char* format_key = (format_hint && format_hint[0] != '\0') ? format_hint : saveAs;
    bx::FileWriter writer;
    bx::Error err;

    if (bx::open(&writer, saveAs, false, &err))
    {
        if (!bx::strFindI(format_key, "tga").isEmpty())
        {
            bimg::imageWriteTga(&writer, image->m_width, image->m_height, image->m_width * 4, image->m_data, false, false, &err);
        }
        else if (!bx::strFindI(format_key, "ktx").isEmpty())
        {
            bimg::imageWriteKtx(&writer, *image, image->m_data, image->m_size, &err);
        }
        else if (!bx::strFindI(format_key, "dds").isEmpty())
        {
            bimg::imageWriteDds(&writer, *image, image->m_data, image->m_size, &err);
        }
        else if (!bx::strFindI(format_key, "png").isEmpty())
        {
            if (image->m_format != bimg::TextureFormat::RGBA8)
            {
                auto converted = bimg::imageConvert(entry::getAllocator(), bimg::TextureFormat::RGBA8, *image);
                if(converted)
                {
                    bimg::ImageMip mip;
                    bimg::imageGetRawData(*converted, 0, 0, converted->m_data, converted->m_size, mip);
                    bimg::imageWritePng(&writer
                                        , mip.m_width
                                        , mip.m_height
                                        , mip.m_width*4
                                        , mip.m_data
                                        , converted->m_format
                                        , false
                                        , &err
                                        );
                    bimg::imageFree(converted);
                }
                else
                {
                    //err.setError(bx::ErrorResult())
                }
                //help("Incompatible image texture format. image PNG format must be RGBA8.", err);
            }
            else
            {
                bimg::ImageMip mip;
                bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip);
                bimg::imageWritePng(&writer
                                    , mip.m_width
                                    , mip.m_height
                                    , mip.m_width*4
                                    , mip.m_data
                                    , image->m_format
                                    , false
                                    , &err
                                    );
            }
        }
        else if (!bx::strFindI(format_key, "exr").isEmpty())
        {
            bimg::ImageMip mip;
            bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip);
            bimg::imageWriteExr(&writer
                                , mip.m_width
                                , mip.m_height
                                , mip.m_width*8
                                , mip.m_data
                                , image->m_format
                                , false
                                , &err
                                );
        }
        else if (!bx::strFindI(format_key, "hdr").isEmpty())
        {
            bimg::ImageMip mip;
            bimg::imageGetRawData(*image, 0, 0, image->m_data, image->m_size, mip);
            bimg::imageWriteHdr(&writer
                                , mip.m_width
                                , mip.m_height
                                , mip.m_width*getBitsPerPixel(mip.m_format)/8
                                , mip.m_data
                                , image->m_format
                                , false
                                , &err
                                );
        }

        bx::close(&writer);
    }

    return err.isOk();
}
