#include "editor_actions.h"
#include "engine/scripting/script.h"

#include <engine/assets/asset_manager.h>
#include <engine/assets/impl/asset_extensions.h>
#include <engine/assets/impl/asset_reader.h>
#include <engine/defaults/defaults.h>
#include <engine/ecs/ecs.h>
#include <engine/engine.h>
#include <engine/events.h>
#include <engine/meta/assets/asset_database.hpp>
#include <engine/meta/ecs/entity.hpp>
#include <engine/scripting/ecs/systems/script_system.h>
#include <filesystem/watcher.h>
#include <engine/assets/impl/asset_writer.h>
#include <editor/editing/editing_manager.h>
#include <editor/system/project_manager.h>
#include <editor/imgui/integration/imgui_notify.h>
#include <filedialog/filedialog.h>
#include <filesystem/filesystem.h>
#include <filesystem>
#include <subprocess/subprocess.hpp>

#include <base/platform/config.hpp>
#include <string_utils/utils.h>

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
    json_stream << "        \"dotnet.preferCSharpExtension\": true,\n";
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

    // Close the files.exclude object and the settings object
    json_stream << "\n";
    json_stream << "        }\n"; // End of "files.exclude"
    json_stream << "    }\n";     // End of "settings"

    // Add the "extensions" section
    json_stream << ",\n";
    json_stream << "    \"extensions\": {\n";
    json_stream << "        \"recommendations\": [\n";
    json_stream << "             \"ms-vscode.mono-debug\",\n";
    json_stream << "             \"ms-dotnettools.csharp\"\n";
    json_stream << "        ]\n";
    json_stream << "    }\n";

    // Add the "launch" section
    json_stream << ",\n";
    json_stream << "    \"launch\": {\n";
    json_stream << "        \"version\": \"0.2.0\",\n";
    json_stream << "        \"configurations\": [\n";
    json_stream << "            {\n";
    json_stream << "                \"name\": \"Attach to Mono\",\n";
    json_stream << "                \"request\": \"attach\",\n";
    json_stream << "                \"type\": \"mono\",\n";
    json_stream << "                \"address\": \"" << settings.debugger.ip << "\",\n";
    json_stream << "                \"port\": " << settings.debugger.port << "\n";
    json_stream << "            }\n";
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

/**
 * @brief Generates a .csproj file based on the provided parameters.
 *
 * @param source_directory Directory containing C# source files.
 * @param external_dll_path Path to the external DLL to reference.
 * @param output_directory Directory where the .csproj file will be generated.
 * @param project_name Name of the project and the .csproj file (default: "MyLibrary").
 * @param dotnet_sdk_version Target .NET SDK version (default: "7.0").
 *
 * @throws std::runtime_error if the .csproj file cannot be created.
 */
