# MCP Server Integration Tests

This document describes how to test the VICE MCP server integration end-to-end.

## Prerequisites

1. VICE built with MCP support (see "Building from Source" in the repository
   root `README.md`; configure with `--enable-mcp-server`)
2. `curl` for HTTP testing
3. `python3` with json.tool for pretty-printing responses
4. `requests` (`pip install requests`) for the Python example

For an automated run against a live instance, see the pytest suite in
`tools/tests/test_mcp_protocol.py` at the repository root.

## Build Verification

After building VICE with `--enable-mcp-server`, verify the MCP server compiled correctly:

```bash
cd /path/to/vice
./autogen.sh
# not "build": vice/build/ is VICE's packaging directory
mkdir -p build-headless && cd build-headless
../configure --enable-headlessui --disable-pdf-docs --enable-mcp-server
make -j"$(getconf _NPROCESSORS_ONLN)"
```

Expected: No linker errors for `_mcp_server_*` symbols; `libmcp.a` linked into
every emulator except `vsid` (VSID has no MCP server).

The commands below run from the build directory (`./src/x64sc`).

## Test Suite

### Test 1: Server Startup

**Objective**: Verify emulators start with MCP server enabled

**Commands**:
```bash
# Test x64sc
./src/x64sc -mcpserver -mcpserverport 6510 &

# Test xvic
./src/xvic -mcpserver -mcpserverport 6520 &

# Test xpet
./src/xpet -mcpserver -mcpserverport 6530 &

# Test x128
./src/x128 -mcpserver -mcpserverport 6540 &
```

**Verification**:
```bash
lsof -i :6510  # Should show x64sc listening
lsof -i :6520  # Should show xvic listening
lsof -i :6530  # Should show xpet listening
lsof -i :6540  # Should show x128 listening
```

**Expected Output**: Each emulator process listening on its assigned port

**Troubleshooting**:
- If process exits immediately: something failed at startup (missing ROMs, or
  a port below 1024, which `-mcpserverport` rejects). Run it in the foreground
  and read the log. Headless builds keep running until killed.
- If "Connection refused": Server didn't start. Check logs for errors.
- If "Address already in use": Another process is using the port. Choose a different port.

### Test 2: Ping / Connectivity

**Objective**: Verify MCP server responds to HTTP requests

**Command**:
```bash
curl -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.ping","id":1}' | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "status": "ok",
        "version": "3.10",
        "machine": "C64SC",
        "execution": "running"
    }
}
```

**Failure Modes**:
- HTTP connection refused → Server not started
- Timeout → Firewall blocking connection
- Method not found → Tool registry issue

### Test 3: Memory Read

**Objective**: Verify memory read from emulated machine

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":1024,"size":16},"id":2}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "result": {
        "address": 1024,
        "size": 16,
        "encoding": "array",
        "data": ["20", "20", "20", "20", "20", "20", "20", "20",
                 "20", "20", "20", "20", "20", "20", "20", "20"]
    }
}
```

**Notes**:
- Address 1024 (0x0400) is C64 screen memory
- 0x20 = space (screen code and PETSCII)
- Data is returned as hex strings; pass `"encoding": "hex"` to get one
  `data_hex` string instead

### Test 4: Memory Write

**Objective**: Write to emulated machine memory

**Command** (write "HELLO" to screen RAM as screen codes):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.write","params":{"address":1024,"data":[8,5,12,12,15]},"id":3}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 3,
    "result": {
        "status": "ok",
        "address": 1024,
        "bytes_written": 5
    }
}
```

**Verification** (read back):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":1024,"size":5},"id":4}' \
  | python3 -m json.tool
```

**Expected**:
```json
{
    "jsonrpc": "2.0",
    "id": 4,
    "result": {
        "address": 1024,
        "size": 5,
        "encoding": "array",
        "data": ["08", "05", "0C", "0C", "0F"]
    }
}
```

### Test 5: Register Read

**Objective**: Read CPU register values

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.registers.get","params":{},"id":5}' \
  | python3 -m json.tool
```

