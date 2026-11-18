#include "clipboard_document.h"

#include <editor/system/project_manager.h>
#include <engine/engine.h>

#include <string_utils/utils.h>
#include <version/version.h>

#include <logging/logging.h>

namespace unravel
{
namespace clipboard_document
{

auto make_head(hpp::string_view kind) -> std::string
{
    // The kind comes before the project's name, which has no length limit: is_kind reads the head only.
    const auto& project = engine::context().get_cached<project_manager>().get_name();
    return fmt::format(R"({{"{}":{},"kind":"{}","engine_version":"{}","project":"{}")",
                       VERSION_KEY,
                       VERSION,
                       kind,
                       string_utils::escape_json(version::get_full()),
                       string_utils::escape_json(project));
}

auto is_kind(hpp::string_view text, hpp::string_view kind) -> bool
{
    // The version and the kind come first: the head is enough to tell.
    const auto head = text.substr(0, 512);
    return head.find(VERSION_KEY) != hpp::string_view::npos &&
           head.find(fmt::format("\"kind\":\"{}\"", kind)) != hpp::string_view::npos;
}

auto describe_newer(hpp::string_view engine_version) -> std::string
{
    return fmt::format("The copy was made by a newer editor ({}). Paste it in that editor, or copy it again here.",
                       engine_version);
}

} // namespace clipboard_document
} // namespace unravel
