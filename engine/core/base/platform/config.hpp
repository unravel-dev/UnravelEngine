#pragma once
#include <cstdint>

#if !defined(DEBUG) && !defined(_DEBUG)
#ifndef NDEBUG
#define NDEBUG
#endif
#ifndef _NDEBUG
#define _NDEBUG
#endif
#endif


// Architecture
#define UNRAVEL_ARCH_32BIT 0
#define UNRAVEL_ARCH_64BIT 0

// Compiler
#define UNRAVEL_COMPILER_CLANG          0
#define UNRAVEL_COMPILER_CLANG_ANALYZER 0
#define UNRAVEL_COMPILER_GCC            0
#define UNRAVEL_COMPILER_MSVC           0

// Endianness
#define UNRAVEL_CPU_ENDIAN_BIG    0
#define UNRAVEL_CPU_ENDIAN_LITTLE 0

// CPU
#define UNRAVEL_CPU_ARM   0
#define UNRAVEL_CPU_JIT   0
#define UNRAVEL_CPU_MIPS  0
#define UNRAVEL_CPU_PPC   0
#define UNRAVEL_CPU_RISCV 0
#define UNRAVEL_CPU_X86   0

// C Runtime
#define UNRAVEL_CRT_BIONIC 0
#define UNRAVEL_CRT_GLIBC  0
#define UNRAVEL_CRT_LIBCXX 0
#define UNRAVEL_CRT_MINGW  0
#define UNRAVEL_CRT_MSVC   0
#define UNRAVEL_CRT_NEWLIB 0

#ifndef UNRAVEL_CRT_NONE
#	define UNRAVEL_CRT_NONE 0
#endif // UNRAVEL_CRT_NONE

// Language standard version
#define UNRAVEL_LANGUAGE_CPP17 201703L
#define UNRAVEL_LANGUAGE_CPP20 202002L
#define UNRAVEL_LANGUAGE_CPP23 202207L

// Platform
#define UNRAVEL_PLATFORM_ANDROID    0
#define UNRAVEL_PLATFORM_BSD        0
#define UNRAVEL_PLATFORM_EMSCRIPTEN 0
#define UNRAVEL_PLATFORM_HAIKU      0
#define UNRAVEL_PLATFORM_HURD       0
#define UNRAVEL_PLATFORM_IOS        0
#define UNRAVEL_PLATFORM_LINUX      0
#define UNRAVEL_PLATFORM_NX         0
#define UNRAVEL_PLATFORM_OSX        0
#define UNRAVEL_PLATFORM_PS4        0
#define UNRAVEL_PLATFORM_PS5        0
#define UNRAVEL_PLATFORM_RPI        0
#define UNRAVEL_PLATFORM_VISIONOS   0
#define UNRAVEL_PLATFORM_WINDOWS    0
#define UNRAVEL_PLATFORM_WINRT      0
#define UNRAVEL_PLATFORM_XBOXONE    0

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Compilers
#if defined(__clang__)
// clang defines __GNUC__ or _MSC_VER
#	undef  UNRAVEL_COMPILER_CLANG
#	define UNRAVEL_COMPILER_CLANG (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#	if defined(__clang_analyzer__)
#		undef  UNRAVEL_COMPILER_CLANG_ANALYZER
#		define UNRAVEL_COMPILER_CLANG_ANALYZER 1
#	endif // defined(__clang_analyzer__)
#elif defined(_MSC_VER)
#	undef  UNRAVEL_COMPILER_MSVC
#	define UNRAVEL_COMPILER_MSVC _MSC_VER
#elif defined(__GNUC__)
#	undef  UNRAVEL_COMPILER_GCC
#	define UNRAVEL_COMPILER_GCC (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#else
#	error "UNRAVEL_COMPILER_* is not defined!"
#endif //

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Architectures
#if defined(__arm__)     \
 || defined(__aarch64__) \
 || defined(_M_ARM)
#	undef  UNRAVEL_CPU_ARM
#	define UNRAVEL_CPU_ARM 1
#	define UNRAVEL_CACHE_LINE_SIZE 64
#elif defined(__MIPSEL__)     \
 ||   defined(__mips_isa_rev) \
 ||   defined(__mips64)
