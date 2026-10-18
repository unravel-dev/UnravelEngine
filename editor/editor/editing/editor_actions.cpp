#include "editor_actions.h"
#include "entity_inspect.h"
#include "engine/scripting/script.h"
#include "engine/ui/ui_tree.h"
#include "threadpp/thread.h"

#include <editor/assets/asset_watcher.h>
#include <editor/deploy/deploy_pipeline.h>
#include <editor/editing/prefab_bake.h>
#include <editor/editing/actions/entity_actions.h>
#include <editor/editing/animation_preview.h>
#include <editor/editing/create_scene_modal.h>
#include <editor/editing/editing_manager.h>
#include <editor/hub/hub.h>
#include <editor/hub/panels/animation_panel/animation_panel.h>
#include <editor/hub/panels/console_log_panel/console_log_panel.h>
#include <editor/hub/panels/inspector_panel/inspectors/inspectors.h>
#include <editor/imgui/integration/imgui_messagebox.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <editor/system/project_manager.h>
#include <editor/system/project_thumbnail.h>
#include <engine/assets/asset_dependency_graph.h>
#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/assets/impl/asset_writer.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/ecs/components/prefab_component.h>
#include <engine/ecs/components/transform_component.h>
#include <engine/ecs/prefab.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/play_mode.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/assets/asset_importer_meta.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/rendering/material.h>
#include <engine/rendering/mesh.h>
#include <engine/rendering/font.h>
#include <engine/animation/animation.h>
#include <engine/audio/audio_clip.h>
#include <engine/physics/physics_material.h>
#include <engine/animation/animation_controller.h>
#include <engine/ui/style_sheet.h>
#include <engine/scripting/ecs/systems/script_system.h>
#include <engine/rendering/ecs/systems/reflection_probe_system.h>
#include <engine/rendering/ecs/components/model_component.h>
#include <engine/rendering/ecs/components/reflection_probe_component.h>
#include <engine/rendering/renderer.h>
#include <engine/ecs/scene.h>
#include <filedialog/filedialog.h>
#include <filesystem/filesystem.h>
#include <filesystem/watcher.h>
#include <subprocess/subprocess.hpp>


#include <base/platform/config.hpp>
#include <hpp/string_view.hpp>
#include <string_utils/utils.h>
#include <array>
#include <memory>
#include <set>

