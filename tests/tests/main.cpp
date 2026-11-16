#include "tests.h"

#include <service/service.h>

// Deliberately not service_main.h: this is a console program whose exit code says how the run went, and that header's
// main returns the service result instead.
auto main(int argc, char* argv[]) -> int
{
    // Before the engine boots: a scheduler (CTest) lists the suites and should not wait for a boot to do it.
    if(unravel::tests::print_list_if_asked(argc, argv))
    {
        return 0;
    }
    unravel::tests::set_command_line(argc, argv);
    const int service_result = service_main("tests", argc, argv);
    if(service_result != 0)
    {
        return service_result;
    }

    return unravel::tests::exit_code();
}
