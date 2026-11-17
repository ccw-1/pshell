---
title: "pshell User's Guide"
subtitle: "Portable Programmable Shell: terminal sessions you can script"
author: "ccw-1"
---

# Introduction

## What pshell is

pshell is a small daemon that runs programs inside pseudo terminals (PTYs)
and lets other programs, scripts and people look at them and type into
them. Think of it as a mixture of `tmux` and `expect`:

* like `tmux`, it keeps many terminal **sessions** alive in the background
  and you can attach to them;
* like `expect`, it can wait for a **pattern** in the output and then send
  the next input, so interactive programs can be automated;
* unlike both, it is controlled through plain interfaces: a Unix socket
  (`pshellctl`), an HTTP REST API, a web page and an MCP server for AI
  agents.

pshell emulates a VT100/xterm terminal. It keeps a **screen buffer** (what a
person would see right now), a **scrollback** (what has scrolled off) and an
optional **output log** (everything, as raw bytes), so you can ask "what is
on the screen?" instead of parsing escape sequences yourself.

It is written in portable C99 with no libraries beyond POSIX. It builds on
Linux, macOS, the BSDs, Solaris, AIX and z/OS.

## The programs

| Program | Purpose |
|---|---|
| `pshell` | The daemon. Owns all sessions. |
| `pshellctl` | Command line client over the Unix socket. The tool you will use most. |
| `pshell-client` | Command line client over the HTTP REST API (for remote daemons). |
| `pshell-mcp` | MCP server (stdio) so AI agents can drive sessions. Starts the daemon if needed. |

## How the pieces fit

```
  pshellctl        pshell-client / curl / browser        MCP client
      |                      |                               |
  Unix socket             HTTP REST API                  pshell-mcp
      |                      |                               |
      +----------+-----------+-------------------------------+
                 |                                  (Unix socket)
            pshell daemon
                 |
      +----------+----------+
   session 1  session 2  session N   each = PTY + child process
                                     + screen + scrollback + log
```

## How to read this guide

Chapter 2 builds and installs pshell and Chapter 3 gets a first session
running in five minutes. Chapters 4 to 11 explain the concepts and the
everyday commands. Chapters 12 to 15 cover logging, the REST API, the web
interface and the MCP server. Chapters 16 and 17 are about security and
troubleshooting. Appendix A is a command summary.

# Installation

## Requirements

* A POSIX.1-2008 system with a C99 compiler (gcc, clang or xlc) and `make`
* POSIX threads, POSIX regular expressions, POSIX sockets and PTYs
  (`posix_openpt`)
* Nothing else: no HTTP, JSON or SSL libraries are used

## Build

```sh
git clone https://github.com/ccw-1/pshell.git
cd pshell
make
```

This produces `bin/pshell`, `bin/pshellctl`, `bin/pshell-client` and
`bin/pshell-mcp`. `make mcp` builds only the MCP server, `make test` runs
the unit tests and `make clean` removes the build output.

## Install

```sh
sudo make install
```

| What | Where |
|---|---|
| The four programs | `/usr/local/bin` |
| Web interface files | `/usr/local/share/pshell/web` |
| Example configuration | `/etc/pshell/pshell.conf` |

You can also run everything from the source tree without installing: use
`bin/pshell -c config/pshell.conf` and `bin/pshellctl`.

## Check the version

Every program reports its version and, when built from a git checkout, the
git state it was built from:

```sh
pshell -v
pshellctl -V
pshell-client -V
pshell-mcp -v
```
```
pshell version 1.3.0 (v1.3.0)
```

A build between releases shows a description such as
`v1.3.0-4-g1a2b3c4` (4 commits after the tag), with `-dirty` added when
the working tree has uncommitted changes. Outside a git checkout (a
tarball) it shows `unknown`. The version is also in the file `VERSION`,
and `make version` prints it. The MCP server reports the same version to
its clients.

# Quick start

The following steps start a private daemon with the sample configuration,
run a command in a session, read its screen and clean up.

