#include "pshell/screen_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "pshell/utf8.h"

/* Helper: Get cell pointer */
static inline char_cell_t *get_cell(screen_buffer_t *screen, int row, int col) {
    if (row < 0 || row >= screen->rows || col < 0 || col >= screen->cols) {
        return NULL;
    }
    return &screen->cells[row * screen->cols + col];
}

/* Helper: Clear cell */
static inline void clear_cell(char_cell_t *cell) {
    cell->ch = ' ';
    memset(&cell->attr, 0, sizeof(cell->attr));
    cell->attr.fg_color = COLOR_DEFAULT;
    cell->attr.bg_color = COLOR_DEFAULT;
}

/* Helper: Before overwriting (row, col), blank the other half of a
 * double-width character that the write would otherwise leave orphaned. */
static void break_wide(screen_buffer_t *screen, int row, int col) {
    char_cell_t *cell = get_cell(screen, row, col);
    if (!cell) return;
    if (cell->ch == CELL_CONTINUATION) {
        char_cell_t *prev = get_cell(screen, row, col - 1);
        if (prev) clear_cell(prev);
    }
    char_cell_t *next = get_cell(screen, row, col + 1);
    if (next && next->ch == CELL_CONTINUATION) clear_cell(next);
}

/* Helper: An erase of columns [from, to] must not leave half a wide
 * character at either edge. */
static void fix_wide_edges(screen_buffer_t *screen, int row, int from, int to) {
    char_cell_t *first = get_cell(screen, row, from);
    if (first && first->ch == CELL_CONTINUATION) {
        char_cell_t *lead = get_cell(screen, row, from - 1);
        if (lead) clear_cell(lead);
    }
    char_cell_t *after = get_cell(screen, row, to + 1);
    if (after && after->ch == CELL_CONTINUATION) clear_cell(after);
}

/* Helper: Clamp cursor to screen bounds */
static void clamp_cursor(screen_buffer_t *screen) {
    if (screen->cursor.row < 0) screen->cursor.row = 0;
    if (screen->cursor.row >= screen->rows) screen->cursor.row = screen->rows - 1;
    if (screen->cursor.col < 0) screen->cursor.col = 0;
    if (screen->cursor.col >= screen->cols) screen->cursor.col = screen->cols - 1;
}

/* Helper: Add line to scrollback */
static void add_to_scrollback(screen_buffer_t *screen, const char_cell_t *line) {
    if (screen->scrollback_size == 0) return;
    
    /* Copy line to scrollback circular buffer */
    int pos = screen->scrollback_pos;
    memcpy(&screen->scrollback[pos * screen->cols], line, screen->cols * sizeof(char_cell_t));
    
    /* Update position */
    screen->scrollback_pos = (pos + 1) % screen->scrollback_size;
    
    /* Update used count */
    if (screen->scrollback_used < screen->scrollback_size) {
        screen->scrollback_used++;
    }
}

screen_buffer_t *screen_buffer_create(int rows, int cols, int scrollback_lines) {
    /* Defense in depth: callers validate ranges, but a wrapped int
     * multiplication (rows*cols) would otherwise allocate a tiny buffer
     * while the struct claims the full dimensions. */
    if (rows <= 0 || cols <= 0 || scrollback_lines < 0) {
        return NULL;
    }
    if ((size_t)rows > SIZE_MAX / (size_t)cols / sizeof(char_cell_t)) {
        return NULL;
    }
    if (scrollback_lines > 0 &&
        (size_t)scrollback_lines > SIZE_MAX / (size_t)cols / sizeof(char_cell_t)) {
        return NULL;
    }
    
    screen_buffer_t *screen = calloc(1, sizeof(screen_buffer_t));
    if (!screen) return NULL;
    
    screen->rows = rows;
    screen->cols = cols;
    screen->scrollback_size = scrollback_lines;
    screen->last_row = -1;
    
    /* Allocate screen cells */
    screen->cells = calloc(rows * cols, sizeof(char_cell_t));
    if (!screen->cells) {
        free(screen);
        return NULL;
    }
    
    /* Allocate scrollback buffer */
    if (scrollback_lines > 0) {
        screen->scrollback = calloc(scrollback_lines * cols, sizeof(char_cell_t));
        if (!screen->scrollback) {
            free(screen->cells);
            free(screen);
            return NULL;
        }
    }
    
    /* Allocate tab stops */
    screen->tab_stops = calloc(cols, sizeof(bool));
    if (!screen->tab_stops) {
        free(screen->scrollback);
        free(screen->cells);
        free(screen);
        return NULL;
    }
    
    /* Initialize */
    screen_buffer_clear(screen);
    screen_buffer_reset_tabs(screen);
    
    /* Set scroll region to full screen */
    screen->scroll_top = 0;
    screen->scroll_bottom = rows - 1;
    
    /* Default modes */
    screen->auto_wrap = true;
    screen->origin_mode = false;
    screen->insert_mode = false;
    
    return screen;
}

void screen_buffer_destroy(screen_buffer_t *screen) {
    if (!screen) return;
    free(screen->cells);
    free(screen->scrollback);
    free(screen->tab_stops);
    free(screen->clusters);
    free(screen);
}

