#include "pshell/timeout_manager.h"
#include <time.h>
#include <string.h>

void timeout_init(timeout_ctx_t *ctx, int timeout_ms)
{
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(timeout_ctx_t));
    ctx->timeout_ms = timeout_ms;
    ctx->has_timeout = (timeout_ms > 0);
    
    if (ctx->has_timeout) {
        clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    }
}

bool timeout_expired(const timeout_ctx_t *ctx)
{
    if (!ctx || !ctx->has_timeout) {
        return false;
    }

    return timeout_remaining(ctx) <= 0;
}

int timeout_remaining(const timeout_ctx_t *ctx)
{
    if (!ctx || !ctx->has_timeout) {
        return -1; /* No timeout */
    }

    long elapsed = timeout_elapsed(ctx);
    int remaining = ctx->timeout_ms - elapsed;
    
    return remaining > 0 ? remaining : 0;
}

bool timeout_to_timeval(const timeout_ctx_t *ctx, struct timespec *ts)
{
    if (!ctx || !ts) {
        return false;
    }

    if (!ctx->has_timeout) {
        return false;
    }

    int remaining = timeout_remaining(ctx);
    if (remaining <= 0) {
        ts->tv_sec = 0;
        ts->tv_nsec = 0;
    } else {
        ts->tv_sec = remaining / 1000;
        ts->tv_nsec = (remaining % 1000) * 1000000;
    }

    return true;
}

long timeout_elapsed(const timeout_ctx_t *ctx)
{
    if (!ctx || !ctx->has_timeout) {
        return 0;
    }

    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);

    long elapsed_ms = (current_time.tv_sec - ctx->start_time.tv_sec) * 1000 +
                     (current_time.tv_nsec - ctx->start_time.tv_nsec) / 1000000;

    return elapsed_ms;
}

void timeout_reset(timeout_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->has_timeout) {
        clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);
    }
}
