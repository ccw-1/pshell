#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;

int main() {
    fprintf(stderr, "START\n");
    fflush(stderr);
    
    fprintf(stderr, "Locking mutex...\n");
    fflush(stderr);
    
    pthread_mutex_lock(&test_mutex);
    
    fprintf(stderr, "Locked\n");
    fflush(stderr);
    
    pthread_mutex_unlock(&test_mutex);
    
    fprintf(stderr, "END\n");
    fflush(stderr);
    
    return 0;
}
