# Security Considerations

## Multi-User System Security

### HTTP REST API Security Context

**CRITICAL SECURITY WARNING**: On multi-user systems, spawned shell sessions run with the **security context of the daemon owner**, not the connecting user.

**Implications:**
- If pshell daemon runs as root, all spawned shells run as root
- If pshell daemon runs as user `alice`, all shells run as `alice`
- HTTP clients cannot be authenticated to different user contexts
- Any user who can access the HTTP API can execute commands as the daemon owner

### Recommended Configuration for Multi-User Systems

**Disable HTTP, Use Unix Socket Only:**

```ini
[http]
enabled = false

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0600  # Owner only
```

**Why Unix Socket is Safer:**
1. **File System Permissions**: Access controlled by OS-level file permissions
2. **Local Only**: Cannot be accessed over network
3. **Per-User Instances**: Each user can run their own pshell daemon
4. **No Authentication Needed**: File permissions provide access control

### Socket Permission Modes

| Mode | Octal | Access | Use Case |
|------|-------|--------|----------|
| Owner only | 0600 | Single user | Most secure, recommended |
| Owner + Group | 0660 | Team access | Shared team daemon |
| All users | 0666 | Public | Development only, NOT recommended |

### When HTTP Can Be Enabled

HTTP REST API should **only** be enabled when:

1. **Single-User Development**: Developer's local machine, no other users
2. **Containerized**: Running in isolated container with single user
3. **API Key Auth**: `[auth] enabled = true` with strong keys (see below)
4. **Network Isolated**: Behind firewall with strict access controls

### API Key Authentication

Enable auth on the REST API with the `[auth]` section:

```ini
[auth]
enabled = true
api_keys = my-secret-key-1, my-secret-key-2   # comma-separated, rotation supported
```

- With `enabled = true` and no keys configured, the daemon **refuses to start**
  (fail closed).
- Clients authenticate with either header:
  `X-API-Key: <key>` or `Authorization: Bearer <key>`.
- The legacy `[http] api_key = <key>` single-key form is still honored.
- With no auth configured, log a warning that all REST endpoints are open —
  keep `bind_address = 127.0.0.1` in that case.
- Key comparison is constant-time; responses use a generic 401 message.
- Static files and `/api/health` are served **without** authentication (the
  web UI needs them). Do not place sensitive files under `static_dir`.

Even with auth enabled, the HTTP API has no per-user authorization: any client
holding a key can access every session. Use the Unix socket with file
permissions for stronger access control.

### Configuration Examples

#### Development (Single User)
```ini
[http]
enabled = true
port = 8080
bind_address = 127.0.0.1  # Localhost only

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0600
```

#### Production (Multi-User)
```ini
[http]
enabled = false  # DISABLED for security

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0600  # Owner only
```

#### Team Shared (Trusted Group)
```ini
[http]
enabled = false

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0660  # Owner + group
# Ensure socket file is owned by appropriate group
```

## Future Security Enhancements

Planned features to improve multi-user security:

1. **Per-User Authentication**: Map HTTP users to system users
2. **PAM Integration**: Use system authentication
3. **Capability-Based Security**: Drop privileges after authentication
4. **Audit Logging**: Track all command execution
5. **Session Isolation**: Separate namespaces per user

## Best Practices

1. **Run as Non-Root**: Never run pshell daemon as root in production
2. **Minimal Permissions**: Use most restrictive socket permissions possible
3. **Regular Audits**: Monitor pshell.log for suspicious activity
4. **Network Isolation**: If HTTP enabled, bind to localhost only
5. **Update Regularly**: Keep pshell updated for security patches

## Reporting Security Issues

If you discover a security vulnerability, please email: security@example.com

Do NOT open public GitHub issues for security vulnerabilities.
