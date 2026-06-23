#include "format_bytes.h"

#include <bx/string.h>

#include <algorithm>
#include <array>

namespace unravel
{
namespace
{
auto clamp_non_negative(std::int64_t bytes) -> std::uint64_t
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, bytes));
}

void write_formatted(std::uint64_t bytes, char* out, std::size_t out_size, std::uint8_t num_frac)
{
    if(out == nullptr || out_size == 0)
    {
        return;
    }
    if(num_frac == 0)
    {
        bx::prettify(out, static_cast<int32_t>(out_size), bytes, bx::Units::KibiByte);
        return;
    }
    const bx::FixedStringT<32> formatted = bx::toHuman<32>(bytes, bx::Units::KibiByte, num_frac);
    bx::strCopy(out, static_cast<int32_t>(out_size), formatted.getCPtr());
}
} // namespace

auto format_bytes(std::uint64_t bytes, std::uint8_t num_frac) -> std::string
{
    std::array<char, 32> buffer{};
    write_formatted(bytes, buffer.data(), buffer.size(), num_frac);
    return buffer.data();
}

auto format_bytes(std::int64_t bytes, std::uint8_t num_frac) -> std::string
{
    return format_bytes(clamp_non_negative(bytes), num_frac);
}

void format_bytes(std::uint64_t bytes, char* out, std::size_t out_size, std::uint8_t num_frac)
{
    write_formatted(bytes, out, out_size, num_frac);
}

} // namespace unravel