#	undef  UNRAVEL_CPU_MIPS
#	define UNRAVEL_CPU_MIPS 1
#	define UNRAVEL_CACHE_LINE_SIZE 64
#elif defined(_M_PPC)        \
 ||   defined(__powerpc__)   \
 ||   defined(__powerpc64__)
#	undef  UNRAVEL_CPU_PPC
#	define UNRAVEL_CPU_PPC 1
#	define UNRAVEL_CACHE_LINE_SIZE 128
#elif defined(__riscv)   \
 ||   defined(__riscv__) \
 ||   defined(RISCVEL)
#	undef  UNRAVEL_CPU_RISCV
#	define UNRAVEL_CPU_RISCV 1
#	define UNRAVEL_CACHE_LINE_SIZE 64
#elif defined(_M_IX86)    \
 ||   defined(_M_X64)     \
 ||   defined(__i386__)   \
 ||   defined(__x86_64__)
#	undef  UNRAVEL_CPU_X86
#	define UNRAVEL_CPU_X86 1
#	define UNRAVEL_CACHE_LINE_SIZE 64
#else // PNaCl doesn't have CPU defined.
#	undef  UNRAVEL_CPU_JIT
#	define UNRAVEL_CPU_JIT 1
#	define UNRAVEL_CACHE_LINE_SIZE 64
#endif //

#if defined(__x86_64__)    \
 || defined(_M_X64)        \
 || defined(__aarch64__)   \
 || defined(__64BIT__)     \
 || defined(__mips64)      \
 || defined(__powerpc64__) \
 || defined(__ppc64__)     \
 || defined(__LP64__)
#	undef  UNRAVEL_ARCH_64BIT
#	define UNRAVEL_ARCH_64BIT 64
#else
#	undef  UNRAVEL_ARCH_32BIT
#	define UNRAVEL_ARCH_32BIT 32
#endif //

#if UNRAVEL_CPU_PPC
// __BIG_ENDIAN__ is gcc predefined macro
#	if defined(__BIG_ENDIAN__)
#		undef  UNRAVEL_CPU_ENDIAN_BIG
#		define UNRAVEL_CPU_ENDIAN_BIG 1
#	else
#		undef  UNRAVEL_CPU_ENDIAN_LITTLE
#		define UNRAVEL_CPU_ENDIAN_LITTLE 1
#	endif
#else
#	undef  UNRAVEL_CPU_ENDIAN_LITTLE
#	define UNRAVEL_CPU_ENDIAN_LITTLE 1
#endif // UNRAVEL_CPU_PPC

// http://sourceforge.net/apps/mediawiki/predef/index.php?title=Operating_Systems
#if defined(_DURANGO) || defined(_XBOX_ONE)
#	undef  UNRAVEL_PLATFORM_XBOXONE
#	define UNRAVEL_PLATFORM_XBOXONE 1
#elif defined(_WIN32) || defined(_WIN64)
// http://msdn.microsoft.com/en-us/library/6sehtctf.aspx
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif // NOMINMAX
//  If _USING_V110_SDK71_ is defined it means we are using the v110_xp or v120_xp toolset.
#	if defined(_MSC_VER) && (_MSC_VER >= 1700) && !defined(_USING_V110_SDK71_)
#		include <winapifamily.h>
#	endif // defined(_MSC_VER) && (_MSC_VER >= 1700) && (!_USING_V110_SDK71_)
#	if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
#		undef  UNRAVEL_PLATFORM_WINDOWS
#		if !defined(WINVER) && !defined(_WIN32_WINNT)
#			if UNRAVEL_ARCH_64BIT
//				When building 64-bit target Win7 and above.
#				define WINVER 0x0601
#				define _WIN32_WINNT 0x0601
#			else
//				Windows Server 2003 with SP1, Windows XP with SP2 and above
#				define WINVER 0x0502
#				define _WIN32_WINNT 0x0502
#			endif // UNRAVEL_ARCH_64BIT
#		endif // !defined(WINVER) && !defined(_WIN32_WINNT)
#		define UNRAVEL_PLATFORM_WINDOWS _WIN32_WINNT
#	else
#		undef  UNRAVEL_PLATFORM_WINRT
#		define UNRAVEL_PLATFORM_WINRT 1
#	endif
#elif defined(__ANDROID__)
// Android compiler defines __linux__
#	include <sys/cdefs.h> // Defines __BIONIC__ and includes android/api-level.h
#	undef  UNRAVEL_PLATFORM_ANDROID
#	define UNRAVEL_PLATFORM_ANDROID __ANDROID_API__
#elif defined(__VCCOREVER__)
// RaspberryPi compiler defines __linux__
#	undef  UNRAVEL_PLATFORM_RPI
#	define UNRAVEL_PLATFORM_RPI 1
#elif  defined(__linux__)
#	undef  UNRAVEL_PLATFORM_LINUX
#	define UNRAVEL_PLATFORM_LINUX 1
#elif  defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) \
    || defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__)
