#pragma once

#include "audit_input.h"

#include <functional>
#include <string>

namespace unravel::audit
{

/// The analysis tool's output format this engine reads.
constexpr int CODE_ANALYSIS_FORMAT_VERSION = 1;

/**
 * @brief Runs the script analysis tool over the project's compiled script library and reads what it found. Waits
 * for the tool; any thread.
 * @param should_stop Asked while the tool runs; true ends it, and the result says it was stopped.
 */
auto run_code_analysis(const code_tool_paths& paths, const std::function<bool()>& should_stop) -> code_analysis;

/**
 * @brief Reads the tool's JSON output.
 * @return False with `error` set when the text is not the tool's output in this format.
 */
auto read_code_analysis(const std::string& text, const fs::path& project_root, code_analysis& result, std::string& error) -> bool;

} // namespace unravel::audit
