#include "web_page.h"

#include <base/platform/config.hpp>

#if UNRAVEL_PLATFORM_EMSCRIPTEN
#include <emscripten/emscripten.h>

#include <cstdlib>

EM_JS_DEPS(unravel_web_page, "$UTF8ToString,$stringToNewUTF8");

EM_JS(int, unravel_page_has_function, (const char* name), {
    const page = globalThis.unravelPage;
    return page && page.functions && typeof page.functions[UTF8ToString(name)] === 'function' ? 1 : 0;
});

// The answer as text the caller frees, or null. A function that throws must not unwind through the engine.
EM_JS(char*, unravel_page_call, (const char* name, const char* argument), {
    const page = globalThis.unravelPage;
    const callee = page && page.functions ? page.functions[UTF8ToString(name)] : undefined;
    if (typeof callee !== 'function') {
        return 0;
    }
    try {
        const answer = callee(UTF8ToString(argument));
        return answer === undefined || answer === null ? 0 : stringToNewUTF8(String(answer));
    } catch (e) {
        console.error('[player] The page function ' + UTF8ToString(name) + ' failed: ' + e);
        return 0;
    }
});

EM_JS(int, unravel_page_get_message_count, (), {
    const page = globalThis.unravelPage;
    return page && Array.isArray(page.messages) ? page.messages.length : 0;
});

// The oldest message as text the caller frees, or null.
EM_JS(char*, unravel_page_take_message, (), {
    const page = globalThis.unravelPage;
    if (!page || !Array.isArray(page.messages) || page.messages.length === 0) {
        return 0;
    }
    return stringToNewUTF8(String(page.messages.shift()));
});
#endif

namespace unravel::web_page
{
#if UNRAVEL_PLATFORM_EMSCRIPTEN

namespace
{
/// Takes over text the page allocated; empty for none.
auto take_text(char* text) -> std::string
{
    if(text == nullptr)
    {
        return {};
    }
    std::string result(text);
    std::free(text);
    return result;
}
} // namespace

auto is_available() -> bool
{
    return true;
}

auto has_function(const std::string& name) -> bool
{
    return unravel_page_has_function(name.c_str()) != 0;
}

auto call(const std::string& name, const std::string& argument) -> std::string
{
    return take_text(unravel_page_call(name.c_str(), argument.c_str()));
}

auto get_message_count() -> int
{
    return unravel_page_get_message_count();
}

auto take_message() -> std::string
{
    return take_text(unravel_page_take_message());
}

#else

auto is_available() -> bool
{
    return false;
}

auto has_function(const std::string& /*name*/) -> bool
{
    return false;
}

auto call(const std::string& /*name*/, const std::string& /*argument*/) -> std::string
{
    return {};
}

auto get_message_count() -> int
{
    return 0;
}

auto take_message() -> std::string
{
    return {};
}

#endif
} // namespace unravel::web_page
