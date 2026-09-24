#include "graphics.h"
#include "bgfx/bgfx.h"
#include "eviction.h"
#include "uniform.h"
#include <bimg/bimg.h>
#include <bx/file.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <map>
namespace gfx
{
namespace
{
auto get_loggers() -> std::map<std::string, std::function<void(const std::string&, const char*, uint16_t)>>&
{
    static std::map<std::string, std::function<void(const std::string&, const char*, uint16_t)>> loggers;
    return loggers;
}

auto get_cache_directory() -> std::string&
{
    static std::string dir;
    return dir;
}

/// One file per cache id inside the configured directory; empty path when caching is off.
auto make_cache_path(uint64_t id) -> std::filesystem::path
{
    const auto& dir = get_cache_directory();
    if(dir.empty())
    {
        return {};
    }
    char name[32]{};
    std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(id));
    return std::filesystem::path(dir) / name;
}

struct context_data
{
    bool initted = false;
    uint32_t frame{};

    bgfx::UniformHandle u_world{bgfx::kInvalidHandle};
    bgfx::UniformHandle u_prev_world{bgfx::kInvalidHandle};
    bgfx::TextureHandle fallback_texture{bgfx::kInvalidHandle};
    allocation_failure_policy allocation_policy = allocation_failure_policy::skip_with_fallback;

} s_context;

auto create_fallback_texture() -> bgfx::TextureHandle
{
    constexpr std::uint16_t dim = 4;
    std::array<std::uint8_t, dim * dim * 4> pixels{};
    for(std::size_t i = 0; i < dim * dim; ++i)
    {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = 255;
    }
    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    return bgfx::createTexture2D(dim,
                                 dim,
                                 false,
                                 1,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
                                 mem);
}

} // namespace

void set_trace_logger(const std::function<void(const std::string&, const char*, uint16_t)>& logger)
{
    get_loggers()["trace"] = logger;
}

void set_debug_logger(const std::function<void(const std::string&, const char*, uint16_t)>& logger)
{
    get_loggers()["debug"] = logger;
}

void set_info_logger(const std::function<void(const std::string&, const char*, uint16_t)>& logger)
{
    get_loggers()["info"] = logger;
}
void set_warning_logger(const std::function<void(const std::string&, const char*, uint16_t)>& logger)
{
    get_loggers()["warning"] = logger;
}
void set_error_logger(const std::function<void(const std::string&, const char*, uint16_t)>& logger)
{
    get_loggers()["error"] = logger;
}

void log(const std::string& category, const std::string& log_msg, const char* _filePath, uint16_t _line)
{
    if(get_loggers()[category])
    {
        get_loggers()[category](log_msg, _filePath, _line);
    }
}

namespace
{
struct profiler_hook_state
{
    gfx_profiler_begin_hook on_begin{};
    gfx_profiler_begin_literal_hook on_begin_literal{};
    gfx_profiler_end_hook on_end{};
};

auto profiler_hooks() -> profiler_hook_state&
{
    static profiler_hook_state s{};
    return s;
}

} // namespace

struct gfx_callback final : public bgfx::CallbackI
{
    ~gfx_callback() = default;

