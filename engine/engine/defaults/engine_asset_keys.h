#pragma once

#include <array>

namespace unravel
{

/**
 * The engine folders whose assets a deploy that ships only used assets ships only when something uses them:
 * the game's assets reference them, or the engine loads them by one of the keys below. Every other engine
 * asset always ships, so an asset in these folders that the engine loads by path must be listed here.
 */
inline constexpr std::array<const char*, 2> ENGINE_FOLDERS_SHIPPED_WHEN_USED = {
    "engine:/data/fonts",
    "engine:/data/ui",
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

/// Every key above: what the engine loads by path from those folders.
inline constexpr auto ENGINE_LOADED_KEYS = []
{
    std::array<const char*, UI_FONT_KEYS.size() + 3> keys{};
    for(std::size_t i = 0; i < UI_FONT_KEYS.size(); ++i)
    {
        keys[i] = UI_FONT_KEYS[i];
    }
    keys[UI_FONT_KEYS.size()] = TEXT_DEFAULT_FONT_KEY;
    keys[UI_FONT_KEYS.size() + 1] = UI_BASE_STYLE_SHEET_KEY;
    keys[UI_FONT_KEYS.size() + 2] = SPLASH_DOCUMENT_KEY;
    return keys;
}();

} // namespace unravel