int screen_buffer_resize(screen_buffer_t *screen, int rows, int cols) {
    if (rows <= 0 || cols <= 0) return -1;
    
    /* Check for integer overflow in allocation size */
    if ((size_t)rows > SIZE_MAX / (size_t)cols / sizeof(char_cell_t)) {
        return -1;
    }

    /* Allocate new cells */
    char_cell_t *new_cells = calloc((size_t)rows * cols, sizeof(char_cell_t));
    if (!new_cells) return -1;
    
    /* Copy old content (as much as fits) */
    int copy_rows = (rows < screen->rows) ? rows : screen->rows;
    int copy_cols = (cols < screen->cols) ? cols : screen->cols;
    
    for (int r = 0; r < copy_rows; r++) {
        for (int c = 0; c < copy_cols; c++) {
            new_cells[r * cols + c] = screen->cells[r * screen->cols + c];
        }
        /* Clear remaining columns in new buffer */
        for (int c = copy_cols; c < cols; c++) {
            clear_cell(&new_cells[r * cols + c]);
        }
    }
    
    /* Clear remaining rows in new buffer */
    for (int r = copy_rows; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            clear_cell(&new_cells[r * cols + c]);
        }
    }
    
    /* Rebuild the scrollback for the new width. Its rows are `cols` cells
     * wide, so keeping the old buffer after a resize makes
     * add_to_scrollback()/get_scrollback_line() index past the allocation
     * when the screen gets wider. Lines are linearised oldest-first. */
    char_cell_t *new_scrollback = NULL;
    int new_sb_pos = 0;
    if (screen->scrollback_size > 0) {
        if ((size_t)screen->scrollback_size > SIZE_MAX / (size_t)cols / sizeof(char_cell_t)) {
            free(new_cells);
            return -1;
        }
        new_scrollback = calloc((size_t)screen->scrollback_size * cols, sizeof(char_cell_t));
        if (!new_scrollback) {
            free(new_cells);
            return -1;
        }
        for (int i = 0; i < screen->scrollback_used; i++) {
            int src = (screen->scrollback_pos - screen->scrollback_used + i +
                       screen->scrollback_size) % screen->scrollback_size;
            const char_cell_t *old_line = &screen->scrollback[(size_t)src * screen->cols];
            char_cell_t *new_line = &new_scrollback[(size_t)i * cols];
            for (int c = 0; c < copy_cols; c++) {
                new_line[c] = old_line[c];
            }
            for (int c = copy_cols; c < cols; c++) {
                clear_cell(&new_line[c]);
            }
        }
        new_sb_pos = screen->scrollback_used % screen->scrollback_size;
    }
    
    /* Reallocate tab stops */
    bool *new_tabs = calloc(cols, sizeof(bool));
    if (!new_tabs) {
        free(new_scrollback);
        free(new_cells);
        return -1;
    }
    
    /* Copy old tab stops */
    for (int c = 0; c < copy_cols && c < cols; c++) {
        new_tabs[c] = screen->tab_stops[c];
    }
    
    /* Set default tabs for new columns */
    for (int c = copy_cols; c < cols; c++) {
        new_tabs[c] = (c % 8 == 0);
    }
    
    /* Replace old buffers */
    free(screen->cells);
    free(screen->tab_stops);
    free(screen->scrollback);
    screen->cells = new_cells;
    screen->tab_stops = new_tabs;
    screen->scrollback = new_scrollback;
    screen->scrollback_pos = new_sb_pos;
    screen->rows = rows;
    screen->cols = cols;
    screen->last_row = -1;
    
    /* Adjust cursor */
    clamp_cursor(screen);
    
    /* Adjust scroll region */
    screen->scroll_bottom = rows - 1;
    if (screen->scroll_top >= rows) {
        screen->scroll_top = 0;
    }
    
    return 0;
}

void screen_buffer_clear(screen_buffer_t *screen) {
    for (int i = 0; i < screen->rows * screen->cols; i++) {
        clear_cell(&screen->cells[i]);
    }
    
    screen->last_row = -1;
    screen->cursor.row = 0;
    screen->cursor.col = 0;
    screen->cursor.visible = true;
    memset(&screen->cursor.attr, 0, sizeof(screen->cursor.attr));
    screen->cursor.attr.fg_color = COLOR_DEFAULT;
    screen->cursor.attr.bg_color = COLOR_DEFAULT;
}

/* Helper: Move to the start of the next line (auto-wrap), scrolling at the
 * bottom of the scroll region. */
static void wrap_to_next_line(screen_buffer_t *screen) {
    screen->cursor.col = 0;
    screen->cursor.row++;
    if (screen->cursor.row > screen->scroll_bottom) {
        screen_buffer_scroll_up(screen, 1);
        screen->cursor.row = screen->scroll_bottom;
        /* The content moved up one row */
        if (screen->last_row > 0) screen->last_row--; else screen->last_row = -1;
    }
}

/* ---- Combining marks and joined sequences ---- */

/* Code points that belong to the character before them instead of taking a
 * cell: combining marks and other zero-width characters (not controls), and
 * emoji skin tone modifiers. */
static bool is_attaching(uint32_t cp) {
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return true;
    return cp >= 0xA0 && utf8_cp_width(cp) == 0;
}

