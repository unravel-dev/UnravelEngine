#pragma once
#include <engine/engine_export.h>

#include <string>

/**
 * @brief The page a browser build runs in, as the game talks to it.
 *
 * The page keeps one object, globalThis.unravelPage (game/web/wwwroot/main.js): the functions it offers the game
 * (functions.<name> = (argument) => result) and the messages it posted for the game (post(text)). Outside a
 * browser there is no page: nothing is called and no message waits.
 */
namespace unravel::web_page
{

/// Whether the game runs in a browser page.
ENGINE_EXPORT auto is_available() -> bool;

/// Whether the page offers a function named `name`.
ENGINE_EXPORT auto has_function(const std::string& name) -> bool;

/// Calls the page's function `name` with `argument` and returns its answer as text; empty when the page offers no
/// such function, the function throws (the browser's console says what), or it answers nothing.
ENGINE_EXPORT auto call(const std::string& name, const std::string& argument) -> std::string;

/// How many messages the page posted that the game has not taken yet.
ENGINE_EXPORT auto get_message_count() -> int;

/// Takes the oldest message the page posted; empty when none waits.
ENGINE_EXPORT auto take_message() -> std::string;

} // namespace unravel::web_page
