# VICE Embedded MCP Server Design

**Date:** 2026-02-02
**Status:** Design Phase
**Target:** VICE upstream PR submission

## Executive Summary

This document describes the architecture for embedding a Model Context Protocol (MCP) server directly into VICE, enabling AI assistants to control and inspect Commodore 8-bit emulations with unprecedented precision. Unlike the current external MCP server (ViceMCP) that relies on VICE's primitive binary monitor protocol, this embedded approach provides zero-copy access to emulation state, machine-agnostic abstractions, and state-based breakpoints.

The embedded MCP server will use Streamable HTTP transport (HTTP POST + SSE) on port 6510, expose chip-specific toolsets (VIC-II, VIC, TED, SID, CIA, etc.), and support source code annotations for AI debugging hints. This will enable AI agents to debug their own 6510/6502 assembly code with full control over sprites, raster timing, interrupts, memory banking, and all other machine capabilities.

## Architecture Overview

### Transport Layer: Streamable HTTP on Port 6510

**Protocol:** Streamable HTTP (as of March 2025 MCP spec)
- **Client → Server:** HTTP POST to `/mcp` with JSON-RPC 2.0 messages
- **Server → Client:** HTTP 200 + SSE (Server-Sent Events) for streaming responses
- **Port:** 6510 (chosen for 6510 CPU reference, not arbitrary like 6502)

**Why Not Stdio?**
- VICE is a GUI application that needs stderr/stdout for debugging
- Stdio transport would complicate development and testing
- Network transport allows remote debugging and multiple client connections

**Why Not WebSocket?**
- Streamable HTTP is the current MCP standard (March 2025)
- Better HTTP infrastructure compatibility (proxies, load balancers)
- Simpler implementation (standard HTTP + SSE)

### Zero-Copy Architecture

**Single Source of Truth:** MCP server reads directly from emulation state
- No caching of register values, memory contents, or sprite data
- Every MCP tool call reads fresh data from VICE's internal structures
- Eliminates state drift issues that plague external monitor protocol

**Direct State Access:**
```
MCP Tool Call → VICE Internal API → Emulation State
                                    ↓
                                Read/Write directly
```

**Benefits:**
- Always accurate, never stale
- No synchronization overhead
- Minimal memory footprint
- Zero latency for state queries

### Event-Driven Updates

**Server-Sent Events (SSE) for Real-Time Notifications:**
- Breakpoint hit → immediate SSE notification with PC, registers, context
- Frame complete → optional SSE with frame number, cycle count
- Sprite collision → SSE with collision mask, raster line
- Raster interrupt → SSE with line number, cycle within line

