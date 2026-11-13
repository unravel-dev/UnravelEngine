/*
 * Validation suite for content packages (.unravelpkg): export, import, update and removal.
 *
 * Runs inside the unravel-tests runner:
 *   cmake --build <build-dir> --target tests
 *   <build-dir>/bin/unravel-tests --suite "content packages"
 *
 * Projects are folders under a temp folder, their asset databases read from their .meta files the way the
 * editor reads them. This pins:
 *   - versions and ranges parse and order (pre-releases before releases, caret and tilde ranges);
 *   - package paths that climb out of the data folder, are absolute, hold odd characters, name a Windows device or
 *     end in a dot or a space are refused, and so is a package whose folder or demo scene has such a path;
 *   - an export takes the folder's assets, the project assets they reference (outside the folder too), the
 *     script that defines a script type a prefab uses, and lists the engine assets without shipping them;
 *   - the same content exports to the same bytes; the folder's README travels beside the content;
 *   - a first import writes every file with its .meta and its id and records the package; importing it again
 *     changes nothing;
 *   - an update writes the files unchanged since the install, keeps an edited one, and moves a file the new
 *     version dropped to the trash;
 *   - a file whose id the project uses for another file is added as a copy with an id made from the project, and
 *     the package's other files point at the copy; a file whose name is taken is added under a free name;
 *   - a copy whose made id the project holds already gets a new one, and the record names the id it was written with;
 *   - a scene or prefab in a layout this engine does not read stays out; a package that needs a newer engine is
 *     refused; a script type two files would define blocks the import for the actions picked, and another action
 *     clears it; a file with no free name left for its copy can only be skipped;
 *   - a damaged package is refused and writes nothing; a trash that fails at once or part way puts the project back
 *     as it was, for an import and for a removal;
 *   - a project record (packages.cfg) that does not read stops imports and removals instead of being written over;
 *   - two plans of an unchanged project are the same, a file created in between makes them differ, and the actions
 *     picked carry over to the files planned alike;
 *   - removing a package moves its unchanged files to the trash, keeps the edited ones and drops its record.
 */

#include "../tests.h"

#include <packages/package_archive.h>
#include <packages/package_export.h>
#include <packages/package_import.h>
#include <packages/package_install.h>
#include <packages/package_version.h>

#include <engine/meta/assets/asset_database.hpp>
#include <filesystem/filesystem.h>
#include <uuid/uuid.h>
#include <version/version.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
using namespace unravel;
using namespace unravel::packages;

int g_checks = 0;
int g_failures = 0;

constexpr const char* PACKAGES_TEST_NAME = "com.tests.kit";
constexpr const char* PACKAGES_SCRIPT_TYPE = "Kit.ButtonScript";
constexpr const char* PACKAGES_ENGINE_CUBE_KEY = "engine:/data/meshes/cube.emesh";
/// A layout no build reads.
constexpr int PACKAGES_FUTURE_DOCUMENT_VERSION = 99;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if(!condition)
    {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto get_temp_folder() -> fs::path
{
    return fs::temp_directory_path() / "unravel-tests-content-packages";
}

void write_text(const fs::path& file, const std::string& text)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), std::streamsize(text.size()));
}

void write_binary(const fs::path& file, const fs::byte_array_t& bytes)
{
    fs::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

auto read_text(const fs::path& file) -> std::string
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

auto is_on_disk(const fs::path& file) -> bool
{
    fs::error_code ec;
    return fs::exists(file, ec);
}

/// A .meta as the editor writes one, with the asset's type and id.
auto make_meta(const std::string& type, const hpp::uuid& uid) -> std::string
{
    return "{\n \"meta\": {\n  \"type\": \"" + type + "\",\n  \"uid\": \"" + hpp::to_string(uid) + "\"\n }\n}\n";
}

/// Writes an asset and its .meta into the project's data folder.
void write_asset(const fs::path& project, const std::string& path, const std::string& text, const hpp::uuid& uid)
{
    const auto file = project / "data" / fs::path(path);
    write_text(file, text);
    write_text(fs::path(file.string() + ".meta"), make_meta(fs::path(path).extension().string(), uid));
}

/// The project's asset database, read from its .meta files.
auto read_database(const fs::path& project) -> asset_database::database_t
{
    asset_database::database_t database;
    const auto data = project / "data";
    fs::error_code ec;
    for(fs::recursive_directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->path().extension() != ".meta")
        {
            continue;
        }
        asset_meta meta;
        if(!load_from_file(it->path().string(), meta) || meta.uid.is_nil())
        {
            continue;
        }
        auto source = it->path();
        source.replace_extension();
        const auto relative = fs::relative(source, data, ec).generic_string();
        database[meta.uid] = {get_data_key(relative), meta};
    }
    return database;
}

/// Everything the test project's engine has: the cube.
auto get_engine_cube_uid() -> hpp::uuid
{
    return generate_uuid("tests-engine-cube");
}

