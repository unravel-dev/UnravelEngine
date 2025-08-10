#pragma once
#include "engine/scripting/script.h"
#include <engine/engine_export.h>
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

} // namespace unravel

namespace ex
{

template<typename T>
auto get_suported_formats() -> const std::vector<std::string>&;

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
    static std::vector<std::string> formats = {".etex", ".png", ".jpg", ".jpeg", ".tga", ".dds", ".ktx", ".pvr", ".exr", ".hdr", ".bmp", ".gif", ".psd"};
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
                                                                ex::get_suported_formats<unravel::script>()};

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
inline auto get_format(bool include_dot = true) -> std::string
{
    auto format = get_suported_formats<T>().front();
    if(include_dot)
    {
        return format;
    }
    return format.substr(1);
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
    if(is_directory)
    {
        static const std::string result = "Folder";
        return result;
    }

    static const std::string result = "";
    return result;
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

    static const std::string result = "";
    return result;
}

} // namespace ex
