#include "pshell/screen_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdint.h>

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
    
    screen->cursor.row = 0;
    screen->cursor.col = 0;
    screen->cursor.visible = true;
    memset(&screen->cursor.attr, 0, sizeof(screen->cursor.attr));
    screen->cursor.attr.fg_color = COLOR_DEFAULT;
    screen->cursor.attr.bg_color = COLOR_DEFAULT;
}

void screen_buffer_put_char(screen_buffer_t *screen, char ch) {
    char_cell_t *cell = get_cell(screen, screen->cursor.row, screen->cursor.col);
    if (!cell) return;
    
    /* Insert mode: shift characters right */
    if (screen->insert_mode && screen->cursor.col < screen->cols - 1) {
        memmove(cell + 1, cell, (screen->cols - screen->cursor.col - 1) * sizeof(char_cell_t));
    }
    
    /* Write character */
    cell->ch = ch;
    cell->attr = screen->cursor.attr;
    
    /* Advance cursor */
    screen->cursor.col++;
    
    /* Handle wrap */
    if (screen->cursor.col >= screen->cols) {
        if (screen->auto_wrap) {
            screen->cursor.col = 0;
            screen->cursor.row++;
            if (screen->cursor.row > screen->scroll_bottom) {
                screen_buffer_scroll_up(screen, 1);
                screen->cursor.row = screen->scroll_bottom;
            }
        } else {
            screen->cursor.col = screen->cols - 1;
        }
    }
}

void screen_buffer_cursor_up(screen_buffer_t *screen, int n) {
    screen->cursor.row -= n;
    if (screen->cursor.row < screen->scroll_top) {
        screen->cursor.row = screen->scroll_top;
    }
}

void screen_buffer_cursor_down(screen_buffer_t *screen, int n) {
    screen->cursor.row += n;
    if (screen->cursor.row > screen->scroll_bottom) {
        screen->cursor.row = screen->scroll_bottom;
    }
}

void screen_buffer_cursor_forward(screen_buffer_t *screen, int n) {
    screen->cursor.col += n;
    if (screen->cursor.col >= screen->cols) {
        screen->cursor.col = screen->cols - 1;
    }
}

void screen_buffer_cursor_back(screen_buffer_t *screen, int n) {
    screen->cursor.col -= n;
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
    
    for (int c = start_col; c <= end_col; c++) {
        char_cell_t *cell = get_cell(screen, screen->cursor.row, c);
        if (cell) clear_cell(cell);
    }
}

