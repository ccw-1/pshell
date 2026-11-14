#include "pshell/vt100_parser.h"
#include "pshell/screen_buffer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("Testing %s... ", name); \
    fflush(stdout);

#define PASS() \
    printf("PASS\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
    return;

void test_vt100_parser_init(void) {
    TEST("vt100_parser_init");
    
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    assert(parser.state == VT100_STATE_NORMAL);
    assert(parser.param_count == 0);
    
    PASS();
}

void test_vt100_parse_printable(void) {
    TEST("vt100_parse_printable");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    bool result = vt100_parser_feed(&parser, 'A', &seq);
    
    if (!result) {
        FAIL("Expected sequence");
        return;
    }
    
    if (seq.type != VT100_SEQ_PRINT) {
        FAIL("Expected PRINT sequence");
        return;
    }
    
    if (seq.text[0] != 'A') {
        FAIL("Expected 'A'");
        return;
    }
    
    PASS();
}

void test_vt100_parse_cursor_up(void) {
    TEST("vt100_parse_cursor_up");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    const char *input = "\x1B[5A";  /* ESC [ 5 A */
    bool got_seq = false;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            got_seq = true;
            break;
        }
    }
    
    if (!got_seq) {
        FAIL("No sequence parsed");
        return;
    }
    
    if (seq.type != VT100_SEQ_CURSOR_UP) {
        FAIL("Expected CURSOR_UP");
        return;
    }
    
    if (seq.params[0] != 5) {
        FAIL("Expected param 5");
        return;
    }
    
    PASS();
}

void test_vt100_parse_sgr(void) {
    TEST("vt100_parse_sgr");
    
    vt100_parser_t parser;
    vt100_sequence_t seq;
    vt100_parser_init(&parser);
    
    const char *input = "\x1B[1;31m";  /* ESC [ 1 ; 31 m (bold red) */
    bool got_seq = false;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            got_seq = true;
            break;
        }
    }
    
    if (!got_seq) {
        FAIL("No sequence parsed");
        return;
    }
    
    if (seq.type != VT100_SEQ_SGR) {
        FAIL("Expected SGR");
        return;
    }
    
    if (seq.param_count != 2) {
        FAIL("Expected 2 params");
        return;
    }
    
    if (seq.params[0] != 1 || seq.params[1] != 31) {
        FAIL("Expected params 1, 31");
        return;
    }
    
    PASS();
}

