#include "layer_mask.h"
namespace unravel
{


auto get_reserved_layers() -> const std::vector<std::string>&
{
    static const std::vector<std::string> layers{"Default", "Static", "Transparent", "Reserved"};
    return layers;
}

auto get_reserved_layers_as_array() -> const std::array<std::string, 32>&
{
    static const std::array<std::string, 32> layers = []()
    {
        std::array<std::string, 32> result;
        for(size_t i = 0; i < get_reserved_layers().size(); ++i)
        {
            result[i] = get_reserved_layers()[i];
        }
        return result;
    }();
    return layers;
}
} // namespace unravel