auto make_project_view(const fs::path& project, const std::vector<script_type_source>& script_types = {}) -> project_view
{
    project_view view;
    view.project_root = project;
    view.project_guid = project.filename().string();
    view.database = read_database(project);
    view.engine_uids.insert(get_engine_cube_uid());
    view.script_types = script_types;
    view.engine = version::get_current();
    if(!load_registry(get_registry_path(project), view.registry, view.registry_error) && view.registry_error.empty())
    {
        view.registry_error = "The record does not read.";
    }
    return view;
}

/// The trash: a folder of batches beside the projects.
struct test_trash
{
    fs::path root;
    int batches{};
    /// How many files the trash moves before it refuses, as a locked file stops the editor's; -1 never refuses.
    int fails_after{-1};

    auto make_hooks() -> install_hooks
    {
        install_hooks hooks;
        hooks.move_to_trash = [this](const std::vector<fs::path>& files, std::string& batch, std::string& err) -> bool
        {
            batch = std::to_string(++batches);
            fs::error_code ec;
            for(std::size_t index = 0; index < files.size(); ++index)
            {
                if(fails_after >= 0 && int(index) >= fails_after)
                {
                    err = "The trash refuses.";
                    return false;
                }
                const auto folder = root / batch / std::to_string(index);
                fs::create_directories(folder, ec);
                const auto meta = fs::path(files[index].string() + ".meta");
                fs::rename(files[index], folder / files[index].filename(), ec);
                fs::rename(meta, folder / meta.filename(), ec);
                write_text(folder / "origin.txt", files[index].string());
            }
            return true;
        };
        hooks.restore_from_trash = [this](const std::string& batch, std::string&) -> bool
        {
            fs::error_code ec;
            for(fs::directory_iterator it(root / batch, ec), end; !ec && it != end; it.increment(ec))
            {
                const fs::path origin(read_text(it->path() / "origin.txt"));
                fs::rename(it->path() / origin.filename(), origin, ec);
                fs::rename(it->path() / (origin.filename().string() + ".meta"), fs::path(origin.string() + ".meta"), ec);
            }
            return true;
        };
        return hooks;
    }
};

/// The ids of the source project's assets.
struct source_ids
{
    hpp::uuid prefab = generate_uuid("tests-kit-button-prefab");
    hpp::uuid material = generate_uuid("tests-kit-button-material");
    hpp::uuid texture = generate_uuid("tests-shared-wood-texture");
    hpp::uuid script = generate_uuid("tests-scripts-button-script");
    hpp::uuid unrelated = generate_uuid("tests-other-unrelated");
};

auto make_prefab_text(const source_ids& ids, int version) -> std::string
{
    return "{\n \"version\": " + std::to_string(version) + ",\n \"entities\": [\n  {\n   \"material\": {\"uid\": \"" +
           hpp::to_string(ids.material) + "\"},\n   \"scripts\": [\n    {\n     \"type\": \"" + PACKAGES_SCRIPT_TYPE +
           "\",\n     \"@slot\": \"" + hpp::to_string(generate_uuid("tests-slot")) + "\"\n    }\n   ]\n  }\n ]\n}\n";
}

auto make_material_text(const source_ids& ids, const std::string& tint) -> std::string
{
    return "{\n \"material\": {\n  \"tint\": \"" + tint + "\",\n  \"color_map\": {\"uid\": \"" + hpp::to_string(ids.texture) +
           "\"},\n  \"mesh\": {\"uid\": \"" + hpp::to_string(get_engine_cube_uid()) + "\"}\n }\n}\n";
}

/// Bytes with zeros: a binary asset.
auto make_texture_bytes() -> fs::byte_array_t
{
    fs::byte_array_t bytes(256);
    for(std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = std::uint8_t(index % 7 == 0 ? 0 : index);
    }
    return bytes;
}

void make_source_project(const fs::path& project, const source_ids& ids)
{
    write_asset(project, "Kit/Button.pfb", make_prefab_text(ids, 1), ids.prefab);
    write_asset(project, "Kit/Button.mat", make_material_text(ids, "red"), ids.material);
    write_binary(project / "data/Shared/wood.png", make_texture_bytes());
    write_text(project / "data/Shared/wood.png.meta", make_meta(".png", ids.texture));
    write_asset(project, "Scripts/ButtonScript.cs", "namespace Kit { public class ButtonScript {} }\n", ids.script);
    write_asset(project, "Other/unrelated.mat", "{\"material\": {}}\n", ids.unrelated);
    write_text(project / "data/Kit/README.md", "# Kit\n\nButtons.\n");
}

auto get_script_types() -> std::vector<script_type_source>
{
    return {{PACKAGES_SCRIPT_TYPE, "app:/data/Scripts/ButtonScript.cs"}};
}