/* Return the pool index of the sequence, adding it if new; -1 if the pool is full */
static int intern_cluster(screen_buffer_t *screen, const uint32_t *cps, int count) {
    for (int i = 0; i < screen->cluster_count; i++) {
        const cluster_t *cl = &screen->clusters[i];
        if (cl->count == count && memcmp(cl->cps, cps, (size_t)count * sizeof(uint32_t)) == 0) {
            return i;
        }
    }
    if (screen->cluster_count >= CLUSTER_POOL_MAX) return -1;
    if (screen->cluster_count == screen->cluster_cap) {
        int cap = screen->cluster_cap ? screen->cluster_cap * 2 : 64;
        cluster_t *p = realloc(screen->clusters, (size_t)cap * sizeof(cluster_t));
        if (!p) return -1;
        screen->clusters = p;
        screen->cluster_cap = cap;
    }
    cluster_t *cl = &screen->clusters[screen->cluster_count];
    memset(cl, 0, sizeof(*cl));
    memcpy(cl->cps, cps, (size_t)count * sizeof(uint32_t));
    cl->count = (uint8_t)count;
    return screen->cluster_count++;
}

int screen_buffer_cell_codepoints(const screen_buffer_t *screen, const char_cell_t *cell,
                                  uint32_t *out) {
    if (cell->ch == CELL_CONTINUATION) return 0;
    if (cell->ch & CELL_CLUSTER_FLAG) {
        uint32_t idx = cell->ch & ~CELL_CLUSTER_FLAG;
        if ((int)idx < screen->cluster_count) {
            const cluster_t *cl = &screen->clusters[idx];
            memcpy(out, cl->cps, cl->count * sizeof(uint32_t));
            return cl->count;
        }
        out[0] = UTF8_REPLACEMENT;
        return 1;
    }
    out[0] = cell->ch;
    return 1;
}

/* The cell of the last printed character, if the cursor is still right
 * behind it (nothing has moved the cursor or changed the screen since) */
static char_cell_t *attach_target(screen_buffer_t *screen) {
    if (screen->last_row < 0) return NULL;
    int after = screen->last_col + screen->last_width;
    bool behind;
    if (after < screen->cols) {
        behind = screen->cursor.row == screen->last_row && screen->cursor.col == after;
    } else if (screen->auto_wrap) {
        behind = screen->cursor.row == screen->last_row + 1 && screen->cursor.col == 0;
    } else {
        behind = screen->cursor.row == screen->last_row &&
                 screen->cursor.col == screen->cols - 1;
    }
    if (!behind) return NULL;
    char_cell_t *cell = get_cell(screen, screen->last_row, screen->last_col);
    if (!cell || cell->ch == CELL_CONTINUATION) return NULL;
    return cell;
}

/* Append cp to the cell's cluster. Silently drops it if the cluster is full. */
static void cluster_append(screen_buffer_t *screen, char_cell_t *cell, uint32_t cp) {
    uint32_t cps[CLUSTER_MAX_CPS];
    int n = screen_buffer_cell_codepoints(screen, cell, cps);
    if (n <= 0 || n >= CLUSTER_MAX_CPS) return;
    cps[n++] = cp;
    int idx = intern_cluster(screen, cps, n);
    if (idx >= 0) cell->ch = CELL_CLUSTER_FLAG | (uint32_t)idx;
}

/* U+FE0F after a narrow emoji-capable character: show it as a 2-column emoji,
 * taking the next cell (when there is one and the cursor is right behind). */
static void widen_last_cell(screen_buffer_t *screen, char_cell_t *target) {
    int col = screen->last_col + 1;
    if (screen->last_width != 1 || col >= screen->cols) return;
    if (screen->cursor.row != screen->last_row || screen->cursor.col != col) return;
    char_cell_t *right = get_cell(screen, screen->last_row, col);
    if (!right) return;
    break_wide(screen, screen->last_row, col);
    right->ch = CELL_CONTINUATION;
    right->attr = target->attr;
    screen->last_width = 2;
    screen->cursor.col++;
    if (screen->cursor.col >= screen->cols) {
        if (screen->auto_wrap) {
            wrap_to_next_line(screen);
        } else {
            screen->cursor.col = screen->cols - 1;
        }
    }
}

