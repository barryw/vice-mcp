/*
 * mcp_tools_helpers.c - Shared utility functions for MCP tools
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
#include "monitor.h"
#include "monitor/mon_breakpoint.h"
#include "archdep_user_config_path.h"
#include "archdep_mkdir.h"
#include "util.h"

#include <time.h>

/* -------------------------------------------------------------------------
 * Catastrophic error JSON (static allocation for OOM situations)
 * ------------------------------------------------------------------------- */

const char * const CATASTROPHIC_ERROR_JSON =
    "{\"code\":-32603,\"message\":\"Internal error: out of memory\"}";

/* -------------------------------------------------------------------------
 * Error response helper
 * ------------------------------------------------------------------------- */

cJSON* mcp_error(int code, const char *message)
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

/* -------------------------------------------------------------------------
 * JSON Schema helpers
 * ------------------------------------------------------------------------- */

cJSON* mcp_schema_empty(void)
{
    cJSON *schema = cJSON_CreateObject();
    if (schema == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddFalseToObject(schema, "additionalProperties");
    return schema;
}

cJSON* mcp_schema_object(cJSON *properties, cJSON *required_array)
{
    cJSON *schema = cJSON_CreateObject();
    if (schema == NULL) {
        if (properties) cJSON_Delete(properties);
        if (required_array) cJSON_Delete(required_array);
        return NULL;
    }
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddItemToObject(schema, "properties", properties);
    if (required_array != NULL) {
        cJSON_AddItemToObject(schema, "required", required_array);
    }
    return schema;
}

/* -------------------------------------------------------------------------
 * JSON Schema property helpers
 * ------------------------------------------------------------------------- */

cJSON* mcp_prop_number(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "number");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

cJSON* mcp_prop_string(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "string");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

cJSON* mcp_prop_boolean(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "boolean");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

cJSON* mcp_prop_array(const char *item_type, const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    cJSON *items;
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "array");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    if (item_type) {
        items = cJSON_CreateObject();
        if (items) {
            cJSON_AddStringToObject(items, "type", item_type);
            cJSON_AddItemToObject(prop, "items", items);
        }
    }
    return prop;
}

/* -------------------------------------------------------------------------
 * Symbol-aware address resolution
 * ------------------------------------------------------------------------- */

int mcp_resolve_address(cJSON *value, const char **error_msg)
{
    static const char *err_null = "address value is null";
    static const char *err_invalid = "address must be number or string";
    static const char *err_symbol = "symbol not found";
    static const char *err_hex = "invalid hex address";
    static const char *err_range = "address out of 16-bit range";

    if (value == NULL) {
        if (error_msg) *error_msg = err_null;
        return -1;
    }

    /* Direct number */
    if (cJSON_IsNumber(value)) {
        if (value->valueint < 0 || value->valueint > 65535) {
            if (error_msg) *error_msg = err_range;
            return -1;
        }
        return value->valueint;
    }

    /* String - could be symbol name or hex address */
    if (cJSON_IsString(value)) {
        const char *str = value->valuestring;
        int sym_addr;

        /* Check for hex format: $xxxx or 0xXXXX */
        if (str[0] == '$') {
            unsigned int addr;
            if (sscanf(str + 1, "%x", &addr) == 1) {
                if (addr > 0xFFFF) {
                    if (error_msg) *error_msg = err_range;
                    return -1;
                }
                return (int)addr;
            }
            if (error_msg) *error_msg = err_hex;
            return -1;
        }
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            unsigned int addr;
            if (sscanf(str + 2, "%x", &addr) == 1) {
                if (addr > 0xFFFF) {
                    if (error_msg) *error_msg = err_range;
                    return -1;
                }
                return (int)addr;
            }
            if (error_msg) *error_msg = err_hex;
            return -1;
        }

        /* Try as symbol name */
        sym_addr = mon_symbol_table_lookup_addr(e_comp_space, (char *)str);
        if (sym_addr >= 0) {
            return sym_addr;
        }
        if (error_msg) *error_msg = err_symbol;
        return -1;
    }

    if (error_msg) *error_msg = err_invalid;
    return -1;
}

/* -------------------------------------------------------------------------
 * Snapshot directory management
 * ------------------------------------------------------------------------- */