namespace unravel
{

namespace
{

auto get_vscode_executable() -> fs::path
{
    fs::path executablePath;

#if UNRAVEL_PLATFORM_WINDOWS
    // Windows implementation
    try
    {
        // Common installation paths
        std::vector<fs::path> possiblePaths = {"C:\\Program Files\\Microsoft VS Code\\Code.exe",
                                               "C:\\Program Files (x86)\\Microsoft VS Code\\Code.exe",
                                               fs::path(std::getenv("LOCALAPPDATA")) / "Programs" /
                                                   "Microsoft VS Code" / "Code.exe"};

        for(const auto& path : possiblePaths)
        {
            if(fs::exists(path))
            {
                executablePath = path;
                break;
            }
        }

        if(executablePath.empty())
        {
            // Search for Code.exe in the PATH environment variable
            const char* pathEnv = std::getenv("PATH");
            if(pathEnv)
            {
                std::string pathEnvStr(pathEnv);
                std::stringstream ss(pathEnvStr);
                std::string token;
                while(std::getline(ss, token, ';'))
                {
                    fs::path codePath = fs::path(token) / "Code.exe";
                    if(fs::exists(codePath))
                    {
                        executablePath = codePath;
                        break;
                    }
                }
            }

            // If still not found, perform a recursive search in Program Files
            if(executablePath.empty())
            {
                std::vector<fs::path> directoriesToSearch = {"C:\\Program Files",
                                                             "C:\\Program Files (x86)",
                                                             fs::path(std::getenv("LOCALAPPDATA")) / "Programs"};

                for(const auto& dir : directoriesToSearch)
                {
                    try
                    {
                        for(const auto& entry : fs::recursive_directory_iterator(dir))
                        {
                            if(entry.is_regular_file() && entry.path().filename() == "Code.exe")
                            {
                                executablePath = entry.path();
                                break;
                            }
                        }
                        if(!executablePath.empty())
                        {
                            break;
                        }
                    }
                    catch(const fs::filesystem_error&)
                    {
                        continue;
                    }
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error finding VSCode executable path on Windows: " << e.what() << std::endl;
    }

#elif UNRAVEL_PLATFORM_OSX
    // macOS implementation
    try
    {
        // Common application bundle paths
        std::vector<fs::path> possibleAppPaths = {"/Applications/Visual Studio Code.app",
                                                  "/Applications/Visual Studio Code - Insiders.app",
                                                  fs::path(std::getenv("HOME")) / "Applications" /
                                                      "Visual Studio Code.app"};

        for(const auto& appPath : possibleAppPaths)
        {
            if(fs::exists(appPath))
            {
                // The executable is inside the app bundle
                fs::path codeExecutable = appPath / "Contents" / "MacOS" / "Electron";
                if(fs::exists(codeExecutable))
                {
                    executablePath = codeExecutable;
                    break;
                }
            }
        }

        if(executablePath.empty())
        {
            // Search for 'code' in /usr/local/bin or /usr/bin
            std::vector<fs::path> possibleLinks = {"/usr/local/bin/code", "/usr/bin/code"};
            for(const auto& linkPath : possibleLinks)
            {
                if(fs::exists(linkPath))
                {
                    // Resolve symlink
                    executablePath = fs::canonical(linkPath);
                    break;
                }
            }
        }

        if(executablePath.empty())
        {
            // Search in PATH environment variable
            const char* pathEnv = std::getenv("PATH");
            if(pathEnv)
            {
                std::string pathEnvStr(pathEnv);
                std::stringstream ss(pathEnvStr);
                std::string token;
                while(std::getline(ss, token, ':'))
                {
                    fs::path codePath = fs::path(token) / "code";
                    if(fs::exists(codePath))
                    {
                        executablePath = fs::canonical(codePath);
                        break;
                    }
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error finding VSCode executable path on macOS: " << e.what() << std::endl;
    }

#elif UNRAVEL_PLATFORM_LINUX
    // Linux implementation
    try
    {
        // Search for 'code' executable in PATH
        const char* pathEnv = std::getenv("PATH");
        if(pathEnv)
        {
            std::string pathEnvStr(pathEnv);
            std::stringstream ss(pathEnvStr);
            std::string token;
            while(std::getline(ss, token, ':'))
            {
                fs::path codePath = fs::path(token) / "code";
                if(fs::exists(codePath) && fs::is_regular_file(codePath))
                {
                    // Resolve symlink if necessary
                    executablePath = fs::canonical(codePath);
                    break;
                }
            }
        }

        if(executablePath.empty())
        {
            // Check common installation directories
            std::vector<fs::path> possiblePaths = {
                "/usr/bin/code",
                "/bin/code",
                "/sbin/code",
                "/usr/share/code/bin/code",
                "/usr/share/code-insiders/bin/code",
                "/usr/local/share/code/bin/code",
                "/opt/visual-studio-code/bin/code",
                "/var/lib/flatpak/app/com.visualstudio.code/current/active/files/bin/code",
                fs::path(std::getenv("HOME")) / ".vscode" / "bin" / "code"};

            for(const auto& path : possiblePaths)
            {
                if(fs::exists(path))
                {
                    executablePath = path;
                    break;
                }
            }
        }
    }
    catch(const std::exception& e)
    {
        std::cerr << "Error finding VSCode executable path on Linux: " << e.what() << std::endl;
    }

#else
#error "Unsupported operating system."
#endif

    return executablePath;
}

void remove_extensions(std::vector<std::vector<std::string>>& resourceExtensions,
                       const std::vector<std::string>& extsToRemove)
{
    // Convert extsToRemove to a set of lowercase strings
    std::unordered_set<std::string> extsToRemoveSet;
    for(const auto& ext : extsToRemove)
    {
        extsToRemoveSet.insert(string_utils::to_lower(ext));
    }

    for(auto outerIt = resourceExtensions.begin(); outerIt != resourceExtensions.end();)
    {
        std::vector<std::string>& innerVec = *outerIt;

        innerVec.erase(std::remove_if(innerVec.begin(),
                                      innerVec.end(),
                                      [&extsToRemoveSet](const std::string& ext)
                                      {
                                          return extsToRemoveSet.find(string_utils::to_lower(ext)) !=
                                                 extsToRemoveSet.end();
                                      }),
                       innerVec.end());

        if(innerVec.empty())
        {
            outerIt = resourceExtensions.erase(outerIt);
        }
        else
        {
            ++outerIt;
        }
    }
}
void generate_workspace_file(const std::string& file_path,
                             const std::vector<std::vector<std::string>>& exclude_extensions,
                             const editor_settings& settings)
{
    // Start constructing the JSON content
    std::ostringstream json_stream;

    json_stream << "{\n";
    json_stream << "    \"folders\": [\n";
    json_stream << "        {\n";
    json_stream << "            \"path\": \"..\"\n";
    json_stream << "        }\n";
    json_stream << "    ],\n";
    json_stream << "    \"settings\": {\n";
    json_stream << "        \"dotnet.preferCSharpExtension\": false,\n";
    json_stream << "        \"files.exclude\": {\n";
    json_stream << "            \"**/.git\": true,\n";
    json_stream << "            \"**/.svn\": true";

    // Add the exclude patterns from the provided extensions
    for(const auto& extensions : exclude_extensions)
    {
        for(const auto& ext : extensions)
        {
            // Escape any special characters in the extension if necessary

            // Create the pattern to exclude files with the given extension
            std::string pattern = "**/*" + ext;

            // Add a comma before each new entry
            json_stream << ",\n";
            json_stream << "            \"" << pattern << "\": true";
        }
    }

    // Close the files.exclude object and add files.associations
    json_stream << "\n";
    json_stream << "        },\n"; // End of "files.exclude"
    json_stream << "        \"files.associations\": {\n";
    json_stream << "            \"*.rcss\": \"css\",\n";
    json_stream << "            \"*.rhtml\": \"html\"\n";
    json_stream << "        }\n";
    json_stream << "    }\n";     // End of "settings"

    // Add the "extensions" section
    json_stream << ",\n";
    json_stream << "    \"extensions\": {\n";
    json_stream << "        \"recommendations\": [\n";
#if DOTNETPP_BACKEND_MONO
    json_stream << "             \"ms-vscode.mono-debug\",\n";
#endif
    json_stream << "             \"ms-dotnettools.csharp\"\n";
    json_stream << "        ]\n";
    json_stream << "    }\n";

    // Add the "launch" section
    json_stream << ",\n";
    json_stream << "    \"launch\": {\n";
    json_stream << "        \"version\": \"0.2.0\",\n";
    json_stream << "        \"configurations\": [\n";
#if DOTNETPP_BACKEND_MONO
    json_stream << "            {\n";
    json_stream << "                \"name\": \"Attach to Mono\",\n";
    json_stream << "                \"request\": \"attach\",\n";
    json_stream << "                \"type\": \"mono\",\n";
    json_stream << "                \"address\": \"" << settings.debugger.ip << "\",\n";
    json_stream << "                \"port\": " << settings.debugger.port << "\n";
    json_stream << "            }\n";
#else
    (void)settings;
    json_stream << "            {\n";
    json_stream << "                \"name\": \"Attach to " << EDITOR_NAME << "\",\n";
    json_stream << "                \"type\": \"coreclr\",\n";
    json_stream << "                \"request\": \"attach\",\n";
    json_stream << "                \"processName\": \"" << EDITOR_NAME << "\"\n";
    json_stream << "            },\n";
    json_stream << "            {\n";
    json_stream << "                \"name\": \".NET Core Attach\",\n";
    json_stream << "                \"type\": \"coreclr\",\n";
    json_stream << "                \"request\": \"attach\"\n";
    json_stream << "            }\n";
#endif
    json_stream << "        ]\n";
    json_stream << "    }\n";

    // Close the JSON object
    json_stream << "}";

    // Write the JSON string to a file
    std::ofstream file(file_path);
    if(file.is_open())
    {
        file << json_stream.str();
    }

    APPLOG_TRACE("Workspace {}", file_path);
}

namespace
{

auto collect_csharp_sources(const fs::path& source_directory, std::vector<fs::path>& out_sources) -> bool
{
    fs::error_code ec;
    const fs::recursive_directory_iterator end;
    fs::recursive_directory_iterator it(source_directory, ec);
    if(ec)
    {
        APPLOG_ERROR("Failed to iterate source directory {}: {}", source_directory.string(), ec.message());
        return false;
    }
    while(it != end)
    {
        const fs::path current_path = it->path();
        const bool is_regular = it->is_regular_file(ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to query {}: {}", current_path.string(), ec.message());
            return false;
        }
        if(is_regular && current_path.extension() == ".cs")
        {
            out_sources.push_back(current_path);
        }
        it.increment(ec);
        if(ec)
        {
            APPLOG_ERROR("Failed while iterating {}: {}", source_directory.string(), ec.message());
            return false;
        }
    }
    return true;
}

auto validate_csproj_inputs(const fs::path& source_directory,
                            const std::vector<fs::path>& external_dll_paths,
                            const fs::path& output_directory) -> bool
{
    fs::error_code ec;
    fs::create_directories(output_directory, ec);
    if(ec)
    {
        APPLOG_ERROR("Failed to create output directory {}: {}", output_directory.string(), ec.message());
        return false;
    }
    if(!fs::exists(source_directory, ec) || !fs::is_directory(source_directory, ec))
    {
        APPLOG_ERROR("Source directory does not exist or is not a directory: {}", source_directory.string());
        return false;
    }
    for(const auto& dll_path : external_dll_paths)
    {
        if(!fs::exists(dll_path, ec) || !fs::is_regular_file(dll_path, ec))
        {
            APPLOG_ERROR("External DLL does not exist or is not a file: {}", dll_path.string());
            return false;
        }
    }
    return true;
}

auto write_csproj_file(const fs::path& csproj_path, const std::string& csproj_content) -> bool
{
    std::ofstream csproj_file(csproj_path);
    if(!csproj_file.is_open())
    {
        APPLOG_ERROR("Failed to create .csproj file at {}", csproj_path.string());
        return false;
    }
    csproj_file << csproj_content;
    if(!csproj_file)
    {
        APPLOG_ERROR("Failed to write .csproj file at {}", csproj_path.string());
        return false;
    }
    APPLOG_TRACE("Generated {}", csproj_path.string());
    return true;
}

auto build_external_dll_references(const std::vector<fs::path>& external_dll_paths) -> std::string
{
    std::string external_dll_references;
    fs::error_code ec;
    for(const auto& dll_path : external_dll_paths)
    {
        const std::string dll_name = dll_path.filename().string();
        const fs::path dll_absolute_path = fs::absolute(dll_path, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to resolve absolute path for {}: {}", dll_path.string(), ec.message());
            return {};
        }
        external_dll_references += "    <Reference Include=\"" + dll_name + "\">\n";
        external_dll_references += "      <HintPath>" + dll_absolute_path.generic_string() + "</HintPath>\n";
        external_dll_references += "      <Private>False</Private>\n";
        external_dll_references += "    </Reference>\n";
    }
    return external_dll_references;
}

} // namespace

/**
 * @brief Generates a .csproj file based on the provided parameters.
 *
 * @return true on success; false on failure (errors are logged, never thrown).
 */
#if !DOTNETPP_BACKEND_MONO
auto generate_csproj(const fs::path& source_directory,
                     const std::vector<fs::path>& external_dll_paths,
                     const fs::path& output_directory,
                     const std::string& project_name = "MyLibrary",
                     std::string dotnet_sdk_version = {}) -> bool
{
    if(dotnet_sdk_version.empty())
    {
        dotnet_sdk_version = dotnet::get_dotnet_version();
    }
    if(!validate_csproj_inputs(source_directory, external_dll_paths, output_directory))
    {
        return false;
    }
    std::vector<fs::path> csharp_sources;
    if(!collect_csharp_sources(source_directory, csharp_sources))
    {
        return false;
    }
    fs::error_code ec;
    const fs::path csproj_directory = fs::absolute(output_directory, ec);
    if(ec)
    {
        APPLOG_ERROR("Failed to resolve output directory {}: {}", output_directory.string(), ec.message());
        return false;
    }
    const fs::path source_root = fs::absolute(source_directory, ec);
    if(ec)
    {
        APPLOG_ERROR("Failed to resolve source directory {}: {}", source_directory.string(), ec.message());
        return false;
    }
    std::string csharp_source_items;
    for(const auto& source_file : csharp_sources)
    {
        const fs::path absolute_source = fs::absolute(source_file, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to resolve source {}: {}", source_file.string(), ec.message());
            return false;
        }
        const fs::path include_path = fs::relative(absolute_source, csproj_directory, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to make relative include for {}: {}", absolute_source.string(), ec.message());
            return false;
        }
        const fs::path link_path = fs::relative(absolute_source, source_root, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to make relative link for {}: {}", absolute_source.string(), ec.message());
            return false;
        }
        csharp_source_items += "    <Compile Include=\"" + include_path.generic_string() + "\">\n";
        csharp_source_items += "      <Link>" + link_path.generic_string() + "</Link>\n";
        csharp_source_items += "    </Compile>\n";
    }
    const std::string external_dll_references = build_external_dll_references(external_dll_paths);
    if(external_dll_references.empty() && !external_dll_paths.empty())
    {
        return false;
    }
    // IDE tooling project; engine compiles scripts with csc. Keep outputs under temp/.
    std::string csproj_content;
    csproj_content += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
    csproj_content += "  <PropertyGroup>\n";
    csproj_content += "    <TargetFramework>net" + dotnet_sdk_version + "</TargetFramework>\n";
    csproj_content += "    <OutputType>Library</OutputType>\n";
    csproj_content += "    <AssemblyName>" + project_name + "</AssemblyName>\n";
    csproj_content += "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n";
    csproj_content += "    <ImplicitUsings>disable</ImplicitUsings>\n";
    csproj_content += "    <Nullable>disable</Nullable>\n";
    csproj_content += "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n";
    csproj_content += "    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>\n";
    csproj_content += "    <BaseOutputPath>temp/bin</BaseOutputPath>\n";
    csproj_content += "    <BaseIntermediateOutputPath>temp/obj</BaseIntermediateOutputPath>\n";
    csproj_content += "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
    csproj_content += "  </PropertyGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += csharp_source_items;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += external_dll_references;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "</Project>\n";
    const fs::path csproj_path = output_directory / (project_name + ".csproj");
    return write_csproj_file(csproj_path, csproj_content);
}
#else
auto generate_csproj_legacy(const fs::path& source_directory,
                            const std::vector<fs::path>& external_dll_paths,
                            const fs::path& output_directory,
                            const std::string& project_name = "MyLibrary",
                            const std::string& dotnet_framework_version = "v4.7.1") -> bool
{
    const auto uid = generate_uuid(project_name);
    const fs::path output_path = fs::path("temp") / "bin" / "Debug";
    const fs::path intermediate_output_path = fs::path("temp") / "obj" / "Debug";
    if(!validate_csproj_inputs(source_directory, external_dll_paths, output_directory))
    {
        return false;
    }
    std::vector<fs::path> csharp_sources;
    if(!collect_csharp_sources(source_directory, csharp_sources))
    {
        return false;
    }
    fs::error_code ec;
    const fs::path csproj_directory = fs::absolute(output_directory, ec);
    if(ec)
    {
        APPLOG_ERROR("Failed to resolve output directory {}: {}", output_directory.string(), ec.message());
        return false;
    }
    std::string csharp_source_items;
    for(const auto& source_file : csharp_sources)
    {
        const fs::path absolute_source = fs::absolute(source_file, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to resolve source {}: {}", source_file.string(), ec.message());
            return false;
        }
        const fs::path include_path = fs::relative(absolute_source, csproj_directory, ec);
        if(ec)
        {
            APPLOG_ERROR("Failed to make relative include for {}: {}", absolute_source.string(), ec.message());
            return false;
        }
        csharp_source_items += "    <Compile Include=\"" + include_path.generic_string() + "\" />\n";
    }
    const std::string external_dll_references = build_external_dll_references(external_dll_paths);
    if(external_dll_references.empty() && !external_dll_paths.empty())
    {
        return false;
    }
    std::string csproj_content;
    csproj_content += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    csproj_content += "<Project ToolsVersion=\"4.0\" DefaultTargets=\"Build\" "
                      "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
    csproj_content += "  <PropertyGroup>\n";
    csproj_content += "    <LangVersion>9.0</LangVersion>\n";
    csproj_content += "  </PropertyGroup>\n";
    csproj_content += "  <PropertyGroup>\n";
    csproj_content += "    <Configuration Condition=\" '$(Configuration)' == '' \">Debug</Configuration>\n";
    csproj_content += "    <Platform Condition=\" '$(Platform)' == '' \">AnyCPU</Platform>\n";
    csproj_content += "    <ProductVersion>10.0.20506</ProductVersion>\n";
    csproj_content += "    <SchemaVersion>2.0</SchemaVersion>\n";
    csproj_content += "    <RootNamespace></RootNamespace>\n";
    csproj_content += "    <ProjectGuid>{" + hpp::to_string_upper(uid) + "}</ProjectGuid>\n";
    csproj_content += "    <OutputType>Library</OutputType>\n";
    csproj_content += "    <AppDesignerFolder>Properties</AppDesignerFolder>\n";
    csproj_content += "    <AssemblyName>" + project_name + "</AssemblyName>\n";
    csproj_content += "    <TargetFrameworkVersion>" + dotnet_framework_version + "</TargetFrameworkVersion>\n";
    csproj_content += "    <FileAlignment>512</FileAlignment>\n";
    csproj_content += "    <BaseDirectory>.</BaseDirectory>\n";
    csproj_content += "    <OutputPath>" + output_path.generic_string() + "</OutputPath>\n";
    csproj_content +=
        "    <IntermediateOutputPath>" + intermediate_output_path.generic_string() + "</IntermediateOutputPath>\n";
    csproj_content += "  </PropertyGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += csharp_source_items;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += external_dll_references;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "  <Import Project=\"$(MSBuildToolsPath)\\Microsoft.CSharp.targets\" />\n";
    csproj_content += "  <Target Name=\"GenerateTargetFrameworkMonikerAttribute\" />\n";
    csproj_content += "</Project>\n";
    const fs::path csproj_path = output_directory / (project_name + ".csproj");
    return write_csproj_file(csproj_path, csproj_content);
}
#endif

auto save_scene_impl(rtti::context& ctx, const fs::path& path) -> bool
{
    return editor_actions::save_scene_to_path(ctx, path, false, true);
}

auto add_extension_if_missing(const std::string& p) -> fs::path
{
    fs::path def_path = p;
    if(!ex::is_format<scene_prefab>(def_path.extension().generic_string()))
    {
        def_path.replace_extension(ex::get_format<scene_prefab>(false));
    }

    return def_path;
}

auto save_scene_as_impl(rtti::context& ctx, fs::path& path, const std::string& default_name = {}) -> bool
{
    auto& ev = ctx.get_cached<events>();
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return false;
    }

    auto& em = ctx.get_cached<editing_manager>();
    if(em.is_prefab_mode())
    {
        em.save_prefab_changes(ctx);
        return true;
    }

    auto save_path = fs::resolve_protocol("app:/data/").string();

    if(!default_name.empty())
    {
        auto def_path = add_extension_if_missing(default_name);

        save_path += def_path.string();
    }

    std::string picked;
    if(native::save_file_dialog(picked,
                                ex::get_suported_formats_with_wildcard<scene_prefab>(),
                                "Scene files",
                                "Save scene as",
                                save_path))
    {
        auto& em = ctx.get_cached<editing_manager>();

        path = add_extension_if_missing(picked);

        return save_scene_impl(ctx, path);
    }

    return false;
}


} // namespace

auto editor_actions::new_scene(rtti::context& ctx) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return false;
    }
    prompt_save_scene(ctx,
                      [&ctx]()
                      {
                          create_scene_modal::show(
                              [&ctx](defaults::scene_preset preset)
                              {
                                  editor_actions::new_scene_from_preset(ctx, preset);
                              });
                      });

    return true;
}

auto editor_actions::load_scene_from_asset(rtti::context& ctx,
                                           const asset_handle<scene_prefab>& asset,
                                           std::string* error) -> bool
{
    auto& em = ctx.get_cached<editing_manager>();
    em.clear();

    auto& ec = ctx.get_cached<ecs>();
    ec.unload_scene();

    auto& scene = ec.get_scene();
    if(!scene.load_from(asset))
    {
        if(error)
        {
            *error = "Failed to load scene: " + asset.id();
        }
        return false;
    }
    // A loaded scene answers a pending request for a new one.
    create_scene_modal::cancel_if_pending();

    em.sync_prefab_instances(ctx, &scene);
    em.clear_unsaved_changes();

    if(ctx.has<project_manager>())
    {
        auto& pm = ctx.get_cached<project_manager>();
        pm.get_project_editor_settings().scene.opened_scene = asset;
        pm.save_project_editor_settings();
    }
    return true;
}

auto editor_actions::new_scene_from_preset(rtti::context& ctx, defaults::scene_preset preset) -> bool
{
    create_scene_modal::cancel_if_pending();

    auto& em = ctx.get_cached<editing_manager>();
    em.clear();

    auto& ec = ctx.get_cached<ecs>();
    ec.unload_scene();

    defaults::create_scene_from_preset(ctx, ec.get_scene(), preset);
    em.clear_unsaved_changes();

    if(ctx.has<project_manager>())
    {
        auto& pm = ctx.get_cached<project_manager>();
        pm.get_project_editor_settings().scene.opened_scene = {};
        pm.save_project_editor_settings();
    }
    return true;
}

auto editor_actions::save_scene_to_path(rtti::context& ctx,
                                        const fs::path& path,
                                        bool update_source,
                                        bool show_notification) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return false;
    }

    fs::path absolute = path;
    if(fs::has_known_protocol(path))
    {
        absolute = fs::resolve_protocol(path);
    }
    absolute = fs::absolute(absolute);

    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    animation_entity_preview::stop_all();
    if(!asset_writer::atomic_save_to_file(absolute.string(), scene))
    {
        return false;
    }

    auto& em = ctx.get_cached<editing_manager>();
    em.clear_unsaved_changes();

    if(ctx.has<project_thumbnail>())
    {
        ctx.get_cached<project_thumbnail>().request_capture();
    }

    if(show_notification)
    {
        ImGui::PushNotification(ImGuiToast(ImGuiToastType_Success, 1000, "Scene saved."));
    }

    if(update_source)
    {
        auto& am = ctx.get_cached<asset_manager>();
        const auto protocol_key = fs::convert_to_protocol(absolute).generic_string();
        scene.source = am.get_asset<scene_prefab>(protocol_key);

        if(ctx.has<project_manager>())
        {
            auto& pm = ctx.get_cached<project_manager>();
            pm.get_project_editor_settings().scene.opened_scene = scene.source;
            pm.save_project_editor_settings();
        }
    }

    return true;
}
auto editor_actions::open_scene(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        play.set_active(ctx, false);
    }