    void traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList) final
    {
        char temp[8192];
        char* out = temp;
        int32_t len = vsnprintf(out, sizeof(temp), _format, _argList);
        if((int32_t)sizeof(temp) < len)
        {
            out = (char*)alloca(len + 1);
            len = vsnprintf(out, len, _format, _argList);
        }
        out[len] = '\0';

        bx::StringView out_view(out, len);
        // Determine log type based on prefix
        if (bx::strCmp(out_view, "WARN ") == 0)
        {
            log("warning", out_view.getPtr() + 5, _filePath, _line);
        }
        else
        {
            log("debug", out, _filePath, _line);
        }
    }

    void profilerBegin(const char* _name, uint32_t _abgr, const char* _filePath, uint16_t _line) final
    {
        if(const auto& fn = profiler_hooks().on_begin)
        {
            fn(_name, _abgr, _filePath, _line);
        }
    }

    void profilerBeginLiteral(const char* _name, uint32_t _abgr, const char* _filePath, uint16_t _line) final
    {
        if(const auto& fn = profiler_hooks().on_begin_literal)
        {
            fn(_name, _abgr, _filePath, _line);
        }
    }

    void profilerEnd() final
    {
        if(const auto& fn = profiler_hooks().on_end)
        {
            fn();
        }
    }
    void fatal(const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str) final
    {
        switch(_code)
        {
            case bgfx::Fatal::InvalidShader:
            {
                std::string error_msg = "[Invalid Shader] " + std::string(_str);
                log("error", error_msg, _filePath, _line);
                break;
            }
            case bgfx::Fatal::UnableToInitialize:
            {
                std::string error_msg = "[Unable To Initialize] " + std::string(_str);
                log("error", error_msg, _filePath, _line);
                break;
            }
            case bgfx::Fatal::UnableToCreateTexture:
            {
                std::string error_msg = "[Unable To Create Texture] " + std::string(_str);
                log("error", error_msg, _filePath, _line);
                break;
            }
            case bgfx::Fatal::DeviceLost:
            {
                std::string error_msg = "[Device Lost] " + std::string(_str);
                log("error", error_msg, _filePath, _line);
                break;
            }
            case bgfx::Fatal::DebugCheck:
            {
                std::string error_msg = "[Debug Check] " + std::string(_str);
                log("error", error_msg, _filePath, _line);
                break;
            }
            default:
            {
                log("error", _str, _filePath, _line);
                break;
            }
        }

    }

    // Binary cache for driver-compiled pipeline state. Backends that compile pipelines at
    // first use (D3D12 especially) otherwise re-pay the full driver compilation of every
    // heavy shader on every launch - measured as a single ~30 second frame the first time a
    // large compute chain (GI) ran. A stale or mismatched blob is safe: the backends verify
    // and fall back to a fresh compile, then overwrite the entry.
    uint32_t cacheReadSize(uint64_t _id) final
    {
        const auto path = make_cache_path(_id);
        if(path.empty())
        {
            return 0;
        }
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        return ec ? 0u : uint32_t(size);
    }

    bool cacheRead(uint64_t _id, void* _data, uint32_t _size) final
    {
        const auto path = make_cache_path(_id);
        if(path.empty())
        {
            return false;
        }
        std::FILE* file = std::fopen(path.string().c_str(), "rb");
        if(file == nullptr)
        {
            return false;
        }
        const size_t read = std::fread(_data, 1, _size, file);
        std::fclose(file);
        return read == _size;
    }

    void cacheWrite(uint64_t _id, const void* _data, uint32_t _size) final
    {
        const auto path = make_cache_path(_id);
        if(path.empty())
        {
            return;
        }
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        if(file == nullptr)
        {
            return;
        }
        std::fwrite(_data, 1, _size, file);
        std::fclose(file);
    }

    virtual void screenShot(			  
        const char* _filePath
        , uint32_t _width
        , uint32_t _height
        , uint32_t _pitch
        , bgfx::TextureFormat::Enum _format
        , const void* _data
        , uint32_t _size
        , bool _yflip) override
    {
        const std::string full_path = std::string(_filePath) + ".png";

        bx::FileWriter writer;
        if (bx::open(&writer, full_path.c_str()))
        {
            //if (image_type == ImageType::Png)
            {
                bimg::imageWritePng(&writer,
                                    _width,
                                    _height,
                                    _pitch,
                                    _data,
                                    static_cast<bimg::TextureFormat::Enum>(_format),
                                    _yflip,
                                    nullptr);
            }
            // else
            // {
            //     bimg::imageWriteTga(&writer, _width, _height, _pitch, _data, false, _yflip);
            // }

            bx::close(&writer);
        }
    }

    void captureBegin(uint32_t /*_width*/,
                      uint32_t /*_height*/,
                      uint32_t /*_pitch*/,
                      bgfx::TextureFormat::Enum /*_format*/,
                      bool /*_yflip*/) final
    {
    }

    void captureEnd() final
    {
    }

    void captureFrame(const void* /*_data*/, uint32_t /*_size*/) final
    {
    }
};

