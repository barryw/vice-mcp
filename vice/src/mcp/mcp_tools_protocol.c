/*
 * mcp_tools_protocol.c - MCP protocol handlers (ping, initialize, tools/list, tools/call)
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
#include "monitor.h"  /* For exit_mon */
#include "ui.h"       /* For ui_pause_active */
#include "version.h"

/* =========================================================================
 * Tool Implementations - Phase 1
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
 * ========================================================================= */

cJSON* mcp_tool_ping(cJSON *params)
{
    cJSON *response;
    const char *exec_state;

    (void)params;

    log_message(mcp_tools_log, "Handling vice.ping");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "version", VERSION);
    cJSON_AddStringToObject(response, "machine", machine_get_name());

    /* Report execution state based on UI pause state AND monitor state.
     * The emulator can be paused in two ways:
     * 1. UI pause - controlled by ui_pause_enable/disable
     * 2. Monitor mode - controlled by exit_mon variable
     * We report "paused" if either is active. */
    if (ui_pause_active()) {
        exec_state = "paused";
    } else {
        switch (exit_mon) {
            case 0:  /* exit_mon_no - in monitor */
                exec_state = "paused";
                break;
            case 1:  /* exit_mon_continue - running */
                exec_state = "running";
                break;
            default:
                exec_state = "transitioning";
                break;
        }
    }
    cJSON_AddStringToObject(response, "execution", exec_state);

    return response;
}

/* =========================================================================
 * MCP Base Protocol Handlers
 * ========================================================================= */