1. Start the daemon in the background:

   ```sh
   pshell -c config/pshell.conf
   ```

   The sample configuration uses the socket `/tmp/pshell.sock`, which is
   also the default of `pshellctl`.

2. Check that it answers:

   ```sh
   pshellctl status
   ```
   ```
   Status: running
   Sessions: 0
   ```

3. Create a session. The command prints the new session ID:

   ```sh
   S=$(pshellctl create --command bash)
   ```

4. Type into it. Single quotes keep your shell from touching the `\n`,
   which pshell turns into the Enter key:

   ```sh
   pshellctl send $S 'echo hello pshell\n'
   ```

5. Wait for the output, then look at the screen:

   ```sh
   pshellctl expect $S "hello pshell" 2000
   pshellctl screen $S
   ```

6. Watch and type into it interactively (press `Ctrl-]` to leave):

   ```sh
   pshellctl attach $S
   ```

7. Remove the session and stop the daemon:

   ```sh
   pshellctl destroy $S
   kill $(cat /tmp/pshell.pid)
   ```

# Concepts

## Sessions

A session is one child process connected to a PTY, plus the state pshell
keeps for it. Every session has a UUID such as
`7838fced-6fe9-4f0e-9807-141dc0b9598b`. Sessions live inside the daemon, so
they keep running when the client that created them disconnects. They end
when the child process exits or when you destroy them.

The child runs with the privileges of the daemon owner. See Chapter 16.

## Four ways to look at output

The same terminal output is available in four forms. Choosing the right one
is the most important skill when scripting pshell.

| View | Command | What you get |
|---|---|---|
| **Screen** | `pshellctl screen` | The visible grid, as text, plus the cursor position. What a person would see now. |
| **Scrollback** | `pshellctl scrollback` | Lines that have scrolled off the top of the screen (default 1000 lines). |
| **Raw output** | `pshellctl output` | The raw byte stream, with all escape sequences, new data since the last read. |
| **Output log** | `pshellctl lines` | Everything ever printed, from a log file you asked for with `--output`. |

For full-screen programs (editors, menus) use the screen. For command line
tools that print a lot, use the output log with `lines --tail`.

## Expect

`expect` waits until a regular expression appears in the output, or a
timeout expires. It is how a script knows that a program is ready for its
next input. See Chapter 10.

## Line endings and the Enter key

Terminals send a carriage return when you press Enter. When you send text
to a session, `\n` is delivered as a line feed, which shells accept. A few
full-screen programs only react to `\r`. If a `\n` does not submit a line,
send `\r` instead.

# Configuration

## The configuration file

The daemon reads an INI file given with `-c`. The default location is
`/etc/pshell/pshell.conf`. Lines starting with `#` are comments. There is no
reload: restart the daemon to apply changes.

```ini
[daemon]
pid_file = /tmp/pshell.pid
log_file = /tmp/pshell.log
log_level = info
use_syslog = false

[http]
enabled = false
bind_address = 127.0.0.1
port = 8080
static_dir = web

[socket]
enabled = true
socket_path = /tmp/pshell.sock
socket_permissions = 0600

[auth]
enabled = false
api_keys =

[sessions]
max_sessions = 100
max_attach = 16
default_shell = /bin/bash
```

## Option reference

| Section | Option | Default | Meaning |
|---|---|---|---|
| `daemon` | `pid_file` | `/var/run/pshell.pid` | PID file. The daemon exits if it cannot write it. |
| `daemon` | `log_file` | `/var/log/pshell.log` | Daemon log. |
| `daemon` | `log_level` | `info` | Log verbosity. |
| `daemon` | `use_syslog` | `true` | Also log to syslog. |
| `http` | `enabled` | `false` | Turn the REST API and web interface on. |
| `http` | `bind_address` | `127.0.0.1` | Address to listen on. |
| `http` | `port` | `8080` | TCP port. |
| `http` | `static_dir` | `web` | Directory with the web interface files. |
| `socket` | `enabled` | `true` | Turn the Unix socket on. |
| `socket` | `socket_path` | `/tmp/pshell.sock` | Socket file. |
| `socket` | `socket_permissions` | `0600` | File mode of the socket. |
| `auth` | `enabled` | `false` | Require an API key for the REST API. |
| `auth` | `api_keys` | none | Comma separated list of valid keys. |
| `sessions` | `max_sessions` | `50` | Maximum number of sessions. |
| `sessions` | `max_attach` | `16` | Terminals per session for `attach` (1 to 256). |
| `sessions` | `default_shell` | `/bin/sh` | Program run when a REST request has no `command`. |

