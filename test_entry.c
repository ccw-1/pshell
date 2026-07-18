#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *msg = "ENTRY POINT REACHED\n";
    write(2, msg, strlen(msg));
    _exit(0);
}
