#pragma once
#include "engine/scripting/script.h"
#include <engine/engine_export.h>
#include <filesystem/filesystem.h>
#include <string>
#include <vector>

namespace gfx
{
struct texture;
struct shader;
} // namespace gfx

namespace unravel
{
class mesh;
class material;
struct prefab;
struct scene_prefab;
struct animation_clip;
struct physics_material;
struct audio_clip;
struct script;
struct font;
struct ui_tree;
struct style_sheet;

} // namespace unravel

namespace ex
{

template<typename T>
auto get_suported_formats() -> const std::vector<std::string>&
{
    static_assert(!std::is_same_v<T, T>, "get_suported_formats must be specialized for this type");
    static const std::vector<std::string> result = {};
    return result;
}

template<typename T>
auto get_suported_dependencies_formats() -> const std::vector<std::string>&;


template<>
inline auto get_suported_formats<unravel::font>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".ttf", ".otf"};
    return formats;
}

template<>
inline auto get_suported_formats<gfx::texture>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".etex", ".png", ".jpg", ".jpeg", ".tga", ".dds", ".ktx", ".ktx2", ".pvr", ".exr", ".hdr", ".bmp", ".gif", ".psd"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::mesh>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".emesh", ".gltf", ".glb", ".obj", ".fbx", ".FBX", ".dae", ".blend", ".3ds"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::audio_clip>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".eaudioclip", ".ogg", ".wav", ".flac", ".mp3"};
    return formats;
}

template<>
inline auto get_suported_dependencies_formats<gfx::shader>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".sh"};
    return formats;
}

template<>
inline auto get_suported_dependencies_formats<unravel::ui_tree>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".rcss"};
    return formats;
}

template<>
inline auto get_suported_formats<gfx::shader>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".sc"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::material>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".mat", ".ematerial"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::animation_clip>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".anim"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::prefab>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".pfb"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::scene_prefab>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".spfb"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::physics_material>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".phm", ".ephmaterial"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::script>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".cs"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::ui_tree>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".rhtml"};
    return formats;
}

template<>
inline auto get_suported_formats<unravel::style_sheet>() -> const std::vector<std::string>&
{
    static std::vector<std::string> formats = {".rcss"};
    return formats;
}

inline auto get_all_formats() -> const std::vector<std::vector<std::string>>&
{
    static const std::vector<std::vector<std::string>> types = {ex::get_suported_formats<gfx::texture>(),
                                                                ex::get_suported_formats<gfx::shader>(),
                                                                ex::get_suported_formats<unravel::material>(),
                                                                ex::get_suported_formats<unravel::mesh>(),
                                                                ex::get_suported_formats<unravel::animation_clip>(),
                                                                ex::get_suported_formats<unravel::audio_clip>(),
                                                                ex::get_suported_formats<unravel::font>(),
                                                                ex::get_suported_formats<unravel::prefab>(),
                                                                ex::get_suported_formats<unravel::scene_prefab>(),
                                                                ex::get_suported_formats<unravel::physics_material>(),
                                                                ex::get_suported_formats<unravel::script>(),
                                                                ex::get_suported_formats<unravel::ui_tree>(),
                                                                ex::get_suported_formats<unravel::style_sheet>()};

    return types;
}

template<typename T>
inline auto is_format(const std::string& ex) -> bool
{
    if(ex.empty())
    {
        return false;
    }

    const auto& supported = ex::get_suported_formats<T>();
    return std::find_if(std::begin(supported),
                        std::end(supported),
                        [&](const std::string& el)
                        {
                            return el.find(ex) != std::string::npos;
                        }) != std::end(supported);
}

template<typename T>
inline auto get_format_version() -> uint64_t
{
    return 0;
}

