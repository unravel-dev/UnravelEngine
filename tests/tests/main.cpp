#include "tests.h"

#include <service/service.h>

// Deliberately not service_main.h: this is a console program whose exit code has to be the
// number of failing checks, and that header's main returns the service result instead.
auto main(int argc, char* argv[]) -> int
{
    const int service_result = service_main("tests", argc, argv);
    if(service_result != 0)
    {
        return service_result;
    }

    return unravel::tests::failure_count() == 0 ? 0 : 1;
}
