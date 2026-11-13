#include "package_export.h"
#include "package_archive.h"

#include <engine/meta/ecs/components/script_component.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <version/version.h>

#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace unravel::packages
{
namespace
{
constexpr const char* EXPORT_DATA_PREFIX = "app:/data/";
constexpr const char* EXPORT_ENGINE_PREFIX = "engine:/";
constexpr const char* EXPORT_SCENE_TYPE = ".spfb";
constexpr const char* EXPORT_PREFAB_TYPE = ".pfb";
constexpr const char* EXPORT_SCRIPT_TYPE = ".cs";
constexpr const char* EXPORT_TYPE_KEY = "\"type\"";
/// The documents a package carries beside its content when its folder has them.
constexpr const char* EXPORT_DOCUMENTS[] = {PACKAGE_README_NAME, PACKAGE_LICENSE_NAME, PACKAGE_CHANGELOG_NAME};

auto starts_with(const std::string& text, const std::string& prefix) -> bool;

/// Where a project asset's source is on disk, from the project's folder: no protocol needs to be registered.
auto get_source_path(const fs::path& project_root, const std::string& key) -> fs::path
{
    return project_root / "data" / fs::path(get_data_path(key));
}

auto starts_with(const std::string& text, const std::string& prefix) -> bool
{
    return text.size() >= prefix.size() && fs::make_path_key(text.substr(0, prefix.size())) == fs::make_path_key(prefix);
}

auto is_document_type(const std::string& type) -> bool
{
    return type == EXPORT_SCENE_TYPE || type == EXPORT_PREFAB_TYPE;
}

auto read_text_file(const fs::path& file) -> std::string
{
    std::ifstream stream(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

auto read_bytes(const fs::path& file, fs::byte_array_t& bytes) -> bool
{
    std::ifstream stream(file, std::ios::binary);
    if(!stream.good())
    {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

auto skip_space(const std::string& text, std::size_t position) -> std::size_t
{
    while(position < text.size() && (text[position] == ' ' || text[position] == '\n' || text[position] == '\r' || text[position] == '\t'))
    {
        ++position;
    }
    return position;
}

/// The quoted string at `position`; npos in `end` when there is none.
auto read_quoted(const std::string& text, std::size_t position, std::size_t& end) -> std::string
{
    end = std::string::npos;
    if(position >= text.size() || text[position] != '"')
    {
        return {};
    }
    const auto close = text.find('"', position + 1);
    if(close == std::string::npos)
    {
        return {};
    }
    end = close + 1;
    return text.substr(position + 1, close - position - 1);
}

/// The script types a scene or prefab document holds: a "type" directly followed by the script's "@slot".
auto find_script_type_names(const std::string& text) -> std::vector<std::string>
{
    std::vector<std::string> names;
    const std::string slot_key = std::string("\"") + SCRIPT_SLOT_KEY + "\"";
    std::size_t position = text.find(EXPORT_TYPE_KEY);
    while(position != std::string::npos)
    {
        std::size_t cursor = skip_space(text, position + std::char_traits<char>::length(EXPORT_TYPE_KEY));
        if(cursor < text.size() && text[cursor] == ':')
        {
            std::size_t end = 0;
            const auto value = read_quoted(text, skip_space(text, cursor + 1), end);
            if(end != std::string::npos)
            {
                cursor = skip_space(text, end);
                if(cursor < text.size() && text[cursor] == ',')
                {
                    cursor = skip_space(text, cursor + 1);
                    if(text.compare(cursor, slot_key.size(), slot_key) == 0 && !value.empty() &&
                       std::find(names.begin(), names.end(), value) == names.end())
                    {
                        names.push_back(value);
                    }
                }
            }
        }
        position = text.find(EXPORT_TYPE_KEY, position + 1);
    }
    return names;
}

/// Walks the references from the folder's files and adds what they reach.
class export_walker
{
public:
    export_walker(const export_source& source, export_plan& plan) : source_(source), plan_(plan)
    {
        for(const auto& [uid, meta] : source.content.database)
        {
            app_by_key_.emplace(fs::make_path_key(meta.location), std::make_pair(uid, &meta));
        }
        for(const auto& [uid, meta] : source.content.engine_database)
        {
            engine_by_key_.emplace(fs::make_path_key(meta.location), uid);
        }
        for(const auto& type : source.script_types)
        {
            script_file_by_type_.emplace(type.full_name, type.key);
        }
        for(const auto& reference : deployment::find_content_references(source.content))
        {
            references_[fs::make_path_key(reference.from)].push_back(reference.to);
        }
    }

    void add_folder()
    {
        const std::string folder_prefix = plan_.folder_key + "/";
        for(const auto& [uid, meta] : source_.content.database)
        {
            if(starts_with(meta.location, folder_prefix))
            {
                add(meta.location, export_reason::in_folder, {});
            }
        }
    }

    void walk()
    {
        while(!queue_.empty())
        {
            const auto key = queue_.front();
            queue_.pop_front();
            follow_references(key);
            follow_script_types(key);
        }
    }

private:
    void add(const std::string& key, export_reason reason, const std::string& used_by)
    {
        const auto found = app_by_key_.find(fs::make_path_key(key));
        if(found == app_by_key_.end() || !seen_.insert(found->second.first).second)
        {
            return;
        }
        const auto& [uid, meta] = found->second;
        export_entry entry;
        entry.key = meta->location;
        entry.path = get_data_path(meta->location);
        entry.uid = uid;
        entry.type = fs::path(meta->location).extension().string();
        entry.reason = reason;
        entry.used_by = used_by;
        entry.is_outside_root = reason != export_reason::in_folder;
        fs::error_code ec;
        entry.size = fs::file_size(get_source_path(source_.content.project_root, meta->location), ec);
        if(entry.path.empty())
        {
            return;
        }
        plan_.entries.push_back(std::move(entry));
        queue_.push_back(meta->location);
    }

    void follow_references(const std::string& key)
    {
        const auto found = references_.find(fs::make_path_key(key));
        if(found == references_.end())
        {
            return;
        }
        for(const auto& target : found->second)
        {
            if(starts_with(target, EXPORT_ENGINE_PREFIX))
            {
                add_engine_asset(target);
                continue;
            }
            add(target, export_reason::referenced, key);
        }
    }

    void follow_script_types(const std::string& key)
    {
        const auto type = fs::path(key).extension().string();
        if(!is_document_type(type))
        {
            return;
        }
        for(const auto& name : find_script_type_names(read_text_file(get_source_path(source_.content.project_root, key))))
        {
            const auto file = script_file_by_type_.find(name);
            if(file == script_file_by_type_.end())
            {
                if(std::find(plan_.unresolved_script_types.begin(), plan_.unresolved_script_types.end(), name) ==
                   plan_.unresolved_script_types.end())
                {
                    plan_.unresolved_script_types.push_back(name);
                }
                continue;
            }
            if(!starts_with(file->second, EXPORT_ENGINE_PREFIX))
            {
                add(file->second, export_reason::script, key);
            }
        }
    }

    void add_engine_asset(const std::string& key)
    {
        const auto found = engine_by_key_.find(fs::make_path_key(key));
        if(found == engine_by_key_.end() || !engine_seen_.insert(found->second).second)
        {
            return;
        }
        plan_.engine_assets.push_back({found->second, key});
    }

    const export_source& source_;
    export_plan& plan_;
    std::unordered_map<std::string, std::pair<hpp::uuid, const asset_database::meta*>> app_by_key_;
    std::unordered_map<std::string, hpp::uuid> engine_by_key_;
    std::unordered_map<std::string, std::string> script_file_by_type_;
    std::unordered_map<std::string, std::vector<std::string>> references_;
    std::unordered_set<hpp::uuid> seen_;
    std::unordered_set<hpp::uuid> engine_seen_;
    std::deque<std::string> queue_;
};

/// The defaults of a folder without a package.json: a name and a title made from the folder's name.
auto make_default_definition(const std::string& root) -> package_manifest
{
    package_manifest definition;
    const auto folder_name = fs::path(root).filename().string();
    definition.display_name = folder_name;
    std::string name;
    for(const char value : folder_name)
    {
        const auto c = static_cast<unsigned char>(value);
        if(std::isalnum(c) != 0)
        {
            name.push_back(char(std::tolower(c)));
        }
        else if(!name.empty() && name.back() != '-')
        {
            name.push_back('-');
        }
    }
    while(!name.empty() && name.back() == '-')
    {
        name.pop_back();
    }
    definition.name = "com.project." + (name.empty() ? std::string("package") : name);
    definition.version = "1.0.0";
    return definition;
}

/// The manifest's record of one file; false with `err` set when the file or its .meta cannot be read.
auto make_package_file(const fs::path& project_root, const export_entry& entry, package_file& file, std::string& err) -> bool
{
    const auto source = get_source_path(project_root, entry.key);
    fs::byte_array_t bytes;
    fs::byte_array_t meta_bytes;
    if(!read_bytes(source, bytes))
    {
        err = "Could not read " + entry.key + ".";
        return false;
    }
    if(!read_bytes(fs::path(source.string() + ".meta"), meta_bytes))
    {
        err = entry.key + " has no .meta file. Let the editor finish importing it, then export again.";
        return false;
    }
    file.path = entry.path;
    file.uid = entry.uid;
    file.type = entry.type;
    file.size = bytes.size();
    file.hash = fingerprint_bytes(bytes);
    file.meta_hash = fingerprint_bytes(meta_bytes);
    if(is_document_type(entry.type))
    {
        file.document_version = read_document_version(hpp::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    }
    return true;
}
} // namespace

auto export_plan::get_included_count() const -> std::size_t
{
    return std::size_t(std::count_if(entries.begin(),
                                     entries.end(),
                                     [](const export_entry& entry)
                                     {
                                         return entry.is_included;
                                     }));
}

auto export_plan::get_included_size() const -> std::uint64_t
{
    std::uint64_t size = 0;
    for(const auto& entry : entries)
    {
        size += entry.is_included ? entry.size : 0;
    }
    return size;
}

auto export_plan::get_included_script_files() const -> std::vector<fs::path>
{
    std::vector<fs::path> files;
    for(const auto& entry : entries)
    {
        if(entry.is_included && entry.type == EXPORT_SCRIPT_TYPE)
        {
            files.push_back(get_source_path(project_root, entry.key));
        }
    }
    return files;
}

auto find_missing_type_names(const std::vector<std::string>& messages) -> std::vector<std::string>
{
    // CS0246: a type or namespace name not found; CS0103: a name used without its declaration (a static class).
    static const std::regex MISSING_TYPE(R"((?:type or namespace name|The name) '([A-Za-z_][A-Za-z0-9_]*)')");
    std::vector<std::string> names;
    for(const auto& message : messages)
    {
        for(std::sregex_iterator it(message.begin(), message.end(), MISSING_TYPE), end; it != end; ++it)
        {
            const auto name = (*it)[1].str();
            if(std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }
    }
    return names;
}

auto add_scripts_declaring(const export_source& source, const std::vector<std::string>& type_names, export_plan& plan)
    -> std::vector<std::string>
{
    std::vector<std::string> added;
    if(type_names.empty())
    {
        return added;
    }
    std::string alternatives;
    for(const auto& name : type_names)
    {
        alternatives += (alternatives.empty() ? "" : "|") + name;
    }
    const std::regex declaration(R"(\b(?:class|struct|enum|interface|record|delegate\s+[\w<>\[\],\s]+?)\s+(?:)" + alternatives +
                                 R"()\b)");
    std::unordered_set<std::string> planned;
    for(const auto& entry : plan.entries)
    {
        planned.insert(fs::make_path_key(entry.key));
    }
    for(const auto& [uid, meta] : source.content.database)
    {
        const auto& key = meta.location;
        if(fs::path(key).extension().string() != EXPORT_SCRIPT_TYPE || planned.count(fs::make_path_key(key)) != 0)
        {
            continue;
        }
        const auto file = get_source_path(source.content.project_root, key);
        if(!std::regex_search(read_text_file(file), declaration))
        {
            continue;
        }
        export_entry entry;
        entry.key = key;
        entry.path = get_data_path(key);
        entry.uid = uid;
        entry.type = EXPORT_SCRIPT_TYPE;
        entry.reason = export_reason::script;
        entry.used_by = "A script of the package";
        entry.is_outside_root = !starts_with(key, plan.folder_key + "/");
        fs::error_code ec;
        entry.size = fs::file_size(file, ec);
        plan.entries.push_back(std::move(entry));
        added.push_back(key);
        for(const auto& type : source.script_types)
        {
            if(fs::make_path_key(type.key) == fs::make_path_key(key))
            {
                plan.script_types.push_back(type);
            }
        }
    }
    std::sort(plan.entries.begin(),
              plan.entries.end(),
              [](const export_entry& lhs, const export_entry& rhs)
              {
                  return lhs.path < rhs.path;
              });
    return added;
}

auto format_compiler_message(const std::string& file, int line, const std::string& message) -> std::string
{
    // The compiler repeats the file and the code before the text: "C:\...\Game.cs(12,5): error CS0246: The type ...".
    std::string text = message;
    const auto code = text.find(": error ");
    if(code != std::string::npos)
    {
        const auto colon = text.find(": ", code + 2);
        text = colon == std::string::npos ? text.substr(code + 2) : text.substr(colon + 2);
    }
    const auto name = fs::path(file).filename().string();
    return name.empty() ? text : name + " line " + std::to_string(line) + ": " + text;
}

auto get_definition_path(const fs::path& project_root, const std::string& root) -> fs::path
{
    return project_root / "data" / fs::path(root) / PACKAGE_MANIFEST_NAME;
}

auto get_data_path(const std::string& key) -> std::string
{
    const std::string prefix = EXPORT_DATA_PREFIX;
    if(!starts_with(key, prefix))
    {
        return {};
    }
    return key.substr(prefix.size());
}

auto get_data_key(const std::string& path) -> std::string
{
    return std::string(EXPORT_DATA_PREFIX) + path;
}

auto get_export_reason_name(export_reason reason) -> const char*
{
    switch(reason)
    {
        case export_reason::in_folder:
            return "In the folder";
        case export_reason::referenced:
            return "Referenced";
        case export_reason::script:
            return "Script in use";
    }
    return "";
}

auto plan_export(const export_source& source, const std::string& folder_key) -> export_plan
{
    export_plan plan;
    plan.folder_key = folder_key;
    while(!plan.folder_key.empty() && plan.folder_key.back() == '/')
    {
        plan.folder_key.pop_back();
    }
    plan.root = get_data_path(plan.folder_key);
    plan.project_root = source.content.project_root;
    const auto definition_path = get_definition_path(plan.project_root, plan.root);
    std::string err;
    fs::error_code ec;
    plan.has_definition = fs::exists(definition_path, ec) && load_manifest(definition_path, plan.definition, err);
    if(!plan.has_definition)
    {
        plan.definition = make_default_definition(plan.root);
    }
    plan.definition.root = plan.root;
    export_walker walker(source, plan);
    walker.add_folder();
    walker.walk();
    std::sort(plan.entries.begin(),
              plan.entries.end(),
              [](const export_entry& lhs, const export_entry& rhs)
              {
                  return lhs.path < rhs.path;
              });
    std::sort(plan.engine_assets.begin(),
              plan.engine_assets.end(),
              [](const package_engine_asset& lhs, const package_engine_asset& rhs)
              {
                  return lhs.key < rhs.key;
              });
    std::unordered_set<std::string> script_keys;
    for(const auto& entry : plan.entries)
    {
        if(entry.type == EXPORT_SCRIPT_TYPE)
        {
            script_keys.insert(fs::make_path_key(entry.key));
        }
    }
    for(const auto& type : source.script_types)
    {
        if(script_keys.count(fs::make_path_key(type.key)) != 0)
        {
            plan.script_types.push_back(type);
        }
    }
    return plan;
}

auto export_package(const export_plan& plan,
                    const package_manifest& definition,
                    const fs::path& output_file,
                    std::string& err,
                    package_manifest* written) -> bool
{
    package_manifest manifest = definition;
    manifest.format_version = PACKAGE_FORMAT_VERSION;
    manifest.root = plan.root;
    manifest.exported_with = version::get_current().to_string();
    manifest.engine_assets = plan.engine_assets;
    manifest.files.clear();
    manifest.script_types.clear();
    if(const auto name_problem = check_package_name(manifest.name); !name_problem.empty())
    {
        err = name_problem;
        return false;
    }
    std::vector<package_input> inputs;
    std::unordered_set<std::string> included_scripts;
    for(const auto& entry : plan.entries)
    {
        if(!entry.is_included)
        {
            continue;
        }
        package_file file;
        if(!make_package_file(plan.project_root, entry, file, err))
        {
            return false;
        }
        const auto source = get_source_path(plan.project_root, entry.key);
        inputs.push_back({entry.path, source, fs::path(source.string() + ".meta")});
        if(entry.type == EXPORT_SCRIPT_TYPE)
        {
            included_scripts.insert(fs::make_path_key(entry.key));
            // An import tells from these which of its scripts would define a type a second time.
            for(const auto& type : plan.script_types)
            {
                if(fs::make_path_key(type.key) == fs::make_path_key(entry.key))
                {
                    file.script_types.push_back(type.full_name);
                }
            }
            std::sort(file.script_types.begin(), file.script_types.end());
        }
        manifest.files.push_back(std::move(file));
    }
    if(manifest.files.empty())
    {
        err = "The package would hold no file.";
        return false;
    }
    for(const auto& type : plan.script_types)
    {
        if(included_scripts.count(fs::make_path_key(type.key)) != 0)
        {
            manifest.script_types.push_back(type.full_name);
        }
    }
    std::sort(manifest.script_types.begin(), manifest.script_types.end());
    if(!manifest.entry_scene.empty() && manifest.find_file(manifest.entry_scene) == nullptr)
    {
        manifest.entry_scene.clear();
    }
    std::vector<std::pair<std::string, fs::path>> documents;
    fs::error_code ec;
    for(const auto* name : EXPORT_DOCUMENTS)
    {
        const auto document = plan.project_root / "data" / fs::path(plan.root) / name;
        if(fs::exists(document, ec))
        {
            documents.emplace_back(name, document);
        }
    }
    if(!write_package(output_file, manifest, inputs, documents, err))
    {
        return false;
    }
    if(written != nullptr)
    {
        *written = std::move(manifest);
    }
    return true;
}

} // namespace unravel::packages
