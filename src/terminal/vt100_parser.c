#include "pshell/vt100_parser.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>

/* Control characters */
#define CTRL_BEL  0x07  /* Bell */
#define CTRL_BS   0x08  /* Backspace */
#define CTRL_HT   0x09  /* Horizontal Tab */
#define CTRL_LF   0x0A  /* Line Feed */
#define CTRL_CR   0x0D  /* Carriage Return */
#define CTRL_ESC  0x1B  /* Escape */

/* Helper function to reset sequence */
static void reset_sequence(vt100_parser_t *parser) {
    memset(&parser->current_seq, 0, sizeof(parser->current_seq));
    parser->param_count = 0;
    parser->current_param = 0;
    parser->colon_mask = 0;
    parser->private_mode = false;
    parser->osc_len = 0;
}

void vt100_parser_init(vt100_parser_t *parser) {
    memset(parser, 0, sizeof(*parser));
    parser->state = VT100_STATE_NORMAL;
}

void vt100_parser_reset(vt100_parser_t *parser) {
    parser->state = VT100_STATE_NORMAL;
    reset_sequence(parser);
}

bool vt100_is_control(char ch) {
    return (ch >= 0x00 && ch <= 0x1F) || ch == 0x7F;
}

bool vt100_is_printable(char ch) {
    return ch >= 0x20 && ch <= 0x7E;
}

/* Parse CSI sequence final character and determine type */
static vt100_seq_type_t parse_csi_sequence(char final_char, int *params, int param_count, bool private_mode) {
    switch (final_char) {
        case 'A': return VT100_SEQ_CURSOR_UP;
        case 'B': return VT100_SEQ_CURSOR_DOWN;
        case 'C': return VT100_SEQ_CURSOR_FORWARD;
        case 'D': return VT100_SEQ_CURSOR_BACK;
        case 'H': case 'f': return VT100_SEQ_CURSOR_POS;
        case 'J': return VT100_SEQ_ERASE_DISPLAY;
        case 'K': return VT100_SEQ_ERASE_LINE;
        case 'm': return VT100_SEQ_SGR;
        case 's': return VT100_SEQ_SAVE_CURSOR;
        case 'u': return VT100_SEQ_RESTORE_CURSOR;
        case 'S': return VT100_SEQ_SCROLL_UP;
        case 'T': return VT100_SEQ_SCROLL_DOWN;
        case 'h': return VT100_SEQ_SET_MODE;
        case 'l': return VT100_SEQ_RESET_MODE;
        case 'n': return VT100_SEQ_DEVICE_STATUS;
        default: return VT100_SEQ_UNKNOWN;
    }
}

/* Finalize CSI sequence */
static bool finalize_csi(vt100_parser_t *parser, char final_char, vt100_sequence_t *seq) {
    /* Add current parameter if being built */
    if (parser->param_count < VT100_MAX_PARAMS) {
        parser->params[parser->param_count++] = parser->current_param;
    }
    
    /* Build sequence */
    seq->type = parse_csi_sequence(final_char, parser->params, parser->param_count, parser->private_mode);
    seq->final_char = final_char;
    seq->private_mode = parser->private_mode;
    seq->param_count = parser->param_count;
    seq->colon_mask = parser->colon_mask;
    
    /* Colon sub-parameters only have a meaning in SGR */
    if (parser->colon_mask && final_char != 'm') {
        seq->type = VT100_SEQ_UNKNOWN;
    }
    
    /* Copy parameters */
    for (int i = 0; i < parser->param_count && i < VT100_MAX_PARAMS; i++) {
        seq->params[i] = parser->params[i];
    }
    
    /* Default parameters for common sequences */
    if (seq->param_count == 0) {
        switch (seq->type) {
            case VT100_SEQ_CURSOR_UP:
            case VT100_SEQ_CURSOR_DOWN:
            case VT100_SEQ_CURSOR_FORWARD:
            case VT100_SEQ_CURSOR_BACK:
            case VT100_SEQ_SCROLL_UP:
            case VT100_SEQ_SCROLL_DOWN:
                seq->params[0] = 1;
                seq->param_count = 1;
                break;
            case VT100_SEQ_CURSOR_POS:
                seq->params[0] = 1;
                seq->params[1] = 1;
                seq->param_count = 2;
                break;
            case VT100_SEQ_ERASE_DISPLAY:
            case VT100_SEQ_ERASE_LINE:
                seq->params[0] = 0;
                seq->param_count = 1;
                break;
            default:
                break;
        }
    }
    
    parser->state = VT100_STATE_NORMAL;
    reset_sequence(parser);
    return true;
}