**Client can subscribe to events:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "vice.events.subscribe",
    "arguments": {
      "events": ["breakpoint", "sprite_collision", "frame_complete"]
    }
  }
}
```

## MCP Tools/Commands

### Execution Control
- `vice.execution.run` - Resume execution
- `vice.execution.pause` - Pause at next instruction
- `vice.execution.step` - Step one or more instructions (with step-over support)
- `vice.execution.step_out` - Execute until current subroutine returns (RTS/RTI)
- `vice.execution.reset` - Soft or hard reset

### State Inspection
- `vice.registers.get` - Get all CPU registers (PC, A, X, Y, SP, flags)
- `vice.registers.set` - Set specific register values
- `vice.memory.read` - Read memory range (supports all banks)
- `vice.memory.write` - Write to memory
- `vice.memory.search` - Search for byte patterns
- `vice.memory.compare` - Compare two memory regions

### Breakpoints (Traditional)
- `vice.breakpoints.set` - Set PC-based breakpoint
- `vice.breakpoints.list` - List all breakpoints
- `vice.breakpoints.delete` - Remove breakpoint
- `vice.breakpoints.toggle` - Enable/disable breakpoint
- `vice.breakpoints.condition` - Set conditional expression (e.g., "A==0xFF && X>$10")

### State-Based Breakpoints (NEW)
- `vice.breakpoints.raster` - Break at specific raster line(s)
- `vice.breakpoints.sprite_collision` - Break on sprite-sprite or sprite-background collision
- `vice.breakpoints.vic.register_write` - Break when VIC-II register written
- `vice.breakpoints.memory.watch` - Break on memory read/write/change
- `vice.breakpoints.composite` - Combine multiple conditions (e.g., "raster==100 && sprite_y[0]<50")

### Chip-Specific Toolsets

#### VIC-II Tools (C64, C128)
- `vice.vic2.registers.get` - Get all VIC-II registers ($D000-$D02E)
- `vice.vic2.sprites.get` - Get sprite data (X, Y, color, multicolor, priority, enable mask)
- `vice.vic2.sprites.set` - Update sprite properties
- `vice.vic2.raster.get` - Get current raster line and cycle
- `vice.vic2.screen.capture` - Capture framebuffer as PNG/base64
- `vice.vic2.borders.get` - Get border color history (for raster effects)

#### VIC Tools (VIC-20)
- `vice.vic.registers.get` - Get VIC registers
- `vice.vic.screen.capture` - Capture screen

#### TED Tools (Plus/4, C16)
- `vice.ted.registers.get` - Get TED registers (video + sound)
- `vice.ted.screen.capture` - Capture screen

#### SID Tools (All machines with SID)
- `vice.sid.registers.get` - Get SID registers ($D400-$D418)
- `vice.sid.voices.get` - Get voice parameters (frequency, waveform, ADSR)
- `vice.sid.audio.capture` - Capture audio buffer for analysis

#### CIA Tools (C64, C128, VIC-20)
- `vice.cia1.registers.get` - Get CIA#1 registers (keyboard, joystick)
- `vice.cia2.registers.get` - Get CIA#2 registers (serial, user port)
- `vice.cia.timers.get` - Get timer values and settings

### Resource/Configuration Control
- `vice.config.get` - Get VICE resource (e.g., "MachineVideoStandard")
- `vice.config.set` - Set VICE resource
- `vice.config.list` - List available resources for current machine
- `vice.machine.capabilities` - Get machine capabilities (sprites, SID, drive types, memory banks)
- `vice.machine.configure` - Apply machine profile (NTSC/PAL, memory expansion, drive types)

### File Operations
- `vice.files.autostart` - Load and run PRG, D64, T64, etc.
- `vice.files.attach_disk` - Attach disk image to drive 8/9/10/11
- `vice.files.attach_tape` - Attach tape image
- `vice.files.attach_cartridge` - Attach cartridge image
- `vice.files.detach` - Detach media

### Instrumentation & Tracing (NEW)
- `vice.trace.frame.start` - Begin frame tracing (capture all register writes per frame)
- `vice.trace.frame.stop` - Stop tracing and return captured data
- `vice.trace.frame.get` - Get traced frame data as structured JSON
- `vice.trace.cycles.get` - Get cycle count since last reset/checkpoint
- `vice.trace.events.get` - Get instrumentation events (sprite setup, IRQ triggers, etc.)

### Annotation Support
- `vice.annotations.list` - List all @mcp-* annotations found in loaded symbols
- `vice.annotations.get` - Get annotation details for specific address/label
- `vice.annotations.search` - Search annotations by type or content

## Machine-Agnostic Abstraction Layer

### Capability Discovery

**Every MCP client should query machine capabilities on connection:**

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "vice.machine.capabilities"
  }
}
```

**Response includes:**
```json
{
  "machine": "C64",
  "cpu": "6510",
  "video_chip": "VIC-II",
  "sound_chip": "SID",
  "has_sprites": true,
  "max_sprites": 8,
  "has_cartridge_port": true,
  "drive_types": ["1541", "1571", "1581"],
  "memory_banks": ["main", "color_ram", "char_rom", "kernal_rom"],
  "video_standards": ["NTSC", "PAL"],
  "expansion_ports": ["user_port", "expansion_port"],
  "available_toolsets": [
    "vice.vic2.*",
    "vice.sid.*",
    "vice.cia1.*",
    "vice.cia2.*"
  ]
}
```

### Chip-Specific Namespaces

**Tools are organized by chip:**
- `vice.vic2.*` - Only available on C64, C128
- `vice.vic.*` - Only available on VIC-20
- `vice.ted.*` - Only available on Plus/4, C16
- `vice.sid.*` - Available on machines with SID
- `vice.cia1.*` / `vice.cia2.*` - Available on machines with CIA chips

**Attempting to call unavailable tool returns error:**
```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32601,
    "message": "Tool not available on this machine",
    "data": {
      "tool": "vice.vic2.sprites.get",
      "machine": "VIC-20",
      "reason": "VIC-20 does not have VIC-II chip"
    }
  }
}
```

### Machine Profiles

