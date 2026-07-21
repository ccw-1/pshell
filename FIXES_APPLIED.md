# Web Interface Fixes Applied

## Date: 2026-07-21

## Issues Fixed

### 1. Endless Scrolling/Output Loop
**Problem**: Web interface continuously displayed duplicate output, causing endless scrolling.

**Root Cause**: REST API was reading from screen buffer which doesn't consume data on read, causing the same output to be returned repeatedly during polling.

**Solution**: Modified `src/http/rest_api.c` to use `session_read_raw_output()` instead of `session_read_output()`. The raw buffer is a circular buffer that consumes data on read, preventing duplicates.

**Files Changed**:
- `src/http/rest_api.c` (line ~566)

### 2. Scattered Text Display
**Problem**: Terminal output appeared scattered across multiple lines with missing carriage returns, making text unreadable.

**Root Cause**: **Double CR conversion** - Terminal output already contained some CRLF sequences (`\r\n`), but the JavaScript was blindly converting ALL `\n` to `\r\n`, creating `\r\r\n` sequences that confused xterm.js cursor positioning.

**Solution (3 layers)**:
1. **Screen Buffer** (`src/terminal/screen_buffer.c`): `screen_buffer_newline()` sets `cursor.col = 0` to make LF behave as CRLF in the internal buffer
2. **Frontend** (`web/js/app.js`): Smart CRLF conversion using `replace(/([^\r])\n/g, '$1\r\n')` - only converts bare LF (not preceded by CR) to avoid double-converting existing CRLF sequences
3. **HTTP Server** (`src/http/http_server.c`): Added no-cache headers for .js and .css files to prevent browser caching issues

**Files Changed**:
- `src/terminal/screen_buffer.c` (line ~446)
- `web/js/app.js` (line ~284-289)
- `src/http/http_server.c` (added Cache-Control headers)

## Testing

### Socket API (pshellctl)
✅ Perfect output formatting - each line on its own row

### Web Interface
✅ Proper line breaks and cursor positioning
✅ No more endless scrolling
✅ Text displays correctly on separate lines
✅ No double CR characters causing scattered text

## Technical Details

The complete fix ensures that:
1. Output is consumed from the buffer on each read (prevents loops)
2. Newlines properly reset cursor to column 0 (prevents scattered text)
3. Smart CRLF conversion prevents double-converting existing CRLF sequences
4. Browser loads fresh JavaScript without caching issues

## Git Commits
- Initial fixes: `aa4ed7d`
- Final double CR fix: `1ad9b6a`