# VICE MCP Debugging Tools - Design Document

**Date:** 2026-02-03
**Status:** Approved
**Origin:** Chess agent wishlist from debugging session

## Overview

This document specifies **17 new MCP tools plus 2 enhancements** for the VICE emulator, prioritized by the chess debugging agent based on real-world debugging experience. All tools leverage existing VICE monitor primitives where possible.

## Priority Summary

| Priority | Tool | VICE Primitive | Effort |
|----------|------|----------------|--------|
| High | `vice.memory.compare` | `mon_memory_compare()` + snapshot parsing | Medium |
| High | `vice.memory.search` | `mon_memory_hunt()` | Low |
| High | `vice.cycles.stopwatch` | `mon_stopwatch_*()` | Low |
| High | `vice.keyboard.matrix` | Already exists - verify only | None |
| Medium | `vice.trace.start/stop` | CPU hook (new) | High |
| Medium | `vice.watch.add` enhancement | `cond_node_t` exists | Low |
| Medium | `vice.checkpoint.group.*` | MCP bookkeeping only | Low |
| Medium | `vice.interrupt.log.*` | Profiler hooks | Medium |
| Medium | `vice.memory.fill` | `mon_memory_fill()` | Low |
| Low | `vice.checkpoint.set_auto_snapshot` | Extend checkpoint handling | Medium |
| Low | `vice.memory.map` | Bank config query | Low |
| Low | `vice.backtrace` enhancement | Already exists - verify first | TBD |
| Low | `vice.sprite.inspect` | Memory read + decode | Low |

**Tool Count:**
- New tools: 17
- Enhancements to existing tools: 2

---

## High Priority Tools

### 1. `vice.memory.compare`

Compare memory ranges or compare current memory against a saved snapshot.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `mode` | string | Yes | `"ranges"` or `"snapshot"` |
| `range1_start` | address | If mode=ranges | Start of first range |
| `range1_end` | address | If mode=ranges | End of first range |
| `range2_start` | address | If mode=ranges | Start of second range |
| `snapshot_name` | string | If mode=snapshot | Name of snapshot to compare against |
| `start` | address | If mode=snapshot | Start address to compare |
| `end` | address | If mode=snapshot | End address to compare |
| `max_differences` | number | No | Maximum differences to return (default 100) |

**Returns:**
```json
{
  "differences": [
    {"address": "$A000", "current": 76, "reference": 0},
    {"address": "$A001", "current": 72, "reference": 0}
  ],
  "total_differences": 16384,
  "truncated": true
}
```

**Implementation Notes:**
- For `mode=ranges`: Use existing `mon_memory_compare()` primitive
- For `mode=snapshot`: Parse .vsf file directly, extract C64MEM module, compare without loading
- VSF files use chunked format with module headers; C64MEM contains full 64KB

**Use Case:** "What changed since I saved `before_ai_search`?" - catches memory corruption like the TTClear 64KB wipe.

---

### 2. `vice.memory.search`

Search for byte patterns in memory with optional wildcards.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `start` | address | Yes | Start of search range |
| `end` | address | Yes | End of search range |
| `pattern` | array[number] | Yes | Byte pattern to find, e.g., `[0x4C, 0x00, 0xA0]` |
| `mask` | array[number] | No | Per-byte mask: `0xFF`=exact, `0x00`=wildcard |
| `max_results` | number | No | Maximum matches to return (default 100) |

**Returns:**
```json
{
  "matches": ["$C000", "$C100", "$C200"],
  "total_matches": 3,
  "truncated": false
}
```

**Implementation Notes:**
- Uses existing `mon_memory_hunt()` with `data_buf` and `data_mask_buf`
- Mask array must match pattern length if provided

**Use Case:** Find `JMP $A000` instructions: `{"pattern": [0x4C, 0x00, 0xA0]}`

---

### 3. `vice.cycles.stopwatch`

Measure elapsed CPU cycles for timing-critical code analysis.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `action` | string | Yes | `"reset"`, `"read"`, or `"reset_and_read"` |

**Returns:**
```json
{
  "cycles": 19656,
  "previous_cycles": 0,
  "memspace": "computer"
}
```

**Implementation Notes:**
- Uses existing `mon_stopwatch_reset()` and `mon_stopwatch_show()`
- `reset_and_read` is atomic: returns previous value and resets in one call
- Per-memspace tracking (main CPU vs drive CPUs)

**Use Case:** Measure raster routine timing - reset at line start, read at end.

---

### 4. `vice.keyboard.matrix` (Verification Only)

**No new tool needed.** The existing `vice.keyboard.matrix` tool calls `keyboard_set_keyarr()` correctly.

