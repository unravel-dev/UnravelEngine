#include "mesh_import_materials.h"

#include "mesh_import_image.h"
#include "mesh_import_material_workflow.h"
#include "mesh_import_paths.h"
#include "mesh_import_texture_jobs.h"

#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_writer.h>

#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/rendering/material_graph/graph_material.h>
#include <engine/rendering/material_graph/standard_material.h>

#include <graphics/utils/bgfx_utils.h>
#include <logging/logging.h>
#include <math/math.h>
#include <string_utils/utils.h>

#include <assimp/GltfMaterial.h>
#include <assimp/scene.h>

#include <array>
#include <hpp/optional.hpp>
#include <unordered_set>

namespace unravel
{
namespace importer
{
namespace mesh_import
{
namespace
{

enum class material_import_phase
{
    /// Discover every texture the materials need and queue the jobs that produce them.
    collect,
    /// Bind the produced textures and the scalar properties to the engine materials.
    bind
};

struct material_import_context
{
    material_import_phase phase;
    asset_manager& am;
    const fs::path& filename;
    const fs::path& output_dir;
    const aiScene* scene;
    texture_catalog& catalog;
    /// Collect phase only.
    texture_job_store* job_store;
};

// Writes/updates the texture's .meta so the asset compiler knows the authored
// color space of the texels bound to this material slot (base color/emissive are
// sRGB-encoded; normal/metal/rough/AO are linear data), and the type of a normal
// map, which compiles to two channels. An explicit user setting wins: only a
// meta still at `automatic` is tagged, and the file is rewritten only when
// something actually changed.
void tag_texture_colorspace(asset_manager& am,
                            const fs::path& output_dir,
                            const std::string& relative,
                            texture_importer_meta::color_space colorspace,
                            texture_importer_meta::texture_type type)
{
    const auto absolute = resolve_texture_on_disk(output_dir, normalize_assimp_path(relative));
    if(!absolute)
    {
        return;
    }
    fs::path meta_path = fs::convert_to_protocol(*absolute);
    meta_path = fs::resolve_protocol(fs::replace(meta_path, ex::get_data_directory(), ex::get_meta_directory()));
    meta_path += ".meta";

    asset_meta meta;
    load_from_file(meta_path.string(), meta);

    const bool had_importer = meta.importer != nullptr;
    auto importer = safe_pointer_cast<texture_importer_meta>(meta.importer);
    if(!had_importer)
    {
        importer = std::make_shared<texture_importer_meta>();
        meta.importer = importer;
    }
    if(!importer)
    {
        // Meta exists but with a non-texture importer; nothing sane to tag.
        return;
    }
    bool changed = !had_importer;
    if(importer->colorspace == texture_importer_meta::color_space::automatic &&
       importer->colorspace != colorspace)
    {
        importer->colorspace = colorspace;
        changed = true;
    }
    if(importer->type == texture_importer_meta::texture_type::automatic && importer->type != type)
    {
        importer->type = type;
        changed = true;
    }
    if(meta.uid.is_nil())
    {
        // A texture this import just wrote has no .meta yet. It takes the id the database holds for its path, else a
        // new one: registered under the nil id, every material bound to it would save a nil map, and the watcher
        // would then give the file another id and the default import settings.
        const asset_meta generated = am.generate_metadata(fs::convert_to_protocol(*absolute));
        meta.uid = generated.uid;
        meta.type = generated.type;
        meta.uid = am.add_asset_info_for_path(*absolute, meta, true);
        changed = true;
    }
    if(!changed)
    {
        return;
    }
    fs::error_code err;
    asset_writer::atomic_write_file(meta_path,
                                    [&](const fs::path& temp)
                                    {
                                        save_to_file(temp.string(), meta);
                                    },
                                    err);
    if(err)
    {
        APPLOG_WARNING("Mesh Importer: Failed to write color-space meta for '{}': {}", relative, err.message());
    }
}

/**
 * @brief Whether the base color map's border has transparent texels: foliage and fence cards exported
 * without an alpha mode.
 *
 * Formats without alpha are skipped. BC1 counts as alpha-capable only when its alpha is declared - by
 * the file (DDS alpha flag or DX10 alpha mode) or by @p is_alpha_declared - because its punch-through
 * texel decodes exactly like the transparent black opaque BC1 encoders emit for dark texels (NVTT's
 * three-color black); probing those would cut holes into the dark areas of opaque textures.
 */
auto color_map_border_suggests_alpha_cutout(const fs::path& output_dir, const std::string& relative, bool is_alpha_declared)
    -> bool
{
    if(relative.empty() || !texture_file_exists(output_dir, relative))
    {
        return false;
    }

    const fs::path filepath =
        output_dir / resolve_external_texture_path(output_dir, normalize_assimp_path(relative));
    const bx::FilePath bimg_path(filepath.string().c_str());
    const bool has_declared_alpha = is_alpha_declared || dds_header_declares_alpha(filepath);

    bimg::ImageContainer header{};
    if(imageParseInfo(bimg_path, header)
       && !texture_format_has_alpha(header.m_format, header.m_hasAlpha || has_declared_alpha))
    {
        APPLOG_TRACE("Mesh Importer: Texture format does not have alpha: {}", relative);
        return false;
    }

    bimg::ImageContainer* loaded = imageLoad(bimg_path);
    if(!loaded)
    {
        APPLOG_TRACE("Mesh Importer: Failed to load image: {}", relative);
        return false;
    }
    loaded->m_hasAlpha = loaded->m_hasAlpha || has_declared_alpha;

    const bool suggests_cutout = image_border_has_transparency(*loaded, k_import_border_alpha_opaque_threshold);
    APPLOG_TRACE("Mesh Importer: Probing base color map border for transparency: {}", suggests_cutout);
    bimg::imageFree(loaded);
    return suggests_cutout;
}

/**
 * @brief Path a material references for an authored texture path: the file itself (or the same stem
 * with another extension); failing that, the same name with '.' replaced by '_' in its stem, which is
 * what earlier imports renamed dotted textures to before the asset pipeline handled dotted names.
 */
auto resolve_referenced_texture(const fs::path& output_dir, const fs::path& assimp_path) -> fs::path
{
    const fs::path resolved = resolve_external_texture_path(output_dir, assimp_path);
    fs::error_code ec;
    if(fs::exists(output_dir / resolved, ec))
    {
        return resolved;
    }
    const std::string stem = assimp_path.stem().string();
    const std::string renamed_stem = string_utils::replace(stem, ".", "_");
    if(renamed_stem == stem)
    {
        return resolved;
    }
    const fs::path renamed =
        resolve_external_texture_path(output_dir, assimp_path.parent_path() / (renamed_stem + assimp_path.extension().string()));
    return fs::exists(output_dir / renamed, ec) ? renamed : resolved;
}

/// Sampler flags of an Assimp wrap mode; the "unset" default for plain wrapping.
auto get_sampler_flags(aiTextureMapMode mode, uint32_t unset_flags) -> uint32_t
{
    switch(mode)
    {
        case aiTextureMapMode_Mirror:
            return BGFX_SAMPLER_UVW_MIRROR;
        case aiTextureMapMode_Clamp:
            return BGFX_SAMPLER_UVW_CLAMP;
        case aiTextureMapMode_Decal:
            return BGFX_SAMPLER_UVW_BORDER;
        default:
            return unset_flags;
    }
}

/// Trace every texture slot Assimp populated: exporters park PBR maps under very different slots.
void log_material_texture_slots(const aiMaterial* material)
{
    struct slot_info
    {
        aiTextureType type;
        const char* name;
    };
    static constexpr std::array<slot_info, 21> slot_table = {{
        {aiTextureType_DIFFUSE,           "DIFFUSE"},
        {aiTextureType_SPECULAR,          "SPECULAR"},
        {aiTextureType_AMBIENT,           "AMBIENT"},
        {aiTextureType_EMISSIVE,          "EMISSIVE"},
        {aiTextureType_HEIGHT,            "HEIGHT"},
        {aiTextureType_NORMALS,           "NORMALS"},
        {aiTextureType_SHININESS,         "SHININESS"},
        {aiTextureType_OPACITY,           "OPACITY"},
        {aiTextureType_DISPLACEMENT,      "DISPLACEMENT"},
        {aiTextureType_LIGHTMAP,          "LIGHTMAP"},
        {aiTextureType_REFLECTION,        "REFLECTION"},
        {aiTextureType_BASE_COLOR,        "BASE_COLOR"},
        {aiTextureType_NORMAL_CAMERA,     "NORMAL_CAMERA"},
        {aiTextureType_EMISSION_COLOR,    "EMISSION_COLOR"},
        {aiTextureType_METALNESS,         "METALNESS"},
        {aiTextureType_DIFFUSE_ROUGHNESS, "DIFFUSE_ROUGHNESS"},
        {aiTextureType_AMBIENT_OCCLUSION, "AMBIENT_OCCLUSION"},
        {aiTextureType_SHEEN,             "SHEEN"},
        {aiTextureType_CLEARCOAT,         "CLEARCOAT"},
        {aiTextureType_TRANSMISSION,      "TRANSMISSION"},
        {aiTextureType_UNKNOWN,           "UNKNOWN"},
    }};

    std::string slot_log;
    for(const auto& slot : slot_table)
    {
        const auto count = material->GetTextureCount(slot.type);
        for(unsigned int i = 0; i < count; ++i)
        {
            aiString path{};
            if(material->GetTexture(slot.type, i, &path) == AI_SUCCESS && path.length > 0)
            {
                if(!slot_log.empty())
                {
                    slot_log += ", ";
                }
                slot_log += fmt::format("{}[{}]={}", slot.name, i, normalize_assimp_path(path.C_Str()).generic_string());
            }
        }
    }
    aiString mat_name{};
    material->Get(AI_MATKEY_NAME, mat_name);
    APPLOG_TRACE("Mesh Importer: Material '{}' texture slots: {}",
                 mat_name.length > 0 ? mat_name.C_Str() : "<unnamed>",
                 slot_log.empty() ? "<none>" : slot_log);
}

/**
 * @brief Imports one Assimp material in one phase.
 *
 * The collect phase only discovers textures and queues their jobs; the bind phase resolves the
 * produced files through the catalog and fills the Standard material's description. Both walk the slots in the
 * same order, so a texture is requested identically in each.
 *
 * KHR_materials_pbrSpecularGlossiness keeps its own maps and factors in the specular workflow, which the shader
 * converts per pixel; every other workflow is metal / roughness: legacy Phong as a dielectric whose roughness comes
 * from its shininess, as its specular color is a highlight tint, not a reflectance.
 */
class material_importer
{
public:
    material_importer(const material_import_context& ctx,
                      const aiMaterial* material,
                      standard_surface& surface,
                      std::vector<imported_texture>& textures)
        : ctx_(ctx)
        , material_(material)
        , surface_(surface)
        , textures_(textures)
        , workflow_(detect_material_workflow(material))
    {
        APPLOG_TRACE("Mesh Importer: Material workflow detected: {}", material_workflow_label(workflow_));
    }

