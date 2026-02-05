/*
 * mcp_tools_debug.c - MCP debugging tool handlers
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
#include "mem.h"
#include "monitor.h"
#include "monitor/mon_breakpoint.h"
#include "monitor/mon_disassemble.h"

/* =============================================================================
 * Phase 4: Advanced Debugging
 * =============================================================================
 */

/* Disassemble memory to 6502 instructions
 * Accepts address as number, hex string ("$1000"), or symbol name ("FindBestMove")
 * Shows symbol names in output where available */
cJSON* mcp_tool_disassemble(cJSON *params)
{
    cJSON *response, *lines_array, *addr_item, *count_item, *symbols_item;
    uint16_t address;
    int count = 10;  /* Default to 10 instructions */
    int show_symbols = 1;  /* Default: show symbol names in output */
    int resolved;
    int i;
    char line_buf[256];
    const char *error_msg;

    log_message(mcp_tools_log, "Handling vice.disassemble");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get address (required) - can be number, hex string, or symbol name */
    addr_item = cJSON_GetObjectItem(params, "address");
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

    /* Get count (optional, default 10) */
    count_item = cJSON_GetObjectItem(params, "count");
    if (count_item != NULL && cJSON_IsNumber(count_item)) {
        count = count_item->valueint;
        if (count < 1) count = 1;
        if (count > 100) count = 100;  /* Limit to 100 lines */
    }

    /* Get show_symbols option (optional, default true) */
    symbols_item = cJSON_GetObjectItem(params, "show_symbols");
    if (symbols_item != NULL && cJSON_IsBool(symbols_item)) {
        show_symbols = cJSON_IsTrue(symbols_item);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    lines_array = cJSON_CreateArray();
    if (lines_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Disassemble each instruction */
    for (i = 0; i < count && address <= 0xFFFF; i++) {
        const char *disasm;
        unsigned int opc_size = 0;
        cJSON *line_obj;
        uint8_t opc;
        uint8_t p1;
        uint8_t p2;
        uint8_t p3;
        char *addr_symbol = NULL;
        char *target_symbol = NULL;
        uint16_t target_addr = 0;
        int has_target = 0;

        /* Check if this address has a symbol */
        if (show_symbols) {
            addr_symbol = mon_symbol_table_lookup_name(e_comp_space, address);
        }

        /* Read opcode and potential operand bytes from memory */
        opc = mon_get_mem_val(e_comp_space, address);
        p1 = mon_get_mem_val(e_comp_space, (uint16_t)(address + 1));
        p2 = mon_get_mem_val(e_comp_space, (uint16_t)(address + 2));
        p3 = mon_get_mem_val(e_comp_space, (uint16_t)(address + 3));

        /* Use VICE's disassembler - e_comp_space is main CPU memory
         * Args: memspace, addr, opcode, operand1, operand2, operand3, hex_mode, opc_size_ptr */
        disasm = mon_disassemble_to_string_ex(e_comp_space, address, opc, p1, p2, p3, 1, &opc_size);

        if (disasm == NULL) {
            break;
        }

        /* Check for JSR/JMP target symbols (opcodes: JSR=$20, JMP=$4C, JMP indirect=$6C) */
        if (show_symbols && (opc == 0x20 || opc == 0x4C)) {
            /* Absolute addressing - target is p1 (low) + p2 (high) */
            target_addr = (uint16_t)(p1 | (p2 << 8));
            target_symbol = mon_symbol_table_lookup_name(e_comp_space, target_addr);
            has_target = 1;
        }

        line_obj = cJSON_CreateObject();
        if (line_obj == NULL) {
            break;
        }

        cJSON_AddNumberToObject(line_obj, "address", address);

        /* Format output with symbols */
        if (addr_symbol != NULL) {
            if (has_target && target_symbol != NULL) {
                /* Both address and target have symbols */
                snprintf(line_buf, sizeof(line_buf), "$%04X <%s>: %s  ; -> %s",
                         address, addr_symbol, disasm, target_symbol);
            } else {
                snprintf(line_buf, sizeof(line_buf), "$%04X <%s>: %s", address, addr_symbol, disasm);
            }
            cJSON_AddStringToObject(line_obj, "symbol", addr_symbol);
        } else {
            if (has_target && target_symbol != NULL) {
                snprintf(line_buf, sizeof(line_buf), "$%04X: %s  ; -> %s", address, disasm, target_symbol);
            } else {
                snprintf(line_buf, sizeof(line_buf), "$%04X: %s", address, disasm);
            }
        }

        cJSON_AddStringToObject(line_obj, "text", line_buf);
        cJSON_AddStringToObject(line_obj, "instruction", disasm);
        cJSON_AddNumberToObject(line_obj, "size", opc_size);

        if (has_target) {
            cJSON_AddNumberToObject(line_obj, "target", target_addr);
            if (target_symbol != NULL) {
                cJSON_AddStringToObject(line_obj, "target_symbol", target_symbol);
            }
        }

        cJSON_AddItemToArray(lines_array, line_obj);

        /* Move to next instruction */
        if (opc_size == 0) {
            opc_size = 1;  /* Safety: avoid infinite loop */
        }
        {
            uint16_t prev_addr = address;
            address += opc_size;
            if (address < prev_addr) {
                break;  /* Wrapped past $FFFF */
            }
        }
    }

    cJSON_AddItemToObject(response, "lines", lines_array);
    cJSON_AddNumberToObject(response, "count", cJSON_GetArraySize(lines_array));

    return response;
}

/* Load symbol/label file
 * Supports multiple formats:
 * - VICE format: "al C:xxxx .label_name"
 * - Simple format: "label_name = $xxxx" or "label_name = xxxx"
 * - KickAssembler format: ".label NAME=$xxxx" with ".namespace name { }" blocks
 */
cJSON* mcp_tool_symbols_load(cJSON *params)
{
    cJSON *response, *path_item, *format_item;
    const char *path;
    const char *format = NULL;
    FILE *fp;
    char line[512];
    int count = 0;
    int i;
    char *addr_start;
    MON_ADDR mon_addr;
    char namespace_stack[8][64];  /* Support up to 8 nested namespaces */
    int namespace_depth = 0;
    int label_brace_depth = 0;  /* Track { } blocks after labels (not namespaces) */

    log_message(mcp_tools_log, "Handling vice.symbols.load");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    path_item = cJSON_GetObjectItem(params, "path");
    if (!cJSON_IsString(path_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "path required");
    }
    path = path_item->valuestring;

    /* Optional format hint: "vice", "kickasm", or "auto" (default) */
    format_item = cJSON_GetObjectItem(params, "format");
    if (format_item != NULL && cJSON_IsString(format_item)) {
        format = format_item->valuestring;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Cannot open symbol file");
    }

    /* Auto-detect format if not specified */
    if (format == NULL || strcmp(format, "auto") == 0) {
        /* Peek at first non-empty line to detect format */
        while (fgets(line, sizeof(line), fp) != NULL) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#' || strncmp(p, "//", 2) == 0) continue;

            /* Check for KickAssembler markers */
            if (strncmp(p, ".label ", 7) == 0 || strncmp(p, ".namespace ", 11) == 0 ||
                strncmp(p, ".const ", 7) == 0 || strncmp(p, ".var ", 5) == 0) {
                format = "kickasm";
            } else if (strncmp(p, "al ", 3) == 0) {
                format = "vice";
            } else {
                format = "simple";
            }
            break;
        }
        rewind(fp);
        if (format == NULL) format = "simple";
    }

    log_message(mcp_tools_log, "Symbol file format: %s", format);

    /* Parse symbol file line by line */
    while (fgets(line, sizeof(line), fp) != NULL) {
        char label[128];
        char full_label[256];
        unsigned int addr;
        char *p;

        /* Skip empty lines and comments */
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == ';' || *p == '#') continue;
        if (strncmp(p, "//", 2) == 0) continue;  /* KickAssembler comments */

        /* KickAssembler format parsing */
        if (strcmp(format, "kickasm") == 0) {
            /* Handle namespace opening: ".namespace name {" */
            if (strncmp(p, ".namespace ", 11) == 0) {
                char ns_name[64];
                if (sscanf(p + 11, "%63[^{ \t\n]", ns_name) == 1) {
                    if (namespace_depth < 8) {
                        strncpy(namespace_stack[namespace_depth], ns_name, 63);
                        namespace_stack[namespace_depth][63] = '\0';
                        namespace_depth++;
                        log_message(mcp_tools_log, "Entered namespace: %s (depth=%d)", ns_name, namespace_depth);
                    }
                }
                continue;
            }

            /* Handle closing brace - could be from namespace or label block */
            if (*p == '}') {
                if (label_brace_depth > 0) {
                    label_brace_depth--;  /* Label block closed, not namespace */
                } else if (namespace_depth > 0) {
                    namespace_depth--;
                    log_message(mcp_tools_log, "Exited namespace (depth=%d)", namespace_depth);
                }
                continue;
            }

            /* Handle .label NAME=$ADDR or .label NAME=$ADDR { */
            if (strncmp(p, ".label ", 7) == 0) {
                char *eq = strchr(p + 7, '=');
                if (eq != NULL) {
                    /* Extract label name (everything between .label and =) */
                    int name_len = (int)(eq - (p + 7));
                    if (name_len > 0 && name_len < 127) {
                        strncpy(label, p + 7, name_len);
                        label[name_len] = '\0';
                        /* Trim trailing whitespace from label */
                        while (name_len > 0 && (label[name_len-1] == ' ' || label[name_len-1] == '\t')) {
                            label[--name_len] = '\0';
                        }

                        /* Parse address after = (skip $ if present) */
                        addr_start = eq + 1;
                        while (*addr_start == ' ' || *addr_start == '\t') addr_start++;
                        if (*addr_start == '$') addr_start++;

                        if (sscanf(addr_start, "%x", &addr) == 1) {
                            /* Build full label with namespace prefix */
                            full_label[0] = '\0';
                            for (i = 0; i < namespace_depth; i++) {
                                if (strlen(full_label) + strlen(namespace_stack[i]) + 2 < sizeof(full_label)) {
                                    strcat(full_label, namespace_stack[i]);
                                    strcat(full_label, ".");
                                }
                            }
                            if (strlen(full_label) + strlen(label) < sizeof(full_label)) {
                                strcat(full_label, label);
                            }

                            mon_addr = new_addr(e_comp_space, (uint16_t)addr);
                            /* Must strdup - mon_add_name_to_symbol_table stores pointer */
                            mon_add_name_to_symbol_table(mon_addr, lib_strdup(full_label));
                            count++;

                            /* Check if this label opens a block (has { at end of line) */
                            if (strchr(addr_start, '{') != NULL) {
                                label_brace_depth++;
                            }
                        }
                    }
                }
                continue;
            }

            /* Handle .const NAME=$ADDR (treat same as .label) */
            if (strncmp(p, ".const ", 7) == 0) {
                char *eq = strchr(p + 7, '=');
                if (eq != NULL) {
                    int name_len = (int)(eq - (p + 7));
                    if (name_len > 0 && name_len < 127) {
                        strncpy(label, p + 7, name_len);
                        label[name_len] = '\0';
                        while (name_len > 0 && (label[name_len-1] == ' ' || label[name_len-1] == '\t')) {
                            label[--name_len] = '\0';
                        }

                        addr_start = eq + 1;
                        while (*addr_start == ' ' || *addr_start == '\t') addr_start++;
                        if (*addr_start == '$') addr_start++;

                        if (sscanf(addr_start, "%x", &addr) == 1) {
                            full_label[0] = '\0';
                            for (i = 0; i < namespace_depth; i++) {
                                if (strlen(full_label) + strlen(namespace_stack[i]) + 2 < sizeof(full_label)) {
                                    strcat(full_label, namespace_stack[i]);
                                    strcat(full_label, ".");
                                }
                            }
                            if (strlen(full_label) + strlen(label) < sizeof(full_label)) {
                                strcat(full_label, label);
                            }

                            mon_addr = new_addr(e_comp_space, (uint16_t)addr);
                            /* Must strdup - mon_add_name_to_symbol_table stores pointer */
                            mon_add_name_to_symbol_table(mon_addr, lib_strdup(full_label));
                            count++;

                            /* Check if this const opens a block (has { at end of line) */
                            if (strchr(addr_start, '{') != NULL) {
                                label_brace_depth++;
                            }
                        }
                    }
                }
                continue;
            }

            continue;  /* Skip unrecognized KickAssembler directives */
        }

        /* VICE format: "al C:xxxx .label_name" */
        if (strcmp(format, "vice") == 0 || strcmp(format, "simple") == 0) {
            if (strncmp(p, "al ", 3) == 0) {
                if (sscanf(p, "al C:%x .%127s", &addr, label) == 2 ||
                    sscanf(p, "al %*c:%x .%127s", &addr, label) == 2) {
                    mon_addr = new_addr(e_comp_space, (uint16_t)addr);
                    /* Must strdup - mon_add_name_to_symbol_table stores pointer */
                    mon_add_name_to_symbol_table(mon_addr, lib_strdup(label));
                    count++;
                    continue;
                }
            }
        }

        /* Simple format: "label = $xxxx" or "label = xxxx" */
        if (strcmp(format, "simple") == 0 || strcmp(format, "vice") == 0) {
            if (sscanf(p, "%127s = $%x", label, &addr) == 2 ||
                sscanf(p, "%127s = %x", label, &addr) == 2) {
                mon_addr = new_addr(e_comp_space, (uint16_t)addr);
                /* Must strdup - mon_add_name_to_symbol_table stores pointer */
                mon_add_name_to_symbol_table(mon_addr, lib_strdup(label));
                count++;
            }
        }
    }

    fclose(fp);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddStringToObject(response, "format_detected", format);
    cJSON_AddNumberToObject(response, "symbols_loaded", count);

    return response;
}

