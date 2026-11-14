#ifndef PSHELL_SCREEN_BUFFER_H
#define PSHELL_SCREEN_BUFFER_H

#include "pshell/vt100_parser.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * Screen Buffer Management
 * 
 * Maintains the terminal screen state including:
 * - Character cells with attributes
 * - Cursor position
 * - Scrollback buffer
 * - Screen dimensions
 */

/* Character attributes (SGR parameters) */
typedef struct {
    unsigned int bold:1;
    unsigned int dim:1;
    unsigned int italic:1;
    unsigned int underline:1;
    unsigned int blink:1;
    unsigned int reverse:1;
    unsigned int hidden:1;
    unsigned int strikethrough:1;
    unsigned char fg_color;  /* 0-255 or special values */
    unsigned char bg_color;  /* 0-255 or special values */
} char_attr_t;

/* Special color values */
#define COLOR_DEFAULT 0
#define COLOR_BLACK   1
#define COLOR_RED     2
#define COLOR_GREEN   3
#define COLOR_YELLOW  4
#define COLOR_BLUE    5
#define COLOR_MAGENTA 6
#define COLOR_CYAN    7
#define COLOR_WHITE   8

/* Cell value of the right half of a double-width character */
#define CELL_CONTINUATION 0

/* Character cell */
typedef struct {
    uint32_t ch;          /* Unicode code point (CELL_CONTINUATION = 2nd column of a wide char) */
    char_attr_t attr;     /* Character attributes */
} char_cell_t;

/* Cursor state */
typedef struct {
    int row;              /* 0-based row */
    int col;              /* 0-based column */
    bool visible;         /* Cursor visibility */
    char_attr_t attr;     /* Current attributes for new text */
} cursor_t;

/* Saved cursor state (for save/restore) */
typedef struct {
    int row;
    int col;
    char_attr_t attr;
} saved_cursor_t;

/* Screen buffer */
typedef struct {
    int rows;             /* Screen height */
    int cols;             /* Screen width */
    char_cell_t *cells;   /* Screen cells (rows * cols) */
    cursor_t cursor;      /* Current cursor state */
    saved_cursor_t saved; /* Saved cursor state */
    
    /* Scrollback buffer */
    char_cell_t *scrollback;  /* Scrollback lines */
    int scrollback_size;      /* Max scrollback lines */
    int scrollback_used;      /* Used scrollback lines */
    int scrollback_pos;       /* Current position in circular buffer */
    
    /* Scroll region (for CSI r) */
    int scroll_top;       /* Top of scroll region (0-based) */
    int scroll_bottom;    /* Bottom of scroll region (0-based) */
    
    /* Tab stops */
    bool *tab_stops;      /* Tab stop positions */
    
    /* Modes */
    bool auto_wrap;       /* Auto wrap at end of line */
    bool origin_mode;     /* Origin mode (cursor addressing) */
    bool insert_mode;     /* Insert mode */
    
    /* UTF-8 decoder state (bytes arrive one at a time) */
    uint32_t utf8_cp;     /* Code point being assembled */
    uint32_t utf8_min;    /* Smallest valid value (rejects overlong forms) */
    int utf8_need;        /* Continuation bytes still expected */
} screen_buffer_t;

/**
 * Create a new screen buffer
 */
screen_buffer_t *screen_buffer_create(int rows, int cols, int scrollback_lines);

/**
 * Destroy screen buffer and free resources
 */
void screen_buffer_destroy(screen_buffer_t *screen);

/**
 * Resize screen buffer
 * Returns 0 on success, -1 on error
 */
int screen_buffer_resize(screen_buffer_t *screen, int rows, int cols);

/**
 * Clear screen buffer
 */
void screen_buffer_clear(screen_buffer_t *screen);

/**
 * Process a VT100 sequence and update screen state
 */
void screen_buffer_process_sequence(screen_buffer_t *screen, const vt100_sequence_t *seq);

/**
 * Get character cell at position
 * Returns NULL if position is out of bounds
 */
const char_cell_t *screen_buffer_get_cell(const screen_buffer_t *screen, int row, int col);

/**
 * Get a line of text (for rendering or output) as UTF-8.
 * Returns number of bytes written to buffer (NUL terminated).
 * A line needs up to cols * UTF8_MAX_BYTES + 1 bytes.
 */