    void import()
    {
        if(is_binding())
        {
            surface_.workflow = is_specular_workflow() ? standard_workflow::specular : standard_workflow::metallic;
            surface_.cull = is_material_two_sided(material_) ? cull_type::none : surface_.cull;
        }
        import_base_color();
        if(is_binding())
        {
            import_scalar_factors();
        }
        if(is_specular_workflow())
        {
            import_specular_map();
        }
        else
        {
            import_metallic_roughness();
        }
        import_normal();
        import_occlusion();
        import_emissive_map();
        if(is_collecting())
        {
            return;
        }
        import_emissive_factors();
        import_alpha_mode();
    }

private:
    using texture_handle_t = asset_handle<gfx::texture>;

    auto is_collecting() const -> bool
    {
        return ctx_.phase == material_import_phase::collect;
    }

    auto is_binding() const -> bool
    {
        return ctx_.phase == material_import_phase::bind;
    }

    /// KHR spec-gloss materials keep their diffuse, specular and glossiness (standard_workflow::specular).
    auto is_specular_workflow() const -> bool
    {
        return workflow_ == material_workflow::khr_specular_glossiness;
    }

    /**
     * @brief Resolve the texture in slot @p type / @p index: an embedded texture or an external file
     * that exists next to the source (alternate extensions accepted). False when the slot is empty or
     * the file is missing.
     */
    auto resolve_texture(aiTextureType type, unsigned int index, const std::string& semantic, imported_texture& tex)
        -> bool
    {
        aiString path{};
        // Assimp writes up to three wrap modes (U, V, W).
        std::array<aiTextureMapMode, 3> map_modes{aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        unsigned int texture_flags = 0;
        aiGetMaterialTexture(material_, type, index, &path, nullptr, nullptr, nullptr, nullptr, map_modes.data(), &texture_flags);
        if(path.length == 0)
        {
            return false;
        }
        const bool is_inverted = (texture_flags & aiTextureFlags_Invert) != 0;
        const auto [embedded_texture, embedded_index] = ctx_.scene->GetEmbeddedTextureAndIndex(path.C_Str());
        if(embedded_texture)
        {
            tex.name = get_embedded_texture_name(embedded_texture,
                                                 embedded_index,
                                                 ctx_.filename,
                                                 semantic,
                                                 is_pixel_conversion(semantic, is_inverted));
            tex.embedded_index = embedded_index;
        }
        else
        {
            const fs::path assimp_path = normalize_assimp_path(path.C_Str());
            tex.name = resolve_referenced_texture(ctx_.output_dir, assimp_path).generic_string();
            if(!texture_file_exists(ctx_.output_dir, tex.name))
            {
                APPLOG_WARNING("Mesh Importer: External texture '{}' not found on disk - skipping '{}'",
                               assimp_path.generic_string(),
                               semantic);
                return false;
            }
        }
        tex.semantic = semantic;
        tex.inverse = is_inverted;
        tex.flags = get_sampler_flags(map_modes[0], tex.flags);
        return true;
    }

    /// First candidate that resolves, or nullptr.
    auto resolve_first(const std::vector<texture_slot_candidate>& candidates, imported_texture& tex)
        -> const texture_slot_candidate*
    {
        for(const auto& candidate : candidates)
        {
            if(resolve_texture(candidate.type, candidate.index, candidate.semantic, tex))
            {
                if(candidate.trace_on_use)
                {
                    APPLOG_TRACE("Mesh Importer: {}", candidate.trace_on_use);
                }
                return &candidate;
            }
        }
        return nullptr;
    }

    auto resolve_target(texture_target target, imported_texture& tex) -> bool
    {
        return resolve_first(get_texture_slot_candidates(material_, workflow_, target), tex) != nullptr;
    }

    void enqueue_simple_texture_job(const imported_texture& texture)
    {
        texture_job job{};
        job.desc = texture;
        if(texture.embedded_index >= 0)
        {
            job.type = texture_job_type::embedded_extract;
        }
        else if(needs_external_texture_conversion(texture))
        {
            job.type = texture_job_type::external_convert;
        }
        else
        {
            return;
        }
        ctx_.job_store->try_add(std::move(job));
    }

    /// Collect: queue the texture's job. Bind: point @p texture at the file its job produced.
    void process_texture(imported_texture& texture)
    {
        if(is_collecting())
        {
            enqueue_simple_texture_job(texture);
            if(texture.embedded_index < 0 && !needs_external_texture_conversion(texture)
               && texture_file_exists(ctx_.output_dir, texture.name))
            {
                ctx_.catalog.register_entry(texture, texture);
            }
            return;
        }
        if(ctx_.catalog.resolve(texture))
        {
            return;
        }
        if(adopt_from_manifest(texture))
        {
            return;
        }
        textures_.emplace_back(texture);
        produce_texture_now(texture);
    }

    /// A texture this import already produced for the same semantic.
    auto adopt_from_manifest(imported_texture& texture) const -> bool
    {
        if(texture.embedded_index >= 0)
        {
            const auto it = std::find_if(std::begin(textures_),
                                         std::end(textures_),
                                         [&](const imported_texture& rhs)
                                         {
                                             return rhs.embedded_index == texture.embedded_index
                                                 && rhs.semantic == texture.semantic;
                                         });
            if(it == std::end(textures_))
            {
                return false;
            }
            texture.name = it->name;
            texture.flags = it->flags;
            texture.inverse = it->inverse;
            texture.process_count = it->process_count;
            return true;
        }
        const auto it = std::find_if(std::begin(textures_),
                                     std::end(textures_),
                                     [&](const imported_texture& rhs)
                                     {
                                         return rhs.embedded_index < 0 && rhs.name == texture.name
                                             && rhs.semantic == texture.semantic;
                                     });
        if(it == std::end(textures_))
        {
            return false;
        }
        if(needs_external_texture_conversion(texture))
        {
            texture.name = make_converted_texture_name(texture.name, texture.semantic);
        }
        return true;
    }

    /// Bind-time fallback for a texture no collected job produced: extract or convert it now.
    void produce_texture_now(imported_texture& texture)
    {
        if(texture.embedded_index >= 0)
        {
            const auto& embedded_texture = ctx_.scene->mTextures[texture.embedded_index];
            process_embedded_texture(embedded_texture, texture.embedded_index, ctx_.filename, ctx_.output_dir, textures_);
            return;
        }
        if(!needs_external_texture_conversion(texture))
        {
            return;
        }
        fs::path original_file = ctx_.output_dir / texture.name;
        const auto converted_name = make_converted_texture_name(texture.name, texture.semantic);
        fs::path converted_file = ctx_.output_dir / converted_name;
        bimg::ImageContainer* image = imageLoad(bx::FilePath(original_file.string().c_str()));
        if(image)
        {
            apply_texture_conversion(image, texture.semantic, texture.inverse);
            atomic_image_save(converted_file, image);
            bimg::imageFree(image);
            texture.name = converted_name;
            APPLOG_TRACE("Mesh Importer: Applied {} conversion to external texture: {}", texture.semantic, texture.name);
        }
    }

    /// The texture asset for @p relative, its .meta tagged with @p colorspace; nullopt (logged) when unbindable.
    auto load_texture_map(const std::string& relative,
                          texture_importer_meta::color_space colorspace,
                          const char* label,
                          texture_importer_meta::texture_type type = texture_importer_meta::texture_type::automatic)
        -> hpp::optional<texture_handle_t>
    {
        const auto key = try_make_texture_asset_key(ctx_.output_dir, relative);
        if(!key)
        {
            APPLOG_WARNING("Mesh Importer: Could not bind {} texture '{}'", label, relative);
            return std::nullopt;
        }
        tag_texture_colorspace(ctx_.am, ctx_.output_dir, relative, colorspace, type);
        return ctx_.am.get_asset<gfx::texture>(*key, load_flags::standard, load_mode::deferred);
    }

    void import_base_color()
    {
        imported_texture texture;
        if(!resolve_target(texture_target::base_color, texture))
        {
            return;
        }
        process_texture(texture);
        base_color_map_relative_ = texture.name;
        if(is_binding())
        {
            if(const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::srgb, "base color"))
            {
                surface_.color_map = *map;
            }
        }
    }

