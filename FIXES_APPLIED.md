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

**Root Cause**: Terminal emulation was receiving only Line Feed (LF/\n) without Carriage Return (CR/\r). In terminal emulation, a newline should move to the next line AND return to column 0.

**Solution**: 
- Modified `src/terminal/screen_buffer.c` - `screen_buffer_newline()` now sets `cursor.col = 0` to make LF behave as CRLF
- Modified `web/js/app.js` - Added JavaScript conversion `output.replace(/\n/g, '\r\n')` before writing to xterm.js

**Files Changed**:
- `src/terminal/screen_buffer.c` (line ~446)
- `web/js/app.js` (line ~283)

## Testing

### Socket API (pshellctl)
✅ Perfect output formatting - each line on its own row

### Web Interface
✅ Proper line breaks and cursor positioning
✅ No more endless scrolling
✅ Text displays correctly on separate lines

## Technical Details

The fix ensures that:
1. Output is consumed from the buffer on each read (prevents loops)
2. Newlines properly reset cursor to column 0 (prevents scattered text)
3. Web terminal receives CRLF sequences for proper display
