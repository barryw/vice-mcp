/*
 * mcp_tools.c - MCP tool implementations for VICE
 *
 * Written by:
 *  Barry Walker <barrywalker@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <string.h>

#include "mcp_tools_internal.h"

log_t mcp_tools_log = LOG_DEFAULT;
static int mcp_tools_initialized = 0;  /* Double-initialization guard */


/* MCP step mode state moved to mcp_tools_execution.c */
/* Forward declarations for keyboard auto-release moved to mcp_tools_input.c */
/* Trace/interrupt log data and functions moved to mcp_tools_trace.c */
/* Helper functions moved to mcp_tools_helpers.c */

/* Forward declarations now in mcp_tools_internal.h */

/* Tool registry - const to prevent modification */
const mcp_tool_t tool_registry[] = {
    /* MCP Base Protocol */
    { "initialize", "Initialize MCP session", mcp_tool_initialize },
    { "notifications/initialized", "Client initialized notification", mcp_tool_initialized_notification },

    /* Meta */
    { "tools/list", "List all available tools with schemas", mcp_tool_tools_list },
    { "tools/call", "Call a tool by name", mcp_tool_tools_call },

    /* Phase 1: Core tools */
    { "vice.ping", "Check if VICE is responding", mcp_tool_ping },
    { "vice.execution.run", "Resume execution", mcp_tool_execution_run },
    { "vice.execution.pause", "Pause execution", mcp_tool_execution_pause },
    { "vice.execution.step", "Step one or more instructions", mcp_tool_execution_step },
    { "vice.registers.get", "Get CPU registers", mcp_tool_registers_get },
    { "vice.registers.set", "Set CPU register value", mcp_tool_registers_set },
    { "vice.memory.read", "Read memory range (with optional bank selection)", mcp_tool_memory_read },
    { "vice.memory.write", "Write to memory", mcp_tool_memory_write },
    { "vice.memory.banks", "List available memory banks", mcp_tool_memory_banks },
    { "vice.memory.search", "Search for byte patterns in memory with optional wildcards", mcp_tool_memory_search },

    /* Phase 2.1: Checkpoints/Breakpoints */
    { "vice.checkpoint.add", "Add checkpoint/breakpoint", mcp_tool_checkpoint_add },
    { "vice.checkpoint.delete", "Delete checkpoint", mcp_tool_checkpoint_delete },
    { "vice.checkpoint.list", "List all checkpoints", mcp_tool_checkpoint_list },
    { "vice.checkpoint.toggle", "Enable/disable checkpoint", mcp_tool_checkpoint_toggle },
    { "vice.checkpoint.set_condition", "Set checkpoint condition", mcp_tool_checkpoint_set_condition },
    { "vice.checkpoint.set_ignore_count", "Set checkpoint ignore count", mcp_tool_checkpoint_set_ignore_count },

    /* Phase 2.2: Sprite Control (C64/C128/DTV only) */
    { "vice.sprite.get", "Get sprite state", mcp_tool_sprite_get },
    { "vice.sprite.set", "Set sprite properties", mcp_tool_sprite_set },

    /* Phase 2.3: Chip State Access */
    { "vice.vicii.get_state", "Get VIC-II internal state", mcp_tool_vicii_get_state },
    { "vice.vicii.set_state", "Set VIC-II registers", mcp_tool_vicii_set_state },
    { "vice.sid.get_state", "Get SID state (voices, filter)", mcp_tool_sid_get_state },
    { "vice.sid.set_state", "Set SID registers", mcp_tool_sid_set_state },
    { "vice.cia.get_state", "Get CIA state (timers, ports)", mcp_tool_cia_get_state },
    { "vice.cia.set_state", "Set CIA registers", mcp_tool_cia_set_state },

    /* Phase 2.4: Disk Management */
    { "vice.disk.attach", "Attach disk image to drive", mcp_tool_disk_attach },
    { "vice.disk.detach", "Detach disk image from drive", mcp_tool_disk_detach },
    { "vice.disk.list", "List directory contents", mcp_tool_disk_list },
    { "vice.disk.read_sector", "Read raw sector data", mcp_tool_disk_read_sector },

    /* Autostart */
    { "vice.autostart", "Autostart a PRG or disk image", mcp_tool_autostart },

    /* Machine control */
    { "vice.machine.reset", "Reset the machine (soft or hard reset)", mcp_tool_machine_reset },

    /* Phase 2.5: Display Capture */
    { "vice.display.screenshot", "Capture screenshot (to file or base64)", mcp_tool_display_screenshot },
    { "vice.display.get_dimensions", "Get display dimensions", mcp_tool_display_get_dimensions },

    /* Phase 3.1: Input Control */
    { "vice.keyboard.type", "Type text (uppercase ASCII displays as uppercase on C64 by default)", mcp_tool_keyboard_type },
    { "vice.keyboard.key_press", "Press a specific key", mcp_tool_keyboard_key_press },
    { "vice.keyboard.key_release", "Release a specific key", mcp_tool_keyboard_key_release },
    { "vice.keyboard.restore", "Press/release RESTORE key (triggers NMI, not in matrix)", mcp_tool_keyboard_restore },
    { "vice.joystick.set", "Set joystick state", mcp_tool_joystick_set },

    /* Phase 4: Advanced Debugging */
    { "vice.disassemble", "Disassemble memory to 6502 instructions", mcp_tool_disassemble },
    { "vice.symbols.load", "Load symbol/label file (VICE, KickAssembler, simple formats)", mcp_tool_symbols_load },
    { "vice.symbols.lookup", "Lookup symbol by name or address", mcp_tool_symbols_lookup },
    { "vice.watch.add", "Add memory watchpoint with optional condition (shorthand for checkpoint with store/load)", mcp_tool_watch_add },
    { "vice.backtrace", "Show call stack (JSR return addresses)", mcp_tool_backtrace },
    { "vice.run_until", "Run until address or for N cycles (with timeout)", mcp_tool_run_until },
    { "vice.keyboard.matrix", "Direct keyboard matrix control (for games that scan keyboard directly)", mcp_tool_keyboard_matrix },

    /* Snapshot Management */
    { "vice.snapshot.save",
      "Save complete emulator state to a named snapshot. "
      "Use this to capture a debugging checkpoint that can be restored later with snapshot.load. "
      "Snapshots are stored in ~/.config/vice/mcp_snapshots/ with JSON metadata.",
      mcp_tool_snapshot_save },
    { "vice.snapshot.load",
      "Restore emulator state from a previously saved snapshot. "
      "Use this to return to a known debugging checkpoint without re-running setup steps. "
      "The emulator will be in the exact state it was when the snapshot was saved.",
      mcp_tool_snapshot_load },
    { "vice.snapshot.list",
      "List all available snapshots with their metadata. "
      "Returns snapshot names, descriptions, creation times, and machine types. "
      "Use this to find the right snapshot to load for debugging.",
      mcp_tool_snapshot_list },

    /* Phase 5.1: Enhanced Debugging Tools */
    { "vice.cycles.stopwatch",
      "Measure elapsed CPU cycles for timing-critical code analysis. "
      "Use 'reset' to start timing, 'read' to get elapsed cycles, or 'reset_and_read' "
      "for atomic read-and-reset. Ideal for measuring raster routine timing.",
      mcp_tool_cycles_stopwatch },
    { "vice.memory.fill",
      "Fill memory range with a repeating byte pattern. "
      "Useful for clearing memory, creating NOP sleds, or setting up test conditions. "
      "The pattern repeats to fill the entire range from start to end (inclusive).",
      mcp_tool_memory_fill },

    /* Phase 5.2: Memory Compare */
    { "vice.memory.compare",
      "Compare two memory ranges or compare current memory against a snapshot. "
      "Use mode='ranges' to compare two live memory ranges, or mode='snapshot' to compare "
      "against a previously saved state. Returns list of differences with addresses and values.",
      mcp_tool_memory_compare },

    /* Phase 5.3: Checkpoint Groups */
    { "vice.checkpoint.group.create",
      "Create a named checkpoint group for batch operations. "
      "Groups allow you to enable/disable multiple breakpoints at once. "
      "Optionally include initial checkpoint_ids array.",
      mcp_tool_checkpoint_group_create },
    { "vice.checkpoint.group.add",
      "Add checkpoints to an existing group. "
      "Use this to grow a group after creation.",
      mcp_tool_checkpoint_group_add },
    { "vice.checkpoint.group.toggle",
      "Enable or disable all checkpoints in a group. "
      "Use enabled=true to enable, enabled=false to disable.",
      mcp_tool_checkpoint_group_toggle },
    { "vice.checkpoint.group.list",
      "List all checkpoint groups with their member counts. "
      "Returns group names, checkpoint IDs, and enabled/disabled counts.",
      mcp_tool_checkpoint_group_list },

    /* Phase 5.3: Auto-Snapshot on Checkpoint Hit */
    { "vice.checkpoint.set_auto_snapshot",
      "Configure automatic snapshot on checkpoint hit. "
      "When the checkpoint triggers, saves to {prefix}_{hit_count:03d}.vsf with ring buffer. "
      "NOTE: Actual triggering requires VICE callback integration (config stored MCP-side).",
      mcp_tool_checkpoint_set_auto_snapshot },
    { "vice.checkpoint.clear_auto_snapshot",
      "Remove auto-snapshot configuration from a checkpoint. "
      "Stops automatic snapshot-on-hit behavior for this checkpoint.",
      mcp_tool_checkpoint_clear_auto_snapshot },

    /* Phase 5.4: Execution Tracing */
    { "vice.trace.start",
      "Start execution trace recording to a file. "
      "Records disassembled instructions as they execute, optionally filtered by PC range. "
      "NOTE: Actual tracing requires VICE CPU hook integration (config stored MCP-side).",
      mcp_tool_trace_start },
    { "vice.trace.stop",
      "Stop an active execution trace and get recording statistics. "
      "Returns instructions recorded, output file path, and cycles elapsed.",
      mcp_tool_trace_stop },

    /* Phase 5.4: Interrupt Logging */
    { "vice.interrupt.log.start",
      "Start logging interrupt events (IRQ, NMI, BRK). "
      "Returns log_id for later reference. Filter by interrupt types and set max entries. "
      "NOTE: Actual logging requires VICE interrupt hook integration (config stored MCP-side).",
      mcp_tool_interrupt_log_start },
    { "vice.interrupt.log.stop",
      "Stop an active interrupt log and retrieve all recorded entries. "
      "Returns entries with type, cycle, pc, vector_address, and handler_address.",
      mcp_tool_interrupt_log_stop },
    { "vice.interrupt.log.read",
      "Read entries from an active interrupt log without stopping it. "
      "Supports incremental reads via since_index parameter.",
      mcp_tool_interrupt_log_read },

    /* Machine Configuration */
    { "vice.machine.config.get",
      "Get current machine configuration including chips, memory map, and resources. "
      "Returns machine type, available chips, memory layout, and configurable resources.",
      mcp_tool_machine_config_get },
    { "vice.machine.config.set",
      "Set machine configuration resources (WarpMode, Speed, video standard, SID model, CIA model, etc). "
      "WarpMode (0/1) disables speed limiting for fast execution. "
      "Speed (1-10000, 0=unlimited) sets CPU speed percentage. "
      "Only whitelisted resources can be changed. Returns the new configuration.",
      mcp_tool_machine_config_set },

    /* Phase 5.5: Sprite Inspect */
    { "vice.sprite.inspect",
      "Visual representation of sprite bitmap data. "
      "Reads sprite pointer, data, and multicolor settings to render ASCII art. "
      "ASCII Legend: '.'=transparent(00), '#'=sprite color(10), '@'=multi1(01), '%'=multi2(11).",
      mcp_tool_sprite_inspect },

    { NULL, NULL, NULL } /* Sentinel */
};

