#include "material_graph_previews.h"

#include <asset_compiler/asset_compiler.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/threading/threader.h>

#include <filesystem/filesystem.h>
#include <graphics/graphics.h>
#include <graphics/render_pass.h>
#include <graphics/uniform.h>

namespace unravel
{

namespace
{
/// The swatch atlas: square tiles, one per node, in the order the preview code numbers them.
constexpr uint16_t SWATCH_ATLAS_SIZE = 1536;
constexpr uint16_t SWATCH_TILE_SIZE = 96;
constexpr size_t SWATCH_TILES_PER_ROW = SWATCH_ATLAS_SIZE / SWATCH_TILE_SIZE;
constexpr size_t SWATCH_TILE_COUNT = SWATCH_TILES_PER_ROW * SWATCH_TILES_PER_ROW;
/// The code rests this long after a change before it compiles: a dragged constant compiles once, when it stops.
constexpr std::chrono::milliseconds PREVIEW_COMPILE_DELAY{300};
/// Swatches of a graph that animates are drawn again this often.
constexpr std::chrono::milliseconds ANIMATED_SWATCH_INTERVAL{33};
/// What generated code reads the time through.
constexpr const char* PREVIEW_TIME_CODE = "i.time";
constexpr const char* PREVIEW_TEMP_FOLDER = "unravel-material-graph-previews";
/// The shaders compile under these names: their fs_ prefix gives shaderc the stage.
constexpr const char* SWATCH_SHADER_NAME = "fs_material_graph_swatches.eshader";
constexpr const char* SURFACE_SHADER_NAME = "fs_material_graph_surface.eshader";
constexpr const char* VERTEX_SHADER_NAME = "vs_material_graph_surface.eshader";
constexpr const char* SWATCH_SHADER_KEY = "Material Graph Swatches";
constexpr const char* SURFACE_SHADER_KEY = "Material Graph Preview";
constexpr const char* VERTEX_SHADER_KEY = "Material Graph Preview Vertices";
constexpr const char* PREVIEW_MATERIAL_KEY = "Material Graph Preview";

/// A shader of no asset of its own, from compiled variants; empty when they hold no default variant.
auto make_detached_shader(tpp::thread_pool& pool, const std::vector<compiled_shader_variant>& variants, const std::string& key)
    -> asset_handle<gfx::shader>
{
    asset_handle<gfx::shader> handle;
    auto shader = asset_reader::create_shader_from_variants(variants, key, key);
    if(!shader)
    {
        return handle;
    }
    handle.set_internal_ids({}, key);
    asset_reader::load_from_instance(pool, handle, std::move(shader));
    return handle;
}

/// The nodes of a compiled preview with a swatch, by id, each with its index in the preview code.
auto make_swatch_tiles(const material_graph& graph, const material_graph_compile_result& preview) -> std::map<std::string, size_t>
{
    std::map<std::string, size_t> tiles;
    for(size_t i = 0; i < graph.nodes.size() && i < SWATCH_TILE_COUNT; ++i)
    {
        if(has_material_graph_preview(graph, preview, i))
        {
            tiles.emplace(graph.nodes[i].id, i);
        }
    }
    return tiles;
}

/// The tile @p index in the atlas, in pixels.
auto get_swatch_tile(size_t index) -> irect32_t
{
    const auto x = int32_t(index % SWATCH_TILES_PER_ROW) * SWATCH_TILE_SIZE;
    const auto y = int32_t(index / SWATCH_TILES_PER_ROW) * SWATCH_TILE_SIZE;
    return irect32_t(x, y, x + SWATCH_TILE_SIZE, y + SWATCH_TILE_SIZE);
}

/// The quad of a tile: its texture coordinates run from the top left, as the preview shader expects, on any renderer.
auto make_swatch_quad(const irect32_t& tile) -> gfx::clip_quad_def
{
    const float atlas_size = float(SWATCH_ATLAS_SIZE);
    gfx::clip_quad_def quad;
    quad.width = float(SWATCH_TILE_SIZE) / atlas_size;
    quad.height = quad.width;
    quad.offset_x = -1.0f + (2.0f * float(tile.left) + float(SWATCH_TILE_SIZE)) / atlas_size;
    quad.offset_y = 1.0f - (2.0f * float(tile.top) + float(SWATCH_TILE_SIZE)) / atlas_size;
    return quad;
}
} // namespace

void material_graph_previews::init(rtti::context& ctx)
{
    auto& am = ctx.get_cached<asset_manager>();
    // Every uniform the previews' programs read exists before the first of them is made (uniforms_cache).
    programs_.init(am.get_asset<gfx::shader>("engine:/data/shaders/deferred_geom/vs_deferred_geom.eshader"));
    preview_uniform_ = std::make_shared<gfx::uniform>(MATERIAL_GRAPH_PREVIEW_UNIFORM, bgfx::UniformType::Vec4);
    quad_shader_ = am.get_asset<gfx::shader>("engine:/data/shaders/vs_clip_quad.eshader");
    atlas_ = std::make_shared<gfx::texture>(SWATCH_ATLAS_SIZE,
                                            SWATCH_ATLAS_SIZE,
                                            false,
                                            1,
                                            bgfx::TextureFormat::RGBA8,
                                            BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    atlas_target_ = std::make_shared<gfx::frame_buffer>();
    atlas_target_->populate({atlas_});
    material_ = std::make_shared<graph_material>();
    material_handle_.set_internal_ids({}, PREVIEW_MATERIAL_KEY);
    asset_reader::load_from_instance(*ctx.get_cached<threader>().pool, material_handle_, std::shared_ptr<material>(material_));
}

void material_graph_previews::deinit()
{
    if(job_.valid())
    {
        job_.wait();
    }
    job_ = {};
    swatch_program_.reset();
    atlas_target_.reset();
    atlas_.reset();
    material_handle_ = {};
    material_.reset();
    shown_code_ = {};
    shown_values_ = {};
}

void material_graph_previews::reset()
{
    wanted_ = {};
    running_ = {};
    shown_code_ = {};
    shown_values_ = {};
    swatch_program_.reset();
    has_failed_ = false;
    has_graph_errors_ = false;
    revision_ = ~uint64_t(0);
    is_dirty_ = true;
    if(material_)
    {
        material_->set_graph({});
        material_->set_shader({});
        material_->set_vertex_shader({});
    }
}

void material_graph_previews::update(rtti::context& ctx,
                                     const material_graph& graph,
                                     const material_graph_compile_result& surface,
                                     uint64_t revision,
                                     const std::string& name)
{
    take_compile(ctx);
    if(revision != revision_)
    {
        revision_ = revision;
        material_graph_compile_options options;
        options.source_name = name;
        options.is_preview = true;
        const auto preview = compile_material_graph(graph, options);
        has_graph_errors_ = surface.has_errors() || preview.has_errors();
        // The texts are the code's identity: a change of values alone leaves them as they are.
        std::string code_hash = preview.shader + surface.shader + surface.vertex_shader;
        swatch_values values{make_material_graph_parameter_registers(preview), preview.textures};
        if(has_graph_errors_)
        {
            wanted_ = {};
        }
        else if(code_hash == shown_code_.code_hash)
        {
            shown_values_ = std::move(values);
            material_->set_graph(graph);
            is_dirty_ = true;
            wanted_ = {};
            // A compile of other code still running is dropped when it ends (take_compile).
            running_ = {};
        }
        else if(code_hash == running_.code.code_hash)
        {
            // The compile running is of this code: it shows these values and this graph when it ends.
            running_.values = std::move(values);
            running_.graph = graph;
            wanted_ = {};
        }
        else
        {
            const bool is_animated = preview.shader.find(PREVIEW_TIME_CODE) != std::string::npos;
            swatch_code code{std::move(code_hash), make_swatch_tiles(graph, preview), is_animated};
            wanted_ = {preview.shader, surface.shader, surface.vertex_shader, std::move(code), std::move(values), graph};
            changed_at_ = std::chrono::steady_clock::now();
        }
    }
    const bool is_resting = std::chrono::steady_clock::now() - changed_at_ >= PREVIEW_COMPILE_DELAY;
    if(!wanted_.code.code_hash.empty() && !job_.valid() && is_resting)
    {
        start_compile(ctx);
    }
}

void material_graph_previews::start_compile(rtti::context& ctx)
{
    running_ = std::move(wanted_);
    wanted_ = {};
    const auto renderer = gfx::get_renderer_based_on_filename_extension(gfx::get_current_renderer_filename_extension());
    auto job = [swatch_code = running_.preview_code, surface_code = running_.surface_code, vertex_code = running_.vertex_code, renderer]()
    {
        compile_output output;
        const auto folder = fs::temp_directory_path() / PREVIEW_TEMP_FOLDER;
        fs::error_code ec;
        fs::create_directories(folder, ec);
        output.is_compiled = asset_compiler::compile_shader_text(folder / SWATCH_SHADER_NAME, swatch_code, renderer, folder, output.preview) &&
                             asset_compiler::compile_shader_text(folder / SURFACE_SHADER_NAME, surface_code, renderer, folder, output.surface) &&
                             (vertex_code.empty() ||
                              asset_compiler::compile_shader_text(folder / VERTEX_SHADER_NAME, vertex_code, renderer, folder, output.vertex));
        return output;
    };
    job_ = ctx.get_cached<threader>().pool->schedule("Compiling Material Graph Previews", std::move(job)).share();
}

void material_graph_previews::take_compile(rtti::context& ctx)
{
    if(!job_.valid() || !job_.is_ready())
    {
        return;
    }
    const auto output = job_.get();
    job_ = {};
    // Started for a graph reset() forgot.
    if(running_.code.code_hash.empty())
    {
        return;
    }
    has_failed_ = !output.is_compiled;
    if(has_failed_)
    {
        running_ = {};
        return;
    }
    auto& pool = *ctx.get_cached<threader>().pool;
    const auto swatch_shader = make_detached_shader(pool, output.preview, SWATCH_SHADER_KEY);
    const auto surface_shader = make_detached_shader(pool, output.surface, SURFACE_SHADER_KEY);
    swatch_program_ = std::make_unique<gpu_program>(quad_shader_, gfx::shader::DEFAULT_VARIANT, swatch_shader, gfx::shader::DEFAULT_VARIANT);
    material_->set_graph(running_.graph);
    material_->set_shader(surface_shader);
    material_->set_vertex_shader(output.vertex.empty() ? asset_handle<gfx::shader>{}
                                                       : make_detached_shader(pool, output.vertex, VERTEX_SHADER_KEY));
    shown_code_ = std::move(running_.code);
    shown_values_ = std::move(running_.values);
    is_dirty_ = true;
    running_ = {};
}

void material_graph_previews::render_swatches(const material_graph& graph)
{
    const auto now = std::chrono::steady_clock::now();
    const bool is_due = is_dirty_ || (shown_code_.is_animated && now - rendered_at_ >= ANIMATED_SWATCH_INTERVAL);
    if(!is_due || !swatch_program_ || !swatch_program_->begin())
    {
        return;
    }
    is_dirty_ = false;
    rendered_at_ = now;
    gfx::render_pass pass("Material Graph Swatches");
    pass.bind(atlas_target_.get());
    for(const auto& [node_id, index] : shown_code_.tiles)
    {
        const int node = find_material_graph_node(graph, node_id);
        if(node >= 0 && graph.nodes[size_t(node)].is_preview_hidden)
        {
            continue;
        }
        const auto tile = get_swatch_tile(index);
        bgfx::setScissor(uint16_t(tile.left), uint16_t(tile.top), SWATCH_TILE_SIZE, SWATCH_TILE_SIZE);
        const uint64_t topology = gfx::clip_quad_ex(make_swatch_quad(tile));
        programs_.submit_values(shown_values_.registers, shown_values_.textures);
        gfx::set_uniform(preview_uniform_, math::vec4(float(index), 0.0f, 0.0f, 0.0f));
        bgfx::setState(topology | BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(pass.id, swatch_program_->native_handle());
    }
    swatch_program_->end();
}

auto material_graph_previews::get_swatch_texture() const -> const gfx::texture::ptr&
{
    return atlas_;
}

auto material_graph_previews::get_swatch_uv(const std::string& node_id, ImVec2& uv0, ImVec2& uv1) const -> bool
{
    const auto found = shown_code_.tiles.find(node_id);
    // Drawn once a render followed the compile: the tile is empty until then.
    if(!swatch_program_ || is_dirty_ || found == shown_code_.tiles.end())
    {
        return false;
    }
    const auto tile = get_swatch_tile(found->second);
    const float atlas_size = float(SWATCH_ATLAS_SIZE);
    uv0 = ImVec2(float(tile.left) / atlas_size, float(tile.top) / atlas_size);
    uv1 = ImVec2(float(tile.right) / atlas_size, float(tile.bottom) / atlas_size);
    return true;
}

auto material_graph_previews::get_material() const -> const asset_handle<material>&
{
    return material_handle_;
}

auto material_graph_previews::is_compiling() const -> bool
{
    return job_.valid() || !wanted_.code.code_hash.empty();
}

auto material_graph_previews::has_failed() const -> bool
{
    return has_failed_;
}

auto material_graph_previews::has_graph_errors() const -> bool
{
    return has_graph_errors_;
}

} // namespace unravel