void generate_csproj(const fs::path& source_directory,
                     const std::vector<fs::path>& external_dll_paths,
                     const fs::path& output_directory,
                     const std::string& project_name = "MyLibrary",
                     const std::string& dotnet_sdk_version = "7.0")
{
    // Ensure the output directory exists
    try
    {
        fs::create_directories(output_directory);
    }
    catch(const fs::filesystem_error& e)
    {
        throw std::runtime_error("Failed to create output directory: " + std::string(e.what()));
    }

    // Verify that the source directory exists
    if(!fs::exists(source_directory) || !fs::is_directory(source_directory))
    {
        throw std::runtime_error("Source directory does not exist or is not a directory: " + source_directory.string());
    }

    // Verify that all external DLLs exist and are files
    for(const auto& dll_path : external_dll_paths)
    {
        if(!fs::exists(dll_path) || !fs::is_regular_file(dll_path))
        {
            throw std::runtime_error("External DLL does not exist or is not a file: " + dll_path.string());
        }
    }

    // Collect all C# source files from the specified source directory
    std::vector<fs::path> csharp_sources;
    try
    {
        for(const auto& entry : fs::recursive_directory_iterator(source_directory))
        {
            if(entry.is_regular_file() && entry.path().extension() == ".cs")
            {
                // Compute the relative path from the source directory
                fs::path relative_path = fs::relative(entry.path(), source_directory);
                csharp_sources.push_back(relative_path);
            }
        }
    }
    catch(const fs::filesystem_error& e)
    {
        throw std::runtime_error("Error while iterating source directory: " + std::string(e.what()));
    }

    // Generate the list of source files for the .csproj file with <Link> elements (for virtual folders)
    std::string csharp_source_items;
    for(const auto& source_file : csharp_sources)
    {
        // Convert path to generic format (forward slashes)
        std::string source_file_str = source_file.string();
        fs::path full_physical_path = fs::absolute(source_directory / source_file);
        std::string full_physical_path_str = full_physical_path.string();

        // Construct the <Compile Include> with <Link>
        csharp_source_items += "    <Compile Include=\"" + full_physical_path_str + "\">\n";
        csharp_source_items += "      <Link>" + source_file_str + "</Link>\n";
        csharp_source_items += "    </Compile>\n";
    }

    // Generate external DLL references
    std::string external_dll_references;
    for(const auto& dll_path : external_dll_paths)
    {
        std::string dll_name = dll_path.filename().string();
        fs::path dll_absolute_path = fs::absolute(dll_path);
        std::string dll_absolute_path_str = dll_absolute_path.string(); // Forward slashes

        external_dll_references += "    <Reference Include=\"" + dll_name + "\">\n";
        external_dll_references += "      <HintPath>" + dll_absolute_path_str + "</HintPath>\n";
        external_dll_references += "    </Reference>\n";
    }

    // Build the .csproj content
    std::string csproj_content;
    csproj_content += "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
    csproj_content += "  <PropertyGroup>\n";
    csproj_content += "    <TargetFramework>net" + dotnet_sdk_version + "</TargetFramework>\n";
    csproj_content += "    <OutputType>Library</OutputType>\n";
    csproj_content +=
        "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"; // Disable default .cs file inclusion
    csproj_content += "  </PropertyGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += csharp_source_items;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "  <ItemGroup>\n";
    csproj_content += external_dll_references;
    csproj_content += "  </ItemGroup>\n";
    csproj_content += "</Project>\n";

    // Define the path to the .csproj file
    fs::path csproj_path = output_directory / (project_name + ".csproj");

    // Write the .csproj file
    std::ofstream csproj_file(csproj_path);
    if(!csproj_file.is_open())
    {
        APPLOG_ERROR("Failed to create .csproj file at {}", csproj_path.string());
        return;
    }

    csproj_file << csproj_content;

    APPLOG_TRACE("Generated {}", csproj_path.string());
}