auto make_export_source(const fs::path& project) -> export_source
{
    export_source source;
    source.content.database = read_database(project);
    source.content.project_root = project;
    asset_database::meta cube;
    cube.location = PACKAGES_ENGINE_CUBE_KEY;
    cube.meta.uid = get_engine_cube_uid();
    cube.meta.type = ".emesh";
    source.content.engine_database[cube.meta.uid] = cube;
    source.content.engine_root = project / "engine";
    source.script_types = get_script_types();
    return source;
}

/// Exports the Kit folder at `version`, optionally leaving the script out.
auto export_kit(const fs::path& project, const fs::path& output, const std::string& version, bool include_script = true)
    -> export_plan
{
    auto plan = plan_export(make_export_source(project), "app:/data/Kit");
    auto definition = plan.definition;
    definition.name = PACKAGES_TEST_NAME;
    definition.display_name = "Kit";
    definition.version = version;
    for(auto& entry : plan.entries)
    {
        entry.is_included = include_script || entry.type != ".cs";
    }
    std::string err;
    const bool is_exported = export_package(plan, definition, output, err);
    check(is_exported, "export of version " + version + " (" + err + ")");
    return plan;
}

auto find_entry(const export_plan& plan, const std::string& path) -> const export_entry*
{
    for(const auto& entry : plan.entries)
    {
        if(entry.path == path)
        {
            return &entry;
        }
    }
    return nullptr;
}

/// Sets the action of the package's script in the plan and checks the clashes again, as the review does.
void set_script_action(import_plan& plan, const project_view& project, file_action action)
{
    for(auto& planned : plan.files)
    {
        if(plan.manifest.files[planned.index].type == ".cs")
        {
            planned.action = action;
        }
    }
    update_script_clashes(plan, project);
}

auto find_planned(const import_plan& plan, const std::string& path) -> const planned_file*
{
    for(const auto& planned : plan.files)
    {
        if(plan.manifest.files[planned.index].path == path)
        {
            return &planned;
        }
    }
    return nullptr;
}

auto open_archive(const fs::path& file, package_archive& archive) -> bool
{
    std::string err;
    const bool is_open = archive.open(file, err);
    check(is_open, "open " + file.filename().string() + " (" + err + ")");
    return is_open;
}

auto install(const fs::path& package, const fs::path& project, test_trash& trash, const std::vector<script_type_source>& types = {})
    -> install_result
{
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return {};
    }
    const auto view = make_project_view(project, types);
    const auto plan = plan_import(archive, view);
    return install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
}

void test_versions()
{
    check(parse_version("1.2.3").has_value() && parse_version("v1.2.3").has_value(), "plain versions parse");
    check(!parse_version("1.2").has_value() && !parse_version("1.2.x").has_value() && !parse_version("").has_value(),
          "malformed versions do not parse");
    check(*parse_version("1.2.3-preview.1") < *parse_version("1.2.3"), "a pre-release orders before its release");
    check(*parse_version("1.10.0") > *parse_version("1.9.9"), "numbers compare as numbers");
    check(is_in_range("1.4.0", "^1.2.0") && !is_in_range("2.0.0", "^1.2.0"), "caret stays below the next major");
    check(is_in_range("0.3.5", "^0.3.0") && !is_in_range("0.4.0", "^0.3.0"), "caret below 1.0 stays in the minor");
    check(is_in_range("1.2.9", "~1.2.0") && !is_in_range("1.3.0", "~1.2.0"), "tilde stays in the minor");
    check(is_in_range("1.5.0", ">=1.0.0 <2.0.0") && !is_in_range("2.0.0", ">=1.0.0 <2.0.0"), "bounds hold together");
    check(is_in_range("7.0.0", "*") && is_in_range("7.0.0", ""), "any version");
    check(parse_version("1.2.3")->get_next_patch().to_string() == "1.2.4", "the next patch");
    check(check_package_name("com.studio.ui-kit").empty() && !check_package_name("Com.Studio").empty() &&
              !check_package_name("").empty() && !check_package_name("a b").empty(),
          "package names");
}

void test_paths()
{
    check(is_safe_package_path("Kit/Button.pfb"), "a plain path is safe");
    check(!is_safe_package_path("../Kit/Button.pfb") && !is_safe_package_path("Kit/../../x") && !is_safe_package_path("Kit/./x"),
          "climbing paths are refused");
    check(!is_safe_package_path("/Kit/x") && !is_safe_package_path("C:/x") && !is_safe_package_path("Kit\\x") &&
              !is_safe_package_path("Kit//x") && !is_safe_package_path(""),
          "absolute, drive and odd paths are refused");
    check(!is_safe_package_path("Audio/NUL.wav") && !is_safe_package_path("con") && !is_safe_package_path("Kit/Com1.txt") &&
              is_safe_package_path("Kit/Console.cs") && is_safe_package_path("Kit/Null.cs"),
          "Windows device names are refused, longer names are not");
    check(!is_safe_package_path("Kit/Button.pfb.") && !is_safe_package_path("Kit /Button.pfb") && !is_safe_package_path("Kit/x "),
          "names Windows would cut a dot or a space from are refused");
}

