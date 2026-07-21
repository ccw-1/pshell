/**
 * pshell Web Application
 * Main application logic with xterm.js integration
 */

class PShellApp {
    constructor() {
        this.apiClient = new PShellAPIClient();
        this.terminal = null;
        this.currentSessionId = null;
        this.sessions = new Map();
        this.pollInterval = null;
        this.pollIntervalMs = 100; // Poll every 100ms for responsive output

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
            if (this.terminal) {
                this.terminal.clear();
            }
        });

        // Resize terminal
        document.getElementById('resize-btn').addEventListener('click', () => {
            if (this.terminal && this.currentSessionId) {
                const dims = this.terminal.getDimensions();
                document.getElementById('resize-rows').value = dims.rows;
                document.getElementById('resize-cols').value = dims.cols;
                this.showModal('resize-modal');
            }
        });

        // Resize confirm
        document.getElementById('resize-confirm-btn').addEventListener('click', () => {
            this.resizeTerminal();
        });

        // Close session
        document.getElementById('close-btn').addEventListener('click', () => {
            this.closeSession();
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
     * Initialize terminal for session
     */
    initTerminal() {
        // Clear terminal container
        const terminalContainer = document.getElementById('terminal');
        terminalContainer.innerHTML = '';

        // Create new terminal instance
        this.terminal = new XTermTerminal(terminalContainer);
        this.terminal.init();

        // Set up input handler - send all input to session
        this.terminal.onData((data) => {
            if (this.currentSessionId) {
                // Debug: log what xterm.js is sending
                console.log('Terminal input:', JSON.stringify(data), 'charCodes:', Array.from(data).map(c => c.charCodeAt(0)));
                
                // CRITICAL FIX: xterm.js sends \r for Enter, but we need to ensure it's sent as actual CR
                // The issue is that when data contains \r (0x0D), it needs to stay as 0x0D through JSON
                this.sendInput(data);
            }
        });

        // Set up resize handler
        this.terminal.onResize(({ cols, rows }) => {
            if (this.currentSessionId) {
                this.apiClient.resizeTerminal(this.currentSessionId, rows, cols)
                    .then(() => {
                        const session = this.sessions.get(this.currentSessionId);
                        if (session) {
                            session.rows = rows;
                            session.cols = cols;
                            document.getElementById('terminal-size').textContent = `${rows}x${cols}`;
                        }
                    })
                    .catch(err => console.error('Failed to resize:', err));
            }
        });

        // Focus terminal
        this.terminal.focus();
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
        document.getElementById('terminal-input').style.display = 'none'; // Hide old input field
        document.getElementById('terminal-title').textContent = session.command;
        document.getElementById('terminal-size').textContent = `${session.rows}x${session.cols}`;

        // Initialize terminal
        this.initTerminal();

        // Load initial screen state
        try {
            const screen = await this.apiClient.getScreen(sessionId);
            if (screen.text) {
                this.terminal.write(screen.text);
            }
        } catch (error) {
            console.error('Failed to load screen:', error);
        }

        // Start polling for output
        this.startPolling();

        // Render session list to update active state
        this.renderSessionList();
    }

    /**
     * Start polling for output
     */
    startPolling() {
        this.pollInterval = setInterval(async () => {
            if (!this.currentSessionId || !this.terminal) {
                return;
            }

            try {
                const response = await this.apiClient.readOutput(this.currentSessionId);
                if (response.output && response.output.length > 0) {
                    // Debug: log what we're receiving
                    console.log('Received output:', JSON.stringify(response.output.substring(0, 100)), 
                                'length:', response.output.length,
                                'charCodes:', Array.from(response.output.substring(0, 20)).map(c => c.charCodeAt(0)));
                    
                    // xterm.js needs CRLF for proper line breaks
                    // Simply replace all LF with CRLF (screen buffer handles cursor positioning)
                    let output = response.output.replace(/\n/g, '\r\n');
                    
                    this.terminal.write(output);
                }
            } catch (error) {
                console.error('Failed to read output:', error);
                // Session might be dead, reload sessions
                this.loadSessions();
            }
        }, this.pollIntervalMs);
    }

    /**
     * Send input to session
     */
    async sendInput(data) {
        if (!data || !this.currentSessionId) {
            return;
        }

        try {
            await this.apiClient.sendInput(this.currentSessionId, data);
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

        if (!this.currentSessionId || !this.terminal) {
            return;
        }

        try {
            // Resize on server
            await this.apiClient.resizeTerminal(this.currentSessionId, rows, cols);
            
            // Resize terminal
            this.terminal.resize(rows, cols);
            
            this.hideModal('resize-modal');
            
            // Update session info
            const session = this.sessions.get(this.currentSessionId);
            session.rows = rows;
            session.cols = cols;
            document.getElementById('terminal-size').textContent = `${rows}x${cols}`;
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

            // Dispose terminal
            if (this.terminal) {
                this.terminal.dispose();
                this.terminal = null;
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

        // Dispose terminal if exists
        if (this.terminal) {
            this.terminal.dispose();
            this.terminal = null;
        }
    }

    /**
     * Show error message
     */
    showError(message) {
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