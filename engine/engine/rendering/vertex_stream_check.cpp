#include "vertex_stream_check.h"

#include "gpu_program.h"

#include <engine/assets/asset_manager.h>

#include <graphics/device_caps.h>
#include <graphics/frame_buffer.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/texture.h>
#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace unravel
{
namespace
{
namespace stream_check
{
/// The grid of cells the check draws: one triangle a cell, whose vertex shader compares the vertex's attributes.
constexpr uint32_t COLUMNS = 16;
constexpr uint32_t ROWS = 20;
constexpr uint32_t CELL_PIXELS = 8;
constexpr uint32_t CELLS = COLUMNS * ROWS;
constexpr uint32_t VERTICES_PER_CELL = 3;
constexpr uint32_t VERTICES = CELLS * VERTICES_PER_CELL;
/// Each path draws a fifth of the grid, in two draws over index ranges, as a mesh draws its submeshes.
constexpr uint32_t PATHS = 5;
constexpr uint32_t CELLS_PER_PATH = CELLS / PATHS;
constexpr uint32_t DRAWS_PER_PATH = 2;
constexpr uint32_t CELLS_PER_DRAW = CELLS_PER_PATH / DRAWS_PER_PATH;
/// Threads per group of cs_vertex_stream_check.eshader.
constexpr uint32_t GROUP_SIZE = 64;
/// The written stream's and the mesh's stages in cs_vertex_stream_check.eshader.
constexpr uint8_t DEFORM_STAGE = 0;
constexpr uint8_t MESH_STAGE = 1;
/// Frames the check waits for its programs, and then for its readback, before it gives up.
constexpr uint32_t MAX_WAIT_FRAMES = 1800;
/// The mesh's positions sit this far right of and above their cells, so a draw that reads them from the mesh
/// instead of stream 0 draws off the target.
constexpr float MESH_POSITION_SHIFT = 4.0f;
/// The previous positions (stream 2) are the drawn ones plus this.
constexpr std::array<float, 3> PREVIOUS_POSITION_OFFSET{0.25f, 0.5f, 0.75f};
/// The pixel of a cell the result is read from: inside its triangle (the lower left half of the cell).
constexpr uint32_t READ_PIXEL_IN_CELL = 2;
/// The target's clear colour (0xRRGGBBAA): a cell that holds it was not drawn; a pixel that holds neither it nor a
/// drawn pixel's blue means the readback did not hold the target.
constexpr uint32_t CLEAR_COLOR = 0x408000ff;
constexpr std::array<uint8_t, 4> CLEAR_PIXEL{0x40, 0x80, 0x00, 0xff};

/// The attributes the vertex shader checks, in the order of its failure bits.
constexpr std::array<const char*, 10> ATTRIBUTE_NAMES{"position",
                                                      "normal",
                                                      "tangent",
                                                      "bitangent",
                                                      "texcoord0",
                                                      "color0",
                                                      "texcoord1",
                                                      "weight",
                                                      "indices",
                                                      "previous position (texcoord7)"};

/// The basis attributes stream 0 replaces, in the order of the mesh's vertex.
constexpr std::array<bgfx::Attrib::Enum, 3> BASIS_ATTRIBUTES{bgfx::Attrib::Normal,
                                                             bgfx::Attrib::Tangent,
                                                             bgfx::Attrib::Bitangent};

/// The paths the check draws. The last three are controls that tell apart why the first two fail on a device: the
/// draw itself, streams in general, or buffers the CPU updates.
enum class path : uint32_t
{
    /// Stream 0 and stream 2 written by the compute shader.
    compute,
    /// Stream 0 and stream 2 written by the CPU into buffers it updates, as on WebGL 2.
    cpu,
    /// The CPU's stream 0 and stream 2 in buffers created with their data, never updated.
    static_streams,
    /// Every attribute from one buffer created with its data.
    single_stream,
    /// Every attribute from one buffer the CPU updates.
    single_updated_stream,
};

constexpr std::array<path, 4> PATHS_WITHOUT_COMPUTE{path::cpu,
                                                    path::static_streams,
                                                    path::single_stream,
                                                    path::single_updated_stream};

auto get_path_name(path p) -> const char*
{
    switch(p)
    {
        case path::compute:
            return "compute-written streams";
        case path::cpu:
            return "CPU-written streams";
        case path::static_streams:
            return "streams from never-updated buffers";
        case path::single_stream:
            return "single-stream control";
        case path::single_updated_stream:
            return "single-stream control from a CPU-updated buffer";
    }
    return "";
}

/// The vertex layouts of one check: the mesh, the written stream, and the two layouts the draw binds them under.
struct layouts
{
    /// A skinned mesh: gfx::mesh_vertex (with its vertex colour and second UV set) and the skin attributes.
    bgfx::VertexLayout mesh;
    /// The written stream: position and the three basis vectors, packed as the mesh packs them.
    bgfx::VertexLayout deform;
    /// The mesh's stride without the attributes the written stream replaces.
    bgfx::VertexLayout rest;
    /// The written stream's stride holding only its position, as a_texcoord7.
    bgfx::VertexLayout previous;
    /// Every attribute the draw reads in one vertex, for the single-stream control.
    bgfx::VertexLayout single;
};

auto make_layouts() -> layouts
{
    layouts result;
    result.mesh.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float, false, true)
        .end();
    result.deform.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
        .end();
    result.rest.begin()
        .skip(uint8_t(result.deform.getStride()))
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float, false, true)
        .end();
    const uint16_t position_size = uint16_t(3 * sizeof(float));
    result.previous.begin()
        .add(bgfx::Attrib::TexCoord7, 3, bgfx::AttribType::Float)
        .skip(uint8_t(result.deform.getStride() - position_size))
        .end();
    result.single.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Tangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::Bitangent, 3, bgfx::AttribType::Uint8, true, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Float, false, true)
        .add(bgfx::Attrib::TexCoord7, 3, bgfx::AttribType::Float)
        .end();
    return result;
}