void test_screen_buffer_create(void) {
    TEST("screen_buffer_create");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    if (screen->rows != 24 || screen->cols != 80) {
        FAIL("Wrong dimensions");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_put_char(void) {
    TEST("screen_buffer_put_char");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Put some characters */
    screen_buffer_put_char(screen, 'H');
    screen_buffer_put_char(screen, 'e');
    screen_buffer_put_char(screen, 'l');
    screen_buffer_put_char(screen, 'l');
    screen_buffer_put_char(screen, 'o');
    
    /* Check cursor position */
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 0 || col != 5) {
        FAIL("Wrong cursor position");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check content */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "Hello", 5) != 0) {
        FAIL("Wrong content");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_cursor_movement(void) {
    TEST("screen_buffer_cursor_movement");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Move cursor */
    screen_buffer_set_cursor(screen, 5, 10);
    
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 5 || col != 10) {
        FAIL("Wrong cursor position after set");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Move up */
    screen_buffer_cursor_up(screen, 2);
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 3 || col != 10) {
        FAIL("Wrong cursor position after up");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Move forward */
    screen_buffer_cursor_forward(screen, 5);
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 3 || col != 15) {
        FAIL("Wrong cursor position after forward");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_erase(void) {
    TEST("screen_buffer_erase");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    /* Fill first line */
    for (int i = 0; i < 10; i++) {
        screen_buffer_put_char(screen, 'X');
    }
    
    /* Erase from cursor to end of line */
    screen_buffer_set_cursor(screen, 0, 5);
    screen_buffer_erase_line(screen, 0);
    
    /* Check that first 5 chars are still there */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "XXXXX", 5) != 0) {
        FAIL("First 5 chars should remain");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check that rest is cleared */
    for (int i = 5; i < 10; i++) {
        if (line[i] != ' ') {
            FAIL("Rest should be cleared");
            screen_buffer_destroy(screen);
            return;
        }
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

void test_screen_buffer_process_sequence(void) {
    TEST("screen_buffer_process_sequence");
    
    screen_buffer_t *screen = screen_buffer_create(24, 80, 100);
    if (!screen) {
        FAIL("Failed to create screen buffer");
        return;
    }
    
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    /* Parse and process "Hello\n" */
    const char *input = "Hello\n";
    vt100_sequence_t seq;
    
    for (int i = 0; input[i]; i++) {
        if (vt100_parser_feed(&parser, input[i], &seq)) {
            screen_buffer_process_sequence(screen, &seq);
        }
    }
    
    /* Check content */
    char line[256];
    screen_buffer_get_line(screen, 0, line, sizeof(line));
    
    if (strncmp(line, "Hello", 5) != 0) {
        FAIL("Wrong content");
        screen_buffer_destroy(screen);
        return;
    }
    
    /* Check cursor moved to next line */
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    
    if (row != 1) {
        FAIL("Cursor should be on line 1");
        screen_buffer_destroy(screen);
        return;
    }
    
    screen_buffer_destroy(screen);
    PASS();
}

/* Feed text through the parser into the screen */
static void feed_text(screen_buffer_t *screen, vt100_parser_t *parser, const char *text) {
    vt100_sequence_t seq;
    for (int i = 0; text[i]; i++) {
        if (vt100_parser_feed(parser, text[i], &seq)) {
            screen_buffer_process_sequence(screen, &seq);
        }
    }
}

static int scrollback_line_starts(const screen_buffer_t *screen, int idx, const char *prefix) {
    const char_cell_t *cells = screen_buffer_get_scrollback_line(screen, idx);
    if (!cells) return 0;
    for (size_t i = 0; prefix[i]; i++) {
        if (cells[i].ch != prefix[i]) return 0;
    }
    return 1;
}

/* Regression: widening the screen used to leave the scrollback allocated for
 * the old width, so later scrolled-off lines overflowed the heap (daemon
 * segfault after `pshellctl attach` resized the session). Run under ASAN. */
void test_screen_buffer_resize_scrollback(void) {
    TEST("screen_buffer_resize_scrollback");
    
    screen_buffer_t *screen = screen_buffer_create(5, 10, 20);
    if (!screen) {
        FAIL("Failed to create screen buffer");
    }
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    char buf[32];
    for (int i = 0; i < 12; i++) {
        snprintf(buf, sizeof(buf), "L%02d\r\n", i);
        feed_text(screen, &parser, buf);
    }
    int used_before = screen->scrollback_used;
    if (used_before <= 0 || used_before >= 20) {
        screen_buffer_destroy(screen);
        FAIL("Unexpected scrollback_used before resize");
    }
    
    if (screen_buffer_resize(screen, 8, 40) != 0) {
        screen_buffer_destroy(screen);
        FAIL("Resize failed");
    }
    if (screen->scrollback_used != used_before ||
        !scrollback_line_starts(screen, 0, "L00")) {
        screen_buffer_destroy(screen);
        FAIL("Scrollback content lost or reordered by resize");
    }
    
    /* Wrap the (now wider) scrollback several times */
    for (int i = 12; i < 100; i++) {
        snprintf(buf, sizeof(buf), "L%02d\r\n", i);
        feed_text(screen, &parser, buf);
    }
    if (screen->scrollback_used != 20) {
        screen_buffer_destroy(screen);
        FAIL("Scrollback should be full");
    }
    /* Newest scrolled-off line: 100 lines fed, 8 rows on screen (last row is the empty cursor row) */
    if (!scrollback_line_starts(screen, 19, "L92")) {
        screen_buffer_destroy(screen);
        FAIL("Newest scrollback line wrong");
    }
    
    /* Shrinking must keep order and not overflow either */
    if (screen_buffer_resize(screen, 4, 6) != 0 ||
        !scrollback_line_starts(screen, 19, "L92") ||
        !scrollback_line_starts(screen, 0, "L73")) {
        screen_buffer_destroy(screen);
        FAIL("Shrink broke scrollback");
    }
    for (int i = 0; i < 50; i++) {
        feed_text(screen, &parser, "xy\r\n");
    }
    
    screen_buffer_destroy(screen);
    PASS();
}
/* ---- UTF-8 ---- */

/* Print `text` on a fresh screen and compare row 0 with `expect_line` */
static int utf8_row_is(int cols, const char *text, int row, const char *expect_line) {
    screen_buffer_t *screen = screen_buffer_create(4, cols, 10);
    if (!screen) return 0;
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    feed_text(screen, &parser, text);
    char line[256];
    screen_buffer_get_line(screen, row, line, sizeof(line));
    /* Trailing blanks are not interesting */
    size_t n = strlen(line);
    while (n > 0 && line[n - 1] == ' ') line[--n] = '\0';
    int ok = strcmp(line, expect_line) == 0;
    if (!ok) printf("\n  got '%s' expected '%s'\n  ", line, expect_line);
    screen_buffer_destroy(screen);
    return ok;
}

void test_utf8_decode(void) {
    TEST("utf8_decode");
    
    if (!utf8_row_is(20, "\xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae", 0,
                     "\xe2\x95\xad\xe2\x94\x80\xe2\x94\x80\xe2\x95\xae")) {
        FAIL("box drawing characters not preserved");
    }
    if (!utf8_row_is(20, "caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9f\x98\x80x", 0,
                     "caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9f\x98\x80x")) {
        FAIL("2, 3 and 4 byte characters not preserved");
    }
    PASS();
}

void test_utf8_cursor_and_width(void) {
    TEST("utf8_cursor_and_width");
    
    screen_buffer_t *screen = screen_buffer_create(4, 20, 0);
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    
    /* Three 3-byte characters are 3 columns, not 9 */
    feed_text(screen, &parser, "\xe2\x94\x82\xe2\x94\x80\xe2\x94\x82");
    int row, col;
    screen_buffer_get_cursor(screen, &row, &col);
    if (col != 3) {
        screen_buffer_destroy(screen);
        FAIL("cursor advanced per byte instead of per character");
    }
    
    /* Wide characters take two columns */
    feed_text(screen, &parser, "\xe6\x97\xa5\xe6\x9c\xac");
    screen_buffer_get_cursor(screen, &row, &col);
    if (col != 7) {
        screen_buffer_destroy(screen);
        FAIL("wide characters should advance two columns");
    }
    screen_buffer_destroy(screen);
    
    /* Combining mark has no width and is not stored: "e" + U+0301 */
    if (!utf8_row_is(10, "e\xcc\x81x", 0, "ex")) {
        FAIL("combining character should be dropped");
    }
    
    /* Wide character that does not fit in the last column wraps */
    if (!utf8_row_is(5, "abcd\xe6\x97\xa5", 0, "abcd") ||
        !utf8_row_is(5, "abcd\xe6\x97\xa5", 1, "\xe6\x97\xa5")) {
        FAIL("wide character at the last column should wrap");
    }
    PASS();
}

void test_utf8_wide_overwrite(void) {
    TEST("utf8_wide_overwrite");
    
    /* Overwrite the right half of a wide character: "日本" then BS, 'x' */
    if (!utf8_row_is(10, "\xe6\x97\xa5\xe6\x9c\xac\x1b[1Dx", 0, "\xe6\x97\xa5 x")) {
        FAIL("overwriting the right half should blank the left half");
    }
    /* Overwrite the left half: CR, 'x' */
    if (!utf8_row_is(10, "\xe6\x97\xa5\xe6\x9c\xac\rx", 0, "x \xe6\x9c\xac")) {
        FAIL("overwriting the left half should blank the right half");
    }
    /* Erase from the middle of a wide character */
    if (!utf8_row_is(10, "a\xe6\x97\xa5" "b\x1b[1;3H\x1b[K", 0, "a")) {
        FAIL("erase starting inside a wide character should remove it");
    }
    PASS();
}

void test_utf8_invalid(void) {
    TEST("utf8_invalid");
    
    /* Stray continuation byte, 0xFF */
    if (!utf8_row_is(10, "a\x80" "b\xff" "c", 0, "a\xef\xbf\xbd" "b\xef\xbf\xbd" "c")) {
        FAIL("stray bytes should print U+FFFD");
    }
    /* Truncated sequence followed by ASCII: the ASCII byte must survive */
    if (!utf8_row_is(10, "\xe2\x94" "A", 0, "\xef\xbf\xbd" "A")) {
        FAIL("truncated sequence should print U+FFFD then continue");
    }
    /* Overlong encoding of NUL and of '/' */
    if (!utf8_row_is(10, "\xc0\x80\xe0\x80\xaf", 0, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd")) {
        FAIL("overlong encodings should be rejected");
    }
    /* UTF-16 surrogate U+D800 (ED A0 80) */
    if (!utf8_row_is(10, "\xed\xa0\x80" "z", 0, "\xef\xbf\xbd" "z")) {
        FAIL("surrogates should be rejected");
    }
    /* Escape sequence interrupts a partial character */
    if (!utf8_row_is(10, "\xe2\x94\x1b[1;4Hq", 0, "\xef\xbf\xbd  q")) {
        FAIL("escape sequence should end a partial character");
    }
    PASS();
}

void test_utf8_output_bounds(void) {
    TEST("utf8_output_bounds");
    
    screen_buffer_t *screen = screen_buffer_create(2, 10, 0);
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    feed_text(screen, &parser, "\xc3\xa9\xc3\xa9\xc3\xa9");
    
    /* 5 bytes hold two 2-byte characters + NUL; never half of one */
    char small[5];
    memset(small, 'Z', sizeof(small));
    size_t n = screen_buffer_get_line(screen, 0, small, sizeof(small));
    if (n != 4 || strcmp(small, "\xc3\xa9\xc3\xa9") != 0) {
        screen_buffer_destroy(screen);
        FAIL("line should be cut on a character boundary");
    }
    
    char text[4];
    n = screen_buffer_get_text(screen, text, sizeof(text));
    if (n != 3 || strcmp(text, "\xc3\xa9\n") != 0) {
        screen_buffer_destroy(screen);
        FAIL("text should be cut on a character boundary");
    }
    screen_buffer_destroy(screen);
    PASS();
}

void test_utf8_scrollback(void) {
    TEST("utf8_scrollback");
    
    screen_buffer_t *screen = screen_buffer_create(2, 6, 5);
    vt100_parser_t parser;
    vt100_parser_init(&parser);
    feed_text(screen, &parser, "\xe2\x94\x82hi\xe6\x97\xa5\r\nline2\r\nline3\r\n");
    
    const char_cell_t *cells = screen_buffer_get_scrollback_line(screen, 0);
    if (!cells) {
        screen_buffer_destroy(screen);
        FAIL("no scrollback line");
    }
    char line[64];
    screen_buffer_cells_to_utf8(cells, screen->cols, line, sizeof(line));
    size_t len = strlen(line);
    while (len > 0 && line[len - 1] == ' ') line[--len] = '\0';
    if (strcmp(line, "\xe2\x94\x82hi\xe6\x97\xa5") != 0) {
        screen_buffer_destroy(screen);
        FAIL("scrollback line not UTF-8");
    }
    screen_buffer_destroy(screen);
    PASS();
}



int main(void) {
    printf("=== Terminal Emulation Tests ===\n\n");
    
    /* VT100 Parser Tests */
    test_vt100_parser_init();
    test_vt100_parse_printable();
    test_vt100_parse_cursor_up();
    test_vt100_parse_sgr();
    
    /* Screen Buffer Tests */
    test_screen_buffer_create();
    test_screen_buffer_put_char();
    test_screen_buffer_cursor_movement();
    test_screen_buffer_erase();
    test_screen_buffer_process_sequence();
    test_screen_buffer_resize_scrollback();
    /* UTF-8 */
    test_utf8_decode();
    test_utf8_cursor_and_width();
    test_utf8_wide_overwrite();
    test_utf8_invalid();
    test_utf8_output_bounds();
    test_utf8_scrollback();
    

    
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
