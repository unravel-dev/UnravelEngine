#pragma once
#include <engine/engine_export.h>

#include <string>

namespace unravel
{

/**
 * @brief The license notices of everything a game ships with, from THIRD_PARTY_NOTICES.txt.
 *
 * The text is compiled into the engine, so it is there on every platform. It names the version of
 * the engine this binary was built from.
 *
 * @return The notices as ASCII text with line feeds.
 */
ENGINE_EXPORT auto get_third_party_notices() -> std::string;

} // namespace unravel
