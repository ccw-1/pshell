#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;

int main() {
    fprintf(stderr, "Before mutex lock\n");
    fflush(stderr);
    pthread_mutex_lock(&test_mutex);
    fprintf(stderr, "After mutex lock\n");
    fflush(stderr);
    pthread_mutex_unlock(&test_mutex);
    fprintf(stderr, "After mutex unlock\n");
    fflush(stderr);
    return 0;
}