    std::string picked;
    if(native::open_file_dialog(picked,
                                ex::get_suported_formats_with_wildcard<scene_prefab>(),
                                "Scene files",
                                "Open scene",
                                fs::resolve_protocol("app:/data/").string()))
    {
        auto path = fs::convert_to_protocol(picked);
        if(ex::is_format<scene_prefab>(path.extension().generic_string()))
        {
            auto& am = ctx.get_cached<asset_manager>();
            auto asset = am.get_asset<scene_prefab>(path.string());

            return open_scene_from_asset(ctx, asset);
        }
    }
    return false;
}

void editor_actions::open_animation_controller(rtti::context& ctx, const asset_handle<animation_controller>& asset)
{
    ctx.get_cached<hub>().get_panels().get_animation_panel().open(asset);
}

auto editor_actions::open_scene_from_asset(rtti::context& ctx, const asset_handle<scene_prefab>& asset) -> bool
{
    return prompt_save_scene(ctx,
                             [&ctx, asset]()
                             {
                                 std::string error;
                                 if(!editor_actions::load_scene_from_asset(ctx, asset, &error))
                                 {
                                     editor_actions::new_scene(ctx);
                                 }
                             });
}
auto editor_actions::save_scene(rtti::context& ctx) -> bool
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    auto& em = ctx.get_cached<editing_manager>();

