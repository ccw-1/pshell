/**
 * Terminal Renderer
 * Renders terminal output with VT100/ANSI escape sequence support
 */

class TerminalRenderer {
    constructor(terminalElement) {
        this.terminal = terminalElement;
        this.lines = [];
        this.cursorRow = 0;
        this.cursorCol = 0;
        this.currentStyle = {};
        this.maxLines = 1000; // Scrollback limit
    }

    /**
     * Clear terminal
     */
    clear() {
        this.lines = [];
        this.cursorRow = 0;
        this.cursorCol = 0;
        this.currentStyle = {};
        this.render();
    }

    /**
     * Process and render text with ANSI codes
     */
    processText(text) {
        let i = 0;
        while (i < text.length) {
            const ch = text[i];
            
            // Handle escape sequences
            if (ch === '\x1b' && i + 1 < text.length) {
                const next = text[i + 1];
                
                // CSI sequences: ESC [
                if (next === '[') {
                    const match = text.substring(i).match(/^\x1b\[([0-9;?]*)([A-Za-z@`])/);
                    if (match) {
                        console.log('CSI sequence:', match[0], 'params:', match[1], 'final:', match[2]);
                        this.processCSI(match[1], match[2]);
                        i += match[0].length;
                        continue;
                    }
                }
                
                // OSC sequences: ESC ]
                if (next === ']') {
                    const match = text.substring(i).match(/^\x1b\]([^\x07\x1b]*)([\x07]|\x1b\\)/);
                    if (match) {
                        console.log('OSC sequence:', match[0]);
                        i += match[0].length;
                        continue;
                    }
                }
                
                // Simple escape sequences
                if (next === '7') { // Save cursor
                    console.log('Save cursor at', this.cursorRow, this.cursorCol);
                    this.savedCursor = { row: this.cursorRow, col: this.cursorCol };
                    i += 2;
                    continue;
                }
                if (next === '8') { // Restore cursor
                    console.log('Restore cursor to', this.savedCursor);
                    if (this.savedCursor) {
                        this.cursorRow = this.savedCursor.row;
                        this.cursorCol = this.savedCursor.col;
                    }
                    i += 2;
                    continue;
                }
                
                // Unknown escape sequence - log and skip
                console.warn('Unknown escape sequence at', i, ':', text.substring(i, i + 10));
                i++;
                continue;
            }
            
            // Handle control characters
            if (ch === '\n') {
                this.cursorRow++;
                this.cursorCol = 0;
                i++;
                continue;
            }
            if (ch === '\r') {
                this.cursorCol = 0;
                i++;
                continue;
            }
            if (ch === '\t') {
                this.cursorCol = Math.floor(this.cursorCol / 8) * 8 + 8;
                i++;
                continue;
            }
            if (ch === '\b') {
                if (this.cursorCol > 0) this.cursorCol--;
                i++;
                continue;
            }
            
            // Printable character
            if (ch >= ' ' && ch <= '~') {
                this.putChar(ch);
            } else if (ch < ' ' || ch === '\x7f') {
                // Log unhandled control characters
                console.warn('Unhandled control char:', ch.charCodeAt(0));
            }
            
            i++;
        }

        this.render();
    }
    
    /**
     * Process CSI (Control Sequence Introducer) sequences
     */
    processCSI(params, final) {
        const args = params.split(';').map(p => parseInt(p) || 0);
        
        switch (final) {
            case 'A': // Cursor up
                this.cursorRow = Math.max(0, this.cursorRow - (args[0] || 1));
                break;
            case 'B': // Cursor down
                this.cursorRow += (args[0] || 1);
                break;
            case 'C': // Cursor forward
                this.cursorCol += (args[0] || 1);
                break;
            case 'D': // Cursor back
                this.cursorCol = Math.max(0, this.cursorCol - (args[0] || 1));
                break;
            case 'H': case 'f': // Cursor position
                this.cursorRow = Math.max(0, (args[0] || 1) - 1);
                this.cursorCol = Math.max(0, (args[1] || 1) - 1);
                break;
            case 'J': // Erase display
                this.eraseDisplay(args[0] || 0);
                break;
            case 'K': // Erase line
                this.eraseLine(args[0] || 0);
                break;
            case 'm': // SGR (colors/attributes)
                this.processSGR(args.length > 0 ? args : [0]);
                break;
            case 's': // Save cursor
                this.savedCursor = { row: this.cursorRow, col: this.cursorCol };
                break;
            case 'u': // Restore cursor
                if (this.savedCursor) {
                    this.cursorRow = this.savedCursor.row;
                    this.cursorCol = this.savedCursor.col;
                }
                break;
        }
    }
    
    /**
     * Put character at cursor position
     */
    putChar(ch) {
        // Ensure line exists
        while (this.lines.length <= this.cursorRow) {
            this.lines.push([]);
        }
        
        // Ensure column exists
        const line = this.lines[this.cursorRow];
        while (line.length <= this.cursorCol) {
            line.push({ char: ' ', style: {} });
        }
        
        // Set character with current style
        line[this.cursorCol] = {
            char: ch,
            style: { ...this.currentStyle }
        };
        
        this.cursorCol++;
    }
    
    /**
     * Erase display
     */
    eraseDisplay(mode) {
        switch (mode) {
            case 0: // Cursor to end
                this.eraseLine(0);
                for (let r = this.cursorRow + 1; r < this.lines.length; r++) {
                    this.lines[r] = [];
                }
                break;
            case 1: // Start to cursor
                for (let r = 0; r < this.cursorRow; r++) {
                    this.lines[r] = [];
                }
                this.eraseLine(1);
                break;
            case 2: // Entire screen
            case 3: // Entire screen + scrollback
                this.lines = [];
                this.cursorRow = 0;
                this.cursorCol = 0;
                break;
        }
    }
    
    /**
     * Erase line
     */
    eraseLine(mode) {
        if (this.cursorRow >= this.lines.length) return;
        
        const line = this.lines[this.cursorRow];
        switch (mode) {
            case 0: // Cursor to end
                line.splice(this.cursorCol);
                break;
            case 1: // Start to cursor
                for (let c = 0; c <= this.cursorCol && c < line.length; c++) {
                    line[c] = { char: ' ', style: {} };
                }
                break;
            case 2: // Entire line
                this.lines[this.cursorRow] = [];
                break;
        }
    }

    /**
     * Process SGR (Select Graphic Rendition) codes
     */
    processSGR(codes) {
        for (const code of codes) {
            if (code === 0) {
                // Reset
                this.currentStyle = {};
            } else if (code === 1) {
                this.currentStyle.bold = true;
            } else if (code === 2) {
                this.currentStyle.dim = true;
            } else if (code === 3) {
                this.currentStyle.italic = true;
            } else if (code === 4) {
                this.currentStyle.underline = true;
            } else if (code >= 30 && code <= 37) {
                // Foreground colors
                this.currentStyle.fg = this.getColorClass(code - 30, false);
            } else if (code >= 90 && code <= 97) {
                // Bright foreground colors
                this.currentStyle.fg = this.getColorClass(code - 90, true);
            } else if (code >= 40 && code <= 47) {
                // Background colors
                this.currentStyle.bg = this.getColorClass(code - 40, false);
            } else if (code >= 100 && code <= 107) {
                // Bright background colors
                this.currentStyle.bg = this.getColorClass(code - 100, true);
            }
        }
    }

    /**
     * Get CSS class for color code
     */
    getColorClass(colorCode, bright) {
        const colors = ['black', 'red', 'green', 'yellow', 'blue', 'magenta', 'cyan', 'white'];
        const prefix = bright ? 'ansi-bright-' : 'ansi-';
        return prefix + colors[colorCode];
    }

    /**
     * Add plain text to terminal
     */
    addText(text) {
        for (let i = 0; i < text.length; i++) {
            const char = text[i];

            if (char === '\n') {
                this.cursorRow++;
                this.cursorCol = 0;
                continue;
            }

            if (char === '\r') {
                this.cursorCol = 0;
                continue;
            }

            if (char === '\t') {
                // Tab to next 8-column boundary
                this.cursorCol = Math.floor(this.cursorCol / 8) * 8 + 8;
                continue;
            }

            // Ensure line exists
            while (this.lines.length <= this.cursorRow) {
                this.lines.push([]);
            }

            // Ensure column exists
            const line = this.lines[this.cursorRow];
            while (line.length <= this.cursorCol) {
                line.push({ char: ' ', style: {} });
            }

            // Set character with current style
            line[this.cursorCol] = {
                char: char,
                style: { ...this.currentStyle }
            };

            this.cursorCol++;
        }

        // Trim to max lines
        if (this.lines.length > this.maxLines) {
            this.lines = this.lines.slice(-this.maxLines);
            this.cursorRow = Math.min(this.cursorRow, this.lines.length - 1);
        }
    }

    /**
     * Render terminal to DOM
     */
    render() {
        // Clear terminal
        this.terminal.innerHTML = '';

        // Render each line
        for (let i = 0; i < this.lines.length; i++) {
            const line = this.lines[i];
            const lineDiv = document.createElement('div');
            lineDiv.className = 'terminal-line';

            let currentSpan = null;
            let currentStyleKey = '';

            for (let j = 0; j < line.length; j++) {
                const cell = line[j];
                const styleKey = this.getStyleKey(cell.style);

                // Create new span if style changed
                if (styleKey !== currentStyleKey) {
                    if (currentSpan) {
                        lineDiv.appendChild(currentSpan);
                    }
                    currentSpan = document.createElement('span');
                    this.applyStyle(currentSpan, cell.style);
                    currentStyleKey = styleKey;
                }

                // Add character
                currentSpan.textContent += cell.char;

                // Add cursor if at cursor position
                if (i === this.cursorRow && j === this.cursorCol) {
                    const cursor = document.createElement('span');
                    cursor.className = 'terminal-cursor';
                    cursor.textContent = ' ';
                    currentSpan.appendChild(cursor);
                }
            }

            if (currentSpan) {
                lineDiv.appendChild(currentSpan);
            }

            // Add cursor at end of line if needed
            if (i === this.cursorRow && this.cursorCol >= line.length) {
                const cursor = document.createElement('span');
                cursor.className = 'terminal-cursor';
                cursor.textContent = ' ';
                lineDiv.appendChild(cursor);
            }

            this.terminal.appendChild(lineDiv);
        }

        // Scroll to bottom
        this.terminal.scrollTop = this.terminal.scrollHeight;
    }

    /**
     * Get unique key for style object
     */
    getStyleKey(style) {
        return JSON.stringify(style);
    }

    /**
     * Apply style to element
     */
    applyStyle(element, style) {
        if (style.fg) {
            element.classList.add(style.fg);
        }
        if (style.bg) {
            element.classList.add(style.bg);
        }
        if (style.bold) {
            element.classList.add('ansi-bold');
        }
        if (style.dim) {
            element.classList.add('ansi-dim');
        }
        if (style.italic) {
            element.classList.add('ansi-italic');
        }
        if (style.underline) {
            element.classList.add('ansi-underline');
        }
    }

    /**
     * Set terminal content from screen buffer
     */
    setScreen(screenText, cursorRow, cursorCol) {
        this.clear();
        this.cursorRow = cursorRow || 0;
        this.cursorCol = cursorCol || 0;
        
        if (screenText) {
            this.processText(screenText);
        }
    }

    /**
     * Append output to terminal
     */
    appendOutput(text) {
        if (text) {
            this.processText(text);
        }
    }
}

// Export for use in other scripts
window.TerminalRenderer = TerminalRenderer;