#	undef  UNRAVEL_PLATFORM_IOS
#	define UNRAVEL_PLATFORM_IOS 1
#elif defined(__has_builtin) && __has_builtin(__is_target_os) && __is_target_os(xros)
#	undef  UNRAVEL_PLATFORM_VISIONOS
#	define UNRAVEL_PLATFORM_VISIONOS 1
#elif defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
#	undef  UNRAVEL_PLATFORM_OSX
#	define UNRAVEL_PLATFORM_OSX __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#elif defined(__EMSCRIPTEN__)
#	include <emscripten/version.h>
#	undef  UNRAVEL_PLATFORM_EMSCRIPTEN
#	define UNRAVEL_PLATFORM_EMSCRIPTEN (__EMSCRIPTEN_major__ * 10000 + __EMSCRIPTEN_minor__ * 100 + __EMSCRIPTEN_tiny__)
#elif defined(__ORBIS__)
#	undef  UNRAVEL_PLATFORM_PS4
#	define UNRAVEL_PLATFORM_PS4 1
#elif defined(__PROSPERO__)
#	undef  UNRAVEL_PLATFORM_PS5
#	define UNRAVEL_PLATFORM_PS5 1
#elif  defined(__FreeBSD__)        \
    || defined(__FreeBSD_kernel__) \
    || defined(__NetBSD__)         \
    || defined(__OpenBSD__)        \
    || defined(__DragonFly__)
#	undef  UNRAVEL_PLATFORM_BSD
#	define UNRAVEL_PLATFORM_BSD 1
#elif defined(__GNU__)
#	undef  UNRAVEL_PLATFORM_HURD
#	define UNRAVEL_PLATFORM_HURD 1
#elif defined(__NX__)
#	undef  UNRAVEL_PLATFORM_NX
#	define UNRAVEL_PLATFORM_NX 1
#elif defined(__HAIKU__)
#	undef  UNRAVEL_PLATFORM_HAIKU
#	define UNRAVEL_PLATFORM_HAIKU 1
#endif //

#if !UNRAVEL_CRT_NONE
// https://sourceforge.net/p/predef/wiki/Libraries/
#	if defined(__BIONIC__)
#		undef  UNRAVEL_CRT_BIONIC
#		define UNRAVEL_CRT_BIONIC 1
#	elif defined(__GLIBC__)
#		undef  UNRAVEL_CRT_GLIBC
#		define UNRAVEL_CRT_GLIBC (__GLIBC__ * 10000 + __GLIBC_MINOR__ * 100)
#	elif defined(__apple_build_version__) || defined(__ORBIS__) || defined(__EMSCRIPTEN__) || defined(__llvm__) || defined(__HAIKU__)
#		undef  UNRAVEL_CRT_LIBCXX
#		define UNRAVEL_CRT_LIBCXX 1
#	elif defined(__MINGW32__) || defined(__MINGW64__)
#		undef  UNRAVEL_CRT_MINGW
#		define UNRAVEL_CRT_MINGW 1
#	elif defined(_MSC_VER)
#		undef  UNRAVEL_CRT_MSVC
#		define UNRAVEL_CRT_MSVC 1
#	endif //
#endif // !UNRAVEL_CRT_NONE