void test_export(const fs::path& source_project, const fs::path& folder, const source_ids& ids)
{
    const auto package = folder / "kit-1.0.0.unravelpkg";
    const auto plan = export_kit(source_project, package, "1.0.0");
    const auto* prefab = find_entry(plan, "Kit/Button.pfb");
    const auto* material = find_entry(plan, "Kit/Button.mat");
    const auto* texture = find_entry(plan, "Shared/wood.png");
    const auto* script = find_entry(plan, "Scripts/ButtonScript.cs");
    check(prefab != nullptr && material != nullptr && prefab->reason == export_reason::in_folder, "the folder's files are in");
    check(texture != nullptr && texture->reason == export_reason::referenced && texture->is_outside_root &&
              texture->used_by == "app:/data/Kit/Button.mat",
          "a referenced file outside the folder is in, with what uses it");
    check(script != nullptr && script->reason == export_reason::script, "the script of a used script type is in");
    check(find_entry(plan, "Other/unrelated.mat") == nullptr, "an unrelated file stays out");
    check(plan.entries.size() == 4, "four files are planned");
    check(plan.engine_assets.size() == 1 && plan.engine_assets.front().key == PACKAGES_ENGINE_CUBE_KEY,
          "the engine asset is listed");
    check(plan.script_types.size() == 1 && plan.script_types.front().full_name == PACKAGES_SCRIPT_TYPE,
          "the script's type is listed");
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    const auto& manifest = archive.get_manifest();
    check(manifest.files.size() == 4 && manifest.name == PACKAGES_TEST_NAME && manifest.root == "Kit", "the manifest");
    check(!manifest.exported_with.empty(), "the manifest records the engine");
    check(manifest.find_file("Kit/Button.pfb") != nullptr && manifest.find_file("Kit/Button.pfb")->document_version == 1,
          "a prefab records its layout");
    check(manifest.script_types.size() == 1, "the manifest lists the script type");
    const auto* script_file = manifest.find_file("Scripts/ButtonScript.cs");
    check(script_file != nullptr && script_file->script_types == std::vector<std::string>{PACKAGES_SCRIPT_TYPE} &&
              manifest.find_file("Kit/Button.pfb")->script_types.empty(),
          "a script file lists the script types it defines");
    check(archive.read_document(PACKAGE_README_NAME).find("Buttons.") != std::string::npos, "the README travels");
    fs::byte_array_t bytes;
    std::string err;
    check(archive.read_file(*manifest.find_file("Shared/wood.png"), bytes, err) && bytes == make_texture_bytes(),
          "a binary file reads back");
    const auto again = folder / "kit-1.0.0-again.unravelpkg";
    export_kit(source_project, again, "1.0.0");
    check(read_text(package) == read_text(again), "the same content exports to the same bytes");
}

void test_script_lookup(const fs::path& source_project)
{
    write_asset(source_project, "Scripts/Board.cs", "namespace Kit\n{\n    public enum Direction { Up, Down }\n}\n", generate_uuid("tests-board-script"));
    const std::vector<std::string> messages = {
        "C:\\p\\data\\Scripts\\ButtonScript.cs(4,9): error CS0246: The type or namespace name 'Direction' could not be "
        "found (are you missing a using directive or an assembly reference?)",
        "C:\\p\\data\\Scripts\\ButtonScript.cs(9,1): error CS0103: The name 'Missing' does not exist in the current context"};
    const auto names = find_missing_type_names(messages);
    check(names.size() == 2 && names[0] == "Direction" && names[1] == "Missing", "missing type names are read from the compiler");
    auto plan = plan_export(make_export_source(source_project), "app:/data/Kit");
    const auto added = add_scripts_declaring(make_export_source(source_project), names, plan);
    check(added.size() == 1 && added.front() == "app:/data/Scripts/Board.cs" && find_entry(plan, "Scripts/Board.cs") != nullptr,
          "the script declaring a missing type is added");
    check(add_scripts_declaring(make_export_source(source_project), names, plan).empty(), "a script is added once");
    check(format_compiler_message("C:/p/data/Scripts/ButtonScript.cs", 4, messages[0]) ==
              "ButtonScript.cs line 4: The type or namespace name 'Direction' could not be found (are you missing a using "
              "directive or an assembly reference?)",
          "a compiler message reads short");
    fs::error_code ec;
    fs::remove(source_project / "data/Scripts/Board.cs", ec);
    fs::remove(source_project / "data/Scripts/Board.cs.meta", ec);
}