    if(em.is_prefab_mode())
    {
        em.save_prefab_changes(ctx);
        return true;
    }

    if(!scene.source)
    {
        fs::path picked;
        if(save_scene_as_impl(ctx, picked, "Scene3D"))
        {
            auto path = fs::convert_to_protocol(picked);

            auto& am = ctx.get_cached<asset_manager>();
            scene.source = am.get_asset<scene_prefab>(path.string());
            return true;
        }
    }
    else
    {
        auto path = fs::resolve_protocol(scene.source.id());
        return save_scene_impl(ctx, path);
    }

    return false;
}
auto editor_actions::save_scene_as(rtti::context& ctx) -> bool
{
    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();

    fs::path p;
    return save_scene_as_impl(ctx, p, scene.source.name());
}

auto editor_actions::prompt_save_scene(rtti::context& ctx, const std::function<void()>& on_continue) -> bool
{
    auto& ev = ctx.get_cached<events>();
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        on_continue();
        return false;
    }

    auto& em = ctx.get_cached<editing_manager>();
    if(!em.has_unsaved_changes())
    {
        on_continue();
        return true;
    }

    ImBox::ShowSaveConfirmation("Save scene?",
                                "Do you want to save the changes you made?",
                                [&ctx, on_continue](ImBox::ModalResult result)
                                {
                                    if(result == ImBox::ModalResult::Save)
                                    {
                                        save_scene(ctx);
                                    }

                                    if(result != ImBox::ModalResult::Cancel)
                                    {
                                        on_continue();
                                    }
                                });

    return true;
}

