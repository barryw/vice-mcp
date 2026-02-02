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

**Phase 1 - Initial Structure (CURRENT)**
- ✅ Basic file structure created
- ⏳ HTTP server integration (TODO: add libmicrohttpd)
- ⏳ JSON library integration (TODO: add cJSON or jansson)
- ⏳ VICE internal API integration (TODO: include headers from vice/src/)

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

## Security

- Default binding is localhost-only (127.0.0.1)
- Remote access requires explicit configuration
- Port range restricted to 1024-65535 (no privileged ports)
- TODO: Add IP whitelist support
- TODO: Add authentication/authorization

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

(To be added)

## Design Documentation

Full design documentation is available at:
`/docs/plans/2026-02-02-vice-mcp-server-design.md`

## Contributing

This implementation follows VICE coding guidelines. See:
- `vice/doc/coding-guidelines.txt`
- `vice/doc/Documentation-Howto.txt`

## License

This code is part of VICE and follows the same GPL v2+ license.