// Template specializations for format versions
// Increment these versions when the binary serialization format changes for each asset type
template<>
inline auto get_format_version<unravel::mesh>() -> uint64_t
{
    // 8: per-submesh stable_id, per-bone bind-space bounds, per-LOD submesh bbox recompute.
    // 9: mesh bounds are bind-pose only (import-time animation sampling removed; runtime
    //    pose bounds handle animation-driven expansion).
    // 11: baked sparse signed distance field for GI tracing.
    // 13: One SDF per SUBMESH instead of one per mesh. Submeshes are drawn at their own node
    //     transforms, so a single whole-mesh field cannot be placed correctly for a model whose
    //     submeshes differ -- it duplicated the entire model once per transform.
    // 12: SDF adjacency computed on position-welded topology, open surfaces fall back to an
    //     unsigned shell.
    // 14: A submesh's SDF covers that submesh alone. It was selected by data group, which is the
    //     MATERIAL index, so every submesh sharing a material baked that whole group -- each one
    //     carrying its siblings' geometry at its own transform.
    // 15: fields are bounded by a TOTAL voxel budget, not only by a per-axis resolution cap. The
    //     per-axis cap alone permitted 16.7M voxels in one field, so a model split into many
    //     submeshes overran the atlas several times over and the bake cost hours of voxel work.
    // 16: fields bake from LOD 2 by default rather than the base topology. A DEFAULT counts as a
    //     bake-algorithm change for every asset that never overrode it -- their .meta files carry
    //     no value to compare, so nothing else would notice the switch and they would keep the
    //     field baked from full detail.
    // 17: unsigned SHELL fields classify bricks in the field's own space. The stored voxels are
    //     `unsigned - thickness`, but brick classification and the empty-brick distance used the
    //     raw unsigned value, so every empty entry over-reported by exactly the thickness -- an
    //     over-estimate, which is the one direction a conservative field may never err in, and a
    //     trace stepped that far too much and passed through the shell. It also stops the interior
    //     of a thick shell being stored as surface bricks, so a scene of open meshes uses far less
    //     atlas.
    // 18: triangles carrying no surface -- zero-area slivers and non-finite positions -- no longer
    //     reach the bake or the bounds. The renderer discards them, so such a submesh looks empty in
    //     the viewport, but their corners still sized the field: measured extents of 7,000 to 10,000
    //     units on a scene whose buildings are tens across, giving voxels of 40+ units. Since an
    //     unsigned shell is floored at one voxel, those fields traced as solid blocks swallowing
    //     whole streets, sourced from geometry nobody could see.
    // 19: a submesh whose geometry is scattered over more than 32x its largest connected piece is
    //     REFUSED a field rather than given an unusable one. The voxel comes from the bounds, so
    //     parts spread far apart each fall below one voxel and what the bake produced was a blob the
    //     size of the spread, tracing as solid geometry that is not there. Refusing costs the GI
    //     contribution of parts too small to have contributed usefully; the field cost a
    //     neighbourhood. The sliver threshold also moved from 1e-6 to 1e-3 in the same pass.
    // 20: the closedness test now requires every welded edge to be shared by EXACTLY two faces,
    //     and triangles whose corners weld together no longer contribute topology. Double-sided
    //     sheet geometry (the engine's own plane primitive is two coincident, oppositely wound
    //     sheets) previously counted as closed, its cancelling pseudonormals made every voxel's
    //     sign floating-point noise, and the field baked as random brick-quantised walls and
    //     stairs. Such geometry now takes the unsigned-shell path. UV-sphere pole slivers also
    //     stop poisoning the pole pseudonormals.
    // 21: skinned submeshes are refused a field. The bake reads bind-pose vertices and skinning
    //     rewrites the surface every frame, so the field could only ever occlude as a rigid
    //     bind-pose statue pinned to the entity's root transform. The runtime walk also refuses
    //     stale fields, so this bump is what reclaims the disk and atlas space they held.
    //
    // NOTE: the compiled asset is a function of the BAKE ALGORITHM, not only of the source
    // mesh. Any change to mesh_sdf_baker that alters its output needs a bump here, or existing
    // projects silently keep the field produced by the previous code.
    return 21;
}

template<>
inline auto get_format_version<unravel::material>() -> uint64_t
{
    return 4;
}

template<>
inline auto get_format_version<unravel::animation_clip>() -> uint64_t
{
    return 1;
}

template<>
inline auto get_format_version<unravel::physics_material>() -> uint64_t
{
    return 1;
}

template<>
inline auto get_format_version<unravel::audio_clip>() -> uint64_t
{
    return 1;
}

template<>
inline auto get_format_version<gfx::texture>() -> uint64_t
{
    return 1;
}

inline auto get_format_version(const std::string& extension) -> uint64_t
{
    if(is_format<gfx::texture>(extension))
    {
        return get_format_version<gfx::texture>();
    }

    if(is_format<unravel::mesh>(extension))
    {
        return get_format_version<unravel::mesh>();
    }
    
    if(is_format<unravel::material>(extension))
    {
        return get_format_version<unravel::material>();
    }
    
    if(is_format<unravel::animation_clip>(extension))
    {
        return get_format_version<unravel::animation_clip>();
    }
    
    if(is_format<unravel::physics_material>(extension))
    {
        return get_format_version<unravel::physics_material>();
    }
    
    if(is_format<unravel::audio_clip>(extension))
    {
        return get_format_version<unravel::audio_clip>();
    }
    
    return 0;
}