void test_first_import(const fs::path& package, const fs::path& target, test_trash& trash, const source_ids& ids)
{
    const auto result = install(package, target, trash);
    check(result.is_done && result.written_keys.size() == 4, "a first import writes every file (" + result.error + ")");
    check(is_on_disk(target / "data/Kit/Button.pfb.meta") && is_on_disk(target / "data/Shared/wood.png"), "files land with their .meta");
    const auto database = read_database(target);
    check(database.count(ids.prefab) != 0 && database.count(ids.texture) != 0, "files keep their ids");
    package_registry registry;
    std::string err;
    check(load_registry(get_registry_path(target), registry, err) && registry.find(PACKAGES_TEST_NAME) != nullptr &&
              registry.find(PACKAGES_TEST_NAME)->files.size() == 4,
          "the registry records the package");
    package_archive archive;
    if(open_archive(package, archive))
    {
        const auto plan = plan_import(archive, make_project_view(target));
        check(plan.count(file_status::unchanged) == 4 && !plan.has_changes() && !plan.can_apply(),
              "importing it again changes nothing");
    }
}

void test_update(const fs::path& source_project, const fs::path& folder, const fs::path& target, test_trash& trash, const source_ids& ids)
{
    write_text(source_project / "data/Kit/Button.mat", make_material_text(ids, "blue"));
    write_text(source_project / "data/Kit/Button.mat.meta", make_meta(".mat", ids.material));
    const auto package = folder / "kit-1.1.0.unravelpkg";
    export_kit(source_project, package, "1.1.0", false);
    const std::string edited_prefab = make_prefab_text(ids, 1) + "\n";
    write_text(target / "data/Kit/Button.pfb", edited_prefab);
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    const auto view = make_project_view(target);
    const auto plan = plan_import(archive, view);
    const auto* material = find_planned(plan, "Kit/Button.mat");
    const auto* prefab = find_planned(plan, "Kit/Button.pfb");
    check(plan.is_update(), "an installed package is an update");
    check(material != nullptr && material->status == file_status::updated && material->action == file_action::write,
          "an unchanged file is updated");
    check(prefab != nullptr && prefab->status == file_status::edited && prefab->action == file_action::keep_mine,
          "an edited file is kept by default");
    check(plan.removals.size() == 1 && plan.removals.front().is_removed && plan.removals.front().uid == ids.script,
          "the dropped script is planned for the trash");
    const auto result = install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
    check(result.is_done, "the update applies (" + result.error + ")");
    check(read_text(target / "data/Kit/Button.mat").find("blue") != std::string::npos, "the updated file has the new content");
    check(read_text(target / "data/Kit/Button.pfb") == edited_prefab, "the edited file is kept");
    check(!is_on_disk(target / "data/Scripts/ButtonScript.cs") && result.trash_batch.size() > 0, "the dropped script went to the trash");
    package_registry registry;
    std::string err;
    load_registry(get_registry_path(target), registry, err);
    const auto* record = registry.find(PACKAGES_TEST_NAME);
    check(record != nullptr && record->version == "1.1.0" && record->files.size() == 3, "the registry holds the new version");
}

