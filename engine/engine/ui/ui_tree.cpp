#include "ui_tree.h"

namespace unravel
{

auto ui_tree::is_valid() const -> bool
{
    return !content.empty();
}

auto ui_tree::get_content_size() const -> size_t
{
    return content.size();
}

} // namespace unravel