void screen_buffer_put_codepoint(screen_buffer_t *screen, uint32_t cp) {
    int width = utf8_cp_width(cp);
    if (is_attaching(cp)) {
        char_cell_t *target = attach_target(screen);
        if (target) {
            uint32_t cps[CLUSTER_MAX_CPS];
            int n = screen_buffer_cell_codepoints(screen, target, cps);
            cluster_append(screen, target, cp);
            if (cp == 0xFE0F && n == 1 && utf8_is_emoji_vs_base(cps[0])) {
                widen_last_cell(screen, target);
            }
        }
        return;                 /* combining / zero-width: no cell of its own */
    }
    if (width == 0) return;     /* control character */
    
    /* After a zero width joiner the next character joins the previous one */
    {
        char_cell_t *target = attach_target(screen);
        if (target) {
            uint32_t cps[CLUSTER_MAX_CPS];
            int n = screen_buffer_cell_codepoints(screen, target, cps);
            if (n > 0 && cps[n - 1] == 0x200D) {
                cluster_append(screen, target, cp);
                return;
            }
        }
    }
    /* The second regional indicator of a pair joins the first (a flag) */
    if (utf8_is_regional_indicator(cp)) {
        char_cell_t *target = attach_target(screen);
        if (target) {
            uint32_t cps[CLUSTER_MAX_CPS];
            if (screen_buffer_cell_codepoints(screen, target, cps) == 1 &&
                utf8_is_regional_indicator(cps[0])) {
                cluster_append(screen, target, cp);
                return;
            }
        }
    }
    if (width == 2 && screen->cols < 2) {
        cp = '?';
        width = 1;
    }
    
    /* A wide character does not fit in the last column: wrap first */
    if (width == 2 && screen->cursor.col == screen->cols - 1) {
        if (!screen->auto_wrap) return;
        wrap_to_next_line(screen);
    }
    
    char_cell_t *cell = get_cell(screen, screen->cursor.row, screen->cursor.col);
    if (!cell) return;
    
    /* Insert mode: shift characters right */
    if (screen->insert_mode && screen->cursor.col < screen->cols - width) {
        memmove(cell + width, cell,
                (screen->cols - screen->cursor.col - width) * sizeof(char_cell_t));
    }
    
    /* Do not leave half of an overwritten wide character behind */
    break_wide(screen, screen->cursor.row, screen->cursor.col);
    if (width == 2) {
        break_wide(screen, screen->cursor.row, screen->cursor.col + 1);
    }
    
    /* Write character */
    cell->ch = cp;
    cell->attr = screen->cursor.attr;
    screen->last_row = screen->cursor.row;
    screen->last_col = screen->cursor.col;
    screen->last_width = width;
    if (width == 2) {
        char_cell_t *right = get_cell(screen, screen->cursor.row, screen->cursor.col + 1);
        right->ch = CELL_CONTINUATION;
        right->attr = screen->cursor.attr;
    }
    
    /* Advance cursor */
    screen->cursor.col += width;
    
    /* Handle wrap */
    if (screen->cursor.col >= screen->cols) {
        if (screen->auto_wrap) {
            wrap_to_next_line(screen);
        } else {
            screen->cursor.col = screen->cols - 1;
        }
    }
}

/* Helper: Abandon a partially received UTF-8 sequence */
static void utf8_flush(screen_buffer_t *screen) {
    if (screen->utf8_need > 0) {
        screen->utf8_need = 0;
        screen_buffer_put_codepoint(screen, UTF8_REPLACEMENT);
    }
}

void screen_buffer_put_char(screen_buffer_t *screen, char ch) {
    unsigned char b = (unsigned char)ch;
    
    if (screen->utf8_need > 0) {
        if ((b & 0xC0) == 0x80) {
            screen->utf8_cp = (screen->utf8_cp << 6) | (b & 0x3F);
            if (--screen->utf8_need == 0) {
                uint32_t cp = screen->utf8_cp;
                if (cp < screen->utf8_min || cp > 0x10FFFF ||
                    (cp >= 0xD800 && cp <= 0xDFFF)) {
                    cp = UTF8_REPLACEMENT;   /* overlong, surrogate, out of range */
                }
                screen_buffer_put_codepoint(screen, cp);
            }
            return;
        }
        /* Truncated sequence: replace it, then handle this byte afresh */
        utf8_flush(screen);
    }
    
    if (b < 0x80) {
        screen_buffer_put_codepoint(screen, b);
    } else if (b >= 0xC2 && b <= 0xDF) {
        screen->utf8_need = 1;
        screen->utf8_cp = b & 0x1F;
        screen->utf8_min = 0x80;
    } else if (b >= 0xE0 && b <= 0xEF) {
        screen->utf8_need = 2;
        screen->utf8_cp = b & 0x0F;
        screen->utf8_min = 0x800;
    } else if (b >= 0xF0 && b <= 0xF4) {
        screen->utf8_need = 3;
        screen->utf8_cp = b & 0x07;
        screen->utf8_min = 0x10000;
    } else {
        /* Stray continuation byte, C0/C1 or F5..FF */
        screen_buffer_put_codepoint(screen, UTF8_REPLACEMENT);
    }
}

/* Clamp a movement count so cursor arithmetic cannot overflow. */
static int clamp_count(int n, int limit) {
    if (n < 0) return 0;
    return n > limit ? limit : n;
}

void screen_buffer_cursor_up(screen_buffer_t *screen, int n) {
    screen->cursor.row -= clamp_count(n, screen->rows);
    if (screen->cursor.row < screen->scroll_top) {
        screen->cursor.row = screen->scroll_top;
    }
}

void screen_buffer_cursor_down(screen_buffer_t *screen, int n) {
    screen->cursor.row += clamp_count(n, screen->rows);
    if (screen->cursor.row > screen->scroll_bottom) {
        screen->cursor.row = screen->scroll_bottom;
    }
}

void screen_buffer_cursor_forward(screen_buffer_t *screen, int n) {
    screen->cursor.col += clamp_count(n, screen->cols);
    if (screen->cursor.col >= screen->cols) {
        screen->cursor.col = screen->cols - 1;
    }
}

void screen_buffer_cursor_back(screen_buffer_t *screen, int n) {
    screen->cursor.col -= clamp_count(n, screen->cols);
    if (screen->cursor.col < 0) {
        screen->cursor.col = 0;
    }
}

