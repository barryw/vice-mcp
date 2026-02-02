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

/* Tool handler function signature */
typedef cJSON* (*mcp_tool_handler_t)(cJSON *params);

/* Tool registration */
typedef struct mcp_tool_s {
    const char *name;
    const char *description;
    mcp_tool_handler_t handler;
} mcp_tool_t;

/* MCP tools initialization and cleanup */
extern int mcp_tools_init(void);
extern void mcp_tools_shutdown(void);

/* Tool dispatch */
extern cJSON* mcp_tools_dispatch(const char *tool_name, cJSON *params);

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

/* Notification helpers */
extern void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id);
extern void mcp_notify_execution_state_changed(const char *state);

#endif /* VICE_MCP_TOOLS_H */
