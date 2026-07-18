/**
 * pshell Web Application
 * Main application logic
 */

class PShellApp {
    constructor() {
        this.apiClient = new PShellAPIClient();
        this.terminalRenderer = null;
        this.currentSessionId = null;
        this.sessions = new Map();
        this.pollInterval = null;
        this.pollIntervalMs = 500; // Poll every 500ms

        this.initializeUI();
        this.loadSessions();
        this.startHealthCheck();
    }

    /**
     * Initialize UI event handlers
     */
    initializeUI() {
        // New session button
        document.getElementById('new-session-btn').addEventListener('click', () => {
            this.showModal('new-session-modal');
        });

        // Refresh button
        document.getElementById('refresh-btn').addEventListener('click', () => {
            this.loadSessions();
        });

        // Create session
        document.getElementById('create-session-btn').addEventListener('click', () => {
            this.createSession();
        });

        // Clear terminal
        document.getElementById('clear-btn').addEventListener('click', () => {
            if (this.terminalRenderer) {
                this.terminalRenderer.clear();
            }
        });

        // Resize terminal
        document.getElementById('resize-btn').addEventListener('click', () => {
            this.showModal('resize-modal');
        });

        // Resize confirm
        document.getElementById('resize-confirm-btn').addEventListener('click', () => {
            this.resizeTerminal();
        });

        // Close session
        document.getElementById('close-btn').addEventListener('click', () => {
            this.closeSession();
        });

        // Command input - handle all keypresses
        const commandInput = document.getElementById('command-input');
        commandInput.addEventListener('keydown', (e) => {
            if (!this.currentSessionId) return;
            
            // Prevent default for special keys we handle
            if (e.key === 'Enter' || e.key === 'Tab' || e.key === 'Backspace') {
                e.preventDefault();
            }
            
            // Send character directly to session
            let char = null;
            if (e.key === 'Enter') {
                char = '\n';  // Send newline for Enter
            } else if (e.key === 'Tab') {
                char = '\t';
            } else if (e.key === 'Backspace') {
                char = '\x7f';  // DEL character
            } else if (e.key.length === 1 && !e.ctrlKey && !e.altKey && !e.metaKey) {
                char = e.key;
            } else if (e.ctrlKey && e.key.length === 1) {
                // Handle Ctrl+key combinations
                const code = e.key.toUpperCase().charCodeAt(0);
                if (code >= 65 && code <= 90) {
                    char = String.fromCharCode(code - 64);  // Ctrl+A = 0x01, etc.
                }
            }
            
            if (char !== null) {
                console.log('Sending char:', char.charCodeAt(0), 'key:', e.key);
                this.sendInput(char);
            }
        });
        
        // Disable the input field's default behavior
        commandInput.addEventListener('keypress', (e) => {
            e.preventDefault();
        });

        // Modal close buttons
        document.querySelectorAll('.modal-close').forEach(btn => {
            btn.addEventListener('click', (e) => {
                const modal = e.target.closest('.modal');
                this.hideModal(modal.id);
            });
        });

        // Click outside modal to close
        document.querySelectorAll('.modal').forEach(modal => {
            modal.addEventListener('click', (e) => {
                if (e.target === modal) {
                    this.hideModal(modal.id);
                }
            });
        });

        // Initialize terminal renderer
        const terminalElement = document.getElementById('terminal');
        this.terminalRenderer = new TerminalRenderer(terminalElement);
    }

    /**
     * Show modal
     */
    showModal(modalId) {
        document.getElementById(modalId).classList.add('active');
    }

    /**
     * Hide modal
     */
    hideModal(modalId) {
        document.getElementById(modalId).classList.remove('active');
    }

    /**
     * Load sessions from API
     */
    async loadSessions() {
        try {
            const response = await this.apiClient.listSessions();
            const sessions = response.sessions || [];

            // Update sessions map
            this.sessions.clear();
            sessions.forEach(session => {
                this.sessions.set(session.id, session);
            });

            // Render session list
            this.renderSessionList();

            // If current session no longer exists, clear it
            if (this.currentSessionId && !this.sessions.has(this.currentSessionId)) {
                this.currentSessionId = null;
                this.showWelcomeScreen();
            }
        } catch (error) {
            console.error('Failed to load sessions:', error);
            this.showError('Failed to load sessions');
        }
    }

    /**
     * Render session list
     */
    renderSessionList() {
        const sessionList = document.getElementById('session-list');
        sessionList.innerHTML = '';

        if (this.sessions.size === 0) {
            sessionList.innerHTML = '<div class="empty-state">No sessions</div>';
            return;
        }

        this.sessions.forEach((session, id) => {
            const item = document.createElement('div');
            item.className = 'session-item';
            if (id === this.currentSessionId) {
                item.classList.add('active');
            }

            const title = document.createElement('div');
            title.className = 'session-item-title';
            title.textContent = session.command || 'Session';

            const info = document.createElement('div');
            info.className = 'session-item-info';
            info.textContent = `${session.rows}x${session.cols} • PID ${session.pid}`;

            item.appendChild(title);
            item.appendChild(info);

            item.addEventListener('click', () => {
                this.selectSession(id);
            });

            sessionList.appendChild(item);
        });
    }

