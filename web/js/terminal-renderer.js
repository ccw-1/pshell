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
        // Split by ANSI escape sequences
        const ansiRegex = /\x1b\[([0-9;]*)m/g;
        let lastIndex = 0;
        let match;

        while ((match = ansiRegex.exec(text)) !== null) {
            // Add text before escape sequence
            if (match.index > lastIndex) {
                const plainText = text.substring(lastIndex, match.index);
                this.addText(plainText);
            }

            // Process escape sequence
            const codes = match[1].split(';').map(c => parseInt(c) || 0);
            this.processSGR(codes);

            lastIndex = match.index + match[0].length;
        }

        // Add remaining text
        if (lastIndex < text.length) {
            this.addText(text.substring(lastIndex));
        }

        this.render();
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