    void import_scalar_factors()
    {
        if(is_specular_workflow())
        {
            import_specular_glossiness_factors();
            return;
        }
        aiColor3D base_color_property{1.0f, 1.0f, 1.0f};
        float metallic_property = 0.0f;
        float roughness_property = 0.5f;
        process_material_with_workflow_conversion(material_, workflow_, base_color_property, metallic_property, roughness_property);
        const math::vec3 base_color(base_color_property.r, base_color_property.g, base_color_property.b);
        surface_.base_color = math::color(math::vec4(math::clamp(base_color, math::vec3(0.0f), math::vec3(1.0f)), 1.0f));
        surface_.metallic = math::clamp(metallic_property, 0.0f, 1.0f);
        surface_.roughness = math::clamp(roughness_property, 0.0f, 1.0f);
    }

    /// The diffuse, specular and glossiness factors the diffuse map and the specular map are multiplied by.
    void import_specular_glossiness_factors()
    {
        const spec_gloss_factors_t factors = gather_spec_gloss_factors(material_);
        const math::vec3 diffuse(factors.diffuse_r, factors.diffuse_g, factors.diffuse_b);
        const math::vec3 specular(factors.specular_r, factors.specular_g, factors.specular_b);
        surface_.base_color = math::color(math::vec4(math::clamp(diffuse, math::vec3(0.0f), math::vec3(1.0f)), 1.0f));
        surface_.specular_color = math::color(math::vec4(math::clamp(specular, math::vec3(0.0f), math::vec3(1.0f)), 1.0f));
        surface_.glossiness = math::clamp(factors.glossiness, 0.0f, 1.0f);
        APPLOG_TRACE("Mesh Importer: Specular workflow factors: diffuse=({:.3f}, {:.3f}, {:.3f}), specular=({:.3f}, "
                     "{:.3f}, {:.3f}), glossiness={:.3f}",
                     diffuse.x,
                     diffuse.y,
                     diffuse.z,
                     specular.x,
                     specular.y,
                     specular.z,
                     factors.glossiness);
    }

