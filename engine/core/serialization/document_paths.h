#pragma once

#include "serialization.h"

#include <hpp/string_view.hpp>

#include <set>
#include <string>

/// Property paths of associative documents: which properties a document states, which changed between
/// two documents, and saves limited to some properties. A path is relative to the document's root node,
/// with '/' between names and "[i]" for an element: "graphics/particles/spawn_scale".
namespace serialization
{

using path_set = std::set<std::string>;

/// Whether `path` is `ancestor` itself or lies inside it: "a/b" and "a[0]" lie in "a", "ab" does not.
auto is_path_within(hpp::string_view path, hpp::string_view ancestor) -> bool;

/// Whether `path` lies within one of `paths`.
auto is_path_covered(hpp::string_view path, const path_set& paths) -> bool;

/// Whether `path` is one of `paths`, lies inside one, or holds one.
auto is_path_related(hpp::string_view path, const path_set& paths) -> bool;

/// Adds `path`, and drops the paths it now holds.
void add_path(path_set& paths, const std::string& path);

/// Removes every path within `path`, and the path holding `path` when there is one: a value is stated as
/// a whole, so reverting part of it reverts all of it.
void remove_path(path_set& paths, hpp::string_view path);

/**
 * @brief The leaf paths of a document: objects are walked into, while arrays, values and empty objects
 * are leaves.
 * @param document An associative document whose top level holds the node `root`.
 * @return False when the document does not parse or has no object named `root`; `out` is then empty.
 */
auto get_leaf_paths(hpp::string_view document, hpp::string_view root, path_set& out) -> bool;

/**
 * @brief The leaf paths whose values differ between two documents of one type, walked as
 * get_leaf_paths walks them. A path one document has and the other lacks counts as changed.
 * @return False when either document does not parse or lacks `root`; `out` is then empty.
 */
auto get_changed_paths(hpp::string_view before, hpp::string_view after, hpp::string_view root, path_set& out) -> bool;

/**
 * @brief While alive, saves and loads on this thread visit only the properties related to `paths` (see
 * is_path_related), as paths below the node `root`; everything inside a listed path is visited whole. A
 * save writes only those properties, and a load over an object replaces only them.
 *
 * It replaces this thread's path context for its lifetime and puts the previous one back.
 */
class scoped_path_filter
{
public:
    scoped_path_filter(const std::string& root, const path_set& paths);
    ~scoped_path_filter();

    scoped_path_filter(const scoped_path_filter&) = delete;
    scoped_path_filter(scoped_path_filter&&) = delete;
    auto operator=(const scoped_path_filter&) -> scoped_path_filter& = delete;
    auto operator=(scoped_path_filter&&) -> scoped_path_filter& = delete;

private:
    path_context context_;
    path_context* previous_{};
};

} // namespace serialization
