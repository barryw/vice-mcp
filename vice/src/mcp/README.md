# VICE MCP Server

This directory contains the embedded Model Context Protocol (MCP) server for VICE, enabling AI assistants to control and inspect Commodore 8-bit emulations.

## Architecture

The MCP server is organized into three layers:

### 1. MCP Server Core (`mcp_server.c/h`)
- Initialization and lifecycle management
- VICE resources (`MCPServerEnabled`, `MCPServerPort`, `MCPServerHost`,
  `MCPServerToken`, `MCPServerCORSOrigin`)
- Command-line options (`-mcpserver`, `+mcpserver`, `-mcpserverport`,
  `-mcpserverhost`, `-mcpservertoken`, `-mcpservercorsorigin`)
- Changing a resource while the server runs restarts it with the new setting

### 2. Transport Layer (`mcp_transport.c/h`)
- HTTP via libmicrohttpd: JSON-RPC 2.0 over `POST /mcp`, plain JSON replies
- `GET /mcp` (and any non-POST method on `/mcp`) returns `405 Method Not Allowed`,
  the MCP answer for "no SSE stream"; the reserved `GET /events` returns `501`
- Bearer-token auth and single-origin CORS
- Dispatch onto the emulator: through a CPU trap while the machine runs, under
  VICE's mainlock while the UI is paused or the machine is held, directly while
  the monitor is open. Calls are serialized by a dispatch mutex; `vice.ping`
  and `vice.execution.pause` bypass it.
- Default port: 6510 (chosen for 6510 CPU reference)

### 3. Tools (`mcp_tools*.c/h`)
- `mcp_tools.c` - tool registry (`tool_registry[]`), name matching, dispatch
- `mcp_tools_protocol.c` - `initialize`, `tools/list` (JSON Schemas),
  `tools/call`, `vice.ping`
- One file per area: `_execution`, `_memory`, `_checkpoint`, `_chipstate`
  (sprites, VIC-II, SID, CIA), `_disk`, `_display`, `_input`, `_machine`,
  `_debug`, `_snapshot`
- `mcp_tools_helpers.c`, `mcp_tools_validation.c` - shared parsing and checks
- `mcp_tools_trace.c` - execution-trace and interrupt-log handlers. These are
  compiled but **not registered**, so clients cannot call them.

## Building

Requires libmicrohttpd 0.9.71 or newer (the transport uses `enum MHD_Result`).
`configure` checks for it.

```bash
# Configure with MCP server support
./configure --enable-mcp-server

# Build VICE
make

# Run with MCP server enabled on 127.0.0.1:6510
x64sc -mcpserver
```

## Configuration

The MCP server can be configured via resources or command-line options:

**Resources:**
- `MCPServerEnabled` (0/1) - Enable/disable MCP server
- `MCPServerPort` (1024-65535) - Port to listen on (default: 6510)
- `MCPServerHost` (string) - IPv4 address to bind to (default: 127.0.0.1). Host names and IPv6 are rejected.
- `MCPServerToken` (string) - Optional bearer token. Required for CORS; strongly recommended for non-loopback binds.
- `MCPServerCORSOrigin` (string) - Optional exact CORS origin. `*` is rejected.

**Command-line:**
```bash
x64sc -mcpserver                  # Enable MCP server
x64sc +mcpserver                  # Disable MCP server
x64sc -mcpserverport 6510         # Set port
x64sc -mcpservertoken secret      # Require Authorization: Bearer secret
x64sc -mcpserverhost 0.0.0.0
x64sc -mcpserverhost 0.0.0.0 -mcpservertoken secret
x64sc -mcpservercorsorigin http://localhost:3000 -mcpservertoken secret
```

`MCPServerHost` is the address VICE binds to:

- `127.0.0.1` means same-machine clients only. This is the default.
- `0.0.0.0` means listen on every network interface.
- Clients do not connect to `0.0.0.0`. Same-machine clients use
  `http://127.0.0.1:6510/mcp`; remote clients use the host's real LAN IP, for
  example `http://192.168.1.42:6510/mcp`.

Token behavior:

- No token configured: requests do not need an `Authorization` header.
- Token configured: every request must include `Authorization: Bearer <token>`.
- CORS configured: a token is required.
- `0.0.0.0` without a token is allowed for backwards compatibility, but the
  transport logs a warning because remote clients can control the emulator.

Common setups:

| Use case | Start VICE with | Client URL | Auth header |
|---|---|---|---|
| Same machine | `x64sc -mcpserver` | `http://127.0.0.1:6510/mcp` | None |
| Same machine, custom port | `x64sc -mcpserver -mcpserverport 7000` | `http://127.0.0.1:7000/mcp` | None |
| LAN access | `x64sc -mcpserver -mcpserverhost 0.0.0.0` | `http://<host-ip>:6510/mcp` | None |
| LAN access with token | `x64sc -mcpserver -mcpserverhost 0.0.0.0 -mcpservertoken secret` | `http://<host-ip>:6510/mcp` | `Authorization: Bearer secret` |
| Browser client | `x64sc -mcpserver -mcpservercorsorigin http://localhost:3000 -mcpservertoken secret` | `http://127.0.0.1:6510/mcp` | `Authorization: Bearer secret` |

## HTTP Transport Layer

The MCP server exposes two HTTP endpoints:

### POST /mcp - JSON-RPC 2.0 Endpoint