    /**
     * Create new session
     */
    async createSession() {
        const command = document.getElementById('session-command').value;
        const rows = parseInt(document.getElementById('session-rows').value);
        const cols = parseInt(document.getElementById('session-cols').value);

        try {
            const response = await this.apiClient.createSession(command, rows, cols);
            this.hideModal('new-session-modal');
            
            // Reload sessions and select new one
            await this.loadSessions();
            this.selectSession(response.id);
        } catch (error) {
            console.error('Failed to create session:', error);
            this.showError('Failed to create session');
        }
    }

    /**
     * Select session
     */
    async selectSession(sessionId) {
        // Stop polling previous session
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }

        this.currentSessionId = sessionId;
        const session = this.sessions.get(sessionId);

        // Update UI
        document.getElementById('terminal-header').style.display = 'flex';
        document.getElementById('terminal-input').style.display = 'block';
        document.getElementById('terminal-title').textContent = session.command;
        document.getElementById('terminal-size').textContent = `${session.rows}x${session.cols}`;

        // Load screen state
        try {
            const screen = await this.apiClient.getScreen(sessionId);
            this.terminalRenderer.setScreen(screen.text, screen.cursor_row, screen.cursor_col);
        } catch (error) {
            console.error('Failed to load screen:', error);
            this.terminalRenderer.clear();
        }

        // Start polling for output
        this.startPolling();

        // Render session list to update active state
        this.renderSessionList();

        // Focus input
        document.getElementById('command-input').focus();
    }

    /**
     * Start polling for output
     */
    startPolling() {
        this.pollInterval = setInterval(async () => {
            if (!this.currentSessionId) {
                return;
            }

            try {
                const response = await this.apiClient.readOutput(this.currentSessionId);
                if (response.output && response.output.length > 0) {
                    console.log('Received output:', JSON.stringify(response.output), 'length:', response.output.length);
                    console.log('Output bytes:', Array.from(response.output).map(c => c.charCodeAt(0)));
                    this.terminalRenderer.appendOutput(response.output);
                }
            } catch (error) {
                console.error('Failed to read output:', error);
                // Session might be dead, reload sessions
                this.loadSessions();
            }
        }, this.pollIntervalMs);
    }

    /**
     * Send single character/string to session
     */
    async sendInput(text) {
        if (!text || !this.currentSessionId) {
            return;
        }

        try {
            console.log('sendInput called with:', JSON.stringify(text), 'bytes:', Array.from(text).map(c => c.charCodeAt(0)));
            await this.apiClient.sendInput(this.currentSessionId, text);
        } catch (error) {
            console.error('Failed to send input:', error);
            this.showError('Failed to send input');
        }
    }
    
    /**
     * Send command to session (legacy - kept for compatibility)
     */
    async sendCommand() {
        const input = document.getElementById('command-input');
        const text = input.value;

        if (!text || !this.currentSessionId) {
            return;
        }

        try {
            await this.apiClient.sendInput(this.currentSessionId, text + '\n');
            input.value = '';
        } catch (error) {
            console.error('Failed to send input:', error);
            this.showError('Failed to send input');
        }
    }

    /**
     * Resize terminal
     */
    async resizeTerminal() {
        const rows = parseInt(document.getElementById('resize-rows').value);
        const cols = parseInt(document.getElementById('resize-cols').value);

        if (!this.currentSessionId) {
            return;
        }

        try {
            await this.apiClient.resizeTerminal(this.currentSessionId, rows, cols);
            this.hideModal('resize-modal');
            
            // Update session info
            const session = this.sessions.get(this.currentSessionId);
            session.rows = rows;
            session.cols = cols;
            document.getElementById('terminal-size').textContent = `${rows}x${cols}`;
            
            // Reload screen
            const screen = await this.apiClient.getScreen(this.currentSessionId);
            this.terminalRenderer.setScreen(screen.text, screen.cursor_row, screen.cursor_col);
        } catch (error) {
            console.error('Failed to resize terminal:', error);
            this.showError('Failed to resize terminal');
        }
    }

    /**
     * Close current session
     */
    async closeSession() {
        if (!this.currentSessionId) {
            return;
        }

        if (!confirm('Close this session?')) {
            return;
        }

        try {
            await this.apiClient.destroySession(this.currentSessionId);
            
            // Stop polling
            if (this.pollInterval) {
                clearInterval(this.pollInterval);
                this.pollInterval = null;
            }

            this.currentSessionId = null;
            this.showWelcomeScreen();
            this.loadSessions();
        } catch (error) {
            console.error('Failed to close session:', error);
            this.showError('Failed to close session');
        }
    }

    /**
     * Show welcome screen
     */
    showWelcomeScreen() {
        document.getElementById('terminal-header').style.display = 'none';
        document.getElementById('terminal-input').style.display = 'none';
        
        const terminal = document.getElementById('terminal');
        terminal.innerHTML = `
            <div class="welcome-screen">
                <h2>Welcome to pshell</h2>
                <p>Create a new session or select an existing one to get started.</p>
            </div>
        `;
    }

    /**
     * Show error message
     */
    showError(message) {
        // Simple error display - could be enhanced with a toast/notification system
        alert(message);
    }

    /**
     * Start health check
     */
    startHealthCheck() {
        setInterval(async () => {
            try {
                await this.apiClient.healthCheck();
                document.getElementById('status').style.color = 'var(--success-color)';
            } catch (error) {
                document.getElementById('status').style.color = 'var(--danger-color)';
            }
        }, 5000); // Check every 5 seconds
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new PShellApp();
});