void generate_csproj_legacy(const fs::path& source_directory,
                            const std::vector<fs::path>& external_dll_paths,
                            const fs::path& output_directory,
                            const std::string& project_name = "MyLibrary",
                            const std::string& dotnet_framework_version = "v4.7.1")
{
    auto uid = generate_uuid(project_name);
    fs::path output_path = fs::path("temp") / "bin" / "Debug";
    fs::path intermediate_output_path = fs::path("temp") / "obj" / "Debug";

    // Ensure the output directory exists
    try
    {
        fs::create_directories(output_directory);
    }
    catch(const fs::filesystem_error& e)
    {
        throw std::runtime_error("Failed to create output directory: " + std::string(e.what()));
    }

    // Verify that the source directory exists
    if(!fs::exists(source_directory) || !fs::is_directory(source_directory))
    {
        throw std::runtime_error("Source directory does not exist or is not a directory: " + source_directory.string());
    }

    // Verify that all external DLLs exist and are files
    for(const auto& dll_path : external_dll_paths)
    {
        if(!fs::exists(dll_path) || !fs::is_regular_file(dll_path))
        {
            throw std::runtime_error("External DLL does not exist or is not a file: " + dll_path.string());
        }
    }

    // Collect all C# source files from the specified source directory
    std::vector<fs::path> csharp_sources;
    try
    {
        for(const auto& entry : fs::recursive_directory_iterator(source_directory))
        {
            if(entry.is_regular_file() && entry.path().extension() == ".cs")
            {
                // Compute the relative path from the output directory
                fs::path relative_path = fs::relative(entry.path(), output_directory);
                csharp_sources.push_back(relative_path);
            }
        }
    }
    catch(const fs::filesystem_error& e)
    {
        throw std::runtime_error("Error while iterating source directory: " + std::string(e.what()));
    }

    // Generate the list of source files for the .csproj file
    std::string csharp_source_items;
    for(const auto& source_file : csharp_sources)
    {
        // Convert path to generic format (forward slashes)
        std::string source_file_str = source_file.string();
        csharp_source_items += "    <Compile Include=\"" + source_file_str + "\" />\n";
    }

    // Generate external DLL references
    std::string external_dll_references;
    for(const auto& dll_path : external_dll_paths)
    {
        std::string dll_name = dll_path.filename().string();
        fs::path dll_absolute_path = fs::absolute(dll_path);
        std::string dll_absolute_path_str = dll_absolute_path.string(); // Forward slashes

        external_dll_references += "    <Reference Include=\"" + dll_name + "\">\n";
        external_dll_references += "      <HintPath>" + dll_absolute_path_str + "</HintPath>\n";
        external_dll_references += "      <Private>False</Private>\n"; // Mimic Unity's references
        external_dll_references += "    </Reference>\n";
    }

    // Build the .csproj content
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
    csproj_content += "    <OutputPath>" + output_path.string() + "</OutputPath>\n";
    csproj_content +=
        "    <IntermediateOutputPath>" + intermediate_output_path.string() + "</IntermediateOutputPath>\n";

    csproj_content += "  </PropertyGroup>\n";

    // Add other necessary PropertyGroups as needed (similar to the Unity example)
    // ...

    // ItemGroup for Compile (C# files)
    csproj_content += "  <ItemGroup>\n";
    csproj_content += csharp_source_items;
    csproj_content += "  </ItemGroup>\n";

    // ItemGroup for References
    csproj_content += "  <ItemGroup>\n";
    csproj_content += external_dll_references;
    csproj_content += "  </ItemGroup>\n";

    // Add other ItemGroups as needed (e.g., Analyzers, etc.)
    // ...

    // Import the C# targets
    csproj_content += "  <Import Project=\"$(MSBuildToolsPath)\\Microsoft.CSharp.targets\" />\n";

    // Optionally add custom Targets
    csproj_content += "  <Target Name=\"GenerateTargetFrameworkMonikerAttribute\" />\n";

    // Optionally add BeforeBuild and AfterBuild targets
    csproj_content +=
        "  <!-- To modify your build process, add your task inside one of the targets below and uncomment it.\n";
    csproj_content += "       Other similar extension points exist, see Microsoft.Common.targets.\n";
    csproj_content += "  <Target Name=\"BeforeBuild\">\n";
    csproj_content += "  </Target>\n";
    csproj_content += "  <Target Name=\"AfterBuild\">\n";
    csproj_content += "  </Target>\n";
    csproj_content += "  -->\n";

    csproj_content += "</Project>\n";

    // Define the path to the .csproj file
    fs::path csproj_path = output_directory / (project_name + ".csproj");

    // Write the .csproj file
    std::ofstream csproj_file(csproj_path);
    if(!csproj_file.is_open())
    {
        APPLOG_ERROR("Failed to create .csproj file at {}", csproj_path.string());
        return;
    }

    csproj_file << csproj_content;

    APPLOG_TRACE("Generated {}", csproj_path.string());
}

auto trim_line = [](std::string& line)
{
    // Trim trailing spaces and \r
    line.erase(std::find_if(line.rbegin(),
                            line.rend(),
                            [](char ch)
                            {
                                return !std::isspace(int(ch));
                            })
                   .base(),
               line.end());
};