size_t screen_buffer_get_line(const screen_buffer_t *screen, int row, char *buffer, size_t size);

/**
 * Get entire screen as UTF-8 text (for debugging or snapshots)
 * Returns number of bytes written. The text needs up to
 * rows * (cols * UTF8_MAX_BYTES + 1) + 1 bytes.
 */
size_t screen_buffer_get_text(const screen_buffer_t *screen, char *buffer, size_t size);

/**
 * Encode the cells of a line (screen or scrollback row) as UTF-8.
 * Continuation cells (right half of a wide character) are skipped. Stops
 * before overflowing size. Returns the bytes written (NUL terminated).
 */
size_t screen_buffer_cells_to_utf8(const char_cell_t *cells, int count, char *buffer, size_t size);

/**
 * Get scrollback line
 * line_index: 0 = oldest line, scrollback_used-1 = newest line
 * Returns NULL if index is out of bounds
 */
const char_cell_t *screen_buffer_get_scrollback_line(const screen_buffer_t *screen, int line_index);

/**
 * Get cursor position
 */
void screen_buffer_get_cursor(const screen_buffer_t *screen, int *row, int *col);

/**
 * Set cursor position (0-based)
 */
void screen_buffer_set_cursor(screen_buffer_t *screen, int row, int col);

/**
 * Get cursor visibility
 */
bool screen_buffer_is_cursor_visible(const screen_buffer_t *screen);

/**
 * Feed one output byte. ASCII is printed directly; bytes >= 0x80 are
 * assembled as UTF-8 (invalid input prints U+FFFD).
 */
void screen_buffer_put_char(screen_buffer_t *screen, char ch);

/**
 * Print a Unicode code point at the cursor position and advance the cursor
 * (two columns for wide characters, none for combining characters)
 */
void screen_buffer_put_codepoint(screen_buffer_t *screen, uint32_t cp);

/**
 * Move cursor up
 */
void screen_buffer_cursor_up(screen_buffer_t *screen, int n);

/**
 * Move cursor down
 */
void screen_buffer_cursor_down(screen_buffer_t *screen, int n);

/**
 * Move cursor forward (right)
 */
void screen_buffer_cursor_forward(screen_buffer_t *screen, int n);

/**
 * Move cursor back (left)
 */
void screen_buffer_cursor_back(screen_buffer_t *screen, int n);

/**
 * Erase display
 * mode: 0 = cursor to end, 1 = start to cursor, 2 = entire screen
 */
void screen_buffer_erase_display(screen_buffer_t *screen, int mode);

/**
 * Erase line
 * mode: 0 = cursor to end, 1 = start to cursor, 2 = entire line
 */
void screen_buffer_erase_line(screen_buffer_t *screen, int mode);

/**
 * Scroll up (add lines at bottom)
 */
void screen_buffer_scroll_up(screen_buffer_t *screen, int n);

/**
 * Scroll down (add lines at top)
 */
void screen_buffer_scroll_down(screen_buffer_t *screen, int n);

/**
 * Set graphic rendition (colors, bold, etc.)
 */
void screen_buffer_set_sgr(screen_buffer_t *screen, const int *params, int count);

/**
 * Save cursor position and attributes
 */
void screen_buffer_save_cursor(screen_buffer_t *screen);

/**
 * Restore cursor position and attributes
 */
void screen_buffer_restore_cursor(screen_buffer_t *screen);

/**
 * Handle newline (LF)
 */
void screen_buffer_newline(screen_buffer_t *screen);

/**
 * Handle carriage return (CR)
 */
void screen_buffer_carriage_return(screen_buffer_t *screen);

/**
 * Handle tab (HT)
 */
void screen_buffer_tab(screen_buffer_t *screen);

/**
 * Handle backspace (BS)
 */
void screen_buffer_backspace(screen_buffer_t *screen);

/**
 * Reset tab stops to default (every 8 columns)
 */
void screen_buffer_reset_tabs(screen_buffer_t *screen);

/**
 * Set tab stop at current cursor position
 */
void screen_buffer_set_tab(screen_buffer_t *screen);

/**
 * Clear tab stop at current cursor position
 */
void screen_buffer_clear_tab(screen_buffer_t *screen);

#endif /* PSHELL_SCREEN_BUFFER_H */
