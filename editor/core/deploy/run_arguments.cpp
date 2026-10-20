#include "run_arguments.h"

#include <sstream>

namespace unravel::deployment
{
namespace
{
/// Inside single quotes only the quote itself is special: it closes the quoted part, an escaped quote
/// follows, and a new quoted part opens.
constexpr const char* QUOTED_SINGLE_QUOTE = "'\\''";
} // namespace

auto split_run_arguments(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> arguments;
    std::istringstream stream(text);
    std::string argument;
    while(stream >> argument)
    {
        arguments.push_back(argument);
    }
    return arguments;
}

auto quote_for_shell(const std::string& text) -> std::string
{
    std::string quoted = "'";
    for(const char character : text)
    {
        if(character == '\'')
        {
            quoted += QUOTED_SINGLE_QUOTE;
        }
        else
        {
            quoted += character;
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace unravel::deployment
