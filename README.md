# VICE MCP - AI Meets the Commodore 64

**An MCP server embedded directly inside VICE**, giving AI agents and modern tools
full programmatic control over the world's most iconic 8-bit computer.

> Load a disk image. Set breakpoints. Inspect sprites. Read SID registers.
> Type on the keyboard. Take screenshots. Step through 6502 code.
> All through a clean JSON-RPC API that any MCP client can speak.

This is [VICE](https://vice-emu.sourceforge.io/) — the legendary Commodore emulator —
with a [Model Context Protocol](https://modelcontextprotocol.io/) server built into its core.
Not bolted on. Not a wrapper. **~17,000 lines of C woven into the emulator itself.**

## What Can You Do With This?

### For AI Agents
Point any MCP-compatible client — Claude Desktop, Cursor, your own agent — at
`localhost:6510` and you have a fully controllable Commodore 64. Your agent can:

- **Load and run software** — autostart PRGs and disk images
- **Debug 6502 code** — breakpoints, watchpoints, conditional breaks, single-stepping
- **Inspect everything** — CPU registers, memory banks, VIC-II graphics, SID audio, CIA timers
- **See what's on screen** — take screenshots, read sprite bitmaps as ASCII art
- **Interact like a human** — type text, press keys, move joysticks
- **Measure performance** — cycle-accurate stopwatch, execution tracing, interrupt logging
- **Save and restore state** — full snapshot management with metadata

### For C64 Developers
If you write code for the Commodore 64, this gives you a **modern debugging workflow**
without leaving your editor:

- Set breakpoints from your IDE while your program runs
- Load KickAssembler or VICE symbol files and debug by label name
- Search memory for byte patterns with wildcard support
- Compare memory regions against saved snapshots to find what changed
- Trace execution with PC-range filtering to focus on your code
- Log interrupts to understand IRQ/NMI timing
- Group breakpoints and toggle them as a set

### For Researchers & Educators
- Automate ROM analysis and reverse engineering
- Build interactive tutorials that control a live C64
- Capture screen states for documentation
- Replay and analyze historical software

## 63 Tools Across 14 Categories

Every tool follows MCP conventions with full JSON Schema validation, meaningful errors,
and consistent parameter naming.

| Category | Tools | What They Do |
|---|---|---|
| **Execution** | `vice.execution.run` `vice.execution.pause` `vice.execution.step` `vice.run_until` | Control the CPU — resume, halt, single-step, run to address or cycle count |
| **Registers** | `vice.registers.get` `vice.registers.set` | Read/write all 6502 registers (A, X, Y, SP, PC, status flags) |
| **Memory** | `vice.memory.read` `vice.memory.write` `vice.memory.banks` `vice.memory.search` `vice.memory.fill` `vice.memory.compare` | Full memory access with bank selection, pattern search with wildcards |
| **Checkpoints** | `vice.checkpoint.add` `vice.checkpoint.delete` `vice.checkpoint.list` `vice.checkpoint.toggle` `vice.checkpoint.set_condition` `vice.checkpoint.set_ignore_count` `vice.checkpoint.group.*` `vice.checkpoint.set_auto_snapshot` `vice.checkpoint.clear_auto_snapshot` | Breakpoints, watchpoints, tracepoints — with conditions, groups, and auto-snapshots |
| **Sprites** | `vice.sprite.get` `vice.sprite.set` `vice.sprite.inspect` | Read/write sprite state, ASCII art bitmap visualization |
| **VIC-II** | `vice.vicii.get_state` `vice.vicii.set_state` | Full access to the C64's video chip — raster, colors, scroll, bank |
| **SID** | `vice.sid.get_state` `vice.sid.set_state` | The legendary sound chip — voices, filters, ADSR, waveforms |
| **CIA** | `vice.cia.get_state` `vice.cia.set_state` | Timer and I/O chip state — both CIA1 and CIA2 |
| **Disk** | `vice.disk.attach` `vice.disk.detach` `vice.disk.list` `vice.disk.read_sector` | Mount D64/D71/D81 images, browse directories, read raw sectors |
| **Machine** | `vice.machine.reset` `vice.machine.config.get` `vice.machine.config.set` `vice.autostart` | Hard/soft reset, resource control (warp, speed, model), program loading |
| **Display** | `vice.display.screenshot` `vice.display.get_dimensions` | Screen capture to file or base64, display geometry |
| **Input** | `vice.keyboard.type` `vice.keyboard.key_press` `vice.keyboard.key_release` `vice.keyboard.restore` `vice.keyboard.matrix` `vice.joystick.set` | Keyboard and joystick — text typing, individual keys, direct matrix, RESTORE/NMI |
| **Debug** | `vice.disassemble` `vice.symbols.load` `vice.symbols.lookup` `vice.watch.add` `vice.backtrace` `vice.cycles.stopwatch` | Disassembly, symbol files, call stack, cycle-accurate timing |
| **Snapshots** | `vice.snapshot.save` `vice.snapshot.load` `vice.snapshot.list` | Full emulator state save/restore with JSON metadata |
| **Tracing** | `vice.trace.start` `vice.trace.stop` `vice.interrupt.log.start` `vice.interrupt.log.stop` `vice.interrupt.log.read` | Execution recording with PC filtering, IRQ/NMI/BRK event capture |

## Architecture

This isn't a sidecar process or a screen-scraper. The MCP server is compiled directly
into VICE as a first-class subsystem — across **every machine VICE emulates**.

### Supported Machines

| Machine | CPU | Notable Hardware |
|---|---|---|
| **C64 / C64 SC** | 6510 | VIC-II, SID, 2×CIA, Sprites |
| **C128** | 8502/Z80 | VIC-II, VDC 80-col, SID, 2×CIA |
| **SCPU64** | 65816 | SuperCPU accelerator |
| **C64 DTV** | 6510 (extended) | DTV-specific registers |
| **VIC-20** | 6502 | VIC-I video, expansion memory |
| **Plus/4 & C16** | 7501/8501 | TED video+audio chip |
| **PET** | 6502 | CRTC video, PIA/VIA I/O |
| **CBM-II** | 6509 | CRTC, MOS 6526 CIA |

The MCP server **adapts to the running machine automatically**. When an AI agent
calls `vice.machine.config.get`, it receives the actual hardware configuration — which
chips are present, what memory banks exist, valid address ranges, and available
resources. An agent debugging a VIC-20 cartridge gets VIC-I registers; the same
agent debugging a C128 program gets VIC-II *and* the VDC 80-column display.

```
┌─────────────────────────────────────────────────┐
│                   VICE Emulator                  │
│                                                  │
│  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │
│  │   CPU    │  │  Video   │  │    Audio     │  │
│  │ (varies) │  │ (varies) │  │   (varies)   │  │
│  └────┬─────┘  └────┬─────┘  └──────┬───────┘  │
│       │              │               │           │
│       └──────────┬───┴───────────────┘           │
│                  │                                │
│         ┌────────┴────────┐                      │
│         │  MCP Server     │                      │
│         │  (libmcp.a)     │                      │
│         │                 │                      │
│         │  JSON-RPC 2.0   │◄──── POST /mcp ─────┤
│         │  libmicrohttpd  │────► GET /events ───►│
│         │  Trap Dispatch  │                      │
│         └─────────────────┘                      │
│                                                  │
└─────────────────────────────────────────────────┘
        localhost:6510
```

**Key design decisions:**

- **Trap-based dispatch** — HTTP requests are dispatched through VICE's trap mechanism,
  ensuring all tool logic executes on the emulator's main thread. No race conditions,
  no locking surprises.
- **Zero-copy access** — Tools read directly from emulator internals. When you ask for
  VIC-II state, you get the actual register values, not a cached approximation.
- **Machine-aware responses** — Tools report hardware capabilities, chip availability,
  and valid memory ranges for whatever machine is running. The agent always knows
  what it's working with.
- **Monitor integration** — Works alongside VICE's built-in monitor. If the emulator
  is paused in the monitor, MCP requests execute directly without traps.
- **SSE events** — Real-time notifications for breakpoint hits and state changes via
  Server-Sent Events on `/events`.

## Quick Start

### Connect to VICE

Start any VICE machine with the MCP server enabled:

```bash
# C64 (cycle-exact)
x64sc -mcpserver

# C128
x128 -mcpserver

# VIC-20
xvic -mcpserver

# Custom port and host
x64sc -mcpserver -mcpserverport 7000 -mcpserverhost 0.0.0.0
```

The MCP server starts on `localhost:6510` by default.

### Talk to It

```bash
# Ping
curl -s http://localhost:6510/mcp -d '{
  "jsonrpc": "2.0", "id": 1,
  "method": "tools/call",
  "params": { "name": "vice.ping" }
}'

# Read the BASIC ROM entry point
curl -s http://localhost:6510/mcp -d '{
  "jsonrpc": "2.0", "id": 2,
  "method": "tools/call",
  "params": {
    "name": "vice.memory.read",
    "arguments": { "address": "0xA000", "length": 16 }
  }
}'

# Take a screenshot
curl -s http://localhost:6510/mcp -d '{
  "jsonrpc": "2.0", "id": 3,
  "method": "tools/call",
  "params": {
    "name": "vice.display.screenshot",
    "arguments": { "format": "base64" }
  }
}'
```

### Use with Claude Desktop

Add this to your Claude Desktop MCP config:

```json
{
  "mcpServers": {
    "vice": {
      "url": "http://localhost:6510/mcp"
    }
  }
}
```

Then just talk to it: *"Load the game on drive 8 and show me what's on screen."*

## Building from Source

### Prerequisites

| Platform | Install |
|---|---|
| **Debian/Ubuntu** | `apt install build-essential autoconf automake pkg-config libmicrohttpd-dev libgtk-3-dev xa65 flex byacc` |
| **macOS** | `brew install autoconf automake pkg-config libmicrohttpd gtk+3 xa lame` |
| **Windows (MSYS2)** | `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libmicrohttpd mingw-w64-x86_64-gtk3 autoconf automake pkg-config` |

### Build

```bash
cd vice
./autogen.sh
mkdir build && cd build
../configure --enable-mcp-server --enable-gtk3ui
make -j$(nproc)
```

### Verify

```bash
# MCP flags should appear in help output
src/x64sc -help | grep mcp

# Expected:
# -mcpserver          Enable MCP server
# -mcpserverport <port>  Set MCP server port (default: 6510)
# -mcpserverhost <host>  Set MCP server host (default: 127.0.0.1)
```

### Pre-built Binaries

Pre-built binaries are available on the [Releases](https://github.com/barryw/vice-mcp/releases) page.

| Platform | GUI | Headless | Notes |
|---|---|---|---|
| **Linux x86_64** | Yes | Yes | GTK3 UI |
| **macOS arm64** | Yes | Yes | GTK3 UI (Apple Silicon) |
| **Windows x86_64** | No | Yes | Headless only — cross-compiled via MinGW-w64 |

Windows does not include a GUI build. GTK3 cross-compilation for Windows is not
supported by VICE's build system. If you need a Windows GUI, [build from source](#building-from-source)
natively using MSYS2.

## Python Client

A resilient Python client is included with retry logic, connection pooling, and
a convenience method for every tool:

```python
from tools.resilience.vice_mcp_resilient import ViceMCPClient

with ViceMCPClient("http://localhost:6510") as vice:
    # Load a program
    vice.autostart("/path/to/game.prg")

    # Set a breakpoint at the main loop
    vice.checkpoint_add(start_address=0x0810, stop_address=0x0810)

    # Run until it hits
    vice.execution_run()

    # Read the screen
    regs = vice.registers_get()
    screenshot = vice.display_screenshot(format="base64")

    # Inspect a sprite
    art = vice.sprite_inspect(sprite_number=0)
    print(art)
```

### Protocol Test Suite

167 tests across 25 test classes validate every tool, every parameter, and every
error condition:

```bash
# Requires a running VICE instance with MCP enabled
pytest tools/tests/test_mcp_protocol.py -v
```

## Real-World Usage

### sim6502 — Unit Testing for 6502 Assembly

[sim6502](https://github.com/barryw/sim6502) is a unit testing framework for
6502/6510/65C02 assembly that uses VICE MCP as an execution backend. Write tests
in a custom DSL, run them against a live VICE instance with cycle-accurate hardware:

```
suite "sprite collision" {
    load "game.prg"

    test "player hits enemy" {
        jsr setup_sprites
        poke $d015, #$03          ; enable sprites 0 and 1
        poke $d000, #$80          ; sprite 0 x = 128
        poke $d002, #$80          ; sprite 1 x = 128
        jsr main_loop
        assert $d01e & #$03 != 0  ; collision register set
    }
}
```

sim6502 connects over MCP to load programs, set breakpoints, read registers,
compare memory, and snapshot/restore state between tests — bringing modern CI/CD
practices to retro computing development.

### AI Agent Workflows

Any MCP-compatible client can drive VICE directly:

- **Claude Desktop / Cursor** — "Load this disk image, find the main loop, and
  explain what the IRQ handler does"
- **Custom agents** — Automated ROM analysis, regression testing, screenshot capture
- **Research tools** — Systematic exploration of historical software behavior

## Security

The MCP server is **localhost-only by default** (`127.0.0.1`). It has no authentication
and no TLS — it's designed for local development, not network exposure.

If you need remote access, put it behind a reverse proxy with proper auth:

```
nginx/caddy → auth → https → localhost:6510
```

Binding to `0.0.0.0` is supported via `-mcpserverhost` but you're on your own
for security at that point.

## Relationship to Upstream VICE

This is a fork of the [VICE SVN mirror](https://github.com/VICE-Team/svn-mirror).
The MCP server is implemented as a self-contained subsystem in `src/mcp/` — it
touches VICE internals through well-defined interfaces but doesn't modify core
emulation logic.

The `main` branch tracks upstream VICE. The `mcp-server` branch contains all
MCP additions.

The goal is to contribute this work back to the VICE project. The implementation
is structured to export cleanly as unified diffs for SVN submission.

## Tool Reference

<details>
<summary><strong>Click to expand full reference for all 63 tools</strong></summary>

### Execution Control

#### `vice.ping`
Check if VICE is responding. No parameters.

#### `vice.execution.run`
Resume execution. No parameters.

#### `vice.execution.pause`
Pause execution. No parameters.

#### `vice.execution.step`
Step one or more instructions.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `count` | number | | Number of instructions to step |
| `stepOver` | boolean | | Step over subroutines |

#### `vice.run_until`
Run until address or for N cycles with timeout.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | string | | Target address (hex, decimal, or symbol name) |
| `cycles` | number | | Max cycles to run |

---

### Registers

#### `vice.registers.get`
Get all CPU registers (A, X, Y, SP, PC, status flags). No parameters.

#### `vice.registers.set`
Set a CPU register value.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `register` | string | yes | Register name: `PC` `A` `X` `Y` `SP` `N` `V` `B` `D` `I` `Z` `C` |
| `value` | number | yes | Value to set |

---

### Memory

#### `vice.memory.read`
Read a memory range with optional bank selection.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Address: number, hex (`$1000`), or symbol name |
| `size` | number | yes | Bytes to read (1-65535) |
| `bank` | string | | Memory bank name (use `vice.memory.banks` to list) |

#### `vice.memory.write`
Write bytes to memory.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Address: number, hex (`$1000`), or symbol name |
| `data` | number[] | yes | Bytes to write (0-255 each) |

#### `vice.memory.banks`
List available memory banks for the current machine. No parameters.

#### `vice.memory.search`
Search for byte patterns with optional wildcard mask.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | string | yes | Start address |
| `end` | string | yes | End address |
| `pattern` | number[] | yes | Byte pattern, e.g. `[0x4C, 0x00, 0xA0]` |
| `mask` | number[] | | Per-byte mask: `0xFF`=exact, `0x00`=wildcard |
| `max_results` | number | | Max matches (default: 100, max: 10000) |

#### `vice.memory.fill`
Fill a memory range with a repeating byte pattern.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | string | yes | Start address |
| `end` | string | yes | End address (inclusive) |
| `pattern` | number[] | yes | Byte pattern to repeat |

#### `vice.memory.compare`
Compare two memory ranges or compare against a snapshot.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `mode` | string | yes | `ranges` or `snapshot` |
| `range1_start` | string | ranges | Start of first range |
| `range1_end` | string | ranges | End of first range |
| `range2_start` | string | ranges | Start of second range |
| `snapshot_name` | string | snapshot | Snapshot to compare against |
| `start` | string | snapshot | Start address to compare |
| `end` | string | snapshot | End address to compare |
| `max_differences` | number | | Max diffs to return (default: 100) |

---

### Checkpoints & Breakpoints

#### `vice.checkpoint.add`
Add a checkpoint (breakpoint, watchpoint, or tracepoint).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | string | yes | Start address |
| `end` | string | | End address (default = start) |
| `stop` | boolean | | Stop on hit (default: true) |
| `load` | boolean | | Break on memory read (default: false) |
| `store` | boolean | | Break on memory write (default: false) |
| `exec` | boolean | | Break on execution (default: true) |

#### `vice.checkpoint.delete`
Delete a checkpoint.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_num` | number | yes | Checkpoint number |

#### `vice.checkpoint.list`
List all checkpoints. No parameters.

#### `vice.checkpoint.toggle`
Enable or disable a checkpoint.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_num` | number | yes | Checkpoint number |
| `enabled` | boolean | yes | Enable or disable |

#### `vice.checkpoint.set_condition`
Set a condition expression on a checkpoint.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_num` | number | yes | Checkpoint number |
| `condition` | string | yes | Expression, e.g. `A == $42` |

#### `vice.checkpoint.set_ignore_count`
Set how many hits to ignore before stopping.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_num` | number | yes | Checkpoint number |
| `count` | number | yes | Hits to ignore |

#### `vice.checkpoint.group.create`
Create a named checkpoint group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Group name |
| `checkpoint_ids` | number[] | | Initial checkpoint IDs |

#### `vice.checkpoint.group.add`
Add checkpoints to an existing group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `group` | string | yes | Group name |
| `checkpoint_ids` | number[] | yes | Checkpoint IDs to add |

#### `vice.checkpoint.group.toggle`
Enable or disable all checkpoints in a group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `group` | string | yes | Group name |
| `enabled` | boolean | yes | Enable or disable all |

#### `vice.checkpoint.group.list`
List all checkpoint groups. No parameters.

#### `vice.checkpoint.set_auto_snapshot`
Auto-save a snapshot when a checkpoint is hit.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_id` | number | yes | Checkpoint ID |
| `snapshot_prefix` | string | yes | Filename prefix (e.g. `crash_repro`) |
| `max_snapshots` | number | | Ring buffer size (default: 10) |
| `include_disks` | boolean | | Include disk state (default: false) |

#### `vice.checkpoint.clear_auto_snapshot`
Remove auto-snapshot from a checkpoint.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_id` | number | yes | Checkpoint ID |

---

### Sprites (C64/C128/DTV)

#### `vice.sprite.get`
Get sprite state.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `sprite` | number | | Sprite number 0-7 (omit for all) |

#### `vice.sprite.set`
Set sprite properties.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `sprite` | number | yes | Sprite number 0-7 |
| `x` | number | | X position 0-511 |
| `y` | number | | Y position 0-255 |
| `enabled` | boolean | | Enable sprite |
| `multicolor` | boolean | | Multicolor mode |
| `expand_x` | boolean | | Double width |
| `expand_y` | boolean | | Double height |
| `priority_foreground` | boolean | | Draw over background |
| `color` | number | | Sprite color 0-15 |

#### `vice.sprite.inspect`
Visual ASCII art representation of a sprite's bitmap.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `sprite_number` | number | yes | Sprite number 0-7 |
| `format` | string | | `ascii` (default), `binary`, or `png_base64` |

---

### Chip State

#### `vice.vicii.get_state`
Get VIC-II internal state. No parameters.

#### `vice.vicii.set_state`
Set VIC-II registers.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x2E) |

#### `vice.sid.get_state`
Get SID state (voices, filter, ADSR). No parameters.

#### `vice.sid.set_state`
Set SID registers.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x1C) |

#### `vice.cia.get_state`
Get CIA state (timers, ports).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `cia` | number | | CIA number: 1 or 2 (omit for both) |

#### `vice.cia.set_state`
Set CIA registers.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `cia1_registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x0F) |
| `cia2_registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x0F) |

---

### Disk Management

#### `vice.disk.attach`
Attach a disk image to a drive.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |
| `path` | string | yes | Path to disk image (.d64, .g64, etc.) |

#### `vice.disk.detach`
Detach a disk image.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |

#### `vice.disk.list`
List directory contents of an attached disk.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |

#### `vice.disk.read_sector`
Read raw sector data.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |
| `track` | number | yes | Track number (1-42 for D64) |
| `sector` | number | yes | Sector number |

---

### Machine Control

#### `vice.autostart`
Autostart a PRG or disk image.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to .prg, .d64, .g64, etc. |
| `program` | string | | Program name to load from disk |
| `run` | boolean | | Run after loading (default: true) |
| `index` | number | | Program index on disk, 0-based |

#### `vice.machine.reset`
Reset the machine.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `mode` | string | | `soft` (default) or `hard` (power cycle) |
| `run_after` | boolean | | Resume after reset (default: true) |

#### `vice.machine.config.get`
Get machine configuration — chips, memory map, resources. No parameters.

#### `vice.machine.config.set`
Set machine resources.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `resources` | object | yes | Resource name/value pairs, e.g. `{"WarpMode": 1}` |

---

### Display

#### `vice.display.screenshot`
Capture the screen.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | string | | File path to save |
| `format` | string | | `PNG` (default) or `BMP` |
| `return_base64` | boolean | | Return as base64 data URI |

#### `vice.display.get_dimensions`
Get display dimensions. No parameters.

---

### Input

#### `vice.keyboard.type`
Type text with automatic PETSCII conversion.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `text` | string | yes | Text to type (`\n` for Return) |
| `petscii_upper` | boolean | | Uppercase mapping (default: true) |

#### `vice.keyboard.key_press`
Press a key.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | yes | Key name or single char |
| `modifiers` | string[] | | `shift`, `control`, `alt`, `meta`, etc. |
| `hold_frames` | number | | Hold duration in frames (1-300) |
| `hold_ms` | number | | Hold duration in ms (1-5000) |

#### `vice.keyboard.key_release`
Release a key.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | yes | Key name or single char |
| `modifiers` | string[] | | Modifiers to release |

#### `vice.keyboard.restore`
Press/release the RESTORE key (triggers NMI).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `pressed` | boolean | | true=press, false=release (default: true) |

#### `vice.keyboard.matrix`
Direct keyboard matrix control for games.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | | Key name: `A`-`Z`, `0`-`9`, `SPACE`, `RETURN`, etc. |
| `row` | number | | Matrix row 0-7 (alternative to key) |
| `col` | number | | Matrix column 0-7 (alternative to key) |
| `pressed` | boolean | | Key state (default: true) |
| `hold_frames` | number | | Hold duration in frames |
| `hold_ms` | number | | Hold duration in ms |

#### `vice.joystick.set`
Set joystick state.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `port` | number | | Port 1 or 2 (default: 1) |
| `direction` | string | | `up`, `down`, `left`, `right`, `center` |
| `fire` | boolean | | Fire button (default: false) |

---

### Advanced Debugging

#### `vice.disassemble`
Disassemble memory to 6502 instructions.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Start address |
| `count` | number | | Instructions to disassemble (default: 10, max: 100) |
| `show_symbols` | boolean | | Show symbol names (default: true) |

#### `vice.symbols.load`
Load a symbol/label file.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to .sym or .lbl file |
| `format` | string | | `auto`, `kickasm`, `vice`, or `simple` |

#### `vice.symbols.lookup`
Look up a symbol by name or address.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | | Symbol name (returns address) |
| `address` | number | | Address (returns symbol name) |

#### `vice.watch.add`
Add a memory watchpoint.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Address to watch |
| `size` | number | | Bytes to watch (default: 1) |
| `type` | string | | `read`, `write`, or `both` (default: `write`) |
| `condition` | string | | Condition, e.g. `A == $42` |

#### `vice.backtrace`
Show call stack from JSR return addresses.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `depth` | number | | Max frames (default: 16, max: 64) |

#### `vice.cycles.stopwatch`
Measure elapsed CPU cycles.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `action` | string | yes | `reset`, `read`, or `reset_and_read` |

---

### Snapshots

#### `vice.snapshot.save`
Save complete emulator state.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Snapshot name (alphanumeric, `_`, `-`) |
| `description` | string | | What this snapshot captures |
| `include_roms` | boolean | | Include ROMs (default: false) |
| `include_disks` | boolean | | Include disk state (default: false) |

#### `vice.snapshot.load`
Restore emulator state from a snapshot.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Snapshot name |

#### `vice.snapshot.list`
List all snapshots with metadata. No parameters.

---

### Execution Tracing

#### `vice.trace.start`
Start recording executed instructions.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `output_file` | string | yes | Path to output file |
| `pc_filter_start` | number | | Filter start address (default: 0) |
| `pc_filter_end` | number | | Filter end address (default: 65535) |
| `max_instructions` | number | | Max to record (default: 10000) |
| `include_registers` | boolean | | Include register state (default: false) |

#### `vice.trace.stop`
Stop tracing and get statistics.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `trace_id` | string | yes | Trace ID from `vice.trace.start` |

---

### Interrupt Logging

#### `vice.interrupt.log.start`
Start logging IRQ, NMI, and BRK events.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `types` | string[] | | Filter: `irq`, `nmi`, `brk` (default: all) |
| `max_entries` | number | | Max entries (default: 1000, max: 10000) |

#### `vice.interrupt.log.stop`
Stop logging and retrieve all entries.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `log_id` | string | yes | Log ID from `vice.interrupt.log.start` |

#### `vice.interrupt.log.read`
Read entries without stopping the log.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `log_id` | string | yes | Log ID |
| `since_index` | number | | Return entries from this index onwards |

</details>

## Project Status

This is active, working software. The MCP server compiles and runs on Linux, macOS,
and Windows. All 63 tools are implemented and tested. CI produces binaries for all
three platforms on every push.

**What's solid:**
- Full tool suite — execution, memory, breakpoints, sprites, chip state, disk, input, debugging
- Machine-aware responses across all VICE-emulated platforms
- Python client with retry logic and full test coverage
- Cross-platform builds: Linux x86_64 (GUI + headless), macOS arm64 (GUI + headless), Windows x86_64 (headless)
- Automated CI/CD pipeline with binary releases

**What's in progress:**
- SSE event streaming (infrastructure present, limited by libmicrohttpd)
- Execution tracing and interrupt logging hooks into VICE CPU core

## Contributing

> *"Cross over, children. All are welcome. All welcome."*
> — Tangina Barrons, speaking to contributors about this repo

This project bridges two communities that don't often overlap: retro computing
and modern AI tooling. Contributions from either world (or both) are welcome.

**Areas where help would be especially appreciated:**

- **VICE internals** — Hooking execution tracing and interrupt logging into the CPU core
- **WebSocket transport** — Upgrading from SSE to full bidirectional streaming
- **Additional machine support** — Testing and tuning tools for PET, CBM-II, Plus/4
- **Client libraries** — TypeScript, Rust, Go clients
- **Documentation** — Tutorials, example workflows, video demos
- **Testing** — Running the protocol test suite against edge cases

The MCP server is entirely contained in `vice/src/mcp/`. Start there.

## License

VICE is released under the GNU General Public License v2. The MCP server additions
follow the same license.

## Acknowledgments

- The [VICE Team](https://vice-emu.sourceforge.io/) for 30+ years of the best
  8-bit emulator ever written
- [Anthropic](https://anthropic.com/) for the Model Context Protocol specification
- The Commodore 64 community — still going strong after four decades