void screen_buffer_erase_display(screen_buffer_t *screen, int mode) {
    int start_row, start_col, end_row, end_col;
    
    switch (mode) {
        case 0: /* Cursor to end */
            start_row = screen->cursor.row;
            start_col = screen->cursor.col;
            end_row = screen->rows - 1;
            end_col = screen->cols - 1;
            break;
        case 1: /* Start to cursor */
            start_row = 0;
            start_col = 0;
            end_row = screen->cursor.row;
            end_col = screen->cursor.col;
            break;
        case 2: /* Entire screen */
        case 3: /* Entire screen + scrollback */
            start_row = 0;
            start_col = 0;
            end_row = screen->rows - 1;
            end_col = screen->cols - 1;
            if (mode == 3) {
                /* Clear scrollback */
                screen->scrollback_used = 0;
                screen->scrollback_pos = 0;
            }
            break;
        default:
            return;
    }
    
    /* Clear cells */
    for (int r = start_row; r <= end_row; r++) {
        int sc = (r == start_row) ? start_col : 0;
        int ec = (r == end_row) ? end_col : screen->cols - 1;
        fix_wide_edges(screen, r, sc, ec);
        for (int c = sc; c <= ec; c++) {
            char_cell_t *cell = get_cell(screen, r, c);
            if (cell) clear_cell(cell);
        }
    }
}

void screen_buffer_erase_line(screen_buffer_t *screen, int mode) {
    int start_col, end_col;
    
    switch (mode) {
        case 0: /* Cursor to end */
            start_col = screen->cursor.col;
            end_col = screen->cols - 1;
            break;
        case 1: /* Start to cursor */
            start_col = 0;
            end_col = screen->cursor.col;
            break;
        case 2: /* Entire line */
            start_col = 0;
            end_col = screen->cols - 1;
            break;
        default:
            return;
    }
    
    fix_wide_edges(screen, screen->cursor.row, start_col, end_col);
    for (int c = start_col; c <= end_col; c++) {
        char_cell_t *cell = get_cell(screen, screen->cursor.row, c);
        if (cell) clear_cell(cell);
    }
}

void screen_buffer_scroll_up(screen_buffer_t *screen, int n) {
    if (n <= 0) return;
    
    int top = screen->scroll_top;
    int bottom = screen->scroll_bottom;
    n = clamp_count(n, bottom - top + 1);
    
    /* Add top line to scrollback before scrolling */
    if (top == 0) {
        add_to_scrollback(screen, &screen->cells[0]);
    }
    
    /* Scroll lines up */
    for (int i = 0; i < n; i++) {
        /* Move lines up */
        for (int r = top; r < bottom; r++) {
            memcpy(&screen->cells[r * screen->cols],
                   &screen->cells[(r + 1) * screen->cols],
                   screen->cols * sizeof(char_cell_t));
        }
        
        /* Clear bottom line */
        for (int c = 0; c < screen->cols; c++) {
            clear_cell(get_cell(screen, bottom, c));
        }
    }
}

void screen_buffer_scroll_down(screen_buffer_t *screen, int n) {
    if (n <= 0) return;
    
    int top = screen->scroll_top;
    int bottom = screen->scroll_bottom;
    n = clamp_count(n, bottom - top + 1);
    
    /* Scroll lines down */
    for (int i = 0; i < n; i++) {
        /* Move lines down */
        for (int r = bottom; r > top; r--) {
            memcpy(&screen->cells[r * screen->cols],
                   &screen->cells[(r - 1) * screen->cols],
                   screen->cols * sizeof(char_cell_t));
        }
        
        /* Clear top line */
        for (int c = 0; c < screen->cols; c++) {
            clear_cell(get_cell(screen, top, c));
        }
    }
}

/* Reset all text attributes and colours (SGR 0) */
static void reset_attr(char_attr_t *attr) {
    memset(attr, 0, sizeof(*attr));
    attr->fg_color = COLOR_DEFAULT;
    attr->bg_color = COLOR_DEFAULT;
}

/* Parse the colour arguments after SGR 38/48 at params[*i]: "5;n" or
 * "2;r;g;b". Advances *i past what was consumed. Malformed or truncated
 * arguments are consumed without changing the colour. */
static void sgr_extended_color(const int *params, int count, int *i, uint32_t *color) {
    if (*i + 1 >= count) { *i = count; return; }
    int kind = params[*i + 1];
    if (kind == 5) {
        if (*i + 2 < count) {
            int n = params[*i + 2];
            if (n >= 0 && n <= 255) *color = COLOR_PALETTE(n);
        }
        *i = (*i + 2 < count) ? *i + 2 : count;
    } else if (kind == 2) {
        if (*i + 4 < count) {
            int r = params[*i + 2], g = params[*i + 3], b = params[*i + 4];
            if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                *color = COLOR_RGB(r, g, b);
            }
        }
        *i = (*i + 4 < count) ? *i + 4 : count;
    }
}

void screen_buffer_set_sgr(screen_buffer_t *screen, const int *params, int count) {
    screen_buffer_set_sgr_ex(screen, params, count, 0);
}

/* Colon sub-parameters of params[i] (bits of colon_mask) are params[i+1..*end-1] */
static int sgr_sub_end(int i, int count, unsigned colon_mask) {
    int j = i + 1;
    while (j < count && j < 32 && (colon_mask & (1u << j))) j++;
    return j;
}