void set_profiler_hooks(gfx_profiler_begin_hook on_begin,
                        gfx_profiler_begin_literal_hook on_begin_literal,
                        gfx_profiler_end_hook on_end)
{
    auto& h = profiler_hooks();
    h.on_begin = std::move(on_begin);
    h.on_begin_literal = std::move(on_begin_literal);
    h.on_end = std::move(on_end);
}

void clear_profiler_hooks()
{
    auto& h = profiler_hooks();
    h.on_begin = {};
    h.on_begin_literal = {};
    h.on_end = {};
}

void shutdown()
{
    clear_profiler_hooks();
    if(s_context.initted)
    {
        eviction::shutdown();
        if(bgfx::isValid(s_context.fallback_texture))
        {
            bgfx::destroy(s_context.fallback_texture);
            s_context.fallback_texture = {bgfx::kInvalidHandle};
        }
        deinit_uniform_cache();
        bgfx::destroy(s_context.u_world);
        bgfx::destroy(s_context.u_prev_world);
        bgfx::shutdown();
    }
    s_context = {};
}

void set_cache_directory(const std::string& directory)
{
    auto& dir = get_cache_directory();
    dir = directory;
    if(!dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if(ec)
        {
            log("warning", "Failed to create the renderer cache directory: " + dir, __FILE__, __LINE__);
            dir.clear();
        }
    }
}

void set_allocation_failure_policy(allocation_failure_policy policy)
{
    s_context.allocation_policy = policy;
}

auto get_allocation_failure_policy() -> allocation_failure_policy
{
    return s_context.allocation_policy;
}

auto fallback_texture() -> bgfx::TextureHandle
{
    return s_context.fallback_texture;
}

bool init(bgfx::Init init_data)
{
    if(init_data.callback == nullptr)
    {
        static gfx_callback callback;
        init_data.callback = &callback;
    }
    s_context.frame = 0;
    s_context.initted = bgfx::init(init_data);
    s_context.u_world = bgfx::createUniform("u_world", bgfx::UniformType::Mat4, get_max_blend_transforms());
    s_context.u_prev_world = bgfx::createUniform("u_prev_world", bgfx::UniformType::Mat4, get_max_blend_transforms());

    bgfx::frame();
    // Initialize after the first frame so the backend has reported its GPU memory budget.
    switch(eviction::init())
    {
        case eviction::init_status::ok:
            log("info", "GPU resource eviction enabled.", __FILE__, __LINE__);
            break;
        case eviction::init_status::unnecessary:
            log("info", "GPU resource eviction disabled: backend does not need eviction.", __FILE__, __LINE__);
            break;
        case eviction::init_status::unsupported:
            log("info",
                "GPU resource eviction disabled: backend does not report a GPU memory budget.",
                __FILE__,
                __LINE__);
            break;
        case eviction::init_status::failed:
            log("warning", "GPU resource eviction failed to initialize.", __FILE__, __LINE__);
            break;
    }
    if(s_context.initted)
    {
        s_context.fallback_texture = create_fallback_texture();
        if(bgfx::isValid(s_context.fallback_texture))
        {
            bgfx::setName(s_context.fallback_texture, "gfx-fallback");
        }
    }
    return s_context.initted;
}

uint32_t frame(uint8_t _flags)
{
    s_context.frame = bgfx::frame(_flags);
    eviction::set_frame(s_context.frame);
    eviction::clear_queued_allocations();
    return s_context.frame;
}

void set_image_3d(uint8_t _stage,
                  bgfx::TextureHandle _handle,
                  uint8_t _mip,
                  bgfx::Access::Enum _access,
                  bgfx::TextureFormat::Enum _format)
{
    // The explicit layer range is the whole point: see the header note. (0, 1) is the full
    // range of a 3D texture on every backend, and its presence makes the GL binding layered.
    bgfx::setImage(_stage, _handle, 0, 1, _mip, _access, _format);
}

