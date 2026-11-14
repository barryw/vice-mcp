/*
 * mcp_tools_coverage.c - MCP execution coverage tool handlers
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

#include "machine.h"
#include "mcp_server.h"

/* One byte per address, set by the main CPU core each time it fetches an
 * opcode there (6510core.c, 6510dtvcore.c) while mcp_coverage_map points
 * at it. A byte rather than a bit keeps the cost per instruction to a test
 * and a store, whatever the number of addresses watched; an exec checkpoint
 * per address costs a list walk per instruction instead. Handlers run with
 * the CPU stopped (in a trap, under the mainlock or in the monitor), so the
 * map is never read or cleared while the CPU writes it. */
static uint8_t coverage[0x10000];

uint8_t *mcp_coverage_map = NULL;

static cJSON *coverage_state(void)
{
    cJSON *response = cJSON_CreateObject();

    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddBoolToObject(response, "enabled", mcp_coverage_map != NULL);
    return response;
}

cJSON *mcp_tool_coverage_start(cJSON *params)
{
    (void)params;

    if (machine_class == VICE_MACHINE_SCPU64) {
        return mcp_error(MCP_ERROR_NOT_IMPLEMENTED,
                         "Coverage records the 6502-family main CPU; "
                         "xscpu64's 65816 is not recorded");
    }
    memset(coverage, 0, sizeof(coverage));
    mcp_coverage_map = coverage;
    return coverage_state();
}

cJSON *mcp_tool_coverage_stop(cJSON *params)
{
    (void)params;

    mcp_coverage_map = NULL;
    return coverage_state();
}

/* Returns the whole map as a bitmap, or the executed addresses of a range
 * when start or end is given; reset clears the map after reading it. */
cJSON *mcp_tool_coverage_read(cJSON *params)
{
    cJSON *start_item = params ? cJSON_GetObjectItem(params, "start") : NULL;
    cJSON *end_item = params ? cJSON_GetObjectItem(params, "end") : NULL;
    cJSON *reset_item = params ? cJSON_GetObjectItem(params, "reset") : NULL;
    cJSON *response;
    const char *error_msg = NULL;
    int start = 0;
    int end = 0xffff;
    int count = 0;
    int addr;

    if (start_item != NULL) {
        start = mcp_resolve_address(start_item, &error_msg);
    }
    if (start >= 0 && end_item != NULL) {
        end = mcp_resolve_address(end_item, &error_msg);
    }
    if (start < 0 || end < 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, error_msg);
    }
    if (start > end) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start must not be after end");
    }
    if (reset_item != NULL && !cJSON_IsBool(reset_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "reset must be a boolean");
    }

    response = coverage_state();
    if (cJSON_GetObjectItem(response, "code") != NULL) {
        return response;
    }

    for (addr = 0; addr <= 0xffff; addr++) {
        count += coverage[addr];
    }
    cJSON_AddNumberToObject(response, "count", count);

    if (start_item != NULL || end_item != NULL) {
        cJSON *addresses = cJSON_AddArrayToObject(response, "addresses");

        cJSON_AddNumberToObject(response, "start", start);
        cJSON_AddNumberToObject(response, "end", end);
        for (addr = start; addr <= end && addresses != NULL; addr++) {
            if (coverage[addr]) {
                cJSON_AddItemToArray(addresses, cJSON_CreateNumber(addr));
            }
        }
    } else {
        uint8_t bits[0x10000 / 8];
        char *encoded;

        memset(bits, 0, sizeof(bits));
        for (addr = 0; addr <= 0xffff; addr++) {
            if (coverage[addr]) {
                bits[addr >> 3] |= (uint8_t)(1 << (addr & 7));
            }
        }
        encoded = mcp_base64_encode(bits, sizeof(bits), NULL);
        if (encoded == NULL) {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        cJSON_AddStringToObject(response, "bitmap", encoded);
        lib_free(encoded);
    }

    if (cJSON_IsTrue(reset_item)) {
        memset(coverage, 0, sizeof(coverage));
    }
    return response;
}