namespace
{
/// A running deploy reads the project as it runs: it must not close or change under it.
auto is_project_busy_deploying() -> bool
{
    if(!deployment::is_deploy_running())
    {
        return false;
    }
    APPLOG_WARNING("A deploy is running; the project stays open until it finishes.");
    return true;
}
} // namespace

auto editor_actions::close_project(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active() || is_project_busy_deploying())
    {
        return false;
    }

    prompt_save_scene(ctx, [&ctx]() {
        auto& pm = ctx.get_cached<project_manager>();
        pm.close_project(ctx);
    });

    return true;
}

auto editor_actions::reload_project(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active() || is_project_busy_deploying())
    {
        return false;
    }
    auto& pm = ctx.get_cached<project_manager>();
    if(!pm.has_open_project())
    {
        return false;
    }
    auto project_path = fs::resolve_protocol("app:/");

    pm.close_project(ctx);

    auto& em = ctx.get_cached<editing_manager>();
    em.queue_action<untracked_editor_state_action_t>("Reload Project", [&ctx, &pm, project_path]()
    {
        pm.open_project(ctx, project_path);
    });
    return true;
}

auto editor_actions::restart_editor(rtti::context& ctx) -> bool
{
    (void)ctx;
    APPLOG_INFO("Editor restart requested from menu (reason=user_menu)");
    return engine::request_restart();
}

void editor_actions::generate_script_workspace()
{
    auto& ctx = engine::context();
    auto& pm = ctx.get_cached<project_manager>();
    auto project_name = pm.get_name();
    const auto& editor_settings = pm.get_editor_settings();

    fs::error_code err;

    auto workspace_folder = fs::resolve_protocol("app:/.vscode");
    fs::create_directories(workspace_folder, err);

    auto formats = ex::get_all_formats();
    formats.emplace_back(std::vector<std::string>{".meta"});
    formats.emplace_back(std::vector<std::string>{".asset"});
    formats.emplace_back(std::vector<std::string>{".manifest"});
    formats.emplace_back(std::vector<std::string>{".temp"});

    remove_extensions(formats, ex::get_suported_formats<gfx::shader>());
    remove_extensions(formats, ex::get_suported_formats<script>());
    remove_extensions(formats, ex::get_suported_formats<ui_tree>());
    remove_extensions(formats, ex::get_suported_formats<style_sheet>());

    auto workspace_file = workspace_folder / fmt::format("{}-workspace.code-workspace", project_name);
    generate_workspace_file(workspace_file.string(), formats, editor_settings);

    auto source_path = fs::resolve_protocol("app:/data");

    auto engine_dep = fs::resolve_protocol(script_system::get_lib_compiled_key("engine"));

    auto output_path = fs::resolve_protocol("app:/");

#if DOTNETPP_BACKEND_MONO
    (void)generate_csproj_legacy(source_path, {engine_dep}, output_path, project_name);
#else
    (void)generate_csproj(source_path, {engine_dep}, output_path, project_name);
#endif
}

void editor_actions::open_workspace_on_file(const fs::path& file, int line)
{
    auto& ctx = engine::context();
    auto& pm = ctx.get_cached<project_manager>();
    auto project_name = pm.get_name();
    auto vscode_exe = pm.get_editor_settings().external_tools.vscode_executable;
    tpp::async(
        [vscode_exe, project_name, file, line]()
        {
            auto external_tool = vscode_exe;
            if(external_tool.empty())
            {
                external_tool = get_vscode_executable();
            }

            static const char* tool = "[Visual Studio Code]";
            static const char* setup_hint = "Edit -> Editor Settings -> External Tools";

            if(external_tool.empty())
            {
                APPLOG_ERROR("Cannot locate external tool {}", tool);
                APPLOG_ERROR("To configure {} visit : {}", tool, setup_hint);
                return;
            }
            auto workspace_key = fmt::format("app:/.vscode/{}-workspace.code-workspace", project_name);
            auto workspace_path = fs::resolve_protocol(workspace_key);

            auto result = subprocess::call(external_tool.string(),
                                           {workspace_path.string(), "-g", fmt::format("{}:{}", file.string(), line)});

            if(result.retcode != 0)
            {
                APPLOG_ERROR("Cannot open external tool {} for file {}", tool, external_tool.string(), file.string());
                APPLOG_ERROR("To configure {} visit : {}", tool, setup_hint);
            }
        });
}

void editor_actions::recompile_shaders(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto shaders = am.get_assets<gfx::shader>(group);
    fs::watcher::with_paused(
        [&]
        {
            for(auto& asset : shaders)
            {
                fs::error_code ec;
                auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                fs::watcher::touch(path, false);
            }
        });
}

auto editor_actions::save_import_settings(const std::string& key, const asset_meta& meta) -> bool
{
    return asset_writer::atomic_save_to_file(asset_writer::resolve_meta_file_for_key(key).string(), meta);
}

auto editor_actions::save_entity_as_prefab(rtti::context& ctx,
                                           entt::handle entity,
                                           const fs::path& prefab_path,
                                           bool attach) -> asset_handle<prefab>
{
    animation_entity_preview::stop_all();
    if(!asset_writer::atomic_save_to_file(prefab_path.string(), entity))
    {
        return {};
    }
    auto& am = ctx.get_cached<asset_manager>();
    auto prefab_asset = am.get_asset<prefab>(fs::convert_to_protocol(prefab_path).generic_string());
    if(!attach)
    {
        return prefab_asset;
    }
    // The entity may already be an instance of a different prefab. Its old bookkeeping is keyed by
    // uids that also exist in the file just written (prefab uids survive the save), so keeping it
    // would suppress those properties from the new asset on every resync - and a stale instance_id
    // would claim a slot in a document that never wrote this instance. Becoming the source of a new
    // prefab leaves nothing overridden by definition.
    //
    // Reset field by field, not replaced: emplace_or_replace on an existing component takes entt's
    // patch path, which assigns a default-constructed component over the live one - owner handle
    // included - and fires only on_update, so the owner never gets re-stamped. That null owner
    // surfaced as a crash in the inspector's Apply All. And not removed-and-re-added either: the
    // on_destroy hook strips prefab ids from the whole subtree.
    // A nested instance saved as its own prefab is no longer its container's slot: the container's
    // document would put the old instance back there on its next replay. The slot is stated removed
    // on the container, and the new instance is the user's.
    if(const auto* old_prefab = entity.try_get<prefab_component>();
       old_prefab != nullptr && !old_prefab->instance_id.is_nil())
    {
        const auto* trans = entity.try_get<transform_component>();
        auto container =
            trans != nullptr ? prefab_override_context::find_prefab_root_entity(trans->get_parent()) : entt::handle{};
        if(auto* container_prefab = container ? container.try_get<prefab_component>() : nullptr)
        {
            container_prefab->remove_instance(old_prefab->instance_id);
            container_prefab->changed = true;
        }
    }
    auto& prefab_comp = entity.get_or_emplace<prefab_component>();
    prefab_comp.clear_overrides();
    prefab_comp.instance_id = {};
    prefab_comp.instance_document = {};
    prefab_comp.source = prefab_asset;
    return prefab_asset;
}