void frames(int _count, int32_t _flags)
{
    for(int i = 0; i < _count; ++i)
    {
        frame(_flags);
    }
}

auto screen_quad(float dest_width, float dest_height, float depth, float width, float height) -> uint64_t
{
    float texture_half = get_half_texel();
    bool origin_bottom_left = is_origin_bottom_left();

    if(3 == getAvailTransientVertexBuffer(3, pos_texcoord0_vertex::get_layout()))
    {
        bgfx::TransientVertexBuffer vb;
        bgfx::allocTransientVertexBuffer(&vb, 3, pos_texcoord0_vertex::get_layout());
        auto vertex = reinterpret_cast<pos_texcoord0_vertex*>(vb.data);

        const float minx = -width;
        const float maxx = width;
        const float miny = 0.0f;
        const float maxy = height * 2.0f;

        const float texel_half_w = texture_half / dest_width;
        const float texel_half_h = texture_half / dest_height;
        const float minu = -1.0f + texel_half_w;
        const float maxu = 1.0f + texel_half_h;

        const float zz = depth;

        float minv = texel_half_h;
        float maxv = 2.0f + texel_half_h;

        if(origin_bottom_left)
        {
            float temp = minv;
            minv = maxv;
            maxv = temp;

            minv -= 1.0f;
            maxv -= 1.0f;
        }

        vertex[0].x = minx;
        vertex[0].y = miny;
        vertex[0].z = zz;
        vertex[0].u = minu;
        vertex[0].v = minv;

        vertex[1].x = maxx;
        vertex[1].y = miny;
        vertex[1].z = zz;
        vertex[1].u = maxu;
        vertex[1].v = minv;

        vertex[2].x = maxx;
        vertex[2].y = maxy;
        vertex[2].z = zz;
        vertex[2].u = maxu;
        vertex[2].v = maxv;

        bgfx::setVertexBuffer(0, &vb);
    }

    return 0;
}

auto clip_fullscreen_triangle(float depth, float width, float height) -> uint64_t
{
    bool origin_bottom_left = is_origin_bottom_left();

    if(3 != getAvailTransientVertexBuffer(3, pos_texcoord0_vertex::get_layout()))
    {
        return 0;
    }

    bgfx::TransientVertexBuffer vb;
    bgfx::allocTransientVertexBuffer(&vb, 3, pos_texcoord0_vertex::get_layout());
    auto vertex = reinterpret_cast<pos_texcoord0_vertex*>(vb.data);

    const float minx = -width;
    const float maxx = width;
    const float miny = -height;
    const float maxy = height;

    const float minu = 0.0f;
    const float maxu = 1.0f;

    const float zz = depth;

    float minv = 1.0f;
    float maxv = 0.0f;

    if(origin_bottom_left)
    {
        minv = 1.0f - minv;
        maxv = 1.0f - maxv;
    }

    // One triangle covering [-width,width] x [-height,height] with extrapolated UVs (no quad diagonal).
    // v0: BL, v1: bottom edge extrapolated past BR, v2: left edge extrapolated past TL.
    vertex[0].x = minx;
    vertex[0].y = miny;
    vertex[0].z = zz;
    vertex[0].u = minu;
    vertex[0].v = minv;

    vertex[1].x = maxx + 2.0f * width;
    vertex[1].y = miny;
    vertex[1].z = zz;
    vertex[1].u = minu + 2.0f * (maxu - minu);
    vertex[1].v = minv;

    vertex[2].x = minx;
    vertex[2].y = maxy + 2.0f * height;
    vertex[2].z = zz;
    vertex[2].u = minu;
    vertex[2].v = minv + 2.0f * (maxv - minv);

    bgfx::setVertexBuffer(0, &vb);

    return BGFX_STATE_PT_TRISTRIP;
}