auto parse_line(std::string& line, const fs::path& fs_parent_path) -> bool
{
#if UNRAVEL_PLATFORM_WINDOWS
    // parse dependencies output
    if(line.find("[ApplicationDirectory]") != std::string::npos)
    {
        std::size_t pos = line.find(':');
        if(pos != std::string::npos)
        {
            line = line.substr(pos + 2); // +2 to skip ": "
            trim_line(line);

            return true;
        }
    }
#else
    // parse ldd output
    size_t pos = line.find("=> ");
    if(pos != std::string::npos)
    {
        line = line.substr(pos + 3); // +3 to remove '=> '
        size_t address_pos = line.find(" (0x");
        if(address_pos != std::string::npos)
        {
            line = line.substr(0, address_pos); // remove the address
        }

        trim_line(line);

        fs::path fs_path(line);

        if(fs::exists(fs_path) && fs::exists(fs_parent_path))
        {
            if(fs::equivalent(fs_path.parent_path(), fs_parent_path))
            {
                return true;
            }
        }
    }

#endif
    return false;
}

auto get_subprocess_params(const fs::path& file) -> std::vector<std::string>
{
    std::vector<std::string> params;

#if UNRAVEL_PLATFORM_WINDOWS
    params.emplace_back(fs::resolve_protocol("editor:/tools/dependencies/Dependencies.exe").string());
    params.emplace_back("-modules");
    params.emplace_back(file.string());

#else

    params.emplace_back("ldd");
    params.emplace_back(file.string());
#endif
    return params;
}

auto parse_dependencies(const std::string& input, const fs::path& fs_parent_path) -> std::vector<std::string>
{
    std::vector<std::string> dependencies;
    std::stringstream ss(input);
    std::string line;

    while(std::getline(ss, line))
    {
        if(parse_line(line, fs_parent_path))
        {
            dependencies.push_back(line);
        }
    }
    return dependencies;
}

auto get_dependencies(const fs::path& file) -> std::vector<std::string>
{
    auto parent_path = file.parent_path();

    auto params = get_subprocess_params(file);
    auto result = subprocess::call(params);
    return parse_dependencies(result.out_output, parent_path);
}

auto save_scene_impl(rtti::context& ctx, const fs::path& path) -> bool
{
    auto& ev = ctx.get_cached<events>();
    if(ev.is_playing)
    {
        return false;
    }

    auto& ec = ctx.get_cached<ecs>();
    if(asset_writer::atomic_save_to_file(path.string(), ec.get_scene()))
    {
        ImGui::PushNotification(ImGuiToast(ImGuiToastType_Success, 1000,"Scene saved."));

        auto& em = ctx.get_cached<editing_manager>();
        em.clear_unsaved_changes();
    }

    return true;
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
    if(ev.is_playing)
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

void try_delete_empty_parents(const fs::path& start, const fs::path& root, fs::error_code& ec)
{
    fs::path current = start.parent_path();
    while (current != root && fs::is_empty(current, ec))
    {
        APPLOG_TRACE("Removing Empty Parent Directory {}", current.generic_string());
        fs::remove(current, ec);
        current = current.parent_path();
    }
}

void remove_unreferenced_files(const fs::path& root)
{
    fs::error_code ec;
    const fs::recursive_directory_iterator end;

    std::vector<fs::path> deleted_dirs;

    // First pass: remove matching script files
    {
        fs::recursive_directory_iterator it(root, ec);
        while (it != end)
        {
            const fs::path current_path = it->path();
            ++it;

            for (const auto& type : ex::get_suported_formats<script>())
            {
                auto ext = fs::reduce_trailing_extensions(current_path).extension().generic_string();
                if (ext == type)
                {
                    APPLOG_TRACE("Removing Script {}", current_path.generic_string());
                    fs::remove(current_path, ec);
                    deleted_dirs.push_back(current_path.parent_path());
                    break;
                }
            }
        }
    }

    // Second pass: remove now-empty directories
    {
        fs::recursive_directory_iterator it(root, ec);
        while (it != end)
        {
            const fs::path current_path = it->path();
            ++it;

            if (fs::is_directory(current_path, ec) && fs::is_empty(current_path, ec))
            {
                APPLOG_TRACE("Removing Empty Directory {}", current_path.generic_string());
                fs::remove(current_path, ec);
                deleted_dirs.push_back(current_path.parent_path());
            }
        }
    }

    // Deduplicate deleted parent paths and sort deepest first
    std::sort(deleted_dirs.begin(), deleted_dirs.end());
    deleted_dirs.erase(std::unique(deleted_dirs.begin(), deleted_dirs.end()), deleted_dirs.end());
    std::sort(deleted_dirs.begin(), deleted_dirs.end(),
              [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });

    // Final cleanup: walk up and try deleting empty parents
    for (const auto& path : deleted_dirs)
    {
        try_delete_empty_parents(path, root, ec);
    }
}

} // namespace