///
#define UNRAVEL_PLATFORM_POSIX (0   \
    ||  UNRAVEL_PLATFORM_ANDROID    \
    ||  UNRAVEL_PLATFORM_BSD        \
    ||  UNRAVEL_PLATFORM_EMSCRIPTEN \
    ||  UNRAVEL_PLATFORM_HAIKU      \
    ||  UNRAVEL_PLATFORM_HURD       \
    ||  UNRAVEL_PLATFORM_IOS        \
    ||  UNRAVEL_PLATFORM_LINUX      \
    ||  UNRAVEL_PLATFORM_NX         \
    ||  UNRAVEL_PLATFORM_OSX        \
    ||  UNRAVEL_PLATFORM_PS4        \
    ||  UNRAVEL_PLATFORM_PS5        \
    ||  UNRAVEL_PLATFORM_RPI        \
    ||  UNRAVEL_PLATFORM_VISIONOS   \
    )

///
#define UNRAVEL_PLATFORM_NONE !(0   \
    ||  UNRAVEL_PLATFORM_ANDROID    \
    ||  UNRAVEL_PLATFORM_BSD        \
    ||  UNRAVEL_PLATFORM_EMSCRIPTEN \
    ||  UNRAVEL_PLATFORM_HAIKU      \
    ||  UNRAVEL_PLATFORM_HURD       \
    ||  UNRAVEL_PLATFORM_IOS        \
    ||  UNRAVEL_PLATFORM_LINUX      \
    ||  UNRAVEL_PLATFORM_NX         \
    ||  UNRAVEL_PLATFORM_OSX        \
    ||  UNRAVEL_PLATFORM_PS4        \
    ||  UNRAVEL_PLATFORM_PS5        \
    ||  UNRAVEL_PLATFORM_RPI        \
    ||  UNRAVEL_PLATFORM_VISIONOS   \
    ||  UNRAVEL_PLATFORM_WINDOWS    \
    ||  UNRAVEL_PLATFORM_WINRT      \
    ||  UNRAVEL_PLATFORM_XBOXONE    \
    )

///
#define UNRAVEL_PLATFORM_OS_CONSOLE  (0 \
    ||  UNRAVEL_PLATFORM_NX             \
    ||  UNRAVEL_PLATFORM_PS4            \
    ||  UNRAVEL_PLATFORM_PS5            \
    ||  UNRAVEL_PLATFORM_WINRT          \
    ||  UNRAVEL_PLATFORM_XBOXONE        \
    )

///
#define UNRAVEL_PLATFORM_OS_DESKTOP  (0 \
    ||  UNRAVEL_PLATFORM_BSD            \
    ||  UNRAVEL_PLATFORM_HAIKU          \
    ||  UNRAVEL_PLATFORM_HURD           \
    ||  UNRAVEL_PLATFORM_LINUX          \
    ||  UNRAVEL_PLATFORM_OSX            \
    ||  UNRAVEL_PLATFORM_WINDOWS        \
    )

///
#define UNRAVEL_PLATFORM_OS_EMBEDDED (0 \
    ||  UNRAVEL_PLATFORM_RPI            \
    )

///
#define UNRAVEL_PLATFORM_OS_MOBILE   (0 \
    ||  UNRAVEL_PLATFORM_ANDROID        \
    ||  UNRAVEL_PLATFORM_IOS            \
    )

///
#define UNRAVEL_PLATFORM_OS_WEB      (0 \
    ||  UNRAVEL_PLATFORM_EMSCRIPTEN     \
    )

///
#if UNRAVEL_COMPILER_GCC
#	define UNRAVEL_COMPILER_NAME "GCC "       \
        UNRAVEL_STRINGIZE(__GNUC__) "."       \
        UNRAVEL_STRINGIZE(__GNUC_MINOR__) "." \
        UNRAVEL_STRINGIZE(__GNUC_PATCHLEVEL__)
#elif UNRAVEL_COMPILER_CLANG
#	define UNRAVEL_COMPILER_NAME "Clang "      \
        UNRAVEL_STRINGIZE(__clang_major__) "." \
        UNRAVEL_STRINGIZE(__clang_minor__) "." \
        UNRAVEL_STRINGIZE(__clang_patchlevel__)