/// Whether each layout the draw binds a buffer under has the stride of that buffer, as bgfx requires.
auto has_matching_strides(const layouts& l) -> bool
{
    return l.rest.getStride() == l.mesh.getStride() && l.previous.getStride() == l.deform.getStride() &&
           l.deform.getStride() == 24;
}

/// Where corner @p v of the grid's triangles lies in clip space (vs_vertex_stream_check.eshader works it out alike).
auto get_cell_position(uint32_t v) -> std::array<float, 3>
{
    const uint32_t cell = v / VERTICES_PER_CELL;
    const uint32_t corner = v % VERTICES_PER_CELL;
    const uint32_t row = cell / COLUMNS;
    const uint32_t column = cell % COLUMNS;
    const float x = float(column + (corner == 1 ? 1u : 0u)) * (2.0f / float(COLUMNS)) - 1.0f;
    const float y = float(row + (corner == 2 ? 1u : 0u)) * (2.0f / float(ROWS)) - 1.0f;
    return {x, y, 0.5f};
}

/// Byte @p component of basis vector @p basis of vertex @p v in the check's mesh: component 0 in 20..79, 1 in
/// 100..159, 2 in 180..239, so turning the components moves each by tens of 8-bit steps
/// (vs_vertex_stream_check.eshader get_mesh_basis_byte).
auto get_mesh_basis_byte(uint32_t v, uint32_t basis, uint32_t component) -> float
{
    const uint32_t value = v * (7 + 4 * basis + 2 * component) + 9 * basis;
    return float(20 + 80 * component + value % 60);
}