Note that the built-in defaults for the PID file and the log file point to
`/var/run` and `/var/log`. Unless you run the daemon as root, set both to
a writable place such as `/tmp`, as the sample configuration does.

## Environment variables

| Variable | Used by | Meaning |
|---|---|---|
| `PSHELL_SOCKET_PATH` | `pshellctl`, `pshell-mcp` | Socket to connect to. The `-s` option wins over the variable, the variable wins over the default `/tmp/pshell.sock`. |
| `PSHELL_CONFIG` | `pshell-mcp` | Configuration file used when it starts the daemon. |
| `PSHELL_DAEMON_BIN` | `pshell-mcp` | Daemon binary to start. |
| `PSHELL_NO_AUTOSTART` | `pshell-mcp` | Set to `1` to never start the daemon. |

Several daemons can run side by side if each has its own socket, PID file
and `PSHELL_SOCKET_PATH`.

# Running the daemon

## Start and stop

```sh
pshell -c /etc/pshell/pshell.conf        # background (daemonizes)
pshell -c /etc/pshell/pshell.conf -f     # foreground, for debugging
kill $(cat /tmp/pshell.pid)              # graceful stop
```

In foreground mode `Ctrl-C` stops the daemon. `pshell -h` lists the
options and `pshell -v` prints the version.

## Running as a service

A minimal systemd unit:

```ini
[Unit]
Description=pshell daemon
After=network.target

[Service]
Type=forking
ExecStart=/usr/local/bin/pshell -c /etc/pshell/pshell.conf
PIDFile=/tmp/pshell.pid
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Run the service as an ordinary user rather than root unless you really
need sessions with root rights.

## Checking on it

```sh
pshellctl status        # running state and number of sessions
tail -f /tmp/pshell.log
```

With the REST API on, `GET /api/health` answers `{"status":"healthy"}`
without authentication, which suits load balancers and monitors.

# pshellctl: the command line client

`pshellctl` talks to the daemon over the Unix socket. Access control is the
file permission of the socket, so no key is needed.

```
pshellctl [-s SOCKET] COMMAND [ARGS...]
```

| Command | Purpose |
|---|---|
| `create [--command C] [--rows N] [--cols N] [--scrollback N] [--output F] [--rotate N:M]` | Create a session and print its ID. Default command is `bash`, default size 24x80. |
| `list` | List sessions: ID, command, size, PID, state. |
| `info ID` | Show one session. |
| `send ID TEXT` | Type text (escape sequences allowed, see Chapter 8). |
| `screen ID` | Print the visible screen and the cursor position. |
| `output ID` | Print raw new output since the last read. |
| `scrollback ID [--start N] [--count N]` | Print scrollback lines. |
| `lines ID --start N --end M` | Print a range of the output log. Also `--head N`, `--tail N`. |
| `expect ID PATTERN [MS]` | Wait for a pattern in old and new output. |
| `expect-incoming ID PATTERN MS` | Wait for a pattern in new output only. |
| `resize ID ROWS COLS` | Resize the terminal. |
| `attach ID` | Interactive terminal on the session. |
| `destroy ID` | Kill the session. |
| `status` | Daemon state. |
| `generate-key [--length BYTES]` | Make a random API key (16 to 64 bytes, default 32). |

Options: `-s PATH` selects the socket, `-h` prints help.

## Creating sessions

```sh
pshellctl create --command bash
pshellctl create --command "ssh user@host" --rows 40 --cols 120
pshellctl create --command bash --scrollback 5000
pshellctl create --command bash --output /tmp/s.log --rotate 4:1000
```

pshell splits the command on blanks (single or double quotes group words)
and runs it directly; no shell is involved. To use pipes, redirections or
variables, run a shell explicitly: `--command "sh -c 'ls | wc -l'"`.

# Sending input

`pshellctl send` interprets backslash sequences in its text argument. Put
the argument in **single quotes** so your own shell leaves the backslashes
alone.

```sh
pshellctl send $S 'ls -la\n'        # a command and Enter
pshellctl send $S '\cC'             # Ctrl-C
pshellctl send $S '\e:wq\n'         # Escape, then ":wq" and Enter
```

## Sequence reference

| Group | Sequences |
|---|---|
| Standard | `\n` line feed, `\r` carriage return, `\t` tab, `\e` Escape, `\a` bell, `\b` backspace, `\f` form feed, `\v` vertical tab, `\\` a backslash |
| Control keys | `\cA` to `\cZ` (`\cC` = Ctrl-C, `\cD` = Ctrl-D, `\cL` = Ctrl-L, `\cU` = kill line, `\cZ` = suspend) |
| Function keys | `\F1` to `\F12` |
| Cursor keys | `\UP` `\DOWN` `\LEFT` `\RIGHT` |
| Editing keys | `\HOME` `\END` `\INS` `\DEL` `\PGUP` `\PGDN` |
| Shift keys | `\SHIFT_UP` `\SHIFT_DOWN` `\SHIFT_LEFT` `\SHIFT_RIGHT` `\SHIFT_TAB` `\SHIFT_F1` to `\SHIFT_F12` |
| Numeric | `\xHH` hexadecimal byte (`\x03` = Ctrl-C), `\NNN` octal byte (`\033` = Escape) |

`\0` ends the string, because embedded NUL bytes cannot be sent. The full
table with the terminal codes is in `docs/ESCAPE_SEQUENCES.md`.

## Where escape processing happens

The backslash sequences are a feature of `pshellctl` and of the MCP tool
`pshell_send` (with `process_escapes: true`). The REST API sends the text
exactly as decoded from JSON. Over REST use JSON escapes instead: `"\n"` for
Enter and `"\u0003"` for Ctrl-C.

## Full-screen programs

Programs with their own input line (editors, chat interfaces, menus) treat
a burst of bytes as a *paste*. If you send text and Enter in one call, the
Enter may become a newline inside the text instead of submitting it. Send
the text and the Enter key in two separate calls with a short pause between
them:

```sh
pshellctl send $S 'my message'
pshellctl send $S '\r'
```

# Reading output

## The screen

```sh
pshellctl screen $S
```

prints the visible rows followed by the cursor position:

```
7
8
...
15
$