template<typename T>
inline auto get_format(bool include_dot = true) -> std::string
{
    auto format = get_suported_formats<T>().front();
    if(include_dot)
    {
        return format;
    }
    return format.substr(1);
}

/**
 * @brief True when the last path segment has a filename extension (e.g. ".spfb").
 *
 * Allocation-free scan from the end. Leading-dot names (".gitignore") are not
 * treated as having an extension.
 */
inline auto has_filename_extension(const std::string& key) noexcept -> bool
{
    if(key.empty())
    {
        return false;
    }
    for(std::size_t i = key.size(); i-- > 0;)
    {
        const char c = key[i];
        if(c == '/' || c == '\\')
        {
            return false;
        }
        if(c == '.')
        {
            const bool has_chars_after = (i + 1) < key.size();
            const bool not_leading_dot =
                (i > 0) && key[i - 1] != '/' && key[i - 1] != '\\';
            return has_chars_after && not_leading_dot;
        }
    }
    return false;
}

/**
 * @brief True for runtime/embedded instance keys such as "engine:/embedded/cube".
 *
 * These keys intentionally have no filename extension and must not be rewritten.
 */
inline auto is_embedded_key(const std::string& key) noexcept -> bool
{
    constexpr const char* k_embedded_marker = ":/embedded/";
    return key.find(k_embedded_marker) != std::string::npos;
}

/**
 * @brief True when @p key should be used as-is (has an extension or is embedded).
 */
inline auto should_keep_key_as_is(const std::string& key) noexcept -> bool
{
    return has_filename_extension(key) || is_embedded_key(key);
}

/**
 * @brief Appends a supported extension when @p key has none.
 *
 * Call only when @ref should_keep_key_as_is returns false. Tries each supported
 * format for @tparam T against the filesystem; the first existing candidate wins.
 * If none exist, appends the primary format for @tparam T.
 */
template<typename T>
inline auto resolve_key_missing_extension(const std::string& key) -> std::string
{
    fs::error_code err;
    for(const auto& format : get_suported_formats<T>())
    {
        std::string candidate;
        candidate.reserve(key.size() + format.size());
        candidate.assign(key);
        candidate.append(format);
        if(fs::exists(fs::resolve_protocol(candidate), err))
        {
            return candidate;
        }
    }
    return key + get_format<T>();
}

template<typename T>
inline auto get_suported_formats_with_wildcard() -> std::vector<std::string>
{
    auto formats = get_suported_formats<T>();
    for(auto& fmt : formats)
    {
        fmt.insert(fmt.begin(), '*');
    }

    return formats;
}

inline auto get_meta_format() -> const std::string&
{
    static const std::string result = ".meta";
    return result;
}


inline auto get_meta_directory_no_slash(const std::string& prefix = {}) -> std::string
{
    return prefix + "data";
}

inline auto get_data_directory_no_slash(const std::string& prefix = {}) -> std::string
{
    return prefix + "data";
}

inline auto get_compiled_directory_no_slash(const std::string& prefix = {}) -> std::string
{
    return prefix + "compiled";
}

inline auto get_meta_directory(const std::string& prefix = {}) -> std::string
{
    return get_meta_directory_no_slash(prefix + ":/");
}

inline auto get_data_directory(const std::string& prefix = {}) -> std::string
{
    return get_data_directory_no_slash(prefix + ":/");
}

inline auto get_compiled_directory(const std::string& prefix = {}) -> std::string
{
    return get_compiled_directory_no_slash(prefix + ":/");
}