void test_clashes(const fs::path& package, const fs::path& folder, test_trash& trash, const source_ids& ids)
{
    // Another file of the project holds the material's id.
    const auto id_project = folder / "id-clash";
    write_asset(id_project, "Mine/Other.mat", "{\"material\": {\"mine\": true}}\n", ids.material);
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    const auto view = make_project_view(id_project);
    const auto plan = plan_import(archive, view);
    const auto* material = find_planned(plan, "Kit/Button.mat");
    check(material != nullptr && material->status == file_status::uid_clash && material->action == file_action::write_copy,
          "an id in use is added as a copy by default");
    const auto result = install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
    check(result.is_done, "the import with a copy applies (" + result.error + ")");
    const auto copy_uid = make_copy_uid(view.project_guid, PACKAGES_TEST_NAME, ids.material);
    const auto prefab_text = read_text(id_project / "data/Kit/Button.pfb");
    check(prefab_text.find(hpp::to_string(copy_uid)) != std::string::npos &&
              prefab_text.find(hpp::to_string(ids.material)) == std::string::npos,
          "the package's prefab points at the copy");
    check(read_text(id_project / "data/Kit/Button.mat.meta").find(hpp::to_string(copy_uid)) != std::string::npos,
          "the copy's .meta has the copy's id");
    check(read_text(id_project / "data/Mine/Other.mat").find("mine") != std::string::npos, "the project's file is untouched");
    // The record is gone and the package comes again: the made copy id is taken by the first copy.
    fs::error_code ec;
    fs::remove(get_registry_path(id_project), ec);
    const auto again_view = make_project_view(id_project);
    const auto again =
        install_package(archive, plan_import(archive, again_view), again_view, get_temp_folder() / "stage", trash.make_hooks());
    package_registry again_registry;
    std::string err;
    load_registry(get_registry_path(id_project), again_registry, err);
    const auto* again_record = again_registry.find(PACKAGES_TEST_NAME);
    const installed_file* again_material = nullptr;
    for(std::size_t index = 0; again_record != nullptr && index < again_record->files.size(); ++index)
    {
        if(again_record->files[index].source_uid == ids.material)
        {
            again_material = &again_record->files[index];
        }
    }
    check(again.is_done && again_material != nullptr && again_material->uid != copy_uid &&
              read_text(id_project / "data/Kit/Button (1).mat.meta").find(hpp::to_string(again_material->uid)) != std::string::npos,
          "a second copy gets an id of its own, and the record names the id it was written with (" + again.error + ")");
    // Another file of the project is where the prefab goes.
    const auto name_project = folder / "name-clash";
    write_asset(name_project, "Kit/Button.pfb", "{\"version\": 1, \"entities\": []}\n", generate_uuid("tests-someone-elses-prefab"));
    const auto name_plan = plan_import(archive, make_project_view(name_project));
    const auto* prefab = find_planned(name_plan, "Kit/Button.pfb");
    check(prefab != nullptr && prefab->status == file_status::path_clash && prefab->copy_key == "app:/data/Kit/Button (1).pfb",
          "a taken name gets a free one");
    // A script type the project defines in another file.
    const auto script_project = folder / "script-clash";
    write_asset(script_project, "Mine/MyButton.cs", "namespace Kit { public class ButtonScript {} }\n", generate_uuid("tests-my-script"));
    const auto script_view = make_project_view(script_project, {{PACKAGES_SCRIPT_TYPE, "app:/data/Mine/MyButton.cs"}});
    auto script_plan = plan_import(archive, script_view);
    check(!script_plan.clashes.empty() && !script_plan.can_apply(), "a script type defined twice is refused");
    set_script_action(script_plan, script_view, file_action::skip);
    check(script_plan.clashes.empty() && script_plan.can_apply(), "skipping the package's script clears the clash");
}

void test_script_clash_actions(const fs::path& package, const fs::path& folder, test_trash& trash, const source_ids& ids)
{
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    // The installed script, edited since.
    const auto edited_project = folder / "script-edited";
    install(package, edited_project, trash);
    write_text(edited_project / "data/Scripts/ButtonScript.cs", "namespace Kit { public class ButtonScript { int mine; } }\n");
    const auto edited_view = make_project_view(edited_project, get_script_types());
    auto edited_plan = plan_import(archive, edited_view);
    check(edited_plan.clashes.empty(), "keeping an edited script defines its type once");
    set_script_action(edited_plan, edited_view, file_action::write_copy);
    check(!edited_plan.clashes.empty() && !edited_plan.can_apply(), "adding the edited script's copy defines its type twice");
    set_script_action(edited_plan, edited_view, file_action::replace);
    check(edited_plan.clashes.empty(), "replacing the edited script defines its type once");
    // A project script with the package script's id and type, somewhere else.
    const auto id_project = folder / "script-id";
    write_asset(id_project, "Mine/MyButton.cs", "namespace Kit { public class ButtonScript { int other; } }\n", ids.script);
    const auto id_view = make_project_view(id_project, {{PACKAGES_SCRIPT_TYPE, "app:/data/Mine/MyButton.cs"}});
    auto id_plan = plan_import(archive, id_view);
    check(!id_plan.clashes.empty(), "the copy of a script whose id is in use defines its type a second time");
    set_script_action(id_plan, id_view, file_action::replace);
    check(id_plan.clashes.empty() && id_plan.can_apply(), "replacing the script whose id is in use clears the clash");
}

void test_no_free_name(const fs::path& package, const fs::path& folder)
{
    const auto project = folder / "no-free-name";
    write_asset(project, "Kit/Button.pfb", "{\"version\": 1, \"entities\": []}\n", generate_uuid("tests-crowded-prefab"));
    for(int number = 1; number < 1000; ++number)
    {
        write_text(project / "data/Kit" / ("Button (" + std::to_string(number) + ").pfb"), "{}\n");
    }
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    const auto plan = plan_import(archive, make_project_view(project));
    const auto* prefab = find_planned(plan, "Kit/Button.pfb");
    const bool is_warned = std::any_of(plan.warnings.begin(),
                                       plan.warnings.end(),
                                       [](const std::string& warning)
                                       {
                                           return warning.find("Kit/Button.pfb") != std::string::npos;
                                       });
    check(prefab != nullptr && prefab->action == file_action::skip && prefab->choices == std::vector<file_action>{file_action::skip} &&
              is_warned,
          "a file with no free name for its copy can only be skipped, with a warning");
}

