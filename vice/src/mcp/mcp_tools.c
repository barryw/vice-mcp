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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mcp_tools.h"
#include "mcp_transport.h"
#include "log.h"

/* TODO: Include VICE internal headers for CPU, memory, etc. */
/* #include "6510core.h" */
/* #include "mem.h" */
/* #include "machine.h" */

static log_t mcp_tools_log = LOG_ERR;

/* Tool registry */
static mcp_tool_t tool_registry[] = {
    { "vice.ping", "Check if VICE is responding", mcp_tool_ping },
    { "vice.execution.run", "Resume execution", mcp_tool_execution_run },
    { "vice.execution.pause", "Pause execution", mcp_tool_execution_pause },
    { "vice.execution.step", "Step one or more instructions", mcp_tool_execution_step },
    { "vice.registers.get", "Get CPU registers", mcp_tool_registers_get },
    { "vice.registers.set", "Set CPU register value", mcp_tool_registers_set },
    { "vice.memory.read", "Read memory range", mcp_tool_memory_read },
    { "vice.memory.write", "Write to memory", mcp_tool_memory_write },
    { NULL, NULL, NULL } /* Sentinel */
};

int mcp_tools_init(void)
{
    mcp_tools_log = log_open("MCP-Tools");

    log_message(mcp_tools_log, "MCP tools initializing...");

    /* TODO: Register all tools */

    log_message(mcp_tools_log, "MCP tools initialized - %d tools registered",
                (int)(sizeof(tool_registry) / sizeof(tool_registry[0]) - 1));

    return 0;
}

void mcp_tools_shutdown(void)
{
    log_message(mcp_tools_log, "MCP tools shutting down...");

    /* TODO: Cleanup if needed */

    log_message(mcp_tools_log, "MCP tools shut down");
}

json_t* mcp_tools_dispatch(const char *tool_name, json_t *params)
{
    int i;

    log_message(mcp_tools_log, "Dispatching tool: %s", tool_name);

    /* Find tool in registry */
    for (i = 0; tool_registry[i].name != NULL; i++) {
        if (strcmp(tool_registry[i].name, tool_name) == 0) {
            return tool_registry[i].handler(params);
        }
    }

    log_error(mcp_tools_log, "Tool not found: %s", tool_name);

    /* TODO: Return JSON-RPC error response */
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Tool Implementations - Phase 1 */

json_t* mcp_tool_ping(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.ping");

    /* TODO: Create JSON response with VICE version info */
    /* Example: {"status": "ok", "version": "3.10", "machine": "C64"} */

    return NULL;
}

json_t* mcp_tool_execution_run(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.run");

    /* TODO: Resume emulation */
    /* TODO: Return execution state */

    mcp_notify_execution_state_changed("RUNNING");

    return NULL;
}

json_t* mcp_tool_execution_pause(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.pause");

    /* TODO: Pause emulation */
    /* TODO: Return current PC and execution state */

    mcp_notify_execution_state_changed("PAUSED");

    return NULL;
}

json_t* mcp_tool_execution_step(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.step");

    /* TODO: Extract step count from params */
    /* TODO: Step CPU instructions */
    /* TODO: Return new PC and register values */

    return NULL;
}

json_t* mcp_tool_registers_get(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.registers.get");

    /* TODO: Read CPU registers directly from VICE internal state */
    /* Example implementation:
     * uint16_t pc = (uint16_t)reg_pc_read();
     * uint8_t a = (uint8_t)reg_a_read();
     * uint8_t x = (uint8_t)reg_x_read();
     * uint8_t y = (uint8_t)reg_y_read();
     * uint8_t sp = (uint8_t)reg_sp_read();
     * uint8_t flags = (uint8_t)reg_p_read();
     */

    /* TODO: Build JSON response */

    return NULL;
}

json_t* mcp_tool_registers_set(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.registers.set");

    /* TODO: Extract register name and value from params */
    /* TODO: Write to CPU register using VICE internal functions */

    return NULL;
}

json_t* mcp_tool_memory_read(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.memory.read");

    /* TODO: Extract address and size from params */
    /* TODO: Read directly from memory using mem_read() */
    /* TODO: Format as hex and return JSON response */

    return NULL;
}

json_t* mcp_tool_memory_write(json_t *params)
{
    log_message(mcp_tools_log, "Handling vice.memory.write");

    /* TODO: Extract address and data from params */
    /* TODO: Write to memory using mem_store() */
    /* TODO: Return success/error */

    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Notification Helpers */

void mcp_notify_breakpoint(uint16_t pc, uint32_t bp_id)
{
    log_message(mcp_tools_log, "Notifying breakpoint hit: PC=$%04X, BP=%u", pc, bp_id);

    /* TODO: Build JSON event */
    /* TODO: Send via SSE */
    /* mcp_transport_sse_send_event("breakpoint", json_data); */
}

void mcp_notify_execution_state_changed(const char *state)
{
    log_message(mcp_tools_log, "Notifying execution state: %s", state);

    /* TODO: Build JSON event */
    /* TODO: Send via SSE */
    /* mcp_transport_sse_send_event("execution_state", json_data); */
}
