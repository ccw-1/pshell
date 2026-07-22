# Escape Sequence Reference

## Overview

pshell's `send` command supports comprehensive escape sequence processing, allowing you to send special keys, control characters, and terminal sequences to sessions. This enables automation of interactive applications that require keyboard input beyond simple text.

## Supported Escape Sequences

### Standard Escapes

| Sequence | Description | ASCII Value |
|----------|-------------|-------------|
| `\n` | Newline (Line Feed) | 10 (0x0A) |
| `\r` | Carriage Return | 13 (0x0D) |
| `\t` | Horizontal Tab | 9 (0x09) |
| `\e` | Escape key | 27 (0x1B) |
| `\\` | Literal backslash | 92 (0x5C) |
| `\0` | Null character | 0 (0x00) |
| `\a` | Alert (Bell) | 7 (0x07) |
| `\b` | Backspace | 8 (0x08) |
| `\f` | Form Feed | 12 (0x0C) |
| `\v` | Vertical Tab | 11 (0x0B) |

### Control Characters

Control characters are generated using `\cX` or `\cX` where X is A-Z:

| Sequence | Description | ASCII Value | Common Use |
|----------|-------------|-------------|------------|
| `\cA` | Ctrl-A | 1 | Start of line (bash) |
| `\cB` | Ctrl-B | 2 | Move backward (bash) |
| `\cC` | Ctrl-C | 3 | Interrupt (SIGINT) |
| `\cD` | Ctrl-D | 4 | EOF / Exit |
| `\cE` | Ctrl-E | 5 | End of line (bash) |
| `\cF` | Ctrl-F | 6 | Move forward (bash) |
| `\cK` | Ctrl-K | 11 | Kill to end of line |
| `\cL` | Ctrl-L | 12 | Clear screen |
| `\cU` | Ctrl-U | 21 | Kill entire line |
| `\cZ` | Ctrl-Z | 26 | Suspend (SIGTSTP) |

**Note**: Both uppercase and lowercase work: `\cC` and `\cc` are equivalent.