/* Finalize OSC sequence */
static bool finalize_osc(vt100_parser_t *parser, vt100_sequence_t *seq) {
    /* Parse OSC command */
    if (parser->osc_len > 0 && parser->osc_buffer[0] >= '0' && parser->osc_buffer[0] <= '9') {
        int cmd = parser->osc_buffer[0] - '0';
        if (parser->osc_len > 1 && parser->osc_buffer[1] >= '0' && parser->osc_buffer[1] <= '9') {
            cmd = cmd * 10 + (parser->osc_buffer[1] - '0');
        }
        
        /* OSC 0 or 2 = Set window title */
        if (cmd == 0 || cmd == 2) {
            seq->type = VT100_SEQ_TITLE;
            
            /* Find semicolon separator */
            char *sep = strchr(parser->osc_buffer, ';');
            if (sep && sep < parser->osc_buffer + parser->osc_len) {
                size_t title_len = parser->osc_len - (sep - parser->osc_buffer + 1);
                if (title_len > 0 && title_len < sizeof(seq->text)) {
                    memcpy(seq->text, sep + 1, title_len);
                    seq->text[title_len] = '\0';
                    seq->text_len = title_len;
                }
            }
        } else {
            seq->type = VT100_SEQ_UNKNOWN;
        }
    } else {
        seq->type = VT100_SEQ_UNKNOWN;
    }
    
    parser->state = VT100_STATE_NORMAL;
    reset_sequence(parser);
    return true;
}

bool vt100_parser_feed(vt100_parser_t *parser, char ch, vt100_sequence_t *seq) {
    memset(seq, 0, sizeof(*seq));
    
    switch (parser->state) {
        case VT100_STATE_NORMAL:
            /* Handle control characters */
            if (ch == CTRL_ESC) {
                parser->state = VT100_STATE_ESC;
                reset_sequence(parser);
                return false;
            } else if (ch == CTRL_BEL) {
                seq->type = VT100_SEQ_BELL;
                return true;
            } else if (ch == CTRL_BS) {
                seq->type = VT100_SEQ_BACKSPACE;
                return true;
            } else if (ch == CTRL_HT) {
                seq->type = VT100_SEQ_TAB;
                return true;
            } else if (ch == CTRL_LF) {
                seq->type = VT100_SEQ_NEWLINE;
                return true;
            } else if (ch == CTRL_CR) {
                seq->type = VT100_SEQ_CARRIAGE_RETURN;
                return true;
            } else if (vt100_is_printable(ch)) {
                seq->type = VT100_SEQ_PRINT;
                seq->text[0] = ch;
                seq->text[1] = '\0';
                seq->text_len = 1;
                return true;
            } else if ((unsigned char)ch >= 0x80) {
                /* Pass through high bytes (UTF-8 continuation/lead bytes) */
                seq->type = VT100_SEQ_PRINT;
                seq->text[0] = ch;
                seq->text[1] = '\0';
                seq->text_len = 1;
                return true;
            }
            /* Ignore other control characters */
            return false;
            
        case VT100_STATE_ESC:
            if (ch == '[') {
                parser->state = VT100_STATE_CSI;
                return false;
            } else if (ch == ']') {
                parser->state = VT100_STATE_OSC;
                return false;
            } else if (ch == '(' || ch == ')') {
                parser->state = VT100_STATE_CHARSET;
                return false;
            } else if (ch == '7') {
                /* ESC 7 - Save cursor */
                seq->type = VT100_SEQ_SAVE_CURSOR;
                parser->state = VT100_STATE_NORMAL;
                return true;
            } else if (ch == '8') {
                /* ESC 8 - Restore cursor */
                seq->type = VT100_SEQ_RESTORE_CURSOR;
                parser->state = VT100_STATE_NORMAL;
                return true;
            } else if (ch == 'M') {
                /* ESC M - Reverse index (scroll down) */
                seq->type = VT100_SEQ_SCROLL_DOWN;
                seq->params[0] = 1;
                seq->param_count = 1;
                parser->state = VT100_STATE_NORMAL;
                return true;
            } else {
                /* Unknown escape sequence */
                parser->state = VT100_STATE_NORMAL;
                return false;
            }
            
        case VT100_STATE_CSI:
            if (ch == '?') {
                /* Private mode indicator */
                parser->private_mode = true;
                return false;
            } else if (ch >= '0' && ch <= '9') {
                /* Build parameter with overflow check */
                if (parser->current_param > (INT_MAX - 9) / 10) {
                    parser->current_param = INT_MAX;
                } else {
                    parser->current_param = parser->current_param * 10 + (ch - '0');
                }
                return false;
            } else if (ch == ';') {
                /* Parameter separator */
                if (parser->param_count < VT100_MAX_PARAMS) {
                    parser->params[parser->param_count++] = parser->current_param;
                    parser->current_param = 0;
                }
                return false;
            } else if (ch == ':') {
                /* Sub-parameter separator (38:2::r:g:b, 4:3) */
                if (parser->param_count < VT100_MAX_PARAMS) {
                    parser->params[parser->param_count++] = parser->current_param;
                    parser->current_param = 0;
                    if (parser->param_count < VT100_MAX_PARAMS) {
                        parser->colon_mask |= 1u << parser->param_count;
                    }
                }
                return false;
            } else if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                /* Final character - complete sequence */
                return finalize_csi(parser, ch, seq);
            } else {
                /* Invalid character - abort */
                parser->state = VT100_STATE_NORMAL;
                reset_sequence(parser);
                return false;
            }
            
        case VT100_STATE_OSC:
            if (ch == CTRL_BEL || ch == '\\') {
                /* OSC terminator (BEL or ESC \) */
                return finalize_osc(parser, seq);
            } else if (ch == CTRL_ESC) {
                /* Might be ESC \ terminator */
                return false;
            } else if (parser->osc_len < sizeof(parser->osc_buffer) - 1) {
                /* Accumulate OSC string */
                parser->osc_buffer[parser->osc_len++] = ch;
                return false;
            } else {
                /* Buffer overflow - abort */
                parser->state = VT100_STATE_NORMAL;
                reset_sequence(parser);
                return false;
            }
            
        case VT100_STATE_CHARSET:
            /* Charset selection - ignore for now */
            parser->state = VT100_STATE_NORMAL;
            return false;
    }
    
    return false;
}

