#pragma once

#include <audit/audit_config.h>
#include <audit/audit_types.h>
#include <context/context.hpp>

#include <string>

namespace unravel
{

/**
 * @brief Applies an issue's fix (audit::fixes) through the editor's own functions: import settings are written to
 * the asset's .meta file as the inspector's Apply writes them, and the asset compiles again; profile settings and
 * content rules change as Project Settings and the Deploy window change them. Main thread, edit mode.
 * @param settings The project's audit settings: a fix that lowers a size lowers it to the rule's threshold.
 * @return False with `error` set, in a sentence, when it could not be applied.
 */
auto apply_audit_fix(rtti::context& ctx, const audit::issue& item, const audit::config& settings, std::string& error) -> bool;

} // namespace unravel