/// The check's mesh: every attribute a value the vertex shader can work out from the vertex index.
auto make_mesh_vertices(const bgfx::VertexLayout& layout) -> std::vector<uint8_t>
{
    std::vector<uint8_t> data(layout.getSize(VERTICES));
    for(uint32_t v = 0; v < VERTICES; ++v)
    {
        const auto cell = get_cell_position(v);
        const float fv = float(v);
        const float position[4] = {cell[0] + MESH_POSITION_SHIFT, cell[1] + MESH_POSITION_SHIFT, cell[2], 0.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, layout, data.data(), v);
        for(uint32_t basis = 0; basis < BASIS_ATTRIBUTES.size(); ++basis)
        {
            // Not normalized: the bytes as they are.
            const float bytes[4] = {get_mesh_basis_byte(v, basis, 0),
                                    get_mesh_basis_byte(v, basis, 1),
                                    get_mesh_basis_byte(v, basis, 2),
                                    0.0f};
            bgfx::vertexPack(bytes, false, BASIS_ATTRIBUTES[basis], layout, data.data(), v);
        }
        const float texcoord0[4] = {fv, fv * 0.5f, 0.0f, 0.0f};
        bgfx::vertexPack(texcoord0, false, bgfx::Attrib::TexCoord0, layout, data.data(), v);
        const float color[4] = {float((v * 3) % 256), float((v * 5 + 17) % 256), float((v * 7 + 34) % 256), float((v * 9 + 51) % 256)};
        bgfx::vertexPack(color, false, bgfx::Attrib::Color0, layout, data.data(), v);
        const float texcoord1[4] = {-fv, fv * 0.25f, 0.0f, 0.0f};
        bgfx::vertexPack(texcoord1, false, bgfx::Attrib::TexCoord1, layout, data.data(), v);
        const float weight[4] = {fv * 0.125f, fv + 1.0f, 2.5f, -fv};
        bgfx::vertexPack(weight, false, bgfx::Attrib::Weight, layout, data.data(), v);
        const float indices[4] = {float(v % 128), float((v + 1) % 128), 7.0f, 64.0f};
        bgfx::vertexPack(indices, false, bgfx::Attrib::Indices, layout, data.data(), v);
    }
    return data;
}

/// What the compute half writes, made on the CPU: the mesh's positions plus @p offset, each basis vector decoded as
/// the mesh shaders decode it, its components turned (y, z, x) and packed by bgfx::vertexPack.
auto make_deformed_vertices(const layouts& l, const std::array<float, 3>& offset) -> std::vector<uint8_t>
{
    std::vector<uint8_t> data(l.deform.getSize(VERTICES));
    for(uint32_t v = 0; v < VERTICES; ++v)
    {
        const auto cell = get_cell_position(v);
        const float position[4] = {cell[0] + MESH_POSITION_SHIFT + offset[0],
                                   cell[1] + MESH_POSITION_SHIFT + offset[1],
                                   cell[2] + offset[2],
                                   0.0f};
        bgfx::vertexPack(position, false, bgfx::Attrib::Position, l.deform, data.data(), v);
        for(uint32_t basis = 0; basis < BASIS_ATTRIBUTES.size(); ++basis)
        {
            std::array<float, 3> value{};
            for(uint32_t component = 0; component < 3; ++component)
            {
                value[component] = get_mesh_basis_byte(v, basis, component) / 255.0f * 2.0f - 1.0f;
            }
            const float turned[4] = {value[1], value[2], value[0], 0.0f};
            bgfx::vertexPack(turned, true, BASIS_ATTRIBUTES[basis], l.deform, data.data(), v);
        }
    }
    return data;
}

/// The offsets the written streams add to the mesh's positions: the drawn one (stream 0) and the previous (stream 2).
auto get_current_offset() -> std::array<float, 3>
{
    return {-MESH_POSITION_SHIFT, -MESH_POSITION_SHIFT, 0.0f};
}

auto get_previous_offset() -> std::array<float, 3>
{
    return {PREVIOUS_POSITION_OFFSET[0] - MESH_POSITION_SHIFT,
            PREVIOUS_POSITION_OFFSET[1] - MESH_POSITION_SHIFT,
            PREVIOUS_POSITION_OFFSET[2]};
}