void test_same_plan(const fs::path& package, const fs::path& folder)
{
    const auto project = folder / "same-plan";
    fs::error_code ec;
    fs::create_directories(project / "data", ec);
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    auto first = plan_import(archive, make_project_view(project));
    const auto second = plan_import(archive, make_project_view(project));
    check(is_same_plan(first, second), "an unchanged project plans the same");
    for(auto& planned : first.files)
    {
        planned.action = file_action::skip;
    }
    write_asset(project, "Kit/Button.mat", "{\"material\": {\"theirs\": true}}\n", generate_uuid("tests-late-material"));
    auto third = plan_import(archive, make_project_view(project));
    check(!is_same_plan(first, third), "a file created in between changes the plan");
    carry_actions(first, third);
    const auto* prefab = find_planned(third, "Kit/Button.pfb");
    const auto* material = find_planned(third, "Kit/Button.mat");
    check(prefab != nullptr && prefab->action == file_action::skip && material != nullptr &&
              material->status == file_status::path_clash && material->action == file_action::write_copy,
          "the actions picked carry over to the files planned alike only");
}

void test_registry_error(const fs::path& package, const fs::path& folder, test_trash& trash)
{
    package_archive archive;
    if(!open_archive(package, archive))
    {
        return;
    }
    for(const std::string& record : {std::string("<<<<<<< HEAD\n"), std::string("{}\n")})
    {
        const auto project = folder / "bad-record";
        fs::error_code ec;
        fs::create_directories(project / "data", ec);
        write_text(get_registry_path(project), record);
        const auto view = make_project_view(project);
        const auto plan = plan_import(archive, view);
        const auto result = install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
        removal_plan removal;
        removal.package.name = PACKAGES_TEST_NAME;
        const auto removed = remove_package(removal, view, trash.make_hooks());
        check(!view.registry_error.empty() && !plan.can_apply() && !result.is_done && !removed.is_done &&
                  read_text(get_registry_path(project)) == record && !is_on_disk(project / "data/Kit/Button.pfb"),
              "a project record that does not read stops the import and the removal and stays as it is");
    }
}

void test_unsafe_manifest(const fs::path& folder)
{
    package_manifest manifest;
    manifest.name = PACKAGES_TEST_NAME;
    manifest.version = "1.0.0";
    manifest.root = "../Outside";
    const auto file = folder / "kit-unsafe-root.unravelpkg";
    std::string err;
    write_package(file, manifest, {}, {}, err);
    package_archive archive;
    check(is_on_disk(file) && !archive.open(file, err), "a package whose folder climbs out of the data folder is refused");
}

void test_compatibility(const fs::path& source_project, const fs::path& folder, const source_ids& ids)
{
    write_text(source_project / "data/Kit/Button.pfb", make_prefab_text(ids, PACKAGES_FUTURE_DOCUMENT_VERSION));
    const auto future = folder / "kit-future.unravelpkg";
    export_kit(source_project, future, "2.0.0");
    write_text(source_project / "data/Kit/Button.pfb", make_prefab_text(ids, 1));
    package_archive archive;
    if(open_archive(future, archive))
    {
        const auto plan = plan_import(archive, make_project_view(folder / "fresh"));
        const auto* prefab = find_planned(plan, "Kit/Button.pfb");
        check(prefab != nullptr && prefab->status == file_status::incompatible && prefab->action == file_action::skip,
              "a prefab of a newer layout stays out");
    }
    auto plan = plan_export(make_export_source(source_project), "app:/data/Kit");
    auto definition = plan.definition;
    definition.name = PACKAGES_TEST_NAME;
    definition.version = "3.0.0";
    definition.minimum_engine = "999.0.0";
    const auto newer = folder / "kit-newer.unravelpkg";
    std::string err;
    export_package(plan, definition, newer, err);
    package_archive newer_archive;
    if(open_archive(newer, newer_archive))
    {
        const auto newer_plan = plan_import(newer_archive, make_project_view(folder / "fresh"));
        check(!newer_plan.problems.empty() && !newer_plan.can_apply(), "a package for a newer engine is refused");
    }
}

void test_damage(const fs::path& package, const fs::path& folder, test_trash& trash)
{
    auto bytes = read_text(package);
    const auto damaged = folder / "kit-damaged.unravelpkg";
    // The first entry's bytes: the table of contents stays whole, the entry no longer matches its hash.
    bytes[bytes.size() / 4] = char(bytes[bytes.size() / 4] ^ 0x5a);
    write_text(damaged, bytes);
    const auto project = folder / "damaged-target";
    fs::error_code ec;
    fs::create_directories(project / "data", ec);
    package_archive archive;
    std::string err;
    if(archive.open(damaged, err))
    {
        const auto view = make_project_view(project);
        const auto plan = plan_import(archive, view);
        const auto result = install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
        check(!result.is_done && !result.error.empty(), "a damaged package does not install");
    }
    check(read_database(project).empty() && !is_on_disk(get_registry_path(project)), "a damaged package writes nothing");
}

