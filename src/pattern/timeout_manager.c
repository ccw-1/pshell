#include "pshell/timeout_manager.h"
#include <sys/time.h>
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
        gettimeofday(&ctx->start_time, NULL);
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

bool timeout_to_timeval(const timeout_ctx_t *ctx, struct timeval *tv)
{
    if (!ctx || !tv) {
        return false;
    }

    if (!ctx->has_timeout) {
        return false;
    }

    int remaining = timeout_remaining(ctx);
    if (remaining <= 0) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    } else {
        tv->tv_sec = remaining / 1000;
        tv->tv_usec = (remaining % 1000) * 1000;
    }

    return true;
}

long timeout_elapsed(const timeout_ctx_t *ctx)
{
    if (!ctx || !ctx->has_timeout) {
        return 0;
    }

    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    long elapsed_ms = (current_time.tv_sec - ctx->start_time.tv_sec) * 1000 +
                     (current_time.tv_usec - ctx->start_time.tv_usec) / 1000;

    return elapsed_ms;
}

void timeout_reset(timeout_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->has_timeout) {
        gettimeofday(&ctx->start_time, NULL);
    }
}