/// The single-stream control's vertices: the mesh's attributes with the written stream's position and basis in place
/// (the same 24 bytes, which both layouts start with) and the previous position as texcoord7.
auto make_single_stream_vertices(const layouts& l) -> std::vector<uint8_t>
{
    auto data = make_mesh_vertices(l.single);
    const auto current = make_deformed_vertices(l, get_current_offset());
    const auto previous = make_deformed_vertices(l, get_previous_offset());
    const uint16_t replaced_bytes = l.deform.getStride();
    for(uint32_t v = 0; v < VERTICES; ++v)
    {
        std::memcpy(data.data() + size_t(v) * l.single.getStride(),
                    current.data() + size_t(v) * replaced_bytes,
                    replaced_bytes);
        float position[4] = {};
        bgfx::vertexUnpack(position, bgfx::Attrib::Position, l.deform, previous.data(), v);
        bgfx::vertexPack(position, false, bgfx::Attrib::TexCoord7, l.single, data.data(), v);
    }
    return data;
}

/// How one path's cells came out.
struct path_result
{
    uint32_t right = 0;
    uint32_t wrong = 0;
    uint32_t not_drawn = 0;
    uint32_t unreadable = 0;
    std::array<uint32_t, ATTRIBUTE_NAMES.size()> wrong_by_attribute{};
};

auto is_clear_pixel(const ReadbackImage& image, size_t offset) -> bool
{
    return std::equal(CLEAR_PIXEL.begin(), CLEAR_PIXEL.end(), image.pixels.begin() + std::ptrdiff_t(offset));
}

/// The cells of @p p read from @p image: blue marks a drawn pixel, red and green hold the failure bits, the clear
/// colour a cell that was not drawn.
auto read_path_result(const ReadbackImage& image, path p) -> path_result
{
    path_result result;
    const uint32_t first_cell = uint32_t(p) * CELLS_PER_PATH;
    for(uint32_t cell = first_cell; cell < first_cell + CELLS_PER_PATH; ++cell)
    {
        const uint32_t x = (cell % COLUMNS) * CELL_PIXELS + READ_PIXEL_IN_CELL;
        // Rows counted up from clip space y = -1; a target's origin is at the top, so that row is its last.
        const uint32_t y_up = (cell / COLUMNS) * CELL_PIXELS + READ_PIXEL_IN_CELL;
        const uint32_t y = image.height - 1 - y_up;
        const size_t offset = (size_t(y) * image.width + x) * 4;
        if(offset + 4 > image.pixels.size())
        {
            ++result.unreadable;
            continue;
        }
        if(image.pixels[offset + 2] < 128)
        {
            ++(is_clear_pixel(image, offset) ? result.not_drawn : result.unreadable);
            continue;
        }
        const uint32_t mask = uint32_t(image.pixels[offset]) | (uint32_t(image.pixels[offset + 1]) << 8);
        if(mask == 0)
        {
            ++result.right;
            continue;
        }
        ++result.wrong;
        for(size_t bit = 0; bit < ATTRIBUTE_NAMES.size(); ++bit)
        {
            if((mask & (1u << bit)) != 0)
            {
                ++result.wrong_by_attribute[bit];
            }
        }
    }
    return result;
}

/// The attributes that came wrong, as "normal in 32 cells, tangent in 32 cells".
auto describe_wrong_attributes(const path_result& result) -> std::string
{
    std::string text;
    for(size_t bit = 0; bit < ATTRIBUTE_NAMES.size(); ++bit)
    {
        if(result.wrong_by_attribute[bit] == 0)
        {
            continue;
        }
        if(!text.empty())
        {
            text += ", ";
        }
        text += fmt::format("{} in {} cells", ATTRIBUTE_NAMES[bit], result.wrong_by_attribute[bit]);
    }
    return text;
}

void log_path_result(const char* renderer_name, path p, const path_result& result)
{
    if(result.right == CELLS_PER_PATH)
    {
        APPLOG_INFO("Vertex stream check on {}: the {} drew all {} cells right.",
                    renderer_name,
                    get_path_name(p),
                    CELLS_PER_PATH);
        return;
    }
    APPLOG_ERROR("Vertex stream check on {}: the {} drew {} of {} cells right, {} wrong and {} not at all (a cell is "
                 "not drawn when its position came from the wrong stream or the draw did not run); {} cells could "
                 "not be read (the readback did not hold the target). Wrong attributes: {}.",
                 renderer_name,
                 get_path_name(p),
                 result.right,
                 CELLS_PER_PATH,
                 result.wrong,
                 result.not_drawn,
                 result.unreadable,
                 result.wrong == 0 ? std::string("none") : describe_wrong_attributes(result));
}

