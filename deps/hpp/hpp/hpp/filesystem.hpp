#pragma once
#include <array>
#include <cstdlib>
#include <vector>
#include <algorithm>

// #include <version>
// #if defined(__cpp_lib_filesystem)
#if defined(__cplusplus) && __cplusplus >= 201703L && !defined(STX_NO_STD_FILESYSTEM)
#if defined(__has_include) && __has_include(<filesystem>)
#include <filesystem>
namespace fs
{
using namespace std::filesystem;
} // namespace fs
#endif
#else
#include "detail/filesystem_impl.hpp"

namespace fs
{
using namespace ghc::filesystem;
} // namespace fs
#endif
// #endif
namespace fs
{

using error_code = std::error_code;
inline file_time_type now()
{
	return file_time_type::clock::now();
}
//-----------------------------------------------------------------------------
//  Name : executable_path()
/// <summary>
/// Retrieve the directory of the currently running application.
/// </summary>
//-----------------------------------------------------------------------------
path executable_path(const char* argv0);

std::string executable_extension();
//-----------------------------------------------------------------------------
//  Name : show_in_graphical_env ()
/// <summary>
/// Shows a path in the graphical environment e.g Explorer, Finder... etc.
/// </summary>
//-----------------------------------------------------------------------------
void show_in_graphical_env(const path& _path);

//-----------------------------------------------------------------------------
//  Name : persistent_path ()
/// <summary>
/// Returns os specific persistent folder like AppData on windows etc.
/// </summary>
//-----------------------------------------------------------------------------
path persistent_path();

inline bool is_executable(const path& path)
{
	fs::error_code ec;
	auto status = fs::status(path, ec);

	if(ec)
	{
		return false;
	}

	if(fs::is_regular_file(status))
	{
#if defined(_WIN32) || defined(_WIN64)
		// On Windows, just check if it's a regular file
		return true;
#else
		// On Unix-like systems, check the execute permissions
		return (status.permissions() & fs::perms::owner_exec) != fs::perms::none ||
			   (status.permissions() & fs::perms::group_exec) != fs::perms::none ||
			   (status.permissions() & fs::perms::others_exec) != fs::perms::none;
#endif
	}
	return false;
}

inline fs::path find_program(const std::vector<std::string>& names, const std::vector<path>& paths)
{
	for(auto dir : paths)
	{
		dir.make_preferred();
		for(const auto& name : names)
		{
			path full_path = dir / name;
			if(fs::exists(full_path) && fs::is_executable(full_path))
			{
				return full_path;
			}
		}
	}

	return {};
}


inline fs::path find_program(const std::vector<std::string>& names, const std::vector<std::string>& paths)
{
	std::vector<fs::path> lib_paths;
	lib_paths.reserve(paths.size());
	std::transform(std::begin(paths), std::end(paths), std::back_inserter(lib_paths), [](const auto& p)
				   {
					   return fs::path(p).make_preferred();
				   });
	return find_program(names, lib_paths);
}

inline std::vector<std::string> get_library_extensions()
{
	std::vector<std::string> extensions;
#if defined(_WIN32) || defined(_WIN64)
	extensions.push_back(".dll");
	extensions.push_back(".lib");
#elif defined(__APPLE__)
	extensions.push_back(".dylib");
	extensions.push_back(".a");
#else
	extensions.push_back(".so");
	extensions.push_back(".a");
#endif
	return extensions;
}

inline fs::path find_library(const std::vector<std::string>& names, const std::vector<path>& paths)
{
	auto extensions = get_library_extensions();

	for(auto dir : paths)
	{
		dir.make_preferred();
		for(const auto& name : names)
		{
			for(const auto& ext : extensions)
			{
				path full_path = dir / (name + ext);
				if(fs::exists(full_path))
				{
					return full_path;
				}
			}
		}
	}
	return {};
}

inline fs::path find_library(const std::vector<std::string>& names, const std::vector<std::string>& paths)
{
	std::vector<fs::path> lib_paths;
	lib_paths.reserve(paths.size());
	std::transform(std::begin(paths), std::end(paths), std::back_inserter(lib_paths), [](const auto& p)
				   {
					   return fs::path(p).make_preferred();
				   });
	return find_library(names, lib_paths);
}
} // namespace fs

namespace fs
{
inline path executable_path_fallback(const char* argv0)
{
	if(nullptr == argv0 || 0 == argv0[0])
	{
		return "";
	}
	fs::error_code err;
	path full_path(absolute(path(std::string(argv0)), err));
	return full_path;
}
} // namespace fs
#if defined(_WIN32)
#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>

