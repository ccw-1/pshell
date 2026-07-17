#ifndef PSHELL_VT100_PARSER_H
#define PSHELL_VT100_PARSER_H

#include <stddef.h>
#include <stdbool.h>

/**
 * VT100 Escape Sequence Parser
 * 
 * Parses VT100/ANSI escape sequences from terminal output.
 * Supports common escape sequences for cursor movement, colors, etc.
 */

/* Maximum parameters in an escape sequence (e.g., CSI sequences) */
#define VT100_MAX_PARAMS 16

/* Parser states */
typedef enum {
    VT100_STATE_NORMAL,      /* Normal text */
    VT100_STATE_ESC,         /* ESC received */
    VT100_STATE_CSI,         /* CSI sequence (ESC [) */
    VT100_STATE_OSC,         /* OSC sequence (ESC ]) */
    VT100_STATE_CHARSET      /* Charset selection (ESC ( or ESC )) */
} vt100_state_t;

/* Escape sequence types */
typedef enum {
    VT100_SEQ_NONE,
    VT100_SEQ_PRINT,         /* Printable character */
    VT100_SEQ_CURSOR_UP,     /* CUU - Cursor Up */
    VT100_SEQ_CURSOR_DOWN,   /* CUD - Cursor Down */
    VT100_SEQ_CURSOR_FORWARD,/* CUF - Cursor Forward */
    VT100_SEQ_CURSOR_BACK,   /* CUB - Cursor Back */
    VT100_SEQ_CURSOR_POS,    /* CUP - Cursor Position */
    VT100_SEQ_ERASE_DISPLAY, /* ED - Erase Display */
    VT100_SEQ_ERASE_LINE,    /* EL - Erase Line */
    VT100_SEQ_SGR,           /* SGR - Select Graphic Rendition */
    VT100_SEQ_SAVE_CURSOR,   /* SCP - Save Cursor Position */
    VT100_SEQ_RESTORE_CURSOR,/* RCP - Restore Cursor Position */
    VT100_SEQ_SCROLL_UP,     /* SU - Scroll Up */
    VT100_SEQ_SCROLL_DOWN,   /* SD - Scroll Down */
    VT100_SEQ_NEWLINE,       /* LF - Line Feed */
    VT100_SEQ_CARRIAGE_RETURN,/* CR - Carriage Return */
    VT100_SEQ_TAB,           /* HT - Horizontal Tab */
    VT100_SEQ_BACKSPACE,     /* BS - Backspace */
    VT100_SEQ_BELL,          /* BEL - Bell */
    VT100_SEQ_SET_MODE,      /* SM - Set Mode */
    VT100_SEQ_RESET_MODE,    /* RM - Reset Mode */
    VT100_SEQ_DEVICE_STATUS, /* DSR - Device Status Report */
    VT100_SEQ_TITLE,         /* OSC 0/2 - Set window title */
    VT100_SEQ_UNKNOWN        /* Unknown/unsupported sequence */
} vt100_seq_type_t;

/* Parsed escape sequence */
typedef struct {
    vt100_seq_type_t type;
    int params[VT100_MAX_PARAMS];
    int param_count;
    char final_char;         /* Final character of CSI sequence */
    bool private_mode;       /* CSI sequence starts with ? */
    char text[256];          /* For printable text or OSC strings */
    size_t text_len;
} vt100_sequence_t;

/* VT100 parser context */
typedef struct {
    vt100_state_t state;
    vt100_sequence_t current_seq;
    int params[VT100_MAX_PARAMS];
    int param_count;
    int current_param;
    bool private_mode;
    char osc_buffer[256];
    size_t osc_len;
} vt100_parser_t;

/**
 * Initialize VT100 parser
 */
void vt100_parser_init(vt100_parser_t *parser);

/**
 * Reset parser to initial state
 */
void vt100_parser_reset(vt100_parser_t *parser);

/**
 * Parse a single character
 * Returns true if a complete sequence was parsed
 */
bool vt100_parser_feed(vt100_parser_t *parser, char ch, vt100_sequence_t *seq);

/**
 * Parse a buffer of characters
 * Calls callback for each complete sequence
 */
typedef void (*vt100_callback_t)(const vt100_sequence_t *seq, void *user_data);
void vt100_parser_parse(vt100_parser_t *parser, const char *data, size_t len,
                        vt100_callback_t callback, void *user_data);

/**
 * Get sequence type name (for debugging)
 */
const char *vt100_seq_type_name(vt100_seq_type_t type);

/**
 * Check if character is a control character
 */
bool vt100_is_control(char ch);

/**
 * Check if character is printable
 */
bool vt100_is_printable(char ch);

#endif /* PSHELL_VT100_PARSER_H */