void vt100_parser_parse(vt100_parser_t *parser, const char *data, size_t len,
                        vt100_callback_t callback, void *user_data) {
    vt100_sequence_t seq;
    
    for (size_t i = 0; i < len; i++) {
        if (vt100_parser_feed(parser, data[i], &seq)) {
            if (callback) {
                callback(&seq, user_data);
            }
        }
    }
}

const char *vt100_seq_type_name(vt100_seq_type_t type) {
    switch (type) {
        case VT100_SEQ_NONE: return "NONE";
        case VT100_SEQ_PRINT: return "PRINT";
        case VT100_SEQ_CURSOR_UP: return "CURSOR_UP";
        case VT100_SEQ_CURSOR_DOWN: return "CURSOR_DOWN";
        case VT100_SEQ_CURSOR_FORWARD: return "CURSOR_FORWARD";
        case VT100_SEQ_CURSOR_BACK: return "CURSOR_BACK";
        case VT100_SEQ_CURSOR_POS: return "CURSOR_POS";
        case VT100_SEQ_ERASE_DISPLAY: return "ERASE_DISPLAY";
        case VT100_SEQ_ERASE_LINE: return "ERASE_LINE";
        case VT100_SEQ_SGR: return "SGR";
        case VT100_SEQ_SAVE_CURSOR: return "SAVE_CURSOR";
        case VT100_SEQ_RESTORE_CURSOR: return "RESTORE_CURSOR";
        case VT100_SEQ_SCROLL_UP: return "SCROLL_UP";
        case VT100_SEQ_SCROLL_DOWN: return "SCROLL_DOWN";
        case VT100_SEQ_NEWLINE: return "NEWLINE";
        case VT100_SEQ_CARRIAGE_RETURN: return "CARRIAGE_RETURN";
        case VT100_SEQ_TAB: return "TAB";
        case VT100_SEQ_BACKSPACE: return "BACKSPACE";
        case VT100_SEQ_BELL: return "BELL";
        case VT100_SEQ_SET_MODE: return "SET_MODE";
        case VT100_SEQ_RESET_MODE: return "RESET_MODE";
        case VT100_SEQ_DEVICE_STATUS: return "DEVICE_STATUS";
        case VT100_SEQ_TITLE: return "TITLE";
        case VT100_SEQ_UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}
