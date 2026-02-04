/*
 * mcp_tools.h - MCP tool definitions and dispatch for VICE
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

#ifndef VICE_MCP_TOOLS_H
#define VICE_MCP_TOOLS_H

#include "types.h"
#include "cJSON.h"

/* JSON-RPC 2.0 error codes */
#define MCP_ERROR_PARSE_ERROR      -32700  /* Invalid JSON */
#define MCP_ERROR_INVALID_REQUEST  -32600  /* JSON is not valid Request */
#define MCP_ERROR_METHOD_NOT_FOUND -32601  /* Method does not exist */
#define MCP_ERROR_INVALID_PARAMS   -32602  /* Invalid method parameters */
#define MCP_ERROR_INTERNAL_ERROR   -32603  /* Internal JSON-RPC error */

/* MCP-specific errors (use -32000 to -32099 range per spec) */
#define MCP_ERROR_NOT_IMPLEMENTED  -32000  /* Feature not yet implemented */
#define MCP_ERROR_EMULATOR_RUNNING -32001  /* Operation requires pause */
#define MCP_ERROR_INVALID_ADDRESS  -32002  /* Address out of range */
#define MCP_ERROR_INVALID_VALUE    -32003  /* Value out of range */
#define MCP_ERROR_SNAPSHOT_FAILED  -32004  /* Snapshot operation failed */

/** @brief Function signature for MCP tool handlers.
 *
 *  @param params  JSON object containing tool parameters (may be NULL)
 *  @return JSON object with result or error response (caller must free)
 */
typedef cJSON* (*mcp_tool_handler_t)(cJSON *params);

/** @brief Tool registration entry.
 *
 *  Each MCP tool is registered with a name, description, and handler function.
 */
typedef struct mcp_tool_s {
    const char *name;           /**< Tool name (e.g., "vice.ping") */
    const char *description;    /**< Human-readable description */
    mcp_tool_handler_t handler; /**< Handler function */
} mcp_tool_t;

/** @brief Initialize the MCP tools subsystem.
 *
 *  Registers all available tools. Must be called before mcp_tools_dispatch().
 *
 *  @return 0 on success, -1 on failure
 */
extern int mcp_tools_init(void);

/** @brief Shutdown the MCP tools subsystem.
 *
 *  Frees any resources allocated by mcp_tools_init().
 */
extern void mcp_tools_shutdown(void);

/** @brief Dispatch a tool call by name.
 *
 *  Looks up the tool by name and calls its handler with the given params.
 *  This function is called from the transport layer for each JSON-RPC request.
 *
 *  @param tool_name  The tool name (e.g., "vice.ping", "tools/call")
 *  @param params     JSON object with parameters (passed to handler)
 *  @return JSON response object (caller must free with cJSON_Delete)
 */
extern cJSON* mcp_tools_dispatch(const char *tool_name, cJSON *params);

/* Tool handlers - MCP Base Protocol */
extern cJSON* mcp_tool_initialize(cJSON *params);
extern cJSON* mcp_tool_initialized_notification(cJSON *params);

/* Tool handlers - Meta */
extern cJSON* mcp_tool_tools_list(cJSON *params);

/* Tool handlers - Phase 1: Core tools
 *
 * THREAD SAFETY WARNING:
 * Phase 1 tools do NOT synchronize with the emulator. Callers MUST ensure
 * emulation is paused before calling any tool except ping(). Calling tools
 * while emulation is running may cause race conditions or incorrect results.
 *
 * Phase 2 will add proper synchronization via VICE's IK_MONITOR interrupt.
 */
extern cJSON* mcp_tool_ping(cJSON *params);
extern cJSON* mcp_tool_execution_run(cJSON *params);
extern cJSON* mcp_tool_execution_pause(cJSON *params);
extern cJSON* mcp_tool_execution_step(cJSON *params);
extern cJSON* mcp_tool_registers_get(cJSON *params);
extern cJSON* mcp_tool_registers_set(cJSON *params);
extern cJSON* mcp_tool_memory_read(cJSON *params);
extern cJSON* mcp_tool_memory_write(cJSON *params);
extern cJSON* mcp_tool_memory_banks(cJSON *params);
extern cJSON* mcp_tool_memory_search(cJSON *params);

