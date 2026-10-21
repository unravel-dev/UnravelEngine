#include "document_paths.h"

#include <ser20/external/simdjson/simdjson.h>

#include <algorithm>
#include <exception>

namespace serialization
{
namespace
{
constexpr char DOCUMENT_PATH_SEPARATOR = '/';
constexpr char DOCUMENT_ELEMENT_OPEN = '[';

auto join_document_path(const std::string& prefix, hpp::string_view name) -> std::string
{
    std::string path = prefix;
    if(!path.empty())
    {
        path += DOCUMENT_PATH_SEPARATOR;
    }
    path.append(name.data(), name.size());
    return path;
}

/// The object a non-empty value holds; false for anything else, empty objects included.
auto get_nonempty_object(simdjson::dom::element value, simdjson::dom::object& out) -> bool
{
    return value.get_object().get(out) == simdjson::SUCCESS && out.size() > 0;
}

void collect_leaves(simdjson::dom::object object, const std::string& prefix, path_set& out);

void collect_value_leaves(simdjson::dom::element value, const std::string& path, path_set& out)
{
    simdjson::dom::object object;
    if(get_nonempty_object(value, object))
    {
        collect_leaves(object, path, out);
        return;
    }
    out.insert(path);
}

void collect_leaves(simdjson::dom::object object, const std::string& prefix, path_set& out)
{
    for(const auto field : object)
    {
        collect_value_leaves(field.value, join_document_path(prefix, field.key), out);
    }
}

void collect_changes(simdjson::dom::object before, simdjson::dom::object after, const std::string& prefix, path_set& out)
{
    for(const auto field : after)
    {
        const auto path = join_document_path(prefix, field.key);
        simdjson::dom::element before_value;
        if(before[field.key].get(before_value) != simdjson::SUCCESS)
        {
            collect_value_leaves(field.value, path, out);
            continue;
        }
        simdjson::dom::object before_object;
        simdjson::dom::object after_object;
        if(get_nonempty_object(before_value, before_object) && get_nonempty_object(field.value, after_object))
        {
            collect_changes(before_object, after_object, path, out);
            continue;
        }
        if(simdjson::minify(before_value) != simdjson::minify(field.value))
        {
            out.insert(path);
        }
    }
    for(const auto field : before)
    {
        simdjson::dom::element after_value;
        if(after[field.key].get(after_value) != simdjson::SUCCESS)
        {
            collect_value_leaves(field.value, join_document_path(prefix, field.key), out);
        }
    }
}

/// The object named `root` at the top of `document`, parsed by `parser`.
auto parse_root(simdjson::dom::parser& parser, hpp::string_view document, hpp::string_view root, simdjson::dom::object& out)
    -> bool
{
    simdjson::dom::element top;
    if(parser.parse(document.data(), document.size()).get(top) != simdjson::SUCCESS)
    {
        return false;
    }
    simdjson::dom::element node;
    return top[std::string(root.data(), root.size())].get(node) == simdjson::SUCCESS &&
           node.get_object().get(out) == simdjson::SUCCESS;
}
} // namespace

auto is_path_within(hpp::string_view path, hpp::string_view ancestor) -> bool
{
    if(path.size() < ancestor.size() || path.substr(0, ancestor.size()) != ancestor)
    {
        return false;
    }
    if(path.size() == ancestor.size())
    {
        return true;
    }
    const char next = path[ancestor.size()];
    return next == DOCUMENT_PATH_SEPARATOR || next == DOCUMENT_ELEMENT_OPEN;
}

auto is_path_covered(hpp::string_view path, const path_set& paths) -> bool
{
    return std::any_of(paths.begin(),
                       paths.end(),
                       [path](const std::string& listed)
                       {
                           return is_path_within(path, listed);
                       });
}

auto is_path_related(hpp::string_view path, const path_set& paths) -> bool
{
    return std::any_of(paths.begin(),
                       paths.end(),
                       [path](const std::string& listed)
                       {
                           return is_path_within(path, listed) || is_path_within(listed, path);
                       });
}

void add_path(path_set& paths, const std::string& path)
{
    if(is_path_covered(path, paths))
    {
        return;
    }
    std::erase_if(paths,
                  [&path](const std::string& listed)
                  {
                      return is_path_within(listed, path);
                  });
    paths.insert(path);
}

void remove_path(path_set& paths, hpp::string_view path)
{
    std::erase_if(paths,
                  [path](const std::string& listed)
                  {
                      return is_path_within(listed, path) || is_path_within(path, listed);
                  });
}

auto get_leaf_paths(hpp::string_view document, hpp::string_view root, path_set& out) -> bool
{
    out.clear();
    try
    {
        simdjson::dom::parser parser;
        simdjson::dom::object node;
        if(!parse_root(parser, document, root, node))
        {
            return false;
        }
        collect_leaves(node, {}, out);
        return true;
    }
    catch(const std::exception&)
    {
        out.clear();
        return false;
    }
}

auto get_changed_paths(hpp::string_view before, hpp::string_view after, hpp::string_view root, path_set& out) -> bool
{
    out.clear();
    try
    {
        simdjson::dom::parser before_parser;
        simdjson::dom::parser after_parser;
        simdjson::dom::object before_node;
        simdjson::dom::object after_node;
        if(!parse_root(before_parser, before, root, before_node) || !parse_root(after_parser, after, root, after_node))
        {
            return false;
        }
        collect_changes(before_node, after_node, {}, out);
        return true;
    }
    catch(const std::exception&)
    {
        out.clear();
        return false;
    }
}

scoped_path_filter::scoped_path_filter(const std::string& root, const path_set& paths)
    : previous_(get_path_context())
{
    context_.should_serialize_property_callback = [root, paths](const std::string& full_path) -> bool
    {
        if(full_path == root)
        {
            return true;
        }
        if(!is_path_within(full_path, root))
        {
            return false;
        }
        const auto relative = hpp::string_view(full_path).substr(root.size() + 1);
        return is_path_related(relative, paths);
    };
    context_.enable_recording();
    set_path_context(&context_);
}

scoped_path_filter::~scoped_path_filter()
{
    set_path_context(previous_);
}

} // namespace serialization