void screen_buffer_set_sgr_ex(screen_buffer_t *screen, const int *params, int count,
                              unsigned colon_mask) {
    char_attr_t *attr = &screen->cursor.attr;
    if (count == 0) {
        reset_attr(attr);
        return;
    }
    
    for (int i = 0; i < count; i++) {
        int param = params[i];
        
        int end = sgr_sub_end(i, count, colon_mask);
        if (end > i + 1) {
            /* Colon form: 38:5:n  38:2:r:g:b  38:2:cs:r:g:b  4:style. Anything else is ignored. */
            int subs = end - i - 1;
            if (param == 38 || param == 48) {
                uint32_t *color = (param == 38) ? &attr->fg_color : &attr->bg_color;
                int kind = params[i + 1];
                if (kind == 5 && subs >= 2) {
                    int n = params[i + 2];
                    if (n >= 0 && n <= 255) *color = COLOR_PALETTE(n);
                } else if (kind == 2 && subs >= 4) {
                    /* With a colour space id (usually empty) there are 5 sub-parameters */
                    int k = (subs >= 5) ? i + 3 : i + 2;
                    int r = params[k], g = params[k + 1], b = params[k + 2];
                    if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
                        *color = COLOR_RGB(r, g, b);
                    }
                }
            } else if (param == 4) {
                attr->underline = params[i + 1] != 0;
            }
            i = end - 1;
            continue;
        }
        
        switch (param) {
            case 0: reset_attr(attr); break;
            case 1: attr->bold = 1; break;
            case 2: attr->dim = 1; break;
            case 3: attr->italic = 1; break;
            case 4: attr->underline = 1; break;
            case 5: attr->blink = 1; break;
            case 7: attr->reverse = 1; break;
            case 8: attr->hidden = 1; break;
            case 9: attr->strikethrough = 1; break;
            case 22: attr->bold = 0; attr->dim = 0; break;
            case 23: attr->italic = 0; break;
            case 24: attr->underline = 0; break;
            case 25: attr->blink = 0; break;
            case 27: attr->reverse = 0; break;
            case 28: attr->hidden = 0; break;
            case 29: attr->strikethrough = 0; break;
            
            case 39: attr->fg_color = COLOR_DEFAULT; break;
            case 49: attr->bg_color = COLOR_DEFAULT; break;
            
            /* Extended colours: 38;5;n  38;2;r;g;b  (48 for background) */
            case 38: sgr_extended_color(params, count, &i, &attr->fg_color); break;
            case 48: sgr_extended_color(params, count, &i, &attr->bg_color); break;
            
            default:
                if (param >= 30 && param <= 37) {
                    attr->fg_color = COLOR_PALETTE(param - 30);
                } else if (param >= 40 && param <= 47) {
                    attr->bg_color = COLOR_PALETTE(param - 40);
                } else if (param >= 90 && param <= 97) {
                    attr->fg_color = COLOR_PALETTE(param - 90 + 8);
                } else if (param >= 100 && param <= 107) {
                    attr->bg_color = COLOR_PALETTE(param - 100 + 8);
                }
                break;
        }
    }
}

bool screen_buffer_attr_equal(const char_attr_t *a, const char_attr_t *b) {
    return a->bold == b->bold && a->dim == b->dim && a->italic == b->italic &&
           a->underline == b->underline && a->blink == b->blink &&
           a->reverse == b->reverse && a->hidden == b->hidden &&
           a->strikethrough == b->strikethrough &&
           a->fg_color == b->fg_color && a->bg_color == b->bg_color;
}

/* Append ";..." SGR arguments for one colour. fg selects 30/90/38, else 40/100/48 */
static size_t sgr_color_args(char *out, size_t size, uint32_t color, bool fg) {
    if ((color & COLOR_KIND_MASK) == COLOR_KIND_PALETTE) {
        unsigned n = color & 0xffu;
        if (n < 8) return (size_t)snprintf(out, size, ";%u", (fg ? 30u : 40u) + n);
        if (n < 16) return (size_t)snprintf(out, size, ";%u", (fg ? 90u : 100u) + n - 8);
        return (size_t)snprintf(out, size, ";%u;5;%u", fg ? 38u : 48u, n);
    }
    if ((color & COLOR_KIND_MASK) == COLOR_KIND_RGB) {
        return (size_t)snprintf(out, size, ";%u;2;%u;%u;%u", fg ? 38u : 48u,
                                (color >> 16) & 0xffu, (color >> 8) & 0xffu, color & 0xffu);
    }
    return 0;
}

size_t screen_buffer_attr_to_sgr(const char_attr_t *attr, char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    char tmp[96];
    size_t n = 0;
    n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, "\x1b[0");
    if (attr->bold)          n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";1");
    if (attr->dim)           n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";2");
    if (attr->italic)        n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";3");
    if (attr->underline)     n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";4");
    if (attr->blink)         n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";5");
    if (attr->reverse)       n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";7");
    if (attr->hidden)        n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";8");
    if (attr->strikethrough) n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, ";9");
    n += sgr_color_args(tmp + n, sizeof(tmp) - n, attr->fg_color, true);
    n += sgr_color_args(tmp + n, sizeof(tmp) - n, attr->bg_color, false);
    n += (size_t)snprintf(tmp + n, sizeof(tmp) - n, "m");
    if (n >= size) { buffer[0] = '\0'; return 0; }
    memcpy(buffer, tmp, n + 1);
    return n;
}

void screen_buffer_save_cursor(screen_buffer_t *screen) {
    screen->saved.row = screen->cursor.row;
    screen->saved.col = screen->cursor.col;
    screen->saved.attr = screen->cursor.attr;
}

void screen_buffer_restore_cursor(screen_buffer_t *screen) {
    screen->cursor.row = screen->saved.row;
    screen->cursor.col = screen->saved.col;
    screen->cursor.attr = screen->saved.attr;
    clamp_cursor(screen);
}

