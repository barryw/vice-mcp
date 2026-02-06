/*
 * mcp_tools_execution.c - MCP execution control and register tool handlers
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

#include "mcp_tools_internal.h"

#include "maincpu.h"
#include "mos6510.h"
#include "monitor.h"  /* For mon_instructions_step/next, exit_mon */
#include "ui.h"       /* For ui_pause_enable/disable/active */

/* mcp_step_active state is defined in monitor.c to avoid a circular link
 * dependency between libmonitor.a and libmcp.a (GNU ld limitation).
 * Use mcp_set_step_active() / mcp_is_step_active() from monitor.h. */

/* =========================================================================
 * Execution Control Tools
 * ========================================================================= */

cJSON* mcp_tool_execution_run(cJSON *params)
{
    cJSON *response;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.execution.run");

    /* Handle both UI pause mode and monitor mode.
     * The emulator can be paused in two ways:
     * 1. UI pause - controlled by ui_pause_enable/disable
     * 2. Monitor mode - controlled by exit_mon variable
     * We need to handle both cases. */

    /* Disable UI pause if active */
    if (ui_pause_active()) {
        log_message(mcp_tools_log, "Disabling UI pause");
        ui_pause_disable();
    }

    /* Also signal monitor to exit if in monitor mode */
    exit_mon = exit_mon_continue;

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Execution resumed");

    return response;
}

cJSON* mcp_tool_execution_pause(cJSON *params)
{
    cJSON *response;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.execution.pause");

    /* Use UI pause to stop the emulator without entering monitor mode.
     * This keeps the emulator window visible (no monitor popup) while
     * still stopping CPU execution. The transport layer acquires the
     * mainlock when dispatching during UI pause for thread safety. */
    if (!ui_pause_active()) {
        log_message(mcp_tools_log, "Enabling UI pause");
        ui_pause_enable();
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Execution paused");

    return response;
}

cJSON* mcp_tool_execution_step(cJSON *params)
{
    cJSON *response;
    cJSON *count_item, *step_over_item;
    int count = 1;
    bool step_over = false;

    log_message(mcp_tools_log, "Handling vice.execution.step");

    /* Parse optional count parameter */
    if (params != NULL) {
        count_item = cJSON_GetObjectItem(params, "count");
        if (count_item != NULL && cJSON_IsNumber(count_item)) {
            count = count_item->valueint;
            if (count < 1) {
                count = 1;
            }
        }

        step_over_item = cJSON_GetObjectItem(params, "stepOver");
        if (step_over_item != NULL && cJSON_IsBool(step_over_item)) {
            step_over = cJSON_IsTrue(step_over_item);
        }
    }

    /* Set MCP step mode flag - this tells monitor_check_icount() to use
     * ui_pause_enable() instead of monitor_startup() when stepping completes,
     * preventing the monitor window from opening during MCP operations. */
    mcp_set_step_active(1);

    /* Use VICE's step functions:
     * - mon_instructions_step: Step into subroutines
     * - mon_instructions_next: Step over subroutines */
    if (step_over) {
        mon_instructions_next(count);
    } else {
        mon_instructions_step(count);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "instructions", count);
    cJSON_AddBoolToObject(response, "step_over", step_over);

    return response;
}

/* =========================================================================
 * Register Access Tools
 * ========================================================================= */

cJSON* mcp_tool_registers_get(cJSON *params)
{
    cJSON *response;
    unsigned int pc;
    unsigned int a;
    unsigned int x;
    unsigned int y;
    unsigned int sp;

    (void)params;  /* Unused */

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

    /* Set register using VICE register access macros */
    if (strcmp(register_name, "PC") == 0) {
        if (value < 0 || value > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "PC value out of range (must be 0-65535)");
        }
        MOS6510_REGS_SET_PC(&maincpu_regs, (uint16_t)value);
    } else if (strcmp(register_name, "A") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "A value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_A(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "X") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "X value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_X(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "Y") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Y value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_Y(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "SP") == 0) {
        if (value < 0 || value > 0xFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "SP value out of range (must be 0-255)");
        }
        MOS6510_REGS_SET_SP(&maincpu_regs, (uint8_t)value);
    } else if (strcmp(register_name, "V") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "V flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_OVERFLOW(&maincpu_regs, value);
    } else if (strcmp(register_name, "B") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "B flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_BREAK(&maincpu_regs, value);
    } else if (strcmp(register_name, "D") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "D flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_DECIMAL(&maincpu_regs, value);
    } else if (strcmp(register_name, "I") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "I flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_INTERRUPT(&maincpu_regs, value);
    } else if (strcmp(register_name, "C") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "C flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_CARRY(&maincpu_regs, value);
    } else if (strcmp(register_name, "N") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "N flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_SIGN(&maincpu_regs, value);
    } else if (strcmp(register_name, "Z") == 0) {
        if (value < 0 || value > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Z flag value out of range (must be 0 or 1)");
        }
        MOS6510_REGS_SET_ZERO(&maincpu_regs, value);
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

/* =========================================================================
 * Notification Functions (SSE Events)
 * ========================================================================= */

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