auto clip_quad(float depth, float width, float height) -> uint64_t
{
    // float texture_half = get_half_texel();
    bool origin_bottom_left = is_origin_bottom_left();

    if(4 == getAvailTransientVertexBuffer(4, pos_texcoord0_vertex::get_layout()))
    {
        bgfx::TransientVertexBuffer vb;
        bgfx::allocTransientVertexBuffer(&vb, 4, pos_texcoord0_vertex::get_layout());
        auto vertex = reinterpret_cast<pos_texcoord0_vertex*>(vb.data);

        const float minx = -width;
        const float maxx = width;
        const float miny = -height;
        const float maxy = height;

        // const float texel_half_w = texture_half;
        // const float texel_half_h = texture_half;
        const float minu = 0.0f;
        const float maxu = 1.0f;

        const float zz = depth;

        float minv = 1.0f;
        float maxv = 0.0f;

        if(origin_bottom_left)
        {
            minv = 1.0f - minv;
            maxv = 1.0f - maxv;
        }

        vertex[0].x = minx;
        vertex[0].y = maxy;
        vertex[0].z = zz;
        vertex[0].u = minu;
        vertex[0].v = maxv;

        vertex[1].x = maxx;
        vertex[1].y = maxy;
        vertex[1].z = zz;
        vertex[1].u = maxu;
        vertex[1].v = maxv;

        vertex[2].x = minx;
        vertex[2].y = miny;
        vertex[2].z = zz;
        vertex[2].u = minu;
        vertex[2].v = minv;

        vertex[3].x = maxx;
        vertex[3].y = miny;
        vertex[3].z = zz;
        vertex[3].u = maxu;
        vertex[3].v = minv;

        bgfx::setVertexBuffer(0, &vb);
    }

    return BGFX_STATE_PT_TRISTRIP;
}

uint64_t clip_quad_ex(const clip_quad_def& def)
{
    // float texture_half = get_half_texel();
    bool origin_bottom_left = is_origin_bottom_left();

    if(4 == getAvailTransientVertexBuffer(4, pos_texcoord0_vertex::get_layout()))
    {
        bgfx::TransientVertexBuffer vb;
        bgfx::allocTransientVertexBuffer(&vb, 4, pos_texcoord0_vertex::get_layout());
        auto vertex = reinterpret_cast<pos_texcoord0_vertex*>(vb.data);

        // Apply position offset to the quad geometry
        const float minx = -def.width + def.offset_x;
        const float maxx = def.width + def.offset_x;
        const float miny = -def.height + def.offset_y;
        const float maxy = def.height + def.offset_y;

        // const float texel_half_w = texture_half;
        // const float texel_half_h = texture_half;
        const float minu = 0.0f;
        const float maxu = 1.0f;

        const float zz = def.depth;

        float minv = 1.0f;
        float maxv = 0.0f;

        vertex[0].x = minx;
        vertex[0].y = maxy;
        vertex[0].z = zz;
        vertex[0].u = minu;
        vertex[0].v = maxv;

        vertex[1].x = maxx;
        vertex[1].y = maxy;
        vertex[1].z = zz;
        vertex[1].u = maxu;
        vertex[1].v = maxv;

        vertex[2].x = minx;
        vertex[2].y = miny;
        vertex[2].z = zz;
        vertex[2].u = minu;
        vertex[2].v = minv;

        vertex[3].x = maxx;
        vertex[3].y = miny;
        vertex[3].z = zz;
        vertex[3].u = maxu;
        vertex[3].v = minv;

        // Apply UV offset and scaling
        for(int i = 0; i < 4; i++)
        {
            vertex[i].u = (vertex[i].u * def.uv_scaling_x) + def.uv_offset_x;
            vertex[i].v = (vertex[i].v * def.uv_scaling_y) + def.uv_offset_y;
            if(origin_bottom_left)
            {
                vertex[i].v = 1.0f - vertex[i].v;
            }
        }

        bgfx::setVertexBuffer(0, &vb);
    }

    return BGFX_STATE_PT_TRISTRIP;
}