/// The pixels the readback delivered, shared with its callback, which may run after the check is gone.
struct readback_result
{
    bool has_landed = false;
    ReadbackImage image;
};

} // namespace stream_check
} // namespace

struct vertex_stream_check::impl
{
    enum class phase
    {
        waiting_for_programs,
        waiting_for_readback,
        done,
    };

    struct programs : uniforms_cache
    {
        void cache_uniforms()
        {
            cache_uniform(compute.get(), u_stream_check_compute, "u_stream_check_compute", bgfx::UniformType::Vec4);
            cache_uniform(compute.get(), u_stream_check_offset, "u_stream_check_offset", bgfx::UniformType::Vec4);
            cache_uniform(draw.get(), u_stream_check_grid, "u_stream_check_grid", bgfx::UniformType::Vec4);
        }

        gfx::program::uniform_ptr u_stream_check_compute;
        gfx::program::uniform_ptr u_stream_check_offset;
        gfx::program::uniform_ptr u_stream_check_grid;
        std::unique_ptr<gpu_program> compute;
        std::unique_ptr<gpu_program> draw;
    };

    ~impl()
    {
        release_buffers();
    }

    void load_programs(asset_manager& am)
    {
        // Uniforms before programs: the OpenGL renderer wires a program's uniforms at link time.
        progs.cache_uniforms();
        progs.draw = std::make_unique<gpu_program>(
            am.get_asset<gfx::shader>("engine:/data/shaders/vertex_stream_check/vs_vertex_stream_check.eshader"),
            am.get_asset<gfx::shader>("engine:/data/shaders/vertex_stream_check/fs_vertex_stream_check.eshader"));
        if(wants_compute)
        {
            progs.compute = std::make_unique<gpu_program>(
                am.get_asset<gfx::shader>("engine:/data/shaders/vertex_stream_check/cs_vertex_stream_check.eshader"));
        }
    }

    void create_buffers()
    {
        layout_handles.rest = bgfx::createVertexLayout(vertex_layouts.rest);
        layout_handles.previous = bgfx::createVertexLayout(vertex_layouts.previous);
        const auto mesh_data = stream_check::make_mesh_vertices(vertex_layouts.mesh);
        // A compute shader reads the mesh's own buffer, as the deform pass will.
        mesh = bgfx::createVertexBuffer(bgfx::copy(mesh_data.data(), uint32_t(mesh_data.size())),
                                        vertex_layouts.mesh,
                                        runs_compute ? BGFX_BUFFER_COMPUTE_READ : BGFX_BUFFER_NONE);
        const auto single_data = stream_check::make_single_stream_vertices(vertex_layouts);
        single_stream = bgfx::createVertexBuffer(bgfx::copy(single_data.data(), uint32_t(single_data.size())),
                                                 vertex_layouts.single);
        single_updated_stream = bgfx::createDynamicVertexBuffer(stream_check::VERTICES, vertex_layouts.single);
        const std::array<std::vector<uint8_t>, 2> static_data{
            stream_check::make_deformed_vertices(vertex_layouts, stream_check::get_current_offset()),
            stream_check::make_deformed_vertices(vertex_layouts, stream_check::get_previous_offset())};
        for(size_t i = 0; i < static_streams.size(); ++i)
        {
            static_streams[i] = bgfx::createVertexBuffer(bgfx::copy(static_data[i].data(), uint32_t(static_data[i].size())),
                                                         vertex_layouts.deform);
        }
        std::vector<uint16_t> index_data(stream_check::VERTICES);
        for(uint32_t i = 0; i < stream_check::VERTICES; ++i)
        {
            index_data[i] = uint16_t(i);
        }
        indices = bgfx::createIndexBuffer(bgfx::copy(index_data.data(), uint32_t(index_data.size() * sizeof(uint16_t))));
        if(runs_compute)
        {
            for(auto& buffer : compute_streams)
            {
                buffer = bgfx::createDynamicVertexBuffer(stream_check::VERTICES,
                                                         vertex_layouts.deform,
                                                         BGFX_BUFFER_COMPUTE_WRITE);
            }
        }
        // bgfx updates no compute-written buffer from the CPU: the CPU path has buffers of its own, as on WebGL 2.
        for(auto& buffer : cpu_streams)
        {
            buffer = bgfx::createDynamicVertexBuffer(stream_check::VERTICES, vertex_layouts.deform);
        }
        target_texture = std::make_shared<gfx::texture>(uint16_t(stream_check::COLUMNS * stream_check::CELL_PIXELS),
                                                        uint16_t(stream_check::ROWS * stream_check::CELL_PIXELS),
                                                        false,
                                                        uint16_t(1),
                                                        bgfx::TextureFormat::RGBA8,
                                                        BGFX_TEXTURE_RT);
        target = std::make_shared<gfx::frame_buffer>(std::vector<gfx::texture::ptr>{target_texture});
    }