**Predefined configurations for common setups:**

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "vice.machine.configure",
    "arguments": {
      "profile": "c64_ntsc_8580_1541",
      "apply_immediately": true
    }
  }
}
```

**Profile includes:**
- Video standard (NTSC/PAL)
- SID model (6581/8580)
- Drive types and count
- Memory expansions
- Cartridge settings

**AI agents can create custom profiles:**
```json
{
  "profile_name": "my_test_config",
  "machine": "C64",
  "video_standard": "PAL",
  "sid_model": "8580",
  "drives": [
    {"unit": 8, "type": "1541"},
    {"unit": 9, "type": "1571"}
  ],
  "memory_expansion": "REU_512K",
  "cartridge": null
}
```

## Annotation Syntax for AI Hints

### Purpose

Source code annotations provide AI debugging hints embedded directly in assembly code. These help AI agents understand:
- Multiplexed sprite systems
- Interrupt handlers and their purposes
- Timing-critical sections
- Memory usage patterns
- State machines and game logic

### Syntax

**Format:** `@mcp-<type>: <description>`

**Placement:** Comments in assembly source files (typically .asm, .s, .a)

**Example:**
```asm
; @mcp-multiplex: Sprite multiplexer, 32 logical sprites using 8 hardware sprites
; @mcp-timing-critical: Must complete within vblank (63 raster lines)
sprite_multiplex:
    lda #$00
    sta sprite_counter
    ; ... multiplexing code ...
    rts

; @mcp-irq-handler: Raster interrupt at line 100 for score display
; @mcp-state: Updates score_dirty flag, safe to modify score_buffer
irq_score_update:
    lda #$01
    sta score_dirty
    ; ... score update code ...
    jmp $ea31  ; @mcp-irq-exit: Standard Kernal IRQ exit
```

### Annotation Types

| Type | Description | Example |
|------|-------------|---------|
| `@mcp-multiplex` | Sprite multiplexing code | `@mcp-multiplex: 32 chess pieces using 8 hardware sprites` |
| `@mcp-irq-handler` | Interrupt handler entry point | `@mcp-irq-handler: Raster IRQ at line 100` |
| `@mcp-irq-exit` | Interrupt handler exit | `@mcp-irq-exit: Chain to Kernal IRQ` |
| `@mcp-timing-critical` | Time-sensitive code section | `@mcp-timing-critical: Must finish before raster line 250` |
| `@mcp-state` | State description or constraints | `@mcp-state: game_phase must be 0-3, 4=game_over` |
| `@mcp-memory` | Memory usage documentation | `@mcp-memory: $C000-$CFFF sprite data, $D000-$D7FF screen buffer` |
| `@mcp-function` | Function purpose and contracts | `@mcp-function: Draw sprite at (X,Y), preserves A` |
| `@mcp-bug-known` | Known bug or workaround | `@mcp-bug-known: Sprite flickers at X=255, hardware limitation` |
| `@mcp-optimization` | Performance-critical section | `@mcp-optimization: Unrolled loop, saves 120 cycles/frame` |
| `@mcp-data` | Data structure layout | `@mcp-data: Each entry is 5 bytes: X, Y, color, sprite_num, flags` |

### Symbol File Integration

**VICE loads symbols from various formats:**
- VICE native (.vs, .labels)
- ACME (.lbl)
- CA65 (.dbg)
- KickAssembler (.sym)

**MCP server scans symbol files for annotations on startup:**
1. Load symbol file(s) specified in VICE config
2. Parse each label/comment for `@mcp-*` patterns
3. Build annotation index: `{address → annotations[]}`
4. Expose via `vice.annotations.*` tools

**AI agents query annotations:**
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "vice.annotations.get",
    "arguments": {
      "address": "$C000"
    }
  }
}
```

**Response:**
```json
{
  "address": "$C000",
  "label": "sprite_multiplex",
  "annotations": [
    {
      "type": "multiplex",
      "description": "Sprite multiplexer, 32 logical sprites using 8 hardware sprites"
    },
    {
      "type": "timing-critical",
      "description": "Must complete within vblank (63 raster lines)"
    }
  ],
  "source_file": "sprites.asm",
  "line_number": 142
}
```

## Instrumentation API

### Frame Tracing

**Purpose:** Capture every VIC-II/VIC/TED register write during a frame for timing analysis.

**Usage:**
```json
// Start tracing
{"name": "vice.trace.frame.start"}

// Let emulation run for N frames
{"name": "vice.execution.run"}

// Stop and retrieve trace data
{"name": "vice.trace.frame.stop"}
{"name": "vice.trace.frame.get"}
```

**Trace Output:**
```json
{
  "frame_number": 1234,
  "frames_captured": 1,
  "total_cycles": 63000,
  "events": [
    {
      "cycle": 125,
      "raster_line": 0,
      "raster_cycle": 125,
      "type": "vic2_write",
      "register": "$D012",
      "register_name": "RASTER",
      "value": "$64",
      "pc": "$C120",
      "annotation": "irq_score_update"
    },
    {
      "cycle": 1802,
      "raster_line": 100,
      "raster_cycle": 42,
      "type": "sprite_write",
      "register": "$D000",
      "register_name": "SP0X",
      "value": "$78",
      "pc": "$C350",
      "annotation": "sprite_multiplex"
    }
  ]
}
```

