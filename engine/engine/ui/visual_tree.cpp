#include "visual_tree.h"

namespace unravel
{

auto visual_tree::is_valid() const -> bool
{
    return !content.empty();
}

auto visual_tree::get_content_size() const -> size_t
{
    return content.size();
}

} // namespace unravel
