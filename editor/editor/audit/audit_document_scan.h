#pragma once

#include "audit_snapshot.h"

#include <audit/audit_input.h>
#include <context/context.hpp>

#include <string>
#include <unordered_map>

namespace unravel
{

/**
 * @brief Loads the project's documents one at a time and checks what they hold, as the game would load them: asset
 * references that resolve to nothing, joints that cannot be made, components a profile turns off, the clips audio
 * sources play, and controllers with mistakes.
 *
 * Scenes and prefabs load into a scratch scene made for the one document and unloaded before scan returns. Main
 * thread, edit mode only.
 */
class audit_document_scanner
{
public:
    /// Starts a run over the project's documents: what the scanner learnt about their materials is forgotten.
    void begin_run();

    /// Checks one document and adds what it found to `out`.
    void scan(rtti::context& ctx, const audit_document& document, const fs::path& project_root, audit::document_scan& out);

private:
    void scan_entities(rtti::context& ctx, const audit_document& document, audit::document_scan& out);
    void scan_material(const audit_document& document, const fs::path& project_root, audit::document_scan& out);
    void scan_controller(const audit_document& document, const fs::path& project_root, audit::document_scan& out);

    /// Whether the materials the run's documents name show a vertex colour, by material id: a material many
    /// documents use is compiled once.
    std::unordered_map<std::string, bool> shown_vertex_colors_;
};

} // namespace unravel