Accepts JSON-RPC 2.0 requests and returns responses.

Every request must use `Content-Type: application/json`. Clients should also send
`Accept: application/json`.

VICE accepts direct JSON-RPC method calls:

```bash
curl -sS http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  --data '{
    "jsonrpc": "2.0",
    "method": "vice.ping",
    "id": 1
  }'
```

Standard MCP clients usually call tools through `tools/call`. The public tool
names returned by `tools/list` use underscores so clients that dislike dotted
names can call them safely:

```bash
curl -sS http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  --data '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "vice_ping",
      "arguments": {}
    },
    "id": 2
  }'
```

If `MCPServerToken` is configured, add the bearer header to every request:

```bash
curl -sS http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  -H "Authorization: Bearer secret" \
  --data '{"jsonrpc":"2.0","method":"vice.ping","id":3}'
```

**Example Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "ok",
    "version": "3.10",
    "machine": "C64SC",
    "execution": "running"
  },
  "id": 1
}
```

**Error Response:**
```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32601,
    "message": "Method not found"
  },
  "id": 1
}
```

`version` is the VICE version, `machine` the emulated machine (`C64SC` under
`x64sc`, `C64` under `x64`), and `execution` is `running` or `paused`.

Other responses:

- Notifications (requests without `id`, e.g. `notifications/initialized`) get
  `202 Accepted` with no body.
- Wrong `Content-Type`: `415`. An `Accept` header that allows neither
  `application/json` nor `*/*`: `406`. Missing or wrong bearer token when one
  is configured: `401`. Any other path: `404`.

### GET /mcp, GET /events - Server-Sent Events (SSE)

SSE is not implemented in this transport. `GET /mcp` (and any other non-POST
method on `/mcp`) returns `405 Method Not Allowed` with `Allow: POST, OPTIONS`,
which MCP clients read as "this server offers no SSE stream". The reserved
`GET /events` returns `501 Not Implemented`.

### Transport Configuration

- **Maximum request size:** 10MB (larger requests are dropped)
- **Connection limit:** 100 concurrent connections
- **Connection timeout:** 30 seconds
- **Tool dispatch timeout:** 5 seconds waiting for the emulator to run a
  queued call (error -32000)
- **CORS:** Disabled by default. Enable only with `MCPServerCORSOrigin` and
  `MCPServerToken`. A preflight `OPTIONS` from any other origin gets `403`.

## Security

- **Default binding:** localhost-only (127.0.0.1)
- **Remote access:** Requires explicit configuration with `-mcpserverhost`. Use `MCPServerToken` unless the network is already trusted.
- **Authentication:** Optional on loopback, recommended for non-loopback binds, required for CORS
- **CORS:** Disabled by default; only one exact origin can be allowed
- **Port range:** Restricted to 1024-65535 (no privileged ports)
- **DoS protection:** Request size limits, connection limits, timeouts
- **Input validation:** Content-Type validation, JSON parsing errors
- **Memory safety:** Integer overflow protection, null checks
- **Thread safety:** Mutex-protected shared state

**Security Warnings:**
- No HTTPS support - terminate TLS at reverse proxy level for remote access
- Command-line tokens may be visible to local process-list tools. Prefer resource files or a wrapper when that matters.
- Binding to `0.0.0.0` exposes emulator control to the network. This is useful
  for remote agents and CI machines, but it is not a browser sandbox or a public
  API boundary.

**Recommended Production Setup:**
```nginx
# nginx reverse proxy with authentication
location /mcp {
    proxy_pass http://127.0.0.1:6510;
    auth_basic "VICE MCP Access";
    auth_basic_user_file /etc/nginx/.htpasswd;
}
```

## Available MCP Tools

62 `vice.*` tools are registered in `tool_registry[]` (`mcp_tools.c`): execution
control, registers, memory, checkpoints and groups, sprites, VIC-II/SID/CIA
state, disk, machine and config, display, keyboard/joystick/paddle input,
debugging (disassembly, symbols, watchpoints, backtrace, stopwatch) and
snapshots. `tools/list` returns their JSON Schemas at runtime. The full
reference is in the repository root `README.md` ("Tool Reference").

## Testing

### Unit Tests

Run automated unit tests:
```bash
cd vice/src/mcp/tests
make test
```

Tests include:
- **test_mcp_tools.c**: Tool dispatch, validation, transport regressions, and
  bind/auth policy coverage

The transport regression tests cover both sides of the bind/auth policy:

- `0.0.0.0` without a token starts successfully.
- CORS without a token is rejected.

### Integration Tests

`tools/tests/test_mcp_protocol.py` (repository root) is a pytest suite that
runs against a live VICE instance. For a quick manual check:
```bash
# Terminal 1: Start VICE with MCP server
x64sc -mcpserver

# Terminal 2: Hit the live MCP endpoint
curl -sS http://127.0.0.1:6510/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json' \
  --data '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"vice_ping","arguments":{}},"id":1}'
```

See `tests/README.md` for detailed testing documentation.

## Design Documentation

The original design and plans are in `docs/plans/` at the repository root.
They predate the implementation; each starts with a status note saying what
was built, what changed and what was not done.

## Contributing

This implementation follows VICE coding guidelines. See:
- `vice/doc/coding-guidelines.txt`
- `vice/doc/Documentation-Howto.txt`

## License

This code is part of VICE and follows the same GPL v2+ license.
