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
#include "mos6510.h"
#include "mem.h"
#include "machine.h"
#include "interrupt.h"
#include "vsync.h"

static log_t mcp_tools_log = LOG_DEFAULT;
static int mcp_tools_initialized = 0;  /* Double-initialization guard */

/* Catastrophic error response when we can't allocate memory */
const char *CATASTROPHIC_ERROR_JSON =
    "{\"code\":-32603,\"message\":\"Internal error: out of memory\"}";

/* Error response helper
 * Returns JSON error object, or NULL on catastrophic allocation failure.
 * Callers MUST check for NULL and use CATASTROPHIC_ERROR_JSON in that case.
 */
static cJSON* mcp_error(int code, const char *message)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        log_error(mcp_tools_log, "CATASTROPHIC: Cannot allocate error response for code %d: %s", code, message);
        return NULL;
    }
    cJSON_AddNumberToObject(response, "code", code);
    cJSON_AddStringToObject(response, "message", message);
    return response;
}

/* Tool registry - const to prevent modification */
static const mcp_tool_t tool_registry[] = {
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
                (int)(sizeof(tool_registry) / sizeof(tool_registry[0]) - 1));

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
        log_error(mcp_tools_log, "Tool name too long: %zu bytes", name_len);
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

/* ------------------------------------------------------------------------- */
/* Tool Implementations - Phase 1
 *
 * THREAD SAFETY NOTICE - PHASE 1 LIMITATION:
 *
 * These Phase 1 tool implementations directly access CPU registers and memory
 * without synchronization. They do NOT check if emulation is paused and do NOT
 * use VICE's interrupt system (IK_MONITOR) for safe CPU state access.
 *
 * CONSEQUENCE: Calling these tools while the emulator is running can cause:
 *   - Race conditions (reading partially-updated state)
 *   - Incorrect results (registers changing mid-read)
 *   - Undefined behavior (concurrent memory access)
 *
 * REQUIREMENT: Callers MUST ensure emulation is paused before invoking Phase 1
 * tools. The MCP transport layer or client application is responsible for this.
 *
 * FUTURE: Phase 2 will integrate with VICE's monitor/interrupt.h to properly
 * pause execution, access state safely via IK_MONITOR, and resume execution.
 * Phase 2 will also add execution control tools (run/pause/step).
 */

cJSON* mcp_tool_ping(cJSON *params)
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

cJSON* mcp_tool_execution_run(cJSON *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.run");

    /* TODO: Implement using VICE's interrupt system (IK_MONITOR) */
    /* This requires integration with monitor/interrupt.h */
    /* For now, return unimplemented error */

    return mcp_error(MCP_ERROR_NOT_IMPLEMENTED, "Execution control not yet implemented - use VICE monitor");
}

cJSON* mcp_tool_execution_pause(cJSON *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.pause");

    /* TODO: Implement using VICE's interrupt system (IK_MONITOR) */
    /* This requires integration with monitor/interrupt.h */
    /* For now, return unimplemented error */

    return mcp_error(MCP_ERROR_NOT_IMPLEMENTED, "Execution control not yet implemented - use VICE monitor");
}

cJSON* mcp_tool_execution_step(cJSON *params)
{
    log_message(mcp_tools_log, "Handling vice.execution.step");

    /* TODO: Implement using VICE's step mechanism */
    /* This requires integration with monitor code */
    /* For now, return unimplemented error */

    return mcp_error(MCP_ERROR_NOT_IMPLEMENTED, "Execution control not yet implemented - use VICE monitor");
}

cJSON* mcp_tool_registers_get(cJSON *params)
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

    /* Add CPU status flags */
    cJSON_AddBoolToObject(response, "N", MOS6510_REGS_GET_SIGN(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "V", MOS6510_REGS_GET_OVERFLOW(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "B", MOS6510_REGS_GET_BREAK(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "D", MOS6510_REGS_GET_DECIMAL(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "I", MOS6510_REGS_GET_INTERRUPT(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "Z", MOS6510_REGS_GET_ZERO(&maincpu_regs) != 0);
    cJSON_AddBoolToObject(response, "C", MOS6510_REGS_GET_CARRY(&maincpu_regs) != 0);

    return response;
}

cJSON* mcp_tool_registers_set(cJSON *params)
{
    cJSON *response;
    cJSON *register_item, *value_item;
    const char *register_name;
    int value;

    log_message(mcp_tools_log, "Handling vice.registers.set");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    register_item = cJSON_GetObjectItem(params, "register");
    value_item = cJSON_GetObjectItem(params, "value");

    if (!cJSON_IsString(register_item) || !cJSON_IsNumber(value_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Invalid parameter types");
    }

    register_name = register_item->valuestring;
    value = value_item->valueint;

    /* Set register using VICE APIs */
    if (strcmp(register_name, "PC") == 0) {
        if (value < 0 || value > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "PC value out of range (must be 0-65535)");
        }
        maincpu_set_pc(value);
    } else if (strcmp(register_name, "A") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "A value out of range (must be 0-255)");
        }
        maincpu_set_a(value);
    } else if (strcmp(register_name, "X") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "X value out of range (must be 0-255)");
        }
        maincpu_set_x(value);
    } else if (strcmp(register_name, "Y") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Y value out of range (must be 0-255)");
        }
        maincpu_set_y(value);
    } else if (strcmp(register_name, "SP") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "SP value out of range (must be 0-255)");
        }
        maincpu_set_sp(value);
    } else if (strcmp(register_name, "N") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "N flag value out of range (must be 0 or 1)");
        }
        maincpu_set_sign(value);
    } else if (strcmp(register_name, "V") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "V flag value out of range (must be 0 or 1)");
        }
        maincpu_set_overflow(value);
    } else if (strcmp(register_name, "B") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "B flag value out of range (must be 0 or 1)");
        }
        maincpu_set_break(value);
    } else if (strcmp(register_name, "D") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "D flag value out of range (must be 0 or 1)");
        }
        maincpu_set_decimal(value);
    } else if (strcmp(register_name, "I") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "I flag value out of range (must be 0 or 1)");
        }
        maincpu_set_interrupt(value);
    } else if (strcmp(register_name, "Z") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Z flag value out of range (must be 0 or 1)");
        }
        maincpu_set_zero(value);
    } else if (strcmp(register_name, "C") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "C flag value out of range (must be 0 or 1)");
        }
        maincpu_set_carry(value);
    } else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown register name (must be PC, A, X, Y, SP, N, V, B, D, I, Z, or C)");
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "register", register_name);
    cJSON_AddNumberToObject(response, "value", value);

    return response;
}