**Expected Output** (values depend on where the CPU is; this is the KERNAL
idle loop after boot):
```json
{
    "jsonrpc": "2.0",
    "id": 5,
    "result": {
        "PC": 58833,
        "A": 0,
        "X": 0,
        "Y": 10,
        "SP": 243,
        "N": false,
        "V": false,
        "B": false,
        "D": false,
        "I": false,
        "Z": true,
        "C": false
    }
}
```

**Notes**:
- PC = Program Counter
- A/X/Y = 8-bit registers
- SP = Stack Pointer
- N/V/B/D/I/Z/C = Status flags

### Test 6: Register Write

**Objective**: Modify CPU register values

**Command** (set A register to 42):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.registers.set","params":{"register":"A","value":42},"id":6}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 6,
    "result": {
        "status": "ok",
        "register": "A",
        "value": 42
    }
}
```

**Valid Registers** (upper case): PC, A, X, Y, SP, N, V, B, D, I, Z, C

### Test 7: Execution Control

**Objective**: Pause, step, and resume execution

**Pause** (returns `{"status": "ok", "message": "Execution paused"}`):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.execution.pause","params":{},"id":7}' \
  | python3 -m json.tool
```

**Step** (execute 1 instruction; returns `instructions`, `step_over`,
`completed: true` and the new `PC`):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.execution.step","params":{"count":1},"id":8}' \
  | python3 -m json.tool
```

**Resume** (returns `{"status": "ok", "message": "Execution resumed"}`):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.execution.run","params":{},"id":9}' \
  | python3 -m json.tool
```

## Error Handling Tests

### Invalid Method

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.nonexistent","id":1}' \
  | python3 -m json.tool
```

**Expected**:
```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": -32601,
        "message": "Method not found"
    }
}
```

### Invalid Parameters

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":"invalid"},"id":2}' \
  | python3 -m json.tool
```

**Expected** (a string address that is not `$hex`/`0xhex` is looked up as a
symbol name):
```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "error": {
        "code": -32602,
        "message": "Cannot resolve address: symbol not found"
    }
}
```

## Cross-Emulator Compatibility

All emulators should support the same MCP tool API:

| Emulator | Machine Type | Memory | Notes |
|----------|-------------|--------|-------|
| x64sc    | C64SC       | 64KB   | Accurate cycle emulation |
| xvic     | VIC20       | Varies | 5KB-32KB depending on config |
| xpet     | PET         | Varies | 8KB-96KB depending on model |
| x128     | C128        | 128KB  | Dual 8502/Z80 CPUs |
| x64      | C64         | 64KB   | Fast C64 emulation; built only with `--enable-x64` |
| xplus4   | PLUS4       | 64KB   | TED chip instead of VIC-II |
| xcbm2    | CBM-II      | Varies | Business machine (6x0/7x0) |
| xcbm5x0  | CBM-II      | Varies | Business machine (5x0) |
| xscpu64  | SCPU64      | 64KB+  | SuperCPU accelerator |
| x64dtv   | C64DTV      | 2MB    | C64 Direct-to-TV |

The VIC-II, sprite and CIA tools only work on the C64 family (x64sc, x64,
x128, xscpu64, x64dtv). `vsid` has no MCP server.

## Integration with External Tools

### Using with Claude Code

Claude Code (MCP client) can connect to the VICE MCP server for AI-assisted debugging:

1. Start VICE with MCP server enabled
2. Add it to Claude Code: `claude mcp add --transport http vice http://127.0.0.1:6510/mcp`
3. Use natural language to interact with emulator:
   - "Read memory at $C000"
   - "Set register A to $FF"
   - "Step 10 instructions"

### Using with Custom Scripts