**Verification procedure:**
1. Call `vice.keyboard.matrix` with row/column for a key
2. Call `vice.memory.read` at `$DC01` (CIA1 port B)
3. Verify the appropriate bit is cleared

If this doesn't work, fix the existing tool rather than adding a new one.

---

## Medium Priority Tools

### 5. `vice.trace.start` / `vice.trace.stop`

Record executed instructions to a file with optional PC range filtering.

**`vice.trace.start` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `output_file` | string | Yes | Path to write trace output |
| `pc_filter_start` | address | No | Only log PCs >= this address |
| `pc_filter_end` | address | No | Only log PCs <= this address |
| `max_instructions` | number | No | Stop after N instructions (default 10000) |
| `include_registers` | boolean | No | Include register values (default false) |

**Returns:**
```json
{
  "trace_id": "trace_001"
}
```

**`vice.trace.stop` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `trace_id` | string | Yes | Trace to stop |

**Returns:**
```json
{
  "instructions_recorded": 5432,
  "output_file": "/tmp/trace.log",
  "cycles_elapsed": 15234
}
```

**Output Format (plain text):**
```
$C000: LDA #$00
$C002: STA $D020
$C005: JMP $C000
```

With `include_registers: true`:
```
$C000: LDA #$00    [A=00 X=FF Y=00 SP=FF P=32]
$C002: STA $D020   [A=00 X=FF Y=00 SP=FF P=32]
```

**Implementation Notes:**
- Hooks CPU execution loop (runs during live emulation, no pause required)
- Ring buffer in memory, flushed to file periodically
- Trace stops on: max_instructions, breakpoint hit, or explicit stop call
- Plain text format; JSON option can be added later if needed

**Use Case:** "What code ran between these two breakpoints?"

---

### 6. `vice.watch.add` Enhancement

Enhance existing tool with condition support.

**Additional Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `condition` | string | No | Condition expression, e.g., `"@($D020) == $02"` |