/* Lookup symbol by name or address */
cJSON* mcp_tool_symbols_lookup(cJSON *params)
{
    cJSON *response, *name_item, *addr_item;

    log_message(mcp_tools_log, "Handling vice.symbols.lookup");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Lookup by name */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item != NULL && cJSON_IsString(name_item)) {
        int addr = mon_symbol_table_lookup_addr(e_comp_space, (char *)name_item->valuestring);
        if (addr >= 0) {
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddStringToObject(response, "name", name_item->valuestring);
            cJSON_AddNumberToObject(response, "address", addr);
            return response;
        } else {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Symbol not found");
        }
    }

    /* Lookup by address */
    addr_item = cJSON_GetObjectItem(params, "address");
    if (addr_item != NULL && cJSON_IsNumber(addr_item)) {
        char *name = mon_symbol_table_lookup_name(e_comp_space, (uint16_t)addr_item->valueint);
        if (name != NULL) {
            cJSON_AddStringToObject(response, "status", "ok");
            cJSON_AddNumberToObject(response, "address", addr_item->valueint);
            cJSON_AddStringToObject(response, "name", name);
            return response;
        } else {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "No symbol at address");
        }
    }

    cJSON_Delete(response);
    return mcp_error(MCP_ERROR_INVALID_PARAMS, "Either 'name' or 'address' required");
}