void screen_buffer_newline(screen_buffer_t *screen) {
    /* Newline in terminal emulation means CR+LF (move down AND return to column 0) */
    screen->cursor.col = 0;
    screen->cursor.row++;
    if (screen->cursor.row > screen->scroll_bottom) {
        screen_buffer_scroll_up(screen, 1);
        screen->cursor.row = screen->scroll_bottom;
    }
}

void screen_buffer_carriage_return(screen_buffer_t *screen) {
    screen->cursor.col = 0;
}

void screen_buffer_tab(screen_buffer_t *screen) {
    /* Find next tab stop */
    for (int c = screen->cursor.col + 1; c < screen->cols; c++) {
        if (screen->tab_stops[c]) {
            screen->cursor.col = c;
            return;
        }
    }
    /* No tab stop found, go to end of line */
    screen->cursor.col = screen->cols - 1;
}

void screen_buffer_backspace(screen_buffer_t *screen) {
    if (screen->cursor.col > 0) {
        screen->cursor.col--;
    }
}

void screen_buffer_reset_tabs(screen_buffer_t *screen) {
    for (int c = 0; c < screen->cols; c++) {
        screen->tab_stops[c] = (c % 8 == 0);
    }
}

void screen_buffer_set_tab(screen_buffer_t *screen) {
    if (screen->cursor.col < screen->cols) {
        screen->tab_stops[screen->cursor.col] = true;
    }
}

void screen_buffer_clear_tab(screen_buffer_t *screen) {
    if (screen->cursor.col < screen->cols) {
        screen->tab_stops[screen->cursor.col] = false;
    }
}