[Cursor at row 9, col 2]
```

Use the screen to answer "what is displayed right now". It is the only view
that is correct for full-screen programs, because it reflects cursor
movement, clearing and overwriting. The screen is UTF-8 aware: accented
letters, box drawing characters, CJK and emoji appear as characters, and
double-width characters take two columns.

## Scrollback

Lines that scroll off the top of the screen move into the scrollback, whose
size is set per session with `--scrollback` (default 1000 lines).

```sh
pshellctl scrollback $S --start 0 --count 50
```
```
Scrollback: 9/1000 lines used
Showing lines 0-49:
...
```

The scrollback stays empty until something scrolls off the screen. While
a full-screen program uses the alternate screen (editors, `less`) no
scrollback is recorded.

## Raw output

`pshellctl output ID` returns the bytes the program wrote since the last
read, including all escape sequences. Each read consumes the data. Use it
when you need the exact byte stream. For ordinary use prefer the screen or
the output log.

## The output log and line ranges

When a session is created with `--output FILE`, everything it prints is
appended to `FILE`, and pshell counts the lines. You can then fetch ranges
by global line number (1-based), independent of the screen size:

```sh
pshellctl lines $S --start 100 --end 200
pshellctl lines $S --head 50
pshellctl lines $S --tail 100 | grep ERROR
```

The data contains raw escape sequences. The REST `/lines` endpoint needs
both `start` and `end`; `--head` and `--tail` exist only in `pshellctl` and
in the MCP tools. The line range commands work across rotated log files.

To process only new output, remember the line count and read from there:

```sh
LAST=$(pshell-client info $S | jq .total_lines)
# ... later ...
NOW=$(pshell-client info $S | jq .total_lines)
pshellctl lines $S --start $((LAST + 1)) --end $NOW
```

## Choosing a view

| You want to | Use |
|---|---|
| Check what a full-screen program shows | `screen` |
| Find a message in the output of a long command | `lines --tail`, or `expect` |
| Read what scrolled away | `scrollback` |
| Record or replay a whole session | the output log (`--output`) |
| Process the exact byte stream | `output` or the log |

# Automating with expect

## Waiting for a pattern

```sh
pshellctl expect $S "Password:" 5000
```

The pattern is a POSIX extended regular expression. The last argument is
the timeout in milliseconds (default 5000). The command prints the match
and exits with status **0**; if the timeout expires it prints
`No match (timeout)` and exits with status **1**.

```
Matched: "hello" at -4
```

The number says where the match is, in lines relative to the cursor.

`expect` searches the existing screen and scrollback first and then the new
output. That is convenient but can match old text. To wait for something
that must appear *after* now, use `expect-incoming`, which ignores
everything that was there before the call:

```sh
pshellctl send $S 'sleep 1; echo READY\n'
pshellctl expect-incoming $S "READY" 3000
```
```
Matched: "READY" at -9 (incoming-only)
```

## Writing patterns

* `$` is a regular expression anchor, so a literal dollar sign needs a
  backslash. Inside double quotes in your shell, write `"\\$ "` to wait
  for a prompt ending in `$ ` (pshell receives the expression `\$ `).
* Quotes and control characters in the pattern are escaped for you;
  backslash pairs are kept as they are.
* Matching is case sensitive. The REST API and MCP tool accept
  `case_sensitive: false`.
* Keep patterns short and specific, such as `login:` or `[Pp]assword`.

## Scripting with exit codes

```sh
#!/bin/sh
S=$(pshellctl create --command bash)
pshellctl send $S './installer\n'

if pshellctl expect $S "Continue\\? \\[y/n\\]" 10000; then
    pshellctl send $S 'y\n'
else
    echo "installer did not ask" >&2
    pshellctl destroy $S
    exit 1
fi

pshellctl expect $S "Installation complete" 600000 || exit 1
pshellctl destroy $S
```

## Several expects at once

The daemon runs each wait on its own worker, so output that arrives while
you wait is matched, and up to 16 expects can wait on one session at the
same time. A session cannot be destroyed while an expect is active; retry
after it returns. If a client disconnects (for example with `Ctrl-C`) the
wait is cancelled and the daemon carries on.

# Attaching to a session

```sh
pshellctl attach $S
```

opens an interactive terminal on the session, like `tmux attach`. You see
the current screen immediately (with colours and attributes) and your
keystrokes go to the session. Your terminal size is applied to the session.

* Press **`Ctrl-]`** to detach. The session keeps running.
* Up to `max_attach` terminals (default 16) can be attached to one session.
* Attach uses only the Unix socket, not HTTP.
* Full-screen programs work, including the alternate screen, mouse and
  paste modes. pshell switches those modes off again when you detach.

Attaching is a good way to look at what an automated script is doing, or
to take over by hand when something unexpected appears.

# Output logging and rotation

Add `--output` when creating a session to record everything it prints:

```sh
pshellctl create --command bash --output /var/log/pshell/s1.log
```

* The log is appended to, never truncated. It is created with mode 0644.
* It holds the raw terminal bytes, including escape sequences.
* Writes are buffered (8 KB) and flushed when you read lines, and when the
  session ends.
* Without rotation the file grows without limit.

## Rotation

`--rotate N:M` keeps `N` files of `M` lines each:

```sh
pshellctl create --command bash --output /tmp/s.log --rotate 4:1000
```

When the active file reaches 1000 lines it becomes `s.log.0`, older files
shift up (`.1`, `.2`, ...) and the oldest is deleted. Rotation is by line
count, not size. Line range queries (`lines`) read across the rotated
files, using global line numbers that never reset during the session.

Over REST the same settings are `output_file`, `rotate_count` and
`rotate_lines`.

# The REST API

## Enabling it

The REST API is off by default. Turn it on and protect it:

```sh
pshellctl generate-key
```
```
Generated API Key (32 bytes, 44 characters):
EXAMPLEKEY-replace-with-your-own-0123456789=
```

```ini
[http]
enabled = true
bind_address = 127.0.0.1
port = 8080

