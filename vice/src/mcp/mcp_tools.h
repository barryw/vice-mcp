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

/* Forward declaration for JSON type */
/* TODO: Include actual JSON library header (cJSON or jansson) */
typedef void json_t;

/* Tool handler function signature */
typedef json_t* (*mcp_tool_handler_t)(json_t *params);

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
extern json_t* mcp_tools_dispatch(const char *tool_name, json_t *params);

/* Tool handlers - Phase 1: Core tools */
extern json_t* mcp_tool_ping(json_t *params);
extern json_t* mcp_tool_execution_run(json_t *params);
extern json_t* mcp_tool_execution_pause(json_t *params);
extern json_t* mcp_tool_execution_step(json_t *params);
extern json_t* mcp_tool_registers_get(json_t *params);
extern json_t* mcp_tool_registers_set(json_t *params);
extern json_t* mcp_tool_memory_read(json_t *params);
extern json_t* mcp_tool_memory_write(json_t *params);

/* Notification helpers */
extern void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id);
extern void mcp_notify_execution_state_changed(const char *state);

#endif /* VICE_MCP_TOOLS_H */
