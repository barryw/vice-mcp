# MCP Server Integration Tests

This document describes how to test the VICE MCP server integration end-to-end.

## Prerequisites

1. VICE built with MCP support (see doc/vice.texi "Building with MCP" section)
2. `curl` for HTTP testing
3. `python3` with json.tool for pretty-printing responses

## Build Verification

After building VICE with `--enable-mcp-server`, verify the MCP server compiled correctly:

```bash
cd /path/to/vice/build
./configure --enable-headlessui --disable-pdf-docs --enable-mcp-server
make -j8
```

Expected: No linker errors for `_mcp_server_*` symbols, `libmcp.a` successfully linked to all emulators.

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
- If process exits immediately: Emulator is in headless mode and needs a UI. This is expected behavior.
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
        "machine": "C64SC"
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
        "data": ["20", "20", "20", "20", "20", "20", "20", "20",
                 "20", "20", "20", "20", "20", "20", "20", "20"]
    }
}
```

**Notes**:
- Address 1024 (0x0400) is C64 screen memory
- 0x20 = space character in PETSCII
- Data is returned as hex strings

### Test 4: Memory Write

**Objective**: Write to emulated machine memory

**Command** (write "HELLO" to screen in PETSCII):
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

**Expected Output**:
```json
{
    "jsonrpc": "2.0",
    "id": 5,
    "result": {
        "PC": 0,
        "A": 0,
        "X": 0,
        "Y": 0,
        "SP": 0,
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

**Valid Registers**: PC, A, X, Y, SP, N, V, B, D, I, Z, C

### Test 7: Execution Control

**Objective**: Pause, step, and resume execution

**Pause**:
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.execution.pause","params":{},"id":7}' \
  | python3 -m json.tool
```

**Step** (execute 1 instruction):
```bash
curl -s -X POST http://127.0.0.1:6510/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"vice.execution.step","params":{"count":1},"id":8}' \
  | python3 -m json.tool
```

**Resume**:
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

**Expected**:
```json
{
    "jsonrpc": "2.0",
    "id": 2,
    "error": {
        "code": -32602,
        "message": "Invalid parameter types"
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
| x64      | C64         | 64KB   | Fast C64 emulation |
| xplus4   | PLUS4       | 64KB   | TED chip instead of VIC-II |
| xcbm2    | CBM2        | Varies | Business machine |
| xscpu64  | SCPU64      | 64KB+  | SuperCPU accelerator |

## Integration with External Tools

### Using with Claude Code

Claude Code (MCP client) can connect to the VICE MCP server for AI-assisted debugging:

1. Start VICE with MCP server enabled
2. Configure Claude Code with VICE MCP server URL
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
1. Use dots not underscores: `vice.memory.read` not `vice_memory_read`
2. Check spelling exactly
3. Verify method is registered in mcp_tools.c

### Issue: "Invalid parameter types"

**Cause**: Wrong parameter types or names

**Solutions**:
1. Memory read needs `address` (number) and `size` (number)
2. Memory write needs `address` (number) and `data` (array of numbers)
3. Register set needs `register` (string) and `value` (number)
4. Check that arrays contain numbers not strings

### Issue: Emulator exits immediately

**Cause**: Headless emulator with no UI has nothing to do

**Solutions**:
1. This is expected behavior in headless mode
2. Use UI-enabled build if you need emulator to stay running
3. Or use `-autostart` to load a program
4. MCP server works fine during brief lifecycle

## Test Results Summary

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
        "path": "/Users/<username>/.config/vice/mcp_snapshots/debug_checkpoint_1.vsf"
    }
}
```

**Notes**:
- Snapshots are stored in `~/.config/vice/mcp_snapshots/`
- A metadata JSON file is created alongside: `debug_checkpoint_1.vsf.json`
- Name must contain only alphanumeric characters, underscores, and hyphens
- Optional parameters: `save_roms` (default: false), `save_disks` (default: true)

**Verification** (check files exist):
```bash
ls -la ~/.config/vice/mcp_snapshots/
# Should show: debug_checkpoint_1.vsf and debug_checkpoint_1.vsf.json
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
        "name": "debug_checkpoint_1"
    }
}
```

**Notes**:
- Emulator state is fully restored to the saved point
- Useful for repeatable debugging sessions
- If snapshot doesn't exist, returns error code -32004

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
                "timestamp": "2026-02-03T14:30:00Z",
                "machine": "C64SC",
                "vice_version": "3.10"
            }
        ]
    }
}
```

**Notes**:
- Lists all `.vsf` files in the snapshots directory
- Includes metadata from companion `.vsf.json` files if available
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
        "message": "Invalid snapshot name: must contain only alphanumeric, underscore, hyphen"
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
        "code": -32004,
        "message": "Snapshot failed: file not found or load error"
    }
}
```

## Next Steps

1. Add automated integration tests to CI/CD pipeline
2. Test remaining emulators (x64, xplus4, xcbm2, xscpu64, vsid, x64dtv)
3. Performance testing under load
4. Long-running stability tests
5. Add UI integration for MCP settings
6. Create example client applications