void test_failed_trash(const fs::path& source_project, const fs::path& folder, test_trash& trash, const source_ids& ids)
{
    const auto project = folder / "rollback";
    install(folder / "kit-1.0.0.unravelpkg", project, trash);
    const std::string edited = "{\"version\": 1, \"entities\": [], \"edited\": true}\n";
    write_text(project / "data/Kit/Button.pfb", edited);
    const auto material_before = read_text(project / "data/Kit/Button.mat");
    package_archive archive;
    if(!open_archive(folder / "kit-1.1.0.unravelpkg", archive))
    {
        return;
    }
    const auto view = make_project_view(project);
    auto plan = plan_import(archive, view);
    for(auto& planned : plan.files)
    {
        if(planned.status == file_status::edited)
        {
            planned.action = file_action::replace;
        }
    }
    // The trash refuses at once, then after the first file (the replaced prefab; the dropped script comes next).
    for(const int fails_after : {0, 1})
    {
        trash.fails_after = fails_after;
        const auto result = install_package(archive, plan, view, get_temp_folder() / "stage", trash.make_hooks());
        trash.fails_after = -1;
        const auto when = " (the trash refuses after " + std::to_string(fails_after) + " file(s))";
        check(!result.is_done && result.trash_batch.empty(), "an install whose trash fails stops" + when);
        check(read_text(project / "data/Kit/Button.pfb") == edited && read_text(project / "data/Kit/Button.mat") == material_before &&
                  is_on_disk(project / "data/Scripts/ButtonScript.cs") && is_on_disk(project / "data/Kit/Button.pfb.meta"),
              "the project is as it was" + when);
    }
    package_registry registry;
    std::string err;
    load_registry(get_registry_path(project), registry, err);
    check(registry.find(PACKAGES_TEST_NAME) != nullptr && registry.find(PACKAGES_TEST_NAME)->version == "1.0.0",
          "the registry is as it was");
}

void test_removal(const fs::path& target, test_trash& trash)
{
    const auto view = make_project_view(target);
    const auto plan = plan_removal(view, PACKAGES_TEST_NAME);
    std::size_t edited = 0;
    for(const auto& file : plan.files)
    {
        edited += file.is_edited ? 1 : 0;
    }
    check(plan.files.size() == 3 && edited == 1, "removal finds the files and the edited one");
    trash.fails_after = 1;
    const auto refused = remove_package(plan, view, trash.make_hooks());
    trash.fails_after = -1;
    check(!refused.is_done && is_on_disk(target / "data/Kit/Button.mat") && is_on_disk(target / "data/Shared/wood.png") &&
              is_on_disk(get_registry_path(target)),
          "a removal whose trash fails part way puts the files back and keeps the record");
    const auto result = remove_package(plan, view, trash.make_hooks());
    check(result.is_done && result.kept_count == 1 && result.removed_keys.size() == 2, "removal applies (" + result.error + ")");
    check(is_on_disk(target / "data/Kit/Button.pfb") && !is_on_disk(target / "data/Kit/Button.mat"),
          "the edited file stays and the others go");
    check(!is_on_disk(get_registry_path(target)), "the record is gone with the last package");
}

auto run_content_packages_suite(rtti::context&) -> int
{
    g_checks = 0;
    g_failures = 0;
    const auto folder = get_temp_folder();
    fs::error_code ec;
    fs::remove_all(folder, ec);
    fs::create_directories(folder, ec);
    const source_ids ids;
    const auto source_project = folder / "source";
    const auto target_project = folder / "target";
    make_source_project(source_project, ids);
    fs::create_directories(target_project / "data", ec);
    test_trash trash;
    trash.root = folder / "trash";
    test_versions();
    test_paths();
    test_export(source_project, folder, ids);
    test_script_lookup(source_project);
    test_first_import(folder / "kit-1.0.0.unravelpkg", target_project, trash, ids);
    test_update(source_project, folder, target_project, trash, ids);
    test_clashes(folder / "kit-1.0.0.unravelpkg", folder, trash, ids);
    test_compatibility(source_project, folder, ids);
    test_damage(folder / "kit-1.0.0.unravelpkg", folder, trash);
    test_failed_trash(source_project, folder, trash, ids);
    test_script_clash_actions(folder / "kit-1.0.0.unravelpkg", folder, trash, ids);
    test_no_free_name(folder / "kit-1.0.0.unravelpkg", folder);
    test_same_plan(folder / "kit-1.0.0.unravelpkg", folder);
    test_registry_error(folder / "kit-1.0.0.unravelpkg", folder, trash);
    test_unsafe_manifest(folder);
    test_removal(target_project, trash);
    std::printf("  content packages: %d checks, %d failures\n", g_checks, g_failures);
    fs::remove_all(folder, ec);
    return g_failures;
}

} // namespace

REGISTER_TEST_SUITE("content packages", run_content_packages_suite)