char* mcp_get_snapshots_dir(void)
{
    const char *config_path;
    char *snapshots_dir;

    config_path = archdep_user_config_path();
    if (config_path == NULL) {
        return NULL;
    }

    snapshots_dir = util_join_paths(config_path, "mcp_snapshots", NULL);
    if (snapshots_dir == NULL) {
        return NULL;
    }

    /* Create directory if it doesn't exist */
    archdep_mkdir(snapshots_dir, 0755);

    return snapshots_dir;
}

char *mcp_build_vsf_path(const char *dir, const char *name)
{
    size_t dir_len, name_len;
    char *path;

    if (dir == NULL || name == NULL) {
        return NULL;
    }

    dir_len = strlen(dir);
    name_len = strlen(name);

    /* Allocate space for: dir + "/" + name + null */
    path = lib_malloc(dir_len + 1 + name_len + 1);
    if (path == NULL) {
        return NULL;
    }

    /* Handle whether dir ends with a slash */
    if (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\')) {
        snprintf(path, dir_len + name_len + 1, "%s%s", dir, name);
    } else {
        snprintf(path, dir_len + 1 + name_len + 1, "%s/%s", dir, name);
    }

    return path;
}

/* -------------------------------------------------------------------------
 * VSF (VICE Snapshot File) Parser for Memory Extraction
 * ------------------------------------------------------------------------- */

/* Magic strings for VSF format validation */
#define VSF_MAGIC_STRING     "VICE Snapshot File\032"
#define VSF_MAGIC_LEN        19
#define VSF_MODULE_NAME_LEN  16
#define VSF_MACHINE_NAME_LEN 16
#define VSF_VERSION_MAGIC    "VICE Version\032"
#define VSF_VERSION_MAGIC_LEN 13

/* C64_RAM_SIZE is defined in mcp_tools_internal.h */