void get_size_from_ratio(bgfx::BackbufferRatio::Enum _ratio, uint16_t& _width, uint16_t& _height)
{
    auto stats = bgfx::getStats();
    _width = stats->width;
    _height = stats->height;
    switch(_ratio)
    {
        case bgfx::BackbufferRatio::Half:
            _width /= 2;
            _height /= 2;
            break;
        case bgfx::BackbufferRatio::Quarter:
            _width /= 4;
            _height /= 4;
            break;
        case bgfx::BackbufferRatio::Eighth:
            _width /= 8;
            _height /= 8;
            break;
        case bgfx::BackbufferRatio::Sixteenth:
            _width /= 16;
            _height /= 16;
            break;
        case bgfx::BackbufferRatio::Double:
            _width *= 2;
            _height *= 2;
            break;

        default:
            break;
    }

    _width = std::max<uint16_t>(1, _width);
    _height = std::max<uint16_t>(1, _height);
}

auto get_renderer_filename_extension(bgfx::RendererType::Enum _type) -> const std::string&
{
    static const std::map<bgfx::RendererType::Enum, std::string> types = {{bgfx::RendererType::Direct3D11, ".dxbc"},
                                                                          {bgfx::RendererType::Direct3D12, ".dxil"},
                                                                          {bgfx::RendererType::Gnm, ".pssl"},
                                                                          {bgfx::RendererType::Metal, ".metal"},
                                                                          {bgfx::RendererType::Nvn, ".nvn"},

                                                                          {bgfx::RendererType::OpenGL, ".gl"},
                                                                          {bgfx::RendererType::OpenGLES, ".essl"},
                                                                          {bgfx::RendererType::Vulkan, ".spirv"}};

    const auto it = types.find(_type);
    if(it != types.cend())
    {
        return it->second;
    }
    static std::string unknown = ".unknown";
    return unknown;
}



auto get_current_renderer_filename_extension() -> const std::string&
{
    return get_renderer_filename_extension(bgfx::getRendererType());
}

auto get_renderer_platform_supported_filename_extensions() -> const std::set<std::string>&
{
#if BX_PLATFORM_WINDOWS
    static const std::set<std::string> supported = {get_renderer_filename_extension(bgfx::RendererType::Direct3D11),
                                                       get_renderer_filename_extension(bgfx::RendererType::Direct3D12),
                                                       get_renderer_filename_extension(bgfx::RendererType::OpenGL),
                                                       get_renderer_filename_extension(bgfx::RendererType::Vulkan)};
    return supported;
#else
    static const std::set<std::string> supported = {get_current_renderer_filename_extension()};
    return supported;
#endif
}


auto get_renderer_based_on_filename_extension(const std::string& _type) -> bgfx::RendererType::Enum
{
    for(int i = bgfx::RendererType::Noop; i < bgfx::RendererType::Count; ++i)
    {
        auto rtype = bgfx::RendererType::Enum(i);
        if(get_renderer_filename_extension(rtype) == _type)
        {
            return rtype;
        }
    }

    return bgfx::getRendererType();
}



auto is_homogeneous_depth() -> bool
{
    //We handle this in shaders.
    return false;//get_caps()->homogeneousDepth;
}

auto is_origin_bottom_left() -> bool
{
    return bgfx::getCaps()->originBottomLeft;
}

auto get_max_blend_transforms() -> uint32_t
{
    return 128;
}

auto get_half_texel() -> float
{
    float half_texel = 0.0f;
    return half_texel;
}

auto is_supported(uint64_t flag) -> bool
{
    const auto caps = bgfx::getCaps();
    bool supported = 0 != (caps->supported & flag);
    return supported;
}

