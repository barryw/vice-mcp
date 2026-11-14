# VICE MCP - AI Meets the Commodore 64

_**VICE MCP** is a Walker Heavy Industries project._

**An MCP server embedded directly inside VICE**, giving AI agents and modern tools
full programmatic control over the world's most iconic 8-bit computer.

> Load a disk image. Set breakpoints. Inspect sprites. Read SID registers.
> Type on the keyboard. Take screenshots. Step through 6502 code.
> All through a clean JSON-RPC API that any MCP client can speak.

This is [VICE](https://vice-emu.sourceforge.io/) — the legendary Commodore emulator —
with a [Model Context Protocol](https://modelcontextprotocol.io/) server built into its core.
Not bolted on. Not a wrapper. **~15,000 lines of C woven into the emulator itself**
(plus ~11,000 lines of C unit tests).

## What Can You Do With This?

### For AI Agents
Point any MCP-compatible client - Claude Desktop, Cursor, your own agent - at
`http://127.0.0.1:6520/mcp` and you have a fully controllable Commodore 64.
Your agent can:

- **Load and run software** — autostart PRGs and disk images
- **Debug 6502 code** — breakpoints, watchpoints, conditional breaks, single-stepping
- **Inspect everything** — CPU registers, memory banks, VIC-II graphics, SID audio, CIA timers
- **See what's on screen** — take screenshots, read sprite bitmaps as ASCII art
- **Interact like a human** — type text, press keys, move joysticks
- **Measure performance** — cycle-accurate stopwatch
- **Save and restore state** — full snapshot management with metadata

### For C64 Developers
If you write code for the Commodore 64, this gives you a **modern debugging workflow**
without leaving your editor:

- Set breakpoints from your IDE while your program runs
- Load KickAssembler or VICE symbol files and debug by label name
- Search memory for byte patterns with wildcard support
- Compare memory regions against saved snapshots to find what changed
- Group breakpoints and toggle them as a set

### For Researchers & Educators
- Automate ROM analysis and reverse engineering
- Build interactive tutorials that control a live C64
- Capture screen states for documentation
- Replay and analyze historical software

## 62 Tools Across 14 Categories

Every tool publishes a JSON Schema for its arguments through `tools/list` and
returns JSON-RPC errors with meaningful messages. The schemas describe the
arguments; the server does not validate calls against them, each tool checks
its own parameters.

`tools/list` publishes each name with `.` replaced by `_` (`vice.memory.read`
is listed as `vice_memory_read`); both spellings are accepted when calling. It
also lists the four protocol methods (`initialize`,
`notifications/initialized`, `tools/list`, `tools/call`), so clients see 70
entries.

| Category | Tools | What They Do |
|---|---|---|
| **Execution** | `vice.ping` `vice.execution.run` `vice.execution.pause` `vice.execution.step` `vice.frame.advance` `vice.run_until` | Check liveness and run state; control the CPU — resume, halt, single-step, advance whole frames, run to an address or for a number of cycles |
| **Registers** | `vice.registers.get` `vice.registers.set` | Read/write all 6502 registers (A, X, Y, SP, PC, status flags) |
| **Memory** | `vice.memory.read` `vice.memory.write` `vice.memory.banks` `vice.memory.search` `vice.memory.fill` `vice.memory.compare` | Full memory access with bank selection, pattern search with wildcards |
| **Checkpoints** | `vice.checkpoint.add` `vice.checkpoint.delete` `vice.checkpoint.list` `vice.checkpoint.toggle` `vice.checkpoint.set_condition` `vice.checkpoint.set_ignore_count` `vice.checkpoint.group.*` | Breakpoints, watchpoints, tracepoints — with conditions and groups |
| **Coverage** | `vice.coverage.start` `vice.coverage.stop` `vice.coverage.read` | Every address the CPU executes, as a 64K map, without the per-instruction cost of checkpoints |
| **Sprites** | `vice.sprite.get` `vice.sprite.set` `vice.sprite.inspect` | Read/write sprite state, ASCII art bitmap visualization |
| **VIC-II** | `vice.vicii.get_state` `vice.vicii.set_state` | VIC-II on C64/C128/DTV/SCPU64 — raster, video mode, colors, scroll, memory pointers, IRQ, collisions |
| **SID** | `vice.sid.get_state` `vice.sid.set_state` | The legendary sound chip — voices, filters, ADSR, waveforms |
| **CIA** | `vice.cia.get_state` `vice.cia.set_state` | Timer and I/O chip state — both CIA1 and CIA2 |
| **Disk** | `vice.disk.attach` `vice.disk.detach` `vice.disk.list` `vice.disk.read_sector` | Mount D64/D71/D81 images, browse directories, read raw sectors |
| **Machine** | `vice.machine.reset` `vice.machine.config.get` `vice.machine.config.set` `vice.autostart` | Hard/soft reset, integer resource control (warp, speed, model, control-port devices), program loading |
| **Display** | `vice.display.screenshot` `vice.display.get_dimensions` | Screen capture to file or base64, display geometry |
| **Input** | `vice.keyboard.type` `vice.keyboard.petscii` `vice.keyboard.key_press` `vice.keyboard.key_release` `vice.keyboard.restore` `vice.keyboard.matrix` `vice.keyboard.chord` `vice.joystick.set` `vice.joystick.tap` `vice.paddle.set` | Keyboard, joystick, and paddle input — text typing, individual keys, direct matrix, RESTORE/NMI |
| **Debug** | `vice.disassemble` `vice.symbols.load` `vice.symbols.lookup` `vice.watch.add` `vice.backtrace` `vice.cycles.stopwatch` | Disassembly, symbol files, call stack, cycle-accurate timing |
| **Snapshots** | `vice.snapshot.save` `vice.snapshot.load` `vice.snapshot.list` `vice.snapshot.delete` | Full emulator state save/restore/replace/delete with JSON metadata |

## Architecture

This isn't a sidecar process or a screen-scraper. The MCP server is compiled directly
into VICE as a first-class subsystem — in **every VICE emulator except VSID**.

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
| **CBM-II** | 6509 | CRTC (6x0/7x0) or VIC-II (5x0), SID, 6526 CIA, 6551 ACIA |

The MCP server **adapts to the running machine**. `vice.machine.config.get`
reports which chips are present (name, model, register address range), which
common chips are absent, the memory map, and the settable resources. The
chip-state tools (`vice.vicii.*`, `vice.sprite.*`, `vice.cia.*`) work on the C64
family (C64, C64SC, SCPU64, C128, C64DTV) and return error -32000 elsewhere. On
a VIC-20 an agent learns it has a VIC at `$9000-$900F` and reads it with
`vice.memory.read`; on a C128 it gets the VIC-II tools plus VDC RAM through
`vice.memory.read` with bank `vdc`. Screenshots on x128 capture the VIC-II
display unless `display: "vdc"` asks for the VDC.

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
│         │  JSON-RPC 2.0   │<---- POST /mcp -----│
│         │  libmicrohttpd  │---- GET /events 501>│
│         │  Trap Dispatch  │                      │
│         └─────────────────┘                      │
│                                                  │
└─────────────────────────────────────────────────┘
        127.0.0.1:6520 by default
```

**Key design decisions:**

- **Trap-based dispatch** — while the machine runs, HTTP requests are dispatched
  through VICE's trap mechanism, so tool logic executes on the emulator thread.
  While it is paused or held, they run under VICE's mainlock. One call runs at a
  time; `vice.ping` and `vice.execution.pause` skip the queue.
- **Zero-copy access** — Tools read directly from emulator internals. When you ask for
  VIC-II state, you get the actual register values, not a cached approximation.
- **Machine-aware responses** — Tools report hardware capabilities, chip availability,
  and valid memory ranges for whatever machine is running. The agent always knows
  what it's working with.
- **Monitor integration** — Works alongside VICE's built-in monitor. If the emulator
  is paused in the monitor, MCP requests execute directly without traps.
- **No event streaming yet** — `GET /mcp` returns `405 Method Not Allowed` (the
  MCP answer for "no SSE stream") and the reserved `GET /events` returns
  `501 Not Implemented`. Poll state through `POST /mcp`.

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

# Listen on all network interfaces, port 7000
x64sc -mcpserver -mcpserverhost 0.0.0.0 -mcpserverport 7000
```

The MCP server starts on `127.0.0.1:6520` by default (VICE's own monitor
servers default to 6502 and 6510). If the port cannot be bound, VICE logs why
and exits rather than run without the server. MCP clients connect to:

```text
http://127.0.0.1:6520/mcp
```

`0.0.0.0` is a bind address, not a client address. It means "listen on every
interface". A client on the same Mac still connects to `127.0.0.1`; a client on
another machine connects to the Mac's LAN IP address, for example
`http://192.168.1.42:6520/mcp`.

### Connection Recipes

| Use case | Start VICE with | Client URL | Auth header |
|---|---|---|---|
| Same Mac, default | `x64sc -mcpserver` | `http://127.0.0.1:6520/mcp` | None |
| Same Mac, custom port | `x64sc -mcpserver -mcpserverport 7000` | `http://127.0.0.1:7000/mcp` | None |
| LAN access, trusted network | `x64sc -mcpserver -mcpserverhost 0.0.0.0` | `http://<mac-lan-ip>:6520/mcp` | None |
| LAN access with bearer token | `x64sc -mcpserver -mcpserverhost 0.0.0.0 -mcpservertoken secret` | `http://<mac-lan-ip>:6520/mcp` | `Authorization: Bearer secret` |
| Browser app with CORS | `x64sc -mcpserver -mcpservercorsorigin http://localhost:3000 -mcpservertoken secret` | `http://127.0.0.1:6520/mcp` | `Authorization: Bearer secret` |

Token rules are intentionally simple:

- No token configured: non-browser MCP clients can connect without an
  `Authorization` header.
- Token configured: every MCP request must include
  `Authorization: Bearer <token>`.
- CORS configured: a token is required. Wildcard CORS (`*`) is rejected.
- Binding to `0.0.0.0` without a token is allowed for backwards compatibility,
  but VICE logs a warning because remote clients can control the emulator.

### Talk to It

All HTTP requests go to `/mcp`, must use `Content-Type: application/json`, and
should send `Accept: application/json`.

```bash
# Direct JSON-RPC call
curl -sS http://127.0.0.1:6520/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json' \
  --data '{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "vice.ping"
}'

# Standard MCP tools/call form, used by Claude Code and other MCP clients
curl -sS http://127.0.0.1:6520/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json' \
  --data '{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": { "name": "vice_ping", "arguments": {} }
}'

# Read the BASIC ROM start vectors at $A000
curl -sS http://127.0.0.1:6520/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json' \
  --data '{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "vice_memory_read",
    "arguments": { "address": "0xA000", "size": 16, "encoding": "hex" }
  }
}'
```

When a token is configured, add the bearer header to every request:

```bash
curl -sS http://127.0.0.1:6520/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json' \
  -H 'Authorization: Bearer secret' \
  --data '{"jsonrpc":"2.0","id":1,"method":"vice.ping"}'
```

### Use with Claude Code

```bash
claude mcp add --transport http vice http://127.0.0.1:6520/mcp

# With a token
claude mcp add --transport http vice http://127.0.0.1:6520/mcp \
  --header "Authorization: Bearer secret"
```

### Use with Claude Desktop

`claude_desktop_config.json` launches local (stdio) servers. Bridge to the
HTTP endpoint with [`mcp-remote`](https://www.npmjs.com/package/mcp-remote):

```json
{
  "mcpServers": {
    "vice": {
      "command": "npx",
      "args": ["-y", "mcp-remote", "http://127.0.0.1:6520/mcp"]
    }
  }
}
```

Restart Claude Desktop after editing the file. Then just talk to it:
*"Load the game on drive 8 and show me what's on screen."*

If VICE was started with `-mcpservertoken`, the client must send
`Authorization: Bearer <token>` on every request (with `mcp-remote`, add
`"--header", "Authorization: Bearer <token>"` to `args`). If your MCP client
cannot configure HTTP headers, do not use a token for local-only `127.0.0.1`
sessions.

## Building from Source

### Prerequisites

| Platform | Install |
|---|---|
| **Debian/Ubuntu** | `apt install build-essential autoconf automake pkg-config libmicrohttpd-dev libgtk-3-dev libglew-dev libevdev-dev libasound2-dev libpulse-dev libcurl4-openssl-dev xa65 flex bison byacc dos2unix` |
| **macOS** | `brew install autoconf automake bison pkg-config libmicrohttpd gtk+3 librsvg adwaita-icon-theme glew xa dos2unix` |
| **Windows (MSYS2)** | See `vice/doc/building/Windows-MinGW-GTK3-Howto.txt`, plus `mingw-w64-x86_64-libmicrohttpd` |

These match the CI pipelines (`.woodpecker/build-linux.yaml`,
`.woodpecker/build-macos.yaml`), which add optional codec and I/O libraries.
libmicrohttpd 0.9.71 or newer is needed (the transport uses `enum MHD_Result`;
`configure` checks for it).

### Build

```bash
cd vice
# not "build": vice/build/ is VICE's packaging directory
mkdir -p build-gtk && cd build-gtk
../configure --enable-mcp-server --enable-gtk3ui
make -j"$(getconf _NPROCESSORS_ONLN)"
```

For a headless build use `--enable-headlessui` instead of `--enable-gtk3ui`;
without ALSA or PulseAudio add `--without-alsa --without-pulse`.

The generated `configure`, `Makefile.in` and parser files are committed, so a
checkout builds as above. After changing `configure.ac` or a `Makefile.am`,
run `./autogen.sh` (autoconf, automake, libtool) and commit what it
regenerates; CI regenerates them on every build regardless.

### Verify

```bash
# MCP flags should appear in help output
src/x64sc -help | grep -A1 mcpserver

# Expected:
# -mcpserver
# 	Enable MCP server
# +mcpserver
# 	Disable MCP server
# -mcpserverport <port>
# 	Set MCP server port (default 6520)
# -mcpserverhost <host>
# 	Set MCP server host (default 127.0.0.1)
# -mcpservertoken <token>
# 	Require bearer token for MCP HTTP requests
# -mcpservercorsorigin <origin>
# 	Enable CORS for one exact origin (requires MCPServerToken)
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

`tools/resilience/vice_mcp_resilient.py` is a Python client with retries
(exponential back-off, falling back from `tools/call` to direct JSON-RPC
dispatch), client-side parameter checks, and a per-call reliability log
(`~/.vice-mcp/reliability.jsonl`). Invalid-params, invalid-request and parse
errors fail at once; transport errors and timeouts are retried. It has a
snake_case method for every registered tool; `vice.call("vice.tool.name",
**params)` calls any tool by name. It does not send a bearer token.

```python
# Run from the repository root (or add it to PYTHONPATH)
from tools.resilience import ViceMCPClient

with ViceMCPClient(host="127.0.0.1", port=6520) as vice:  # POSTs to /mcp
    # Load a program
    vice.autostart("/path/to/game.prg")

    # Set a breakpoint at the main loop
    vice.checkpoint_add(start=0x0810)

    # Resume; returns at once, the machine stops when the breakpoint hits
    vice.execution_run()

    # Read the CPU and the screen
    regs = vice.registers_get()
    shot = vice.display_screenshot(return_base64=True)  # shot["base64"]

    # Inspect a sprite
    art = vice.sprite_inspect(sprite_number=0)
    print(art["bitmap"])
```

### Tests

`tools/tests/test_resilient_client.py` unit-tests the client without a
server (it checks the client's methods and schemas against the C sources):

```bash
pytest tools/tests/test_resilient_client.py -v
```

The protocol suite has 392 test cases (172 test methods in 24 test classes
plus one module-level test, with parametrized schema checks for every
advertised tool) covering the JSON-RPC protocol, tool schemas and parameter
validation against a live VICE:

```bash
# Requires Python 3.10+, pytest, requests, and VICE started with -mcpserver
# on 127.0.0.1:6520 (hard-coded). Without a server every test is skipped.
pytest tools/tests/test_mcp_protocol.py -v

# Skip the test that needs a disk in unit 8 and the 43 that change
# emulator state
pytest tools/tests/test_mcp_protocol.py -v -m "not stateful and not disk_required"
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

The MCP server is **localhost-only by default** (`127.0.0.1`). With the default
settings, only programs on the same machine can connect.

It has no TLS. It has optional bearer-token authentication. It is designed for
local development first, and network exposure should be deliberate.

If you need remote access, put it behind a reverse proxy with proper auth:

```
nginx/caddy -> auth -> https -> 127.0.0.1:6520
```

Binding to `0.0.0.0` is supported via `-mcpserverhost`, which takes an IPv4
address (host names and IPv6 are rejected). `0.0.0.0` makes VICE listen
on every network interface. It does not mean clients connect to `0.0.0.0`; remote
clients connect to the Mac/Linux/Windows host's real IP address.

Use this checklist for remote sessions:

- Start VICE with `-mcpserverhost 0.0.0.0`.
- Prefer adding `-mcpservertoken <token>` unless the network is already trusted.
- Configure the client URL as `http://<host-ip>:6520/mcp`.
- If a token is configured, configure the client to send
  `Authorization: Bearer <token>`.
- For browser-based clients, also configure one exact
  `-mcpservercorsorigin <origin>` and a token. CORS without a token is rejected.

## Relationship to Upstream VICE

This is a fork of the [VICE SVN mirror](https://github.com/VICE-Team/svn-mirror).
The MCP server lives in `vice/src/mcp/`. Integrating it touches a few core
files: `init.c`, `monitor/monitor.c` (step, pause and checkpoint holds),
`interrupt.c` (trap queue, interrupt state across snapshot loads),
`mainc64cpu.c`, `joyport/`, `configure.ac` and `src/Makefile.am`.

`main` is upstream VICE (merged in from `upstream/main`) plus all MCP work;
there is no separate MCP branch.

The goal is to contribute this work back to the VICE project. Generated
autotools files (`configure`, `Makefile.in`, …) are currently committed, so a
raw diff against upstream is not yet ready to submit.

## Tool Reference

<details>
<summary><strong>Click to expand full reference for all 66 tools</strong></summary>

Address parameters accept a JSON number (0-65535), a hex string with a `$` or
`0x` prefix (`"$C000"`, `"0xC000"`), or the name of a loaded symbol. A decimal
string such as `"4096"` is looked up as a symbol name, not parsed as a number.
The schemas published by `tools/list` declare address parameters as strings;
numbers are accepted all the same.

### Execution Control

#### `vice.ping`
Check if VICE is responding. No parameters.

#### `vice.execution.run`
Resume execution. No parameters.

#### `vice.execution.pause`
Pause execution at the next instruction boundary. No parameters. The call
returns once the machine is held there, so the registers read after it are
the CPU's and a register set holds. If it has not stopped within 2 seconds,
the reply carries `timed_out: true` and the machine stops at its next
instruction boundary. Inside the monitor, the stop is taken when the
monitor closes.

#### `vice.execution.step`
Step one or more instructions. On a stopped machine the call returns
after the step, with `completed: true` and the PC. If a checkpoint stops
the machine first, the reply carries `stopped_early: true` and the PC
there; if the step has not finished after 2 seconds (a step over a slow
subroutine), the machine is paused at the next instruction boundary and
the reply carries `timed_out: true`. Either way the rest of the step is
dropped. On a running machine the call arms the step and returns at once.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `count` | number | | Instructions to step (default: 1, max: 10000) |
| `stepOver` | boolean | | Step over subroutines |

#### `vice.frame.advance`
Run whole frames from a stopped machine and stop again, at the first
instruction boundary after the vertical sync that ends the last frame,
with registers exported. Joystick and keyboard state set while
stopped is held while the frames run, which makes a frame-by-frame input
loop possible: set input, advance one frame, read memory, repeat. The
machine must already be stopped (by `vice.execution.pause`, a stopping
checkpoint or `vice.execution.step`); otherwise the call returns error
-32001. If something stops the machine before the boundary, the reply
carries `stopped_early: true`, the number of whole frames that ran, and
`checkpoints`: the numbers of the stopping checkpoints that were hit (empty
when a UI pause or a `vice.run_until` address stopped it). A
frame that has not ended after 2 seconds is abandoned: the machine is
paused at the next instruction boundary and the reply also carries
`timed_out: true`.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `frames` | number | | Whole frames to run before stopping again (default: 1, max: 1000) |

#### `vice.run_until`
Resume until `address` is reached or `cycles` CPU cycles have run, whichever
comes first. `address` sets a temporary exec breakpoint (deleted when hit).
When the cycle limit is reached the machine stops as `vice.execution.pause`
stops it: at the next instruction boundary, held, registers current; the
temporary breakpoint, if not yet hit, is deleted. The call returns at once
without waiting; poll `vice.ping` or `vice.registers.get` to see when the
machine has stopped. Reply: `target_address` and `target_symbol` (when known)
if `address` was given, `cycle_limit` if `cycles` was, and `message`.

A new `vice.run_until` replaces a cycle limit still pending. A pending limit
is dropped once the machine stops for any other reason (the breakpoint, a
pause, a step, another checkpoint), is reset, or loads a snapshot through
`vice.snapshot.load`, so it never stops a machine resumed after that. A
pause, monitor session or snapshot load started from the VICE UI does not
drop it; cycles spent paused do not count.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | number \| string | one of the two | Target address: number, `$hex`/`0xhex` string, or symbol |
| `cycles` | number | one of the two | Stop after this many CPU cycles from now: integer 1-100000000 |

---

### Registers

#### `vice.registers.get`
Get all CPU registers (A, X, Y, SP, PC, status flags). No parameters.

#### `vice.registers.set`
Set a CPU register value.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `register` | string | yes | Register name, uppercase (case-sensitive): `PC` `A` `X` `Y` `SP` `N` `V` `B` `D` `I` `Z` `C` |
| `value` | number | yes | PC 0-65535; A/X/Y/SP 0-255; flags 0 or 1 |

---

### Memory

#### `vice.memory.read`
Read a memory range with optional bank selection.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | number \| string | yes | Start address |
| `size` | number | yes | Bytes to read (1-65535) |
| `bank` | string \| number | | Bank name (see `vice.memory.banks`) or number 0-255; default is the CPU view |
| `encoding` | string | | `array` (default): `data` is an array of 2-digit hex strings (`["A9","00",…]`); `hex`: `data_hex` is one string (`"A900…"`) |

#### `vice.memory.write`
Write bytes to memory.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | number \| string | yes | Start address |
| `data` | number[] | yes | Bytes to write (0-255 each) |

#### `vice.memory.banks`
List available memory banks for the current machine. No parameters.

#### `vice.memory.search`
Search for byte patterns with optional wildcard mask.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | number \| string | yes | Start address |
| `end` | number \| string | yes | End address (inclusive, ≥ `start`) |
| `pattern` | number[] | yes | 1-256 bytes, e.g. `[0x4C, 0x00, 0xA0]` |
| `mask` | number[] | | Same length as `pattern`; per-byte `0xFF`=exact, `0x00`=wildcard |
| `max_results` | number | | Max matches (default: 100, max: 10000) |

Reply: `matches` (`"$XXXX"` strings), `total_matches`, `truncated`.

#### `vice.memory.fill`
Fill a memory range with a repeating byte pattern.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | number \| string | yes | Start address |
| `end` | number \| string | yes | End address (inclusive) |
| `pattern` | number[] | yes | Byte pattern to repeat (1-256 bytes, each 0-255) |

#### `vice.memory.compare`
Compare two memory ranges or compare against a snapshot. Each difference is
`{address, current, reference}`; in `ranges` mode `current` is range 1 and
`reference` is range 2. Snapshot mode needs a C64-family snapshot: it reads
the RAM stored in the snapshot and compares it with live RAM, so ROM or I/O
banked in over RAM does not show up as differences. `ranges` mode compares
what the CPU sees.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `mode` | string | yes | `ranges` or `snapshot` |
| `range1_start` | number \| string | ranges | Start of first range |
| `range1_end` | number \| string | ranges | End of first range |
| `range2_start` | number \| string | ranges | Start of second range (must fit below `$FFFF`) |
| `snapshot_name` | string | snapshot | Snapshot to compare against |
| `start` | number \| string | snapshot | Start address to compare |
| `end` | number \| string | snapshot | End address to compare |
| `max_differences` | number | | Max diffs to return (default: 100, max: 10000) |

---

### Checkpoints & Breakpoints

#### `vice.checkpoint.add`
Add a checkpoint (breakpoint, watchpoint, or tracepoint). Hits are counted
(`hit_count` in `vice.checkpoint.list`) but not written to the VICE log. VICE
checks every enabled checkpoint at each instruction, so thousands of them slow
the emulator down; to find out which code runs, use `vice.coverage.*`.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | number \| string | yes | Start address |
| `end` | number \| string | | End address (default = start) |
| `stop` | boolean | | Stop on hit (default: true) |
| `load` | boolean | | Break on memory read (default: false) |
| `store` | boolean | | Break on memory write (default: false) |
| `exec` | boolean | | Break on execution. Default: true only when `load` and `store` are unset; with either set, pass `exec: true` to also break on execution |

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
| `condition` | string | yes | Only `<reg> == <value>`: reg is `A`, `X`, `Y`, `PC` or `SP`; `=` also accepted; value is `$hex`, `0xhex` or decimal. E.g. `A == $42` |

`vice.checkpoint.list` reports conditions as text (`"A == $42"`), including
expression trees set in the VICE monitor.

#### `vice.checkpoint.set_ignore_count`
Set how many hits to ignore before stopping.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `checkpoint_num` | number | yes | Checkpoint number |
| `count` | number | yes | Hits to ignore (≥ 0) |

#### `vice.checkpoint.group.create`
Create a named checkpoint group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Unique group name (max 63 chars; max 32 groups) |
| `checkpoint_ids` | number[] | | Initial checkpoint IDs: existing checkpoints only, max 64 per group; otherwise error -32602 and no group is created |

#### `vice.checkpoint.group.add`
Add checkpoints to an existing group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `group` | string | yes | Group name |
| `checkpoint_ids` | number[] | yes | Existing checkpoint IDs to add; past 64 members, or an unknown ID, is error -32602 and nothing is added. Reply `added` |

#### `vice.checkpoint.group.toggle`
Enable or disable all checkpoints in a group.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `group` | string | yes | Group name |
| `enabled` | boolean | yes | Enable or disable all |

#### `vice.checkpoint.group.list`
List all checkpoint groups. No parameters.


---

### Sprites (C64, C64SC, SCPU64, C128, C64DTV)

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
Visual ASCII art representation of a sprite's bitmap. Returns `pointer` (the
address of the sprite-pointer byte, not its value), `data_address`,
`dimensions`, `multicolor`, `colors` (`main`, plus `multi1`/`multi2` when
multicolor), `raw_data` (63 bytes) and `bitmap`. The VIC bank, screen base and
colors come from the I/O chips, and the data is read as the VIC-II sees it
(RAM, plus the character ROM at `$1000-$1FFF` of VIC banks 0 and 2 on the C64
family).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `sprite_number` | number | yes | Sprite number 0-7 |
| `format` | string | | `ascii` (default) or `binary`. `png_base64` returns error -32000 (not implemented) |

---

### Chip State

VIC-II and CIA tools work on C64, C64SC, SCPU64, C128 and C64DTV; on other
machines they return error -32000 (`<chip> not available on this machine`).
The `set_state` tools (and `vice.sprite.set`) write to the chips through the
I/O bank, whatever `$01` maps at `$D000`. Every value is checked first: one
invalid value or register entry fails the call with -32602 and nothing is
written. Decoded colors are 0-15 (the VIC-II drives only the low nibble; the
DTV's color registers are 8-bit). The raw `registers` arrays are unmasked.

#### `vice.vicii.get_state`
Get VIC-II internal state. No parameters.

#### `vice.vicii.set_state`
Set VIC-II registers.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x2E, value 0-255) |
| `border_color` | number | | `$D020`, 0-15 (0-255 on the DTV) |
| `background_color` | number | | `$D021`, 0-15 (0-255 on the DTV) |
| `background_color_1` `background_color_2` `background_color_3` | number | | `$D022-$D024`, 0-15 (0-255 on the DTV) |
| `control_1` | number | | `$D011` raw byte |
| `control_2` | number | | `$D016` raw byte |
| `memory_pointers` | number | | `$D018` raw byte |
| `irq_raster_line` | number | | Raster IRQ compare line 0-511 (writes `$D012` and bit 7 of `$D011`) |
| `interrupt_enable` | number | | `$D01A`, 0-15 |

Reply includes `registers_updated` (the number of register writes).

#### `vice.sid.get_state`
Get SID state (voices, filter, ADSR). No parameters.

#### `vice.sid.set_state`
Set SID registers.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `registers` | object[] | | Array of `{offset, value}` (offset 0x00-0x1C) |
| `voice0` `voice1` `voice2` | object | | 0-based (`get_state` numbers voices 1-3). Keys: `frequency` (16-bit), `pulse_width` (12-bit), `control`, `attack_decay`, `sustain_release` (raw bytes) |
| `filter_cutoff` | number | | 11-bit cutoff, split across `$D415`/`$D416` |
| `filter_resonance` | number | | Raw `$D417` byte (resonance bits 4-7, voice routing bits 0-3) |
| `filter_mode_volume` | number | | Raw `$D418` byte (mode bits 4-7, volume bits 0-3) |

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
| `cia1` `cia2` | object | | Named writes: `port_a`, `port_b`, `ddr_a`, `ddr_b`, `timer_a_low`, `timer_a_high`, `timer_b_low`, `timer_b_high` (timer latches), `interrupt_control` (a write sets the ICR mask), `control_a`, `control_b` |

---

### Disk Management

#### `vice.disk.attach`
Attach a disk image to a drive.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |
| `drive` | number | | Drive 0 or 1 within a dual-drive unit (default: 0) |
| `path` | string | yes | Path to disk image (.d64, .g64, etc.) |

#### `vice.disk.detach`
Detach a disk image.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `unit` | number | yes | Drive unit (8-11) |
| `drive` | number | | Drive 0 or 1 within a dual-drive unit (default: 0) |

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
Autostart a disk, tape, snapshot, cartridge or PRG/P00 file. The type is
auto-detected; disk images go to unit 8.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to .prg, .d64, .g64, etc. |
| `program` | string | | Program name to load from disk |
| `run` | boolean | | Run after loading (default: true) |
| `index` | number | | Directory entry to load when `program` is omitted: 1 = first file, 2 = second, …; 0 or omitted loads `*` |

#### `vice.machine.reset`
Reset the machine.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `mode` | string | | `soft` (default; `cpu` is an alias) or `hard` (power cycle; `power` is an alias) |
| `run_after` | boolean | | Resume after reset (default: true) |

#### `vice.machine.config.get`
Get machine configuration — chips, memory map, resources. No parameters.

#### `vice.machine.config.set`
Set integer VICE resources, by the names `vicerc` uses, plus `WarpMode` (0/1).
For example `{"JoyPort1Device": 1}` plugs a joystick into control port 1 and
`{"JoyPort1Device": 0}` unplugs it. Resources that would reconfigure the MCP
server (`MCPServer*`), open other listeners (`MonitorServer*`,
`BinaryMonitorServer*`, `NetworkServer*`, `NetworkControl`, `IDE64USBServer`),
write log or core files (`MonitorLog*`, `Log*`, `DoCoreDump`) or save the
configuration (`SaveResourcesOnExit`) are refused, as are string resources.

Changing a model resource power-cycles the machine and resumes it:

- All machines: `MachineVideoStandard` (1=PAL, 2=NTSC, 3=NTSC-Old, 4=PAL-N)
- C64/C64SC/SCPU64: `SidModel`, `CIA1Model`, `CIA2Model`
- C128: those plus `VDCRevision`, `VDC64KB`, `C128FullBanks`
- C64DTV: `SidModel`
- VIC-20: `RAMBlock0/1/2/3/5`
- Plus/4: `RamSize`, `Acia1Enable`
- PET: `RamSize`, `IOSize`, `Crtc`, `VideoSize`, `SuperPET`
- CBM-II: `RamSize`, `ModelLine`, `SidModel`

Other resources take effect without a reset. The request is all or nothing: every pair is checked first,
and if a resource rejects its value the pairs already applied are restored.
Returns `changes_applied` (`resource`, `old_value`, `new_value`,
`needs_reset`), `machine_reset`, and `new_config` (same shape as
`vice.machine.config.get`).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `resources` | object | yes | Resource name/value pairs, e.g. `{"WarpMode": 1}` |

---

### Display

#### `vice.display.screenshot`
Capture a display. Reply: `status`, `format`, `display` (the chip captured:
`vicii`, `vdc`, `vic`, `ted` or `crtc`), `path` when one was given, and with
`return_base64` the image.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `display` | string | | `vicii` (40-column VIC-II) or `vdc` (x128's 80-column VDC). Default: the machine's main display, the VIC-II on x128. A display the machine lacks returns error -32602 |
| `path` | string | unless `return_base64` | File to write. Omitted with `return_base64: true`: a temp file is used and deleted |
| `format` | string | | `PNG` or `BMP` (lower case accepted). Default `PNG`, or `BMP` if VICE was built without libpng |
| `return_base64` | boolean | | Also return the image as `base64`, `data_uri` and `size` (bytes) |

#### `vice.display.get_dimensions`
Get display dimensions. Returns `display` (as for `vice.display.screenshot`),
`width`, `height` (physical canvas, or the visible size when headless),
`visible_width` and `visible_height`.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `display` | string | | `vicii` or `vdc`, as for `vice.display.screenshot` |

---

### Input

#### `vice.keyboard.type`
Type text with automatic PETSCII conversion.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `text` | string | yes | Text to type (`\n` for Return) |
| `petscii_upper` | boolean | | Uppercase mapping (default: true) |

#### `vice.keyboard.petscii`
Feed exact PETSCII bytes into the KERNAL keyboard buffer, with no ASCII
conversion. Bytes share VICE's keyboard-buffer queue with
`vice.keyboard.type` and are delivered as the running KERNAL empties its
buffer. Returns `bytes_queued`; fails if the queue is full or keyboard-buffer
injection is unavailable.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `data` | number[] | yes | 1-4096 PETSCII byte values, each 1-255 (`13` = Return; `0` is not allowed) |

#### `vice.keyboard.key_press`
Press a host key; it goes through the active host keymap.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string \| number | yes | Case-sensitive name: `Return`/`Enter`, `Space`, `BackSpace`, `Delete`, `Escape`, `Tab`, `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `F1`-`F8`; a single character; or a numeric VHK key code |
| `modifiers` | string[] | | `shift`, `control`/`ctrl`, `alt`, `meta`, `command`/`cmd`; unknown names are ignored |
| `hold_frames` | number | | Hold duration in frames (1-300) |
| `hold_ms` | number | | Hold duration in ms (1-5000), converted at 20 ms/frame; overrides `hold_frames` |

#### `vice.keyboard.key_release`
Release a key.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string \| number | yes | Same as `vice.keyboard.key_press` |
| `modifiers` | string[] | | Modifiers to release |

#### `vice.keyboard.restore`
Press/release the RESTORE key (triggers NMI).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `pressed` | boolean | | true=press, false=release (default: true) |

#### `vice.keyboard.matrix`
Direct keyboard matrix control for games. Positions are the C64 matrix layout.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | | C64 key name, case-insensitive (takes precedence over `row`/`col`): `A`-`Z`, `0`-`9`, `SPACE`, `RETURN`/`ENTER`, `STOP`/`RUNSTOP`, `F1` `F3` `F5` `F7`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `LSHIFT`/`SHIFT`, `RSHIFT`, `CTRL`/`CONTROL`, `CBM`/`C=`, `HOME`/`CLR`, `DEL`/`INST`, `POUND`, `ARROWUP`, `ARROWLEFT`, `PLUS`, `MINUS`, `ASTERISK`, `AT`, `COLON`, `SEMICOLON`, `EQUALS`, `COMMA`, `PERIOD`, `SLASH`. `UP` and `LEFT` are SHIFT + CRSR DOWN/RIGHT on the C64, so they press SHIFT too (reply has `shift: true`) |
| `row` | number | | Matrix row 0-7 (alternative to key) |
| `col` | number | | Matrix column 0-7 (alternative to key) |
| `pressed` | boolean | | Key state (default: true) |
| `hold_frames` | number | | Auto-release after N frames (0-300; 0 = no auto-release) |
| `hold_ms` | number | | Auto-release after N ms (0-5000, 20 ms/frame, at least 1 frame); overrides `hold_frames` |

`vice.keyboard.matrix` and `vice.keyboard.chord` share 16 auto-release slots
(`vice.keyboard.key_press` has its own 16). A held press that finds no free
slot fails with -32603 and presses nothing.

#### `vice.keyboard.chord`
Press several keyboard-matrix keys at the same instant, hold them, then
release them all. Uses C64 matrix positions. Returns `keys` (the `row`/`col`
of each), `hold_frames` (plus `hold_ms` if given) and
`auto_release_scheduled`. If the shared auto-release pool cannot take every
key, the call fails and presses nothing.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `keys` | array | yes | 1-16 entries. Each is a key name (as `vice.keyboard.matrix`; `UP`/`LEFT` add SHIFT, counting toward the 16), `{"key": name}`, or `{"row": 0-7, "col": 0-7}` |
| `hold_frames` | number | | Frames to hold before release (1-300, default: 3) |
| `hold_ms` | number | | Milliseconds to hold (1-5000, 20 ms/frame); overrides `hold_frames` |

Example: cursor up = `{"keys": ["UP"]}`, the same as `{"keys": ["SHIFT", "DOWN"]}`.

#### `vice.joystick.set`
Set joystick state. The reply's `value` is a bitmask: up=1, down=2, left=4,
right=8, fire=16. Returns error -32602 when nothing is plugged into the port
(`JoyPort1Device`/`JoyPort2Device` is 0), except for a release to center;
plug a joystick in with
`vice.machine.config.set` `{"resources": {"JoyPort1Device": 1}}`.
`vice.joystick.tap` does the same.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `port` | number | | Port 1 or 2 (default: 1) |
| `direction` | string \| string[] | | `up`, `down`, `left`, `right`, `center`/`none`; an array combines directions, e.g. `["up","left"]` |
| `fire` | boolean | | Fire button (default: false) |

#### `vice.joystick.tap`
Set a direction and/or fire for a fixed time, then center.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `port` | number | | Port 1 or 2 (default: 1) |
| `direction` | string \| string[] | | As `vice.joystick.set` |
| `fire` | boolean | | Fire button (default: false) |
| `duration_frames` | number | | Frames before auto-center (1-300, default: 3) |
| `duration_ms` | number | | Milliseconds before auto-center (1-5000, 20 ms/frame); overrides `duration_frames` |

#### `vice.paddle.set`
Set both paddle positions on a C64 control port. The values feed SID POTX/POTY reads at `$D419`/`$D41A` through VICE's normal port selection and sampling. `255` means an open paddle input; analog SID reads may vary slightly due to VICE's paddle noise model. After a port is set, MCP values override attached paddle or mouse input on that port until VICE exits.

| Parameter | Type | Required | Description |
|---|---|---|---|
| `port` | number | yes | Control port 1 or 2 |
| `x` | number | yes | X position, integer 0-255 |
| `y` | number | yes | Y position, integer 0-255 |

---

### Advanced Debugging

#### `vice.disassemble`
Disassemble memory to 6502 instructions.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `address` | number \| string | yes | Start address |
| `count` | number | | Instructions to disassemble (default: 10, max: 100) |
| `show_symbols` | boolean | | Show symbol names (default: true) |

#### `vice.symbols.load`
Load a symbol/label file.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `path` | string | yes | Path to .sym or .lbl file |
| `format` | string | | `auto` (default), `kickasm`, `vice`, or `simple`; anything else is error -32602. `vice` and `simple` both read `al C:xxxx .name`, ld65's `al 00xxxx .name`, and `name = xxxx` lines (value hex, `$` optional, spaces around `=` optional) |

Reply: `format_detected`, `symbols_loaded`.

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
| `address` | number \| string | yes | Address to watch |
| `size` | number | | Bytes to watch (default: 1) |
| `type` | string | | `read`, `write`, or `both` (default: `write`) |
| `load` | boolean | | Alternative to `type`: watch reads, as in `vice.checkpoint.add` |
| `store` | boolean | | Alternative to `type`: watch writes, as in `vice.checkpoint.add` |
| `stop` | boolean | | Stop on hit (default: true); `false` counts hits without stopping |
| `condition` | string | | Same grammar as `vice.checkpoint.set_condition`, e.g. `A == $42` |

`type` overrides `load`/`store` when both are given; `load: false, store: false`
is an error.

#### `vice.backtrace`
Heuristic call stack: reads 2-byte pairs from SP+1 upward as return
addresses, skipping values below `$0200` or above `$FFFC`. `called_from` and
`called_target` appear only where a JSR precedes the address; data pushed
with PHA can misalign or pad the list.
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
Save complete emulator state. Fails if a snapshot with that name already
exists, unless `overwrite` is true; a replacement is written beside the old
snapshot and renamed over it, so a failed save keeps the old one. Files go in
`mcp_snapshots/` under the VICE user config directory; `vice.snapshot.list`
returns the `directory`.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Snapshot name (alphanumeric, `_`, `-`) |
| `description` | string | | What this snapshot captures |
| `include_roms` | boolean | | Include ROMs (default: false) |
| `include_disks` | boolean | | Include disk state (default: false) |
| `overwrite` | boolean | | Replace a snapshot of the same name (default: false); the reply's `replaced` says whether one was |

#### `vice.snapshot.load`
Restore emulator state from a snapshot. That includes the devices plugged
into the control ports when it was saved: a snapshot taken with port 1 empty
leaves port 1 empty, through resets, until `JoyPort1Device` is set again.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Snapshot name |

#### `vice.snapshot.list`
List all snapshots with metadata. No parameters.

#### `vice.snapshot.delete`
Delete a snapshot and its metadata file. Error -32602 if there is none.
| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Snapshot name |

---

### Coverage

The main CPU marks each address it fetches an opcode from while coverage is
on: one test and store per instruction, with no measurable slowdown. Addresses
are CPU addresses, so RAM and ROM banked in at the same address share a mark.
The drive CPUs, x128's Z80 and xscpu64's 65816 are not recorded
(`vice.coverage.start` returns -32000 on xscpu64).

#### `vice.coverage.start`
Clear the map and start recording. No parameters.

#### `vice.coverage.stop`
Stop recording; the map is kept for `vice.coverage.read`. No parameters.

#### `vice.coverage.read`
Returns `enabled` and `count` (executed addresses), and either `bitmap` (no
range given: base64 of 8192 bytes, where bit k, LSB first, of byte i is
address 8*i+k) or `addresses` (the executed addresses from `start` to `end`).
| Parameter | Type | Required | Description |
|---|---|---|---|
| `start` | number \| string | | First address of a range (default: 0) |
| `end` | number \| string | | Last address of the range, inclusive (default: $FFFF) |
| `reset` | boolean | | Clear the map after reading it (default: false) |

</details>

## Project Status

This is active, working software. The MCP server compiles and runs on Linux,
macOS and Windows. 66 tools are registered. CI builds binaries for all three
platforms on every push to `main`; pull requests and pushes to `main` build a
headless VICE and run the C unit suite (`vice/src/mcp/tests`). The live
protocol suite (`tools/tests`) is run by hand.

**What's solid:**
- Full tool suite — execution, memory, breakpoints, sprites, chip state, disk, input, debugging
- Machine-aware responses across the VICE emulators (chip-state tools cover the C64 family)
- Python client with retry logic and its own unit tests
- Cross-platform builds: Linux x86_64 (GUI + headless), macOS arm64 (GUI + headless), Windows x86_64 (headless)
- Automated CI/CD pipeline with binary releases

**Not done yet:**
- Event streaming. `GET /events` is reserved but returns `501 Not Implemented` today.
- Execution tracing (a per-instruction log) and interrupt logging.
  `vice.coverage.*` records which addresses ran, not their order.
- `vice.sprite.inspect` `png_base64` output (returns error -32000).

## Contributing

> *"Cross over, children. All are welcome. All welcome."*
> — Tangina Barrons, speaking to contributors about this repo

This project bridges two communities that don't often overlap: retro computing
and modern AI tooling. Contributions from either world (or both) are welcome.

**Areas where help would be especially appreciated:**

- **VICE internals** — Hooking execution tracing and interrupt logging into the CPU core
- **Event streaming transport** — Adding real-time breakpoint and state-change notifications
- **Additional machine support** — Testing and tuning tools for PET, CBM-II, Plus/4
- **Client libraries** — TypeScript, Rust, Go clients
- **Documentation** — Tutorials, example workflows, video demos
- **Testing** — Running the protocol test suite against edge cases

The MCP server lives in `vice/src/mcp/` (hooks in `init.c`, `monitor/monitor.c`,
`interrupt.c`, `mainc64cpu.c`, `joyport/`). Start there.

### Versioning & releases

This repo follows the Walker Heavy Industries Build & Release Standard:

- **Conventional Commits** are required. Versioning is automated with
  [Cocogitto](https://docs.cocogitto.io/) — `feat:` bumps the minor, `fix:` the
  patch, and a `!`/`BREAKING CHANGE` bumps the major. Install the local commit
  hook once with `cog install-hook --all` (CI validates commits on every push;
  it does not run on pull requests).
- On every push to `main`, CI runs `cog bump --auto`, which tags the next
  `vX.Y.Z`, updates `CHANGELOG.md`, and publishes a GitHub Release with the
  generated changelog notes.
- The multi-OS build matrix (Linux/macOS/Windows) attaches its artifacts to the
  Release that push created. `ci/release-version.sh` finds it: the `v` tag cog
  put on the bump commit, whose parent is the pushed commit. A push with no
  bump-worthy commits builds but publishes nothing, so it cannot overwrite the
  latest Release. The bump commit is marked `[skip ci]`, and the bump waits for
  the unit suite (`test.yaml`) and the commit check (`commits.yaml`).
- The latest `v` tag / Release is the single source of truth for the version.
  `compute-version.sh` and the `vice-mcp-*` tag scheme are retired (old
  `vice-mcp-*` tags remain on the remote).

## License

VICE is released under the GNU General Public License v2. The MCP server additions
follow the same license.

## Acknowledgments

- The [VICE Team](https://vice-emu.sourceforge.io/) for 30+ years of the best
  8-bit emulator ever written
- [Anthropic](https://anthropic.com/) for the Model Context Protocol specification
- The Commodore 64 community — still going strong after four decades

## Part of the suite

VICE MCP is part of the **Walker Heavy Industries** retro toolchain —
modern tools for the retro 8- and 16-bit ecosystem.

- **House hub:** https://whi.dev
- **Siblings:** VICE Mac · VICE MCP · FamiForge · NESBasic · Novus · Miggy Draw · NovaVM