inline auto get_type(const std::string& ex, bool is_directory = false) -> const std::string&
{
    if(is_format<gfx::texture>(ex))
    {
        static const std::string result = "Texture";
        return result;
    }
    if(is_format<gfx::shader>(ex))
    {
        static const std::string result = "Shader";
        return result;
    }
    if(is_format<unravel::material>(ex))
    {
        static const std::string result = "Material";
        return result;
    }
    if(is_format<unravel::mesh>(ex))
    {
        static const std::string result = "Mesh";
        return result;
    }
    if(is_format<unravel::animation_clip>(ex))
    {
        static const std::string result = "Animation Clip";
        return result;
    }
    if(is_format<unravel::audio_clip>(ex))
    {
        static const std::string result = "Audio Clip";
        return result;
    }
    if(is_format<unravel::prefab>(ex))
    {
        static const std::string result = "Prefab";
        return result;
    }
    if(is_format<unravel::scene_prefab>(ex))
    {
        static const std::string result = "Scene";
        return result;
    }
    if(is_format<unravel::physics_material>(ex))
    {
        static const std::string result = "Physics Material";
        return result;
    }
    if(is_format<unravel::script>(ex))
    {
        static const std::string result = "Script";
        return result;
    }
    if(is_format<unravel::font>(ex))
    {
        static const std::string result = "Font";
        return result;
    }
    if(is_format<unravel::ui_tree>(ex))
    {
        static const std::string result = "UI Tree";
        return result;
    }
    if(is_format<unravel::style_sheet>(ex))
    {
        static const std::string result = "Style Sheet";
        return result;
    }
    if(is_directory)
    {
        static const std::string result = "Folder";
        return result;
    }

    static const std::string result;
    return result;
}

inline auto is_binary(const std::string& ex) -> bool
{
    // Binary formats (compiled/processed assets)
    if(is_format<gfx::texture>(ex))
    {
        // Most texture formats are binary, except some like .hdr which can be text-based
        // But for simplicity, we'll consider all texture formats as binary
        return true;
    }
    if(is_format<unravel::mesh>(ex))
    {
        // .emesh is binary, but source formats like .obj, .gltf can be text
        // Check for binary formats specifically
        return ex == ".emesh" || ex == ".glb" || ex == ".fbx" || ex == ".FBX" || 
               ex == ".blend" || ex == ".3ds" || ex == ".dae";
    }
    if(is_format<unravel::audio_clip>(ex))
    {
        // Audio formats are typically binary
        return true;
    }
    if(is_format<gfx::shader>(ex))
    {
        // .sc shader files are text-based
        return false;
    }
    if(is_format<unravel::material>(ex))
    {
        // .mat and .ematerial are typically text-based (JSON/XML)
        return false;
    }
    if(is_format<unravel::animation_clip>(ex))
    {
        // .anim files are typically text-based
        return false;
    }
    if(is_format<unravel::prefab>(ex))
    {
        // .pfb files are typically text-based
        return false;
    }
    if(is_format<unravel::scene_prefab>(ex))
    {
        // .spfb files are typically text-based
        return false;
    }
    if(is_format<unravel::physics_material>(ex))
    {
        // Physics material files are typically text-based
        return false;
    }
    if(is_format<unravel::script>(ex))
    {
        // .cs script files are text-based
        return false;
    }
    if(is_format<unravel::font>(ex))
    {
        // Font files (.ttf, .otf) are binary
        return true;
    }
    if(is_format<unravel::ui_tree>(ex))
    {
        // .rhtml files are text-based
        return false;
    }
    if(is_format<unravel::style_sheet>(ex))
    {
        // .rcss files are text-based
        return false;
    }

    // Unknown format, assume text-based for safety
    return false;
}

template<typename T>
inline auto get_type() -> const std::string&
{
    if constexpr(std::is_same_v<T, gfx::texture>)
    {
        static const std::string result = "Texture";
        return result;
    }
    if constexpr(std::is_same_v<T, gfx::shader>)
    {
        static const std::string result = "Shader";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::material>)
    {
        static const std::string result = "Material";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::mesh>)
    {
        static const std::string result = "Mesh";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::animation_clip>)
    {
        static const std::string result = "Animation Clip";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::audio_clip>)
    {
        static const std::string result = "Audio Clip";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::prefab>)
    {
        static const std::string result = "Prefab";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::scene_prefab>)
    {
        static const std::string result = "Scene";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::physics_material>)
    {
        static const std::string result = "Physics Material";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::script>)
    {
        static const std::string result = "Script";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::script_library>)
    {
        static const std::string result = "Scripts";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::font>)
    {
        static const std::string result = "Font";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::ui_tree>)
    {
        static const std::string result = "UI Tree";
        return result;
    }
    if constexpr(std::is_same_v<T, unravel::style_sheet>)
    {
        static const std::string result = "Style Sheet";
        return result;
    }

    static const std::string result;
    return result;
}

} // namespace ex