/* Read a little-endian 32-bit value from a byte array */
static uint32_t vsf_read_dword(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

int vsf_extract_c64_ram(const char *vsf_path, uint8_t *ram_buffer)
{
    FILE *f;
    uint8_t header[64];
    uint8_t module_header[22];
    char module_name[VSF_MODULE_NAME_LEN + 1];
    uint32_t module_size;
    long module_offset;
    size_t bytes_read;
    int found_c64mem = 0;

    /* Open the file */
    f = fopen(vsf_path, "rb");
    if (f == NULL) {
        return -1;  /* Cannot open file */
    }

    /* Read and validate magic string */
    bytes_read = fread(header, 1, VSF_MAGIC_LEN, f);
    if (bytes_read < VSF_MAGIC_LEN ||
        memcmp(header, VSF_MAGIC_STRING, VSF_MAGIC_LEN) != 0) {
        fclose(f);
        return -2;  /* Invalid magic string */
    }

    /* Skip version (2 bytes) */
    fseek(f, 2, SEEK_CUR);

    /* Read and check machine name (should be C64) */
    bytes_read = fread(header, 1, VSF_MACHINE_NAME_LEN, f);
    if (bytes_read < VSF_MACHINE_NAME_LEN) {
        fclose(f);
        return -6;  /* Read error */
    }
    /* Accept C64, C64SC, or similar */
    if (strncmp((char*)header, "C64", 3) != 0) {
        fclose(f);
        return -3;  /* Machine mismatch */
    }

    /* Try to skip VICE version info (newer snapshots have this) */
    bytes_read = fread(header, 1, VSF_VERSION_MAGIC_LEN, f);
    if (bytes_read == VSF_VERSION_MAGIC_LEN &&
        memcmp(header, VSF_VERSION_MAGIC, VSF_VERSION_MAGIC_LEN) == 0) {
        /* Skip version bytes (4) + revision (4) */
        fseek(f, 8, SEEK_CUR);
    } else {
        /* Old format - rewind to where modules start */
        fseek(f, VSF_MAGIC_LEN + 2 + VSF_MACHINE_NAME_LEN, SEEK_SET);
    }

    /* Now we're at the start of modules - search for C64MEM */
    while (!found_c64mem) {
        module_offset = ftell(f);

        /* Read module header */
        bytes_read = fread(module_header, 1, 22, f);
        if (bytes_read < 22) {
            fclose(f);
            return -4;  /* C64MEM module not found */
        }

        /* Extract module name (null-terminate) */
        memcpy(module_name, module_header, VSF_MODULE_NAME_LEN);
        module_name[VSF_MODULE_NAME_LEN] = '\0';

        /* Extract module size (little-endian) */
        module_size = vsf_read_dword(module_header + 18);

        /* Validate module_size to prevent infinite loop on corrupted files */
        if (module_size < 22) {
            fclose(f);
            return -5;  /* Module corrupted/invalid size */
        }

        /* Check if this is C64MEM */
        if (strcmp(module_name, "C64MEM") == 0) {
            found_c64mem = 1;

            /* Module size includes header (22 bytes) + data
             * We need: 4 bytes (pport stuff) + 65536 bytes (RAM) */
            if (module_size < 22 + 4 + C64_RAM_SIZE) {
                fclose(f);
                return -5;  /* Module too small */
            }

            /* Skip pport.data, pport.dir, exrom, game (4 bytes) */
            fseek(f, 4, SEEK_CUR);

            /* Read the RAM */
            bytes_read = fread(ram_buffer, 1, C64_RAM_SIZE, f);
            if (bytes_read < C64_RAM_SIZE) {
                fclose(f);
                return -6;  /* Read error */
            }

            fclose(f);
            return 0;  /* Success */
        }

        /* Skip to next module */
        if (fseek(f, module_offset + module_size, SEEK_SET) != 0) {
            fclose(f);
            return -4;  /* C64MEM module not found */
        }
    }

    fclose(f);
    return -4;  /* C64MEM module not found */
}

/* -------------------------------------------------------------------------
 * Snapshot metadata (JSON sidecar files)
 * ------------------------------------------------------------------------- */

int mcp_write_snapshot_metadata(const char *vsf_path, const char *name,
                                const char *description, int include_roms,
                                int include_disks)
{
    char *json_path;
    size_t json_path_len;
    cJSON *meta;
    char *json_str;
    FILE *f;
    time_t now;
    char timestamp[32];

    /* Create .json path from .vsf path */
    json_path_len = strlen(vsf_path);
    if (json_path_len < 4 || strcmp(vsf_path + json_path_len - 4, ".vsf") != 0) {
        /* Path must end with .vsf */
        return -1;
    }
    json_path = lib_malloc(json_path_len + 2);  /* .vsf(4) -> .json(5) + NUL */
    if (json_path == NULL) {
        return -1;
    }
    strcpy(json_path, vsf_path);
    /* Replace .vsf extension with .json */
    strcpy(json_path + json_path_len - 4, ".json");

    /* Build metadata JSON */
    meta = cJSON_CreateObject();
    if (meta == NULL) {
        lib_free(json_path);
        return -1;
    }

    cJSON_AddStringToObject(meta, "name", name);
    if (description) {
        cJSON_AddStringToObject(meta, "description", description);
    }

    /* Timestamp */
    time(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cJSON_AddStringToObject(meta, "created", timestamp);

    /* Machine info */
    cJSON_AddStringToObject(meta, "machine", machine_get_name());
    cJSON_AddStringToObject(meta, "vice_version", VERSION);

    /* Save options */
    cJSON_AddBoolToObject(meta, "includes_roms", include_roms ? 1 : 0);
    cJSON_AddBoolToObject(meta, "includes_disks", include_disks ? 1 : 0);

    /* Write to file */
    json_str = cJSON_Print(meta);
    cJSON_Delete(meta);
    if (json_str == NULL) {
        lib_free(json_path);
        return -1;
    }

    f = fopen(json_path, "w");
    if (f == NULL) {
        free(json_str);
        lib_free(json_path);
        return -1;
    }
    fputs(json_str, f);
    fclose(f);

    free(json_str);
    lib_free(json_path);
    return 0;
}

cJSON* mcp_read_snapshot_metadata(const char *vsf_path)
{
    char *json_path;
    size_t json_path_len;
    FILE *f;
    long file_size;
    char *json_str;
    cJSON *meta;

    /* Create .json path from .vsf path */
    json_path_len = strlen(vsf_path);
    if (json_path_len < 4 || strcmp(vsf_path + json_path_len - 4, ".vsf") != 0) {
        /* Path must end with .vsf */
        return NULL;
    }
    json_path = lib_malloc(json_path_len + 2);  /* .vsf(4) -> .json(5) + NUL */
    if (json_path == NULL) {
        return NULL;
    }
    strcpy(json_path, vsf_path);
    strcpy(json_path + json_path_len - 4, ".json");

    /* Read file */
    f = fopen(json_path, "r");
    if (f == NULL) {
        lib_free(json_path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 65536) {  /* Sanity limit */
        fclose(f);
        lib_free(json_path);
        return NULL;
    }

    json_str = lib_malloc(file_size + 1);
    if (json_str == NULL) {
        fclose(f);
        lib_free(json_path);
        return NULL;
    }

    if (fread(json_str, 1, file_size, f) != (size_t)file_size) {
        lib_free(json_str);
        fclose(f);
        lib_free(json_path);
        return NULL;
    }
    json_str[file_size] = '\0';
    fclose(f);
    lib_free(json_path);

    meta = cJSON_Parse(json_str);
    lib_free(json_str);

    return meta;
}

/* -------------------------------------------------------------------------
 * Simple condition parser for checkpoint conditions
 * ------------------------------------------------------------------------- */

cond_node_t* parse_simple_condition(const char *condition_str)
{
    cond_node_t *node = NULL;
    char reg_name[8];
    MON_REG reg_num = 0;
    unsigned int value;
    const char *p = condition_str;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Parse register name */
    if (util_strncasecmp(p, "A", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "A");
        reg_num = e_A;
        p += 1;
    } else if (util_strncasecmp(p, "X", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "X");
        reg_num = e_X;
        p += 1;
    } else if (util_strncasecmp(p, "Y", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "Y");
        reg_num = e_Y;
        p += 1;
    } else if (util_strncasecmp(p, "PC", 2) == 0 && !isalnum((unsigned char)p[2])) {
        strcpy(reg_name, "PC");
        reg_num = e_PC;
        p += 2;
    } else if (util_strncasecmp(p, "SP", 2) == 0 && !isalnum((unsigned char)p[2])) {
        strcpy(reg_name, "SP");
        reg_num = e_SP;
        p += 2;
    } else {
        log_error(mcp_tools_log, "Unknown register in condition: %s", condition_str);
        return NULL;
    }

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Parse operator (must be == or =) */
    if (*p == '=' && *(p+1) == '=') {
        p += 2;
    } else if (*p == '=') {
        p += 1;
    } else {
        log_error(mcp_tools_log, "Expected '==' or '=' in condition: %s", condition_str);
        return NULL;
    }

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Parse value (hex with $ prefix or decimal) */
    if (*p == '$') {
        p++;
        if (sscanf(p, "%x", &value) != 1) {
            log_error(mcp_tools_log, "Invalid hex value in condition: %s", condition_str);
            return NULL;
        }
    } else if (*p == '0' && (*(p+1) == 'x' || *(p+1) == 'X')) {
        p += 2;
        if (sscanf(p, "%x", &value) != 1) {
            log_error(mcp_tools_log, "Invalid hex value in condition: %s", condition_str);
            return NULL;
        }
    } else if (isdigit((unsigned char)*p)) {
        if (sscanf(p, "%u", &value) != 1) {
            log_error(mcp_tools_log, "Invalid decimal value in condition: %s", condition_str);
            return NULL;
        }
    } else {
        log_error(mcp_tools_log, "Expected numeric value in condition: %s", condition_str);
        return NULL;
    }

    /* Build condition node tree: REG == VALUE */
    /* This creates: (register) == (constant) */
    node = new_cond;
    if (node == NULL) {
        return NULL;
    }
    memset(node, 0, sizeof(cond_node_t));

    node->operation = e_EQU;  /* Equals comparison */
    node->is_parenthized = false;

    /* Left child: register reference */
    node->child1 = new_cond;
    if (node->child1 == NULL) {
        lib_free(node);
        return NULL;
    }
    memset(node->child1, 0, sizeof(cond_node_t));
    node->child1->is_reg = true;
    node->child1->reg_num = reg_num;
    node->child1->banknum = -1;
    node->child1->child1 = NULL;
    node->child1->child2 = NULL;

    /* Right child: constant value */
    node->child2 = new_cond;
    if (node->child2 == NULL) {
        lib_free(node->child1);
        lib_free(node);
        return NULL;
    }
    memset(node->child2, 0, sizeof(cond_node_t));
    node->child2->is_reg = false;
    node->child2->value = (int)value;
    node->child2->banknum = -1;
    node->child2->child1 = NULL;
    node->child2->child2 = NULL;

    log_message(mcp_tools_log, "Parsed condition: %s == %u", reg_name, value);

    return node;
}
