#include "third_party_notices.h"

#include <version/version.h>

#include <cstring>

namespace unravel
{

namespace
{
/// Where the notices name the engine version, which the text file cannot know.
constexpr const char* ENGINE_VERSION_PLACEHOLDER = "@ENGINE_VERSION@";

constexpr unsigned char NOTICES_TEXT[] = {
#include <engine/third_party_notices_text.inc>
};
} // namespace

auto get_third_party_notices() -> std::string
{
    std::string text(reinterpret_cast<const char*>(NOTICES_TEXT), sizeof(NOTICES_TEXT));
    const auto placeholder = text.find(ENGINE_VERSION_PLACEHOLDER);
    if(placeholder != std::string::npos)
    {
        text.replace(placeholder, std::strlen(ENGINE_VERSION_PLACEHOLDER), version::get_full());
    }
    return text;
}

} // namespace unravel
