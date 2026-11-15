#pragma once
#include <engine/engine_export.h>

#include <engine/rendering/material_graph/material_graph.h>

#include <serialization/serialization.h>

#include <memory>
#include <string>

namespace unravel
{
SAVE_EXTERN(material_graph_input);
LOAD_EXTERN(material_graph_input);

SERIALIZE_EXTERN(material_graph_setting);

SAVE_EXTERN(material_graph_node);
LOAD_EXTERN(material_graph_node);

SERIALIZE_EXTERN(material_graph_comment);

SAVE_EXTERN(material_graph);
LOAD_EXTERN(material_graph);

/**
 * @brief The graph as the JSON text it is saved as, and back. An input's value is a list of up to four numbers
 * ("value": [1.0, 0.5, 0.2]); fields left out keep their defaults, so a hand-written graph lists only what it sets.
 * A texture node saves its texture's uid and key; written by hand, "texture_key": "app:/data/T.etex" is enough.
 * Load returns false when the text does not parse.
 */
auto save_to_string(const material_graph& obj) -> std::string;
auto load_from_string(const std::string& text, material_graph& obj) -> bool;

/// Saves the graph as JSON to @p absolute_path, written beside it first and renamed into place, so a reader (the
/// dependency watcher) never sees half a file. False when the file cannot be written.
auto save_to_file(const std::string& absolute_path, const material_graph& obj) -> bool;
/// Loads a graph saved as JSON. False when the file cannot be read or does not parse.
auto load_from_file(const std::string& absolute_path, material_graph& obj) -> bool;

/**
 * @brief The material function in the file at @p key (a protocol path such as app:/data/f.mgraph), read again only
 * when the file changed. Null when there is no such file or it does not read as a graph. Thread safe.
 */
auto load_material_function(const std::string& key) -> std::shared_ptr<const material_graph>;

} // namespace unravel
