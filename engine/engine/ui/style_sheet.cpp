#include "style_sheet.h"

namespace unravel
{

auto style_sheet::is_valid() const -> bool
{
    return !content.empty();
}

auto style_sheet::get_content_size() const -> size_t
{
    return content.size();
}

} // namespace unravel
