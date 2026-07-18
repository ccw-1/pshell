#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

__attribute__((constructor))
static void init_func(void) {
    fprintf(stderr, "Constructor called\n");
    fflush(stderr);
}

int main() {
    fprintf(stderr, "main() started\n");
    fflush(stderr);
    
    pthread_mutex_lock(&g_mutex);
    fprintf(stderr, "mutex locked\n");
    fflush(stderr);
    pthread_mutex_unlock(&g_mutex);
    
    fprintf(stderr, "main() done\n");
    fflush(stderr);
    return 0;
}