cJSON* mcp_tool_initialize(cJSON *params)
{
    cJSON *response, *capabilities, *server_info;
    cJSON *protocol_version;
    cJSON *logging_cap;
    cJSON *tools_cap;
    const char *version_str;

    log_message(mcp_tools_log, "Initialize request received");

    /* Get protocol version from params */
    protocol_version = cJSON_GetObjectItem(params, "protocolVersion");
    if (protocol_version != NULL && cJSON_IsString(protocol_version)) {
        version_str = protocol_version->valuestring;
        log_message(mcp_tools_log, "Client protocol version: %s", version_str);

        /* Verify we support this version */
        if (strcmp(version_str, "2025-11-25") != 0 &&
            strcmp(version_str, "2025-06-18") != 0 &&
            strcmp(version_str, "2024-11-05") != 0) {
            log_warning(mcp_tools_log, "Unsupported protocol version: %s", version_str);
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                           "Unsupported protocol version (supported: 2025-11-25, 2025-06-18, 2024-11-05)");
        }
    } else {
        log_warning(mcp_tools_log, "No protocol version specified, assuming 2024-11-05");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Protocol version we're using */
    cJSON_AddStringToObject(response, "protocolVersion", "2025-11-25");

    /* Server capabilities */
    capabilities = cJSON_CreateObject();
    if (capabilities == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add logging capability (like working servers) */
    logging_cap = cJSON_CreateObject();
    if (logging_cap != NULL) {
        cJSON_AddItemToObject(capabilities, "logging", logging_cap);
    }

    /* Add tools capability with listChanged */
    tools_cap = cJSON_CreateObject();
    if (tools_cap != NULL) {
        cJSON_AddBoolToObject(tools_cap, "listChanged", 1);  /* true */
        cJSON_AddItemToObject(capabilities, "tools", tools_cap);
    }

    cJSON_AddItemToObject(response, "capabilities", capabilities);

    /* Server info */
    server_info = cJSON_CreateObject();
    if (server_info != NULL) {
        cJSON_AddStringToObject(server_info, "name", "VICE MCP");
        cJSON_AddStringToObject(server_info, "version", VERSION);
        cJSON_AddItemToObject(response, "serverInfo", server_info);
    }

    log_message(mcp_tools_log, "Initialize complete - protocol version 2025-11-25");

    return response;
}

cJSON* mcp_tool_initialized_notification(cJSON *params)
{
    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Client initialized notification received");

    /* Notifications don't get responses per JSON-RPC 2.0 spec */
    /* Return NULL to signal no response body */
    return NULL;
}

/* =========================================================================
 * Meta Tools
 * ========================================================================= */

/* tools/call - MCP standard method to invoke a tool */
cJSON* mcp_tool_tools_call(cJSON *params)
{
    cJSON *name_item, *args_item;
    const char *tool_name;
    int i;
    char *params_str, *args_str;
    int args_created = 0;

    log_message(mcp_tools_log, "Handling tools/call");

    /* Debug: dump full params */
    params_str = cJSON_PrintUnformatted(params);
    if (params_str != NULL) {
        log_message(mcp_tools_log, "tools/call params: %s", params_str);
        free(params_str);
    }

    /* Extract tool name from params */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        log_error(mcp_tools_log, "tools/call: missing or invalid 'name' parameter");
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing 'name' parameter");
    }
    tool_name = name_item->valuestring;

    /* Extract arguments (optional) */
    args_item = cJSON_GetObjectItem(params, "arguments");
    if (args_item == NULL) {
        log_message(mcp_tools_log, "tools/call: no 'arguments' found, creating empty object");
        args_item = cJSON_CreateObject();  /* Empty args */
        args_created = 1;
    }

    /* Debug: dump arguments being passed */
    args_str = cJSON_PrintUnformatted(args_item);
    if (args_str != NULL) {
        log_message(mcp_tools_log, "tools/call: arguments = %s", args_str);
        free(args_str);
    }

    log_message(mcp_tools_log, "tools/call: invoking tool '%s'", tool_name);

    /* Find and invoke the tool */
    for (i = 0; tool_registry[i].name != NULL; i++) {
        if (strcmp(tool_registry[i].name, tool_name) == 0) {
            cJSON *tool_result;
            cJSON *response;
            cJSON *code_item;
            cJSON *content;
            cJSON *text_content;
            char *result_str;

            tool_result = tool_registry[i].handler(args_item);

            /* Clean up args if we created it */
            if (args_created) {
                cJSON_Delete(args_item);
                args_item = NULL;
            }

            /* NULL result means the handler had nothing to return
             * (e.g. a notification).  Treat as internal error for
             * tools/call since a tool must produce a result. */
            if (tool_result == NULL) {
                return mcp_error(MCP_ERROR_INTERNAL_ERROR,
                                 "Tool returned no result");
            }

            /* Check if tool returned an error */
            code_item = cJSON_GetObjectItem(tool_result, "code");
            if (code_item != NULL && cJSON_IsNumber(code_item)) {
                /* Tool returned an error - pass it through */
                return tool_result;
            }

            /* Wrap result in MCP tools/call response format */
            response = cJSON_CreateObject();
            if (response == NULL) {
                cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            /* Wrap successful result in content array */
            content = cJSON_CreateArray();
            if (content == NULL) {
                cJSON_Delete(response);
                cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            text_content = cJSON_CreateObject();
            if (text_content == NULL) {
                cJSON_Delete(content);
                cJSON_Delete(response);
                cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            cJSON_AddStringToObject(text_content, "type", "text");

            /* Convert tool result to JSON string for text content */
            result_str = cJSON_PrintUnformatted(tool_result);
            if (result_str == NULL) {
                cJSON_Delete(text_content);
                cJSON_Delete(content);
                cJSON_Delete(response);
                cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddStringToObject(text_content, "text", result_str);
            free(result_str);

            cJSON_Delete(tool_result);

            cJSON_AddItemToArray(content, text_content);
            cJSON_AddItemToObject(response, "content", content);

            return response;
        }
    }

    /* Clean up args if we created it */
    if (args_created) {
        cJSON_Delete(args_item);
    }

    log_error(mcp_tools_log, "tools/call: tool not found: %s", tool_name);
    return mcp_error(MCP_ERROR_METHOD_NOT_FOUND, "Tool not found");
}

/* =========================================================================
 * tools/list - List all available tools with JSON Schema
 * ========================================================================= */

cJSON* mcp_tool_tools_list(cJSON *params)
{
    cJSON *response, *tools_array, *tool_obj, *schema, *props, *required;
    int i;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling tools/list");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    tools_array = cJSON_CreateArray();
    if (tools_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Build tool list with JSON Schema for each tool */
    for (i = 0; tool_registry[i].name != NULL; i++) {
        const char *name = tool_registry[i].name;
        const char *desc = tool_registry[i].description;

        tool_obj = cJSON_CreateObject();
        if (tool_obj == NULL) {
            cJSON_Delete(tools_array);
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddStringToObject(tool_obj, "name", name);
        cJSON_AddStringToObject(tool_obj, "description", desc);

        /* Build inputSchema for each tool */
        schema = NULL;

        if (strcmp(name, "initialize") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "protocolVersion", mcp_prop_string("Protocol version"));
            cJSON_AddItemToObject(props, "capabilities", cJSON_CreateObject());
            cJSON_AddItemToObject(props, "clientInfo", cJSON_CreateObject());
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "notifications/initialized") == 0 ||
                   strcmp(name, "tools/list") == 0 ||
                   strcmp(name, "vice.ping") == 0 ||
                   strcmp(name, "vice.execution.run") == 0 ||
                   strcmp(name, "vice.execution.pause") == 0 ||
                   strcmp(name, "vice.checkpoint.list") == 0 ||
                   strcmp(name, "vice.vicii.get_state") == 0 ||
                   strcmp(name, "vice.sid.get_state") == 0 ||
                   strcmp(name, "vice.display.get_dimensions") == 0) {
            /* Tools with truly no parameters */
            schema = mcp_schema_empty();

        } else if (strcmp(name, "vice.sprite.get") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "sprite", mcp_prop_number("Sprite number 0-7 (omit to get all sprites)"));
            /* No required params - sprite is optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.vicii.set_state") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "registers", mcp_prop_array("object",
                "Array of {offset, value} objects to set VIC-II registers (offset 0x00-0x2E)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.sid.set_state") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "registers", mcp_prop_array("object",
                "Array of {offset, value} objects to set SID registers (offset 0x00-0x1C)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.cia.set_state") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "cia1_registers", mcp_prop_array("object",
                "Array of {offset, value} objects to set CIA1 registers (offset 0x00-0x0F)"));
            cJSON_AddItemToObject(props, "cia2_registers", mcp_prop_array("object",
                "Array of {offset, value} objects to set CIA2 registers (offset 0x00-0x0F)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.checkpoint.set_condition") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_num", mcp_prop_number("Checkpoint number"));
            cJSON_AddItemToObject(props, "condition", mcp_prop_string("Condition expression (e.g., 'A == $42')"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_num"));
            cJSON_AddItemToArray(required, cJSON_CreateString("condition"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.disk.attach") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "unit", mcp_prop_number("Drive unit (8-11)"));
            cJSON_AddItemToObject(props, "path", mcp_prop_string("Path to disk image (.d64, .g64, etc)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("unit"));
            cJSON_AddItemToArray(required, cJSON_CreateString("path"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.disk.detach") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "unit", mcp_prop_number("Drive unit (8-11)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("unit"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.disk.list") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "unit", mcp_prop_number("Drive unit (8-11)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("unit"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.disk.read_sector") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "unit", mcp_prop_number("Drive unit (8-11)"));
            cJSON_AddItemToObject(props, "track", mcp_prop_number("Track number (1-42 for D64)"));
            cJSON_AddItemToObject(props, "sector", mcp_prop_number("Sector number (0-20 depending on track)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("unit"));
            cJSON_AddItemToArray(required, cJSON_CreateString("track"));
            cJSON_AddItemToArray(required, cJSON_CreateString("sector"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.autostart") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "path", mcp_prop_string("Path to PRG file or disk image (.d64, .g64, .prg)"));
            cJSON_AddItemToObject(props, "program", mcp_prop_string("Program name to load from disk image (optional)"));
            cJSON_AddItemToObject(props, "run", mcp_prop_boolean("Run after loading (default: true)"));
            cJSON_AddItemToObject(props, "index", mcp_prop_number("Program index on disk, 0-based (default: 0)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("path"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.machine.reset") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "mode", mcp_prop_string("Reset mode: 'soft' (CPU reset, default) or 'hard' (power cycle)"));
            cJSON_AddItemToObject(props, "run_after", mcp_prop_boolean("Resume execution after reset (default: true)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.display.screenshot") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "path", mcp_prop_string("File path to save screenshot (optional if return_base64=true)"));
            cJSON_AddItemToObject(props, "format", mcp_prop_string("Image format: PNG or BMP (default: PNG)"));
            cJSON_AddItemToObject(props, "return_base64", mcp_prop_boolean("Return screenshot as base64 data URI (default: false)"));
            /* No required params since path is optional when return_base64 is true */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.execution.step") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "count", mcp_prop_number("Number of instructions to step"));
            cJSON_AddItemToObject(props, "stepOver", mcp_prop_boolean("Step over subroutines"));
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.registers.get") == 0) {
            schema = mcp_schema_empty();

        } else if (strcmp(name, "vice.registers.set") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "register", mcp_prop_string("Register name: PC|A|X|Y|SP|N|V|B|D|I|Z|C"));
            cJSON_AddItemToObject(props, "value", mcp_prop_number("Register value"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("register"));
            cJSON_AddItemToArray(required, cJSON_CreateString("value"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.memory.read") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "address", mcp_prop_string("Address: number, hex string ($1000), or symbol name"));
            cJSON_AddItemToObject(props, "size", mcp_prop_number("Bytes to read (1-65535)"));
            cJSON_AddItemToObject(props, "bank", mcp_prop_string("Optional: Memory bank name (e.g., 'ram' to read RAM under ROM). Use vice.memory.banks to list available banks."));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("address"));
            cJSON_AddItemToArray(required, cJSON_CreateString("size"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.memory.banks") == 0) {
            schema = mcp_schema_empty();

        } else if (strcmp(name, "vice.memory.write") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "address", mcp_prop_string("Address: number, hex string ($1000), or symbol name"));
            cJSON_AddItemToObject(props, "data", mcp_prop_array("number", "Bytes to write (0-255 each)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("address"));
            cJSON_AddItemToArray(required, cJSON_CreateString("data"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.memory.search") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "start", mcp_prop_string("Start address: number, hex string ($C000), or symbol name"));
            cJSON_AddItemToObject(props, "end", mcp_prop_string("End address: number, hex string ($FFFF), or symbol name"));
            cJSON_AddItemToObject(props, "pattern", mcp_prop_array("number", "Byte pattern to find, e.g., [0x4C, 0x00, 0xA0] for JMP $A000"));
            cJSON_AddItemToObject(props, "mask", mcp_prop_array("number", "Per-byte mask: 0xFF=exact match, 0x00=wildcard (optional)"));
            cJSON_AddItemToObject(props, "max_results", mcp_prop_number("Maximum matches to return (default: 100, max: 10000)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("start"));
            cJSON_AddItemToArray(required, cJSON_CreateString("end"));
            cJSON_AddItemToArray(required, cJSON_CreateString("pattern"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.add") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "start", mcp_prop_string("Start address: number, hex string ($1000), or symbol name"));
            cJSON_AddItemToObject(props, "end", mcp_prop_string("End address: number, hex, or symbol (optional, default=start)"));
            cJSON_AddItemToObject(props, "stop", mcp_prop_boolean("Stop execution when hit (default=true)"));
            cJSON_AddItemToObject(props, "load", mcp_prop_boolean("Break on memory read (default=false)"));
            cJSON_AddItemToObject(props, "store", mcp_prop_boolean("Break on memory write (default=false)"));
            cJSON_AddItemToObject(props, "exec", mcp_prop_boolean("Break on execution (default=true)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("start"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.delete") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_num", mcp_prop_number("Checkpoint number to delete"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_num"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.toggle") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_num", mcp_prop_number("Checkpoint number"));
            cJSON_AddItemToObject(props, "enabled", mcp_prop_boolean("Enable (true) or disable (false)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_num"));
            cJSON_AddItemToArray(required, cJSON_CreateString("enabled"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.set_ignore_count") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_num", mcp_prop_number("Checkpoint number"));
            cJSON_AddItemToObject(props, "count", mcp_prop_number("Number of hits to ignore"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_num"));
            cJSON_AddItemToArray(required, cJSON_CreateString("count"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.sprite.set") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "sprite", mcp_prop_number("Sprite number 0-7"));
            cJSON_AddItemToObject(props, "x", mcp_prop_number("X position 0-511"));
            cJSON_AddItemToObject(props, "y", mcp_prop_number("Y position 0-255"));
            cJSON_AddItemToObject(props, "enabled", mcp_prop_boolean("Enable sprite"));
            cJSON_AddItemToObject(props, "multicolor", mcp_prop_boolean("Multicolor mode"));
            cJSON_AddItemToObject(props, "expand_x", mcp_prop_boolean("Double width"));
            cJSON_AddItemToObject(props, "expand_y", mcp_prop_boolean("Double height"));
            cJSON_AddItemToObject(props, "priority_foreground", mcp_prop_boolean("Draw over background"));
            cJSON_AddItemToObject(props, "color", mcp_prop_number("Sprite color 0-15"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("sprite"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.keyboard.type") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "text", mcp_prop_string("Text to type (converts to PETSCII). Use \\n for Return."));
            cJSON_AddItemToObject(props, "petscii_upper", mcp_prop_boolean("Default true: uppercase ASCII displays as uppercase on C64. Set false for raw PETSCII (uppercase ASCII maps to graphics)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("text"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.keyboard.key_press") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "key", mcp_prop_string("Key name or VHK code. Names: Return, Space, BackSpace, Delete, Escape, Tab, Up, Down, Left, Right, Home, End, F1-F8, or single char"));
            cJSON_AddItemToObject(props, "modifiers", mcp_prop_array("string", "Optional modifiers: shift, control, ctrl, alt, meta, command, cmd"));
            cJSON_AddItemToObject(props, "hold_frames", mcp_prop_number("Hold duration in frames (1-300). Key auto-releases after this many frames (~16.7ms each at 60Hz)"));
            cJSON_AddItemToObject(props, "hold_ms", mcp_prop_number("Hold duration in milliseconds (1-5000). Key auto-releases after this time. Overrides hold_frames if both specified"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("key"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.keyboard.key_release") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "key", mcp_prop_string("Key name or VHK code. Names: Return, Space, BackSpace, Delete, Escape, Tab, Up, Down, Left, Right, Home, End, F1-F8, or single char"));
            cJSON_AddItemToObject(props, "modifiers", mcp_prop_array("string", "Optional modifiers: shift, control, ctrl, alt, meta, command, cmd"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("key"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.joystick.set") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "port", mcp_prop_number("Joystick port 1 or 2 (default=1)"));
            cJSON_AddItemToObject(props, "direction", mcp_prop_string("Direction: up, down, left, right, center, or array for diagonals"));
            cJSON_AddItemToObject(props, "fire", mcp_prop_boolean("Fire button state (default=false)"));
            schema = mcp_schema_object(props, NULL);

        /* Phase 4: Advanced Debugging schemas */
        } else if (strcmp(name, "vice.disassemble") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "address", mcp_prop_string("Start address: number, hex string ($1000), or symbol name"));
            cJSON_AddItemToObject(props, "count", mcp_prop_number("Number of instructions to disassemble (default: 10, max: 100)"));
            cJSON_AddItemToObject(props, "show_symbols", mcp_prop_boolean("Show symbol names in output (default: true)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("address"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.symbols.load") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "path", mcp_prop_string("Path to symbol file (.sym, .lbl)"));
            cJSON_AddItemToObject(props, "format", mcp_prop_string("Format: 'auto' (default), 'kickasm', 'vice', or 'simple'. Auto-detects KickAssembler (.label/.namespace) vs VICE (al C:xxxx) format"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("path"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.symbols.lookup") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "name", mcp_prop_string("Symbol name to look up (returns address)"));
            cJSON_AddItemToObject(props, "address", mcp_prop_number("Address to look up (returns symbol name)"));
            /* Neither required - one or the other */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.watch.add") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "address", mcp_prop_string("Address: number, hex string ($1000), or symbol name"));
            cJSON_AddItemToObject(props, "size", mcp_prop_number("Number of bytes to watch (default: 1)"));
            cJSON_AddItemToObject(props, "type", mcp_prop_string("Watch type: 'read', 'write', or 'both' (default: 'write')"));
            cJSON_AddItemToObject(props, "condition", mcp_prop_string(
                "Condition expression for conditional watchpoint. "
                "Supported: 'A == $xx', 'X == $xx', 'Y == $xx', 'PC == $xxxx', 'SP == $xx'. "
                "For store watches, condition is evaluated after the store completes."));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("address"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.backtrace") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "depth", mcp_prop_number("Max stack frames to show (default: 16, max: 64)"));
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.run_until") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "address", mcp_prop_string("Target address: number, hex, or symbol (stops when PC reaches this)"));
            cJSON_AddItemToObject(props, "cycles", mcp_prop_number("Max cycles to run (timeout, not yet implemented)"));
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.keyboard.matrix") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "key", mcp_prop_string("Key name: A-Z, 0-9, SPACE, RETURN, F1, F3, F5, F7, UP, DOWN, LEFT, RIGHT, STOP"));
            cJSON_AddItemToObject(props, "row", mcp_prop_number("Keyboard matrix row (0-7, alternative to key name)"));
            cJSON_AddItemToObject(props, "col", mcp_prop_number("Keyboard matrix column (0-7, alternative to key name)"));
            cJSON_AddItemToObject(props, "pressed", mcp_prop_boolean("Key pressed state (default: true)"));
            cJSON_AddItemToObject(props, "hold_frames", mcp_prop_number("Hold duration in frames (1-300). Key auto-releases after this many frames (~16.7ms each at 60Hz)"));
            cJSON_AddItemToObject(props, "hold_ms", mcp_prop_number("Hold duration in milliseconds (1-5000). Key auto-releases after this time. Overrides hold_frames if both specified"));
            schema = mcp_schema_object(props, NULL);

        /* Snapshot tools */
        } else if (strcmp(name, "vice.snapshot.save") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "name", mcp_prop_string(
                "Unique name for this snapshot (alphanumeric, underscore, hyphen only). "
                "Choose descriptive names like 'before_crash' or 'level3_boss_fight'."));
            cJSON_AddItemToObject(props, "description", mcp_prop_string(
                "Human-readable description of what state this snapshot captures. "
                "Be specific: 'Player at level 3, about to trigger sprite collision bug'."));
            cJSON_AddItemToObject(props, "include_roms", mcp_prop_boolean(
                "Include ROM images in snapshot (default: false). "
                "Enable for full reproducibility, disable for smaller files."));
            cJSON_AddItemToObject(props, "include_disks", mcp_prop_boolean(
                "Include disk drive state in snapshot (default: false). "
                "Enable if disk contents are relevant to the bug being debugged."));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("name"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.snapshot.load") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "name", mcp_prop_string(
                "Name of the snapshot to load (as provided to snapshot.save). "
                "Use snapshot.list to see available snapshots."));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("name"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.snapshot.list") == 0) {
            schema = mcp_schema_empty();

        } else if (strcmp(name, "vice.cycles.stopwatch") == 0) {
            cJSON *action_prop;
            props = cJSON_CreateObject();
            action_prop = mcp_prop_string(
                "Action to perform: 'reset' (start timing), "
                "'read' (get elapsed cycles), or 'reset_and_read' (atomic read and reset)");
            if (action_prop) {
                cJSON *enum_arr = cJSON_CreateArray();
                cJSON_AddItemToArray(enum_arr, cJSON_CreateString("reset"));
                cJSON_AddItemToArray(enum_arr, cJSON_CreateString("read"));
                cJSON_AddItemToArray(enum_arr, cJSON_CreateString("reset_and_read"));
                cJSON_AddItemToObject(action_prop, "enum", enum_arr);
                cJSON_AddItemToObject(props, "action", action_prop);
            }
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("action"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.memory.fill") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "start", mcp_prop_string(
                "Start address: number, hex string ($A000), or symbol name"));
            cJSON_AddItemToObject(props, "end", mcp_prop_string(
                "End address (inclusive): number, hex string ($DFFF), or symbol name"));
            cJSON_AddItemToObject(props, "pattern", mcp_prop_array("number",
                "Byte pattern to repeat, e.g., [0] for zero-fill, [0xAA, 0x55] for alternating"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("start"));
            cJSON_AddItemToArray(required, cJSON_CreateString("end"));
            cJSON_AddItemToArray(required, cJSON_CreateString("pattern"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.memory.compare") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "mode", mcp_prop_string(
                "Comparison mode: 'ranges' to compare two memory ranges, 'snapshot' to compare against saved state"));
            /* Parameters for ranges mode */
            cJSON_AddItemToObject(props, "range1_start", mcp_prop_string(
                "Start address of first range (for mode='ranges'): number, hex string ($1000), or symbol"));
            cJSON_AddItemToObject(props, "range1_end", mcp_prop_string(
                "End address of first range (for mode='ranges'): number, hex string ($1FFF), or symbol"));
            cJSON_AddItemToObject(props, "range2_start", mcp_prop_string(
                "Start address of second range (for mode='ranges'): number, hex string ($2000), or symbol"));
            /* Parameters for snapshot mode */
            cJSON_AddItemToObject(props, "snapshot_name", mcp_prop_string(
                "Name of snapshot to compare against (for mode='snapshot')"));
            cJSON_AddItemToObject(props, "start", mcp_prop_string(
                "Start address to compare (for mode='snapshot'): number, hex string ($A000), or symbol"));
            cJSON_AddItemToObject(props, "end", mcp_prop_string(
                "End address to compare (for mode='snapshot'): number, hex string ($BFFF), or symbol"));
            /* Common parameters */
            cJSON_AddItemToObject(props, "max_differences", mcp_prop_number(
                "Maximum differences to return (default: 100, max: 10000)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("mode"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.group.create") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "name", mcp_prop_string("Group name (unique identifier)"));
            cJSON_AddItemToObject(props, "checkpoint_ids", mcp_prop_array("number",
                "Optional array of checkpoint IDs to include in the group"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("name"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.group.add") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "group", mcp_prop_string("Group name to add checkpoints to"));
            cJSON_AddItemToObject(props, "checkpoint_ids", mcp_prop_array("number",
                "Array of checkpoint IDs to add to the group"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("group"));
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_ids"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.group.toggle") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "group", mcp_prop_string("Group name"));
            cJSON_AddItemToObject(props, "enabled", mcp_prop_boolean("Enable (true) or disable (false) all checkpoints in the group"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("group"));
            cJSON_AddItemToArray(required, cJSON_CreateString("enabled"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.group.list") == 0) {
            schema = mcp_schema_empty();

        } else if (strcmp(name, "vice.checkpoint.set_auto_snapshot") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_id", mcp_prop_number(
                "Checkpoint ID to configure auto-snapshot for"));
            cJSON_AddItemToObject(props, "snapshot_prefix", mcp_prop_string(
                "Filename prefix for snapshots (e.g., 'ai_move' -> ai_move_001.vsf)"));
            cJSON_AddItemToObject(props, "max_snapshots", mcp_prop_number(
                "Ring buffer size - oldest deleted when exceeded (default: 10)"));
            cJSON_AddItemToObject(props, "include_disks", mcp_prop_boolean(
                "Include disk state in snapshots (default: false)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_id"));
            cJSON_AddItemToArray(required, cJSON_CreateString("snapshot_prefix"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.checkpoint.clear_auto_snapshot") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "checkpoint_id", mcp_prop_number(
                "Checkpoint ID to clear auto-snapshot configuration from"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("checkpoint_id"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.trace.start") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "output_file", mcp_prop_string(
                "Path to output file for trace data"));
            cJSON_AddItemToObject(props, "pc_filter_start", mcp_prop_number(
                "Start address for PC filter (default: 0)"));
            cJSON_AddItemToObject(props, "pc_filter_end", mcp_prop_number(
                "End address for PC filter (default: 65535)"));
            cJSON_AddItemToObject(props, "max_instructions", mcp_prop_number(
                "Maximum instructions to record (default: 10000)"));
            cJSON_AddItemToObject(props, "include_registers", mcp_prop_boolean(
                "Include register state in output (default: false)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("output_file"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.trace.stop") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "trace_id", mcp_prop_string(
                "The trace ID returned from trace.start"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("trace_id"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.interrupt.log.start") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "types", mcp_prop_array("string",
                "Interrupt types to log ('irq', 'nmi', 'brk'). Default: all types"));
            cJSON_AddItemToObject(props, "max_entries", mcp_prop_number(
                "Maximum entries to store (default: 1000, max: 10000)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.interrupt.log.stop") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "log_id", mcp_prop_string(
                "The log ID returned from interrupt.log.start"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("log_id"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.interrupt.log.read") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "log_id", mcp_prop_string(
                "The log ID returned from interrupt.log.start"));
            cJSON_AddItemToObject(props, "since_index", mcp_prop_number(
                "Return only entries from this index onwards (for incremental reads)"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("log_id"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.keyboard.restore") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "pressed", mcp_prop_boolean(
                "Key pressed state: true to press (triggers NMI), false to release (default: true)"));
            /* No required params - pressed is optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.memory.map") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "start", mcp_prop_string(
                "Start address: number, hex string ($0000), or symbol (default: $0000)"));
            cJSON_AddItemToObject(props, "end", mcp_prop_string(
                "End address: number, hex string ($FFFF), or symbol (default: $FFFF)"));
            cJSON_AddItemToObject(props, "granularity", mcp_prop_number(
                "Region granularity in bytes (default: 256)"));
            /* No required params - all optional */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "vice.sprite.inspect") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "sprite_number", mcp_prop_number(
                "Sprite number 0-7 to inspect"));
            cJSON_AddItemToObject(props, "format", mcp_prop_string(
                "Output format: 'ascii' (default), 'binary', or 'png_base64'"));
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("sprite_number"));
            schema = mcp_schema_object(props, required);

        } else if (strcmp(name, "vice.cia.get_state") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "cia", mcp_prop_number(
                "CIA chip number: 1 or 2 (optional, default returns both)"));
            /* No required params */
            schema = mcp_schema_object(props, NULL);

        } else if (strcmp(name, "tools/call") == 0) {
            props = cJSON_CreateObject();
            cJSON_AddItemToObject(props, "name", mcp_prop_string("Tool name to call"));
            cJSON_AddItemToObject(props, "arguments", cJSON_CreateObject());
            required = cJSON_CreateArray();
            cJSON_AddItemToArray(required, cJSON_CreateString("name"));
            schema = mcp_schema_object(props, required);

        } else {
            /* Default: empty schema for unknown tools */
            schema = mcp_schema_empty();
        }

        if (schema) {
            cJSON_AddItemToObject(tool_obj, "inputSchema", schema);
        }

        cJSON_AddItemToArray(tools_array, tool_obj);
    }

    cJSON_AddItemToObject(response, "tools", tools_array);
    cJSON_AddNumberToObject(response, "count", i);

    return response;
}