**AI uses this to:**
- Verify sprite multiplexing timing
- Debug raster effects
- Analyze interrupt handler performance
- Detect timing violations (too many cycles in vblank)

### Sprite Events

**Purpose:** Track sprite-related events without manual breakpoints.

**Events:**
- Sprite enabled/disabled (D015 writes)
- Sprite position changes (D000-D00F writes)
- Sprite collision detected (D01E, D01F reads)
- Sprite priority changes (D01B writes)
- Sprite multicolor changes (D01C writes)

**Captured automatically when `vice.trace.frame.start` active.**

### Cycle Counting

**Purpose:** Measure precise cycle counts between checkpoints.

```json
// Reset cycle counter
{"name": "vice.trace.cycles.reset"}

// Run code
{"name": "vice.execution.run"}

// Break at endpoint
{"name": "vice.breakpoints.set", "arguments": {"address": "$C200"}}

// Get cycle count
{"name": "vice.trace.cycles.get"}
// Returns: {"cycles": 1234, "frames": 0, "raster_lines": 19}
```

## State-Based Breakpoint System

### Traditional Breakpoints

**PC-based breakpoints:**
```json
{
  "name": "vice.breakpoints.set",
  "arguments": {
    "address": "$C000",
    "condition": "A==0xFF"  // optional
  }
}
```

### Raster Breakpoints (NEW)

**Break at specific raster line:**
```json
{
  "name": "vice.breakpoints.raster",
  "arguments": {
    "line": 100,
    "condition": "sprite_y[0] < 50"  // optional
  }
}
```

**Break on range:**
```json
{
  "name": "vice.breakpoints.raster",
  "arguments": {
    "line_start": 100,
    "line_end": 150,
    "break_on_each": false  // break once when entering range
  }
}
```

### Sprite Collision Breakpoints (NEW)

**Break on any sprite collision:**
```json
{
  "name": "vice.breakpoints.sprite_collision",
  "arguments": {
    "type": "sprite_sprite"  // or "sprite_background"
  }
}
```

**Break on specific sprite collision:**
```json
{
  "name": "vice.breakpoints.sprite_collision",
  "arguments": {
    "type": "sprite_sprite",
    "sprite_mask": 0b00000011,  // sprites 0 and 1
    "condition": "raster_line > 100"
  }
}
```

### VIC-II Register Breakpoints (NEW)

**Break when VIC-II register written:**
```json
{
  "name": "vice.breakpoints.vic.register_write",
  "arguments": {
    "register": "$D012",  // RASTER
    "condition": "value > $80"
  }
}
```

**Track all D012 writes in a frame:**
```json
{
  "name": "vice.breakpoints.vic.register_write",
  "arguments": {
    "register": "$D012",
    "log_only": true,  // don't break, just log to trace
    "duration_frames": 1
  }
}
```

### Memory Watch Breakpoints (NEW)

**Break on memory change:**
```json
{
  "name": "vice.breakpoints.memory.watch",
  "arguments": {
    "address": "$0400",
    "size": 1000,  // watch $0400-$07E7 (screen memory)
    "trigger": "write",  // or "read" or "change"
    "condition": "value == $20"  // space character
  }
}
```

### Composite Breakpoints (NEW)

**Combine multiple conditions:**
```json
{
  "name": "vice.breakpoints.composite",
  "arguments": {
    "conditions": [
      {"type": "raster", "line": 100},
      {"type": "sprite_y", "sprite": 0, "operator": "<", "value": 50},
      {"type": "register", "register": "A", "operator": "==", "value": 0}
    ],
    "operator": "AND"  // all conditions must be true
  }
}
```

**Break when any condition is true:**
```json
{
  "name": "vice.breakpoints.composite",
  "arguments": {
    "conditions": [
      {"type": "pc", "address": "$C000"},
      {"type": "pc", "address": "$C100"},
      {"type": "sprite_collision", "collision_type": "sprite_sprite"}
    ],
    "operator": "OR"
  }
}
```

## Error Handling & State Management

### Execution State Machine

**VICE emulation states:**
- `RUNNING` - Normal execution
- `PAUSED` - Paused by user or MCP command
- `BREAKPOINT` - Stopped at breakpoint
- `ERROR` - Emulation error (invalid opcode, bus error, etc.)
- `RESET` - Machine reset in progress

