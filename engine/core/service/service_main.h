#pragma once
#include "service.h"

#ifndef ENTRY_APP_NAME
#error "You must define ENTRY_APP_NAME, e.g. #define ENTRY_APP_NAME myapp"
#define ENTRY_APP_NAME "app"
#endif

// 1) Standard Unix-style entry:
int main(int argc, char* argv[])
{
    return service_main(ENTRY_APP_NAME, argc, argv);
}

#if defined(_WIN32)

// these typedefs/macros let us declare WinMain/wWinMain without pulling in windows.h
#ifndef WINAPI
#define WINAPI __stdcall
#endif

typedef struct HINSTANCE__* HINSTANCE;
typedef char* LPSTR;
typedef wchar_t* PWSTR;

// 2) Unicode console apps on MSVC use wmain()
#if defined(_MSC_VER) && defined(UNICODE)
extern "C" int wmain(int argc, wchar_t* wargv[], wchar_t* wenvp)
{
    (void)wenvp;
    // __argc/__argv always exist in MSVC CRT, even under UNICODE
    return service_main(ENTRY_APP_NAME, __argc, __argv);
}
#endif

// 3) GUI apps on Windows look for WinMain or wWinMain
#ifdef __cplusplus
extern "C"
{
#endif

#if defined(UNICODE)
    int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR lpCmdLine, int nShowCmd)
#else
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd)
#endif
    {
        (void)hInst;
        (void)hPrev;
        (void)lpCmdLine;
        (void)nShowCmd;
        // again, forward to service_main with ANSI argv
        return service_main(ENTRY_APP_NAME, __argc, __argv);
    }

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _WIN32
