#pragma once

#include <engine/assets/impl/asset_extensions.h>

#include <array>
#include <cstddef>
#include <string>

namespace unravel
{

/// The folder of the engine's built-in shapes (cube.emesh, sphere.emesh, ...), which the editor's Create menu
/// offers by name.
inline constexpr const char* BUILT_IN_MESH_FOLDER = "engine:/data/meshes";

/// How a built-in shape's key was spelled while the shapes were generated at startup: this and the shape's name
/// ("engine:/embedded/cube"). Keys in that spelling still load the shape.
inline constexpr const char* EMBEDDED_MESH_PREFIX = "engine:/embedded/";

/// The key of the built-in shape called `name` ("cube" -> "engine:/data/meshes/cube.emesh").
inline auto get_built_in_mesh_key(const std::string& name) -> std::string
{
    return std::string(BUILT_IN_MESH_FOLDER) + "/" + name + ex::get_mesh_shape_format();
}

/// The built-in shape a key in the earlier spelling names ("engine:/embedded/cube" ->
/// "engine:/data/meshes/cube.emesh"); empty for every other key. Whether the engine has that shape is the caller's
/// question.
inline auto get_built_in_mesh_key_of_embedded(const std::string& key) -> std::string
{
    const std::string prefix = EMBEDDED_MESH_PREFIX;
    if(key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
    {
        return {};
    }
    return get_built_in_mesh_key(key.substr(prefix.size()));
}

/**
 * The engine folders a deploy decides asset by asset when it ships only used assets: their assets ship when the game
 * uses them or the engine loads them by path (ENGINE_LOADED_KEYS). Every other engine asset always ships. An asset in
 * these folders that the engine loads by path must therefore be listed below, or such a deploy leaves it out.
 */
inline constexpr std::array<const char*, 3> ENGINE_FOLDERS_SHIPPED_WHEN_USED = {
    "engine:/data/fonts",
    "engine:/data/ui",
    BUILT_IN_MESH_FOLDER,
};

/// The UI's font faces, which the UI system loads as it starts and the base style sheet declares again.
inline constexpr std::array<const char*, 6> UI_FONT_KEYS = {
    "engine:/data/fonts/Inter/InterVariable.ttf",
    "engine:/data/fonts/Inter/InterVariable-Italic.ttf",
    "engine:/data/fonts/OpenSans/OpenSans-VariableFont_wdth,wght.ttf",
    "engine:/data/fonts/OpenSans/OpenSans-Italic-VariableFont_wdth,wght.ttf",
    "engine:/data/fonts/RobotoMono/RobotoMono-VariableFont_wght.ttf",
    "engine:/data/fonts/RobotoMono/RobotoMono-Italic-VariableFont_wght.ttf",
};

/// The font a text component starts with.
inline constexpr const char* TEXT_DEFAULT_FONT_KEY = "engine:/data/fonts/Inter/static/Inter-Regular.ttf";

/// The style sheet every UI document starts from.
inline constexpr const char* UI_BASE_STYLE_SHEET_KEY = "engine:/data/ui/rml.rcss";

/// The splash screen's document when the project names none.
inline constexpr const char* SPLASH_DOCUMENT_KEY = "engine:/data/ui/splash.rhtml";

namespace detail
{
template<std::size_t HeadSize, std::size_t TailSize>
constexpr auto join_keys(const std::array<const char*, HeadSize>& head, const std::array<const char*, TailSize>& tail)
    -> std::array<const char*, HeadSize + TailSize>
{
    std::array<const char*, HeadSize + TailSize> keys{};
    for(std::size_t i = 0; i < HeadSize; ++i)
    {
        keys[i] = head[i];
    }
    for(std::size_t i = 0; i < TailSize; ++i)
    {
        keys[HeadSize + i] = tail[i];
    }
    return keys;
}
} // namespace detail

/// Every key above: what the engine loads by path from those folders.
inline constexpr auto ENGINE_LOADED_KEYS =
    detail::join_keys(UI_FONT_KEYS, std::array{TEXT_DEFAULT_FONT_KEY, UI_BASE_STYLE_SHEET_KEY, SPLASH_DOCUMENT_KEY});

} // namespace unravel
