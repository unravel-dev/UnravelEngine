#pragma once

#include <audit/audit_input.h>
#include <context/context.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace unravel
{

/// A document the audit loads to check what it holds.
struct audit_document
{
    enum class kind : std::uint8_t
    {
        scene,
        prefab,
        material,
        controller,
    };

    std::string key;
    kind type{kind::scene};
};

/**
 * @brief The open project as an audit run reads it: its assets with copies of their import settings, its profiles
 * resolved, the deploy problems of each, the scripts' compiler messages and where the script analysis finds what it
 * needs. Main thread. The scan and the run fill the rest.
 * @param analyze_code Whether the run starts the script analysis.
 */
auto make_audit_input(rtti::context& ctx, bool analyze_code) -> audit::input;

/// The scenes, prefabs, materials and controllers of the project, by key.
auto list_audit_documents(const audit::input& in) -> std::vector<audit_document>;

} // namespace unravel
