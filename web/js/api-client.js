/**
 * pshell API Client
 * Handles all HTTP requests to the pshell REST API
 */

class PShellAPIClient {
    constructor(baseURL = '', apiKey = null) {
        this.baseURL = baseURL;
        this.apiKey = apiKey;
    }

    /**
     * Make HTTP request
     */
    async request(method, path, body = null) {
        const url = `${this.baseURL}${path}`;
        const headers = {
            'Content-Type': 'application/json'
        };

        if (this.apiKey) {
            headers['Authorization'] = `Bearer ${this.apiKey}`;
        }

        const options = {
            method,
            headers
        };

        if (body) {
            options.body = JSON.stringify(body);
        }

        try {
            const response = await fetch(url, options);
            
            if (!response.ok) {
                const error = await response.text();
                throw new Error(`HTTP ${response.status}: ${error}`);
            }

            const contentType = response.headers.get('content-type');
            if (contentType && contentType.includes('application/json')) {
                return await response.json();
            }

            return await response.text();
        } catch (error) {
            console.error(`API request failed: ${method} ${path}`, error);
            throw error;
        }
    }

    /**
     * Create new session
     */
    async createSession(command, rows = 24, cols = 80) {
        return await this.request('POST', '/api/sessions', {
            command,
            rows,
            cols
        });
    }

    /**
     * List all sessions
     */
    async listSessions() {
        return await this.request('GET', '/api/sessions');
    }

    /**
     * Get session info
     */
    async getSession(sessionId) {
        return await this.request('GET', `/api/sessions/${sessionId}`);
    }

    /**
     * Destroy session
     */
    async destroySession(sessionId) {
        return await this.request('DELETE', `/api/sessions/${sessionId}`);
    }

    /**
     * Send input to session
     */
    async sendInput(sessionId, text) {
        // Debug: log what we're about to send
        console.log('API sendInput - text:', JSON.stringify(text), 
                    'length:', text.length,
                    'charCodes:', Array.from(text).map(c => c.charCodeAt(0)));
        return await this.request('POST', `/api/sessions/${sessionId}/send`, {
            text
        });
    }

    /**
     * Read output from session
     */
    async readOutput(sessionId) {
        return await this.request('GET', `/api/sessions/${sessionId}/output`);
    }

    /**
     * Get screen state
     */
    async getScreen(sessionId) {
        return await this.request('GET', `/api/sessions/${sessionId}/screen`);
    }

    /**
     * Resize terminal
     */
    async resizeTerminal(sessionId, rows, cols) {
        return await this.request('POST', `/api/sessions/${sessionId}/resize`, {
            rows,
            cols
        });
    }

    /**
     * Wait for pattern (expect)
     */
    async expectPattern(sessionId, pattern, timeout = 30000, caseSensitive = true) {
        return await this.request('POST', `/api/sessions/${sessionId}/expect`, {
            pattern,
            timeout,
            case_sensitive: caseSensitive
        });
    }

    /**
     * Get daemon status
     */
    async getStatus() {
        return await this.request('GET', '/api/status');
    }

    /**
     * Health check
     */
    async healthCheck() {
        return await this.request('GET', '/api/health');
    }
}

// Export for use in other scripts
window.PShellAPIClient = PShellAPIClient;