**Every MCP response includes current state:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "execution_state": "BREAKPOINT",
    "pc": "$C120",
    "breakpoint_id": 5,
    "breakpoint_type": "raster",
    "raster_line": 100
  }
}
```

### Error Response Format

**Standard error format:**
```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32600,
    "message": "Invalid address range",
    "data": {
      "error_type": "ADDRESS_OUT_OF_BOUNDS",
      "requested_address": "$FFFF",
      "requested_size": 256,
      "valid_range": "$0000-$FFFF",
      "suggested_fix": "Reduce size to 1 byte or use valid address"
    }
  }
}
```

**Error codes:**
- `-32600`: Invalid Request (bad parameters)
- `-32601`: Method Not Found (tool doesn't exist or unavailable for machine)
- `-32602`: Invalid Params (wrong parameter types)
- `-32603`: Internal Error (VICE internal error)
- `-32000`: Execution Error (emulation is in error state)
- `-32001`: State Error (operation not valid in current execution state)

### State Transition Guards

**Some operations only valid in certain states:**

| Operation | Valid States | Error if Invalid |
|-----------|--------------|------------------|
| `vice.execution.run` | PAUSED, BREAKPOINT | "Cannot run from ERROR state" |
| `vice.execution.step` | PAUSED, BREAKPOINT | "Cannot step while RUNNING" |
| `vice.memory.write` | PAUSED, BREAKPOINT | "Cannot write memory while RUNNING" |
| `vice.registers.set` | PAUSED, BREAKPOINT | "Cannot set registers while RUNNING" |
| `vice.breakpoints.set` | ANY | Always valid |

**AI agents should check state before operations:**
```json
{"name": "vice.execution.get_state"}
// Returns: {"state": "RUNNING", "pc": "$C120"}

// Pause before modifying
{"name": "vice.execution.pause"}
{"name": "vice.memory.write", "arguments": {...}}
{"name": "vice.execution.run"}
```

### Clearing Errors

**When emulation enters ERROR state:**
```json
{
  "execution_state": "ERROR",
  "error_type": "INVALID_OPCODE",
  "pc": "$C123",
  "opcode": "$FF",
  "message": "Invalid opcode $FF at $C123"
}
```

**AI agent must explicitly clear error:**
```json
// Option 1: Reset machine
{"name": "vice.execution.reset", "arguments": {"mode": "soft"}}

// Option 2: Modify PC and continue
{"name": "vice.registers.set", "arguments": {"register": "PC", "value": "$C124"}}
{"name": "vice.execution.clear_error"}
{"name": "vice.execution.run"}
```

## Tight Coupling Architecture

### Single Source of Truth

**VICE emulation state is the ONLY source of truth.**

**NO separate MCP state:**
- No cached register values
- No cached memory snapshots
- No separate breakpoint storage (MCP reads VICE's internal breakpoint list)
- No execution state duplication

**Every MCP tool reads directly from VICE:**
```c
// Example: Get registers MCP tool implementation
static json_t* mcp_tool_registers_get(json_t* params) {
    // Read directly from VICE's internal structures
    uint16_t pc = (uint16_t)reg_pc_read();
    uint8_t a = (uint8_t)reg_a_read();
    uint8_t x = (uint8_t)reg_x_read();
    uint8_t y = (uint8_t)reg_y_read();
    uint8_t sp = (uint8_t)reg_sp_read();
    uint8_t flags = (uint8_t)reg_p_read();

    // Build JSON response
    json_t* result = json_object();
    json_object_set_new(result, "PC", json_integer(pc));
    json_object_set_new(result, "A", json_integer(a));
    // ... etc
    return result;
}
```

### Zero-Copy Reads

**Memory reads don't copy data unnecessarily:**
```c
// BAD: Copy entire memory range to buffer
uint8_t buffer[65536];
memcpy(buffer, main_memory, 65536);
// ... then format as hex ...

// GOOD: Direct access to memory
uint8_t* mem_ptr = main_memory + start_address;
for (int i = 0; i < length; i++) {
    // Format directly from memory
    snprintf(hex_buf, sizeof(hex_buf), "%02X", mem_ptr[i]);
}
```

### Direct Write Operations

**Memory writes update VICE state directly:**
```c
static void mcp_tool_memory_write(uint16_t address, uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        mem_store((uint16_t)(address + i), data[i]);
    }
}
```

### Event-Driven Notifications

**VICE notifies MCP server of state changes:**
```c
// In VICE's breakpoint handler
void machine_handle_breakpoint(uint16_t pc, uint32_t breakpoint_id) {
    // ... existing breakpoint handling ...

    // Notify MCP server
    mcp_notify_breakpoint(pc, breakpoint_id);
}