/* Add memory watchpoint (convenience wrapper for checkpoint with store/load)
 *
 * Parameters:
 *   - address (required): Address to watch (number, hex string, or symbol)
 *   - size (optional): Number of bytes to watch (default: 1)
 *   - type (optional): "read", "write", or "both" (default: "write")
 *   - condition (optional): Condition expression, e.g., "A == $02", "PC == $1000"
 *
 * When a condition is provided, the watchpoint only triggers when the condition
 * is true. For store watches, the condition is evaluated *after* the store
 * completes. To check "stop when $D020 becomes $02", use:
 *   {"address": "$D020", "type": "write", "condition": "A == $02"}
 */
cJSON* mcp_tool_watch_add(cJSON *params)
{
    cJSON *response, *addr_item, *type_item, *size_item, *condition_item;
    uint16_t address;
    uint16_t end_address;
    int size = 1;
    MEMORY_OP op = 0;
    const char *watch_type = "write";
    const char *condition_str = NULL;
    int checkpoint_num;
    const char *error_msg;
    int resolved;

    log_message(mcp_tools_log, "Handling vice.watch.add");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get address (required) - can be number, hex string, or symbol */
    addr_item = cJSON_GetObjectItem(params, "address");
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

    /* Get size (optional, default 1) */
    size_item = cJSON_GetObjectItem(params, "size");
    if (size_item != NULL && cJSON_IsNumber(size_item)) {
        size = size_item->valueint;
        if (size < 1) size = 1;
    }
    if (size > 0xFFFF || (int)address + size - 1 > 0xFFFF) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Watch range exceeds address space (address + size > $FFFF)");
    }
    end_address = address + size - 1;

    /* Get type (optional: "read", "write", or "both", default "write") */
    type_item = cJSON_GetObjectItem(params, "type");
    if (type_item != NULL && cJSON_IsString(type_item)) {
        watch_type = type_item->valuestring;
    }

    if (strcmp(watch_type, "read") == 0) {
        op = e_load;
    } else if (strcmp(watch_type, "write") == 0) {
        op = e_store;
    } else if (strcmp(watch_type, "both") == 0) {
        op = e_load | e_store;
    } else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "type must be 'read', 'write', or 'both'");
    }

    /* Get condition (optional) */
    condition_item = cJSON_GetObjectItem(params, "condition");
    if (condition_item != NULL && cJSON_IsString(condition_item)) {
        condition_str = condition_item->valuestring;
    }

    /* Create checkpoint with the appropriate operation type */
    checkpoint_num = mon_breakpoint_add_checkpoint(
        (MON_ADDR)address,
        (MON_ADDR)end_address,
        true,   /* stop */
        op,
        false,  /* is_temp */
        true    /* do_print */
    );

    if (checkpoint_num < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to create watchpoint");
    }

    /* If condition provided, parse and set it on the checkpoint */
    if (condition_str != NULL && condition_str[0] != '\0') {
        cond_node_t *cond_node;

        /* Parse condition string using the same parser as checkpoint.set_condition */
        cond_node = parse_simple_condition(condition_str);
        if (cond_node == NULL) {
            /* Delete the checkpoint we just created since condition is invalid */
            mon_breakpoint_delete_checkpoint(checkpoint_num);
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Invalid condition. Supported: 'A == $xx', 'X == $xx', 'Y == $xx', "
                "'PC == $xxxx', 'SP == $xx' (hex with $, 0x, or decimal)");
        }

        /* Set the condition on the checkpoint */
        mon_breakpoint_set_checkpoint_condition(checkpoint_num, cond_node);

        log_message(mcp_tools_log, "Watchpoint %d created with condition: %s",
                    checkpoint_num, condition_str);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);
    cJSON_AddNumberToObject(response, "address", address);
    cJSON_AddNumberToObject(response, "size", size);
    cJSON_AddStringToObject(response, "type", watch_type);

    /* Include condition in response if one was provided */
    if (condition_str != NULL) {
        cJSON_AddStringToObject(response, "condition", condition_str);
    }

    return response;
}

