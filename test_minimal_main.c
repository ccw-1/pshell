#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    write(2, "START\n", 6);
    sleep(1);
    write(2, "END\n", 4);
    return 0;
}