void editor_actions::recompile_textures(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto textures = am.get_assets<gfx::texture>(group);
    fs::watcher::with_paused(
        [&]
        {
            for(auto& asset : textures)
            {
                fs::error_code ec;
                auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                fs::watcher::touch(path, false);
            }
        });
}

auto editor_actions::migrate_prefabs(rtti::context& ctx, size_t* total_count) -> size_t
{
    if(total_count != nullptr)
    {
        *total_count = 0;
    }
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        APPLOG_ERROR("Migrate: refusing to run during play mode.");
        return 0;
    }
    auto& em = ctx.get_cached<editing_manager>();
    if(em.is_prefab_mode())
    {
        APPLOG_ERROR("Migrate: leave prefab mode first.");
        return 0;
    }
    const auto report = validate_prefab_graph(ctx);
    if(!report.is_valid())
    {
        APPLOG_ERROR("Migrate: aborted, the prefab graph contains a cycle. Nothing was written.");
        return 0;
    }

    // Nested prefabs before the prefabs that contain them, scenes last - the same order the
    // bake uses, and for the same reason: a container re-saved after the assets it nests finds
    // their slots named by them, and names only its own additions.
    auto& am = ctx.get_cached<asset_manager>();
    const auto order = asset_deps::compute_build_order(asset_deps::collect_prefab_asset_uids(am),
                                                       asset_deps::make_prefab_dependency_resolver(am));
    scene scratch("prefab_migrate");
    size_t written = 0;
    for(const auto& uid : order.ordered)
    {
        if(bake_prefab_asset(ctx, scratch, uid, {}, true))
        {
            ++written;
        }
    }
    scratch.unload();
    if(total_count != nullptr)
    {
        *total_count = order.ordered.size();
    }
    APPLOG_INFO("Migrate: re-saved {} of {} prefab(s) and scene(s).", written, order.ordered.size());
    return written;
}

auto editor_actions::migrate_texture_color_spaces(const std::string& group) -> size_t
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();

    auto tag = [&](const asset_handle<gfx::texture>& tex, texture_importer_meta::color_space colorspace) -> bool
    {
        if(!tex)
        {
            return false;
        }
        const auto meta_path = asset_writer::resolve_meta_file(tex);
        asset_meta meta;
        if(!load_from_file(meta_path.string(), meta))
        {
            // No meta yet (texture not scanned): the watcher will create a default
            // one; re-running the migration afterwards will pick it up.
            return false;
        }
        auto importer = std::dynamic_pointer_cast<texture_importer_meta>(meta.importer);
        if(!importer)
        {
            return false;
        }
        if(importer->colorspace != texture_importer_meta::color_space::automatic)
        {
            // Explicit user choice wins over the migration.
            return false;
        }
        importer->colorspace = colorspace;
        fs::error_code err;
        asset_writer::atomic_write_file(meta_path,
                                        [&](const fs::path& temp)
                                        {
                                            save_to_file(temp.string(), meta);
                                        },
                                        err);
        if(err)
        {
            APPLOG_WARNING("Color space migration: failed to write meta for {}: {}", tex.id(), err.message());
            return false;
        }
        // Touch the source so the watcher recompiles it into a tagged container.
        fs::error_code ec;
        auto source = fs::absolute(fs::resolve_protocol(tex.id()).string(), ec);
        if(!ec)
        {
            fs::watcher::touch(source, false);
        }
        return true;
    };

    size_t tagged = 0;
    auto materials = am.get_assets<material>(group);
    for(const auto& asset : materials)
    {
        auto mat = asset.get();
        if(!mat || !mat->is<pbr_material>())
        {
            continue;
        }
        const auto& pbr = static_cast<const pbr_material&>(*mat);
        tagged += size_t(tag(pbr.get_color_map(), texture_importer_meta::color_space::srgb));
        tagged += size_t(tag(pbr.get_emissive_map(), texture_importer_meta::color_space::srgb));
        tagged += size_t(tag(pbr.get_normal_map(), texture_importer_meta::color_space::linear));
        tagged += size_t(tag(pbr.get_roughness_map(), texture_importer_meta::color_space::linear));
        tagged += size_t(tag(pbr.get_metalness_map(), texture_importer_meta::color_space::linear));
        tagged += size_t(tag(pbr.get_ao_map(), texture_importer_meta::color_space::linear));
    }
    APPLOG_INFO("Color space migration: tagged {} texture(s) from {} material(s).", tagged, materials.size());
    return tagged;
}

void editor_actions::recompile_meshes(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto meshes = am.get_assets<mesh>(group);
    fs::watcher::with_paused(
        [&]
        {
            for(auto& asset : meshes)
            {
                fs::error_code ec;
                auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                fs::watcher::touch(path, false);
            }
        });
}

void editor_actions::recompile_ui(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    fs::watcher::with_paused(
        [&]
        {
            {
                auto assets = am.get_assets<ui_tree>(group);
                for(auto& asset : assets)
                {
                    fs::error_code ec;
                    auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                    fs::watcher::touch(path, false);
                }
            }
            {
                auto assets = am.get_assets<style_sheet>(group);
                for(auto& asset : assets)
                {
                    fs::error_code ec;
                    auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                    fs::watcher::touch(path, false);
                }
            }
        });
}
void editor_actions::recompile_scripts(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto scripts = am.get_assets<script>(group);
    fs::watcher::with_paused(
        [&]
        {
            for(auto& asset : scripts)
            {
                fs::error_code ec;
                auto path = fs::absolute(fs::resolve_protocol(asset.id()).string(), ec);
                fs::watcher::touch(path, false);
            }
        });
}
void editor_actions::recompile_all(const std::string& group)
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto assets = am.get_all_assets(group);
    fs::watcher::with_paused(
        [&]
        {
            for(auto& asset : assets)
            {
                fs::error_code ec;
                auto path = fs::absolute(fs::resolve_protocol(asset).string(), ec);
                fs::watcher::touch(path, false);
            }
        });
}

auto editor_actions::rebuild_reflection_probes(rtti::context& /*ctx*/, bool force_full_first_frame) -> size_t
{
    size_t count = 0;
    for(auto* scn : scene::get_all_scenes())
    {
        if(!scn || !scn->registry)
        {
            continue;
        }

        count += reflection_probe_system::mark_all_dirty(*scn, force_full_first_frame);
    }
    return count;
}

auto editor_actions::can_enter_play(rtti::context& ctx, std::string* error) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(play.is_active())
    {
        return true;
    }
    auto& scripting = ctx.get_cached<script_system>();
    if(scripting.has_compilation_errors())
    {
        if(error)
        {
            *error = "All compiler errors must be fixed before you can enter Play Mode!";
        }
        return false;
    }
    return true;
}

auto editor_actions::get_play_state(rtti::context& ctx) -> play_state_info
{
    play_state_info info;
    if(!ctx.has<play_mode>())
    {
        return info;
    }
    auto& play = ctx.get_cached<play_mode>();
    info.is_active = play.is_active();
    info.is_paused = play.is_paused();
    info.is_splash = play.is_splash();
    info.is_simulation_running = play.is_simulation_running();
    info.frames_running = play.frames_running();
    info.frames_simulated = play.frames_simulated();
    if(play.is_splash())
    {
        info.phase = "splash";
    }
    else if(play.is_simulation_running())
    {
        info.phase = "running";
    }
    else if(play.is_active())
    {
        info.phase = "active";
    }
    else
    {
        info.phase = "inactive";
    }
    return info;
}