/* Tool handlers - Phase 2.1: Checkpoints/Breakpoints */
extern cJSON* mcp_tool_checkpoint_add(cJSON *params);
extern cJSON* mcp_tool_checkpoint_delete(cJSON *params);
extern cJSON* mcp_tool_checkpoint_list(cJSON *params);
extern cJSON* mcp_tool_checkpoint_toggle(cJSON *params);
extern cJSON* mcp_tool_checkpoint_set_condition(cJSON *params);
extern cJSON* mcp_tool_checkpoint_set_ignore_count(cJSON *params);

/* Tool handlers - Phase 2.2: Sprites */
extern cJSON* mcp_tool_sprite_get(cJSON *params);
extern cJSON* mcp_tool_sprite_set(cJSON *params);

/* Tool handlers - Phase 2.3: Chip State */
extern cJSON* mcp_tool_vicii_get_state(cJSON *params);
extern cJSON* mcp_tool_vicii_set_state(cJSON *params);
extern cJSON* mcp_tool_sid_get_state(cJSON *params);
extern cJSON* mcp_tool_sid_set_state(cJSON *params);
extern cJSON* mcp_tool_cia_get_state(cJSON *params);
extern cJSON* mcp_tool_cia_set_state(cJSON *params);

/* Tool handlers - Phase 2.4: Disk Management */
extern cJSON* mcp_tool_disk_attach(cJSON *params);
extern cJSON* mcp_tool_disk_detach(cJSON *params);
extern cJSON* mcp_tool_disk_list(cJSON *params);
extern cJSON* mcp_tool_disk_read_sector(cJSON *params);

/* Tool handlers - Autostart */
extern cJSON* mcp_tool_autostart(cJSON *params);

/* Tool handlers - Phase 2.5: Display Capture */
extern cJSON* mcp_tool_display_screenshot(cJSON *params);
extern cJSON* mcp_tool_display_get_dimensions(cJSON *params);

/* Tool handlers - Phase 3.1: Input Control */
extern cJSON* mcp_tool_keyboard_type(cJSON *params);
extern cJSON* mcp_tool_keyboard_key_press(cJSON *params);
extern cJSON* mcp_tool_keyboard_key_release(cJSON *params);
extern cJSON* mcp_tool_joystick_set(cJSON *params);

/* Tool handlers - Snapshot Management */
extern cJSON* mcp_tool_snapshot_save(cJSON *params);
extern cJSON* mcp_tool_snapshot_load(cJSON *params);
extern cJSON* mcp_tool_snapshot_list(cJSON *params);

/* Tool handlers - Phase 5.1: Enhanced Debugging */
extern cJSON* mcp_tool_cycles_stopwatch(cJSON *params);
extern cJSON* mcp_tool_memory_fill(cJSON *params);

/* Tool handlers - Phase 5.2: Memory Compare */
extern cJSON* mcp_tool_memory_compare(cJSON *params);

/* Tool handlers - Phase 5.3: Checkpoint Groups */
extern cJSON* mcp_tool_checkpoint_group_create(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_add(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_toggle(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_list(cJSON *params);

/* Tool handlers - Phase 5.3: Auto-Snapshot on Checkpoint Hit */
extern cJSON* mcp_tool_checkpoint_set_auto_snapshot(cJSON *params);
extern cJSON* mcp_tool_checkpoint_clear_auto_snapshot(cJSON *params);

/* Tool handlers - Phase 5.4: Execution Tracing */
extern cJSON* mcp_tool_trace_start(cJSON *params);
extern cJSON* mcp_tool_trace_stop(cJSON *params);

/* Tool handlers - Phase 5.4: Interrupt Logging */
extern cJSON* mcp_tool_interrupt_log_start(cJSON *params);
extern cJSON* mcp_tool_interrupt_log_stop(cJSON *params);
extern cJSON* mcp_tool_interrupt_log_read(cJSON *params);

/* Tool handlers - Phase 5.5: Memory Map */
extern cJSON* mcp_tool_memory_map(cJSON *params);

/** @brief Send a breakpoint hit notification to SSE clients.
 *
 *  Called when a breakpoint is hit during execution. Sends an SSE event
 *  to all connected clients with the breakpoint details.
 *
 *  @param pc     Program counter address where breakpoint was hit
 *  @param bp_id  The breakpoint ID that was triggered
 */
extern void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id);

/** @brief Send an execution state change notification to SSE clients.
 *
 *  Called when the emulator execution state changes (run, pause, step).
 *  Sends an SSE event to all connected clients.
 *
 *  @param state  The new state string ("running", "paused", "stepping")
 */
extern void mcp_notify_execution_state_changed(const char *state);

#endif /* VICE_MCP_TOOLS_H */
