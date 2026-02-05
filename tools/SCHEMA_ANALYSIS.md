# VICE MCP Server Schema Analysis

**Date:** 2026-02-05
**Branch:** feature/mcp-server
**Analyzed:** `vice/src/mcp/mcp_tools.c` (tools/list schema generator vs handler implementations)

## Summary

Audited all 63 MCP tools. Found **7 schema inconsistencies** where `tools/list` returns
an incorrect `inputSchema` that doesn't match what the handler actually expects.

All 7 bugs are the same pattern: the tool was listed in the "empty schema" group
(`mcp_schema_empty()`) but the handler accepts parameters.

## Inconsistencies Found

### 1. `vice.sprite.inspect` — CRITICAL

**Schema says:** No parameters (empty object)
**Handler expects:** `sprite_number` (required, 0-7), `format` (optional: ascii/binary/png_base64)
**Impact:** Tool always fails with "sprite_number (0-7) is required" because MCP clients
don't know to send the parameter.
**Location:** Handler at line 8608, falls through to else at line 1921.

### 2. `vice.sprite.get` — MODERATE

**Schema says:** No parameters (empty object)
**Handler expects:** Optional `sprite` (0-7, omit for all sprites)
**Impact:** Tool works (returns all sprites) but clients can't discover the optional filter.
**Location:** Grouped in empty-schema block at line 1452.

### 3. `vice.memory.map` — MODERATE

**Schema says:** No parameters (empty object)
**Handler expects:** Optional `start`, `end` (addresses), `granularity` (number)
**Impact:** Tool works with defaults but clients can't discover filtering options.
**Location:** Falls through to else at line 1921.

### 4. `vice.keyboard.restore` — LOW

**Schema says:** No parameters (empty object)
**Handler expects:** Optional `pressed` (boolean, default true)
**Impact:** Tool works (presses RESTORE) but clients can't discover they can release it.
**Location:** Falls through to else at line 1921.

### 5. `vice.vicii.set_state` — MODERATE

**Schema says:** No parameters (empty object)
**Handler expects:** `registers` (array of {offset, value} objects)
**Impact:** Tool does nothing without parameters; clients don't know what to send.
**Location:** Grouped in empty-schema block at line 1454.

### 6. `vice.sid.set_state` — MODERATE

**Schema says:** No parameters (empty object)
**Handler expects:** `registers` (array of {offset, value}), plus voice-specific params
**Impact:** Same as vicii.set_state.
**Location:** Grouped in empty-schema block at line 1456.

### 7. `vice.cia.set_state` — MODERATE

**Schema says:** No parameters (empty object)
**Handler expects:** `cia1_registers`, `cia2_registers` (arrays of {offset, value})
**Impact:** Same as vicii.set_state.
**Location:** Grouped in empty-schema block at line 1458.

## Root Cause

Tools were added to the "no parameters" group in `mcp_tool_tools_list()` during initial
implementation. As handlers evolved to accept optional parameters, the schema block wasn't
updated to reflect the new parameters.

The `vice.sprite.inspect`, `vice.memory.map`, and `vice.keyboard.restore` tools were added
in later phases and never got schema entries at all — they fall through to the default `else`
branch which returns `mcp_schema_empty()`.

## Fix Applied

All 7 schemas corrected in `mcp_tools.c`. See the diff for details. Changes compile cleanly.

## Tools Verified Correct (56 tools)

All other tools have schemas that correctly match their handler expectations:
- initialize, notifications/initialized, tools/list
- vice.ping, vice.execution.{run,pause,step}
- vice.registers.{get,set}
- vice.memory.{read,write,banks,search}
- vice.checkpoint.{add,delete,list,toggle,set_condition,set_ignore_count}
- vice.sprite.set
- vice.vicii.get_state, vice.sid.get_state, vice.cia.get_state
- vice.disk.{attach,detach,list,read_sector}
- vice.autostart, vice.machine.reset
- vice.display.{screenshot,get_dimensions}
- vice.keyboard.{type,key_press,key_release,matrix}
- vice.joystick.set
- vice.disassemble, vice.symbols.{load,lookup}
- vice.watch.add, vice.backtrace, vice.run_until
- vice.snapshot.{save,load,list}
- vice.cycles.stopwatch, vice.memory.{fill,compare}
- vice.checkpoint.group.{create,add,toggle,list}
- vice.checkpoint.{set_auto_snapshot,clear_auto_snapshot}
- vice.trace.{start,stop}
- vice.interrupt.log.{start,stop,read}
