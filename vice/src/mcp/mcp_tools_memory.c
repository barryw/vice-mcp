/*
 * mcp_tools_memory.c - MCP memory operation tool handlers
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
#include "mem.h"
#include "monitor.h"  /* For mon_symbol_table_lookup_name */
#include "util.h"

/* =========================================================================
 * Memory Read/Write Tools
 * ========================================================================= */

/* Read memory with automatic 64KB address space wrapping.
 *
 * The 6502/6510 CPU has a 16-bit address space (0x0000-0xFFFF), so all
 * memory addresses automatically wrap at the 64KB boundary. If a read
 * operation crosses 0xFFFF, it continues from 0x0000.
 *
 * For example: reading 4 bytes from 0xFFFE will return bytes at addresses
 * 0xFFFE, 0xFFFF, 0x0000, 0x0001 (in that order).
 *
 * Optional "bank" parameter allows reading from specific memory banks
 * (e.g., "ram" to read RAM under ROM, "io" to read I/O space).
 * Use vice.memory.banks to list available bank names.
 */
cJSON* mcp_tool_memory_read(cJSON *params)
{
    cJSON *response, *data_array;
    cJSON *addr_item, *size_item, *bank_item;
    uint16_t address;
    unsigned int i;
    int size;
    int bank = -1;  /* -1 = use default CPU view */
    uint8_t value;
    char hex_str[3];
    const char *bank_name = NULL;
    const char *error_msg;
    int resolved;

    log_message(mcp_tools_log, "Handling vice.memory.read");

    /* Extract address and size from params */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    size_item = cJSON_GetObjectItem(params, "size");
    bank_item = cJSON_GetObjectItem(params, "bank");

    /* Resolve address - can be number, hex string, or symbol */
    if (addr_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "address required");
    }
    resolved = mcp_resolve_address(addr_item, &error_msg);
    if (resolved < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }
    address = (uint16_t)resolved;

    if (!cJSON_IsNumber(size_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "size required (number)");
    }
    size = size_item->valueint;

    /* Validate size early - prevents negative values from integer issues */
    if (size < 1 || size > 65535) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Size out of range (must be 1-65535)");
    }

    /* Handle optional bank parameter */
    if (bank_item != NULL) {
        if (cJSON_IsString(bank_item)) {
            bank_name = bank_item->valuestring;
            /* Validate bank name length to prevent buffer issues */
            if (strlen(bank_name) > 63) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Bank name too long (max 63 characters)");
            }
            bank = mem_bank_from_name(bank_name);
            if (bank < 0) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown bank name (use vice.memory.banks to list available banks)");
            }
        } else if (cJSON_IsNumber(bank_item)) {
            int bank_val = bank_item->valueint;
            if (bank_val < 0 || bank_val > 255) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Bank number out of range (must be 0-255)");
            }
            bank = bank_val;
        } else {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Bank must be a string name or number");
        }
    }

    /* Build JSON response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "address", address);
    cJSON_AddNumberToObject(response, "size", size);
    if (bank >= 0) {
        cJSON_AddNumberToObject(response, "bank", bank);
        if (bank_name != NULL) {
            cJSON_AddStringToObject(response, "bank_name", bank_name);
        }
    }

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

        /* Use bank-specific peek to avoid side effects on HW registers
         * (e.g. VIC-II collision regs cleared on read, CIA timer latches,
         * SID write-only regs).  Bank 0 = default CPU-visible mapping. */
        if (bank >= 0) {
            value = mem_bank_peek(bank, addr, NULL);
        } else {
            value = mem_bank_peek(0, addr, NULL);
        }
        snprintf(hex_str, sizeof(hex_str), "%02X", value);

        hex_item = cJSON_CreateString(hex_str);
        if (hex_item == NULL) {
            cJSON_Delete(data_array);
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
    cJSON *addr_item;
    cJSON *data_item;
    cJSON *value_item;
    uint16_t address;
    int i;
    int array_size;
    uint8_t byte_val;
    const char *error_msg;
    int resolved;

    log_message(mcp_tools_log, "Handling vice.memory.write");

    /* Extract address and data from params */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    addr_item = cJSON_GetObjectItem(params, "address");
    data_item = cJSON_GetObjectItem(params, "data");

    /* Resolve address - can be number, hex string, or symbol */
    if (addr_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "address required");
    }
    resolved = mcp_resolve_address(addr_item, &error_msg);
    if (resolved < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }
    address = (uint16_t)resolved;

    if (!cJSON_IsArray(data_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid data parameter (must be array)");
    }

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

/* List available memory banks (RAM, ROM, IO, etc.)
 *
 * Returns the list of memory banks available for the current machine.
 * Bank names can be used with the 'bank' parameter of vice.memory.read
 * to access specific memory areas (e.g., reading RAM under ROM).
 */
cJSON* mcp_tool_memory_banks(cJSON *params)
{
    cJSON *response, *banks_array;
    const char **bank_names;
    const int *bank_numbers;
    int i;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.memory.banks");

    /* Get bank list from VICE */
    bank_names = mem_bank_list();
    bank_numbers = mem_bank_list_nos();

    if (bank_names == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Memory bank list not available");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    banks_array = cJSON_CreateArray();
    if (banks_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add each bank to the array */
    for (i = 0; bank_names[i] != NULL; i++) {
        cJSON *bank_obj = cJSON_CreateObject();
        if (bank_obj == NULL) {
            cJSON_Delete(banks_array);
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddStringToObject(bank_obj, "name", bank_names[i]);
        if (bank_numbers != NULL) {
            cJSON_AddNumberToObject(bank_obj, "number", bank_numbers[i]);
        }
        cJSON_AddItemToArray(banks_array, bank_obj);
    }

    cJSON_AddItemToObject(response, "banks", banks_array);
    cJSON_AddStringToObject(response, "machine", machine_get_name());

    return response;
}

/* =========================================================================
 * Memory Search Tool
 * ========================================================================= */

/* Search for byte patterns in memory with optional wildcard masks.
 *
 * Parameters:
 *   start: Start address (required) - number, hex string, or symbol
 *   end: End address (required) - number, hex string, or symbol
 *   pattern: Array of bytes to search for (required)
 *   mask: Per-byte mask array (optional) - 0xFF=exact match, 0x00=wildcard
 *   max_results: Maximum matches to return (optional, default 100)
 *
 * Returns:
 *   matches: Array of addresses where pattern was found (as hex strings)
 *   total_matches: Total number of matches found
 *   truncated: True if results were limited by max_results
 */
cJSON* mcp_tool_memory_search(cJSON *params)
{
    cJSON *response, *matches_array;
    cJSON *start_item, *end_item, *pattern_item, *mask_item, *max_item;
    int start_addr;
    int end_addr;
    int pattern_len;
    int mask_len;
    uint8_t *pattern_buf = NULL;
    uint8_t *mask_buf = NULL;
    int max_results = 100;  /* Default max results */
    int total_matches = 0;
    int results_returned = 0;
    long search_len;
    long search_idx;
    int i;  /* for array indexing in cJSON functions */
    const char *error_msg;
    char addr_str[8];

    log_message(mcp_tools_log, "Handling vice.memory.search");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get and validate start address */
    start_item = cJSON_GetObjectItem(params, "start");
    if (start_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start address required");
    }
    start_addr = mcp_resolve_address(start_item, &error_msg);
    if (start_addr < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }

    /* Get and validate end address */
    end_item = cJSON_GetObjectItem(params, "end");
    if (end_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "end address required");
    }
    end_addr = mcp_resolve_address(end_item, &error_msg);
    if (end_addr < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }

    /* Validate range */
    if (end_addr < start_addr) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "end address must be >= start address");
    }

    /* Get and validate pattern */
    pattern_item = cJSON_GetObjectItem(params, "pattern");
    if (pattern_item == NULL || !cJSON_IsArray(pattern_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern required (array of bytes)");
    }
    pattern_len = cJSON_GetArraySize(pattern_item);
    if (pattern_len == 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern cannot be empty");
    }
    if (pattern_len > 256) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern too long (max 256 bytes)");
    }

    /* Allocate and fill pattern buffer */
    pattern_buf = lib_malloc(pattern_len);
    if (pattern_buf == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    for (i = 0; i < pattern_len; i++) {
        cJSON *byte_item = cJSON_GetArrayItem(pattern_item, i);
        if (!cJSON_IsNumber(byte_item)) {
            lib_free(pattern_buf);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern must contain numbers only");
        }
        pattern_buf[i] = (uint8_t)(byte_item->valueint & 0xFF);
    }

    /* Get optional mask (default: all 0xFF for exact match) */
    mask_buf = lib_malloc(pattern_len);
    if (mask_buf == NULL) {
        lib_free(pattern_buf);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    mask_item = cJSON_GetObjectItem(params, "mask");
    if (mask_item != NULL && cJSON_IsArray(mask_item)) {
        mask_len = cJSON_GetArraySize(mask_item);
        if (mask_len != pattern_len) {
            lib_free(pattern_buf);
            lib_free(mask_buf);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "mask length must match pattern length");
        }
        for (i = 0; i < mask_len; i++) {
            cJSON *byte_item = cJSON_GetArrayItem(mask_item, i);
            if (!cJSON_IsNumber(byte_item)) {
                lib_free(pattern_buf);
                lib_free(mask_buf);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "mask must contain numbers only");
            }
            mask_buf[i] = (uint8_t)(byte_item->valueint & 0xFF);
        }
    } else {
        /* Default: exact match for all bytes */
        for (i = 0; i < pattern_len; i++) {
            mask_buf[i] = 0xFF;
        }
    }

    /* Get optional max_results */
    max_item = cJSON_GetObjectItem(params, "max_results");
    if (max_item != NULL && cJSON_IsNumber(max_item)) {
        max_results = max_item->valueint;
        if (max_results < 1) max_results = 1;
        if (max_results > 10000) max_results = 10000;  /* Reasonable upper limit */
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(pattern_buf);
        lib_free(mask_buf);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    matches_array = cJSON_CreateArray();
    if (matches_array == NULL) {
        cJSON_Delete(response);
        lib_free(pattern_buf);
        lib_free(mask_buf);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Perform the search */
    search_len = (long)end_addr - (long)start_addr + 1;

    for (search_idx = 0; search_idx <= search_len - pattern_len; search_idx++) {
        int found = 1;
        uint16_t check_addr = (uint16_t)(start_addr + search_idx);

        /* Check if pattern matches at this position */
        for (i = 0; i < pattern_len; i++) {
            uint8_t mem_byte = mem_bank_peek(0, (uint16_t)(check_addr + i), NULL);
            uint8_t masked_mem = mem_byte & mask_buf[i];
            uint8_t masked_pattern = pattern_buf[i] & mask_buf[i];

            if (masked_mem != masked_pattern) {
                found = 0;
                break;
            }
        }

        if (found) {
            total_matches++;

            /* Add to results if under limit */
            if (results_returned < max_results) {
                cJSON *match_item;
                snprintf(addr_str, sizeof(addr_str), "$%04X", check_addr);
                match_item = cJSON_CreateString(addr_str);
                if (match_item != NULL) {
                    cJSON_AddItemToArray(matches_array, match_item);
                    results_returned++;
                }
            }
        }
    }

    /* Cleanup */
    lib_free(pattern_buf);
    lib_free(mask_buf);

    /* Build final response */
    cJSON_AddItemToObject(response, "matches", matches_array);
    cJSON_AddNumberToObject(response, "total_matches", total_matches);
    cJSON_AddBoolToObject(response, "truncated", results_returned < total_matches ? 1 : 0);

    return response;
}

/* =========================================================================
 * Memory Fill Tool
 * ========================================================================= */

/**
 * @brief Fill a memory range with a repeating byte pattern.
 *
 * Parameters:
 *   start: (required) Start address - number, hex string ($A000), or symbol
 *   end: (required) End address (inclusive) - number, hex string, or symbol
 *   pattern: (required) Array of bytes to repeat, e.g., [0] or [0xAA, 0x55]
 *
 * Returns:
 *   bytes_written: Total number of bytes written
 *   pattern_repetitions: Number of complete pattern repetitions
 *
 * Examples:
 *   Zero-fill: {"start": "$A000", "end": "$DFFF", "pattern": [0]}
 *   Screen spaces: {"start": "$0400", "end": "$07FF", "pattern": [32]}
 *   NOP sled: {"start": "$C000", "end": "$C0FF", "pattern": [0xEA]}
 *   Alternating: {"start": "$2000", "end": "$3FFF", "pattern": [0xAA, 0x55]}
 *
 * Use case: Clear memory regions, set up test conditions, patch code areas
 * without needing thousands of individual memory write API calls.
 */
cJSON* mcp_tool_memory_fill(cJSON *params)
{
    cJSON *response;
    cJSON *start_item, *end_item, *pattern_item;
    int start_addr;
    int end_addr;
    int pattern_len;
    int i;
    uint16_t addr;
    long bytes_to_write;
    long bytes_written = 0;
    long pattern_reps = 0;
    const char *error_msg;

    log_message(mcp_tools_log, "Handling vice.memory.fill");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get and validate start address */
    start_item = cJSON_GetObjectItem(params, "start");
    if (start_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start address required");
    }
    start_addr = mcp_resolve_address(start_item, &error_msg);
    if (start_addr < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }

    /* Get and validate end address */
    end_item = cJSON_GetObjectItem(params, "end");
    if (end_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "end address required");
    }
    end_addr = mcp_resolve_address(end_item, &error_msg);
    if (end_addr < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }

    /* Validate range */
    if (start_addr > end_addr) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start address must be <= end address");
    }

    /* Get and validate pattern */
    pattern_item = cJSON_GetObjectItem(params, "pattern");
    if (pattern_item == NULL || !cJSON_IsArray(pattern_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern array required");
    }
    pattern_len = cJSON_GetArraySize(pattern_item);
    if (pattern_len < 1) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern array must not be empty");
    }
    if (pattern_len > 256) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern array too large (max 256 bytes)");
    }

    /* Validate all pattern bytes before writing */
    for (i = 0; i < pattern_len; i++) {
        cJSON *byte_item = cJSON_GetArrayItem(pattern_item, i);
        if (!cJSON_IsNumber(byte_item)) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern array must contain only numbers");
        }
        if (byte_item->valueint < 0 || byte_item->valueint > 255) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "pattern byte values must be 0-255");
        }
    }

    /* Calculate bytes to write */
    bytes_to_write = (long)(end_addr - start_addr) + 1;

    /* Fill memory with repeating pattern */
    addr = (uint16_t)start_addr;
    for (bytes_written = 0; bytes_written < bytes_to_write; bytes_written++) {
        cJSON *byte_item = cJSON_GetArrayItem(pattern_item, (int)(bytes_written % pattern_len));
        uint8_t byte_val = (uint8_t)byte_item->valueint;
        mem_store(addr, byte_val);
        addr++;
    }

    /* Calculate complete pattern repetitions */
    pattern_reps = bytes_written / pattern_len;

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "bytes_written", (double)bytes_written);
    cJSON_AddNumberToObject(response, "pattern_repetitions", (double)pattern_reps);

    return response;
}