    void import_metallic_roughness()
    {
        const bool has_metallic_tex = import_metallic_map();
        const bool has_roughness_tex = import_roughness_map();
        // A packed map in the specular slot stores per-pixel metal and rough; the scalars are only multipliers.
        if(has_metallic_tex && has_roughness_tex && material_has_packed_mr_in_specular_slot(material_))
        {
            surface_.metallic = 1.0f;
            surface_.roughness = 1.0f;
        }
    }

    /// The specular workflow's map as authored: specular color in rgb (sRGB), glossiness in alpha.
    void import_specular_map()
    {
        imported_texture texture;
        if(!resolve_texture(aiTextureType_SPECULAR, 0, "Specular", texture))
        {
            return;
        }
        process_texture(texture);
        if(is_binding())
        {
            if(const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::srgb, "specular-glossiness"))
            {
                surface_.specular_map = *map;
            }
        }
    }

    auto import_metallic_map() -> bool
    {
        imported_texture texture;
        if(!resolve_target(texture_target::metallic, texture))
        {
            return false;
        }
        process_texture(texture);
        if(!is_binding())
        {
            return false;
        }
        const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::linear, "metallic");
        if(!map)
        {
            return false;
        }
        surface_.metallic_map = *map;
        return true;
    }

    auto import_roughness_map() -> bool
    {
        imported_texture texture;
        if(!resolve_target(texture_target::roughness, texture))
        {
            return false;
        }
        process_texture(texture);
        if(!is_binding())
        {
            return false;
        }
        const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::linear, "roughness");
        if(!map)
        {
            return false;
        }
        surface_.roughness_map = *map;
        if(texture.semantic == "ShininessToRoughness")
        {
            APPLOG_TRACE("Mesh Importer: Converting shininess texture to roughness: {}", texture.name);
        }
        return true;
    }