```python
import requests
import json

def vice_call(method, params=None):
    payload = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": 1
    }
    response = requests.post(
        "http://127.0.0.1:6510/mcp",
        headers={"Content-Type": "application/json"},
        data=json.dumps(payload)
    )
    return response.json()

# Example: Read screen memory
result = vice_call("vice.memory.read", {"address": 1024, "size": 16})
print(result)
```

## Automated Test Runner

Create `test_mcp.sh`:

```bash
#!/bin/bash

# Start emulator
./src/x64sc -mcpserver -mcpserverport 6510 &
VICE_PID=$!
sleep 2

# Test ping
echo "Testing ping..."
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.ping","id":1}' \
  | python3 -m json.tool

# Test memory read
echo "Testing memory read..."
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":1024,"size":16},"id":2}' \
  | python3 -m json.tool

# Cleanup
kill $VICE_PID
```

## Common Issues

### Issue: "Connection refused"

**Cause**: MCP server didn't start

**Solutions**:
1. Check if emulator is running: `ps aux | grep x64sc`
2. Check if port is listening: `lsof -i :6510`
3. Check logs for errors
4. Verify `-mcpserver` flag was used
5. Try a different port to rule out port conflicts

### Issue: "Method not found"

**Cause**: Incorrect method name

**Solutions**:
1. Both `vice.memory.read` and `vice_memory_read` are accepted (`tools/list`
   publishes the underscore form)
2. Check spelling exactly
3. Verify the tool is registered in `tool_registry[]` in `src/mcp/mcp_tools.c`

### Issue: Error -32602 (invalid params)

**Cause**: Wrong parameter types or names

**Solutions**:
1. Memory read needs `address` (0-65535, `"$C000"`/`"0xC000"`, or a loaded
   symbol name) and `size` (1-65535)
2. Memory write needs `address` (as above) and `data` (array of numbers)
3. Register set needs `register` (string) and `value` (number)
4. Check that arrays contain numbers not strings

### Issue: HTTP 401

**Cause**: VICE was started with `-mcpservertoken`

**Solution**: Send `Authorization: Bearer <token>` with every request.

### Issue: Emulator exits immediately

**Cause**: Startup failed. Headless builds keep running until killed, so an
immediate exit is an error.

**Solutions**:
1. Run in the foreground and read the log
2. Check that the ROMs are found (`-directory`, or an installed VICE)
3. Use a port from 1024 to 65535

## Test Results Summary

Historical record of the first run (2026-02-02), kept as-is.

**Date**: 2026-02-02

**VICE Version**: 3.10

**Build Configuration**: `--enable-headlessui --enable-mcp-server`

**Platform**: macOS ARM64

| Test | x64sc | xvic | xpet | x128 | Status |
|------|-------|------|------|------|--------|
| Server startup | ✅ | ✅ | ✅ | ✅ | PASS |
| Ping | ✅ | ✅ | ✅ | ✅ | PASS |
| Memory read | ✅ | - | - | - | PASS |
| Memory write | ✅ | - | - | - | PASS |
| Register read | ✅ | - | - | - | PASS |
| Register set | ✅ | - | - | - | PASS |

**Notes**:
- All tested emulators start successfully with MCP server enabled
- Ping works on all emulators, confirming HTTP transport layer
- Detailed tool tests performed on x64sc only
- Other emulators share same tool implementation, expected to work identically
- No compilation errors, no linker errors, no runtime crashes

## Snapshot Tests

### Test 8: Save Snapshot

