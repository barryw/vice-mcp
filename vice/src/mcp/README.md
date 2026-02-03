# VICE MCP Server

This directory contains the embedded Model Context Protocol (MCP) server for VICE, enabling AI assistants to control and inspect Commodore 8-bit emulations.

## Architecture

The MCP server is organized into three main components:

### 1. MCP Server Core (`mcp_server.c/h`)
- Initialization and lifecycle management
- VICE resource integration (MCPServerEnabled, MCPServerPort, MCPServerHost)
- Command-line options (-mcpserver, -mcpserverport, etc.)
- Configuration API

### 2. Transport Layer (`mcp_transport.c/h`)
- Streamable HTTP transport (HTTP POST + SSE)
- JSON-RPC 2.0 message handling
- Server-Sent Events (SSE) for real-time notifications
- Default port: 6510 (chosen for 6510 CPU reference)

### 3. Tool Dispatch (`mcp_tools.c/h`)
- MCP tool registry and dispatch
- Tool implementations (execution control, memory, registers, etc.)
- Direct access to VICE internal state (zero-copy architecture)
- Event notifications (breakpoints, state changes)

## Current Status

**Phase 1 - HTTP Transport Layer (COMPLETE)**
- ✅ Basic file structure created
- ✅ HTTP server integration (libmicrohttpd 1.0+)
- ✅ JSON library integration (cJSON bundled)
- ✅ VICE internal API integration
- ✅ JSON-RPC 2.0 endpoint (POST /mcp)
- ✅ Server-Sent Events endpoint (GET /events)
- ✅ Thread safety (pthread mutex)
- ✅ Security hardening (request limits, timeouts, CORS)
- ✅ Comprehensive test coverage

## Building

(To be updated once build system integration is complete)

```bash
# Configure with MCP server support
./configure --enable-mcp-server

# Build VICE
make

# Run with MCP server enabled
x64sc -mcpserver -mcpserverport 6510
```

## Configuration

The MCP server can be configured via resources or command-line options:

**Resources:**
- `MCPServerEnabled` (0/1) - Enable/disable MCP server
- `MCPServerPort` (1024-65535) - Port to listen on (default: 6510)
- `MCPServerHost` (string) - Host to bind to (default: 127.0.0.1)

**Command-line:**
```bash
x64sc -mcpserver                  # Enable MCP server
x64sc +mcpserver                  # Disable MCP server
x64sc -mcpserverport 6510         # Set port
x64sc -mcpserverhost 0.0.0.0      # Allow remote connections
```

## HTTP Transport Layer

The MCP server exposes two HTTP endpoints:

### POST /mcp - JSON-RPC 2.0 Endpoint

Accepts JSON-RPC 2.0 requests and returns responses.

**Example Request:**
```bash
curl -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "vice.ping",
    "params": {},
    "id": 1
  }'
```

**Example Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "ok",
    "version": "3.10",
    "machine": "C64"
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

### GET /events - Server-Sent Events (SSE)

Streams real-time notifications from VICE (breakpoints, execution state changes).

**Example Usage:**
```javascript
const events = new EventSource('http://127.0.0.1:6510/events');
events.addEventListener('breakpoint', (e) => {
  console.log('Breakpoint hit:', JSON.parse(e.data));
});
events.addEventListener('execution_stopped', (e) => {
  console.log('Execution stopped:', JSON.parse(e.data));
});
```

**Phase 2 Note:** Current implementation establishes SSE connections but cannot push events after initial response due to libmicrohttpd limitations. Full streaming requires upgrade to response callbacks or WebSockets.

### Transport Configuration

- **Maximum request size:** 10MB
- **Connection limit:** 100 concurrent connections
- **Connection timeout:** 30 seconds
- **SSE connection limit:** 10 concurrent event streams
- **CORS:** Configurable (default: `*` for development)

To change CORS policy, edit `CORS_ALLOW_ORIGIN` in `mcp_transport.c`:
```c
#define CORS_ALLOW_ORIGIN "http://localhost:3000"  // Specific origin
#define CORS_ALLOW_ORIGIN NULL                     // Disable CORS
```

## Security

- **Default binding:** localhost-only (127.0.0.1)
- **Remote access:** Requires explicit configuration with `-mcpserverhost`
- **Port range:** Restricted to 1024-65535 (no privileged ports)
- **DoS protection:** Request size limits, connection limits, timeouts
- **Input validation:** Content-Type validation, JSON parsing errors
- **Memory safety:** Integer overflow protection, null checks
- **Thread safety:** Mutex-protected shared state

**Security Warnings:**
- No built-in authentication - use reverse proxy (nginx, Apache) for auth
- No HTTPS support - terminate TLS at reverse proxy level
- CORS default is permissive (`*`) - restrict for production

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

### Phase 1 (Basic Control)
- `vice.ping` - Check if VICE is responding
- `vice.execution.run` - Resume execution
- `vice.execution.pause` - Pause execution
- `vice.execution.step` - Step instructions
- `vice.registers.get` - Get CPU registers
- `vice.registers.set` - Set register values
- `vice.memory.read` - Read memory
- `vice.memory.write` - Write memory

### Future Phases
See `/docs/plans/2026-02-02-vice-mcp-server-design.md` for complete roadmap.

## Testing

### Unit Tests

Run automated unit tests:
```bash
cd vice/src/mcp/tests
make test
```

Tests include:
- **test_mcp_tools.c**: Tool dispatch and validation
- **test_mcp_transport.c**: HTTP transport memory safety

All tests pass with zero memory leaks (valgrind verified).

### Integration Tests

Test HTTP transport with running VICE:
```bash
# Terminal 1: Start VICE with MCP server
x64sc -mcpserver -mcpserverport 6510

# Terminal 2: Run integration tests
cd vice/src/mcp/tests
./test_http_transport.sh
```

See `tests/README.md` for detailed testing documentation.

## Design Documentation

Full design documentation is available at:
`/docs/plans/2026-02-02-vice-mcp-server-design.md`

## Contributing

This implementation follows VICE coding guidelines. See:
- `vice/doc/coding-guidelines.txt`
- `vice/doc/Documentation-Howto.txt`

## License

This code is part of VICE and follows the same GPL v2+ license.
