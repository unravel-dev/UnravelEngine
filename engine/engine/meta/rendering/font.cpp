#include "font.hpp"

namespace unravel
{

REFLECT(font)
{
    rttr::registration::class_<font>("font")(rttr::metadata("pretty_name", "Font")).constructor<>()();

    // Register font with entt
    entt::meta_factory<font>{}
        .type("font"_hs)
        .custom<entt::attributes>(entt::attributes{
            entt::attribute{"name", "font"},
            entt::attribute{"pretty_name", "Font"},
        });
}

} // namespace unravel