// MCP server immediately sends SSE to connected clients
void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id) {
    json_t* event = json_object();
    json_object_set_new(event, "event", json_string("breakpoint"));
    json_object_set_new(event, "pc", json_integer(pc));
    json_object_set_new(event, "breakpoint_id", json_integer(bp_id));

    mcp_sse_send_event(event);
}
```

### No State Synchronization

**Because MCP has no separate state, there's no synchronization:**
- No "sync" commands needed
- No risk of MCP seeing stale data
- No cache invalidation logic
- No state drift bugs

**Contrast with external MCP server (ViceMCP):**
- External server must query VICE via binary monitor
- Binary monitor has reliability issues (checkpoints fail when paused)
- No real-time notifications (must poll for changes)
- State can drift if VICE modified outside monitor

## Machine Configuration Control

### AI Agent Autonomy

**AI agents have full control over machine configuration:**
- Video standard (NTSC/PAL)
- SID model (6581/8580)
- Drive types and count (1541, 1571, 1581)
- Memory expansions (REU, GeoRAM, etc.)
- Cartridge types
- Joystick port mappings
- Keyboard layout

**Purpose:** Enable AI to test code across different hardware configurations.

### Configuration Discovery

**Query available configuration options:**
```json
{
  "name": "vice.config.list",
  "arguments": {
    "category": "video"  // or "drives", "memory", "sid", "all"
  }
}
```

**Response:**
```json
{
  "category": "video",
  "resources": [
    {
      "name": "MachineVideoStandard",
      "type": "integer",
      "current_value": 1,
      "possible_values": [
        {"value": 1, "name": "PAL"},
        {"value": 2, "name": "NTSC"},
        {"value": 3, "name": "NTSC-OLD"}
      ],
      "description": "Video standard used by emulated machine"
    },
    {
      "name": "VICIIBorderMode",
      "type": "integer",
      "current_value": 1,
      "possible_values": [
        {"value": 0, "name": "Normal"},
        {"value": 1, "name": "Full"},
        {"value": 2, "name": "Debug"}
      ]
    }
  ]
}
```

### Setting Resources

**Individual resource:**
```json
{
  "name": "vice.config.set",
  "arguments": {
    "resource": "MachineVideoStandard",
    "value": 2  // NTSC
  }
}
```

**Batch resource changes:**
```json
{
  "name": "vice.config.set_batch",
  "arguments": {
    "resources": [
      {"name": "MachineVideoStandard", "value": 2},
      {"name": "SidModel", "value": 1},  // 8580
      {"name": "Drive8Type", "value": 1541}
    ]
  }
}
```

### Machine Profiles

**Create reusable configuration profiles:**
```json
{
  "name": "vice.machine.profile.create",
  "arguments": {
    "profile_name": "c64_ntsc_8580_testing",
    "description": "NTSC C64 with 8580 SID for audio testing",
    "resources": [
      {"name": "MachineVideoStandard", "value": 2},
      {"name": "SidModel", "value": 1},
      {"name": "Drive8Type", "value": 1541},
      {"name": "Drive9Type", "value": 0}  // no drive 9
    ]
  }
}
```

**Apply profile:**
```json
{
  "name": "vice.machine.profile.apply",
  "arguments": {
    "profile_name": "c64_ntsc_8580_testing",
    "reset_after": true  // reset machine after applying
  }
}
```

**List saved profiles:**
```json
{"name": "vice.machine.profile.list"}
```

### Testing Workflow Example

**AI agent tests sprite code across configurations:**

```
1. Load program: vice.files.autostart("sprites.prg")
2. Apply PAL profile: vice.machine.profile.apply("c64_pal_6581")
3. Set breakpoint: vice.breakpoints.raster(line=100)
4. Run and verify: vice.execution.run() + check sprite positions
5. Apply NTSC profile: vice.machine.profile.apply("c64_ntsc_6581")
6. Run and verify: vice.execution.run() + check sprite positions
7. Compare results: Detect PAL/NTSC timing differences
```

## Implementation Strategy

### Phase 1: Core Infrastructure (Weeks 1-2)

**Goal:** Basic MCP server running in VICE with minimal toolset.

**Tasks:**
1. Add HTTP server library to VICE (libmicrohttpd or similar)
2. Implement Streamable HTTP transport (POST + SSE)
3. Create `src/mcp/` directory structure:
   - `mcp_server.c/h` - HTTP server and routing
   - `mcp_transport.c/h` - Streamable HTTP implementation
   - `mcp_tools.c/h` - Tool registration and dispatch
   - `mcp_json.c/h` - JSON parsing (use cJSON or jansson)
4. Implement basic tools:
   - `vice.ping`
   - `vice.execution.run/pause/step`
   - `vice.registers.get/set`
   - `vice.memory.read/write`
5. Test with simple MCP client (curl or custom test script)

**Success Criteria:**
- VICE starts with MCP server on port 6510
- Can connect and call basic tools
- Read/write memory and registers
- Pause/resume execution

### Phase 2: Breakpoints & State Management (Weeks 3-4)

**Goal:** Full breakpoint support including state-based breakpoints.

**Tasks:**
1. Implement traditional breakpoints:
   - `vice.breakpoints.set/list/delete/toggle`
   - `vice.breakpoints.condition`
2. Add SSE notifications for breakpoint hits
3. Implement state-based breakpoints:
   - `vice.breakpoints.raster`
   - `vice.breakpoints.sprite_collision`
   - `vice.breakpoints.memory.watch`
4. Add execution state machine
5. Test with complex breakpoint scenarios

**Success Criteria:**
- Set PC-based breakpoints with conditions
- Break at specific raster lines
- Break on sprite collisions
- Receive SSE notifications when breakpoints hit

### Phase 3: Chip-Specific Toolsets (Weeks 5-6)

**Goal:** Expose chip-specific functionality (VIC-II, SID, CIA).

**Tasks:**
1. Implement capability discovery:
   - `vice.machine.capabilities`
   - Dynamic tool availability based on machine
2. Add VIC-II tools:
   - `vice.vic2.registers.get`
   - `vice.vic2.sprites.get/set`
   - `vice.vic2.raster.get`
   - `vice.vic2.screen.capture`
3. Add SID tools:
   - `vice.sid.registers.get`
   - `vice.sid.voices.get`
4. Add CIA tools:
   - `vice.cia1.registers.get`
   - `vice.cia2.registers.get`
5. Test on multiple machines (C64, VIC-20, Plus/4)

**Success Criteria:**
- Query machine capabilities
- VIC-II tools work on C64, return error on VIC-20
- Can read sprite positions, SID registers, CIA timers
- Screen capture returns PNG data

### Phase 4: Annotation & Instrumentation (Week 7)

**Goal:** Support source code annotations and frame tracing.

**Tasks:**
1. Extend symbol file parser to extract `@mcp-*` annotations
2. Build annotation index on symbol load
3. Implement annotation tools:
   - `vice.annotations.list/get/search`
4. Add instrumentation API:
   - `vice.trace.frame.start/stop/get`
   - `vice.trace.cycles.get`
5. Test with annotated assembly code

**Success Criteria:**
- Load symbols with annotations
- Query annotations by address
- Capture frame traces with all VIC-II writes
- Measure cycle counts between checkpoints

### Phase 5: Machine Configuration (Week 8)

**Goal:** Allow AI agents to modify machine configuration.

**Tasks:**
1. Expose VICE resources via MCP:
   - `vice.config.get/set/list`
2. Add profile management:
   - `vice.machine.profile.create/apply/list/delete`
3. Test configuration changes (PAL↔NTSC, SID models, drive types)

**Success Criteria:**
- Query available resources
- Change video standard and verify timing changes
- Save and apply machine profiles
- AI agent can test code on multiple configurations

### Phase 6: Polish & Documentation (Week 9-10)

**Goal:** Production-ready implementation with full documentation.

**Tasks:**
1. Add comprehensive error handling
2. Optimize zero-copy memory access
3. Add logging and debug output (to VICE log, not stderr)
4. Write user documentation (how to enable MCP server)
5. Write developer documentation (how to add new tools)
6. Create example AI agent scripts
7. Generate patches for SourceForge submission

**Success Criteria:**
- No memory leaks
- Clean error messages for all failure cases
- Documentation covers all tools
- Example scripts demonstrate common use cases
- Patches ready for SourceForge submission

**Note on Contribution Process:**
VICE uses SVN on SourceForge for development. The GitHub mirror (VICE-Team/svn-mirror) is **read-only** and does not accept pull requests. To contribute this MCP server implementation:

1. Develop in Git fork for convenience (modern tooling, branching, etc.)
2. Generate patch files from Git commits using `git format-patch`
3. Submit patches through SourceForge's [patch tracking system](https://sourceforge.net/p/vice-emu/patches/)
4. Engage with VICE developers on the mailing list for review

Alternative approach:
- Check out the SVN repository: `svn co https://svn.code.sf.net/p/vice-emu/code/trunk/ vice`
- Create an SVN branch: `branches/barryw/mcp-server`
- Tag milestones: `tags/barryw/mcp-server-v1`
- Email mailing list with tag path for integration

