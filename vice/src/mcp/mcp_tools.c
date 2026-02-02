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
#include "cJSON.h"
#include "log.h"
#include "maincpu.h"
#include "mem.h"
#include "machine.h"
#include "interrupt.h"
#include "vsync.h"

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
    cJSON *response;

    log_message(mcp_tools_log, "Handling vice.ping");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "version", VERSION);
    cJSON_AddStringToObject(response, "machine", machine_get_name());

    return response;
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
    cJSON *response;
    unsigned int pc, a, x, y, sp;

    log_message(mcp_tools_log, "Handling vice.registers.get");

    /* Read CPU registers from VICE */
    pc = maincpu_get_pc();
    a = maincpu_get_a();
    x = maincpu_get_x();
    y = maincpu_get_y();
    sp = maincpu_get_sp();

    /* Build JSON response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(response, "PC", pc);
    cJSON_AddNumberToObject(response, "A", a);
    cJSON_AddNumberToObject(response, "X", x);
    cJSON_AddNumberToObject(response, "Y", y);
    cJSON_AddNumberToObject(response, "SP", sp);

    /* TODO: Add flags (N, V, B, D, I, Z, C) */

    return response;
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
    cJSON *response, *data_array;
    cJSON *addr_item, *size_item;
    uint16_t address, i;
    int size;
    uint8_t value;
    char hex_str[3];

    log_message(mcp_tools_log, "Handling vice.memory.read");

    /* Extract address and size from params */
    if (params == NULL) {
        return NULL;
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    size_item = cJSON_GetObjectItem(params, "size");

    if (!cJSON_IsNumber(addr_item) || !cJSON_IsNumber(size_item)) {
        return NULL;
    }

    address = (uint16_t)addr_item->valueint;
    size = size_item->valueint;

    if (size < 1 || size > 65536) {
        return NULL;
    }

    /* Build JSON response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(response, "address", address);
    cJSON_AddNumberToObject(response, "size", size);

    data_array = cJSON_CreateArray();
    if (data_array == NULL) {
        cJSON_Delete(response);
        return NULL;
    }

    /* Read memory and add to array */
    for (i = 0; i < size; i++) {
        value = mem_read((uint16_t)(address + i));
        snprintf(hex_str, sizeof(hex_str), "%02X", value);
        cJSON_AddItemToArray(data_array, cJSON_CreateString(hex_str));
    }

    cJSON_AddItemToObject(response, "data", data_array);

    return response;
}

json_t* mcp_tool_memory_write(json_t *params)
{
    cJSON *response;
    cJSON *addr_item, *data_item, *value_item;
    uint16_t address;
    int i, array_size;
    uint8_t byte_val;

    log_message(mcp_tools_log, "Handling vice.memory.write");

    /* Extract address and data from params */
    if (params == NULL) {
        return NULL;
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    data_item = cJSON_GetObjectItem(params, "data");

    if (!cJSON_IsNumber(addr_item) || !cJSON_IsArray(data_item)) {
        return NULL;
    }

    address = (uint16_t)addr_item->valueint;
    array_size = cJSON_GetArraySize(data_item);

    if (array_size < 1 || array_size > 65536) {
        return NULL;
    }

    /* Write to memory */
    for (i = 0; i < array_size; i++) {
        value_item = cJSON_GetArrayItem(data_item, i);
        if (!cJSON_IsNumber(value_item)) {
            /* TODO: Better error handling */
            return NULL;
        }

        byte_val = (uint8_t)(value_item->valueint & 0xFF);
        mem_store((uint16_t)(address + i), byte_val);
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "address", address);
    cJSON_AddNumberToObject(response, "bytes_written", array_size);

    return response;
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
