#ifndef PSHELL_IO_MULTIPLEXER_H
#define PSHELL_IO_MULTIPLEXER_H

#include <sys/types.h>
#include <stdbool.h>

/**
 * I/O multiplexer for pshell
 * Select-based event loop for handling multiple file descriptors
 */

#define MAX_FDS 1024

typedef enum {
    IO_EVENT_READ = 1,
    IO_EVENT_WRITE = 2,
    IO_EVENT_ERROR = 4
} io_event_type_t;

typedef void (*io_callback_t)(int fd, io_event_type_t events, void *user_data);

typedef struct io_multiplexer io_multiplexer_t;

/**
 * Create I/O multiplexer
 * @return Multiplexer or NULL on error
 */
io_multiplexer_t* io_multiplexer_create(void);

/**
 * Destroy I/O multiplexer
 * @param mux Multiplexer to destroy
 */
void io_multiplexer_destroy(io_multiplexer_t *mux);

/**
 * Add file descriptor to monitor
 * @param mux Multiplexer
 * @param fd File descriptor
 * @param events Events to monitor (IO_EVENT_READ | IO_EVENT_WRITE)
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return true on success
 */
bool io_multiplexer_add(io_multiplexer_t *mux, int fd, io_event_type_t events,
                        io_callback_t callback, void *user_data);

/**
 * Remove file descriptor from monitoring
 * @param mux Multiplexer
 * @param fd File descriptor
 * @return true on success
 */
bool io_multiplexer_remove(io_multiplexer_t *mux, int fd);

/**
 * Modify events for file descriptor
 * @param mux Multiplexer
 * @param fd File descriptor
 * @param events New events to monitor
 * @return true on success
 */
bool io_multiplexer_modify(io_multiplexer_t *mux, int fd, io_event_type_t events);

/**
 * Run event loop (blocking)
 * @param mux Multiplexer
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return Number of events processed or -1 on error
 */
int io_multiplexer_run(io_multiplexer_t *mux, int timeout_ms);

/**
 * Stop event loop
 * @param mux Multiplexer
 */
void io_multiplexer_stop(io_multiplexer_t *mux);

/**
 * Check if multiplexer is running
 * @param mux Multiplexer
 * @return true if running
 */
bool io_multiplexer_is_running(const io_multiplexer_t *mux);

/**
 * Get number of monitored file descriptors
 * @param mux Multiplexer
 * @return Number of FDs
 */
int io_multiplexer_count(const io_multiplexer_t *mux);

#endif /* PSHELL_IO_MULTIPLEXER_H */