## Testing Strategy

### Unit Tests

**Test MCP tool logic in isolation:**
- Mock VICE internal state
- Test JSON parsing and generation
- Test error handling
- Test tool parameter validation

### Integration Tests

**Test MCP server with real VICE:**
- Launch VICE with MCP enabled
- Connect MCP client
- Execute tool sequences
- Verify emulation state changes

### Cross-Machine Tests

**Verify machine-agnostic abstraction:**
- Test same MCP client code on C64, VIC-20, Plus/4
- Verify capability discovery works correctly
- Verify chip-specific tools only available on correct machines

### Performance Tests

**Measure overhead:**
- Memory read/write performance vs direct access
- SSE notification latency
- Frame tracing overhead
- Breakpoint hit latency

### Compatibility Tests

**Test with multiple MCP clients:**
- Claude Desktop
- Custom Python client
- Custom JavaScript client
- Verify Streamable HTTP transport compliance

## Security Considerations

### Network Security

**Port 6510 is localhost-only by default:**
```c
// Default: bind to 127.0.0.1 only
#define MCP_DEFAULT_HOST "127.0.0.1"
#define MCP_DEFAULT_PORT 6510
```

**Optional remote access via config:**
```ini
[MCP]
Enabled=1
Host=0.0.0.0  ; Allow remote connections
Port=6510
AllowedClients=192.168.1.0/24  ; IP whitelist
```