#elif UNRAVEL_COMPILER_MSVC
#	if UNRAVEL_COMPILER_MSVC >= 1930 // Visual Studio 2022
#		define UNRAVEL_COMPILER_NAME "MSVC 17.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1920 // Visual Studio 2019
#		define UNRAVEL_COMPILER_NAME "MSVC 16.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1910 // Visual Studio 2017
#		define UNRAVEL_COMPILER_NAME "MSVC 15.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1900 // Visual Studio 2015
#		define UNRAVEL_COMPILER_NAME "MSVC 14.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1800 // Visual Studio 2013
#		define UNRAVEL_COMPILER_NAME "MSVC 12.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1700 // Visual Studio 2012
#		define UNRAVEL_COMPILER_NAME "MSVC 11.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1600 // Visual Studio 2010
#		define UNRAVEL_COMPILER_NAME "MSVC 10.0"
#	elif UNRAVEL_COMPILER_MSVC >= 1500 // Visual Studio 2008
#		define UNRAVEL_COMPILER_NAME "MSVC 9.0"
#	else
#		define UNRAVEL_COMPILER_NAME "MSVC"
#	endif //
#endif // UNRAVEL_COMPILER_

#if UNRAVEL_PLATFORM_ANDROID
#	define UNRAVEL_PLATFORM_NAME "Android " \
        UNRAVEL_STRINGIZE(UNRAVEL_PLATFORM_ANDROID)
#elif UNRAVEL_PLATFORM_BSD
#	define UNRAVEL_PLATFORM_NAME "BSD"
#elif UNRAVEL_PLATFORM_EMSCRIPTEN
#	define UNRAVEL_PLATFORM_NAME "Emscripten "      \
        UNRAVEL_STRINGIZE(__EMSCRIPTEN_major__) "." \
        UNRAVEL_STRINGIZE(__EMSCRIPTEN_minor__) "." \
        UNRAVEL_STRINGIZE(__EMSCRIPTEN_tiny__)
#elif UNRAVEL_PLATFORM_HAIKU
#	define UNRAVEL_PLATFORM_NAME "Haiku"
#elif UNRAVEL_PLATFORM_HURD
#	define UNRAVEL_PLATFORM_NAME "Hurd"
#elif UNRAVEL_PLATFORM_IOS
#	define UNRAVEL_PLATFORM_NAME "iOS"
#elif UNRAVEL_PLATFORM_LINUX
#	define UNRAVEL_PLATFORM_NAME "Linux"
#elif UNRAVEL_PLATFORM_NONE
#	define UNRAVEL_PLATFORM_NAME "None"
#elif UNRAVEL_PLATFORM_NX
#	define UNRAVEL_PLATFORM_NAME "NX"
#elif UNRAVEL_PLATFORM_OSX
#	define UNRAVEL_PLATFORM_NAME "macOS"
#elif UNRAVEL_PLATFORM_PS4
#	define UNRAVEL_PLATFORM_NAME "PlayStation 4"
#elif UNRAVEL_PLATFORM_PS5
#	define UNRAVEL_PLATFORM_NAME "PlayStation 5"
#elif UNRAVEL_PLATFORM_RPI
#	define UNRAVEL_PLATFORM_NAME "RaspberryPi"
#elif UNRAVEL_PLATFORM_VISIONOS
#	define UNRAVEL_PLATFORM_NAME "visionOS"
#elif UNRAVEL_PLATFORM_WINDOWS
#	define UNRAVEL_PLATFORM_NAME "Windows"
#elif UNRAVEL_PLATFORM_WINRT
#	define UNRAVEL_PLATFORM_NAME "WinRT"
#elif UNRAVEL_PLATFORM_XBOXONE
#	define UNRAVEL_PLATFORM_NAME "Xbox One"
#else
#	error "Unknown UNRAVEL_PLATFORM!"
#endif // UNRAVEL_PLATFORM_

#if UNRAVEL_CPU_ARM
#	define UNRAVEL_CPU_NAME "ARM"
#elif UNRAVEL_CPU_JIT
#	define UNRAVEL_CPU_NAME "JIT-VM"
#elif UNRAVEL_CPU_MIPS
#	define UNRAVEL_CPU_NAME "MIPS"
#elif UNRAVEL_CPU_PPC
#	define UNRAVEL_CPU_NAME "PowerPC"
#elif UNRAVEL_CPU_RISCV
#	define UNRAVEL_CPU_NAME "RISC-V"
#elif UNRAVEL_CPU_X86
#	define UNRAVEL_CPU_NAME "x86"
#endif // UNRAVEL_CPU_

