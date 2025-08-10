#pragma once

#include "config.hpp"

////////////////////////////////////////////////////////////
// Define helpers to create portable import / export macros for each module
////////////////////////////////////////////////////////////
#if !defined(UNRAVEL_STATIC)

#if UNRAVEL_PLATFORM_WINDOWS
// Windows compilers need specific (and different) keywords for export and import
#define UNRAVEL_API_EXPORT __declspec(dllexport)
#define UNRAVEL_API_IMPORT __declspec(dllimport)

// For Visual C++ compilers, we also need to turn off this annoying C4251 warning
#if UNRAVEL_COMPILER_MSVC
#pragma warning(disable : 4251)
#endif

#else // Linux, FreeBSD, Mac OS X

// GCC 4+ has special keywords for showing/hidding symbols,
// the same keyword is used for both importing and exporting
#define UNRAVEL_API_EXPORT __attribute__((__visibility__("default")))
#define UNRAVEL_API_IMPORT __attribute__((__visibility__("default")))

#endif

#else

// Static build doesn't need import/export macros
#define UNRAVEL_API_EXPORT
#define UNRAVEL_API_IMPORT

#endif

////////////////////////////////////////////////////////////
// Define portable import / export macros
////////////////////////////////////////////////////////////
#if defined(UNRAVEL_API_EXPORTS)
#define UNRAVEL_API UNRAVEL_API_EXPORT
#else
#define UNRAVEL_API UNRAVEL_API_IMPORT
#endif