bool check_avail_transient_buffers(uint32_t _numVertices,
                                   const bgfx::VertexLayout& _layout,
                                   uint32_t _numIndices,
                                   bool _index32)
{
    return _numVertices == bgfx::getAvailTransientVertexBuffer(_numVertices, _layout) &&
           (0 == _numIndices || _numIndices == bgfx::getAvailTransientIndexBuffer(_numIndices, _index32));
}

uint32_t get_render_frame()
{
    return s_context.frame;
}

void set_world_transform(const void* _mtx, uint16_t _num)
{
    bgfx::setUniform(s_context.u_world, _mtx, _num);
}

void set_prev_world_transform(const void* _mtx, uint16_t _num)
{
    bgfx::setUniform(s_context.u_prev_world, _mtx, _num);
}


namespace
{
constexpr uint64_t k_gpu_row_pitch_alignment = 256;
/// Minimum committed image allocation on most Vulkan/D3D12 drivers (typical imageMemReq alignment).
constexpr uint64_t k_gpu_min_image_allocation = 4096;

auto align_up_u64(uint64_t value, uint64_t alignment) -> uint64_t
{
    if(alignment == 0)
    {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

/// Tiled/uncompressed footprint with API row-pitch rules (D3D12 uses 256-byte pitch alignment).
auto estimate_row_pitch_layout_size(const bgfx::TextureInfo& info) -> uint64_t
{
    const auto format = static_cast<bimg::TextureFormat::Enum>(info.format);
    if(info.bitsPerPixel == 0 || bimg::isCompressed(format) || bimg::isDepth(format))
    {
        return info.storageSize;
    }

    const uint64_t bytes_per_pixel = (static_cast<uint64_t>(info.bitsPerPixel) + 7) / 8;
    const uint32_t faces = static_cast<uint32_t>(info.numLayers) * (info.cubeMap ? 6u : 1u);

    uint32_t width = info.width;
    uint32_t height = info.height;
    uint32_t depth = info.depth > 0 ? info.depth : 1u;

    uint64_t total = 0;
    for(uint32_t mip = 0; mip < info.numMips; ++mip)
    {
        const uint32_t mip_w = bx::max<uint32_t>(1u, width);
        const uint32_t mip_h = bx::max<uint32_t>(1u, height);
        const uint32_t mip_d = bx::max<uint32_t>(1u, depth);

        const uint64_t row_bytes = static_cast<uint64_t>(mip_w) * bytes_per_pixel;
        const uint64_t pitch = align_up_u64(row_bytes, k_gpu_row_pitch_alignment);
        total += pitch * mip_h * mip_d * faces;

        width = bx::max<uint32_t>(1u, width >> 1);
        height = bx::max<uint32_t>(1u, height >> 1);
        depth = bx::max<uint32_t>(1u, depth >> 1);
    }
    return total;
}

} // namespace

uint64_t estimate_texture_gpu_size(const bgfx::TextureInfo& _info, uint64_t _flags)
{
    // bgfx tracks textureMemoryUsed as the sum of storageSize per texture. For eviction bookkeeping
    // we start there, then uplift uncompressed layouts to row-pitch size (256-byte alignment) which
    // better matches committed image allocations. We do NOT apply D3D12 placement alignment (64 KiB)
    // — bgfx uses committed resources / dedicated Vk allocations, not suballocated heap placement.
    uint64_t size = std::max<uint64_t>(_info.storageSize, estimate_row_pitch_layout_size(_info));

    // MSAA render targets allocate sample_count x the single-sample surface.
    const uint64_t msaa_field = (_flags & BGFX_TEXTURE_RT_MSAA_MASK) >> BGFX_TEXTURE_RT_MSAA_SHIFT;
    if(msaa_field >= 2)
    {
        size *= (uint64_t(1) << (msaa_field - 1));
    }

    // Committed image allocations are typically rounded to at least one page (4 KiB) on device-local
    // heaps; cap the uplift so tiny textures are not reported as 64 KiB each.
    if(size > 0 && size < k_gpu_min_image_allocation)
    {
        size = k_gpu_min_image_allocation;
    }
    return size;
}


} // namespace gfx