### Function Keys (F1-F12)

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\F1` | ESC O P | Function key F1 |
| `\F2` | ESC O Q | Function key F2 |
| `\F3` | ESC O R | Function key F3 |
| `\F4` | ESC O S | Function key F4 |
| `\F5` | ESC [ 15 ~ | Function key F5 |
| `\F6` | ESC [ 17 ~ | Function key F6 |
| `\F7` | ESC [ 18 ~ | Function key F7 |
| `\F8` | ESC [ 19 ~ | Function key F8 |
| `\F9` | ESC [ 20 ~ | Function key F9 |
| `\F10` | ESC [ 21 ~ | Function key F10 |
| `\F11` | ESC [ 23 ~ | Function key F11 |
| `\F12` | ESC [ 24 ~ | Function key F12 |

### Arrow Keys

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\UP` | ESC [ A | Up arrow |
| `\DOWN` | ESC [ B | Down arrow |
| `\LEFT` | ESC [ D | Left arrow |
| `\RIGHT` | ESC [ C | Right arrow |

### Special Keys

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\HOME` | ESC [ H | Home key |
| `\END` | ESC [ F | End key |
| `\INS` | ESC [ 2 ~ | Insert key |
| `\DEL` | ESC [ 3 ~ | Delete key |
| `\PGUP` | ESC [ 5 ~ | Page Up |
| `\PGDN` | ESC [ 6 ~ | Page Down |

### Shift+Key Combinations

Shift modifier keys are useful for text selection and extended function keys:

#### Shift+Arrow Keys (Text Selection)

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\SHIFT_UP` | ESC [ 1 ; 2 A | Shift+Up arrow |
| `\SHIFT_DOWN` | ESC [ 1 ; 2 B | Shift+Down arrow |
| `\SHIFT_LEFT` | ESC [ 1 ; 2 D | Shift+Left arrow |
| `\SHIFT_RIGHT` | ESC [ 1 ; 2 C | Shift+Right arrow |

#### Shift+Tab

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\SHIFT_TAB` | ESC [ Z | Shift+Tab (backtab) |

#### Shift+Function Keys (F13-F24)

| Sequence | Terminal Sequence | Description |
|----------|-------------------|-------------|
| `\SHIFT_F1` | ESC [ 25 ~ | Shift+F1 (F13) |
| `\SHIFT_F2` | ESC [ 26 ~ | Shift+F2 (F14) |
| `\SHIFT_F3` | ESC [ 28 ~ | Shift+F3 (F15) |
| `\SHIFT_F4` | ESC [ 29 ~ | Shift+F4 (F16) |
| `\SHIFT_F5` | ESC [ 31 ~ | Shift+F5 (F17) |
| `\SHIFT_F6` | ESC [ 32 ~ | Shift+F6 (F18) |
| `\SHIFT_F7` | ESC [ 33 ~ | Shift+F7 (F19) |
| `\SHIFT_F8` | ESC [ 34 ~ | Shift+F8 (F20) |
| `\SHIFT_F9` | ESC [ 23 ; 2 ~ | Shift+F9 (F21) |
| `\SHIFT_F10` | ESC [ 24 ; 2 ~ | Shift+F10 (F22) |
| `\SHIFT_F11` | ESC [ 25 ; 2 ~ | Shift+F11 (F23) |
| `\SHIFT_F12` | ESC [ 26 ; 2 ~ | Shift+F12 (F24) |

### Numeric Escapes

#### Hexadecimal

Format: `\xHH` where HH is a two-digit hexadecimal value (00-FF)

Examples:
- `\x03` = Ctrl-C (ASCII 3)
- `\x1b` = ESC (ASCII 27)
- `\x7f` = DEL (ASCII 127)

#### Octal

Format: `\NNN` where NNN is a three-digit octal value (000-377)

Examples:
- `\033` = ESC (ASCII 27)
- `\012` = Newline (ASCII 10)
- `\177` = DEL (ASCII 127)

## Usage Examples

### pshellctl

**Important:** Use single quotes (`'`) to prevent shell interpretation of escape sequences. Double quotes (`"`) allow the shell to interpret escapes before pshellctl sees them.

```bash
# Send newline (use single quotes!)
pshellctl send $SESSION 'ls -la\n'

# Send Ctrl-C to interrupt
pshellctl send $SESSION '\cC'

# Send Ctrl-D to exit
pshellctl send $SESSION '\cD'

# Navigate with arrow keys
pshellctl send $SESSION '\UP\UP\n'

# Use function keys
pshellctl send $SESSION '\F1'

# Clear screen
pshellctl send $SESSION '\cL'

# Select text with Shift+arrows
pshellctl send $SESSION '\SHIFT_RIGHT\SHIFT_RIGHT\SHIFT_RIGHT'

# Backtab (Shift+Tab)
pshellctl send $SESSION '\SHIFT_TAB'

# Complex sequence: Home, Shift+End (select line), Delete
pshellctl send $SESSION '\HOME\SHIFT_END\DEL'

# Hex escape for Ctrl-C
pshellctl send $SESSION '\x03'

# Octal escape for ESC
pshellctl send $SESSION '\033'

# If you must use double quotes, escape the backslash
pshellctl send $SESSION "ls -la\\n"  # Shell sees \\n, pshellctl sees \n
```

### REST API

```bash
# Send with escape sequences
curl -X POST http://localhost:8080/api/sessions/$SESSION/send \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"text":"ls -la\\n"}'

# Send Ctrl-C
curl -X POST http://localhost:8080/api/sessions/$SESSION/send \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"text":"\\cC"}'

# Send function key
curl -X POST http://localhost:8080/api/sessions/$SESSION/send \
  -H "X-API-Key: <your-generated-key>" \
  -H "Content-Type: application/json" \
  -d '{"text":"\\F1"}'
```

## Common Use Cases

### 1. Interactive Shell Navigation

```bash
# Go to start of line, clear to end
pshellctl send $SESSION '\cA\cK'

# Go to end of line
pshellctl send $SESSION '\cE'

# Previous command (up arrow + enter)
pshellctl send $SESSION '\UP\n'
```

### 2. Text Editor Control

```bash
# Vim: Save and quit
pshellctl send $SESSION '\e:wq\n'

# Emacs: Save file
pshellctl send $SESSION '\cX\cS'

# Nano: Exit
pshellctl send $SESSION '\cX'
```

### 3. Application Automation

```bash
# SSH password prompt
pshellctl expect $SESSION "password:"
pshellctl send $SESSION 'mypassword\n'

# Menu navigation with function keys
pshellctl send $SESSION '\F1'  # Help
pshellctl send $SESSION '\F10' # Exit
```

### 4. Text Selection and Editing

```bash
# Select word (Shift+Right x5, then copy)
pshellctl send $SESSION '\SHIFT_RIGHT\SHIFT_RIGHT\SHIFT_RIGHT\SHIFT_RIGHT\SHIFT_RIGHT\cC'

# Select entire line
pshellctl send $SESSION '\HOME\SHIFT_END'

# Delete selected text
pshellctl send $SESSION '\DEL'
```

### 5. Terminal Control

```bash
# Clear screen
pshellctl send $SESSION '\cL'

# Reset terminal
pshellctl send $SESSION '\ec'

# Send break signal
pshellctl send $SESSION '\cC'
```

## Implementation Notes

### Escape Processing

- Escape sequences are processed **in-place** before sending to the PTY
- Multiple escape sequences can be combined in a single send command
- Invalid escape sequences are passed through literally
- Processing is done by `str_unescape()` in `src/utils/string_utils.c`

### Terminal Compatibility

- Sequences are xterm-compatible (works with most modern terminals)
- VT100/ANSI escape sequences are used
- Some applications may interpret sequences differently
- Test with your specific application for compatibility

### Limitations

1. **No Alt/Meta Keys**: Alt+key combinations are not currently supported
2. **No Ctrl+Shift**: Ctrl+Shift combinations are not supported
3. **Application-Specific**: Some applications may not respond to all sequences
4. **Terminal Type**: Behavior depends on TERM environment variable

## Troubleshooting

### Sequence Not Working

1. **Check Terminal Type**: Ensure TERM is set correctly (usually `xterm-256color`)
2. **Test Manually**: Try the sequence in a regular terminal first
3. **Application Support**: Not all applications support all sequences
4. **Escape Properly**: In JSON, use double backslashes: `"\\cC"`

### Debugging

```bash
# Enable debug logging
pshell -d

# View raw bytes sent
pshellctl send $SESSION '\cC' 2>&1 | od -c

# Check terminal type
pshellctl send $SESSION 'echo $TERM\n'
```

## Reference

- **xterm Control Sequences**: https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
- **VT100 User Guide**: https://vt100.net/docs/vt100-ug/
- **ANSI Escape Codes**: https://en.wikipedia.org/wiki/ANSI_escape_code