#undef min
#undef max
namespace fs
{
inline path executable_path(const char* argv0)
{
	std::array<char, 1024> buf;
	buf.fill(0);
	DWORD ret = GetModuleFileNameA(nullptr, buf.data(), DWORD(buf.size()));
	if(ret == 0 || std::size_t(ret) == buf.size())
	{
		return executable_path_fallback(argv0);
	}
	return path(std::string(buf.data()));
}

inline std::string executable_extension()
{
	return ".exe";
}

// inline std::string get_associated_program_for_file_type(const std::string& fileType)
//{
//     std::string exeName;
//     DWORD length = 0;

//    // Get the executable name
//    length = 0;
//    AssocQueryStringA(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, fileType.c_str(), NULL, NULL, &length);
//    if (length > 0)
//    {
//        exeName.resize(length);
//        AssocQueryStringA(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, fileType.c_str(), NULL, exeName.data(),
//        &length);
//    }

//    return exeName;
//}

inline void show_in_graphical_env(const path& _path)
{
	if(is_regular_file(_path))
	{
		// Open Explorer and select the file
		// The correct format is: explorer.exe /select,"path\to\file"
		// Note the lack of space between /select, and the path
		std::string params = "/select,\"" + _path.string() + "\"";
		ShellExecuteA(nullptr, "open", "explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
	}
	else
	{
		// Just open the directory
		ShellExecuteA(nullptr, "open", "explorer.exe", _path.string().c_str(), nullptr, SW_SHOWNORMAL);
	}
}

inline path persistent_path()
{
	TCHAR szPath[MAX_PATH];

	if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, szPath)))
	{
		return path(szPath);
	}
	return {};
}
} // namespace fs
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
namespace fs
{
inline path executable_path(const char* argv0)
{
	std::array<char, 1024> buf;
	buf.fill(0);
	uint32_t size = buf.size();
	int ret = _NSGetExecutablePath(buf.data(), &size);
	if(0 != ret)
	{
		return executable_path_fallback(argv0);
	}
	fs::error_code err;
	path full_path(absolute(fs::absolute(path(std::string(buf.data()))), err));
	return full_path;
}
inline std::string executable_extension()
{
	return ".app";
}
inline void show_in_graphical_env(const path& _path)
{
	if(is_regular_file(_path))
	{
		// Open Finder and select the file
		std::string script = "tell application \"Finder\" to reveal POSIX file \"" + _path.string() + "\"";
		std::string activate = "tell application \"Finder\" to activate";
		
		std::string command = "osascript -e '" + script + "' -e '" + activate + "'";
		auto result = std::system(command.c_str());
		(void)result;
	}
	else
	{
		// Just open the directory
		std::string command = "open \"" + _path.string() + "\"";
		auto result = std::system(command.c_str());
		(void)result;
	}
}
inline path persistent_path()
{
	return {};
}
} // namespace fs
#elif defined(__linux__)

#include <unistd.h>
namespace fs
{
inline path executable_path(const char* argv0)
{
	std::array<char, 1024> buf;
	buf.fill(0);

	ssize_t size = readlink("/proc/self/exe", buf.data(), buf.size());
	if(size == 0 || size == sizeof(buf))
	{
		return executable_path_fallback(argv0);
	}
	std::string p(buf.data(), size);
	fs::error_code err;
	path full_path(absolute(fs::absolute(path(p)), err));
	return full_path;
}
inline std::string executable_extension()
{
	return "";
}
inline void show_in_graphical_env(const path& _path)
{
	static std::string cmd = "xdg-open";
	static std::string space = " ";
	
	if(is_regular_file(_path))
	{
		// Try to use file managers that support selecting files
		// First try nautilus (GNOME)
		std::string nautilus_cmd = "nautilus --select \"" + _path.string() + "\"";
		if(std::system((nautilus_cmd + " 2>/dev/null").c_str()) != 0)
		{
			// Try dolphin (KDE)
			std::string dolphin_cmd = "dolphin --select \"" + _path.string() + "\"";
			if(std::system((dolphin_cmd + " 2>/dev/null").c_str()) != 0)
			{
				// Try nemo (Cinnamon)
				std::string nemo_cmd = "nemo \"" + _path.parent_path().string() + "\"";
				if(std::system((nemo_cmd + " 2>/dev/null").c_str()) != 0)
				{
					// Try Thunar (XFCE)
					std::string thunar_cmd = "thunar \"" + _path.parent_path().string() + "\"";
					if(std::system((thunar_cmd + " 2>/dev/null").c_str()) != 0)
					{
						// Try PCManFM (LXDE/LXQt)
						std::string pcmanfm_cmd = "pcmanfm \"" + _path.parent_path().string() + "\"";
						if(std::system((pcmanfm_cmd + " 2>/dev/null").c_str()) != 0)
						{
							// Fallback to opening the parent directory with xdg-open
							const std::string cmd_args = "\"" + _path.parent_path().string() + "\"";
							const std::string whole_command = cmd + space + cmd_args;
							auto result = std::system(whole_command.c_str());
							(void)result;
						}
					}
				}
			}
		}
	}
	else
	{
		// Just open the directory
		const std::string cmd_args = "\"" + _path.string() + "\"";
		const std::string whole_command = cmd + space + cmd_args;
		auto result = std::system(whole_command.c_str());
		(void)result;
	}
}
inline path persistent_path()
{

	char* home = getenv("XDG_CONFIG_HOME");
	if(!home)
	{
		home = getenv("HOME");

		if(!home)
		{
			return {};
		}
	}

	path result(home);
	result /= ".local/share";
	return result;
}
} // namespace fs
#else
namespace fs
{
inline path executable_path(const char* argv0)
{
	return executable_path_fallback(argv0);
}
inline std::string executable_extension()
{
	return "";
}
inline void show_in_graphical_env(const path& _path)
{
	// Fallback implementation for unsupported platforms
	// At least try to print the path for debugging purposes
	if(is_regular_file(_path))
	{
		std::printf("Showing file in graphical environment (unsupported platform): %s\n", _path.string().c_str());
		std::printf("Parent directory: %s\n", _path.parent_path().string().c_str());
	}
	else
	{
		std::printf("Showing directory in graphical environment (unsupported platform): %s\n", _path.string().c_str());
	}
}

inline path persistent_path()
{
	return {};
}
} // namespace fs
#endif