/** @brief Number of registered tools (excluding sentinel). */
#define MCP_TOOL_COUNT (sizeof(tool_registry) / sizeof(tool_registry[0]) - 1)

int mcp_tools_init(void)
{
    /* Check for double-initialization */
    if (mcp_tools_initialized) {
        log_warning(mcp_tools_log, "MCP tools already initialized - ignoring duplicate init");
        return 0;
    }

    /* Open log (VICE's log system is always-available, no error checking needed) */
    mcp_tools_log = log_open("MCP-Tools");

    log_message(mcp_tools_log, "MCP tools initializing...");

    /* TODO: Register all tools */

    log_message(mcp_tools_log, "MCP tools initialized - %d tools registered",
                (int)MCP_TOOL_COUNT);

    /* Mark as initialized */
    mcp_tools_initialized = 1;

    return 0;
}

void mcp_tools_shutdown(void)
{
    if (!mcp_tools_initialized) {
        /* Not initialized, nothing to shut down */
        return;
    }

    log_message(mcp_tools_log, "MCP tools shutting down...");

    /* TODO: Cleanup if needed */

    log_message(mcp_tools_log, "MCP tools shut down");

    /* Clear initialization flag */
    mcp_tools_initialized = 0;
}

cJSON* mcp_tools_dispatch(const char *tool_name, cJSON *params)
{
    int i;
    size_t name_len;

    /* Validate tool name */
    if (tool_name == NULL || *tool_name == '\0') {
        log_error(mcp_tools_log, "NULL or empty tool name");
        return mcp_error(MCP_ERROR_INVALID_REQUEST, "Invalid Request: missing method name");
    }

    /* Limit length to prevent DoS */
    name_len = strlen(tool_name);
    if (name_len > 256) {
        log_error(mcp_tools_log, "Tool name too long: %lu bytes", (unsigned long)name_len);
        return mcp_error(MCP_ERROR_INVALID_REQUEST, "Invalid Request: method name too long");
    }

    log_message(mcp_tools_log, "Dispatching tool: %s", tool_name);

    /* Find tool in registry */
    for (i = 0; tool_registry[i].name != NULL; i++) {
        if (strcmp(tool_registry[i].name, tool_name) == 0) {
            return tool_registry[i].handler(params);
        }
    }

    log_error(mcp_tools_log, "Tool not found: %s", tool_name);
    return mcp_error(MCP_ERROR_METHOD_NOT_FOUND, "Method not found");
}

/* Protocol handlers (ping, initialize, tools/list, tools/call) moved to mcp_tools_protocol.c */
/* Execution control and register tools moved to mcp_tools_execution.c */
/* Memory read, write, banks, and search tools moved to mcp_tools_memory.c */
/* Execution tracing and interrupt logging tools moved to mcp_tools_trace.c */

/* Memory map tool moved to mcp_tools_memory.c */