[auth]
enabled = true
api_keys = EXAMPLEKEY-replace-with-your-own-0123456789=
```

Restart the daemon. Clients send the key as `X-API-Key: KEY` or as
`Authorization: Bearer KEY`. A missing or wrong key gives status 401. With
`enabled = true` and no keys the daemon refuses to start. `/api/health` and
the static web files are always open.

## Endpoints

| Method and path | Purpose |
|---|---|
| `POST /api/sessions` | Create a session. |
| `GET /api/sessions` | List sessions. |
| `GET /api/sessions/ID` | Session info, including `total_lines`. |
| `DELETE /api/sessions/ID` | Destroy a session. |
| `POST /api/sessions/ID/send` | Send input. Body `{"text": "..."}`. |
| `POST /api/sessions/ID/expect` | Wait for a pattern. |
| `POST /api/sessions/ID/resize` | Resize. Body `{"rows": N, "cols": M}`. |
| `GET /api/sessions/ID/output` | Raw new output. |
| `GET /api/sessions/ID/screen` | Visible screen and cursor. |
| `GET /api/sessions/ID/raw` | Raw buffer. |
| `GET /api/sessions/ID/scrollback?start_line=N&count=M` | Scrollback. |
| `GET /api/sessions/ID/lines?start=N&end=M` | Output log range. |
| `GET /api/status` | Daemon status. |
| `GET /api/health` | Health check (no key needed). |

Create accepts `command`, `rows`, `cols`, `scrollback_lines`,
`output_file`, `rotate_count` and `rotate_lines`. Without `command` the
`default_shell` setting is used.

## Examples

```sh
KEY=EXAMPLEKEY-replace-with-your-own-0123456789=
U=http://127.0.0.1:8080/api

curl -s -X POST $U/sessions -H "X-API-Key: $KEY" \
  -H "Content-Type: application/json" \
  -d '{"command":"bash","rows":24,"cols":80}'
```
```
{"id":"847b9a32-...","command":"bash","rows":24,"cols":80,
 "status":"active"}
```

```sh
curl -s -X POST $U/sessions/$S/send -H "X-API-Key: $KEY" \
  -H "Content-Type: application/json" -d '{"text":"echo hi\n"}'
# {"status":"sent","bytes":8}

curl -s -X POST $U/sessions/$S/expect -H "X-API-Key: $KEY" \
  -H "Content-Type: application/json" \
  -d '{"pattern":"hi","timeout":2000}'
# {"status":"matched","matched_text":"hi","start":25,"end":27}

curl -s -H "X-API-Key: $KEY" $U/sessions/$S/screen
# {"text":"...","cursor_row":9,"cursor_col":2,"bytes":51}
```

Errors come back as JSON such as `{"error":"Session not found","status":404}`.

## Python

```python
import requests

KEY = {"X-API-Key": "<your key>"}
U = "http://127.0.0.1:8080/api"

s = requests.post(f"{U}/sessions", headers=KEY,
                  json={"command": "ssh user@host"}).json()["id"]
