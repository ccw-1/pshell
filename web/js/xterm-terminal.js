/**
 * XTerm Terminal Wrapper
 * Wraps xterm.js for pshell web interface
 */

class XTermTerminal {
    constructor(container) {
        this.container = container;
        this.terminal = null;
        this.fitAddon = null;
        this.webLinksAddon = null;
    }

    /**
     * Initialize terminal
     */
    init() {
        // Create terminal instance
        this.terminal = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            fontFamily: 'Courier New, monospace',
            convertEol: false,
            disableStdin: false,
            localEcho: false,
            theme: {
                background: '#000000',
                foreground: '#ffffff',
                cursor: '#ffffff',
                cursorAccent: '#000000',
                selection: 'rgba(255, 255, 255, 0.3)',
                black: '#000000',
                red: '#cd3131',
                green: '#0dbc79',
                yellow: '#e5e510',
                blue: '#2472c8',
                magenta: '#bc3fbc',
                cyan: '#11a8cd',
                white: '#e5e5e5',
                brightBlack: '#666666',
                brightRed: '#f14c4c',
                brightGreen: '#23d18b',
                brightYellow: '#f5f543',
                brightBlue: '#3b8eea',
                brightMagenta: '#d670d6',
                brightCyan: '#29b8db',
                brightWhite: '#ffffff'
            },
            allowProposedApi: true
        });

        // Load addons
        this.fitAddon = new FitAddon.FitAddon();
        this.terminal.loadAddon(this.fitAddon);

        this.webLinksAddon = new WebLinksAddon.WebLinksAddon();
        this.terminal.loadAddon(this.webLinksAddon);

        // Open terminal in container
        this.terminal.open(this.container);
        
        // Fit to container
        this.fitAddon.fit();

        // Handle window resize
        window.addEventListener('resize', () => {
            this.fitAddon.fit();
        });

        console.log('XTerm terminal initialized:', this.terminal.cols, 'x', this.terminal.rows);
    }

    /**
     * Get terminal dimensions
     */
    getDimensions() {
        return {
            rows: this.terminal.rows,
            cols: this.terminal.cols
        };
    }

    /**
     * Write data to terminal
     */
    write(data) {
        if (this.terminal) {
            this.terminal.write(data);
        }
    }

    /**
     * Clear terminal
     */
    clear() {
        if (this.terminal) {
            this.terminal.clear();
        }
    }

    /**
     * Focus terminal
     */
    focus() {
        if (this.terminal) {
            this.terminal.focus();
        }
    }

    /**
     * Set up input handler
     */
    onData(callback) {
        if (this.terminal) {
            this.terminal.onData(callback);
        }
    }

    /**
     * Set up resize handler
     */
    onResize(callback) {
        if (this.terminal) {
            this.terminal.onResize(callback);
        }
    }

    /**
     * Resize terminal
     */
    resize(rows, cols) {
        if (this.terminal) {
            this.terminal.resize(cols, rows);
        }
    }

    /**
     * Fit terminal to container
     */
    fit() {
        if (this.fitAddon) {
            this.fitAddon.fit();
        }
    }

    /**
     * Dispose terminal
     */
    dispose() {
        if (this.terminal) {
            this.terminal.dispose();
            this.terminal = null;
        }
    }
}

// Export for use in other scripts
window.XTermTerminal = XTermTerminal;