    void release_buffers()
    {
        const auto destroy = [](auto& handle)
        {
            if(bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
                handle = BGFX_INVALID_HANDLE;
            }
        };
        destroy(layout_handles.rest);
        destroy(layout_handles.previous);
        destroy(mesh);
        destroy(single_stream);
        destroy(single_updated_stream);
        for(auto& buffer : static_streams)
        {
            destroy(buffer);
        }
        destroy(indices);
        for(auto& buffer : compute_streams)
        {
            destroy(buffer);
        }
        for(auto& buffer : cpu_streams)
        {
            destroy(buffer);
        }
        target.reset();
        target_texture.reset();
    }

    void dispatch_compute_streams(bgfx::ViewId view)
    {
        const std::array<std::array<float, 3>, 2> offsets{stream_check::get_current_offset(),
                                                           stream_check::get_previous_offset()};
        const float mesh_stride_uints = float(vertex_layouts.mesh.getStride() / sizeof(uint32_t));
        for(size_t i = 0; i < compute_streams.size(); ++i)
        {
            bgfx::setBuffer(stream_check::DEFORM_STAGE, compute_streams[i], bgfx::Access::Write);
            bgfx::setBuffer(stream_check::MESH_STAGE, mesh, bgfx::Access::Read);
            gfx::set_uniform(progs.u_stream_check_compute,
                             math::vec4(float(stream_check::VERTICES), mesh_stride_uints, 0.0f, 0.0f));
            gfx::set_uniform(progs.u_stream_check_offset, math::vec4(offsets[i][0], offsets[i][1], offsets[i][2], 0.0f));
            gfx::dispatch(view,
                          progs.compute->native_handle(),
                          (stream_check::VERTICES + stream_check::GROUP_SIZE - 1) / stream_check::GROUP_SIZE,
                          1,
                          1);
        }
        progs.compute->end();
    }

    void update_cpu_streams()
    {
        const auto current = stream_check::make_deformed_vertices(vertex_layouts, stream_check::get_current_offset());
        const auto previous = stream_check::make_deformed_vertices(vertex_layouts, stream_check::get_previous_offset());
        bgfx::update(cpu_streams[0], 0, bgfx::copy(current.data(), uint32_t(current.size())));
        bgfx::update(cpu_streams[1], 0, bgfx::copy(previous.data(), uint32_t(previous.size())));
        const auto single = stream_check::make_single_stream_vertices(vertex_layouts);
        bgfx::update(single_updated_stream, 0, bgfx::copy(single.data(), uint32_t(single.size())));
    }