    void import_normal()
    {
        const std::vector<texture_slot_candidate> candidates = {{aiTextureType_NORMALS, 0, "Normals"},
                                                                {aiTextureType_NORMAL_CAMERA, 0, "Normals"}};
        imported_texture texture;
        const texture_slot_candidate* hit = resolve_first(candidates, texture);
        if(hit)
        {
            process_texture(texture);
            if(is_binding())
            {
                if(const auto map = load_texture_map(texture.name,
                                                     texture_importer_meta::color_space::linear,
                                                     "normal",
                                                     texture_importer_meta::texture_type::normal_map))
                {
                    surface_.normal_map = *map;
                }
            }
        }
        if(!is_binding())
        {
            return;
        }
        const aiTextureType normals_type = hit ? hit->type : aiTextureType_NORMALS;
        ai_real bump_scale{};
        if(material_->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(normals_type, 0), bump_scale) == AI_SUCCESS
           || material_->Get(AI_MATKEY_BUMPSCALING, bump_scale) == AI_SUCCESS)
        {
            surface_.bumpiness = bump_scale;
        }
    }

    void import_occlusion()
    {
        imported_texture texture;
        if(!resolve_first({{aiTextureType_AMBIENT_OCCLUSION, 0, "Occlusion"},
                           {aiTextureType_AMBIENT, 0, "Occlusion"},
                           {aiTextureType_LIGHTMAP, 0, "Occlusion"}},
                          texture))
        {
            return;
        }
        process_texture(texture);
        if(is_binding())
        {
            if(const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::linear, "occlusion"))
            {
                surface_.ao_map = *map;
            }
        }
    }

    void import_emissive_map()
    {
        imported_texture texture;
        if(!resolve_first({{aiTextureType_EMISSION_COLOR, 0, "Emissive"}, {aiTextureType_EMISSIVE, 0, "Emissive"}},
                          texture))
        {
            return;
        }
        process_texture(texture);
        if(is_binding())
        {
            if(const auto map = load_texture_map(texture.name, texture_importer_meta::color_space::srgb, "emissive"))
            {
                surface_.emissive_map = *map;
            }
        }
    }

    void import_emissive_factors()
    {
        aiColor3D emissive_color{};
        if(material_->Get(AI_MATKEY_COLOR_EMISSIVE, emissive_color) == AI_SUCCESS)
        {
            const math::vec3 emissive(emissive_color.r, emissive_color.g, emissive_color.b);
            surface_.emissive_color = math::color(math::vec4(math::clamp(emissive, math::vec3(0.0f), math::vec3(1.0f)), 1.0f));
        }
        // glTF KHR: emissiveIntensity; premultiplied in deferred submit.
        ai_real intensity = 1.0f;
        if(material_->Get(AI_MATKEY_EMISSIVE_INTENSITY, intensity) == AI_SUCCESS)
        {
            surface_.emissive_intensity = math::clamp(intensity, 0.0f, k_max_emissive_intensity);
        }
        ai_real texture_strength = 1.0f;
        if(material_->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_EMISSION_COLOR, 0), texture_strength) == AI_SUCCESS
           || material_->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_EMISSIVE, 0), texture_strength) == AI_SUCCESS)
        {
            surface_.emissive_intensity = math::clamp(surface_.emissive_intensity * texture_strength, 0.0f, k_max_emissive_intensity);
        }
    }

    /// Whether the material's OPACITY slot names its own albedo map: alpha cut-out by declaration.
    auto is_albedo_alpha_declared() const -> bool
    {
        aiString opacity_path{};
        if(material_->GetTexture(aiTextureType_OPACITY, 0, &opacity_path) != AI_SUCCESS || opacity_path.length == 0)
        {
            return false;
        }
        for(const aiTextureType albedo_slot : {aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR})
        {
            aiString albedo_path{};
            if(material_->GetTexture(albedo_slot, 0, &albedo_path) == AI_SUCCESS
               && material_texture_paths_equal(normalize_assimp_path(opacity_path.C_Str()),
                                               normalize_assimp_path(albedo_path.C_Str())))
            {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief glTF alphaMode + alphaCutoff when declared (the spec: OPAQUE ignores alpha); otherwise a
     * legacy opacity factor, else a probe of the base color map border.
     */
    void import_alpha_mode()
    {
        alpha_mode resolved = alpha_mode::opaque;
        ai_real resolved_cutoff = k_default_alpha_cutoff;
        aiString alpha_mode_str;
        const bool has_declared_alpha_mode = material_->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode_str) == AI_SUCCESS;
        if(has_declared_alpha_mode)
        {
            APPLOG_TRACE("Mesh Importer: glTF alphaMode: {}", alpha_mode_str.C_Str());
            if(alpha_mode_str == aiString("MASK"))
            {
                resolved = alpha_mode::mask;
                material_->Get(AI_MATKEY_GLTF_ALPHACUTOFF, resolved_cutoff);
                if(resolved_cutoff <= 0.0f)
                {
                    resolved_cutoff = k_default_alpha_cutoff;
                }
            }
            else if(alpha_mode_str == aiString("BLEND"))
            {
                resolved = alpha_mode::blend;
            }
        }
        else
        {
            ai_real opacity = 1.0f;
            if(material_opacity_factor_suggests_cutout(material_, opacity))
            {
                resolved = alpha_mode::mask;
                resolved_cutoff = 1.0f - opacity;
            }
        }
        // No declared mode and still opaque: probe the base color map border for transparency
        // (foliage/fences exported without an alpha mode).
        if(!has_declared_alpha_mode && resolved == alpha_mode::opaque && !base_color_map_relative_.empty())
        {
            APPLOG_TRACE("Mesh Importer: Probing base color map border for transparency: {}", base_color_map_relative_);
            if(color_map_border_suggests_alpha_cutout(ctx_.output_dir, base_color_map_relative_, is_albedo_alpha_declared()))
            {
                APPLOG_TRACE("Mesh Importer: Promoting to alpha cutout - base color map '{}' has transparent border pixels",
                             base_color_map_relative_);
                resolved = alpha_mode::mask;
                resolved_cutoff = resolve_import_alpha_cutoff(material_);
            }
        }
        surface_.alpha = resolved;
        if(resolved == alpha_mode::mask)
        {
            surface_.alpha_cutoff = math::clamp(resolved_cutoff, 0.0f, 1.0f);
        }
        // glTF: a BLEND or MASK material's alpha is the base color texture's times baseColorFactor's alpha (a decal
        // authored at 0.35), diffuseFactor's in the specular workflow; OPAQUE ignores it, so the factor's alpha only
        // lands with a declared non-opaque mode.
        if(has_declared_alpha_mode && resolved != alpha_mode::opaque)
        {
            aiColor4D base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
            const bool has_factor = is_specular_workflow() ? material_->Get(AI_MATKEY_COLOR_DIFFUSE, base_color_factor) == AI_SUCCESS
                                                           : material_->Get(AI_MATKEY_BASE_COLOR, base_color_factor) == AI_SUCCESS;
            if(has_factor)
            {
                surface_.base_color.value.a = math::clamp(base_color_factor.a, 0.0f, 1.0f);
            }
        }
    }

    static constexpr float k_max_emissive_intensity = 100.0f;
    static constexpr ai_real k_default_alpha_cutoff = 0.5f;

    const material_import_context& ctx_;
    const aiMaterial* material_;
    standard_surface& surface_;
    std::vector<imported_texture>& textures_;
    const material_workflow workflow_;
    /// Base color map the material binds; probed for an alpha cut-out.
    std::string base_color_map_relative_;
};

void import_material(const material_import_context& ctx,
                     const aiMaterial* material,
                     standard_surface& surface,
                     std::vector<imported_texture>& textures)
{
    if(!material)
    {
        return;
    }
    log_material_texture_slots(material);
    material_importer(ctx, material, surface, textures).import();
}

} // namespace

