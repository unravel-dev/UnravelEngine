#pragma once
#include <string>
#include <vector>

namespace subprocess
{
/// This process's environment with `changes` ("NAME=value") added or replacing entries of the same name
/// (case-insensitive on Windows).
auto merge_environment(const std::vector<std::string>& changes) -> std::vector<std::string>;
} // namespace subprocess