void screen_buffer_process_sequence(screen_buffer_t *screen, const vt100_sequence_t *seq) {
    /* Anything but more text interrupts a partial UTF-8 character and ends
     * the run of characters a combining mark could attach to */
    if (seq->type != VT100_SEQ_PRINT) {
        utf8_flush(screen);
        screen->last_row = -1;
    }
    
    switch (seq->type) {
        case VT100_SEQ_PRINT:
            if (seq->text_len > 0) {
                screen_buffer_put_char(screen, seq->text[0]);
            }
            break;
            
        case VT100_SEQ_CURSOR_UP:
            screen_buffer_cursor_up(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_CURSOR_DOWN:
            screen_buffer_cursor_down(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_CURSOR_FORWARD:
            screen_buffer_cursor_forward(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_CURSOR_BACK:
            screen_buffer_cursor_back(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_CURSOR_POS:
            screen->cursor.row = (seq->params[0] > 0) ? seq->params[0] - 1 : 0;
            screen->cursor.col = (seq->params[1] > 0) ? seq->params[1] - 1 : 0;
            clamp_cursor(screen);
            break;
            
        case VT100_SEQ_ERASE_DISPLAY:
            screen_buffer_erase_display(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_ERASE_LINE:
            screen_buffer_erase_line(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_SGR:
            screen_buffer_set_sgr_ex(screen, seq->params, seq->param_count, seq->colon_mask);
            break;
            
        case VT100_SEQ_SAVE_CURSOR:
            screen_buffer_save_cursor(screen);
            break;
            
        case VT100_SEQ_RESTORE_CURSOR:
            screen_buffer_restore_cursor(screen);
            break;
            
        case VT100_SEQ_SCROLL_UP:
            screen_buffer_scroll_up(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_SCROLL_DOWN:
            screen_buffer_scroll_down(screen, seq->params[0]);
            break;
            
        case VT100_SEQ_NEWLINE:
            screen_buffer_newline(screen);
            break;
            
        case VT100_SEQ_CARRIAGE_RETURN:
            screen_buffer_carriage_return(screen);
            break;
            
        case VT100_SEQ_TAB:
            screen_buffer_tab(screen);
            break;
            
        case VT100_SEQ_BACKSPACE:
            screen_buffer_backspace(screen);
            break;
            
        case VT100_SEQ_BELL:
            /* Bell - could trigger callback */
            break;
            
        default:
            /* Ignore unknown sequences */
            break;
    }
}

const char_cell_t *screen_buffer_get_cell(const screen_buffer_t *screen, int row, int col) {
    if (row < 0 || row >= screen->rows || col < 0 || col >= screen->cols) {
        return NULL;
    }
    return &screen->cells[row * screen->cols + col];
}

size_t screen_buffer_cells_to_utf8(const screen_buffer_t *screen, const char_cell_t *cells,
                                   int count, char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    
    size_t pos = 0;
    for (int c = 0; c < count; c++) {
        uint32_t cps[CLUSTER_MAX_CPS];
        int n = screen_buffer_cell_codepoints(screen, &cells[c], cps);
        if (n == 0) continue;
        char enc[CLUSTER_MAX_CPS * UTF8_MAX_BYTES];
        size_t len = 0;
        for (int i = 0; i < n; i++) len += utf8_encode(cps[i], enc + len);
        if (pos + len >= size) break;   /* keep room for the NUL */
        memcpy(buffer + pos, enc, len);
        pos += len;
    }
    buffer[pos] = '\0';
    return pos;
}

size_t screen_buffer_get_line(const screen_buffer_t *screen, int row, char *buffer, size_t size) {
    if (row < 0 || row >= screen->rows || !buffer || size == 0) {
        return 0;
    }
    
    return screen_buffer_cells_to_utf8(screen, &screen->cells[(size_t)row * screen->cols],
                                       screen->cols, buffer, size);
}

size_t screen_buffer_get_text(const screen_buffer_t *screen, char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    
    size_t pos = 0;
    for (int r = 0; r < screen->rows && pos < size - 1; r++) {
        const char_cell_t *row_cells = &screen->cells[(size_t)r * screen->cols];
        
        /* Find last non-space character in this row */
        int last_col = screen->cols - 1;
        while (last_col >= 0 && row_cells[last_col].ch == ' ') {
            last_col--;
        }
        
        /* Copy characters up to last non-space (or empty line) */
        pos += screen_buffer_cells_to_utf8(screen, row_cells, last_col + 1,
                                           buffer + pos, size - pos);
        
        /* Add newline after each row */
        if (pos < size - 1) {
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = '\0';
    return pos;
}

/* Growable output buffer for screen_buffer_render_ansi */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
    bool failed;
} ansi_out_t;

static void ansi_put(ansi_out_t *o, const char *src, size_t n) {
    if (o->failed) return;
    if (o->len + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (cap < o->len + n + 1) cap *= 2;
        char *p = realloc(o->data, cap);
        if (!p) { o->failed = true; return; }
        o->data = p;
        o->cap = cap;
    }
    memcpy(o->data + o->len, src, n);
    o->len += n;
    o->data[o->len] = '\0';
}

/* A blank cell at the end of a row that shows nothing and need not be drawn */
static bool cell_is_trailing_blank(const char_cell_t *cell) {
    return cell->ch == ' ' && cell->attr.bg_color == COLOR_DEFAULT &&
           !cell->attr.reverse && !cell->attr.underline && !cell->attr.strikethrough;
}

char *screen_buffer_render_ansi(const screen_buffer_t *screen, size_t *out_len) {
    ansi_out_t o = {0};
    char_attr_t cur;
    memset(&cur, 0, sizeof(cur));
    
    for (int r = 0; r < screen->rows; r++) {
        const char_cell_t *row_cells = &screen->cells[(size_t)r * screen->cols];
        int last = screen->cols - 1;
        while (last >= 0 && cell_is_trailing_blank(&row_cells[last])) last--;
        
        for (int c = 0; c <= last; c++) {
            const char_cell_t *cell = &row_cells[c];
            if (cell->ch == CELL_CONTINUATION) continue;
            if (!screen_buffer_attr_equal(&cur, &cell->attr)) {
                char sgr[96];
                size_t n = screen_buffer_attr_to_sgr(&cell->attr, sgr, sizeof(sgr));
                ansi_put(&o, sgr, n);
                cur = cell->attr;
            }
            uint32_t cps[CLUSTER_MAX_CPS];
            int n = screen_buffer_cell_codepoints(screen, cell, cps);
            for (int i = 0; i < n; i++) {
                char enc[UTF8_MAX_BYTES];
                ansi_put(&o, enc, utf8_encode(cps[i], enc));
            }
        }
        
        /* Do not let a row's attributes run into the line break */
        char_attr_t none;
        reset_attr(&none);
        if (!screen_buffer_attr_equal(&cur, &none)) {
            ansi_put(&o, "\x1b[0m", 4);
            cur = none;
        }
        if (r + 1 < screen->rows) ansi_put(&o, "\r\n", 2);
    }
    
    if (o.failed) {
        free(o.data);
        if (out_len) *out_len = 0;
        return NULL;
    }
    if (!o.data) {
        o.data = calloc(1, 1);
    }
    if (out_len) *out_len = o.len;
    return o.data;
}

size_t screen_buffer_text_size(const screen_buffer_t *screen) {
    size_t total = 1;   /* NUL */
    for (int r = 0; r < screen->rows; r++) {
        const char_cell_t *row_cells = &screen->cells[(size_t)r * screen->cols];
        int last_col = screen->cols - 1;
        while (last_col >= 0 && row_cells[last_col].ch == ' ') last_col--;
        for (int c = 0; c <= last_col; c++) {
            uint32_t cps[CLUSTER_MAX_CPS];
            int n = screen_buffer_cell_codepoints(screen, &row_cells[c], cps);
            for (int i = 0; i < n; i++) {
                char enc[UTF8_MAX_BYTES];
                total += utf8_encode(cps[i], enc);
            }
        }
        total++;   /* newline */
    }
    return total;
}

const char_cell_t *screen_buffer_get_scrollback_line(const screen_buffer_t *screen, int line_index) {
    if (line_index < 0 || line_index >= screen->scrollback_used) {
        return NULL;
    }
    
    /* Calculate actual position in circular buffer */
    int pos = (screen->scrollback_pos - screen->scrollback_used + line_index + screen->scrollback_size) % screen->scrollback_size;
    return &screen->scrollback[pos * screen->cols];
}

void screen_buffer_get_cursor(const screen_buffer_t *screen, int *row, int *col) {
    if (row) *row = screen->cursor.row;
    if (col) *col = screen->cursor.col;
}

void screen_buffer_set_cursor(screen_buffer_t *screen, int row, int col) {
    screen->cursor.row = row;
    screen->cursor.col = col;
    clamp_cursor(screen);
}

bool screen_buffer_is_cursor_visible(const screen_buffer_t *screen) {
    return screen->cursor.visible;
}
