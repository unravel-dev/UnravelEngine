#pragma once

#include <string>
#include <vector>

/// The command line options a deployed game starts with after Deploy and Run.
namespace unravel::deployment
{

/// The options in `text`, separated by whitespace. Quotes have no meaning, so an option cannot hold a
/// space; the Android player splits them the same way.
auto split_run_arguments(const std::string& text) -> std::vector<std::string>;

/// `text` as one word of a POSIX shell command, such as the one adb runs on a device: every character
/// stays as given.
auto quote_for_shell(const std::string& text) -> std::string;

} // namespace unravel::deployment
