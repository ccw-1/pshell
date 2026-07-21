#include <stdlib.h>

/* Stub for get_default_shell() - used only by unit tests */
const char* get_default_shell(void) {
    const char *shell = getenv("SHELL");
    return shell ? shell : "/bin/sh";
}