    /// The index range, grid uniform and state of draw @p draw of path @p p, then its submit.
    void submit_cells(bgfx::ViewId view, stream_check::path p, uint32_t draw)
    {
        const uint32_t first_cell = uint32_t(p) * stream_check::CELLS_PER_PATH + draw * stream_check::CELLS_PER_DRAW;
        bgfx::setIndexBuffer(indices,
                             first_cell * stream_check::VERTICES_PER_CELL,
                             stream_check::CELLS_PER_DRAW * stream_check::VERTICES_PER_CELL);
        gfx::set_uniform(progs.u_stream_check_grid,
                         math::vec4(float(stream_check::COLUMNS), float(stream_check::ROWS), 0.0f, 0.0f));
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(view, progs.draw->native_handle());
    }

    /// The draws of one path: stream 0 its written positions and basis, stream 1 the mesh under the rest layout,
    /// stream 2 its previous positions. @p streams are dynamic or static vertex buffer handles.
    template<typename handle>
    void draw_path(bgfx::ViewId view, stream_check::path p, const std::array<handle, 2>& streams)
    {
        for(uint32_t draw = 0; draw < stream_check::DRAWS_PER_PATH; ++draw)
        {
            bgfx::setVertexBuffer(0, streams[0], 0, stream_check::VERTICES);
            bgfx::setVertexBuffer(1, mesh, 0, stream_check::VERTICES, layout_handles.rest);
            bgfx::setVertexBuffer(2, streams[1], 0, stream_check::VERTICES, layout_handles.previous);
            submit_cells(view, p, draw);
        }
    }

    /// The draws of a single-stream control: every attribute from @p buffer.
    template<typename handle>
    void draw_single_stream(bgfx::ViewId view, stream_check::path p, handle buffer)
    {
        for(uint32_t draw = 0; draw < stream_check::DRAWS_PER_PATH; ++draw)
        {
            bgfx::setVertexBuffer(0, buffer, 0, stream_check::VERTICES);
            submit_cells(view, p, draw);
        }
    }

    /// Records the compute dispatches, the CPU uploads, the draws and the readback into views of this frame.
    void submit_work()
    {
        create_buffers();
        if(runs_compute)
        {
            gfx::render_pass compute_pass("Vertex Stream Check Compute");
            dispatch_compute_streams(compute_pass.id);
        }
        update_cpu_streams();

        gfx::render_pass draw_pass("Vertex Stream Check");
        draw_pass.bind(target.get());
        draw_pass.clear(BGFX_CLEAR_COLOR, stream_check::CLEAR_COLOR);
        if(runs_compute)
        {
            draw_path(draw_pass.id, stream_check::path::compute, compute_streams);
        }
        draw_path(draw_pass.id, stream_check::path::cpu, cpu_streams);
        draw_path(draw_pass.id, stream_check::path::static_streams, static_streams);
        draw_single_stream(draw_pass.id, stream_check::path::single_stream, single_stream);
        draw_single_stream(draw_pass.id, stream_check::path::single_updated_stream, single_updated_stream);
        progs.draw->end();

        gfx::render_pass read_pass("Vertex Stream Check Read");
        const std::weak_ptr<stream_check::readback_result> weak_result = result;
        const bool is_reading = readTexture(read_pass.id,
                                            target_texture->native_handle(),
                                            stream_check::COLUMNS * stream_check::CELL_PIXELS,
                                            stream_check::ROWS * stream_check::CELL_PIXELS,
                                            [weak_result](ReadbackImage&& image)
                                            {
                                                if(auto landed = weak_result.lock())
                                                {
                                                    landed->image = std::move(image);
                                                    landed->has_landed = true;
                                                }
                                            });
        if(!is_reading)
        {
            APPLOG_ERROR("Vertex stream check on {}: the result could not be read back.", renderer_name);
            state = phase::done;
            return;
        }
        state = phase::waiting_for_readback;
        waited_frames = 0;
    }

    void report()
    {
        if(runs_compute)
        {
            stream_check::log_path_result(renderer_name,
                                          stream_check::path::compute,
                                          stream_check::read_path_result(result->image, stream_check::path::compute));
        }
        else
        {
            APPLOG_INFO("Vertex stream check on {}: the compute-written streams were not checked because {}.",
                        renderer_name,
                        compute_skip_reason);
        }
        for(const auto p : stream_check::PATHS_WITHOUT_COMPUTE)
        {
            stream_check::log_path_result(renderer_name, p, stream_check::read_path_result(result->image, p));
        }
    }

