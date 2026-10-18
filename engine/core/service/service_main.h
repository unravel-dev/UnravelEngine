#pragma once
#include "service.h"

#include <ospp/main.h>

#ifndef ENTRY_APP_NAME
#error "You must define ENTRY_APP_NAME, e.g. #define ENTRY_APP_NAME myapp"
#define ENTRY_APP_NAME "app"
#endif

// The platform entry points come from ospp; they all start the named service.
auto os_main(int argc, char* argv[]) -> int
{
    return service_main(ENTRY_APP_NAME, argc, argv);
}