requests.post(f"{U}/sessions/{s}/expect", headers=KEY,
              json={"pattern": "password:", "timeout": 5000})
requests.post(f"{U}/sessions/{s}/send", headers=KEY,
              json={"text": "secret\n"})
```

## pshell-client

`pshell-client` is a command line front end for the REST API, useful for
remote daemons:

```sh
pshell-client -h host -p 8080 -k KEY create --command bash
pshell-client -k KEY list
pshell-client -k KEY info ID
pshell-client -k KEY send ID 'ls\n'
pshell-client -k KEY expect ID --pattern 'done' --timeout 30000
pshell-client -k KEY health
```

Options are `-h HOST` (default 127.0.0.1), `-p PORT` (default 8080) and
`-k KEY`. The commands are `create`, `list`, `info`, `send`, `output`,
`screen`, `resize`, `expect`, `destroy`, `status` and `health`. Unlike
`pshellctl`, `pshell-client info` prints the raw JSON, which includes
`total_lines`.

# The web interface

With `[http] enabled = true`, open `http://localhost:8080/` in a browser.
The page lists sessions, shows the selected session in a terminal view and
offers **New Session** (command, rows, columns), **Resize**, **Clear** and
**Close**. The files come from `static_dir`; use `web` when running from the
source tree and `/usr/local/share/pshell/web` after `make install`.

The page has no field for an API key, so it can only control sessions while
`[auth]` is disabled. Use it on a local, single-user machine with
`bind_address = 127.0.0.1`; with authentication on, use `curl`,
`pshell-client` or your own client instead.

# The MCP server