auto editor_actions::set_play_active(rtti::context& ctx, bool active, bool allow_splash, std::string* error) -> bool
{
    if(active && !can_enter_play(ctx, error))
    {
        return false;
    }
    ctx.get_cached<play_mode>().set_active(ctx, active, allow_splash);
    return true;
}

auto editor_actions::toggle_play(rtti::context& ctx, bool allow_splash, std::string* error) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(!play.is_active() && !can_enter_play(ctx, error))
    {
        return false;
    }
    play.toggle(ctx, allow_splash);
    return true;
}

auto editor_actions::set_play_paused(rtti::context& ctx, bool paused, std::string* error) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(!play.is_active())
    {
        if(error)
        {
            *error = "Play mode is not active";
        }
        return false;
    }
    play.set_paused(ctx, paused);
    return true;
}

auto editor_actions::skip_play_frame(rtti::context& ctx, std::string* error) -> bool
{
    auto& play = ctx.get_cached<play_mode>();
    if(!play.is_active())
    {
        if(error)
        {
            *error = "Play mode is not active";
        }
        return false;
    }
    if(!play.is_paused())
    {
        if(error)
        {
            *error = "Play mode must be paused to skip a frame";
        }
        return false;
    }
    play.skip_next_frame();
    return true;
}

auto editor_actions::get_selection(rtti::context& ctx) -> selection_info
{
    selection_info info;
    auto& em = ctx.get_cached<editing_manager>();
    if(auto* active = em.try_get_active_selection_as<entt::handle>())
    {
        info.active_entity_id = entity_id_string(*active);
    }
    for(const auto& handle : em.try_get_selections_as_copy<entt::handle>())
    {
        if(handle)
        {
            info.entity_ids.push_back(entity_id_string(handle));
        }
    }
    return info;
}

auto editor_actions::set_selection(rtti::context& ctx,
                                   const std::vector<std::string>& entity_ids,
                                   bool add,
                                   std::string* error) -> bool
{
    auto& em = ctx.get_cached<editing_manager>();
    auto* scn = em.get_active_scene(ctx);
    if(!scn || !scn->registry)
    {
        if(error)
        {
            *error = "No active scene";
        }
        return false;
    }
    // Every id is resolved before the selection changes, so a bad one leaves it as it was.
    std::vector<entt::handle> entities;
    for(const auto& id : entity_ids)
    {
        auto entity = find_entity_by_id(*scn, id);
        if(!entity)
        {
            if(error)
            {
                *error = "Entity not found: " + id;
            }
            return false;
        }
        entities.push_back(entity);
    }
    if(!add)
    {
        em.unselect();
    }
    for(size_t i = 0; i < entities.size(); ++i)
    {
        const auto mode = (!add && i == 0) ? editing_manager::select_mode::normal : editing_manager::select_mode::ctrl;
        em.select(entities[i], mode);
    }
    return true;
}

void editor_actions::clear_selection(rtti::context& ctx)
{
    ctx.get_cached<editing_manager>().unselect();
}

auto editor_actions::select_asset(rtti::context& ctx, const std::string& key, std::string* error) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    const std::string extension = fs::path(key).extension().string();
    const std::string name = fs::path(key).stem().string();
    bool is_selected = false;
    // The asset types the content browser selects.
    hpp::for_each_type<gfx::texture,
                       gfx::shader,
                       scene_prefab,
                       material,
                       physics_material,
                       ui_tree,
                       style_sheet,
                       audio_clip,
                       mesh,
                       prefab,
                       animation_clip,
                       animation_controller,
                       font,
                       script>(
        [&](auto tag)
        {
            using asset_t = typename std::decay_t<decltype(tag)>::type;
            if(is_selected || !ex::is_format<asset_t>(extension))
            {
                return;
            }
            const auto handle = am.find_asset<asset_t>(key);
            if(!handle)
            {
                return;
            }
            em.select(handle, editing_manager::select_mode::normal, name);
            is_selected = true;
        });
    if(!is_selected && error)
    {
        *error = "No asset at " + key;
    }
    return is_selected;
}

namespace
{
/// Focuses the asset at @p key as its locate button does; false for a folder or a file of no asset type.
auto focus_known_asset(rtti::context& ctx, const std::string& key) -> bool
{
    auto& am = ctx.get_cached<asset_manager>();
    auto& em = ctx.get_cached<editing_manager>();
    const std::string extension = fs::path(key).extension().string();
    bool is_focused = false;
    // The asset types the Content Browser shows as assets.
    hpp::for_each_type<gfx::texture,
                       gfx::shader,
                       scene_prefab,
                       material,
                       physics_material,
                       ui_tree,
                       style_sheet,
                       audio_clip,
                       mesh,
                       prefab,
                       animation_clip,
                       animation_controller,
                       font,
                       script>(
        [&](auto tag)
        {
            using asset_t = typename std::decay_t<decltype(tag)>::type;
            if(is_focused || !ex::is_format<asset_t>(extension))
            {
                return;
            }
            if(const auto handle = am.find_asset<asset_t>(key))
            {
                em.foucs_asset(handle);
                is_focused = true;
            }
        });
    return is_focused;
}
} // namespace

auto editor_actions::reveal_in_content_browser(rtti::context& ctx, const std::string& key, std::string* error) -> bool
{
    if(!ctx.has<hub>())
    {
        if(error)
        {
            *error = "Hub is not available";
        }
        return false;
    }
    const fs::path absolute = fs::absolute(fs::resolve_protocol(key));
    fs::error_code err;
    if(!fs::exists(absolute, err))
    {
        if(error)
        {
            *error = "Nothing at " + key;
        }
        return false;
    }
    if(!focus_known_asset(ctx, key))
    {
        auto& em = ctx.get_cached<editing_manager>();
        em.focus(absolute);
        em.focus_path(absolute);
    }
    auto& panel = ctx.get_cached<hub>().get_panels().get_content_browser_panel();
    panel.set_visible(true);
    panel.focus();
    return true;
}

auto editor_actions::open_project_settings(rtti::context& ctx, const std::string& category, std::string* error)
    -> bool
{
    if(!ctx.has<hub>() || !ctx.get_cached<project_manager>().has_open_project())
    {
        if(error)
        {
            *error = "No open project";
        }
        return false;
    }
    ctx.get_cached<hub>().open_project_settings(ctx, category);
    return true;
}

auto editor_actions::get_recent_logs(rtti::context& ctx,
                                     level::level_enum min_level,
                                     size_t max_count,
                                     uint64_t after_id) -> std::vector<log_query_entry>
{
    if(!ctx.has<hub>())
    {
        return {};
    }
    auto snapshot = ctx.get_cached<hub>().get_panels().get_console_log_panel().snapshot_logs(min_level,
                                                                                           max_count,
                                                                                           after_id);
    std::vector<log_query_entry> out;
    out.reserve(snapshot.size());
    for(auto& entry : snapshot)
    {
        log_query_entry item;
        item.id = entry.id;
        item.level = entry.level;
        item.text = std::move(entry.text);
        item.filename = std::move(entry.filename);
        item.funcname = std::move(entry.funcname);
        item.line = entry.line;
        out.push_back(std::move(item));
    }
    return out;
}

