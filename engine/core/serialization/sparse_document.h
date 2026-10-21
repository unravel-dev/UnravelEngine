#pragma once

#include "associative_archive.h"
#include "document_paths.h"

#include <exception>
#include <sstream>
#include <string>

/// Whole and sparse associative documents of one object: a sparse document states only some properties,
/// and loading it over an object replaces exactly those (every load reads with try_load, which leaves a
/// property the document does not state as it is).
namespace serialization
{

/// `obj` as an associative document whose top level holds the node `root`.
template<typename T>
auto save_to_document(const T& obj, const char* root) -> std::string
{
    std::ostringstream stream;
    {
        auto ar = ser20::create_oarchive_associative(stream);
        try_save(ar, ser20::make_nvp(root, obj));
    }
    return stream.str();
}

/// The properties of `obj` related to `paths` (see is_path_related), each listed path written whole; empty
/// when `paths` is.
template<typename T>
auto save_to_sparse_document(const T& obj, const char* root, const path_set& paths) -> std::string
{
    if(paths.empty())
    {
        return {};
    }
    const scoped_path_filter filter(root, paths);
    return save_to_document(obj, root);
}

/**
 * @brief Loads a document over `obj`: what it states replaces, the rest of `obj` stays.
 * @return False when the document does not parse or its root does not load; an empty document states
 * nothing and loads.
 */
template<typename T>
auto load_from_document(const std::string& document, const char* root, T& obj) -> bool
{
    if(document.empty())
    {
        return true;
    }
    try
    {
        auto ar = ser20::create_iarchive_associative(document.data(), document.size());
        return try_load(ar, ser20::make_nvp(root, obj));
    }
    catch(const std::exception&)
    {
        return false;
    }
}

/**
 * @brief Carries an edit over to `current`: the properties `edited` changes against `before` are loaded into
 * `current`, and the rest of it stays, so what changed in `current` since `before` was taken survives.
 * @return The paths of the properties the edit changed.
 */
template<typename T>
auto apply_edit(const T& before, const T& edited, const char* root, T& current) -> path_set
{
    path_set changed;
    const auto edited_document = save_to_document(edited, root);
    get_changed_paths(save_to_document(before, root), edited_document, root, changed);
    if(!changed.empty())
    {
        const scoped_path_filter filter(root, changed);
        load_from_document(edited_document, root, current);
    }
    return changed;
}

} // namespace serialization