`pshell-mcp` lets AI agents (opencode, Claude Desktop and any other
[MCP](https://modelcontextprotocol.io) client) create and drive terminal
sessions. It speaks MCP over stdio and forwards each call to the daemon over
the Unix socket. If the daemon is not running, `pshell-mcp` starts it.

## Client configuration

opencode (`opencode.json`):

```json
{
  "mcp": {
    "pshell": {
      "type": "local",
      "command": ["/usr/local/bin/pshell-mcp"],
      "enabled": true
    }
  }
}
```

Claude Desktop (`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "pshell": { "command": "/usr/local/bin/pshell-mcp", "args": [] }
  }
}
```

## Options

| Option | Variable | Meaning |
|---|---|---|
| `-s PATH` | `PSHELL_SOCKET_PATH` | Daemon socket |
| `-c PATH` | `PSHELL_CONFIG` | Configuration used when it starts the daemon |
| `-b PATH` | `PSHELL_DAEMON_BIN` | Daemon binary to start |
| `-n` | `PSHELL_NO_AUTOSTART=1` | Never start the daemon |
| `-v` | | Print the version |

## Tools

| Tool | Purpose |
|---|---|
| `pshell_create_session` | Start a session (command, rows, cols, scrollback, output file and rotation). |
| `pshell_list_sessions` | List sessions. |
| `pshell_send` | Send input. `process_escapes: true` enables `\n`, `\cC`, `\UP` and so on. |
| `pshell_read_output` | New raw output since the last read. |
| `pshell_screen` | Current screen and cursor. |
| `pshell_scrollback` | Scrollback lines. |
| `pshell_read_lines` | Output log range (session needs `output_file`). |
| `pshell_expect` | Wait for a regular expression in old and new output. |
| `pshell_expect_incoming` | Wait for new output only. |
| `pshell_resize` | Resize the terminal. |
| `pshell_destroy_session` | Kill a session. |
| `pshell_status` | Daemon status. |

Tips for agents: read the screen with `pshell_screen`, not
`pshell_read_output`; send text and Enter as two separate `pshell_send`
calls for full-screen programs (Chapter 8); `\cU` clears an input line and
`\e` closes dialogs.

The daemon started by `pshell-mcp` keeps running after the MCP client
exits. Stop it with `pkill -x pshell`.

# Security

## The key point

A session runs as **the user who owns the daemon**. Anyone who can reach a
pshell interface can run commands as that user. Treat access to pshell like
a login.

## Recommendations

* **Prefer the Unix socket.** Its access control is the file permission:
  `0600` (owner only, the default), `0660` (a group), never `0666`.
* **Leave HTTP off** unless you need it. If you turn it on, keep
  `bind_address = 127.0.0.1`, enable `[auth]` with strong keys, and put a
  TLS-terminating proxy in front for anything that is not local. pshell
  itself does not speak TLS.
* **Do not run the daemon as root** unless you must. If it runs as root,
  every session is a root shell.
* With one daemon per user (own socket, own PID file), there is no shared
  trust at all.
* API keys are compared in constant time and a bad key gives a generic 401.
  There is no per-user authorization: every key sees every session.
* The log file may record commands. Protect it, and note that the output
  log (`--output`) contains everything the session printed, including
  typed passwords if the program echoes them.

See `docs/SECURITY.md` for more.

# Troubleshooting

| Symptom | Cause and remedy |
|---|---|
| `Failed to connect to pshell daemon` | The daemon is not running, or you are using another socket. Check `-s`, `PSHELL_SOCKET_PATH` and the `socket_path` in the configuration. |
| Daemon exits right after start | It could not write the PID or log file. Set `pid_file` and `log_file` to writable paths. Run with `-f` to see the messages. |
| Daemon refuses to start with `[auth] enabled` | No `api_keys` configured. Generate one with `pshellctl generate-key`. |
| REST calls give 401 | Missing or wrong key. Send `X-API-Key` or `Authorization: Bearer`. |
| `Session not found` | The session ended (its program exited) or was destroyed. Run `pshellctl list`. |
| `expect` times out although the text is on screen | Check the pattern: `$` is an anchor and needs a backslash. Check the case. Try `pshellctl screen`. |
| `expect` matches old text | Use `expect-incoming`. |
| `\n` is typed literally | You used double quotes and your shell ate the backslash, or you are using the REST API. Use single quotes or JSON escapes. |
| Text typed into a full-screen program is not submitted | Send the text and Enter separately (Chapter 8). |
| `lines` reports no data | The session was created without `--output`. |
| `scrollback` is empty | Nothing has scrolled off the screen yet, or the program is on the alternate screen. |
| `destroy` is refused | An expect is waiting on the session. Wait for it to return. |
| Web page does not load | `[http] enabled = false`, wrong `static_dir`, or the browser uses another port. |

For debugging, start the daemon in the foreground with `log_level = debug`
and read `log_file`.

# Appendix A: Command summary {-}

```sh
# daemon
pshell -c CONF [-f]        pshell -v        pshell -h

# sessions
pshellctl create [--command C] [--rows N] [--cols N] [--scrollback N]
                 [--output F] [--rotate N:M]
pshellctl list | info ID | status | destroy ID | resize ID ROWS COLS

# input
pshellctl send ID 'text\n'      pshellctl attach ID

# output
pshellctl screen ID             pshellctl output ID
pshellctl scrollback ID [--start N] [--count N]
pshellctl lines ID (--start N --end M | --head N | --tail N)

# automation
pshellctl expect ID PATTERN [MS]
pshellctl expect-incoming ID PATTERN MS

# keys
pshellctl generate-key [--length BYTES]
```

# Appendix B: Further reading {-}

All of these are in the `docs` directory of the source tree.

| Document | Content |
|---|---|
| `MCP_SERVER.md` | MCP server details and client setup |
| `ESCAPE_SEQUENCES.md` | Complete escape sequence tables |
| `OUTPUT_LOGGING.md` | Logging and rotation reference |
| `API_SCROLLBACK.md` | Scrollback API |
| `SECURITY.md` | Multi-user security |
| `MAINTENANCE.md` | Operations, monitoring and upgrades |

pshell is released under the MIT license (file `LICENSE`). Bug reports and
contributions: <https://github.com/ccw-1/pshell>.