**Condition Syntax (VICE native):**
- Registers: `.A`, `.X`, `.Y`, `.SP`, `.PC`
- Memory: `@($D020)` or `@(address)`
- Constants: `$02`, `#2`, `%00000010`
- Operators: `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `&`, `|`

**Note:** For store watches, conditions are evaluated *after* the store completes. To check "stop when $D020 becomes $02", use:
```json
{
  "address": "$D020",
  "type": "store",
  "condition": "@($D020) == $02"
}
```

**Implementation Notes:**
- Leverages existing `cond_node_t` expression tree and `vice.checkpoint.set_condition`
- This enhancement is syntactic convenience over calling checkpoint tools separately

---

### 7. `vice.checkpoint.group.*`

Manage breakpoints in named groups for bulk enable/disable.

**`vice.checkpoint.group.create` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `name` | string | Yes | Group name, e.g., `"ai_breakpoints"` |
| `checkpoint_ids` | array[number] | No | Initial members |

**`vice.checkpoint.group.add` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `group` | string | Yes | Group name |
| `checkpoint_ids` | array[number] | Yes | Checkpoints to add |

**`vice.checkpoint.group.toggle` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `group` | string | Yes | Group name |
| `enabled` | boolean | Yes | Enable or disable all members |

**Returns:**
```json
{
  "affected_count": 5
}
```

**`vice.checkpoint.group.list` Returns:**
```json
{
  "groups": [
    {"name": "ai_breakpoints", "checkpoint_ids": [1, 2, 3], "enabled_count": 2, "disabled_count": 1},
    {"name": "display_breakpoints", "checkpoint_ids": [4, 5], "enabled_count": 2, "disabled_count": 0}
  ]
}
```

**Implementation Notes:**
- Pure MCP-side bookkeeping (no VICE changes needed)
- Store group membership in MCP server memory
- `toggle` iterates members and calls existing `vice.checkpoint.toggle`

**Use Case:** "Disable all AI breakpoints, enable display breakpoints"

---

### 8. `vice.interrupt.log.*`

Track IRQ/NMI/BRK events with timing and context.

**`vice.interrupt.log.start` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `types` | array[string] | No | Filter: `["irq", "nmi", "brk"]` (default all) |
| `max_entries` | number | No | Ring buffer size (default 1000) |

**Returns:**
```json
{
  "log_id": "intlog_001"
}
```

**`vice.interrupt.log.stop` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `log_id` | string | Yes | Log to stop |

**`vice.interrupt.log.read` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `log_id` | string | Yes | Log to read |
| `since_index` | number | No | For incremental reads |

**Returns:**
```json
{
  "entries": [
    {
      "type": "irq",
      "cycle": 15234,
      "pc": "$C050",
      "vector_address": "$FFFE",
      "handler_address": "$EA31"
    },
    {
      "type": "nmi",
      "cycle": 20000,
      "pc": "$C100",
      "vector_address": "$FFFA",
      "handler_address": "$FE47"
    }
  ],
  "total_interrupts": 2
}
```

**Fields:**
- `pc`: Where CPU was when interrupted
- `vector_address`: Which vector was used ($FFFE for IRQ, $FFFA for NMI)
- `handler_address`: What the vector pointed to at interrupt time (KERNAL vs custom)

**Implementation Notes:**
- Hook interrupt dispatch in CPU emulation (profiler tracks entry/exit; if insufficient, CPU dispatch code handles IRQ/NMI vectors directly and can be hooked)
- Ring buffer with configurable size

**Use Case:** "Show me when interrupts fired and what the PC was"

---

### 9. `vice.memory.fill`

Bulk write a byte pattern to a memory range.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `start` | address | Yes | Start address |
| `end` | address | Yes | End address (inclusive) |
| `pattern` | array[number] | Yes | Byte pattern to repeat |
| `bank` | number | No | Memory bank (default current) |

**Returns:**
```json
{
  "bytes_written": 16384,
  "pattern_repetitions": 16384
}
```

**Examples:**
- Zero-fill: `{"start": "$A000", "end": "$DFFF", "pattern": [0]}`
- Screen spaces: `{"start": "$0400", "end": "$07FF", "pattern": [32]}`
- NOP sled: `{"start": "$C000", "end": "$C0FF", "pattern": [0xEA]}`
- Alternating: `{"start": "$2000", "end": "$3FFF", "pattern": [0xAA, 0x55]}`

**Implementation Notes:**
- Uses existing `mon_memory_fill()` primitive
- Pattern repeats to fill entire range

**Use Case:** Clear memory regions, set up test conditions, patch code - without 16,384 individual API calls.

---

## Low Priority Tools

### 10. `vice.checkpoint.set_auto_snapshot` / `vice.checkpoint.clear_auto_snapshot`

Automatically save snapshots when specific breakpoints are hit.

**`vice.checkpoint.set_auto_snapshot` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `checkpoint_id` | number | Yes | Checkpoint to attach auto-snapshot |
| `snapshot_prefix` | string | Yes | Prefix for snapshot names |
| `max_snapshots` | number | No | Ring buffer size (default 10) |
| `include_disks` | boolean | No | Include disk images (default false) |

**`vice.checkpoint.clear_auto_snapshot` Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `checkpoint_id` | number | Yes | Checkpoint to clear |

**Behavior:**
- When checkpoint is hit, save to `{prefix}_{hit_count:03d}.vsf`
- Ring buffer: when `max_snapshots` exceeded, oldest deleted
- Example: `ai_move_001.vsf`, `ai_move_002.vsf`, ...

**Implementation Notes:**
- Extend checkpoint data structure with auto-snapshot config
- On checkpoint hit, check for auto-snapshot flag, call snapshot save logic

**Use Case:** Capture state at "AI about to make move" for post-mortem analysis.

---

### 11. `vice.memory.map`

Display memory region layout with optional symbol-based content hints.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `start` | address | No | Start address (default $0000) |
| `end` | address | No | End address (default $FFFF) |
| `granularity` | number | No | Minimum region size (default 256) |

**Returns:**
```json
{
  "regions": [
    {
      "start": "$0000",
      "end": "$9FFF",
      "type": "ram",
      "name": "Main RAM",
      "bank": 0,
      "contents_hint": null
    },
    {
      "start": "$A000",
      "end": "$BFFF",
      "type": "ram",
      "name": "Upper RAM",
      "bank": 0,
      "contents_hint": "OpeningBook ($A000-$A4A7), AI_Search ($A4A8-$B8FF)"
    },
    {
      "start": "$D000",
      "end": "$D3FF",
      "type": "io",
      "name": "VIC-II",
      "bank": 0,
      "contents_hint": null
    }
  ]
}
```

**Region Types:**
- `ram` - Read/write memory
- `rom` - BASIC, KERNAL, character ROM
- `io` - VIC-II, SID, CIA, etc.
- `unmapped` - No device at address
- `cartridge` - Cartridge ROM/RAM

**Implementation Notes:**
- Query VICE's current bank configuration
- `contents_hint` populated only when symbols are loaded
- Scans symbol table for addresses in each region, formats as hint string

**Use Case:** Understand memory layout, identify conflicts between code regions.

---

### 12. `vice.backtrace` (Enhancement - Pending Verification)

**Current state:** `vice.backtrace` already exists and should return call stack.

**Action:** Verify existing tool before designing enhancements.

**Potential Enhancement (if needed):**

**Additional Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `include_locals` | boolean | No | Include stack frame contents |

**Enhanced Returns:**
```json
{
  "frames": [
    {
      "depth": 0,
      "return_address": "$C050",
      "call_site": "$C000",
      "type": "jsr",
      "stack_pointer": 247,
      "locals": [0, 0, 5, 10]
    },
    {
      "depth": 1,
      "return_address": "$EA31",
      "call_site": "$FF48",
      "type": "irq",
      "stack_pointer": 244,
      "locals": null
    }
  ]
}
```

**Frame Types:** `jsr`, `irq`, `nmi`, `brk`

---

### 13. `vice.sprite.inspect`

Visual representation of sprite bitmap data.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `sprite_number` | number | Yes | Sprite 0-7 |
| `format` | string | No | `"ascii"` (default), `"binary"`, `"png_base64"` |

**Returns:**
```json
{
  "pointer": "$07F8",
  "data_address": "$2000",
  "dimensions": {"width": 24, "height": 21},
  "multicolor": false,
  "colors": {"main": 1},
  "bitmap": "........########........\n......############......\n..."
}
```

**ASCII Character Legend:**
```
.  = transparent (00)
#  = sprite color (10)
@  = multicolor 1 (01)
%  = multicolor 2 (11)
```

**Multicolor Example:**
```
........@@@@####@@@@....
......@@%%####%%##@@....
....@@%%@@####@@%%##@@..
```

**Implementation Notes:**
- Read sprite pointer from $07F8 + sprite_number
- Calculate data address: pointer * 64
- Read 63 bytes of sprite data
- Decode based on multicolor flag from $D01C
- For multicolor: read colors from $D025 (multi1), $D026 (multi2)

**Use Case:** Visually verify sprite data, detect corruption in ASCII form.

---

## Implementation Order Recommendation

**Phase 5.1 - Quick Wins (Low effort, high value):**
1. `vice.memory.search` - Wraps existing `mon_memory_hunt()`
2. `vice.cycles.stopwatch` - Wraps existing `mon_stopwatch_*()`
3. `vice.memory.fill` - Wraps existing `mon_memory_fill()`

**Phase 5.2 - Memory Comparison:**
4. `vice.memory.compare` (ranges mode) - Wraps `mon_memory_compare()`
5. `vice.memory.compare` (snapshot mode) - Requires .vsf parsing (prototype early to validate)

**Phase 5.3 - Checkpoint Enhancements:**
6. `vice.watch.add` condition enhancement
7. `vice.checkpoint.group.*` (4 tools, MCP-side only)
8. `vice.checkpoint.set_auto_snapshot` / `vice.checkpoint.clear_auto_snapshot`

**Phase 5.4 - Execution Monitoring:**
9. `vice.trace.start` / `vice.trace.stop` - Requires CPU hook
10. `vice.interrupt.log.start` / `vice.interrupt.log.stop` / `vice.interrupt.log.read` - Requires interrupt hook

**Phase 5.5 - Inspection Tools:**
11. `vice.memory.map`
12. `vice.sprite.inspect`
13. `vice.backtrace` enhancement (if needed after testing)

---

## Appendix: VICE Primitives Referenced

| MCP Tool | VICE Function | Location |
|----------|---------------|----------|
| memory.compare | `mon_memory_compare()` | `mon_memory.c` |
| memory.search | `mon_memory_hunt()` | `mon_memory.c:152-200` |
| memory.fill | `mon_memory_fill()` | `mon_memory.c` |
| cycles.stopwatch | `mon_stopwatch_reset/show()` | `monitor.c:1466-1482` |
| watch conditions | `cond_node_t` | `montypes.h:167-177` |
| interrupt.log | Profiler hooks / CPU dispatch | `profiler.h`, CPU core |
| backtrace | `callstack_*[]` arrays | `montypes.h` |

---

## Resolved Questions

1. **Trace file format:** Plain text for now. JSON can be a future parameter option if needed.
2. **Snapshot memory extraction:** VSF files use chunked format with module headers. C64MEM module contains full 64KB. Prototype early in Phase 5.2 to validate feasibility.
3. **Interrupt hooks:** Profiler tracks interrupt entry/exit. If insufficient, CPU dispatch code explicitly handles IRQ/NMI vectors and can be hooked directly.

---

*Document generated from brainstorming session with chess debugging agent.*
*Approved: 2026-02-03*