#if UNRAVEL_CRT_BIONIC
#	define UNRAVEL_CRT_NAME "Bionic libc"
#elif UNRAVEL_CRT_GLIBC
#	define UNRAVEL_CRT_NAME "GNU C Library"
#elif UNRAVEL_CRT_MSVC
#	define UNRAVEL_CRT_NAME "MSVC C Runtime"
#elif UNRAVEL_CRT_MINGW
#	define UNRAVEL_CRT_NAME "MinGW C Runtime"
#elif UNRAVEL_CRT_LIBCXX
#	define UNRAVEL_CRT_NAME "Clang C Library"
#elif UNRAVEL_CRT_NEWLIB
#	define UNRAVEL_CRT_NAME "Newlib"
#elif UNRAVEL_CRT_NONE
#	define UNRAVEL_CRT_NAME "None"
#else
#	define UNRAVEL_CRT_NAME "Unknown CRT"
#endif // UNRAVEL_CRT_

#if UNRAVEL_ARCH_32BIT
#	define UNRAVEL_ARCH_NAME "32-bit"
#elif UNRAVEL_ARCH_64BIT
#	define UNRAVEL_ARCH_NAME "64-bit"
#endif // UNRAVEL_ARCH_

#if defined(__cplusplus)
#	if UNRAVEL_COMPILER_MSVC && defined(_MSVC_LANG) && _MSVC_LANG != __cplusplus
#		error "When using MSVC you must set /Zc:__cplusplus compiler option."
#	endif // UNRAVEL_COMPILER_MSVC && defined(_MSVC_LANG) && _MSVC_LANG != __cplusplus

#	if   __cplusplus < UNRAVEL_LANGUAGE_CPP17
#		define UNRAVEL_CPP_NAME "C++Unsupported"
#	elif __cplusplus < UNRAVEL_LANGUAGE_CPP20
#		define UNRAVEL_CPP_NAME "C++17"
#	elif __cplusplus < UNRAVEL_LANGUAGE_CPP23
#		define UNRAVEL_CPP_NAME "C++20"
#	else
// See: https://gist.github.com/bkaradzic/2e39896bc7d8c34e042b#orthodox-c
#		define UNRAVEL_CPP_NAME "C++WayTooModern"
#	endif // UNRAVEL_CPP_NAME
#else
#	define UNRAVEL_CPP_NAME "C++Unknown"
#endif // defined(__cplusplus)

#if defined(__cplusplus)

static_assert(__cplusplus >= UNRAVEL_LANGUAGE_CPP17, "\n\n"
    "\t** IMPORTANT! **\n\n"
    "\tC++17 standard support is required to build.\n"
    "\t\n");

// https://releases.llvm.org/
static_assert(!UNRAVEL_COMPILER_CLANG || UNRAVEL_COMPILER_CLANG >= 110000, "\n\n"
    "\t** IMPORTANT! **\n\n"
    "\tMinimum supported Clang version is 11.0 (October 12, 2020).\n"
    "\t\n");

// https://gcc.gnu.org/releases.html
static_assert(!UNRAVEL_COMPILER_GCC || UNRAVEL_COMPILER_GCC >= 80400, "\n\n"
    "\t** IMPORTANT! **\n\n"
    "\tMinimum supported GCC version is 8.4 (March 4, 2020).\n"
    "\t\n");

static_assert(!UNRAVEL_CPU_ENDIAN_BIG, "\n\n"
    "\t** IMPORTANT! **\n\n"
    "\tThe code was not tested for big endian, and big endian CPU is considered unsupported.\n"
    "\t\n");

static_assert(!UNRAVEL_PLATFORM_BSD || !UNRAVEL_PLATFORM_HAIKU || !UNRAVEL_PLATFORM_HURD, "\n\n"
    "\t** IMPORTANT! **\n\n"
    "\tYou're compiling for unsupported platform!\n"
    "\tIf you wish to support this platform, make your own fork, and modify code for _yourself_.\n"
    "\t\n"
    "\tDo not submit PR to main repo, it won't be considered, and it would code rot anyway. I have no ability\n"
    "\tto test on these platforms, and over years there wasn't any serious contributor who wanted to take\n"
    "\tburden of maintaining code for these platforms.\n"
    "\t\n");

#endif // defined(__cplusplus)