auto editor_actions::new_scene(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    if(ev.is_playing)
    {
        return false;
    }
    prompt_save_scene(ctx);

    auto& em = ctx.get_cached<editing_manager>();
    em.clear();

    auto& ec = ctx.get_cached<ecs>();
    ec.unload_scene();

    defaults::create_default_3d_scene(ctx, ec.get_scene());
    return true;
}
auto editor_actions::open_scene(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    if(ev.is_playing)
    {
        ev.set_play_mode(ctx, false);
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

auto editor_actions::open_scene_from_asset(rtti::context& ctx, const asset_handle<scene_prefab>& asset) -> bool
{    
    prompt_save_scene(ctx);

    auto& em = ctx.get_cached<editing_manager>();
    em.clear();

    auto& ec = ctx.get_cached<ecs>();
    ec.unload_scene();

    auto& scene = ec.get_scene();
    bool loaded = scene.load_from(asset);

    if(loaded)
    {
        em.sync_prefab_instances(ctx, &scene);
    }

    return loaded;
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

auto editor_actions::prompt_save_scene(rtti::context& ctx) -> bool
{
    auto& em = ctx.get_cached<editing_manager>();
    if(!em.has_unsaved_changes())
    {
        return true;
    }

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);

    auto& ec = ctx.get_cached<ecs>();
    auto& scene = ec.get_scene();
    
    auto result = native::message_box("Do you want to save changes you made?",
        native::dialog_type::yes_no_cancel,
        native::icon_type::question,
        "Save changes?");

    switch(result)
    {
    case native::action_type::ok_or_yes:
        return save_scene(ctx);
    case native::action_type::no_or_cancel:
        return false;
    default:
        return true;
    }
}

auto editor_actions::close_project(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    if(ev.is_playing)
    {
        return false;
    }

    prompt_save_scene(ctx);

    auto& pm = ctx.get_cached<project_manager>();
    pm.close_project(ctx);
    return true;
}

auto editor_actions::reload_project(rtti::context& ctx) -> bool
{
    auto& ev = ctx.get_cached<events>();
    if(ev.is_playing)
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
    return pm.open_project(ctx, project_path);
}

void editor_actions::run_project(const deploy_settings& params)
{
    auto call_params = params.deploy_location / (std::string("game") + fs::executable_extension());
    subprocess::call(call_params.string());
}

auto editor_actions::deploy_project(rtti::context& ctx,
                                    const deploy_settings& params) -> std::map<std::string, tpp::shared_future<void>>
{
    auto& th = ctx.get_cached<threader>();

    std::map<std::string, tpp::shared_future<void>> jobs;
    std::vector<tpp::shared_future<void>> jobs_seq;

    fs::error_code ec;

    auto& am = ctx.get_cached<asset_manager>();
    // am.get_database("engine:/")

    if(params.deploy_dependencies)
    {
        APPLOG_INFO("Clearing {}", params.deploy_location.generic_string());
        fs::remove_all(params.deploy_location, ec);
        fs::create_directories(params.deploy_location, ec);

        auto job =
            th.pool
                ->schedule("Deploying Dependencies",
                    [params]()
                    {
                        APPLOG_INFO("Deploying Dependencies...");

                        fs::path app_executable = fs::resolve_protocol("binary:/game" + fs::executable_extension());
                        auto deps = get_dependencies(app_executable);

                        fs::error_code ec;
                        for(const auto& dep : deps)
                        {
                            APPLOG_TRACE("Copying {} -> {}", fs::path(dep).generic_string(), params.deploy_location.generic_string());
                            fs::copy(dep, params.deploy_location, fs::copy_options::overwrite_existing, ec);
                        }

                        APPLOG_TRACE("Copying {} -> {}", app_executable.generic_string(), params.deploy_location.generic_string());
                        fs::copy(app_executable, params.deploy_location, fs::copy_options::overwrite_existing, ec);

                        APPLOG_INFO("Deploying Dependencies - Done");
                    })
                .share();
        jobs["Deploying Dependencies"] = job;
        jobs_seq.emplace_back(job);
    }

    {
        auto job = th.pool
                       ->schedule("Deploying Project Settings",
                           [params]()
                           {
                               APPLOG_INFO("Deploying Project Settings...");

                               auto data = fs::resolve_protocol("app:/settings");
                               fs::path dst = params.deploy_location / "data" / "app" / "settings";

                               fs::error_code ec;

                               APPLOG_TRACE("Clearing {}", dst.generic_string());
                               fs::remove_all(dst, ec);
                               fs::create_directories(dst, ec);

                               APPLOG_TRACE("Copying {} -> {}", data.generic_string(), dst.generic_string());
                               fs::copy(data, dst, fs::copy_options::recursive, ec);

                               APPLOG_INFO("Deploying Project Settings - Done");
                           })
                       .share();

        jobs["Deploying Project Settings"] = job;
        jobs_seq.emplace_back(job);
    }

    {
        auto job =
            th.pool
                ->schedule("Deploying Project Data",
                    [params, &am]()
                    {
                        APPLOG_INFO("Deploying Project Data...");

                        fs::error_code ec;
                        {
                            auto data = fs::resolve_protocol(ex::get_compiled_directory("app"));
                            fs::path cached_data = params.deploy_location / "data" / "app" / ex::get_compiled_directory_no_slash();

                            APPLOG_TRACE("Clearing {}", cached_data.generic_string());
                            fs::remove_all(cached_data, ec);
                            fs::create_directories(cached_data, ec);

                            APPLOG_TRACE("Copying {} -> {}", data.generic_string(), cached_data.generic_string());
                            fs::copy(data, cached_data, fs::copy_options::recursive, ec);

                            remove_unreferenced_files(cached_data);
                        }

                        {
                            fs::path cached_data = params.deploy_location / "data" / "app" / "assets.pack";
                            APPLOG_TRACE("Creating Asset Pack -> {}", cached_data.generic_string());
                            am.save_database("app:/", cached_data);
                        }

                        APPLOG_INFO("Deploying Project Data - Done");
                    })
                .share();

        jobs["Deploying Project Data"] = job;
        jobs_seq.emplace_back(job);
    }

    {
        auto job = th.pool
                       ->schedule("Deploying Engine Data",
                           [params, &am]()
                           {
                               APPLOG_INFO("Deploying Engine Data...");

                               fs::error_code ec;
                               {
                                   fs::path cached_data = params.deploy_location / "data" / "engine" / ex::get_compiled_directory_no_slash();
                                   auto data = fs::resolve_protocol(ex::get_compiled_directory("engine"));

                                   APPLOG_TRACE("Clearing {}", cached_data.generic_string());
                                   fs::remove_all(cached_data, ec);
                                   fs::create_directories(cached_data, ec);

                                   APPLOG_TRACE("Copying {} -> {}", data.generic_string(), cached_data.generic_string());
                                   fs::copy(data, cached_data, fs::copy_options::recursive, ec);

                                   remove_unreferenced_files(cached_data);
                               }

                               {
                                   fs::path cached_data = params.deploy_location / "data" / "engine" / "assets.pack";
                                   APPLOG_TRACE("Creating Asset Pack -> {}", cached_data.generic_string());
                                   am.save_database("engine:/", cached_data);
                               }

                               APPLOG_INFO("Deploying Engine Data - Done");
                           })
                       .share();
        jobs["Deploying Engine Data..."] = job;
        jobs_seq.emplace_back(job);
    }

    {
        auto job = th.pool
                       ->schedule("Deploying Mono",
                           [params, &am, &ctx]()
                           {
                               APPLOG_INFO("Deploying Mono...");

                               auto paths = script_system::find_mono(ctx);
                               fs::path assembly_path = mono::get_core_assembly_path();
                               fs::path assembly_dir = assembly_path.parent_path();
                               fs::path lib_version = assembly_dir.filename();

                               fs::error_code ec;

                               {
                                   fs::path cached_data = params.deploy_location / "data" / "engine" / "mono" / "lib";
                                   cached_data /= "mono";

                                   APPLOG_TRACE("Clearing {}", cached_data.generic_string());
                                   fs::remove_all(cached_data, ec);

                                   cached_data /= lib_version;

                                   fs::create_directories(cached_data, ec);

                                   APPLOG_TRACE("Copying {} -> {}", assembly_dir.generic_string(), cached_data.generic_string());
                                   fs::copy(assembly_dir, cached_data, fs::copy_options::recursive, ec);
                               }

                               fs::path config_dir = paths.config_dir;
                               config_dir /= "mono";

                               {
                                   fs::path cached_data = params.deploy_location / "data" / "engine" / "mono" / "etc";
                                   cached_data /= "mono";

                                   APPLOG_TRACE("Clearing {}", cached_data.generic_string());
                                   fs::remove_all(cached_data, ec);
                                   fs::create_directories(cached_data, ec);

                                   APPLOG_TRACE("Copying {} -> {}", config_dir.generic_string(), cached_data.generic_string());
                                   fs::copy(config_dir, cached_data, fs::copy_options::recursive, ec);
                               }

                               APPLOG_INFO("Deploying Mono - Done");
                           })
                       .share();
        jobs["Deploying Mono..."] = job;
        jobs_seq.emplace_back(job);
    }

    tpp::when_all(std::begin(jobs_seq), std::end(jobs_seq))
        .then(tpp::this_thread::get_id(),
              [params](auto f)
              {
                  if(params.deploy_and_run)
                  {
                      run_project(params);
                  }
                  else
                  {
                      fs::show_in_graphical_env(params.deploy_location);
                  }
              });

    return jobs;
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
    remove_extensions(formats, ex::get_suported_formats<gfx::shader>());
    remove_extensions(formats, ex::get_suported_formats<script>());

    auto workspace_file = workspace_folder / fmt::format("{}-workspace.code-workspace", project_name);
    generate_workspace_file(workspace_file.string(), formats, editor_settings);

    auto source_path = fs::resolve_protocol("app:/data");

    auto engine_dep = fs::resolve_protocol(script_system::get_lib_compiled_key("engine"));

    auto output_path = fs::resolve_protocol("app:/");

    generate_csproj_legacy(source_path, {engine_dep}, output_path, project_name);
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

void editor_actions::recompile_shaders()
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto shaders = am.get_assets<gfx::shader>();
    fs::watcher::pause();
    for(auto& asset : shaders)
    {
        auto path = fs::absolute(fs::resolve_protocol(asset.id()).string());
        fs::watcher::touch(path, false);
    }
    fs::watcher::resume();
}

void editor_actions::recompile_textures()
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto textures = am.get_assets<gfx::texture>();
    fs::watcher::pause();
    for(auto& asset : textures)
    {
        auto path = fs::absolute(fs::resolve_protocol(asset.id()).string());
        fs::watcher::touch(path, false);
    }
    fs::watcher::resume();
}

void editor_actions::recompile_scripts()
{
    auto& ctx = engine::context();
    auto& am = ctx.get_cached<asset_manager>();
    auto scripts = am.get_assets<script>();
    fs::watcher::pause();
    for(auto& asset : scripts)
    {
        auto path = fs::absolute(fs::resolve_protocol(asset.id()).string());
        fs::watcher::touch(path, false);
    }
    fs::watcher::resume();
}
void editor_actions::recompile_all()
{
    recompile_shaders();
    recompile_textures();
    recompile_scripts();
}
} // namespace unravel
