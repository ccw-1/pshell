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

/* Colours. 0 = terminal default; otherwise the top byte says how the low
 * 24 bits are read: palette index (0-255: 0-7 normal, 8-15 bright, 16-255 the
 * xterm 256-colour cube/greys) or 24-bit RGB. */
#define COLOR_DEFAULT        0u
#define COLOR_KIND_PALETTE   0x01000000u
#define COLOR_KIND_RGB       0x02000000u
#define COLOR_KIND_MASK      0xff000000u
#define COLOR_PALETTE(n)     (COLOR_KIND_PALETTE | ((unsigned)(n) & 0xffu))
#define COLOR_RGB(r, g, b)   (COLOR_KIND_RGB | (((unsigned)(r) & 0xffu) << 16) | \
                              (((unsigned)(g) & 0xffu) << 8) | ((unsigned)(b) & 0xffu))

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
    uint32_t fg_color;       /* COLOR_DEFAULT, COLOR_PALETTE(n) or COLOR_RGB(r,g,b) */
    uint32_t bg_color;
} char_attr_t;

/* Cell value of the right half of a double-width character */
#define CELL_CONTINUATION 0

/* A cell that carries combining marks / a joined sequence stores
 * CELL_CLUSTER_FLAG | index instead of a code point; the index selects an
 * entry of the screen's cluster pool (see screen_buffer_cell_codepoints). */
#define CELL_CLUSTER_FLAG 0x80000000u
#define CLUSTER_MAX_CPS   8      /* code points per cluster (base + marks) */
#define CLUSTER_POOL_MAX  4096   /* distinct clusters per screen */

typedef struct {
    uint32_t cps[CLUSTER_MAX_CPS];
    uint8_t count;
} cluster_t;

/* Character cell */
typedef struct {
    uint32_t ch;          /* Unicode code point, CELL_CONTINUATION (2nd column of a wide char)
                           * or CELL_CLUSTER_FLAG | pool index */
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
    
    /* DEC private modes the screen only records (the application inside the
     * session switches them; attach replays them on the user's terminal) */
    bool app_cursor_keys;   /* ?1 */
    bool bracketed_paste;   /* ?2004 */
    bool focus_events;      /* ?1004 */
    int mouse_track;        /* 0, 9, 1000, 1001, 1002 or 1003 */
    int mouse_encoding;     /* 0, 1005, 1006 or 1015 */
    
    /* Alternate screen (?47, ?1047, ?1049): `cells` is always the grid being
     * shown; while in_alt is set, other_cells holds the main grid. The alternate
     * screen has no scrollback. */
    bool in_alt;
    int alt_mode;           /* Mode that entered it: 47, 1047 or 1049 */
    char_cell_t *other_cells;
    
    /* Pool of code point sequences for cells with combining marks. Entries are
     * de-duplicated and never freed, so cell indexes stay valid while cells are
     * copied between screen and scrollback. */
    cluster_t *clusters;
    int cluster_count;
    int cluster_cap;
    
    /* Where the last printable character went (lead cell), so a following
     * combining mark can be attached to it. last_row = -1: nothing to attach to. */
    int last_row;
    int last_col;
    int last_width;
    
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
 * Get the code points shown in a cell: the base character followed by any
 * combining marks. out must hold CLUSTER_MAX_CPS entries. Returns the count
 * (1 for a plain cell; 0 for the right half of a wide character).
 */
int screen_buffer_cell_codepoints(const screen_buffer_t *screen, const char_cell_t *cell,
                                  uint32_t *out);

/**
 * Exact number of bytes screen_buffer_get_text() needs for the screen
 * (including the terminating NUL).
 */
size_t screen_buffer_text_size(const screen_buffer_t *screen);

/**
 * Get a line of text (for rendering or output) as UTF-8.
 * Returns number of bytes written to buffer (NUL terminated).
 * A line needs up to cols * CLUSTER_MAX_CPS * UTF8_MAX_BYTES + 1 bytes in
 * the worst case; plain text needs cols * UTF8_MAX_BYTES + 1.
 */
size_t screen_buffer_get_line(const screen_buffer_t *screen, int row, char *buffer, size_t size);

/**
 * Get entire screen as UTF-8 text (for debugging or snapshots)
 * Returns number of bytes written. Size the buffer with
 * screen_buffer_text_size().
 */
size_t screen_buffer_get_text(const screen_buffer_t *screen, char *buffer, size_t size);

/**
 * Render the whole screen as terminal output that reproduces it, including
 * colours and text attributes: rows separated by CR LF (none after the last
 * row), trailing blanks omitted, attributes reset at each line end. The
 * caller should clear the terminal first and position the cursor after.
 * Returns a malloc'd NUL-terminated string (free it), NULL on allocation
 * failure. *out_len receives the length.
 */
char *screen_buffer_render_ansi(const screen_buffer_t *screen, size_t *out_len);

/**
 * Like screen_buffer_render_ansi() for the main screen while the alternate
 * screen is shown. Returns NULL when the alternate screen is not active (or
 * on allocation failure).
 */
char *screen_buffer_render_main_ansi(const screen_buffer_t *screen, size_t *out_len);

/**
 * Write the sequences that put a terminal into the recorded private modes
 * (hidden cursor, application cursor keys, mouse reporting, bracketed paste,
 * focus events, no auto-wrap). Modes at their defaults emit nothing. The
 * alternate screen is not included. Returns the bytes written (NUL
 * terminated); size 128 is always enough.
 */
size_t screen_buffer_modes_to_ansi(const screen_buffer_t *screen, char *buf, size_t size);

/**
 * Encode the cells of a line (screen or scrollback row) as UTF-8.
 * Continuation cells (right half of a wide character) are skipped. Stops
 * before overflowing size. Returns the bytes written (NUL terminated).
 */
size_t screen_buffer_cells_to_utf8(const screen_buffer_t *screen, const char_cell_t *cells,
                                   int count, char *buffer, size_t size);

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
 * (two columns for wide characters). Combining marks, joiners and emoji
 * modifiers are attached to the character printed before them and do not
 * advance the cursor.
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

/* Same, with colon sub-parameters: bit i of colon_mask set = params[i] is a
 * sub-parameter of params[i-1] (38:2::r:g:b, 38:5:n, 4:3). */
void screen_buffer_set_sgr_ex(screen_buffer_t *screen, const int *params, int count,
                              unsigned colon_mask);

/** True if two attribute sets are identical */
bool screen_buffer_attr_equal(const char_attr_t *a, const char_attr_t *b);

/**
 * Write the escape sequence that selects exactly these attributes
 * (starts with a reset: "ESC[0;...m"). Returns the length, 0 if it did not fit.
 */
size_t screen_buffer_attr_to_sgr(const char_attr_t *attr, char *buffer, size_t size);

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