### Input Validation

**All MCP tool inputs must be validated:**
- Address ranges within valid memory
- Register names are valid for current machine
- File paths are within allowed directories (no directory traversal)
- Hex strings parse correctly
- Resource values are within valid ranges

### Resource Limits

**Prevent resource exhaustion:**
- Maximum SSE client connections (default: 10)
- Maximum trace buffer size (default: 10MB)
- Maximum annotation index size (default: 100k entries)
- Request rate limiting (default: 1000 req/sec per client)

### Safe State Modification

**Some operations should require confirmation:**
- Destructive operations (reset machine, detach disks)
- Large memory writes (> 4KB)
- Changing machine configuration

**Confirmation pattern:**
```json
// First call returns confirmation token
{
  "name": "vice.execution.reset",
  "arguments": {"mode": "hard"}
}
// Response: {"requires_confirmation": true, "token": "abc123"}

// Second call with token executes
{
  "name": "vice.execution.reset",
  "arguments": {"mode": "hard", "confirmation_token": "abc123"}
}
```

## Future Enhancements

### Remote Debugging

**Multiple AI agents debugging simultaneously:**
- Share execution state via SSE broadcast
- Coordinate breakpoints
- Collaborative annotation editing

### Time-Travel Debugging

**Reverse execution and state replay:**
- Checkpoint emulation state periodically
- Rewind to previous state
- Step backwards through execution
- Compare states across runs

### Advanced Instrumentation

**Hardware-level profiling:**
- Cycle-accurate performance profiling
- Raster time visualization
- Memory bandwidth analysis
- IRQ latency tracking

### AI-Driven Optimization

**Automated code optimization:**
- AI identifies performance bottlenecks
- Suggests optimizations (loop unrolling, table lookups)
- Tests optimizations across configurations
- Verifies correctness with property-based testing

### Integration with Modern Dev Tools

**VS Code extension:**
- Inline annotation editing
- Breakpoint visualization
- Live sprite preview
- Raster position indicator

**Git integration:**
- Track annotations in source control
- Diff annotations across branches
- Annotation coverage metrics

## Conclusion

This embedded MCP server architecture transforms VICE from a standalone emulator into an AI-friendly development platform. By providing zero-copy state access, machine-agnostic abstractions, state-based breakpoints, and source code annotations, we enable AI agents to debug 6502/6510 assembly code with unprecedented precision.

The phased implementation strategy ensures steady progress with testable milestones. The machine-agnostic design ensures compatibility across all VICE-supported platforms (C64, C128, VIC-20, PET, Plus/4, CBM-II). The tight coupling architecture eliminates state drift issues that plague external solutions.

Once merged into VICE upstream, this will be available to the entire retro computing community. AI-assisted debugging will become a standard part of Commodore 8-bit development workflows, bringing modern AI tools to classic computing platforms.

**Next Steps:**
1. Review this design document with VICE community
2. Set up development environment and build system
3. Begin Phase 1 implementation
4. Iterate based on feedback from early testing
