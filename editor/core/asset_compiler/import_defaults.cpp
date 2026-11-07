#include "import_defaults.h"

namespace unravel::asset_compiler
{
namespace
{
/// The protocol of the project's own files.
constexpr const char* PROJECT_PROTOCOL = "app";
} // namespace

auto follows_project_defaults(const fs::path& key) -> bool
{
    return fs::extract_protocol(fs::convert_to_protocol(key)).generic_string() == PROJECT_PROTOCOL;
}

} // namespace unravel::asset_compiler
