#include "font.hpp"

namespace unravel
{

REFLECT(font)
{
    entt::meta_factory<font>{}
        .type("font"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "font"},
            entt::attribute{"pretty_name", "Font"},
        });
}

} // namespace unravel
