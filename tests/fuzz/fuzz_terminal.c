/* Terminal fuzz harness (not part of "make test").
 * Build and run:
 *   cc -fsanitize=address,undefined -fno-sanitize-recover=undefined -g -O1 -Iinclude \
 *      -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE tests/fuzz/fuzz_terminal.c \
 *      src/terminal/*.c src/utils/logger.c -o /tmp/fuzz_terminal -pthread
 *   /tmp/fuzz_terminal 50000
 */
#include "pshell/vt100_parser.h"
#include "pshell/screen_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned long long rs = 88172645463325252ULL;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }

static const char *fin = "ABCDEFGHJKLMPSTXZ@`abcdefghlmnrstu";
static const char *big[] = {"0","1","2","3","5","7","38","48","999","65535","2147483647","4294967295","99999999999","-1"};

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    rs ^= (unsigned long long)time(NULL) * 2654435761u;
    for (long it = 0; it < iters; it++) {
        int rows = 1 + rnd() % 30, cols = 1 + rnd() % 60, sb = rnd() % 20;
        screen_buffer_t *s = screen_buffer_create(rows, cols, sb);
        vt100_parser_t p; vt100_parser_init(&p);
        vt100_sequence_t seq;
        int n = 50 + rnd() % 400;
        for (int i = 0; i < n; i++) {
            char buf[128]; size_t len = 0;
            switch (rnd() % 8) {
              case 0: case 1: buf[len++] = 'a' + rnd() % 26; break;
              case 2: buf[len++] = (char)(rnd() % 256); break;
              case 3: { const char *u[] = {"\xc3\xa9","\xcc\x81","\xe6\x97\xa5","\xf0\x9f\x91\x8d","\xf0\x9f\x8f\xbd","\xe2\x80\x8d","\xef\xb8\x8f","\xf0\x9f\x91\xa8"}; const char *x=u[rnd()%8]; len = strlen(x); memcpy(buf,x,len);} break;
              case 4: buf[len++] = "\r\n\t\b\x07\x0b\x0c"[rnd()%7]; break;
              default: {
                len += snprintf(buf+len, sizeof buf-len, "\x1b[");
                if (rnd()%4==0) buf[len++]='?';
                int np = rnd()%7;
                for (int k=0;k<np;k++){ len += snprintf(buf+len, sizeof buf-len, "%s%s", k?";":"", big[rnd()%14]); }
                buf[len++] = fin[rnd()%strlen(fin)];
              }
            }
            for (size_t k = 0; k < len; k++) {
                if (vt100_parser_feed(&p, buf[k], &seq)) screen_buffer_process_sequence(s, &seq);
            }
            if (rnd() % 97 == 0) screen_buffer_resize(s, 1 + rnd()%30, 1 + rnd()%60);
            if (rnd() % 53 == 0) {
                size_t l; char *o = screen_buffer_render_ansi(s, &l); free(o);
                size_t sz = screen_buffer_text_size(s); char *t = malloc(sz); screen_buffer_get_text(s, t, sz); free(t);
                for (int u = 0; u < s->scrollback_used; u++) { char ln[8192]; screen_buffer_cells_to_utf8(s, screen_buffer_get_scrollback_line(s,u), s->cols, ln, sizeof ln); }
            }
        }
        screen_buffer_destroy(s);
    }
    printf("ok %ld iterations\n", iters);
    return 0;
}