**Objective**: Save emulator state to a named snapshot with metadata

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.save","params":{"name":"debug_checkpoint_1","description":"State before bug reproduction"},"id":10}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 10,
    "result": {
        "status": "ok",
        "name": "debug_checkpoint_1",
        "path": "/Users/<username>/.config/vice/mcp_snapshots/debug_checkpoint_1.vsf",
        "description": "State before bug reproduction",
        "include_roms": false,
        "include_disks": false
    }
}
```

**Notes**:
- Snapshots are stored in `mcp_snapshots/` under the VICE user config
  directory: `$XDG_CONFIG_HOME/vice` (default `~/.config/vice`) on Linux and
  macOS, `%APPDATA%\vice` on Windows
- A metadata JSON file is created alongside: `debug_checkpoint_1.json`
- Name must contain only alphanumeric characters, underscores, and hyphens
- Optional parameters: `include_roms` (default: false), `include_disks` (default: false)
- Saving over an existing name fails (-32602). There is no delete tool; remove
  the `.vsf` and `.json` files before re-running Tests 8 and 11

**Verification** (check files exist):
```bash
ls -la ~/.config/vice/mcp_snapshots/
# Should show: debug_checkpoint_1.vsf and debug_checkpoint_1.json
```

### Test 9: Load Snapshot

**Objective**: Restore emulator state from a named snapshot

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.load","params":{"name":"debug_checkpoint_1"},"id":11}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 11,
    "result": {
        "status": "ok",
        "name": "debug_checkpoint_1",
        "path": "/Users/<username>/.config/vice/mcp_snapshots/debug_checkpoint_1.vsf",
        "message": "Snapshot loaded successfully"
    }
}
```

**Notes**:
- Emulator state is fully restored to the saved point
- Useful for repeatable debugging sessions
- If snapshot doesn't exist, returns error code -32602

### Test 10: List Snapshots

**Objective**: Discover available snapshots with metadata

**Command**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.list","params":{},"id":12}' \
  | python3 -m json.tool
```

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 12,
    "result": {
        "directory": "/Users/<username>/.config/vice/mcp_snapshots",
        "snapshots": [
            {
                "name": "debug_checkpoint_1",
                "path": "/Users/<username>/.config/vice/mcp_snapshots/debug_checkpoint_1.vsf",
                "description": "State before bug reproduction",
                "created": "2026-02-03T14:30:00Z",
                "machine": "C64SC"
            }
        ]
    }
}
```

**Notes**:
- Lists all `.vsf` files in the snapshots directory
- Includes metadata from companion `.json` files if available
- Snapshots without metadata files still appear but with limited info

### Test 11: Snapshot Save/Load Cycle

**Objective**: Verify complete save/restore workflow preserves state

**Workflow**:
```bash
# 1. Write known data to screen memory
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.write","params":{"address":1024,"data":[1,2,3,4,5]},"id":1}'

# 2. Save snapshot
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.save","params":{"name":"memory_test"},"id":2}'

# 3. Overwrite memory with different data
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.write","params":{"address":1024,"data":[255,255,255,255,255]},"id":3}'

# 4. Verify memory changed
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":1024,"size":5},"id":4}'
# Expected: ["FF", "FF", "FF", "FF", "FF"]

# 5. Load snapshot
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.load","params":{"name":"memory_test"},"id":5}'

# 6. Verify memory restored to original
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.memory.read","params":{"address":1024,"size":5},"id":6}'
# Expected: ["01", "02", "03", "04", "05"]
```

**Expected**: After loading snapshot, memory returns to the saved state

### Snapshot Error Cases

**Save with invalid name**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.save","params":{"name":"invalid/name"},"id":1}'
```

**Expected**:
```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": -32602,
        "message": "Invalid name: use only alphanumeric characters, underscores, and hyphens"
    }
}
```

**Load non-existent snapshot**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.snapshot.load","params":{"name":"does_not_exist"},"id":1}'
```

**Expected**:
```json
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": -32602,
        "message": "Snapshot not found. Use vice.snapshot.list to see available snapshots."
    }
}
```

## Next Steps

1. Run the live protocol suite (`tools/tests/test_mcp_protocol.py`) in CI
2. Test remaining emulators (x64, xplus4, xcbm2, xcbm5x0, xscpu64, x64dtv)
3. Performance testing under load
4. Long-running stability tests
5. Add UI integration for MCP settings
6. Create example client applications