void process_materials(asset_manager& am,
                       const fs::path& filename,
                       const fs::path& output_dir,
                       const aiScene* scene,
                       std::vector<imported_material>& materials,
                       std::vector<imported_texture>& textures)
{
    if(scene->mNumMaterials == 0)
    {
        return;
    }

    materials.resize(scene->mNumMaterials);

    texture_job_store job_store;
    texture_catalog catalog;
    std::unordered_set<int> consumed_embedded;
    std::vector<imported_texture> collect_scratch;

    const material_import_context collect_ctx{material_import_phase::collect,
                                              am,
                                              filename,
                                              output_dir,
                                              scene,
                                              catalog,
                                              &job_store};

    APPLOG_TRACE("Mesh Importer: Collecting texture import jobs for {} materials ...", scene->mNumMaterials);
    for(size_t i = 0; i < scene->mNumMaterials; ++i)
    {
        standard_surface collected;
        import_material(collect_ctx, scene->mMaterials[i], collected, collect_scratch);
    }

    APPLOG_TRACE("Mesh Importer: Running {} texture import jobs ...", job_store.jobs().size());
    run_texture_jobs_parallel(job_store, filename, output_dir, scene, catalog, consumed_embedded);

    textures.clear();
    catalog.append_to_manifest(textures);
    sort_imported_textures(textures);

    const material_import_context bind_ctx{material_import_phase::bind,
                                           am,
                                           filename,
                                           output_dir,
                                           scene,
                                           catalog,
                                           nullptr};

    APPLOG_TRACE("Mesh Importer: Binding {} materials to textures ...", scene->mNumMaterials);
    for(size_t i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial* assimp_mat = scene->mMaterials[i];

        standard_surface surface;
        import_material(bind_ctx, assimp_mat, surface, textures);

        std::string assimp_mat_name = assimp_mat->GetName().C_Str();
        if(assimp_mat_name.empty())
        {
            assimp_mat_name = fmt::format("Material {}", filename.string());
        }
        materials[i].mat = make_standard_material(surface);
        materials[i].name =
            replace_invalid_file_name_characters(string_utils::replace(fmt::format("[{}] {}", i, assimp_mat_name), ".", "_"));
    }

    mark_embedded_consumed_from_textures(textures, consumed_embedded);

    texture_job_store orphan_job_store;
    collect_orphan_embedded_texture_jobs(scene, filename, orphan_job_store, consumed_embedded);
    if(!orphan_job_store.jobs().empty())
    {
        APPLOG_TRACE("Mesh Importer: Running {} orphan embedded texture jobs ...", orphan_job_store.jobs().size());
        run_texture_jobs_parallel(orphan_job_store, filename, output_dir, scene, catalog, consumed_embedded);
        catalog.append_to_manifest(textures);
    }

    sort_imported_textures(textures);
}

} // namespace mesh_import
} // namespace importer
} // namespace unravel