/* =========================================================================
 * Phase 5: Enhanced Debugging Tools
 * ========================================================================= */

/* Show call stack (JSR return addresses on 6502 stack)
 * The 6502 stack is at $0100-$01FF, growing downward.
 * JSR pushes the return address minus 1 (high byte first, then low byte).
 * This scans the stack for likely return addresses. */
cJSON* mcp_tool_backtrace(cJSON *params)
{
    cJSON *response, *frames_array, *depth_item;
    int sp;
    int i;
    int max_depth;
    int frame_count = 0;
    uint8_t lo;
    uint8_t hi;
    uint8_t jsr_opcode;
    uint16_t ret_addr;
    uint16_t jsr_target;
    char *symbol;

    log_message(mcp_tools_log, "Handling vice.backtrace");

    max_depth = 16;  /* Default max frames */
    if (params != NULL) {
        depth_item = cJSON_GetObjectItem(params, "depth");
        if (depth_item != NULL && cJSON_IsNumber(depth_item)) {
            max_depth = depth_item->valueint;
            if (max_depth < 1) max_depth = 1;
            if (max_depth > 64) max_depth = 64;
        }
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    frames_array = cJSON_CreateArray();
    if (frames_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Get current stack pointer */
    sp = maincpu_get_sp();

    /* Scan stack for return addresses
     * Stack grows downward, so we scan from SP+1 upward to $FF
     * Return addresses are stored as (addr-1), low byte first (pushed high, then low by JSR) */
    for (i = sp + 1; i < 0xFF && frame_count < max_depth; i += 2) {
        cJSON *frame_obj;

        /* Return addresses on stack are low-byte first when reading */
        lo = mem_bank_peek(0, (uint16_t)(0x100 + i), NULL);
        hi = mem_bank_peek(0, (uint16_t)(0x100 + i + 1), NULL);

        /* JSR stores addr-1, so add 1 to get actual return address */
        ret_addr = (uint16_t)((hi << 8) | lo) + 1;

        /* Basic heuristic: skip if address looks invalid (in zero page or stack area) */
        if (ret_addr < 0x0200 || ret_addr > 0xFFFC) {
            continue;
        }

        frame_obj = cJSON_CreateObject();
        if (frame_obj == NULL) {
            break;
        }

        cJSON_AddNumberToObject(frame_obj, "return_address", ret_addr);
        cJSON_AddNumberToObject(frame_obj, "stack_offset", 0x100 + i);

        /* Look up symbol at return address */
        symbol = mon_symbol_table_lookup_name(e_comp_space, ret_addr);
        if (symbol != NULL) {
            cJSON_AddStringToObject(frame_obj, "symbol", symbol);
        }

        /* Also check the JSR target (3 bytes before return address) */
        if (ret_addr >= 3) {
            jsr_opcode = mem_bank_peek(0, (uint16_t)(ret_addr - 3), NULL);
            if (jsr_opcode == 0x20) {  /* JSR opcode */
                jsr_target = mem_bank_peek(0, (uint16_t)(ret_addr - 2), NULL) |
                                     (mem_bank_peek(0, (uint16_t)(ret_addr - 1), NULL) << 8);
                cJSON_AddNumberToObject(frame_obj, "called_from", ret_addr - 3);
                cJSON_AddNumberToObject(frame_obj, "called_target", jsr_target);
                symbol = mon_symbol_table_lookup_name(e_comp_space, jsr_target);
                if (symbol != NULL) {
                    cJSON_AddStringToObject(frame_obj, "called_symbol", symbol);
                }
            }
        }

        cJSON_AddItemToArray(frames_array, frame_obj);
        frame_count++;
    }

    cJSON_AddNumberToObject(response, "sp", sp);
    cJSON_AddItemToObject(response, "frames", frames_array);
    cJSON_AddNumberToObject(response, "frame_count", frame_count);

    return response;
}

/* Run until an address is reached or a cycle limit is exceeded
 * This helps with step-over getting stuck in busy-wait loops */
cJSON* mcp_tool_run_until(cJSON *params)
{
    cJSON *response, *addr_item, *cycles_item;
    int resolved;
    const char *error_msg;
    char *symbol;
    uint16_t target_addr = 0;
    int cycle_limit = 0;
    int has_addr = 0;
    int bp_num;

    log_message(mcp_tools_log, "Handling vice.run_until");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get optional target address */
    addr_item = cJSON_GetObjectItem(params, "address");
    if (addr_item != NULL) {
        resolved = mcp_resolve_address(addr_item, &error_msg);
        if (resolved < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve address: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }
        target_addr = (uint16_t)resolved;
        has_addr = 1;
    }

    /* Get optional cycle limit */
    cycles_item = cJSON_GetObjectItem(params, "cycles");
    if (cycles_item != NULL && cJSON_IsNumber(cycles_item)) {
        cycle_limit = cycles_item->valueint;
        if (cycle_limit < 0) cycle_limit = 0;
        if (cycle_limit > 10000000) cycle_limit = 10000000;  /* Max ~10M cycles */
    }

    if (!has_addr && cycle_limit == 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Either 'address' or 'cycles' required");
    }

    if (!has_addr && cycle_limit > 0) {
        return mcp_error(MCP_ERROR_NOT_IMPLEMENTED, "cycles-only mode not yet implemented; provide an address");
    }

    /* If we have an address target, set a temporary breakpoint */
    if (has_addr) {
        /* Create temporary breakpoint that will be auto-deleted when hit */
        bp_num = mon_breakpoint_add_checkpoint(
            (MON_ADDR)target_addr,
            (MON_ADDR)target_addr,
            true,   /* stop */
            e_exec, /* exec breakpoint */
            true,   /* is_temp - auto-delete when hit */
            false   /* do_print */
        );

        if (bp_num < 0) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to create temporary breakpoint");
        }
    }

    /* TODO: Implement cycle-limited execution
     * For now, we just set up the breakpoint and resume.
     * A proper implementation would need to hook into VICE's cycle counter. */

    /* Resume execution */
    exit_mon = exit_mon_continue;

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    if (has_addr) {
        cJSON_AddNumberToObject(response, "target_address", target_addr);
        symbol = mon_symbol_table_lookup_name(e_comp_space, target_addr);
        if (symbol != NULL) {
            cJSON_AddStringToObject(response, "target_symbol", symbol);
        }
    }
    if (cycle_limit > 0) {
        cJSON_AddNumberToObject(response, "cycle_limit", cycle_limit);
        cJSON_AddStringToObject(response, "note", "Cycle limit not yet implemented - using breakpoint only");
    }
    cJSON_AddStringToObject(response, "message", "Execution resumed, will stop at target or breakpoint");

    return response;
}

/* =========================================================================
 * Cycles Stopwatch Tool
 * ========================================================================= */

/**
 * @brief Measure elapsed CPU cycles for timing-critical code analysis.
 *
 * Parameters:
 *   action: (required) string - One of:
 *           "reset"         - Reset stopwatch to 0, return new cycle count (0)
 *           "read"          - Read current elapsed cycles
 *           "reset_and_read" - Atomically read current value and reset
 *
 * Returns:
 *   cycles: Current cycle count (after any reset operation)
 *   previous_cycles: (only for reset_and_read) Value before reset
 *   memspace: Which CPU was measured ("computer" for main CPU)
 *
 * Use case: Measure raster routine timing - reset at raster line start,
 * read at end to verify the routine fits within the available cycles.
 */
cJSON* mcp_tool_cycles_stopwatch(cJSON *params)
{
    cJSON *response, *action_item;
    const char *action;
    unsigned long elapsed_cycles;
    unsigned long previous_cycles = 0;
    int do_reset = 0;
    int do_read = 0;
    int include_previous = 0;

    log_message(mcp_tools_log, "Handling vice.cycles.stopwatch");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get and validate action parameter */
    action_item = cJSON_GetObjectItem(params, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "action parameter required (string)");
    }
    action = action_item->valuestring;

    /* Parse action */
    if (strcmp(action, "reset") == 0) {
        do_reset = 1;
        do_read = 1;  /* Return 0 after reset */
    } else if (strcmp(action, "read") == 0) {
        do_read = 1;
    } else if (strcmp(action, "reset_and_read") == 0) {
        do_reset = 1;
        do_read = 1;
        include_previous = 1;  /* Return previous value before reset */
    } else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid action. Must be 'reset', 'read', or 'reset_and_read'");
    }

    /* Read current elapsed cycles if needed for previous value */
    if (include_previous) {
        previous_cycles = mon_stopwatch_get_elapsed();
    }

    /* Perform reset if requested */
    if (do_reset) {
        mon_stopwatch_reset();
    }

    /* Read current (possibly post-reset) cycle count */
    if (do_read) {
        elapsed_cycles = mon_stopwatch_get_elapsed();
    } else {
        elapsed_cycles = 0;
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "cycles", (double)elapsed_cycles);

    if (include_previous) {
        cJSON_AddNumberToObject(response, "previous_cycles", (double)previous_cycles);
    }

    cJSON_AddStringToObject(response, "memspace", "computer");

    return response;
}