    void wait_for_programs(rtti::context& ctx)
    {
        if(!progs.draw)
        {
            if(!ctx.has<asset_manager>())
            {
                return;
            }
            load_programs(ctx.get_cached<asset_manager>());
        }
        ++waited_frames;
        const bool is_draw_ready = progs.draw->begin();
        const bool is_compute_ready = !wants_compute || (progs.compute && progs.compute->begin());
        const bool has_waited_enough = waited_frames > stream_check::MAX_WAIT_FRAMES;
        if(!is_draw_ready)
        {
            if(has_waited_enough)
            {
                APPLOG_ERROR("Vertex stream check on {}: its draw program did not load, so nothing was checked.",
                             renderer_name);
                state = phase::done;
            }
            return;
        }
        if(!is_compute_ready && !has_waited_enough)
        {
            return;
        }
        runs_compute = wants_compute && is_compute_ready;
        if(wants_compute && !is_compute_ready)
        {
            compute_skip_reason = "its compute program did not load";
        }
        submit_work();
    }

    void wait_for_readback()
    {
        if(result->has_landed)
        {
            report();
            state = phase::done;
            return;
        }
        if(++waited_frames > stream_check::MAX_WAIT_FRAMES)
        {
            APPLOG_ERROR("Vertex stream check on {}: its readback never landed.", renderer_name);
            state = phase::done;
        }
    }

    phase state = phase::waiting_for_programs;
    uint32_t waited_frames = 0;
    const char* renderer_name = bgfx::getRendererName(bgfx::getRendererType());
    bool wants_compute = gfx::get_device_caps().has_compute;
    bool runs_compute = false;
    std::string compute_skip_reason = "the device has no compute shaders";

    programs progs;
    stream_check::layouts vertex_layouts = stream_check::make_layouts();
    struct
    {
        bgfx::VertexLayoutHandle rest = BGFX_INVALID_HANDLE;
        bgfx::VertexLayoutHandle previous = BGFX_INVALID_HANDLE;
    } layout_handles;
    bgfx::VertexBufferHandle mesh = BGFX_INVALID_HANDLE;
    /// The single-stream control's vertices.
    bgfx::VertexBufferHandle single_stream = BGFX_INVALID_HANDLE;
    /// The same vertices in a buffer the CPU updates.
    bgfx::DynamicVertexBufferHandle single_updated_stream = BGFX_INVALID_HANDLE;
    /// The CPU's current and previous streams in buffers created with their data.
    std::array<bgfx::VertexBufferHandle, 2> static_streams{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    /// The current (stream 0) and previous (stream 2) written streams of each path.
    std::array<bgfx::DynamicVertexBufferHandle, 2> compute_streams{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<bgfx::DynamicVertexBufferHandle, 2> cpu_streams{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    gfx::texture::ptr target_texture;
    std::shared_ptr<gfx::frame_buffer> target;
    std::shared_ptr<stream_check::readback_result> result = std::make_shared<stream_check::readback_result>();
};

vertex_stream_check::vertex_stream_check() : impl_(std::make_unique<impl>())
{
    if(!stream_check::has_matching_strides(impl_->vertex_layouts))
    {
        APPLOG_ERROR("Vertex stream check on {}: its vertex layouts do not have the strides it needs, so nothing was "
                     "checked.",
                     impl_->renderer_name);
        impl_->state = impl::phase::done;
    }
}

vertex_stream_check::~vertex_stream_check() = default;

void vertex_stream_check::submit(rtti::context& ctx)
{
    switch(impl_->state)
    {
        case impl::phase::waiting_for_programs:
            impl_->wait_for_programs(ctx);
            break;
        case impl::phase::waiting_for_readback:
            impl_->wait_for_readback();
            break;
        case impl::phase::done:
            break;
    }
}

auto vertex_stream_check::is_done() const -> bool
{
    return impl_->state == impl::phase::done;
}

} // namespace unravel