void screen_buffer_scroll_up(screen_buffer_t *screen, int n) {
    if (n <= 0) return;
    
    int top = screen->scroll_top;
    int bottom = screen->scroll_bottom;
    
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

void screen_buffer_set_sgr(screen_buffer_t *screen, const int *params, int count) {
    if (count == 0) {
        /* Reset all attributes */
        memset(&screen->cursor.attr, 0, sizeof(screen->cursor.attr));
        screen->cursor.attr.fg_color = COLOR_DEFAULT;
        screen->cursor.attr.bg_color = COLOR_DEFAULT;
        return;
    }
    
    for (int i = 0; i < count; i++) {
        int param = params[i];
        
        switch (param) {
            case 0: /* Reset */
                memset(&screen->cursor.attr, 0, sizeof(screen->cursor.attr));
                screen->cursor.attr.fg_color = COLOR_DEFAULT;
                screen->cursor.attr.bg_color = COLOR_DEFAULT;
                break;
            case 1: screen->cursor.attr.bold = 1; break;
            case 2: screen->cursor.attr.dim = 1; break;
            case 3: screen->cursor.attr.italic = 1; break;
            case 4: screen->cursor.attr.underline = 1; break;
            case 5: screen->cursor.attr.blink = 1; break;
            case 7: screen->cursor.attr.reverse = 1; break;
            case 8: screen->cursor.attr.hidden = 1; break;
            case 9: screen->cursor.attr.strikethrough = 1; break;
            case 22: screen->cursor.attr.bold = 0; screen->cursor.attr.dim = 0; break;
            case 23: screen->cursor.attr.italic = 0; break;
            case 24: screen->cursor.attr.underline = 0; break;
            case 25: screen->cursor.attr.blink = 0; break;
            case 27: screen->cursor.attr.reverse = 0; break;
            case 28: screen->cursor.attr.hidden = 0; break;
            case 29: screen->cursor.attr.strikethrough = 0; break;
            
            /* Foreground colors (30-37, 90-97) */
            case 30: screen->cursor.attr.fg_color = COLOR_BLACK; break;
            case 31: screen->cursor.attr.fg_color = COLOR_RED; break;
            case 32: screen->cursor.attr.fg_color = COLOR_GREEN; break;
            case 33: screen->cursor.attr.fg_color = COLOR_YELLOW; break;
            case 34: screen->cursor.attr.fg_color = COLOR_BLUE; break;
            case 35: screen->cursor.attr.fg_color = COLOR_MAGENTA; break;
            case 36: screen->cursor.attr.fg_color = COLOR_CYAN; break;
            case 37: screen->cursor.attr.fg_color = COLOR_WHITE; break;
            case 39: screen->cursor.attr.fg_color = COLOR_DEFAULT; break;
            
            /* Background colors (40-47, 100-107) */
            case 40: screen->cursor.attr.bg_color = COLOR_BLACK; break;
            case 41: screen->cursor.attr.bg_color = COLOR_RED; break;
            case 42: screen->cursor.attr.bg_color = COLOR_GREEN; break;
            case 43: screen->cursor.attr.bg_color = COLOR_YELLOW; break;
            case 44: screen->cursor.attr.bg_color = COLOR_BLUE; break;
            case 45: screen->cursor.attr.bg_color = COLOR_MAGENTA; break;
            case 46: screen->cursor.attr.bg_color = COLOR_CYAN; break;
            case 47: screen->cursor.attr.bg_color = COLOR_WHITE; break;
            case 49: screen->cursor.attr.bg_color = COLOR_DEFAULT; break;
            
            /* 256-color mode: 38;5;n (fg) or 48;5;n (bg) */
            case 38:
            case 48:
                if (i + 2 < count && params[i + 1] == 5) {
                    if (param == 38) {
                        screen->cursor.attr.fg_color = params[i + 2];
                    } else {
                        screen->cursor.attr.bg_color = params[i + 2];
                    }
                    i += 2;
                }
                break;
        }
    }
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
            screen_buffer_set_sgr(screen, seq->params, seq->param_count);
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

size_t screen_buffer_get_line(const screen_buffer_t *screen, int row, char *buffer, size_t size) {
    if (row < 0 || row >= screen->rows || !buffer || size == 0) {
        return 0;
    }
    
    size_t pos = 0;
    for (int c = 0; c < screen->cols && pos < size - 1; c++) {
        const char_cell_t *cell = &screen->cells[row * screen->cols + c];
        buffer[pos++] = cell->ch;
    }
    buffer[pos] = '\0';
    return pos;
}

size_t screen_buffer_get_text(const screen_buffer_t *screen, char *buffer, size_t size) {
    if (!buffer || size == 0) return 0;
    
    size_t pos = 0;
    for (int r = 0; r < screen->rows && pos < size - 1; r++) {
        /* Find last non-space character in this row */
        int last_col = screen->cols - 1;
        while (last_col >= 0) {
            const char_cell_t *cell = &screen->cells[r * screen->cols + last_col];
            if (cell->ch != ' ') break;
            last_col--;
        }
        
        /* Copy characters up to last non-space (or empty line) */
        for (int c = 0; c <= last_col && pos < size - 1; c++) {
            const char_cell_t *cell = &screen->cells[r * screen->cols + c];
            buffer[pos++] = cell->ch;
        }
        
        /* Add newline after each row */
        if (pos < size - 1) {
            buffer[pos++] = '\n';
        }
    }
    buffer[pos] = '\0';
    return pos;
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
