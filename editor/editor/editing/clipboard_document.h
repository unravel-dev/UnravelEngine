#pragma once

#include <hpp/string_view.hpp>

#include <cstdint>
#include <string>

namespace unravel
{

/**
 * @brief The head every editor clipboard document starts with: its layout, the editor and project that wrote it, and
 *        what it holds (entities, components or one value).
 *
 * The document is JSON text on the OS clipboard, so a copy outlives play mode, a recompile, the editor and the
 * project it came from, and people and agents can read it.
 */
namespace clipboard_document
{

/// The layout this build writes and the newest it reads.
constexpr int64_t VERSION = 1;
constexpr const char* VERSION_KEY = "unravel_clipboard";

constexpr const char* ENTITIES_KIND = "entities";
constexpr const char* COMPONENTS_KIND = "components";
constexpr const char* VALUE_KIND = "value";

/// The document's opening, for the members of its kind to follow: `{"unravel_clipboard":1,"kind":"...",...`.
auto make_head(hpp::string_view kind) -> std::string;

/// Whether @p text is a clipboard document of @p kind, by its head; it may still be refused when read.
auto is_kind(hpp::string_view text, hpp::string_view kind) -> bool;

/// The sentence a reader gives a document whose layout is newer than this build reads.
auto describe_newer(hpp::string_view engine_version) -> std::string;

} // namespace clipboard_document
} // namespace unravel