/* Read memory with automatic 64KB address space wrapping.
 *
 * The 6502/6510 CPU has a 16-bit address space (0x0000-0xFFFF), so all
 * memory addresses automatically wrap at the 64KB boundary. If a read
 * operation crosses 0xFFFF, it continues from 0x0000.
 *
 * For example: reading 4 bytes from 0xFFFE will return bytes at addresses
 * 0xFFFE, 0xFFFF, 0x0000, 0x0001 (in that order).
 */
cJSON* mcp_tool_memory_read(cJSON *params)
{
    cJSON *response, *data_array;
    cJSON *addr_item, *size_item;
    uint16_t address;
    unsigned int i; /* Changed from uint16_t to prevent infinite loop */
    int size;
    uint8_t value;
    char hex_str[3];

    log_message(mcp_tools_log, "Handling vice.memory.read");

    /* Extract address and size from params */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    size_item = cJSON_GetObjectItem(params, "size");

    if (!cJSON_IsNumber(addr_item) || !cJSON_IsNumber(size_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Invalid parameter types");
    }

    /* Validate address range */
    if (addr_item->valueint < 0 || addr_item->valueint > 0xFFFF) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Address out of range (must be 0-65535)");
    }

    address = (uint16_t)addr_item->valueint;
    size = size_item->valueint;

    /* Limit size to prevent infinite loop and excessive memory */
    if (size < 1 || size > 65535) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Size out of range (must be 1-65535)");
    }

    /* Build JSON response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "address", address);
    cJSON_AddNumberToObject(response, "size", size);

    data_array = cJSON_CreateArray();
    if (data_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Read memory with explicit wrapping at 64KB boundary */
    for (i = 0; i < (unsigned int)size; i++) {
        cJSON *hex_item;
        uint16_t addr;

        addr = (uint16_t)(address + i);  /* Wrap at 64KB */
        value = mem_read(addr);
        snprintf(hex_str, sizeof(hex_str), "%02X", value);

        hex_item = cJSON_CreateString(hex_str);
        if (hex_item == NULL) {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddItemToArray(data_array, hex_item);
    }

    cJSON_AddItemToObject(response, "data", data_array);

    return response;
}

/* Write memory with automatic 64KB address space wrapping.
 *
 * The 6502/6510 CPU has a 16-bit address space (0x0000-0xFFFF), so all
 * memory addresses automatically wrap at the 64KB boundary. If a write
 * operation crosses 0xFFFF, it continues from 0x0000.
 *
 * For example: writing 4 bytes to 0xFFFE will write to addresses
 * 0xFFFE, 0xFFFF, 0x0000, 0x0001 (in that order).
 */
cJSON* mcp_tool_memory_write(cJSON *params)
{
    cJSON *response;
    cJSON *addr_item, *data_item, *value_item;
    uint16_t address;
    int i, array_size;
    uint8_t byte_val;

    log_message(mcp_tools_log, "Handling vice.memory.write");

    /* Extract address and data from params */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    data_item = cJSON_GetObjectItem(params, "data");

    if (!cJSON_IsNumber(addr_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid address parameter");
    }

    if (!cJSON_IsArray(data_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid data parameter (must be array)");
    }

    /* Validate address range */
    if (addr_item->valueint < 0 || addr_item->valueint > 0xFFFF) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Address out of range (must be 0-65535)");
    }

    address = (uint16_t)addr_item->valueint;
    array_size = cJSON_GetArraySize(data_item);

    if (array_size < 1 || array_size > 65535) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Data array size out of range (must be 1-65535)");
    }

    /* Write to memory with validation */
    for (i = 0; i < array_size; i++) {
        value_item = cJSON_GetArrayItem(data_item, i);

        if (!cJSON_IsNumber(value_item)) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Data array must contain only numbers");
        }

        /* Validate byte range */
        if (value_item->valueint < 0 || value_item->valueint > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Byte values must be 0-255");
        }

        byte_val = (uint8_t)value_item->valueint;
        mem_store((uint16_t)(address + i), byte_val);
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
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
