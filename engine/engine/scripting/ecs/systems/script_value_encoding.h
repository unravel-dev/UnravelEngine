#pragma once

namespace unravel::script_value_encoding
{

/// Values cross between native code and scripts as text: a tag character, then the value.
/// The tags mirror UIDataModels (Decode, EncodeValue).
constexpr char VALUE_MISSING = 'n';
constexpr char VALUE_BOOL = 'b';
constexpr char VALUE_INTEGER = 'i';
constexpr char VALUE_NUMBER = 'd';
constexpr char VALUE_STRING = 's';
/// Separates encoded values in an argument list.
constexpr char VALUE_SEPARATOR = '\x1e';

} // namespace unravel::script_value_encoding