/* =========================================================================
 * Memory Compare Tool
 * ========================================================================= */

/* vice.memory.compare - Compare memory ranges or against snapshot
 *
 * Modes:
 *   "ranges"   - Compare two memory ranges byte-by-byte
 *   "snapshot" - Compare current memory against saved snapshot
 *
 * Parameters for mode="ranges":
 *   mode: "ranges" (required)
 *   range1_start: Start address of first range (required)
 *   range1_end: End address of first range (required)
 *   range2_start: Start address of second range (required)
 *   max_differences: Maximum differences to return (optional, default 100)
 *
 * Returns:
 *   differences: Array of {address, current, reference} for each difference
 *   total_differences: Total count of differences found
 *   truncated: True if differences array was limited by max_differences
 */
cJSON* mcp_tool_memory_compare(cJSON *params)
{
    cJSON *response, *differences_array;
    cJSON *mode_item, *range1_start_item, *range1_end_item, *range2_start_item;
    cJSON *max_diff_item;
    const char *mode_str;
    int range1_start;
    int range1_end;
    int range2_start;
    int max_differences = 100;  /* Default max differences to return */
    int total_differences = 0;
    int differences_returned = 0;
    long range_size;
    long offset;
    const char *error_msg;
    char addr_str[8];
    uint8_t byte1;
    uint8_t byte2;

    log_message(mcp_tools_log, "Handling vice.memory.compare");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get and validate mode */
    mode_item = cJSON_GetObjectItem(params, "mode");
    if (mode_item == NULL || !cJSON_IsString(mode_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "mode parameter required (string: 'ranges' or 'snapshot')");
    }
    mode_str = mode_item->valuestring;

    if (strcmp(mode_str, "ranges") == 0) {
        /* --- Mode: ranges - compare two memory ranges --- */

        /* Get and validate range1_start */
        range1_start_item = cJSON_GetObjectItem(params, "range1_start");
        if (range1_start_item == NULL) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "range1_start address required for ranges mode");
        }
        range1_start = mcp_resolve_address(range1_start_item, &error_msg);
        if (range1_start < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve range1_start: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }

        /* Get and validate range1_end */
        range1_end_item = cJSON_GetObjectItem(params, "range1_end");
        if (range1_end_item == NULL) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "range1_end address required for ranges mode");
        }
        range1_end = mcp_resolve_address(range1_end_item, &error_msg);
        if (range1_end < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve range1_end: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }

        /* Validate range1 order */
        if (range1_end < range1_start) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "range1_end must be >= range1_start");
        }

        /* Get and validate range2_start */
        range2_start_item = cJSON_GetObjectItem(params, "range2_start");
        if (range2_start_item == NULL) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "range2_start address required for ranges mode");
        }
        range2_start = mcp_resolve_address(range2_start_item, &error_msg);
        if (range2_start < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve range2_start: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }

        /* Get optional max_differences */
        max_diff_item = cJSON_GetObjectItem(params, "max_differences");
        if (max_diff_item != NULL && cJSON_IsNumber(max_diff_item)) {
            max_differences = max_diff_item->valueint;
            if (max_differences < 1) {
                max_differences = 1;
            }
            if (max_differences > 10000) {
                max_differences = 10000;
            }
        }

        /* Calculate range size */
        range_size = (long)(range1_end - range1_start) + 1;

        if ((long)range2_start + range_size - 1 > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Second range exceeds address space");
        }

        /* Create differences array */
        differences_array = cJSON_CreateArray();
        if (differences_array == NULL) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Compare byte-by-byte */
        for (offset = 0; offset < range_size; offset++) {
            uint16_t addr1 = (uint16_t)(range1_start + offset);
            uint16_t addr2 = (uint16_t)(range2_start + offset);

            byte1 = mem_bank_peek(0, addr1, NULL);
            byte2 = mem_bank_peek(0, addr2, NULL);

            if (byte1 != byte2) {
                total_differences++;

                /* Only add to array if under limit */
                if (differences_returned < max_differences) {
                    cJSON *diff_obj = cJSON_CreateObject();
                    if (diff_obj != NULL) {
                        /* Format address as hex string relative to range1 */
                        snprintf(addr_str, sizeof(addr_str), "$%04X", addr1);
                        cJSON_AddStringToObject(diff_obj, "address", addr_str);
                        cJSON_AddNumberToObject(diff_obj, "current", byte1);
                        cJSON_AddNumberToObject(diff_obj, "reference", byte2);
                        cJSON_AddItemToArray(differences_array, diff_obj);
                        differences_returned++;
                    }
                }
            }
        }

        /* Build response */
        response = cJSON_CreateObject();
        if (response == NULL) {
            cJSON_Delete(differences_array);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddItemToObject(response, "differences", differences_array);
        cJSON_AddNumberToObject(response, "total_differences", total_differences);
        cJSON_AddBoolToObject(response, "truncated", total_differences > differences_returned);

        return response;

    } else if (strcmp(mode_str, "snapshot") == 0) {
        /* --- Mode: snapshot - compare against saved snapshot --- */
        cJSON *snapshot_name_item, *start_item, *end_item;
        const char *snapshot_name;
        char *snapshots_dir;
        char *vsf_path;
        uint8_t *snapshot_ram;
        int start_addr;
        int end_addr;
        int vsf_result;

        /* Get and validate snapshot_name */
        snapshot_name_item = cJSON_GetObjectItem(params, "snapshot_name");
        if (snapshot_name_item == NULL || !cJSON_IsString(snapshot_name_item)) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "snapshot_name parameter required for snapshot mode");
        }
        snapshot_name = snapshot_name_item->valuestring;

        /* Validate name format */
        if (snapshot_name[0] == '\0') {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "snapshot_name cannot be empty");
        }
        {
            const char *p;
            for (p = snapshot_name; *p; p++) {
                if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
                    return mcp_error(MCP_ERROR_INVALID_PARAMS,
                        "Invalid snapshot name: use only alphanumeric characters, underscores, and hyphens");
                }
            }
        }

        /* Get and validate start address */
        start_item = cJSON_GetObjectItem(params, "start");
        if (start_item == NULL) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "start address required for snapshot mode");
        }
        start_addr = mcp_resolve_address(start_item, &error_msg);
        if (start_addr < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }

        /* Get and validate end address */
        end_item = cJSON_GetObjectItem(params, "end");
        if (end_item == NULL) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "end address required for snapshot mode");
        }
        end_addr = mcp_resolve_address(end_item, &error_msg);
        if (end_addr < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }

        /* Validate range order */
        if (end_addr < start_addr) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "end address must be >= start address");
        }

        /* Get optional max_differences */
        max_diff_item = cJSON_GetObjectItem(params, "max_differences");
        if (max_diff_item != NULL && cJSON_IsNumber(max_diff_item)) {
            max_differences = max_diff_item->valueint;
            if (max_differences < 1) {
                max_differences = 1;
            }
            if (max_differences > 10000) {
                max_differences = 10000;
            }
        }

        /* Build path to snapshot file */
        snapshots_dir = mcp_get_snapshots_dir();
        if (snapshots_dir == NULL) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR,
                "Failed to access snapshots directory");
        }

        vsf_path = util_join_paths(snapshots_dir, snapshot_name, NULL);
        lib_free(snapshots_dir);
        if (vsf_path == NULL) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to build snapshot path");
        }

        /* Add .vsf extension */
        {
            size_t len = strlen(vsf_path);
            char *full_path = lib_malloc(len + 5);  /* +5 for ".vsf\0" */
            if (full_path == NULL) {
                lib_free(vsf_path);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            snprintf(full_path, len + 5, "%s.vsf", vsf_path);
            lib_free(vsf_path);
            vsf_path = full_path;
        }

        /* Allocate buffer for snapshot RAM */
        snapshot_ram = lib_malloc(C64_RAM_SIZE);
        if (snapshot_ram == NULL) {
            lib_free(vsf_path);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Extract RAM from snapshot file */
        vsf_result = vsf_extract_c64_ram(vsf_path, snapshot_ram);
        if (vsf_result != 0) {
            lib_free(vsf_path);
            lib_free(snapshot_ram);
            switch (vsf_result) {
                case -1:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "Cannot open snapshot file - does it exist?");
                case -2:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "Invalid snapshot file format");
                case -3:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "Snapshot is not for C64 machine");
                case -4:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "C64MEM module not found in snapshot");
                case -5:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "Snapshot C64MEM module is corrupted");
                default:
                    return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
                        "Failed to read snapshot file");
            }
        }

        /* Create differences array */
        differences_array = cJSON_CreateArray();
        if (differences_array == NULL) {
            lib_free(vsf_path);
            lib_free(snapshot_ram);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Calculate range size */
        range_size = (long)(end_addr - start_addr) + 1;

        /* Compare byte-by-byte: current memory vs snapshot */
        for (offset = 0; offset < range_size; offset++) {
            uint16_t addr = (uint16_t)(start_addr + offset);

            byte1 = mem_bank_peek(0, addr, NULL);  /* Current memory (non-destructive) */
            byte2 = snapshot_ram[addr];   /* Snapshot memory */

            if (byte1 != byte2) {
                total_differences++;

                /* Only add to array if under limit */
                if (differences_returned < max_differences) {
                    cJSON *diff_obj = cJSON_CreateObject();
                    if (diff_obj != NULL) {
                        snprintf(addr_str, sizeof(addr_str), "$%04X", addr);
                        cJSON_AddStringToObject(diff_obj, "address", addr_str);
                        cJSON_AddNumberToObject(diff_obj, "current", byte1);
                        cJSON_AddNumberToObject(diff_obj, "reference", byte2);
                        cJSON_AddItemToArray(differences_array, diff_obj);
                        differences_returned++;
                    }
                }
            }
        }

        lib_free(vsf_path);
        lib_free(snapshot_ram);

        /* Build response */
        response = cJSON_CreateObject();
        if (response == NULL) {
            cJSON_Delete(differences_array);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddItemToObject(response, "differences", differences_array);
        cJSON_AddNumberToObject(response, "total_differences", total_differences);
        cJSON_AddBoolToObject(response, "truncated", total_differences > differences_returned);

        return response;

    } else {
        /* Unknown mode */
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Unknown mode '%s' (valid: 'ranges', 'snapshot')", mode_str);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }
}

/* =========================================================================
 * Memory Map Tool
 * ========================================================================= */

/* C64 memory region types */
typedef enum mcp_mem_region_type_e {
    MCP_MEM_TYPE_RAM,
    MCP_MEM_TYPE_ROM,
    MCP_MEM_TYPE_IO,
    MCP_MEM_TYPE_UNMAPPED,
    MCP_MEM_TYPE_CARTRIDGE
} mcp_mem_region_type_t;

/* C64 memory region definition */
typedef struct mcp_mem_region_s {
    uint16_t start;
    uint16_t end;
    mcp_mem_region_type_t type;
    const char *name;
    int bank;
} mcp_mem_region_t;

/* Static C64 memory map (default configuration with BASIC + KERNAL ROMs)
 * This is a simplified view. The actual C64 has a more complex banking system,
 * but this covers the standard layout. */
static const mcp_mem_region_t c64_memory_map[] = {
    { 0x0000, 0x00FF, MCP_MEM_TYPE_RAM,  "Zero Page",         0 },
    { 0x0100, 0x01FF, MCP_MEM_TYPE_RAM,  "Stack",             0 },
    { 0x0200, 0x03FF, MCP_MEM_TYPE_RAM,  "BASIC Work Area",   0 },
    { 0x0400, 0x07FF, MCP_MEM_TYPE_RAM,  "Screen Memory",     0 },
    { 0x0800, 0x9FFF, MCP_MEM_TYPE_RAM,  "BASIC Program Area",0 },
    { 0xA000, 0xBFFF, MCP_MEM_TYPE_ROM,  "BASIC ROM",         0 },
    { 0xC000, 0xCFFF, MCP_MEM_TYPE_RAM,  "Upper RAM",         0 },
    { 0xD000, 0xD3FF, MCP_MEM_TYPE_IO,   "VIC-II",            0 },
    { 0xD400, 0xD7FF, MCP_MEM_TYPE_IO,   "SID",               0 },
    { 0xD800, 0xDBFF, MCP_MEM_TYPE_IO,   "Color RAM",         0 },
    { 0xDC00, 0xDCFF, MCP_MEM_TYPE_IO,   "CIA 1",             0 },
    { 0xDD00, 0xDDFF, MCP_MEM_TYPE_IO,   "CIA 2",             0 },
    { 0xDE00, 0xDEFF, MCP_MEM_TYPE_IO,   "I/O 1 (Expansion)", 0 },
    { 0xDF00, 0xDFFF, MCP_MEM_TYPE_IO,   "I/O 2 (Expansion)", 0 },
    { 0xE000, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM",        0 },
    { 0, 0, 0, NULL, 0 }  /* Sentinel */
};

static const char* mcp_mem_type_to_string(mcp_mem_region_type_t type)
{
    switch (type) {
        case MCP_MEM_TYPE_RAM:       return "ram";
        case MCP_MEM_TYPE_ROM:       return "rom";
        case MCP_MEM_TYPE_IO:        return "io";
        case MCP_MEM_TYPE_UNMAPPED:  return "unmapped";
        case MCP_MEM_TYPE_CARTRIDGE: return "cartridge";
        default:                     return "unknown";
    }
}

/* Build content hint string by scanning symbol table for addresses in range.
 * Returns allocated string (caller must free) or NULL if no symbols found. */
static char* mcp_build_content_hint(uint16_t start, uint16_t end, int max_symbols)
{
    char *result = NULL;
    char *symbols_found[32];  /* Max symbols to include in hint */
    int symbol_count = 0;
    int i;
    int addr;
    char *name;
    size_t total_len;
    char *p;

    /* Scan address range for symbols */
    for (addr = start; addr <= end && symbol_count < max_symbols && symbol_count < 32; addr++) {
        name = mon_symbol_table_lookup_name(e_comp_space, (uint16_t)addr);
        if (name != NULL) {
            symbols_found[symbol_count++] = name;
        }
    }

    if (symbol_count == 0) {
        return NULL;
    }

    /* Calculate total string length needed */
    total_len = 0;
    for (i = 0; i < symbol_count; i++) {
        total_len += strlen(symbols_found[i]) + 2;  /* +2 for ", " separator */
    }

    /* Allocate and build result string */
    result = lib_malloc(total_len + 1);
    if (result == NULL) {
        return NULL;
    }

    p = result;
    for (i = 0; i < symbol_count; i++) {
        if (i > 0) {
            *p++ = ',';
            *p++ = ' ';
        }
        strcpy(p, symbols_found[i]);
        p += strlen(symbols_found[i]);
    }
    *p = '\0';

    return result;
}

/* Display memory region layout with optional symbol-based content hints.
 *
 * Parameters:
 *   start: Start address (optional, default $0000) - number or hex string
 *   end: End address (optional, default $FFFF) - number or hex string
 *   granularity: Minimum region size (optional, default 256) - affects merging
 *
 * Returns:
 *   regions: Array of memory region objects with:
 *     - start: Hex address string
 *     - end: Hex address string
 *     - type: "ram", "rom", "io", "unmapped", "cartridge"
 *     - name: Human-readable region name
 *     - bank: Memory bank number
 *     - contents_hint: Symbol names found in region (or null)
 */
cJSON* mcp_tool_memory_map(cJSON *params)
{
    cJSON *response, *regions_array, *region_obj;
    cJSON *start_item, *end_item, *granularity_item;
    int start_addr = 0x0000;
    int end_addr = 0xFFFF;
    int granularity = 256;
    int resolved;
    const char *error_msg;
    const mcp_mem_region_t *region;
    int i;
    char addr_str[8];

    log_message(mcp_tools_log, "Handling vice.memory.map");

    /* Validate machine type - memory map is currently C64-specific */
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
            /* Supported - C64-compatible memory map */
            break;
        default:
            return mcp_error(-32000, "Memory map not available for this machine type (C64/C128/DTV only)");
    }

    /* Parse optional parameters */
    if (params != NULL) {
        start_item = cJSON_GetObjectItem(params, "start");
        if (start_item != NULL) {
            resolved = mcp_resolve_address(start_item, &error_msg);
            if (resolved < 0) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
            }
            start_addr = resolved;
        }

        end_item = cJSON_GetObjectItem(params, "end");
        if (end_item != NULL) {
            resolved = mcp_resolve_address(end_item, &error_msg);
            if (resolved < 0) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
            }
            end_addr = resolved;
        }

        granularity_item = cJSON_GetObjectItem(params, "granularity");
        if (granularity_item != NULL && cJSON_IsNumber(granularity_item)) {
            granularity = granularity_item->valueint;
            if (granularity < 1) granularity = 1;
            if (granularity > 0xFFFF) granularity = 0xFFFF;
        }
    }

    /* Validate range */
    if (end_addr < start_addr) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "end address must be >= start address");
    }

    /* Create response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    regions_array = cJSON_CreateArray();
    if (regions_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Iterate over memory map regions */
    for (i = 0; c64_memory_map[i].name != NULL; i++) {
        uint16_t region_start;
        uint16_t region_end;
        char *content_hint;

        region = &c64_memory_map[i];

        /* Skip regions that don't overlap with requested range */
        if (region->end < start_addr || region->start > end_addr) {
            continue;
        }

        /* Clamp region to requested range */
        region_start = (region->start < start_addr) ? start_addr : region->start;
        region_end = (region->end > end_addr) ? end_addr : region->end;

        /* Skip regions smaller than granularity (unless they're the only region) */
        if ((int)(region_end - region_start + 1) < granularity &&
            cJSON_GetArraySize(regions_array) > 0) {
            continue;
        }

        /* Create region object */
        region_obj = cJSON_CreateObject();
        if (region_obj == NULL) {
            cJSON_Delete(regions_array);
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Add region fields */
        snprintf(addr_str, sizeof(addr_str), "$%04X", region_start);
        cJSON_AddStringToObject(region_obj, "start", addr_str);

        snprintf(addr_str, sizeof(addr_str), "$%04X", region_end);
        cJSON_AddStringToObject(region_obj, "end", addr_str);

        cJSON_AddStringToObject(region_obj, "type", mcp_mem_type_to_string(region->type));
        cJSON_AddStringToObject(region_obj, "name", region->name);
        cJSON_AddNumberToObject(region_obj, "bank", region->bank);

        /* Build content hint from symbols in this region */
        content_hint = mcp_build_content_hint(region_start, region_end, 5);
        if (content_hint != NULL) {
            cJSON_AddStringToObject(region_obj, "contents_hint", content_hint);
            lib_free(content_hint);
        } else {
            cJSON_AddNullToObject(region_obj, "contents_hint");
        }

        cJSON_AddItemToArray(regions_array, region_obj);
    }

    cJSON_AddItemToObject(response, "regions", regions_array);
    cJSON_AddNumberToObject(response, "region_count", cJSON_GetArraySize(regions_array));

    log_message(mcp_tools_log, "Memory map returned %d regions for range $%04X-$%04X",
                cJSON_GetArraySize(regions_array), (unsigned int)start_addr, (unsigned int)end_addr);

    return response;
}