auto editor_actions::inspect_entity(rtti::context& ctx,
                                    const std::string& entity_id,
                                    bool include_components,
                                    std::string* error) -> std::string
{
    auto& em = ctx.get_cached<editing_manager>();
    auto* scn = em.get_active_scene(ctx);
    if(!scn || !scn->registry)
    {
        if(error)
        {
            *error = "No active scene";
        }
        return {};
    }
    entt::handle entity;
    if(entity_id.empty())
    {
        if(auto* active = em.try_get_active_selection_as<entt::handle>())
        {
            entity = *active;
        }
        if(!entity)
        {
            if(error)
            {
                *error = "No entity_id provided and no active entity selection";
            }
            return {};
        }
    }
    else
    {
        entity = find_entity_by_id(*scn, entity_id);
        if(!entity)
        {
            if(error)
            {
                *error = "Entity not found: " + entity_id;
            }
            return {};
        }
    }
    auto summary = entity_to_summary_json(entity, 0, 0);
    if(!include_components)
    {
        return summary;
    }
    auto components = entity_components_serialized(entity);
    std::string escaped;
    escaped.reserve(components.size() + 8);
    for(char c : components)
    {
        switch(c)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += c;
                break;
        }
    }
    if(!summary.empty() && summary.back() == '}')
    {
        summary.pop_back();
        summary += ",\"components_serialized\":\"" + escaped + "\"}";
    }
    return summary;
}

auto editor_actions::focus_scene_panel(rtti::context& ctx, std::string* error) -> bool
{
    if(!ctx.has<hub>())
    {
        if(error)
        {
            *error = "Hub is not available";
        }
        return false;
    }
    auto& panel = ctx.get_cached<hub>().get_panels().get_scene_panel();
    panel.set_visible(true);
    panel.focus();
    return true;
}

auto editor_actions::focus_game_panel(rtti::context& ctx, std::string* error) -> bool
{
    if(!ctx.has<hub>())
    {
        if(error)
        {
            *error = "Hub is not available";
        }
        return false;
    }
    auto& panel = ctx.get_cached<hub>().get_panels().get_game_panel();
    panel.set_visible(true);
    panel.focus();
    return true;
}

auto editor_actions::show_mcp_window(rtti::context& ctx, std::string* error) -> bool
{
    if(!ctx.has<hub>())
    {
        if(error)
        {
            *error = "Hub is not available";
        }
        return false;
    }
    ctx.get_cached<hub>().get_panels().get_mcp_panel().show(true);
    return true;
}

auto editor_actions::rebuild_pending_armatures(rtti::context& ctx) -> std::vector<std::shared_ptr<editing_action_t>>
{
    auto& em = ctx.get_cached<editing_manager>();
    auto& ec = ctx.get_cached<ecs>();
    std::vector<scene*> scenes{&ec.get_scene()};
    if(auto* active_scene = em.get_active_scene(ctx); active_scene != scenes.front())
    {
        scenes.push_back(active_scene);
    }
    std::vector<entt::handle> pending;
    for(auto* scn : scenes)
    {
        scn->registry->view<model_component>().each(
            [&](auto entity, auto&& model_comp)
            {
                if(model_comp.is_armature_rebuild_pending())
                {
                    pending.push_back(scn->create_handle(entity));
                }
            });
    }
    std::vector<std::shared_ptr<editing_action_t>> steps;
    for(auto model_entity : pending)
    {
        auto& model_comp = model_entity.get<model_component>();
        // The rebuild creates the nodes first; the step records them, so undo removes them.
        auto created_roots = model_comp.rebuild_armature();
        if(!created_roots.empty())
        {
            auto create_step = std::make_shared<create_entities_action_t>(
                [created_roots]() -> std::vector<entt::handle>
                {
                    return created_roots;
                });
            create_step->do_action();
            steps.push_back(std::move(create_step));
        }
        auto stale_roots = model_comp.take_stale_armature_entities();
        if(!stale_roots.empty())
        {
            auto delete_step = std::make_shared<delete_entities_action_t>(std::move(stale_roots));
            delete_step->do_action();
            steps.push_back(std::move(delete_step));
        }
    }
    return steps;
}

auto editor_actions::request_main_window_focus(rtti::context& ctx, std::string* error) -> bool
{
    if(!ctx.has<renderer>())
    {
        if(error)
        {
            *error = "Renderer is not available";
        }
        return false;
    }
    auto* main_window = ctx.get_cached<renderer>().get_main_window();
    if(!main_window)
    {
        if(error)
        {
            *error = "Main window is not available";
        }
        return false;
    }
    auto& window = main_window->get_window();
    if(!window.is_open())
    {
        if(error)
        {
            *error = "Main window is not open";
        }
        return false;
    }
    if(window.is_minimized())
    {
        window.restore();
    }
    window.show();
    window.raise();
    window.request_focus();
    return true;
}

auto editor_actions::resolve_import_items(const std::vector<std::string>& paths, const fs::path& target_path)
    -> std::vector<import_files_item>
{
    std::vector<import_files_item> items;
    items.reserve(paths.size());
    fs::error_code ec;
    for(const auto& path : paths)
    {
        import_files_item item{};
        fs::path source = fs::path(path).make_preferred();
        fs::path filename = source.filename();
        fs::path dest = target_path / filename;
        item.source_path = source.generic_string();
        item.dest_path = dest.generic_string();
        item.is_directory = fs::is_directory(source, ec);
        const auto protocol = fs::convert_to_protocol(dest);
        if(!protocol.empty())
        {
            item.dest_key = protocol.generic_string();
        }
        items.push_back(std::move(item));
    }
    return items;
}

auto editor_actions::import_files(rtti::context& ctx,
                                  const std::vector<std::string>& paths,
                                  const fs::path& target_path,
                                  bool async) -> import_files_result
{
    import_files_result result;
    fs::error_code ec;
    fs::create_directories(target_path, ec);
    result.items = resolve_import_items(paths, target_path);
    auto copy_batch = [items = result.items]() -> bool
    {
        auto copy_one = [](const fs::path& source, const fs::path& dest, bool is_directory) -> bool
        {
            fs::error_code err;
            if(is_directory)
            {
                fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, err);
                if(err)
                {
                    APPLOG_ERROR("Failed to import directory {}, error: {}", source.string(), err.message());
                    return false;
                }
                return true;
            }
            asset_writer::atomic_copy_file(source, dest, err);
            if(err)
            {
                APPLOG_ERROR("Failed to import file {}, error: {}", source.string(), err.message());
                return false;
            }
            return true;
        };
        // Pause for the whole batch so glTF/.bin/texture sets are never observed mid-copy.
        fs::watcher::scoped_pause pause_guard;
        bool all_ok = true;
        for(const auto& item : items)
        {
            const fs::path source(item.source_path);
            const fs::path dest(item.dest_path);
            APPLOG_INFO("Importing {0}", source.filename().string());
            const bool ok = copy_one(source, dest, item.is_directory);
            if(ok)
            {
                // Ensure the watcher notices the full import even if OS events were coalesced.
                fs::watcher::touch(dest, item.is_directory);
            }
            else
            {
                all_ok = false;
            }
        }
        return all_ok;
    };
    if(async)
    {
        auto& ts = ctx.get_cached<threader>();
        auto job = ts.pool->schedule("Importing files", std::move(copy_batch));
        result.future = job.share();
    }
    else
    {
        const bool ok = copy_batch();
        result.future = tpp::make_ready_future<bool>(bool(ok)).share();
    }
    return result;
}

auto editor_actions::wait_import_jobs(import_files_result& result,
                                      std::chrono::milliseconds timeout) -> bool
{
    tpp::this_thread::register_this_thread();
    if(!result.future.valid())
    {
        return false;
    }
    const auto status = result.future.wait_for(timeout);
    if(status != std::future_status::ready)
    {
        return false;
    }
    return result.future.get();
}
} // namespace unravel
