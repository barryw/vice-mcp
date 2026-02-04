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
#include <ctype.h>

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
#include "monitor/mon_breakpoint.h"
#include "monitor/mon_disassemble.h"
#include "monitor/montypes.h"
#include "monitor.h"  /* For monitor_startup_trap(), mon_load_symbols, etc */
#include "attach.h"
#include "imagecontents.h"
#include "imagecontents/diskcontents.h"
#include "imagecontents/diskcontents-block.h"
#include "vdrive/vdrive.h"
#include "charset.h"
#include "lib.h"
#include "screenshot.h"
#include "machine-video.h"
#include "videoarch.h"
#include "autostart.h"
#include "kbdbuf.h"
#include "keyboard.h"
#include "alarm.h"
#include "joyport/joystick.h"
#include "arch/shared/hotkeys/vhkkeysyms.h"
#include "archdep_user_config_path.h"
#include "archdep_mkdir.h"
#include "util.h"
#include <sys/stat.h>
#include <time.h>
#include <dirent.h>

static log_t mcp_tools_log = LOG_DEFAULT;
static int mcp_tools_initialized = 0;  /* Double-initialization guard */

/* -------------------------------------------------------------------------
 * Checkpoint Group Management (MCP-side bookkeeping)
 *
 * Groups are purely MCP-side - VICE checkpoints don't have native groups.
 * We track membership here and iterate when toggling.
 * ------------------------------------------------------------------------- */
#define MCP_MAX_GROUPS 32
#define MCP_MAX_CHECKPOINTS_PER_GROUP 64
#define MCP_MAX_GROUP_NAME_LEN 64

typedef struct {
    char name[MCP_MAX_GROUP_NAME_LEN];
    int checkpoint_ids[MCP_MAX_CHECKPOINTS_PER_GROUP];
    int checkpoint_count;
    int active;  /* 1 if in use, 0 if slot is free */
} mcp_checkpoint_group_t;

static mcp_checkpoint_group_t checkpoint_groups[MCP_MAX_GROUPS];
static int checkpoint_groups_initialized = 0;

/* Initialize checkpoint groups (called on first use) */
static void mcp_checkpoint_groups_init(void)
{
    int i;
    if (!checkpoint_groups_initialized) {
        for (i = 0; i < MCP_MAX_GROUPS; i++) {
            checkpoint_groups[i].name[0] = '\0';
            checkpoint_groups[i].checkpoint_count = 0;
            checkpoint_groups[i].active = 0;
        }
        checkpoint_groups_initialized = 1;
    }
}

/* Reset all checkpoint groups (for testing)
 * Note: Exposed for test harness, not for general use */
extern void mcp_checkpoint_groups_reset(void);  /* Forward declaration for prototype */
void mcp_checkpoint_groups_reset(void)
{
    int i;
    for (i = 0; i < MCP_MAX_GROUPS; i++) {
        checkpoint_groups[i].name[0] = '\0';
        checkpoint_groups[i].checkpoint_count = 0;
        checkpoint_groups[i].active = 0;
    }
    checkpoint_groups_initialized = 1;
}

/* Find a group by name. Returns index or -1 if not found */
static int mcp_checkpoint_group_find(const char *name)
{
    int i;
    mcp_checkpoint_groups_init();
    for (i = 0; i < MCP_MAX_GROUPS; i++) {
        if (checkpoint_groups[i].active &&
            strcmp(checkpoint_groups[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find a free group slot. Returns index or -1 if full */
static int mcp_checkpoint_group_find_free(void)
{
    int i;
    mcp_checkpoint_groups_init();
    for (i = 0; i < MCP_MAX_GROUPS; i++) {
        if (!checkpoint_groups[i].active) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * Auto-Snapshot Configuration (MCP-side bookkeeping)
 *
 * When a checkpoint hits, automatically save a snapshot with sequential naming.
 * This is MCP-side config storage - the actual snapshot-on-hit requires
 * VICE integration via a callback from the checkpoint system.
 *
 * Ring buffer behavior: when max_snapshots exceeded, oldest is deleted.
 * Naming pattern: {prefix}_{hit_count:03d}.vsf
 * Example: ai_move_001.vsf, ai_move_002.vsf, ...
 * ------------------------------------------------------------------------- */
#define MCP_MAX_AUTO_SNAPSHOTS 64
#define MCP_MAX_SNAPSHOT_PREFIX_LEN 64

typedef struct {
    int checkpoint_id;                        /* Associated checkpoint */
    char prefix[MCP_MAX_SNAPSHOT_PREFIX_LEN]; /* Filename prefix */
    int max_snapshots;                        /* Ring buffer size (default 10) */
    int include_disks;                        /* Include disk state in snapshots */
    int hit_count;                            /* Tracks current position in ring */
    int active;                               /* 1 if in use, 0 if slot is free */
} mcp_auto_snapshot_config_t;

static mcp_auto_snapshot_config_t auto_snapshot_configs[MCP_MAX_AUTO_SNAPSHOTS];
static int auto_snapshot_configs_initialized = 0;

/* Initialize auto-snapshot configs (called on first use) */
static void mcp_auto_snapshot_configs_init(void)
{
    int i;
    if (!auto_snapshot_configs_initialized) {
        for (i = 0; i < MCP_MAX_AUTO_SNAPSHOTS; i++) {
            auto_snapshot_configs[i].checkpoint_id = -1;
            auto_snapshot_configs[i].prefix[0] = '\0';
            auto_snapshot_configs[i].max_snapshots = 10;
            auto_snapshot_configs[i].include_disks = 0;
            auto_snapshot_configs[i].hit_count = 0;
            auto_snapshot_configs[i].active = 0;
        }
        auto_snapshot_configs_initialized = 1;
    }
}

/* Reset all auto-snapshot configs (for testing)
 * Note: Exposed for test harness, not for general use */
extern void mcp_auto_snapshot_configs_reset(void);  /* Forward declaration for prototype */
void mcp_auto_snapshot_configs_reset(void)
{
    int i;
    for (i = 0; i < MCP_MAX_AUTO_SNAPSHOTS; i++) {
        auto_snapshot_configs[i].checkpoint_id = -1;
        auto_snapshot_configs[i].prefix[0] = '\0';
        auto_snapshot_configs[i].max_snapshots = 10;
        auto_snapshot_configs[i].include_disks = 0;
        auto_snapshot_configs[i].hit_count = 0;
        auto_snapshot_configs[i].active = 0;
    }
    auto_snapshot_configs_initialized = 1;
}

/* Find auto-snapshot config by checkpoint_id. Returns index or -1 if not found */
static int mcp_auto_snapshot_find(int checkpoint_id)
{
    int i;
    mcp_auto_snapshot_configs_init();
    for (i = 0; i < MCP_MAX_AUTO_SNAPSHOTS; i++) {
        if (auto_snapshot_configs[i].active &&
            auto_snapshot_configs[i].checkpoint_id == checkpoint_id) {
            return i;
        }
    }
    return -1;
}

/* Find a free auto-snapshot config slot. Returns index or -1 if full */
static int mcp_auto_snapshot_find_free(void)
{
    int i;
    mcp_auto_snapshot_configs_init();
    for (i = 0; i < MCP_MAX_AUTO_SNAPSHOTS; i++) {
        if (!auto_snapshot_configs[i].active) {
            return i;
        }
    }
    return -1;
}

/* -------------------------------------------------------------------------
 * Execution Trace Configuration (MCP-side bookkeeping)
 *
 * Tracing records CPU instruction execution to a file for analysis.
 * This is MCP-side config storage - the actual CPU hook integration
 * with VICE is a separate task (requires hooking VICE's CPU execution loop).
 *
 * MCP stores: trace config (what to trace, where to output, filters)
 * VICE provides: actual instruction logging via CPU hook
 *
 * Output format (plain text):
 *   $C000: LDA #$00
 *   $C002: STA $D020
 *
 * With registers:
 *   $C000: LDA #$00    [A=00 X=FF Y=00 SP=FF P=32]
 * ------------------------------------------------------------------------- */
#define MCP_MAX_TRACE_CONFIGS 16
#define MCP_MAX_TRACE_FILE_LEN 256

typedef struct {
    char trace_id[32];              /* Unique trace identifier */
    char output_file[MCP_MAX_TRACE_FILE_LEN]; /* Output file path */
    uint16_t pc_filter_start;       /* Start address for PC filter (0 = no filter) */
    uint16_t pc_filter_end;         /* End address for PC filter (0xFFFF = no filter) */
    int max_instructions;           /* Maximum instructions to record (default 10000) */
    int include_registers;          /* Include register state in output */
    int instructions_recorded;      /* Number of instructions recorded so far */
    unsigned long start_cycles;     /* Cycle count at trace start */
    int active;                     /* 1 if trace is active, 0 if slot is free */
} mcp_trace_config_t;

static mcp_trace_config_t trace_configs[MCP_MAX_TRACE_CONFIGS];
static int trace_configs_initialized = 0;
static int trace_id_counter = 0;  /* For generating unique trace IDs */

/* Initialize trace configs (called on first use) */
static void mcp_trace_configs_init(void)
{
    int i;
    if (!trace_configs_initialized) {
        for (i = 0; i < MCP_MAX_TRACE_CONFIGS; i++) {
            trace_configs[i].trace_id[0] = '\0';
            trace_configs[i].output_file[0] = '\0';
            trace_configs[i].pc_filter_start = 0;
            trace_configs[i].pc_filter_end = 0xFFFF;
            trace_configs[i].max_instructions = 10000;
            trace_configs[i].include_registers = 0;
            trace_configs[i].instructions_recorded = 0;
            trace_configs[i].start_cycles = 0;
            trace_configs[i].active = 0;
        }
        trace_configs_initialized = 1;
    }
}

/* Reset all trace configs (for testing)
 * Note: Exposed for test harness, not for general use */
extern void mcp_trace_configs_reset(void);  /* Forward declaration for prototype */
void mcp_trace_configs_reset(void)
{
    int i;
    for (i = 0; i < MCP_MAX_TRACE_CONFIGS; i++) {
        trace_configs[i].trace_id[0] = '\0';
        trace_configs[i].output_file[0] = '\0';
        trace_configs[i].pc_filter_start = 0;
        trace_configs[i].pc_filter_end = 0xFFFF;
        trace_configs[i].max_instructions = 10000;
        trace_configs[i].include_registers = 0;
        trace_configs[i].instructions_recorded = 0;
        trace_configs[i].start_cycles = 0;
        trace_configs[i].active = 0;
    }
    trace_configs_initialized = 1;
    trace_id_counter = 0;
}

/* Find a trace config by trace_id. Returns index or -1 if not found */
static int mcp_trace_find(const char *trace_id)
{
    int i;
    mcp_trace_configs_init();
    for (i = 0; i < MCP_MAX_TRACE_CONFIGS; i++) {
        if (trace_configs[i].active &&
            strcmp(trace_configs[i].trace_id, trace_id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find a free trace config slot. Returns index or -1 if full */
static int mcp_trace_find_free(void)
{
    int i;
    mcp_trace_configs_init();
    for (i = 0; i < MCP_MAX_TRACE_CONFIGS; i++) {
        if (!trace_configs[i].active) {
            return i;
        }
    }
    return -1;
}

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

/* JSON Schema helper - creates empty object schema (for tools with no parameters)
 * Returns JSON Schema object, or NULL on allocation failure.
 */
static cJSON* mcp_schema_empty(void)
{
    cJSON *schema = cJSON_CreateObject();
    if (schema == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(schema, "type", "object");
    cJSON_AddFalseToObject(schema, "additionalProperties");
    return schema;
}

/* JSON Schema helper - creates object schema with properties
 * Returns JSON Schema object, or NULL on allocation failure.
 * Caller must provide properties object (ownership transferred).
 * required_array is optional (may be NULL).
 */
static cJSON* mcp_schema_object(cJSON *properties, cJSON *required_array)
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

/* JSON Schema property helper - number with description */
static cJSON* mcp_prop_number(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "number");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

/* JSON Schema property helper - string with description */
static cJSON* mcp_prop_string(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "string");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

/* JSON Schema property helper - boolean with description */
static cJSON* mcp_prop_boolean(const char *desc)
{
    cJSON *prop = cJSON_CreateObject();
    if (prop == NULL) return NULL;
    cJSON_AddStringToObject(prop, "type", "boolean");
    if (desc) cJSON_AddStringToObject(prop, "description", desc);
    return prop;
}

/* JSON Schema property helper - array with description */
static cJSON* mcp_prop_array(const char *item_type, const char *desc)
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
 * Symbol-aware address resolution helper
 *
 * Resolves an address from a cJSON value that can be:
 *   - A number (direct address)
 *   - A string starting with '$' (hex address, e.g., "$1000")
 *   - A string (symbol name to look up)
 *
 * Returns the resolved address, or -1 if resolution fails.
 * If error_msg is provided, it will be set to an error message on failure.
 * ------------------------------------------------------------------------- */
static int mcp_resolve_address(cJSON *value, const char **error_msg)
{
    static const char *err_null = "address value is null";
    static const char *err_invalid = "address must be number or string";
    static const char *err_symbol = "symbol not found";
    static const char *err_hex = "invalid hex address";

    if (value == NULL) {
        if (error_msg) *error_msg = err_null;
        return -1;
    }

    /* Direct number */
    if (cJSON_IsNumber(value)) {
        return value->valueint;
    }

    /* String - could be symbol name or hex address */
    if (cJSON_IsString(value)) {
        const char *str = value->valuestring;

        /* Check for hex format: $xxxx or 0xXXXX */
        if (str[0] == '$') {
            unsigned int addr;
            if (sscanf(str + 1, "%x", &addr) == 1) {
                return (int)addr;
            }
            if (error_msg) *error_msg = err_hex;
            return -1;
        }
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            unsigned int addr;
            if (sscanf(str + 2, "%x", &addr) == 1) {
                return (int)addr;
            }
            if (error_msg) *error_msg = err_hex;
            return -1;
        }

        /* Try as symbol name */
        int addr = mon_symbol_table_lookup_addr(e_comp_space, (char *)str);
        if (addr >= 0) {
            return addr;
        }
        if (error_msg) *error_msg = err_symbol;
        return -1;
    }

    if (error_msg) *error_msg = err_invalid;
    return -1;
}

/* -------------------------------------------------------------------------
 * Snapshot directory management
 *
 * Creates and returns the path to the MCP snapshots directory.
 * Path: ~/.config/vice/mcp_snapshots/
 * Returns NULL on failure. Caller must free result.
 * ------------------------------------------------------------------------- */
static char* mcp_get_snapshots_dir(void)
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

/* -------------------------------------------------------------------------
 * VSF (VICE Snapshot File) Parser for Memory Extraction
 *
 * Parses .vsf files to extract memory contents from the C64MEM module
 * without loading the full snapshot into the emulator. This allows
 * comparing current memory against a saved snapshot state.
 *
 * VSF Format (relevant parts):
 *   Header:
 *     - Magic: "VICE Snapshot File\032" (19 bytes)
 *     - Version: major (1), minor (1)
 *     - Machine name: 16 bytes padded string
 *     - VICE version magic: "VICE Version\032" (13 bytes)
 *     - VICE version: 4 bytes + revision 4 bytes
 *   Modules (repeated):
 *     - Name: 16 bytes padded string
 *     - Major/minor version: 2 bytes
 *     - Size: 4 bytes little-endian (includes header = 22 + data)
 *     - Data: (size - 22) bytes
 *
 * C64MEM module data format (version 0.1):
 *     - pport.data: 1 byte
 *     - pport.dir: 1 byte
 *     - export.exrom: 1 byte
 *     - export.game: 1 byte
 *     - RAM: 65536 bytes <-- This is what we extract
 *     - Additional port state: 14 bytes
 *
 * Returns: Allocated buffer with 64KB of RAM, or NULL on error.
 *          Caller must free the returned buffer with lib_free().
 * ------------------------------------------------------------------------- */

/* Magic strings for VSF format validation */
#define VSF_MAGIC_STRING     "VICE Snapshot File\032"
#define VSF_MAGIC_LEN        19
#define VSF_MODULE_NAME_LEN  16
#define VSF_MACHINE_NAME_LEN 16
#define VSF_VERSION_MAGIC    "VICE Version\032"
#define VSF_VERSION_MAGIC_LEN 13

/* C64 memory size */
#define C64_RAM_SIZE 65536

/* Read a little-endian 32-bit value from a byte array */
static uint32_t vsf_read_dword(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* Extract C64 RAM from a VSF snapshot file.
 *
 * Parameters:
 *   vsf_path: Path to the .vsf file
 *   ram_buffer: Pre-allocated buffer of at least 65536 bytes
 *
 * Returns: 0 on success, negative error code on failure
 *   -1: Cannot open file
 *   -2: Invalid magic string
 *   -3: Machine mismatch (not C64)
 *   -4: C64MEM module not found
 *   -5: Module too small for RAM data
 *   -6: Read error
 */
static int vsf_extract_c64_ram(const char *vsf_path, uint8_t *ram_buffer)
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

/* Write JSON metadata sidecar file for snapshot */
static int mcp_write_snapshot_metadata(const char *vsf_path, const char *name,
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
    json_path_len = strlen(vsf_path) + 2;  /* .vsf -> .json (+2 chars) */
    json_path = lib_malloc(json_path_len);
    if (json_path == NULL) {
        return -1;
    }
    strcpy(json_path, vsf_path);
    /* Replace .vsf extension with .json */
    strcpy(json_path + strlen(json_path) - 4, ".json");

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
        lib_free(json_str);
        lib_free(json_path);
        return -1;
    }
    fputs(json_str, f);
    fclose(f);

    lib_free(json_str);
    lib_free(json_path);
    return 0;
}

/* Read JSON metadata sidecar file for snapshot */
static cJSON* mcp_read_snapshot_metadata(const char *vsf_path)
{
    char *json_path;
    size_t json_path_len;
    FILE *f;
    long file_size;
    char *json_str;
    cJSON *meta;

    /* Create .json path from .vsf path */
    json_path_len = strlen(vsf_path) + 2;
    json_path = lib_malloc(json_path_len);
    if (json_path == NULL) {
        return NULL;
    }
    strcpy(json_path, vsf_path);
    strcpy(json_path + strlen(json_path) - 4, ".json");

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

/* Forward declarations for tools defined after registry */
static cJSON* mcp_tool_tools_call(cJSON *params);
static cJSON* mcp_tool_disassemble(cJSON *params);
static cJSON* mcp_tool_symbols_load(cJSON *params);
static cJSON* mcp_tool_symbols_lookup(cJSON *params);
static cJSON* mcp_tool_watch_add(cJSON *params);
static cJSON* mcp_tool_machine_reset(cJSON *params);
static cJSON* mcp_tool_backtrace(cJSON *params);
static cJSON* mcp_tool_keyboard_matrix(cJSON *params);
static cJSON* mcp_tool_run_until(cJSON *params);
/* snapshot tools are declared extern in mcp_tools.h */

/* Tool registry - const to prevent modification */
static const mcp_tool_t tool_registry[] = {
    /* MCP Base Protocol */
    { "initialize", "Initialize MCP session", mcp_tool_initialize },
    { "notifications/initialized", "Client initialized notification", mcp_tool_initialized_notification },

    /* Meta */
    { "tools/list", "List all available tools with schemas", mcp_tool_tools_list },
    { "tools/call", "Call a tool by name", mcp_tool_tools_call },

    /* Phase 1: Core tools */
    { "vice.ping", "Check if VICE is responding", mcp_tool_ping },
    { "vice.execution.run", "Resume execution", mcp_tool_execution_run },
    { "vice.execution.pause", "Pause execution", mcp_tool_execution_pause },
    { "vice.execution.step", "Step one or more instructions", mcp_tool_execution_step },
    { "vice.registers.get", "Get CPU registers", mcp_tool_registers_get },
    { "vice.registers.set", "Set CPU register value", mcp_tool_registers_set },
    { "vice.memory.read", "Read memory range (with optional bank selection)", mcp_tool_memory_read },
    { "vice.memory.write", "Write to memory", mcp_tool_memory_write },
    { "vice.memory.banks", "List available memory banks", mcp_tool_memory_banks },
    { "vice.memory.search", "Search for byte patterns in memory with optional wildcards", mcp_tool_memory_search },

    /* Phase 2.1: Checkpoints/Breakpoints */
    { "vice.checkpoint.add", "Add checkpoint/breakpoint", mcp_tool_checkpoint_add },
    { "vice.checkpoint.delete", "Delete checkpoint", mcp_tool_checkpoint_delete },
    { "vice.checkpoint.list", "List all checkpoints", mcp_tool_checkpoint_list },
    { "vice.checkpoint.toggle", "Enable/disable checkpoint", mcp_tool_checkpoint_toggle },
    { "vice.checkpoint.set_condition", "Set checkpoint condition", mcp_tool_checkpoint_set_condition },
    { "vice.checkpoint.set_ignore_count", "Set checkpoint ignore count", mcp_tool_checkpoint_set_ignore_count },

    /* Phase 2.2: Sprite Control (C64/C128/DTV only) */
    { "vice.sprite.get", "Get sprite state", mcp_tool_sprite_get },
    { "vice.sprite.set", "Set sprite properties", mcp_tool_sprite_set },

    /* Phase 2.3: Chip State Access */
    { "vice.vicii.get_state", "Get VIC-II internal state", mcp_tool_vicii_get_state },
    { "vice.vicii.set_state", "Set VIC-II registers", mcp_tool_vicii_set_state },
    { "vice.sid.get_state", "Get SID state (voices, filter)", mcp_tool_sid_get_state },
    { "vice.sid.set_state", "Set SID registers", mcp_tool_sid_set_state },
    { "vice.cia.get_state", "Get CIA state (timers, ports)", mcp_tool_cia_get_state },
    { "vice.cia.set_state", "Set CIA registers", mcp_tool_cia_set_state },

    /* Phase 2.4: Disk Management */
    { "vice.disk.attach", "Attach disk image to drive", mcp_tool_disk_attach },
    { "vice.disk.detach", "Detach disk image from drive", mcp_tool_disk_detach },
    { "vice.disk.list", "List directory contents", mcp_tool_disk_list },
    { "vice.disk.read_sector", "Read raw sector data", mcp_tool_disk_read_sector },

    /* Autostart */
    { "vice.autostart", "Autostart a PRG or disk image", mcp_tool_autostart },

    /* Machine control */
    { "vice.machine.reset", "Reset the machine (soft or hard reset)", mcp_tool_machine_reset },

    /* Phase 2.5: Display Capture */
    { "vice.display.screenshot", "Capture screenshot (to file or base64)", mcp_tool_display_screenshot },
    { "vice.display.get_dimensions", "Get display dimensions", mcp_tool_display_get_dimensions },

    /* Phase 3.1: Input Control */
    { "vice.keyboard.type", "Type text (uppercase ASCII displays as uppercase on C64 by default)", mcp_tool_keyboard_type },
    { "vice.keyboard.key_press", "Press a specific key", mcp_tool_keyboard_key_press },
    { "vice.keyboard.key_release", "Release a specific key", mcp_tool_keyboard_key_release },
    { "vice.joystick.set", "Set joystick state", mcp_tool_joystick_set },

    /* Phase 4: Advanced Debugging */
    { "vice.disassemble", "Disassemble memory to 6502 instructions", mcp_tool_disassemble },
    { "vice.symbols.load", "Load symbol/label file (VICE, KickAssembler, simple formats)", mcp_tool_symbols_load },
    { "vice.symbols.lookup", "Lookup symbol by name or address", mcp_tool_symbols_lookup },
    { "vice.watch.add", "Add memory watchpoint with optional condition (shorthand for checkpoint with store/load)", mcp_tool_watch_add },
    { "vice.backtrace", "Show call stack (JSR return addresses)", mcp_tool_backtrace },
    { "vice.run_until", "Run until address or for N cycles (with timeout)", mcp_tool_run_until },
    { "vice.keyboard.matrix", "Direct keyboard matrix control (for games that scan keyboard directly)", mcp_tool_keyboard_matrix },

    /* Snapshot Management */
    { "vice.snapshot.save",
      "Save complete emulator state to a named snapshot. "
      "Use this to capture a debugging checkpoint that can be restored later with snapshot.load. "
      "Snapshots are stored in ~/.config/vice/mcp_snapshots/ with JSON metadata.",
      mcp_tool_snapshot_save },
    { "vice.snapshot.load",
      "Restore emulator state from a previously saved snapshot. "
      "Use this to return to a known debugging checkpoint without re-running setup steps. "
      "The emulator will be in the exact state it was when the snapshot was saved.",
      mcp_tool_snapshot_load },
    { "vice.snapshot.list",
      "List all available snapshots with their metadata. "
      "Returns snapshot names, descriptions, creation times, and machine types. "
      "Use this to find the right snapshot to load for debugging.",
      mcp_tool_snapshot_list },

    /* Phase 5.1: Enhanced Debugging Tools */
    { "vice.cycles.stopwatch",
      "Measure elapsed CPU cycles for timing-critical code analysis. "
      "Use 'reset' to start timing, 'read' to get elapsed cycles, or 'reset_and_read' "
      "for atomic read-and-reset. Ideal for measuring raster routine timing.",
      mcp_tool_cycles_stopwatch },
    { "vice.memory.fill",
      "Fill memory range with a repeating byte pattern. "
      "Useful for clearing memory, creating NOP sleds, or setting up test conditions. "
      "The pattern repeats to fill the entire range from start to end (inclusive).",
      mcp_tool_memory_fill },

    /* Phase 5.2: Memory Compare */
    { "vice.memory.compare",
      "Compare two memory ranges or compare current memory against a snapshot. "
      "Use mode='ranges' to compare two live memory ranges, or mode='snapshot' to compare "
      "against a previously saved state. Returns list of differences with addresses and values.",
      mcp_tool_memory_compare },

    /* Phase 5.3: Checkpoint Groups */
    { "vice.checkpoint.group.create",
      "Create a named checkpoint group for batch operations. "
      "Groups allow you to enable/disable multiple breakpoints at once. "
      "Optionally include initial checkpoint_ids array.",
      mcp_tool_checkpoint_group_create },
    { "vice.checkpoint.group.add",
      "Add checkpoints to an existing group. "
      "Use this to grow a group after creation.",
      mcp_tool_checkpoint_group_add },
    { "vice.checkpoint.group.toggle",
      "Enable or disable all checkpoints in a group. "
      "Use enabled=true to enable, enabled=false to disable.",
      mcp_tool_checkpoint_group_toggle },
    { "vice.checkpoint.group.list",
      "List all checkpoint groups with their member counts. "
      "Returns group names, checkpoint IDs, and enabled/disabled counts.",
      mcp_tool_checkpoint_group_list },

    /* Phase 5.3: Auto-Snapshot on Checkpoint Hit */
    { "vice.checkpoint.set_auto_snapshot",
      "Configure automatic snapshot on checkpoint hit. "
      "When the checkpoint triggers, saves to {prefix}_{hit_count:03d}.vsf with ring buffer. "
      "NOTE: Actual triggering requires VICE callback integration (config stored MCP-side).",
      mcp_tool_checkpoint_set_auto_snapshot },
    { "vice.checkpoint.clear_auto_snapshot",
      "Remove auto-snapshot configuration from a checkpoint. "
      "Stops automatic snapshot-on-hit behavior for this checkpoint.",
      mcp_tool_checkpoint_clear_auto_snapshot },

    /* Phase 5.4: Execution Tracing */
    { "vice.trace.start",
      "Start execution trace recording to a file. "
      "Records disassembled instructions as they execute, optionally filtered by PC range. "
      "NOTE: Actual tracing requires VICE CPU hook integration (config stored MCP-side).",
      mcp_tool_trace_start },
    { "vice.trace.stop",
      "Stop an active execution trace and get recording statistics. "
      "Returns instructions recorded, output file path, and cycles elapsed.",
      mcp_tool_trace_stop },

    /* Phase 5.4: Interrupt Logging */
    { "vice.interrupt.log.start",
      "Start logging interrupt events (IRQ, NMI, BRK). "
      "Returns log_id for later reference. Filter by interrupt types and set max entries. "
      "NOTE: Actual logging requires VICE interrupt hook integration (config stored MCP-side).",
      mcp_tool_interrupt_log_start },
    { "vice.interrupt.log.stop",
      "Stop an active interrupt log and retrieve all recorded entries. "
      "Returns entries with type, cycle, pc, vector_address, and handler_address.",
      mcp_tool_interrupt_log_stop },
    { "vice.interrupt.log.read",
      "Read entries from an active interrupt log without stopping it. "
      "Supports incremental reads via since_index parameter.",
      mcp_tool_interrupt_log_read },

    /* Phase 5.5: Memory Map */
    { "vice.memory.map",
      "Display memory region layout with optional symbol-based content hints. "
      "Returns the C64 memory map showing RAM, ROM, I/O, and cartridge regions. "
      "When symbols are loaded, adds content_hint showing symbols in each region.",
      mcp_tool_memory_map },

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

    /* Report execution state based on exit_mon flag
     * exit_mon_no = 0: paused (in monitor)
     * exit_mon_continue = 1: running
     * Other values indicate transitions */
    switch (exit_mon) {
        case 0:  /* exit_mon_no */
            exec_state = "paused";
            break;
        case 1:  /* exit_mon_continue */
            exec_state = "running";
            break;
        default:
            exec_state = "transitioning";
            break;
    }
    cJSON_AddStringToObject(response, "execution", exec_state);

    return response;
}

/* =================================================================
 * MCP Base Protocol Handlers
 * ================================================================= */

cJSON* mcp_tool_initialize(cJSON *params)
{
    cJSON *response, *capabilities, *server_info;
    cJSON *protocol_version;
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
        return NULL;
    }

    /* Protocol version we're using */
    cJSON_AddStringToObject(response, "protocolVersion", "2025-11-25");

    /* Server capabilities */
    capabilities = cJSON_CreateObject();
    if (capabilities == NULL) {
        cJSON_Delete(response);
        return NULL;
    }

    /* Add logging capability (like working servers) */
    cJSON *logging_cap = cJSON_CreateObject();
    if (logging_cap != NULL) {
        cJSON_AddItemToObject(capabilities, "logging", logging_cap);
    }

    /* Add tools capability with listChanged */
    cJSON *tools_cap = cJSON_CreateObject();
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

/* =================================================================
 * Meta Tools
 * ================================================================= */

/* tools/call - MCP standard method to invoke a tool */
static cJSON* mcp_tool_tools_call(cJSON *params)
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
            cJSON *tool_result = tool_registry[i].handler(args_item);

            /* Clean up args if we created it */
            if (args_created) {
                cJSON_Delete(args_item);
                args_item = NULL;
            }

            /* Wrap result in MCP tools/call response format */
            cJSON *response = cJSON_CreateObject();
            if (response == NULL) {
                if (tool_result != NULL) cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            /* Check if tool returned an error */
            cJSON *code_item = cJSON_GetObjectItem(tool_result, "code");
            if (code_item != NULL && cJSON_IsNumber(code_item)) {
                /* Tool returned an error - pass it through */
                cJSON_Delete(response);
                return tool_result;
            }

            /* Wrap successful result in content array */
            cJSON *content = cJSON_CreateArray();
            if (content == NULL) {
                cJSON_Delete(response);
                if (tool_result != NULL) cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            cJSON *text_content = cJSON_CreateObject();
            if (text_content == NULL) {
                cJSON_Delete(content);
                cJSON_Delete(response);
                if (tool_result != NULL) cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            cJSON_AddStringToObject(text_content, "type", "text");

            /* Convert tool result to JSON string for text content */
            char *result_str = cJSON_PrintUnformatted(tool_result);
            if (result_str == NULL) {
                cJSON_Delete(text_content);
                cJSON_Delete(content);
                cJSON_Delete(response);
                if (tool_result != NULL) cJSON_Delete(tool_result);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            cJSON_AddStringToObject(text_content, "text", result_str);
            free(result_str);

            if (tool_result != NULL) cJSON_Delete(tool_result);

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

/* This is the replacement for mcp_tool_tools_list - using proper JSON Schema */
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
                   strcmp(name, "vice.sprite.get") == 0 ||
                   strcmp(name, "vice.vicii.get_state") == 0 ||
                   strcmp(name, "vice.vicii.set_state") == 0 ||
                   strcmp(name, "vice.sid.get_state") == 0 ||
                   strcmp(name, "vice.sid.set_state") == 0 ||
                   strcmp(name, "vice.cia.get_state") == 0 ||
                   strcmp(name, "vice.cia.set_state") == 0 ||
                   strcmp(name, "vice.display.get_dimensions") == 0) {
            /* Tools with no parameters */
            schema = mcp_schema_empty();

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

        } else if (strcmp(name, "vice.keyboard.key_press") == 0 || strcmp(name, "vice.keyboard.key_release") == 0) {
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
cJSON* mcp_tool_execution_run(cJSON *params)
{
    cJSON *response;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.execution.run");

    /* Signal monitor to exit and continue execution */
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

    /* Trigger monitor entry via interrupt trap
     * This will pause execution at the next safe point */
    monitor_startup_trap();

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "message", "Pause requested (will stop at next safe point)");

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
    } else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown register name (must be PC, A, X, Y, SP, V, B, D, I, or C)");
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

    /* Handle optional bank parameter */
    if (bank_item != NULL) {
        if (cJSON_IsString(bank_item)) {
            bank_name = bank_item->valuestring;
            bank = mem_bank_from_name(bank_name);
            if (bank < 0) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown bank name (use vice.memory.banks to list available banks)");
            }
        } else if (cJSON_IsNumber(bank_item)) {
            bank = bank_item->valueint;
        } else {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Bank must be a string name or number");
        }
    }

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

        /* Use bank-specific read if bank specified, otherwise use CPU view */
        if (bank >= 0) {
            value = mem_bank_peek(bank, addr, NULL);
        } else {
            value = mem_read(addr);
        }
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
    int start_addr, end_addr;
    int pattern_len, mask_len;
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
            uint8_t mem_byte = mem_read((uint16_t)(check_addr + i));
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

/* ------------------------------------------------------------------------- */
/* Notification Helpers */

/* ========================================================================= */
/* Phase 2.1: Checkpoint/Breakpoint Tools                                   */
/* ========================================================================= */

cJSON* mcp_tool_checkpoint_add(cJSON *params)
{
    cJSON *response, *start_item, *end_item, *stop_item;
    cJSON *load_item, *store_item, *exec_item;
    MON_ADDR start_addr, end_addr;
    MEMORY_OP op = 0;
    bool stop = true;
    int checkpoint_num;
    const char *error_msg;
    int resolved;

    log_message(mcp_tools_log, "Handling vice.checkpoint.add");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get start address (required) - can be number, hex string, or symbol */
    start_item = cJSON_GetObjectItem(params, "start");
    if (start_item == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start address required");
    }
    resolved = mcp_resolve_address(start_item, &error_msg);
    if (resolved < 0) {
        char err_buf[128];
        snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
        return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
    }
    start_addr = (MON_ADDR)resolved;

    /* Get end address (optional, defaults to start) - can be number, hex string, or symbol */
    end_item = cJSON_GetObjectItem(params, "end");
    if (end_item != NULL) {
        resolved = mcp_resolve_address(end_item, &error_msg);
        if (resolved < 0) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
        }
        end_addr = (MON_ADDR)resolved;
    } else {
        end_addr = start_addr;
    }

    /* Get stop flag (optional, defaults to true) */
    stop_item = cJSON_GetObjectItem(params, "stop");
    if (stop_item != NULL && cJSON_IsBool(stop_item)) {
        stop = cJSON_IsTrue(stop_item);
    }

    /* Get operation type flags (optional, defaults to exec only) */
    load_item = cJSON_GetObjectItem(params, "load");
    store_item = cJSON_GetObjectItem(params, "store");
    exec_item = cJSON_GetObjectItem(params, "exec");

    if (load_item != NULL && cJSON_IsTrue(load_item)) {
        op |= e_load;
    }
    if (store_item != NULL && cJSON_IsTrue(store_item)) {
        op |= e_store;
    }
    if (exec_item != NULL && cJSON_IsTrue(exec_item)) {
        op |= e_exec;
    }

    /* If no operation specified, default to exec (PC breakpoint) */
    if (op == 0) {
        op = e_exec;
    }

    /* Create checkpoint using VICE monitor API */
    checkpoint_num = mon_breakpoint_add_checkpoint(start_addr, end_addr, stop, op, false, true);

    if (checkpoint_num < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to create checkpoint");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);
    /* Extract 16-bit address from MON_ADDR (high bits contain memory space) */
    cJSON_AddNumberToObject(response, "start", addr_location(start_addr));
    cJSON_AddNumberToObject(response, "end", addr_location(end_addr));
    cJSON_AddBoolToObject(response, "stop", stop);
    cJSON_AddBoolToObject(response, "load", (op & e_load) != 0);
    cJSON_AddBoolToObject(response, "store", (op & e_store) != 0);
    cJSON_AddBoolToObject(response, "exec", (op & e_exec) != 0);

    return response;
}

cJSON* mcp_tool_checkpoint_delete(cJSON *params)
{
    cJSON *response, *num_item;
    int checkpoint_num;

    log_message(mcp_tools_log, "Handling vice.checkpoint.delete");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    num_item = cJSON_GetObjectItem(params, "checkpoint_num");
    if (!cJSON_IsNumber(num_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "checkpoint_num required");
    }

    checkpoint_num = num_item->valueint;

    /* Verify checkpoint exists */
    if (mon_breakpoint_find_checkpoint(checkpoint_num) == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Checkpoint not found");
    }

    /* Delete it */
    mon_breakpoint_delete_checkpoint(checkpoint_num);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);

    return response;
}

cJSON* mcp_tool_checkpoint_list(cJSON *params)
{
    cJSON *response, *checkpoints_array;
    mon_checkpoint_t **checkpoint_list;
    unsigned int count, i;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.checkpoint.list");

    /* Get checkpoint list from VICE */
    checkpoint_list = mon_breakpoint_checkpoint_list_get(&count);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    checkpoints_array = cJSON_CreateArray();
    if (checkpoints_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add each checkpoint to array */
    for (i = 0; i < count; i++) {
        mon_checkpoint_t *cp = checkpoint_list[i];
        cJSON *cp_obj = cJSON_CreateObject();
        char *start_symbol, *end_symbol;
        uint16_t start_loc, end_loc;

        if (cp_obj == NULL) {
            cJSON_Delete(checkpoints_array);
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Extract 16-bit address from MON_ADDR (high bits contain memory space) */
        start_loc = addr_location(cp->start_addr);
        end_loc = addr_location(cp->end_addr);

        cJSON_AddNumberToObject(cp_obj, "checkpoint_num", cp->checknum);
        cJSON_AddNumberToObject(cp_obj, "start", start_loc);
        cJSON_AddNumberToObject(cp_obj, "end", end_loc);

        /* Add symbol names if available */
        start_symbol = mon_symbol_table_lookup_name(e_comp_space, start_loc);
        if (start_symbol != NULL) {
            cJSON_AddStringToObject(cp_obj, "start_symbol", start_symbol);
        }
        end_symbol = mon_symbol_table_lookup_name(e_comp_space, end_loc);
        if (end_symbol != NULL && end_loc != start_loc) {
            cJSON_AddStringToObject(cp_obj, "end_symbol", end_symbol);
        }

        cJSON_AddNumberToObject(cp_obj, "hit_count", cp->hit_count);
        cJSON_AddNumberToObject(cp_obj, "ignore_count", cp->ignore_count);
        cJSON_AddBoolToObject(cp_obj, "stop", cp->stop);
        cJSON_AddBoolToObject(cp_obj, "enabled", cp->enabled);
        cJSON_AddBoolToObject(cp_obj, "check_load", cp->check_load);
        cJSON_AddBoolToObject(cp_obj, "check_store", cp->check_store);
        cJSON_AddBoolToObject(cp_obj, "check_exec", cp->check_exec);
        cJSON_AddBoolToObject(cp_obj, "temporary", cp->temporary);

        if (cp->condition != NULL) {
            cJSON_AddStringToObject(cp_obj, "condition", "<expression>");  /* TODO: serialize condition */
        }

        cJSON_AddItemToArray(checkpoints_array, cp_obj);
    }

    cJSON_AddItemToObject(response, "checkpoints", checkpoints_array);
    cJSON_AddNumberToObject(response, "count", count);

    return response;
}

cJSON* mcp_tool_checkpoint_toggle(cJSON *params)
{
    cJSON *response, *num_item, *enabled_item;
    int checkpoint_num;
    bool enabled;

    log_message(mcp_tools_log, "Handling vice.checkpoint.toggle");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    num_item = cJSON_GetObjectItem(params, "checkpoint_num");
    if (!cJSON_IsNumber(num_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "checkpoint_num required");
    }

    enabled_item = cJSON_GetObjectItem(params, "enabled");
    if (!cJSON_IsBool(enabled_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "enabled (bool) required");
    }

    checkpoint_num = num_item->valueint;
    enabled = cJSON_IsTrue(enabled_item);

    /* Verify checkpoint exists */
    if (mon_breakpoint_find_checkpoint(checkpoint_num) == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Checkpoint not found");
    }

    /* Toggle it (op=1 for enable, op=2 for disable) */
    mon_breakpoint_switch_checkpoint(enabled ? 1 : 2, checkpoint_num);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);
    cJSON_AddBoolToObject(response, "enabled", enabled);

    return response;
}

/* Simple condition parser for basic register/memory comparisons
 *
 * Supported conditions:
 *   A == $xx     - Accumulator equals hex value
 *   A == 42      - Accumulator equals decimal value
 *   X == $xx     - X register equals value
 *   Y == $xx     - Y register equals value
 *   PC == $xxxx  - Program counter equals value
 *   SP == $xx    - Stack pointer equals value
 *
 * Note: Full condition parsing (with memory access, arithmetic, etc.)
 * would require integrating with VICE's monitor expression parser.
 * This simple parser covers the most common debugging use cases.
 */
static cond_node_t* parse_simple_condition(const char *condition_str)
{
    cond_node_t *node = NULL;
    char reg_name[8];
    MON_REG reg_num = 0;
    unsigned int value;
    const char *p = condition_str;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Parse register name */
    if (strncasecmp(p, "A", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "A");
        reg_num = e_A;
        p += 1;
    } else if (strncasecmp(p, "X", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "X");
        reg_num = e_X;
        p += 1;
    } else if (strncasecmp(p, "Y", 1) == 0 && !isalnum((unsigned char)p[1])) {
        strcpy(reg_name, "Y");
        reg_num = e_Y;
        p += 1;
    } else if (strncasecmp(p, "PC", 2) == 0 && !isalnum((unsigned char)p[2])) {
        strcpy(reg_name, "PC");
        reg_num = e_PC;
        p += 2;
    } else if (strncasecmp(p, "SP", 2) == 0 && !isalnum((unsigned char)p[2])) {
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

cJSON* mcp_tool_checkpoint_set_condition(cJSON *params)
{
    cJSON *response, *num_item, *condition_item;
    int checkpoint_num;
    const char *condition_str;
    cond_node_t *cond_node;

    log_message(mcp_tools_log, "Handling vice.checkpoint.set_condition");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    num_item = cJSON_GetObjectItem(params, "checkpoint_num");
    if (!cJSON_IsNumber(num_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "checkpoint_num required");
    }

    condition_item = cJSON_GetObjectItem(params, "condition");
    if (!cJSON_IsString(condition_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "condition (string) required");
    }

    checkpoint_num = num_item->valueint;
    condition_str = condition_item->valuestring;

    /* Verify checkpoint exists */
    if (mon_breakpoint_find_checkpoint(checkpoint_num) == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Checkpoint not found");
    }

    /* Parse condition string */
    cond_node = parse_simple_condition(condition_str);
    if (cond_node == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid condition. Supported: 'A == $xx', 'X == $xx', 'Y == $xx', 'PC == $xxxx', 'SP == $xx' (hex with $, 0x, or decimal)");
    }

    /* Set the condition on the checkpoint */
    mon_breakpoint_set_checkpoint_condition(checkpoint_num, cond_node);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);
    cJSON_AddStringToObject(response, "condition", condition_str);

    return response;
}

cJSON* mcp_tool_checkpoint_set_ignore_count(cJSON *params)
{
    cJSON *response, *num_item, *count_item;
    int checkpoint_num, ignore_count;
    char *params_str;

    log_message(mcp_tools_log, "Handling vice.checkpoint.set_ignore_count");

    if (params == NULL) {
        log_error(mcp_tools_log, "checkpoint_set_ignore_count: params is NULL");
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Debug: dump params JSON */
    params_str = cJSON_PrintUnformatted(params);
    if (params_str != NULL) {
        log_message(mcp_tools_log, "checkpoint_set_ignore_count params: %s", params_str);
        free(params_str);
    }

    num_item = cJSON_GetObjectItem(params, "checkpoint_num");
    if (!cJSON_IsNumber(num_item)) {
        log_error(mcp_tools_log, "checkpoint_set_ignore_count: checkpoint_num not found or not a number");
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "checkpoint_num required");
    }

    count_item = cJSON_GetObjectItem(params, "count");
    if (!cJSON_IsNumber(count_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "count (number) required");
    }

    checkpoint_num = num_item->valueint;
    ignore_count = count_item->valueint;

    if (ignore_count < 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "count must be >= 0");
    }

    /* Verify checkpoint exists */
    if (mon_breakpoint_find_checkpoint(checkpoint_num) == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Checkpoint not found");
    }

    /* Set ignore count */
    mon_breakpoint_set_ignore_count(checkpoint_num, ignore_count);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "checkpoint_num", checkpoint_num);
    cJSON_AddNumberToObject(response, "ignore_count", ignore_count);

    return response;
}

/* ========================================================================= */
/* Phase 5.3: Checkpoint Group Tools (MCP-side bookkeeping)                  */
/* ========================================================================= */

/* vice.checkpoint.group.create - Create a named checkpoint group
 *
 * Parameters:
 *   - name (string, required): Group name
 *   - checkpoint_ids (array of numbers, optional): Initial checkpoint IDs
 *
 * Returns:
 *   - created (bool): true if created
 *   - name (string): Group name
 */
cJSON* mcp_tool_checkpoint_group_create(cJSON *params)
{
    cJSON *response, *name_item, *ids_item;
    const char *name;
    int group_idx, i;

    log_message(mcp_tools_log, "Handling vice.checkpoint.group.create");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    name_item = cJSON_GetObjectItem(params, "name");
    if (!cJSON_IsString(name_item) || name_item->valuestring == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "name (string) required");
    }
    name = name_item->valuestring;

    /* Check name length */
    if (strlen(name) >= MCP_MAX_GROUP_NAME_LEN) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Group name too long");
    }

    /* Check if group already exists */
    if (mcp_checkpoint_group_find(name) >= 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Group already exists");
    }

    /* Find a free slot */
    group_idx = mcp_checkpoint_group_find_free();
    if (group_idx < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Maximum groups reached");
    }

    /* Initialize the group */
    strncpy(checkpoint_groups[group_idx].name, name, MCP_MAX_GROUP_NAME_LEN - 1);
    checkpoint_groups[group_idx].name[MCP_MAX_GROUP_NAME_LEN - 1] = '\0';
    checkpoint_groups[group_idx].checkpoint_count = 0;
    checkpoint_groups[group_idx].active = 1;

    /* Add initial checkpoint IDs if provided */
    ids_item = cJSON_GetObjectItem(params, "checkpoint_ids");
    if (cJSON_IsArray(ids_item)) {
        int array_size = cJSON_GetArraySize(ids_item);
        for (i = 0; i < array_size && checkpoint_groups[group_idx].checkpoint_count < MCP_MAX_CHECKPOINTS_PER_GROUP; i++) {
            cJSON *id_item = cJSON_GetArrayItem(ids_item, i);
            if (cJSON_IsNumber(id_item)) {
                checkpoint_groups[group_idx].checkpoint_ids[checkpoint_groups[group_idx].checkpoint_count++] = id_item->valueint;
            }
        }
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(response, "created", 1);
    cJSON_AddStringToObject(response, "name", name);

    return response;
}

/* vice.checkpoint.group.add - Add checkpoints to an existing group
 *
 * Parameters:
 *   - group (string, required): Group name
 *   - checkpoint_ids (array of numbers, required): Checkpoint IDs to add
 *
 * Returns:
 *   - added (number): Count of checkpoints added
 */
cJSON* mcp_tool_checkpoint_group_add(cJSON *params)
{
    cJSON *response, *group_item, *ids_item;
    const char *group_name;
    int group_idx, i, added_count = 0;

    log_message(mcp_tools_log, "Handling vice.checkpoint.group.add");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    group_item = cJSON_GetObjectItem(params, "group");
    if (!cJSON_IsString(group_item) || group_item->valuestring == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "group (string) required");
    }
    group_name = group_item->valuestring;

    ids_item = cJSON_GetObjectItem(params, "checkpoint_ids");
    if (!cJSON_IsArray(ids_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "checkpoint_ids (array) required");
    }

    /* Find the group */
    group_idx = mcp_checkpoint_group_find(group_name);
    if (group_idx < 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Group not found");
    }

    /* Add checkpoint IDs */
    int array_size = cJSON_GetArraySize(ids_item);
    for (i = 0; i < array_size; i++) {
        cJSON *id_item = cJSON_GetArrayItem(ids_item, i);
        if (cJSON_IsNumber(id_item)) {
            if (checkpoint_groups[group_idx].checkpoint_count < MCP_MAX_CHECKPOINTS_PER_GROUP) {
                checkpoint_groups[group_idx].checkpoint_ids[checkpoint_groups[group_idx].checkpoint_count++] = id_item->valueint;
                added_count++;
            }
        }
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "added", added_count);

    return response;
}

/* vice.checkpoint.group.toggle - Enable/disable all checkpoints in a group
 *
 * Parameters:
 *   - group (string, required): Group name
 *   - enabled (boolean, required): Enable or disable
 *
 * Returns:
 *   - affected_count (number): Number of checkpoints toggled
 */
cJSON* mcp_tool_checkpoint_group_toggle(cJSON *params)
{
    cJSON *response, *group_item, *enabled_item;
    const char *group_name;
    int group_idx, i, affected_count = 0;
    bool enabled;

    log_message(mcp_tools_log, "Handling vice.checkpoint.group.toggle");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    group_item = cJSON_GetObjectItem(params, "group");
    if (!cJSON_IsString(group_item) || group_item->valuestring == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "group (string) required");
    }
    group_name = group_item->valuestring;

    enabled_item = cJSON_GetObjectItem(params, "enabled");
    if (!cJSON_IsBool(enabled_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "enabled (boolean) required");
    }
    enabled = cJSON_IsTrue(enabled_item);

    /* Find the group */
    group_idx = mcp_checkpoint_group_find(group_name);
    if (group_idx < 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Group not found");
    }

    /* Toggle all checkpoints in the group */
    for (i = 0; i < checkpoint_groups[group_idx].checkpoint_count; i++) {
        int checkpoint_num = checkpoint_groups[group_idx].checkpoint_ids[i];

        /* Verify checkpoint still exists before toggling */
        if (mon_breakpoint_find_checkpoint(checkpoint_num) != NULL) {
            /* Toggle it (op=1 for enable, op=2 for disable) */
            mon_breakpoint_switch_checkpoint(enabled ? 1 : 2, checkpoint_num);
            affected_count++;
        }
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "affected_count", affected_count);

    return response;
}

/* vice.checkpoint.group.list - List all checkpoint groups
 *
 * Returns:
 *   - groups (array): Array of group objects with:
 *     - name (string): Group name
 *     - checkpoint_ids (array): Checkpoint IDs in the group
 *     - enabled_count (number): Count of enabled checkpoints
 *     - disabled_count (number): Count of disabled checkpoints
 */
cJSON* mcp_tool_checkpoint_group_list(cJSON *params)
{
    cJSON *response, *groups_array;
    int i;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.checkpoint.group.list");

    mcp_checkpoint_groups_init();

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    groups_array = cJSON_CreateArray();
    if (groups_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    for (i = 0; i < MCP_MAX_GROUPS; i++) {
        if (checkpoint_groups[i].active) {
            cJSON *group_obj = cJSON_CreateObject();
            cJSON *ids_array;
            int j, enabled_count = 0, disabled_count = 0;

            if (group_obj == NULL) {
                cJSON_Delete(groups_array);
                cJSON_Delete(response);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }

            cJSON_AddStringToObject(group_obj, "name", checkpoint_groups[i].name);

            /* Create checkpoint_ids array */
            ids_array = cJSON_CreateArray();
            if (ids_array != NULL) {
                for (j = 0; j < checkpoint_groups[i].checkpoint_count; j++) {
                    int cp_num = checkpoint_groups[i].checkpoint_ids[j];
                    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(cp_num));

                    /* Check if checkpoint exists and is enabled */
                    mon_checkpoint_t *cp = mon_breakpoint_find_checkpoint(cp_num);
                    if (cp != NULL) {
                        if (cp->enabled) {
                            enabled_count++;
                        } else {
                            disabled_count++;
                        }
                    }
                }
                cJSON_AddItemToObject(group_obj, "checkpoint_ids", ids_array);
            }

            cJSON_AddNumberToObject(group_obj, "enabled_count", enabled_count);
            cJSON_AddNumberToObject(group_obj, "disabled_count", disabled_count);

            cJSON_AddItemToArray(groups_array, group_obj);
        }
    }

    cJSON_AddItemToObject(response, "groups", groups_array);

    return response;
}

/* ========================================================================= */
/* Phase 2.2: Sprite Control Tools (C64/C128/DTV only)                      */
/* ========================================================================= */

/* VIC-II sprite register addresses */
#define VICII_BASE 0xD000
#define VICII_SPRITE_ENABLE 0xD015
#define VICII_SPRITE_X_MSB 0xD010
#define VICII_SPRITE_MULTICOLOR 0xD01C
#define VICII_SPRITE_EXPAND_Y 0xD017
#define VICII_SPRITE_EXPAND_X 0xD01D
#define VICII_SPRITE_PRIORITY 0xD01B
#define VICII_SPRITE_COLOR_BASE 0xD027

cJSON* mcp_tool_sprite_get(cJSON *params)
{
    cJSON *response, *sprite_obj, *sprite_item;
    int sprite_num = -1;  /* -1 = all sprites */
    int i, start, end;

    log_message(mcp_tools_log, "Handling vice.sprite.get");

    /* Check if specific sprite requested */
    if (params != NULL) {
        sprite_item = cJSON_GetObjectItem(params, "sprite");
        if (sprite_item != NULL && cJSON_IsNumber(sprite_item)) {
            sprite_num = sprite_item->valueint;
            if (sprite_num < 0 || sprite_num > 7) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite must be 0-7");
            }
        }
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Determine sprite range */
    if (sprite_num >= 0) {
        start = sprite_num;
        end = sprite_num;
    } else {
        start = 0;
        end = 7;
    }

    /* Read sprite data for requested sprite(s) */
    for (i = start; i <= end; i++) {
        uint16_t x, y;
        uint8_t enable_reg, x_msb_reg, multicolor_reg, expand_x_reg, expand_y_reg;
        uint8_t priority_reg, color;
        char sprite_key[16];

        /* Read VIC-II registers */
        x = mem_read(VICII_BASE + (i * 2));  /* $D000, $D002, $D004, ... */
        y = mem_read(VICII_BASE + (i * 2) + 1);  /* $D001, $D003, $D005, ... */
        enable_reg = mem_read(VICII_SPRITE_ENABLE);
        x_msb_reg = mem_read(VICII_SPRITE_X_MSB);
        multicolor_reg = mem_read(VICII_SPRITE_MULTICOLOR);
        expand_x_reg = mem_read(VICII_SPRITE_EXPAND_X);
        expand_y_reg = mem_read(VICII_SPRITE_EXPAND_Y);
        priority_reg = mem_read(VICII_SPRITE_PRIORITY);
        color = mem_read(VICII_SPRITE_COLOR_BASE + i);  /* $D027-$D02E */

        /* Build sprite object */
        sprite_obj = cJSON_CreateObject();
        if (sprite_obj == NULL) {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Calculate full X coordinate (9 bits) */
        if (x_msb_reg & (1 << i)) {
            x |= 0x100;
        }

        cJSON_AddNumberToObject(sprite_obj, "sprite", i);
        cJSON_AddNumberToObject(sprite_obj, "x", x);
        cJSON_AddNumberToObject(sprite_obj, "y", y);
        cJSON_AddBoolToObject(sprite_obj, "enabled", (enable_reg & (1 << i)) != 0);
        cJSON_AddBoolToObject(sprite_obj, "multicolor", (multicolor_reg & (1 << i)) != 0);
        cJSON_AddBoolToObject(sprite_obj, "expand_x", (expand_x_reg & (1 << i)) != 0);
        cJSON_AddBoolToObject(sprite_obj, "expand_y", (expand_y_reg & (1 << i)) != 0);
        cJSON_AddBoolToObject(sprite_obj, "priority_foreground", (priority_reg & (1 << i)) == 0);
        cJSON_AddNumberToObject(sprite_obj, "color", color);

        /* Add to response (use array if all sprites, object if single sprite) */
        if (sprite_num >= 0) {
            /* Single sprite - add directly to response */
            cJSON_AddItemToObject(response, "sprite_data", sprite_obj);
        } else {
            /* Multiple sprites - add to array */
            sprintf(sprite_key, "sprite_%d", i);
            cJSON_AddItemToObject(response, sprite_key, sprite_obj);
        }
    }

    return response;
}

cJSON* mcp_tool_sprite_set(cJSON *params)
{
    cJSON *response, *sprite_item, *x_item, *y_item;
    cJSON *enabled_item, *multicolor_item, *expand_x_item, *expand_y_item;
    cJSON *priority_item, *color_item;
    int sprite_num;
    uint8_t enable_reg, x_msb_reg, multicolor_reg, expand_x_reg, expand_y_reg;
    uint8_t priority_reg;

    log_message(mcp_tools_log, "Handling vice.sprite.set");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get sprite number (required) */
    sprite_item = cJSON_GetObjectItem(params, "sprite");
    if (!cJSON_IsNumber(sprite_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite (0-7) required");
    }
    sprite_num = sprite_item->valueint;
    if (sprite_num < 0 || sprite_num > 7) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite must be 0-7");
    }

    /* Read current register values (for partial updates) */
    enable_reg = mem_read(VICII_SPRITE_ENABLE);
    x_msb_reg = mem_read(VICII_SPRITE_X_MSB);
    multicolor_reg = mem_read(VICII_SPRITE_MULTICOLOR);
    expand_x_reg = mem_read(VICII_SPRITE_EXPAND_X);
    expand_y_reg = mem_read(VICII_SPRITE_EXPAND_Y);
    priority_reg = mem_read(VICII_SPRITE_PRIORITY);

    /* Update X position if specified */
    x_item = cJSON_GetObjectItem(params, "x");
    if (x_item != NULL && cJSON_IsNumber(x_item)) {
        int x = x_item->valueint;
        if (x < 0 || x > 511) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "x must be 0-511");
        }
        mem_store(VICII_BASE + (sprite_num * 2), x & 0xFF);
        if (x >= 256) {
            x_msb_reg |= (1 << sprite_num);
        } else {
            x_msb_reg &= ~(1 << sprite_num);
        }
        mem_store(VICII_SPRITE_X_MSB, x_msb_reg);
    }

    /* Update Y position if specified */
    y_item = cJSON_GetObjectItem(params, "y");
    if (y_item != NULL && cJSON_IsNumber(y_item)) {
        int y = y_item->valueint;
        if (y < 0 || y > 255) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "y must be 0-255");
        }
        mem_store(VICII_BASE + (sprite_num * 2) + 1, y);
    }

    /* Update enabled flag if specified */
    enabled_item = cJSON_GetObjectItem(params, "enabled");
    if (enabled_item != NULL && cJSON_IsBool(enabled_item)) {
        if (cJSON_IsTrue(enabled_item)) {
            enable_reg |= (1 << sprite_num);
        } else {
            enable_reg &= ~(1 << sprite_num);
        }
        mem_store(VICII_SPRITE_ENABLE, enable_reg);
    }

    /* Update multicolor flag if specified */
    multicolor_item = cJSON_GetObjectItem(params, "multicolor");
    if (multicolor_item != NULL && cJSON_IsBool(multicolor_item)) {
        if (cJSON_IsTrue(multicolor_item)) {
            multicolor_reg |= (1 << sprite_num);
        } else {
            multicolor_reg &= ~(1 << sprite_num);
        }
        mem_store(VICII_SPRITE_MULTICOLOR, multicolor_reg);
    }

    /* Update expand_x flag if specified */
    expand_x_item = cJSON_GetObjectItem(params, "expand_x");
    if (expand_x_item != NULL && cJSON_IsBool(expand_x_item)) {
        if (cJSON_IsTrue(expand_x_item)) {
            expand_x_reg |= (1 << sprite_num);
        } else {
            expand_x_reg &= ~(1 << sprite_num);
        }
        mem_store(VICII_SPRITE_EXPAND_X, expand_x_reg);
    }

    /* Update expand_y flag if specified */
    expand_y_item = cJSON_GetObjectItem(params, "expand_y");
    if (expand_y_item != NULL && cJSON_IsBool(expand_y_item)) {
        if (cJSON_IsTrue(expand_y_item)) {
            expand_y_reg |= (1 << sprite_num);
        } else {
            expand_y_reg &= ~(1 << sprite_num);
        }
        mem_store(VICII_SPRITE_EXPAND_Y, expand_y_reg);
    }

    /* Update priority if specified */
    priority_item = cJSON_GetObjectItem(params, "priority_foreground");
    if (priority_item != NULL && cJSON_IsBool(priority_item)) {
        if (cJSON_IsTrue(priority_item)) {
            priority_reg &= ~(1 << sprite_num);  /* 0 = foreground */
        } else {
            priority_reg |= (1 << sprite_num);   /* 1 = background */
        }
        mem_store(VICII_SPRITE_PRIORITY, priority_reg);
    }

    /* Update color if specified */
    color_item = cJSON_GetObjectItem(params, "color");
    if (color_item != NULL && cJSON_IsNumber(color_item)) {
        int color = color_item->valueint;
        if (color < 0 || color > 15) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "color must be 0-15");
        }
        mem_store(VICII_SPRITE_COLOR_BASE + sprite_num, color);
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "sprite", sprite_num);

    return response;
}

/* ========================================================================= */
/* Phase 2.3: Chip State Access                                             */
/* ========================================================================= */

#define SID_BASE 0xD400
#define CIA1_BASE 0xDC00
#define CIA2_BASE 0xDD00

cJSON* mcp_tool_vicii_get_state(cJSON *params)
{
    cJSON *response, *registers_obj;
    int i;
    uint8_t d011, d012;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.vicii.get_state");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Read key VIC-II registers */
    d011 = mem_read(VICII_BASE + 0x11);  /* Control register 1 */
    d012 = mem_read(VICII_BASE + 0x12);  /* Raster line */

    /* Add computed/useful state */
    cJSON_AddNumberToObject(response, "raster_line", d012 | ((d011 & 0x80) << 1));  /* 9-bit raster */
    cJSON_AddNumberToObject(response, "video_mode", ((d011 & 0x60) | (mem_read(VICII_BASE + 0x16) & 0x10)) >> 4);
    cJSON_AddBoolToObject(response, "screen_enabled", (d011 & 0x10) != 0);
    cJSON_AddBoolToObject(response, "25_rows", (d011 & 0x08) != 0);
    cJSON_AddNumberToObject(response, "y_scroll", d011 & 0x07);
    cJSON_AddNumberToObject(response, "x_scroll", mem_read(VICII_BASE + 0x16) & 0x07);

    /* Border colors */
    cJSON_AddNumberToObject(response, "border_color", mem_read(VICII_BASE + 0x20));
    cJSON_AddNumberToObject(response, "background_color_0", mem_read(VICII_BASE + 0x21));
    cJSON_AddNumberToObject(response, "background_color_1", mem_read(VICII_BASE + 0x22));
    cJSON_AddNumberToObject(response, "background_color_2", mem_read(VICII_BASE + 0x23));
    cJSON_AddNumberToObject(response, "background_color_3", mem_read(VICII_BASE + 0x24));

    /* Sprite collisions */
    cJSON_AddNumberToObject(response, "sprite_sprite_collision", mem_read(VICII_BASE + 0x1E));
    cJSON_AddNumberToObject(response, "sprite_background_collision", mem_read(VICII_BASE + 0x1F));

    /* IRQ status */
    cJSON_AddNumberToObject(response, "irq_status", mem_read(VICII_BASE + 0x19));
    cJSON_AddNumberToObject(response, "irq_enabled", mem_read(VICII_BASE + 0x1A));

    /* Memory pointers */
    cJSON_AddNumberToObject(response, "memory_pointers", mem_read(VICII_BASE + 0x18));

    /* Add all registers as array for completeness */
    registers_obj = cJSON_CreateArray();
    if (registers_obj == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    for (i = 0; i < 0x2F; i++) {
        cJSON_AddItemToArray(registers_obj, cJSON_CreateNumber(mem_read(VICII_BASE + i)));
    }

    cJSON_AddItemToObject(response, "registers", registers_obj);

    return response;
}

cJSON* mcp_tool_sid_get_state(cJSON *params)
{
    cJSON *response, *voices_array, *voice_obj;
    int v;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.sid.get_state");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    voices_array = cJSON_CreateArray();
    if (voices_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Read all 3 voices */
    for (v = 0; v < 3; v++) {
        uint16_t freq, pulse_width;
        uint8_t control, attack_decay, sustain_release;
        int base = SID_BASE + (v * 7);

        freq = mem_read(base + 0) | (mem_read(base + 1) << 8);
        pulse_width = mem_read(base + 2) | (mem_read(base + 3) << 8);
        control = mem_read(base + 4);
        attack_decay = mem_read(base + 5);
        sustain_release = mem_read(base + 6);

        voice_obj = cJSON_CreateObject();
        if (voice_obj == NULL) {
            cJSON_Delete(voices_array);
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        cJSON_AddNumberToObject(voice_obj, "voice", v + 1);
        cJSON_AddNumberToObject(voice_obj, "frequency", freq);
        cJSON_AddNumberToObject(voice_obj, "pulse_width", pulse_width & 0xFFF);
        cJSON_AddBoolToObject(voice_obj, "noise", (control & 0x80) != 0);
        cJSON_AddBoolToObject(voice_obj, "pulse", (control & 0x40) != 0);
        cJSON_AddBoolToObject(voice_obj, "sawtooth", (control & 0x20) != 0);
        cJSON_AddBoolToObject(voice_obj, "triangle", (control & 0x10) != 0);
        cJSON_AddBoolToObject(voice_obj, "test", (control & 0x08) != 0);
        cJSON_AddBoolToObject(voice_obj, "ring_mod", (control & 0x04) != 0);
        cJSON_AddBoolToObject(voice_obj, "sync", (control & 0x02) != 0);
        cJSON_AddBoolToObject(voice_obj, "gate", (control & 0x01) != 0);
        cJSON_AddNumberToObject(voice_obj, "attack", (attack_decay >> 4) & 0x0F);
        cJSON_AddNumberToObject(voice_obj, "decay", attack_decay & 0x0F);
        cJSON_AddNumberToObject(voice_obj, "sustain", (sustain_release >> 4) & 0x0F);
        cJSON_AddNumberToObject(voice_obj, "release", sustain_release & 0x0F);

        cJSON_AddItemToArray(voices_array, voice_obj);
    }

    cJSON_AddItemToObject(response, "voices", voices_array);

    /* Filter and volume */
    cJSON_AddNumberToObject(response, "filter_cutoff_low", mem_read(SID_BASE + 0x15));
    cJSON_AddNumberToObject(response, "filter_cutoff_high", mem_read(SID_BASE + 0x16));
    cJSON_AddNumberToObject(response, "filter_resonance", (mem_read(SID_BASE + 0x17) >> 4) & 0x0F);
    cJSON_AddBoolToObject(response, "filter_voice3", (mem_read(SID_BASE + 0x17) & 0x04) != 0);
    cJSON_AddBoolToObject(response, "filter_voice2", (mem_read(SID_BASE + 0x17) & 0x02) != 0);
    cJSON_AddBoolToObject(response, "filter_voice1", (mem_read(SID_BASE + 0x17) & 0x01) != 0);
    cJSON_AddBoolToObject(response, "filter_ext", (mem_read(SID_BASE + 0x18) & 0x08) != 0);
    cJSON_AddBoolToObject(response, "voice3_off", (mem_read(SID_BASE + 0x18) & 0x80) != 0);
    cJSON_AddBoolToObject(response, "highpass", (mem_read(SID_BASE + 0x18) & 0x40) != 0);
    cJSON_AddBoolToObject(response, "bandpass", (mem_read(SID_BASE + 0x18) & 0x20) != 0);
    cJSON_AddBoolToObject(response, "lowpass", (mem_read(SID_BASE + 0x18) & 0x10) != 0);
    cJSON_AddNumberToObject(response, "volume", mem_read(SID_BASE + 0x18) & 0x0F);

    return response;
}

cJSON* mcp_tool_cia_get_state(cJSON *params)
{
    cJSON *response, *cia1_obj, *cia2_obj, *cia_item;
    int cia;

    /* Note: cJSON_GetObjectItem safely returns NULL when params is NULL */
    cia_item = cJSON_GetObjectItem(params, "cia");
    if (cia_item != NULL && cJSON_IsNumber(cia_item)) {
        cia = cia_item->valueint;
        if (cia != 1 && cia != 2) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "cia must be 1 or 2");
        }
    } else {
        cia = 0;  /* Both */
    }

    log_message(mcp_tools_log, "Handling vice.cia.get_state (CIA %d)", cia);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Helper function to build CIA state */
    #define BUILD_CIA_STATE(obj, base) do { \
        uint16_t timer_a, timer_b; \
        obj = cJSON_CreateObject(); \
        if (obj == NULL) { \
            cJSON_Delete(response); \
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory"); \
        } \
        cJSON_AddNumberToObject(obj, "port_a", mem_read(base + 0)); \
        cJSON_AddNumberToObject(obj, "port_b", mem_read(base + 1)); \
        cJSON_AddNumberToObject(obj, "ddr_a", mem_read(base + 2)); \
        cJSON_AddNumberToObject(obj, "ddr_b", mem_read(base + 3)); \
        timer_a = mem_read(base + 4) | (mem_read(base + 5) << 8); \
        timer_b = mem_read(base + 6) | (mem_read(base + 7) << 8); \
        cJSON_AddNumberToObject(obj, "timer_a", timer_a); \
        cJSON_AddNumberToObject(obj, "timer_b", timer_b); \
        cJSON_AddNumberToObject(obj, "tod_10ths", mem_read(base + 8)); \
        cJSON_AddNumberToObject(obj, "tod_seconds", mem_read(base + 9)); \
        cJSON_AddNumberToObject(obj, "tod_minutes", mem_read(base + 10)); \
        cJSON_AddNumberToObject(obj, "tod_hours", mem_read(base + 11)); \
        cJSON_AddNumberToObject(obj, "serial_data", mem_read(base + 12)); \
        cJSON_AddNumberToObject(obj, "interrupt_control", mem_read(base + 13)); \
        cJSON_AddNumberToObject(obj, "control_a", mem_read(base + 14)); \
        cJSON_AddNumberToObject(obj, "control_b", mem_read(base + 15)); \
    } while (0)

    if (cia == 0 || cia == 1) {
        BUILD_CIA_STATE(cia1_obj, CIA1_BASE);
        cJSON_AddItemToObject(response, "cia1", cia1_obj);
    }

    if (cia == 0 || cia == 2) {
        BUILD_CIA_STATE(cia2_obj, CIA2_BASE);
        cJSON_AddItemToObject(response, "cia2", cia2_obj);
    }

    #undef BUILD_CIA_STATE

    return response;
}

cJSON* mcp_tool_vicii_set_state(cJSON *params)
{
    cJSON *response, *item, *registers_array;
    int updates = 0;

    log_message(mcp_tools_log, "Setting VIC-II state");

    /* All parameters optional - set only what's provided */

    /* Generic register array - allows setting any VIC-II register by offset */
    registers_array = cJSON_GetObjectItem(params, "registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i, array_size = cJSON_GetArraySize(registers_array);
        for (i = 0; i < array_size; i++) {
            cJSON *reg_obj = cJSON_GetArrayItem(registers_array, i);
            if (reg_obj != NULL && cJSON_IsObject(reg_obj)) {
                cJSON *offset_item = cJSON_GetObjectItem(reg_obj, "offset");
                cJSON *value_item = cJSON_GetObjectItem(reg_obj, "value");
                if (offset_item != NULL && value_item != NULL &&
                    cJSON_IsNumber(offset_item) && cJSON_IsNumber(value_item)) {
                    int offset = offset_item->valueint;
                    if (offset >= 0 && offset <= 0x2F) {  /* VIC-II has 48 registers */
                        mem_store(VICII_BASE + offset, value_item->valueint & 0xFF);
                        updates++;
                    }
                }
            }
        }
    }

    /* Convenient named parameters for common operations */

    /* Border color ($D020) */
    if ((item = cJSON_GetObjectItem(params, "border_color")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x20, item->valueint & 0x0F);
        updates++;
    }

    /* Background color ($D021) */
    if ((item = cJSON_GetObjectItem(params, "background_color")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x21, item->valueint & 0x0F);
        updates++;
    }

    /* Additional background colors ($D022-$D023) */
    if ((item = cJSON_GetObjectItem(params, "background_color_1")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x22, item->valueint & 0x0F);
        updates++;
    }
    if ((item = cJSON_GetObjectItem(params, "background_color_2")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x23, item->valueint & 0x0F);
        updates++;
    }
    if ((item = cJSON_GetObjectItem(params, "background_color_3")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x24, item->valueint & 0x0F);
        updates++;
    }

    /* Control register 1 ($D011) - video mode, screen enable, raster MSB */
    if ((item = cJSON_GetObjectItem(params, "control_1")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x11, item->valueint & 0xFF);
        updates++;
    }

    /* Control register 2 ($D016) - multicolor, screen width */
    if ((item = cJSON_GetObjectItem(params, "control_2")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x16, item->valueint & 0xFF);
        updates++;
    }

    /* Memory pointers ($D018) */
    if ((item = cJSON_GetObjectItem(params, "memory_pointers")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x18, item->valueint & 0xFF);
        updates++;
    }

    /* IRQ raster line (low 8 bits in $D012, bit 8 in $D011) */
    if ((item = cJSON_GetObjectItem(params, "irq_raster_line")) != NULL && cJSON_IsNumber(item)) {
        int line = item->valueint & 0x1FF;
        mem_store(VICII_BASE + 0x12, line & 0xFF);
        uint8_t d011 = mem_read(VICII_BASE + 0x11);
        if (line & 0x100) {
            d011 |= 0x80;
        } else {
            d011 &= ~0x80;
        }
        mem_store(VICII_BASE + 0x11, d011);
        updates++;
    }

    /* Interrupt enable ($D01A) */
    if ((item = cJSON_GetObjectItem(params, "interrupt_enable")) != NULL && cJSON_IsNumber(item)) {
        mem_store(VICII_BASE + 0x1A, item->valueint & 0x0F);
        updates++;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "registers_updated", updates);

    return response;
}

cJSON* mcp_tool_sid_set_state(cJSON *params)
{
    cJSON *response, *voice_item, *item, *registers_array;
    int updates = 0;
    int v;

    log_message(mcp_tools_log, "Setting SID state");

    /* Generic register array - allows setting any SID register by offset */
    registers_array = cJSON_GetObjectItem(params, "registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i, array_size = cJSON_GetArraySize(registers_array);
        for (i = 0; i < array_size; i++) {
            cJSON *reg_obj = cJSON_GetArrayItem(registers_array, i);
            if (reg_obj != NULL && cJSON_IsObject(reg_obj)) {
                cJSON *offset_item = cJSON_GetObjectItem(reg_obj, "offset");
                cJSON *value_item = cJSON_GetObjectItem(reg_obj, "value");
                if (offset_item != NULL && value_item != NULL &&
                    cJSON_IsNumber(offset_item) && cJSON_IsNumber(value_item)) {
                    int offset = offset_item->valueint;
                    if (offset >= 0 && offset <= 0x1C) {  /* SID has 29 registers */
                        mem_store(SID_BASE + offset, value_item->valueint & 0xFF);
                        updates++;
                    }
                }
            }
        }
    }

    /* Convenient named parameters for voice control */

    /* Voice parameters (voices 0-2) */
    for (v = 0; v < 3; v++) {
        char voice_key[10];
        sprintf(voice_key, "voice%d", v);

        voice_item = cJSON_GetObjectItem(params, voice_key);
        if (voice_item != NULL && cJSON_IsObject(voice_item)) {
            int base = SID_BASE + (v * 7);

            /* Frequency (16-bit) */
            if ((item = cJSON_GetObjectItem(voice_item, "frequency")) != NULL && cJSON_IsNumber(item)) {
                uint16_t freq = item->valueint & 0xFFFF;
                mem_store(base + 0, freq & 0xFF);
                mem_store(base + 1, (freq >> 8) & 0xFF);
                updates++;
            }

            /* Pulse width (12-bit) */
            if ((item = cJSON_GetObjectItem(voice_item, "pulse_width")) != NULL && cJSON_IsNumber(item)) {
                uint16_t pw = item->valueint & 0x0FFF;
                mem_store(base + 2, pw & 0xFF);
                mem_store(base + 3, (pw >> 8) & 0x0F);
                updates++;
            }

            /* Control register */
            if ((item = cJSON_GetObjectItem(voice_item, "control")) != NULL && cJSON_IsNumber(item)) {
                mem_store(base + 4, item->valueint & 0xFF);
                updates++;
            }

            /* Attack/Decay */
            if ((item = cJSON_GetObjectItem(voice_item, "attack_decay")) != NULL && cJSON_IsNumber(item)) {
                mem_store(base + 5, item->valueint & 0xFF);
                updates++;
            }

            /* Sustain/Release */
            if ((item = cJSON_GetObjectItem(voice_item, "sustain_release")) != NULL && cJSON_IsNumber(item)) {
                mem_store(base + 6, item->valueint & 0xFF);
                updates++;
            }
        }
    }

    /* Filter cutoff (11-bit) */
    if ((item = cJSON_GetObjectItem(params, "filter_cutoff")) != NULL && cJSON_IsNumber(item)) {
        uint16_t cutoff = item->valueint & 0x07FF;
        mem_store(SID_BASE + 0x15, cutoff & 0x07);  /* Low 3 bits */
        mem_store(SID_BASE + 0x16, (cutoff >> 3) & 0xFF);  /* High 8 bits */
        updates++;
    }

    /* Filter resonance and routing ($D017) */
    if ((item = cJSON_GetObjectItem(params, "filter_resonance")) != NULL && cJSON_IsNumber(item)) {
        mem_store(SID_BASE + 0x17, item->valueint & 0xFF);
        updates++;
    }

    /* Filter mode and volume ($D018) */
    if ((item = cJSON_GetObjectItem(params, "filter_mode_volume")) != NULL && cJSON_IsNumber(item)) {
        mem_store(SID_BASE + 0x18, item->valueint & 0xFF);
        updates++;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "registers_updated", updates);

    return response;
}

cJSON* mcp_tool_cia_set_state(cJSON *params)
{
    cJSON *response, *cia1_item, *cia2_item, *item, *registers_array;
    int updates = 0;

    log_message(mcp_tools_log, "Setting CIA state");

    /* Generic register arrays for CIA1 and CIA2 */
    registers_array = cJSON_GetObjectItem(params, "cia1_registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i, array_size = cJSON_GetArraySize(registers_array);
        for (i = 0; i < array_size; i++) {
            cJSON *reg_obj = cJSON_GetArrayItem(registers_array, i);
            if (reg_obj != NULL && cJSON_IsObject(reg_obj)) {
                cJSON *offset_item = cJSON_GetObjectItem(reg_obj, "offset");
                cJSON *value_item = cJSON_GetObjectItem(reg_obj, "value");
                if (offset_item != NULL && value_item != NULL &&
                    cJSON_IsNumber(offset_item) && cJSON_IsNumber(value_item)) {
                    int offset = offset_item->valueint;
                    if (offset >= 0 && offset <= 0x0F) {  /* CIA has 16 registers */
                        mem_store(CIA1_BASE + offset, value_item->valueint & 0xFF);
                        updates++;
                    }
                }
            }
        }
    }

    registers_array = cJSON_GetObjectItem(params, "cia2_registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i, array_size = cJSON_GetArraySize(registers_array);
        for (i = 0; i < array_size; i++) {
            cJSON *reg_obj = cJSON_GetArrayItem(registers_array, i);
            if (reg_obj != NULL && cJSON_IsObject(reg_obj)) {
                cJSON *offset_item = cJSON_GetObjectItem(reg_obj, "offset");
                cJSON *value_item = cJSON_GetObjectItem(reg_obj, "value");
                if (offset_item != NULL && value_item != NULL &&
                    cJSON_IsNumber(offset_item) && cJSON_IsNumber(value_item)) {
                    int offset = offset_item->valueint;
                    if (offset >= 0 && offset <= 0x0F) {  /* CIA has 16 registers */
                        mem_store(CIA2_BASE + offset, value_item->valueint & 0xFF);
                        updates++;
                    }
                }
            }
        }
    }

    /* Convenient named parameters for common CIA operations */

    /* Helper macro to set CIA registers */
    #define SET_CIA_REGS(obj, base) do { \
        if ((item = cJSON_GetObjectItem(obj, "port_a")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 0, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "port_b")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 1, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "ddr_a")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 2, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "ddr_b")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 3, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "timer_a_low")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 4, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "timer_a_high")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 5, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "timer_b_low")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 6, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "timer_b_high")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 7, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "interrupt_control")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 13, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "control_a")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 14, item->valueint & 0xFF); \
            updates++; \
        } \
        if ((item = cJSON_GetObjectItem(obj, "control_b")) != NULL && cJSON_IsNumber(item)) { \
            mem_store(base + 15, item->valueint & 0xFF); \
            updates++; \
        } \
    } while (0)

    /* CIA1 registers */
    cia1_item = cJSON_GetObjectItem(params, "cia1");
    if (cia1_item != NULL && cJSON_IsObject(cia1_item)) {
        SET_CIA_REGS(cia1_item, CIA1_BASE);
    }

    /* CIA2 registers */
    cia2_item = cJSON_GetObjectItem(params, "cia2");
    if (cia2_item != NULL && cJSON_IsObject(cia2_item)) {
        SET_CIA_REGS(cia2_item, CIA2_BASE);
    }

    #undef SET_CIA_REGS

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "registers_updated", updates);

    return response;
}

/* ========================================================================= */
/* Phase 2.4: Disk Management Tools                                         */
/* ========================================================================= */

cJSON* mcp_tool_disk_attach(cJSON *params)
{
    cJSON *response, *unit_item, *drive_item, *path_item;
    unsigned int unit, drive;
    const char *path;
    int result;

    /* Parse unit parameter (required, 8-11) */
    unit_item = cJSON_GetObjectItem(params, "unit");
    if (unit_item == NULL || !cJSON_IsNumber(unit_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'unit' parameter (8-11)");
    }
    unit = (unsigned int)unit_item->valueint;
    if (unit < 8 || unit > 11) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "unit must be 8-11");
    }

    /* Parse drive parameter (optional, 0-1, default=0) */
    drive_item = cJSON_GetObjectItem(params, "drive");
    if (drive_item != NULL && cJSON_IsNumber(drive_item)) {
        drive = (unsigned int)drive_item->valueint;
        if (drive > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "drive must be 0 or 1");
        }
    } else {
        drive = 0;
    }

    /* Parse path parameter (required) */
    path_item = cJSON_GetObjectItem(params, "path");
    if (path_item == NULL || !cJSON_IsString(path_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'path' parameter");
    }
    path = path_item->valuestring;
    if (path == NULL || path[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "path cannot be empty");
    }

    log_message(mcp_tools_log, "Attaching disk: unit=%u, drive=%u, path=%s", unit, drive, path);

    /* Attach the disk image */
    result = file_system_attach_disk(unit, drive, path);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    if (result == 0) {
        cJSON_AddStringToObject(response, "status", "ok");
        cJSON_AddNumberToObject(response, "unit", unit);
        cJSON_AddNumberToObject(response, "drive", drive);
        cJSON_AddStringToObject(response, "path", path);
    } else {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to attach disk image");
    }

    return response;
}

cJSON* mcp_tool_disk_detach(cJSON *params)
{
    cJSON *response, *unit_item, *drive_item;
    unsigned int unit, drive;

    /* Parse unit parameter (required, 8-11) */
    unit_item = cJSON_GetObjectItem(params, "unit");
    if (unit_item == NULL || !cJSON_IsNumber(unit_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'unit' parameter (8-11)");
    }
    unit = (unsigned int)unit_item->valueint;
    if (unit < 8 || unit > 11) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "unit must be 8-11");
    }

    /* Parse drive parameter (optional, 0-1, default=0) */
    drive_item = cJSON_GetObjectItem(params, "drive");
    if (drive_item != NULL && cJSON_IsNumber(drive_item)) {
        drive = (unsigned int)drive_item->valueint;
        if (drive > 1) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "drive must be 0 or 1");
        }
    } else {
        drive = 0;
    }

    log_message(mcp_tools_log, "Detaching disk: unit=%u, drive=%u", unit, drive);

    /* Detach the disk */
    file_system_detach_disk(unit, drive);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "unit", unit);
    cJSON_AddNumberToObject(response, "drive", drive);

    return response;
}

cJSON* mcp_tool_disk_list(cJSON *params)
{
    cJSON *response, *files_array, *file_obj, *unit_item;
    unsigned int unit;
    image_contents_t *contents;
    image_contents_file_list_t *file;
    char *name_utf8, *type_utf8;
    int file_count;

    /* Parse unit parameter (required, 8-11) */
    unit_item = cJSON_GetObjectItem(params, "unit");
    if (unit_item == NULL || !cJSON_IsNumber(unit_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'unit' parameter (8-11)");
    }
    unit = (unsigned int)unit_item->valueint;
    if (unit < 8 || unit > 11) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "unit must be 8-11");
    }

    log_message(mcp_tools_log, "Listing directory for unit %u", unit);

    /* Get vdrive for this unit */
    vdrive_t *vdrive = file_system_get_vdrive(unit);
    if (vdrive == NULL || vdrive->image == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "No disk attached to unit");
    }

    /* Read directory contents from attached disk */
    contents = diskcontents_block_read(vdrive, 0);
    if (contents == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Cannot read directory");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        image_contents_destroy(contents);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add disk name and ID */
    name_utf8 = image_contents_to_string(contents, IMAGE_CONTENTS_STRING_UTF8);
    if (name_utf8 != NULL) {
        cJSON_AddStringToObject(response, "disk_name", name_utf8);
        lib_free(name_utf8);
    }
    cJSON_AddNumberToObject(response, "blocks_free", contents->blocks_free);

    /* Build files array */
    files_array = cJSON_CreateArray();
    if (files_array == NULL) {
        cJSON_Delete(response);
        image_contents_destroy(contents);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    file_count = 0;
    for (file = contents->file_list; file != NULL; file = file->next) {
        file_obj = cJSON_CreateObject();
        if (file_obj == NULL) {
            cJSON_Delete(files_array);
            cJSON_Delete(response);
            image_contents_destroy(contents);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Convert PETSCII to UTF-8 */
        name_utf8 = image_contents_filename_to_string(file, IMAGE_CONTENTS_STRING_UTF8);
        type_utf8 = image_contents_filetype_to_string(file, IMAGE_CONTENTS_STRING_UTF8);

        if (name_utf8 != NULL) {
            cJSON_AddStringToObject(file_obj, "name", name_utf8);
            lib_free(name_utf8);
        }
        if (type_utf8 != NULL) {
            cJSON_AddStringToObject(file_obj, "type", type_utf8);
            lib_free(type_utf8);
        }
        cJSON_AddNumberToObject(file_obj, "blocks", file->size);

        cJSON_AddItemToArray(files_array, file_obj);
        file_count++;
    }

    cJSON_AddItemToObject(response, "files", files_array);
    cJSON_AddNumberToObject(response, "file_count", file_count);

    image_contents_destroy(contents);
    return response;
}

cJSON* mcp_tool_disk_read_sector(cJSON *params)
{
    cJSON *response, *unit_item, *track_item, *sector_item;
    unsigned int unit, track, sector;
    vdrive_t *vdrive;
    uint8_t sector_buf[256];
    char hex_buf[768];  /* 256 bytes * 3 chars (XX ) = 768 */
    char *p;
    int result, i;

    /* Parse unit parameter (required, 8-11) */
    unit_item = cJSON_GetObjectItem(params, "unit");
    if (unit_item == NULL || !cJSON_IsNumber(unit_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'unit' parameter (8-11)");
    }
    unit = (unsigned int)unit_item->valueint;
    if (unit < 8 || unit > 11) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "unit must be 8-11");
    }

    /* Parse track parameter (required, 1-42 for most formats) */
    track_item = cJSON_GetObjectItem(params, "track");
    if (track_item == NULL || !cJSON_IsNumber(track_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'track' parameter");
    }
    track = (unsigned int)track_item->valueint;
    if (track < 1 || track > 255) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "track must be 1-255");
    }

    /* Parse sector parameter (required, 0-20 for most tracks) */
    sector_item = cJSON_GetObjectItem(params, "sector");
    if (sector_item == NULL || !cJSON_IsNumber(sector_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'sector' parameter");
    }
    sector = (unsigned int)sector_item->valueint;
    if (sector > 255) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sector must be 0-255");
    }

    log_message(mcp_tools_log, "Reading sector: unit=%u, track=%u, sector=%u", unit, track, sector);

    /* Get vdrive for this unit */
    vdrive = file_system_get_vdrive(unit);
    if (vdrive == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "No disk attached to unit");
    }

    /* Read the sector */
    result = vdrive_read_sector(vdrive, sector_buf, track, sector);
    if (result != 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to read sector (invalid track/sector?)");
    }

    /* Convert sector data to hex string */
    p = hex_buf;
    for (i = 0; i < 256; i++) {
        sprintf(p, "%02X ", sector_buf[i]);
        p += 3;
    }
    *(p - 1) = '\0';  /* Remove trailing space */

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "unit", unit);
    cJSON_AddNumberToObject(response, "track", track);
    cJSON_AddNumberToObject(response, "sector", sector);
    cJSON_AddStringToObject(response, "data", hex_buf);

    return response;
}

/* ========================================================================= */
/* Autostart Tools                                                           */
/* ========================================================================= */

/* Autostart a PRG file or disk image
 *
 * Parameters:
 *   path (required): Path to PRG file or disk image (.d64, .g64, etc.)
 *   program (optional): Program name to load from disk (if path is disk image)
 *   run (optional): Whether to run after loading (default: true)
 *   index (optional): Program index on disk (0-based, default: 0)
 *
 * The function auto-detects file type and uses the appropriate method:
 *   - .prg files: Direct inject into memory
 *   - .d64/.g64/etc: Attach and load from disk
 */
cJSON* mcp_tool_autostart(cJSON *params)
{
    cJSON *response, *path_item, *program_item, *run_item, *index_item;
    const char *path = NULL;
    const char *program = NULL;
    int run = 1;  /* Default: run after loading */
    unsigned int program_index = 0;
    int result;

    log_message(mcp_tools_log, "Handling vice.autostart");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get required path parameter */
    path_item = cJSON_GetObjectItem(params, "path");
    if (path_item == NULL || !cJSON_IsString(path_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'path' parameter");
    }
    path = path_item->valuestring;
    if (path == NULL || path[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Path cannot be empty");
    }

    /* Get optional program name (for disk images) */
    program_item = cJSON_GetObjectItem(params, "program");
    if (program_item != NULL && cJSON_IsString(program_item)) {
        program = program_item->valuestring;
    }

    /* Get optional run flag */
    run_item = cJSON_GetObjectItem(params, "run");
    if (run_item != NULL && cJSON_IsBool(run_item)) {
        run = cJSON_IsTrue(run_item) ? 1 : 0;
    }

    /* Get optional program index */
    index_item = cJSON_GetObjectItem(params, "index");
    if (index_item != NULL && cJSON_IsNumber(index_item)) {
        program_index = (unsigned int)index_item->valueint;
    }

    log_message(mcp_tools_log, "Autostart: path=%s, program=%s, run=%d, index=%u",
                path, program ? program : "(default)", run, program_index);

    /* Use VICE's autostart_autodetect which handles all file types */
    result = autostart_autodetect(path, program, program_index, run ? AUTOSTART_MODE_RUN : AUTOSTART_MODE_LOAD);

    if (result != 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Autostart failed - check file path and format");
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "path", path);
    if (program != NULL) {
        cJSON_AddStringToObject(response, "program", program);
    }
    cJSON_AddBoolToObject(response, "run", run);
    cJSON_AddNumberToObject(response, "index", program_index);
    cJSON_AddStringToObject(response, "message", "Autostart initiated - program will load and run");

    return response;
}

/* ========================================================================= */
/* Machine Control Tools                                                     */
/* ========================================================================= */

/* Reset the machine (soft or hard reset)
 *
 * Parameters:
 *   mode (optional): "soft" (default) or "hard"
 *     - soft: CPU reset only (like pressing reset button)
 *     - hard: Full power cycle (resets all chips and memory)
 */
static cJSON* mcp_tool_machine_reset(cJSON *params)
{
    cJSON *response, *mode_item, *run_after_item;
    const char *mode = "soft";
    unsigned int reset_mode = MACHINE_RESET_MODE_RESET_CPU;
    int run_after = 1;  /* Default: resume execution after reset */

    log_message(mcp_tools_log, "Handling vice.machine.reset");

    /* Get optional parameters */
    if (params != NULL) {
        mode_item = cJSON_GetObjectItem(params, "mode");
        if (mode_item != NULL && cJSON_IsString(mode_item)) {
            mode = mode_item->valuestring;
            if (strcmp(mode, "hard") == 0 || strcmp(mode, "power") == 0) {
                reset_mode = MACHINE_RESET_MODE_POWER_CYCLE;
            } else if (strcmp(mode, "soft") != 0 && strcmp(mode, "cpu") != 0) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Invalid mode - use 'soft' or 'hard'");
            }
        }

        run_after_item = cJSON_GetObjectItem(params, "run_after");
        if (run_after_item != NULL && cJSON_IsBool(run_after_item)) {
            run_after = cJSON_IsTrue(run_after_item);
        }
    }

    log_message(mcp_tools_log, "Resetting machine: mode=%s (%s), run_after=%d", mode,
                reset_mode == MACHINE_RESET_MODE_POWER_CYCLE ? "power cycle" : "CPU reset", run_after);

    /* Trigger the reset - this schedules reset for next CPU cycle */
    machine_trigger_reset(reset_mode);

    /* Resume execution so the reset actually happens and machine boots
     * Without this, the reset is scheduled but execution stays paused */
    if (run_after) {
        exit_mon = exit_mon_continue;
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "mode", reset_mode == MACHINE_RESET_MODE_POWER_CYCLE ? "hard" : "soft");
    cJSON_AddBoolToObject(response, "run_after", run_after);
    cJSON_AddStringToObject(response, "message", reset_mode == MACHINE_RESET_MODE_POWER_CYCLE ?
                            "Machine power cycled" : "Machine reset (CPU)");

    return response;
}

/* ========================================================================= */
/* Phase 2.5: Display Capture Tools                                         */
/* ========================================================================= */

/* Base64 encoding table */
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode binary data to base64 - caller must free returned string */
static char* base64_encode(const uint8_t *data, size_t input_length, size_t *output_length)
{
    size_t i, j;
    size_t out_len = 4 * ((input_length + 2) / 3);
    char *encoded = malloc(out_len + 1);

    if (encoded == NULL) {
        return NULL;
    }

    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        encoded[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 6) & 0x3F];
        encoded[j++] = base64_chars[triple & 0x3F];
    }

    /* Add padding */
    size_t mod = input_length % 3;
    if (mod == 1) {
        encoded[out_len - 1] = '=';
        encoded[out_len - 2] = '=';
    } else if (mod == 2) {
        encoded[out_len - 1] = '=';
    }

    encoded[out_len] = '\0';
    if (output_length) {
        *output_length = out_len;
    }

    return encoded;
}

cJSON* mcp_tool_display_screenshot(cJSON *params)
{
    cJSON *response, *format_item, *path_item, *base64_item;
    const char *format, *path = NULL;
    struct video_canvas_s *canvas;
    int result;
    int return_base64 = 0;
    char temp_path[256];
    int use_temp_file = 0;

    /* Parse return_base64 parameter (optional, default=false) */
    base64_item = cJSON_GetObjectItem(params, "return_base64");
    if (base64_item != NULL && cJSON_IsBool(base64_item)) {
        return_base64 = cJSON_IsTrue(base64_item);
    }

    /* Parse format parameter (optional, default="PNG") */
    format_item = cJSON_GetObjectItem(params, "format");
    if (format_item != NULL && cJSON_IsString(format_item)) {
        format = format_item->valuestring;
        /* Validate format */
        if (strcmp(format, "PNG") != 0 && strcmp(format, "BMP") != 0 &&
            strcmp(format, "png") != 0 && strcmp(format, "bmp") != 0) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "format must be PNG or BMP");
        }
        /* Normalize to uppercase */
        if (strcmp(format, "png") == 0) {
            format = "PNG";
        } else if (strcmp(format, "bmp") == 0) {
            format = "BMP";
        }
    } else {
#ifdef HAVE_PNG
        format = "PNG";
#else
        format = "BMP";
#endif
    }

    /* Parse path parameter (optional if return_base64 is true) */
    path_item = cJSON_GetObjectItem(params, "path");
    if (path_item != NULL && cJSON_IsString(path_item)) {
        path = path_item->valuestring;
        if (path != NULL && path[0] == '\0') {
            path = NULL;  /* Treat empty string as not provided */
        }
    }

    /* If return_base64 and no path, use temp file */
    if (return_base64 && path == NULL) {
        snprintf(temp_path, sizeof(temp_path), "/tmp/vice_mcp_screenshot_%d.%s",
                 (int)getpid(), strcmp(format, "PNG") == 0 ? "png" : "bmp");
        path = temp_path;
        use_temp_file = 1;
    } else if (path == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "path required (or set return_base64=true)");
    }

    log_message(mcp_tools_log, "Taking screenshot: format=%s, path=%s, base64=%d",
                format, path, return_base64);

    /* Get primary video canvas */
    canvas = machine_video_canvas_get(0);
    if (canvas == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Cannot get video canvas");
    }

    /* Save screenshot */
    result = screenshot_save(format, path, canvas);
    if (result != 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to save screenshot");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        if (use_temp_file) {
            remove(path);
        }
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "format", format);

    if (!use_temp_file) {
        cJSON_AddStringToObject(response, "path", path);
    }

    /* Read file and encode as base64 if requested */
    if (return_base64) {
        FILE *f = fopen(path, "rb");
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            long file_size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (file_size > 0 && file_size < 50 * 1024 * 1024) {  /* Max 50MB */
                uint8_t *file_data = malloc(file_size);
                if (file_data != NULL) {
                    size_t bytes_read = fread(file_data, 1, file_size, f);
                    if (bytes_read == (size_t)file_size) {
                        size_t b64_len;
                        char *b64_data = base64_encode(file_data, file_size, &b64_len);
                        if (b64_data != NULL) {
                            /* Add data URI prefix for easy use in HTML/web contexts */
                            char *mime_type = strcmp(format, "PNG") == 0 ? "image/png" : "image/bmp";
                            char *data_uri = malloc(strlen("data:") + strlen(mime_type) +
                                                    strlen(";base64,") + b64_len + 1);
                            if (data_uri != NULL) {
                                sprintf(data_uri, "data:%s;base64,%s", mime_type, b64_data);
                                cJSON_AddStringToObject(response, "data_uri", data_uri);
                                free(data_uri);
                            }
                            cJSON_AddStringToObject(response, "base64", b64_data);
                            cJSON_AddNumberToObject(response, "size", file_size);
                            free(b64_data);
                        }
                    }
                    free(file_data);
                }
            }
            fclose(f);
        }

        /* Clean up temp file */
        if (use_temp_file) {
            remove(path);
        }
    }

    return response;
}

cJSON* mcp_tool_display_get_dimensions(cJSON *params)
{
    cJSON *response;
    struct video_canvas_s *canvas;
    unsigned int width, height;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Getting display dimensions");

    /* Get primary video canvas */
    canvas = machine_video_canvas_get(0);
    if (canvas == NULL || canvas->draw_buffer == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Cannot get video canvas");
    }

    /* Get canvas dimensions from draw buffer */
    width = canvas->draw_buffer->canvas_physical_width;
    height = canvas->draw_buffer->canvas_physical_height;

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddNumberToObject(response, "width", width);
    cJSON_AddNumberToObject(response, "height", height);
    cJSON_AddNumberToObject(response, "visible_width", canvas->draw_buffer->visible_width);
    cJSON_AddNumberToObject(response, "visible_height", canvas->draw_buffer->visible_height);

    return response;
}

/* =============================================================================
 * Phase 3.1: Input Control
 * =============================================================================
 */

cJSON* mcp_tool_keyboard_type(cJSON *params)
{
    cJSON *response;
    cJSON *text_item, *petscii_upper_item;
    const char *text;
    char *converted_text = NULL;
    int result;
    int petscii_upper = 1;  /* Default: convert for uppercase PETSCII mode */

    log_message(mcp_tools_log, "Keyboard type request");

    /* Get required text parameter */
    text_item = cJSON_GetObjectItem(params, "text");
    if (text_item == NULL || !cJSON_IsString(text_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing or invalid 'text' parameter");
    }

    text = text_item->valuestring;
    if (text == NULL || text[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Text parameter cannot be empty");
    }

    /* Check optional petscii_upper parameter (default true)
     * When true (default): Convert uppercase ASCII to unshifted PETSCII (0x41-0x5A)
     *   so they display as uppercase A-Z in the C64's default uppercase mode.
     *   This is the intuitive behavior: "LOAD" displays as "LOAD".
     * When false: Use raw PETSCII conversion where uppercase ASCII maps to
     *   shifted PETSCII (0xC1-0xDA) which displays as graphics in uppercase mode.
     */
    petscii_upper_item = cJSON_GetObjectItem(params, "petscii_upper");
    if (petscii_upper_item != NULL) {
        if (cJSON_IsBool(petscii_upper_item)) {
            petscii_upper = cJSON_IsTrue(petscii_upper_item);
        } else if (cJSON_IsNumber(petscii_upper_item)) {
            petscii_upper = (petscii_upper_item->valueint != 0);
        }
    }

    log_message(mcp_tools_log, "Typing text: %s (petscii_upper=%d)", text, petscii_upper);

    /* If petscii_upper is enabled, convert uppercase ASCII to lowercase
     * so that VICE's PETSCII conversion produces unshifted codes (0x41-0x5A)
     * which display as uppercase in the C64's default character set mode */
    if (petscii_upper) {
        size_t len = strlen(text);
        size_t i;
        converted_text = lib_malloc(len + 1);
        if (converted_text == NULL) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        for (i = 0; i < len; i++) {
            char c = text[i];
            /* Convert uppercase ASCII to lowercase for correct PETSCII display */
            if (c >= 'A' && c <= 'Z') {
                converted_text[i] = c + ('a' - 'A');  /* Convert to lowercase */
            } else {
                converted_text[i] = c;
            }
        }
        converted_text[len] = '\0';
        text = converted_text;
    }

    /* Feed text to keyboard buffer
     * Note: kbdbuf_feed_string returns 0 on success, -1 on failure
     * (queue full or keyboard buffer disabled) */
    result = kbdbuf_feed_string(text);

    if (converted_text != NULL) {
        lib_free(converted_text);
    }

    if (result < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to queue keyboard input (buffer full or disabled)");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "characters_queued", (int)strlen(text_item->valuestring));

    return response;
}

/**
 * Parse a key name or code from JSON to a VHK key code.
 *
 * @param key_item  JSON item containing key name (string) or code (number)
 * @param key_code  Output: the parsed key code
 * @return 0 on success, -1 if key_item is NULL, -2 if key name is unknown,
 *         -3 if key_item is neither string nor number
 */
static int parse_key_code(cJSON *key_item, signed long *key_code)
{
    const char *key_name;

    if (key_item == NULL) {
        return -1;
    }

    if (cJSON_IsString(key_item)) {
        key_name = key_item->valuestring;

        /* Map common key names to VHK codes */
        if (strcmp(key_name, "Return") == 0 || strcmp(key_name, "Enter") == 0) {
            *key_code = VHK_KEY_Return;
        } else if (strcmp(key_name, "Space") == 0) {
            *key_code = ' ';
        } else if (strcmp(key_name, "BackSpace") == 0) {
            *key_code = VHK_KEY_BackSpace;
        } else if (strcmp(key_name, "Delete") == 0) {
            *key_code = VHK_KEY_Delete;
        } else if (strcmp(key_name, "Escape") == 0) {
            *key_code = VHK_KEY_Escape;
        } else if (strcmp(key_name, "Tab") == 0) {
            *key_code = VHK_KEY_Tab;
        } else if (strcmp(key_name, "Up") == 0) {
            *key_code = VHK_KEY_Up;
        } else if (strcmp(key_name, "Down") == 0) {
            *key_code = VHK_KEY_Down;
        } else if (strcmp(key_name, "Left") == 0) {
            *key_code = VHK_KEY_Left;
        } else if (strcmp(key_name, "Right") == 0) {
            *key_code = VHK_KEY_Right;
        } else if (strcmp(key_name, "Home") == 0) {
            *key_code = VHK_KEY_Home;
        } else if (strcmp(key_name, "End") == 0) {
            *key_code = VHK_KEY_End;
        } else if (strcmp(key_name, "F1") == 0) {
            *key_code = VHK_KEY_F1;
        } else if (strcmp(key_name, "F2") == 0) {
            *key_code = VHK_KEY_F2;
        } else if (strcmp(key_name, "F3") == 0) {
            *key_code = VHK_KEY_F3;
        } else if (strcmp(key_name, "F4") == 0) {
            *key_code = VHK_KEY_F4;
        } else if (strcmp(key_name, "F5") == 0) {
            *key_code = VHK_KEY_F5;
        } else if (strcmp(key_name, "F6") == 0) {
            *key_code = VHK_KEY_F6;
        } else if (strcmp(key_name, "F7") == 0) {
            *key_code = VHK_KEY_F7;
        } else if (strcmp(key_name, "F8") == 0) {
            *key_code = VHK_KEY_F8;
        } else if (strlen(key_name) == 1) {
            /* Single character - use ASCII value */
            *key_code = (signed long)key_name[0];
        } else {
            return -2;  /* Unknown key name */
        }
    } else if (cJSON_IsNumber(key_item)) {
        *key_code = (signed long)key_item->valueint;
    } else {
        return -3;  /* Invalid type */
    }

    return 0;
}

/**
 * Parse keyboard modifiers from a JSON array.
 *
 * @param mod_item  JSON array of modifier strings (may be NULL)
 * @return Combined modifier bitmask (VHK_MOD_*)
 */
static int parse_key_modifiers(cJSON *mod_item)
{
    int modifiers = 0;
    int i;

    if (mod_item == NULL || !cJSON_IsArray(mod_item)) {
        return 0;
    }

    for (i = 0; i < cJSON_GetArraySize(mod_item); i++) {
        cJSON *mod = cJSON_GetArrayItem(mod_item, i);
        if (cJSON_IsString(mod)) {
            const char *mod_name = mod->valuestring;
            if (strcmp(mod_name, "shift") == 0) {
                modifiers |= VHK_MOD_SHIFT;
            } else if (strcmp(mod_name, "control") == 0 || strcmp(mod_name, "ctrl") == 0) {
                modifiers |= VHK_MOD_CONTROL;
            } else if (strcmp(mod_name, "alt") == 0) {
                modifiers |= VHK_MOD_ALT;
            } else if (strcmp(mod_name, "meta") == 0) {
                modifiers |= VHK_MOD_META;
            } else if (strcmp(mod_name, "command") == 0 || strcmp(mod_name, "cmd") == 0) {
                modifiers |= VHK_MOD_COMMAND;
            }
        }
    }

    return modifiers;
}

cJSON* mcp_tool_keyboard_key_press(cJSON *params)
{
    cJSON *response;
    cJSON *key_item;
    signed long key_code = 0;
    int modifiers = 0;
    int result;

    log_message(mcp_tools_log, "Keyboard key press request");

    /* Get required key parameter */
    key_item = cJSON_GetObjectItem(params, "key");
    result = parse_key_code(key_item, &key_code);
    if (result == -1) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing 'key' parameter");
    } else if (result == -2) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown key name");
    } else if (result == -3) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "'key' must be string or number");
    }

    /* Get optional modifiers */
    modifiers = parse_key_modifiers(cJSON_GetObjectItem(params, "modifiers"));

    log_message(mcp_tools_log, "Pressing key: code=%ld, modifiers=0x%04x", key_code, (unsigned int)modifiers);

    /* Press the key */
    keyboard_key_pressed(key_code, modifiers);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "key_code", key_code);
    cJSON_AddNumberToObject(response, "modifiers", modifiers);

    return response;
}

cJSON* mcp_tool_keyboard_key_release(cJSON *params)
{
    cJSON *response;
    cJSON *key_item;
    signed long key_code = 0;
    int modifiers = 0;
    int result;

    log_message(mcp_tools_log, "Keyboard key release request");

    /* Get required key parameter */
    key_item = cJSON_GetObjectItem(params, "key");
    result = parse_key_code(key_item, &key_code);
    if (result == -1) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing 'key' parameter");
    } else if (result == -2) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown key name");
    } else if (result == -3) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "'key' must be string or number");
    }

    /* Get optional modifiers */
    modifiers = parse_key_modifiers(cJSON_GetObjectItem(params, "modifiers"));

    log_message(mcp_tools_log, "Releasing key: code=%ld, modifiers=0x%04x", key_code, (unsigned int)modifiers);

    /* Release the key */
    keyboard_key_released(key_code, modifiers);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "key_code", key_code);
    cJSON_AddNumberToObject(response, "modifiers", modifiers);

    return response;
}

cJSON* mcp_tool_joystick_set(cJSON *params)
{
    cJSON *response;
    cJSON *port_item, *dir_item, *fire_item;
    unsigned int port = 1;  /* Default to port 1 */
    uint16_t value = 0;

    log_message(mcp_tools_log, "Joystick set request");

    /* Get optional port parameter (1 or 2) */
    port_item = cJSON_GetObjectItem(params, "port");
    if (port_item != NULL && cJSON_IsNumber(port_item)) {
        port = (unsigned int)port_item->valueint;
        if (port < 1 || port > 2) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Port must be 1 or 2");
        }
    }

    /* Get optional direction parameter (string or array) */
    dir_item = cJSON_GetObjectItem(params, "direction");
    if (dir_item != NULL) {
        if (cJSON_IsString(dir_item)) {
            const char *dir = dir_item->valuestring;
            if (strcmp(dir, "up") == 0) {
                value |= JOYSTICK_DIRECTION_UP;
            } else if (strcmp(dir, "down") == 0) {
                value |= JOYSTICK_DIRECTION_DOWN;
            } else if (strcmp(dir, "left") == 0) {
                value |= JOYSTICK_DIRECTION_LEFT;
            } else if (strcmp(dir, "right") == 0) {
                value |= JOYSTICK_DIRECTION_RIGHT;
            } else if (strcmp(dir, "none") != 0 && strcmp(dir, "center") != 0) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS, "Invalid direction");
            }
        } else if (cJSON_IsArray(dir_item)) {
            int i;
            for (i = 0; i < cJSON_GetArraySize(dir_item); i++) {
                cJSON *d = cJSON_GetArrayItem(dir_item, i);
                if (cJSON_IsString(d)) {
                    const char *dir = d->valuestring;
                    if (strcmp(dir, "up") == 0) {
                        value |= JOYSTICK_DIRECTION_UP;
                    } else if (strcmp(dir, "down") == 0) {
                        value |= JOYSTICK_DIRECTION_DOWN;
                    } else if (strcmp(dir, "left") == 0) {
                        value |= JOYSTICK_DIRECTION_LEFT;
                    } else if (strcmp(dir, "right") == 0) {
                        value |= JOYSTICK_DIRECTION_RIGHT;
                    }
                }
            }
        }
    }

    /* Get optional fire button parameter */
    fire_item = cJSON_GetObjectItem(params, "fire");
    if (fire_item != NULL && cJSON_IsBool(fire_item)) {
        if (cJSON_IsTrue(fire_item)) {
            value |= 16;  /* Fire button bit */
        }
    }

    log_message(mcp_tools_log, "Setting joystick port %u to value 0x%04x", port, value);

    /* Set joystick state */
    joystick_set_value_absolute(port, value);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "port", port);
    cJSON_AddNumberToObject(response, "value", value);

    return response;
}

/* =============================================================================
 * Phase 4: Advanced Debugging
 * =============================================================================
 */

/* Disassemble memory to 6502 instructions
 * Accepts address as number, hex string ("$1000"), or symbol name ("FindBestMove")
 * Shows symbol names in output where available */
static cJSON* mcp_tool_disassemble(cJSON *params)
{
    cJSON *response, *lines_array, *addr_item, *count_item, *symbols_item;
    uint16_t address;
    int count = 10;  /* Default to 10 instructions */
    int show_symbols = 1;  /* Default: show symbol names in output */
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
    int resolved = mcp_resolve_address(addr_item, &error_msg);
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
    for (i = 0; i < count && address < 0xFFFF; i++) {
        const char *disasm;
        unsigned int opc_size = 0;
        cJSON *line_obj;
        uint8_t opc, p1, p2, p3;
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
        address += opc_size;
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
static cJSON* mcp_tool_symbols_load(cJSON *params)
{
    cJSON *response, *path_item, *format_item;
    const char *path;
    const char *format = NULL;
    FILE *fp;
    char line[512];
    int count = 0;
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
                        char *addr_start = eq + 1;
                        while (*addr_start == ' ' || *addr_start == '\t') addr_start++;
                        if (*addr_start == '$') addr_start++;

                        if (sscanf(addr_start, "%x", &addr) == 1) {
                            /* Build full label with namespace prefix */
                            full_label[0] = '\0';
                            for (int i = 0; i < namespace_depth; i++) {
                                if (strlen(full_label) + strlen(namespace_stack[i]) + 2 < sizeof(full_label)) {
                                    strcat(full_label, namespace_stack[i]);
                                    strcat(full_label, ".");
                                }
                            }
                            if (strlen(full_label) + strlen(label) < sizeof(full_label)) {
                                strcat(full_label, label);
                            }

                            MON_ADDR mon_addr = new_addr(e_comp_space, (uint16_t)addr);
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

                        char *addr_start = eq + 1;
                        while (*addr_start == ' ' || *addr_start == '\t') addr_start++;
                        if (*addr_start == '$') addr_start++;

                        if (sscanf(addr_start, "%x", &addr) == 1) {
                            full_label[0] = '\0';
                            for (int i = 0; i < namespace_depth; i++) {
                                if (strlen(full_label) + strlen(namespace_stack[i]) + 2 < sizeof(full_label)) {
                                    strcat(full_label, namespace_stack[i]);
                                    strcat(full_label, ".");
                                }
                            }
                            if (strlen(full_label) + strlen(label) < sizeof(full_label)) {
                                strcat(full_label, label);
                            }

                            MON_ADDR mon_addr = new_addr(e_comp_space, (uint16_t)addr);
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
                    MON_ADDR mon_addr = new_addr(e_comp_space, (uint16_t)addr);
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
                MON_ADDR mon_addr = new_addr(e_comp_space, (uint16_t)addr);
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
static cJSON* mcp_tool_symbols_lookup(cJSON *params)
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
static cJSON* mcp_tool_watch_add(cJSON *params)
{
    cJSON *response, *addr_item, *type_item, *size_item, *condition_item;
    uint16_t address, end_address;
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
static cJSON* mcp_tool_backtrace(cJSON *params)
{
    cJSON *response, *frames_array, *depth_item;
    int sp, max_depth;
    int frame_count = 0;
    uint8_t lo, hi;
    uint16_t ret_addr;
    char *symbol;

    (void)params;

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
    for (int i = sp + 1; i < 0xFF && frame_count < max_depth; i += 2) {
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
            uint8_t jsr_opcode = mem_bank_peek(0, (uint16_t)(ret_addr - 3), NULL);
            if (jsr_opcode == 0x20) {  /* JSR opcode */
                uint16_t jsr_target = mem_bank_peek(0, (uint16_t)(ret_addr - 2), NULL) |
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
static cJSON* mcp_tool_run_until(cJSON *params)
{
    cJSON *response, *addr_item, *cycles_item;
    int resolved;
    const char *error_msg;
    uint16_t target_addr = 0;
    int cycle_limit = 0;
    int has_addr = 0;

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

    /* If we have an address target, set a temporary breakpoint */
    if (has_addr) {
        /* Create temporary breakpoint that will be auto-deleted when hit */
        int bp_num = mon_breakpoint_add_checkpoint(
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
        char *symbol = mon_symbol_table_lookup_name(e_comp_space, target_addr);
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

/* Direct keyboard matrix control for games that scan the keyboard directly
 * Instead of going through KERNAL's keyboard buffer, this sets the CIA
 * keyboard matrix state directly.
 *
 * The C64 keyboard matrix:
 * - CIA1 port A ($DC00) selects rows (active low)
 * - CIA1 port B ($DC01) reads columns (active low)
 * - See C64 keyboard matrix diagram for key positions
 */

/* Structure to track pending key releases */
#define MAX_PENDING_KEY_RELEASES 16
typedef struct {
    int row;
    int col;
    int active;
} pending_key_release_t;

static pending_key_release_t pending_key_releases[MAX_PENDING_KEY_RELEASES];
static alarm_t *keyboard_matrix_alarm = NULL;
static int keyboard_matrix_alarm_initialized = 0;

/* Alarm callback to release keys */
static void keyboard_matrix_alarm_callback(CLOCK offset, void *data)
{
    (void)offset;
    (void)data;
    /* DEBUG: Empty callback to test if alarm itself is the issue */
}

/* Initialize the keyboard matrix alarm (lazy init) */
static void keyboard_matrix_init_alarm(void)
{
    if (!keyboard_matrix_alarm_initialized && maincpu_alarm_context != NULL) {
        keyboard_matrix_alarm = alarm_new(maincpu_alarm_context,
                                          "MCP-KeyboardMatrix",
                                          keyboard_matrix_alarm_callback,
                                          NULL);
        memset(pending_key_releases, 0, sizeof(pending_key_releases));
        keyboard_matrix_alarm_initialized = 1;
        log_message(mcp_tools_log, "Keyboard matrix alarm initialized");
    }
}

/* Add a pending key release */
static int add_pending_key_release(int row, int col)
{
    int i;
    for (i = 0; i < MAX_PENDING_KEY_RELEASES; i++) {
        if (!pending_key_releases[i].active) {
            pending_key_releases[i].row = row;
            pending_key_releases[i].col = col;
            pending_key_releases[i].active = 1;
            return 0;
        }
    }
    return -1;  /* No free slots */
}

static cJSON* mcp_tool_keyboard_matrix(cJSON *params)
{
    cJSON *response, *row_item, *col_item, *pressed_item, *key_item;
    cJSON *hold_frames_item, *hold_ms_item;
    int row, col;
    int pressed = 1;  /* Default: press the key */
    int hold_frames = 0;
    int hold_ms = 0;
    CLOCK hold_cycles = 0;

    log_message(mcp_tools_log, "Handling vice.keyboard.matrix");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Option 1: Direct row/col specification */
    row_item = cJSON_GetObjectItem(params, "row");
    col_item = cJSON_GetObjectItem(params, "col");

    /* Option 2: Named key (common keys mapped to row/col) */
    key_item = cJSON_GetObjectItem(params, "key");

    /* Get pressed state (optional, default true) */
    pressed_item = cJSON_GetObjectItem(params, "pressed");
    if (pressed_item != NULL && cJSON_IsBool(pressed_item)) {
        pressed = cJSON_IsTrue(pressed_item);
    }

    if (key_item != NULL && cJSON_IsString(key_item)) {
        const char *key = key_item->valuestring;

        /* Map common key names to matrix positions
         * C64 keyboard matrix (row, col):
         * See https://sta.c64.org/cbm64kbdlay.html */
        if (strcmp(key, "SPACE") == 0)       { row = 7; col = 4; }
        else if (strcmp(key, "RETURN") == 0) { row = 0; col = 1; }
        else if (strcmp(key, "STOP") == 0)   { row = 7; col = 7; }
        else if (strcmp(key, "F1") == 0)     { row = 0; col = 4; }
        else if (strcmp(key, "F3") == 0)     { row = 0; col = 5; }
        else if (strcmp(key, "F5") == 0)     { row = 0; col = 6; }
        else if (strcmp(key, "F7") == 0)     { row = 0; col = 3; }
        else if (strcmp(key, "UP") == 0)     { row = 0; col = 7; }  /* Shifted CRSR DOWN */
        else if (strcmp(key, "DOWN") == 0)   { row = 0; col = 7; }
        else if (strcmp(key, "LEFT") == 0)   { row = 0; col = 2; }  /* Shifted CRSR RIGHT */
        else if (strcmp(key, "RIGHT") == 0)  { row = 0; col = 2; }
        /* Letters (unshifted) */
        else if (strlen(key) == 1 && key[0] >= 'A' && key[0] <= 'Z') {
            /* Simple mapping for letters - this is approximate */
            static const int letter_map[26][2] = {
                {1,2}, /* A */ {3,4}, /* B */ {2,4}, /* C */ {2,2}, /* D */
                {1,6}, /* E */ {2,5}, /* F */ {3,2}, /* G */ {3,5}, /* H */
                {4,1}, /* I */ {4,2}, /* J */ {4,5}, /* K */ {5,2}, /* L */
                {4,4}, /* M */ {4,7}, /* N */ {4,6}, /* O */ {5,1}, /* P */
                {7,6}, /* Q */ {2,1}, /* R */ {1,5}, /* S */ {2,6}, /* T */
                {3,6}, /* U */ {3,7}, /* V */ {1,1}, /* W */ {2,7}, /* X */
                {3,1}, /* Y */ {1,4}, /* Z */
            };
            int idx = key[0] - 'A';
            row = letter_map[idx][0];
            col = letter_map[idx][1];
        }
        /* Numbers */
        else if (strlen(key) == 1 && key[0] >= '0' && key[0] <= '9') {
            static const int num_map[10][2] = {
                {4,3}, /* 0 */ {7,0}, /* 1 */ {7,3}, /* 2 */ {1,0}, /* 3 */
                {1,3}, /* 4 */ {2,0}, /* 5 */ {2,3}, /* 6 */ {3,0}, /* 7 */
                {3,3}, /* 8 */ {4,0}, /* 9 */
            };
            int idx = key[0] - '0';
            row = num_map[idx][0];
            col = num_map[idx][1];
        }
        else {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Unknown key name");
        }
    }
    else if (cJSON_IsNumber(row_item) && cJSON_IsNumber(col_item)) {
        row = row_item->valueint;
        col = col_item->valueint;

        if (row < 0 || row > 7 || col < 0 || col > 7) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "Row and col must be 0-7");
        }
    }
    else {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Either 'key' name or 'row'/'col' required");
    }

    /* Get hold duration parameters */
    hold_frames_item = cJSON_GetObjectItem(params, "hold_frames");
    hold_ms_item = cJSON_GetObjectItem(params, "hold_ms");

    if (hold_frames_item != NULL && cJSON_IsNumber(hold_frames_item)) {
        hold_frames = hold_frames_item->valueint;
        if (hold_frames < 0 || hold_frames > 300) {  /* Max 5 seconds at 60Hz */
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "hold_frames must be 0-300");
        }
        /* Convert frames to cycles: ~19656 cycles per frame (PAL) */
        /* Using ~20000 as approximate for both PAL/NTSC */
        hold_cycles = (CLOCK)hold_frames * 20000;
    }

    if (hold_ms_item != NULL && cJSON_IsNumber(hold_ms_item)) {
        hold_ms = hold_ms_item->valueint;
        if (hold_ms < 0 || hold_ms > 5000) {  /* Max 5 seconds */
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "hold_ms must be 0-5000");
        }
        /* Convert ms to cycles: ~985 cycles per ms (PAL) / ~1023 (NTSC) */
        /* Using ~1000 as approximate */
        hold_cycles = (CLOCK)hold_ms * 1000;
    }

    /* Initialize alarm system if needed */
    keyboard_matrix_init_alarm();

    /* Set or release the key in VICE's keyboard matrix */
    if (pressed) {
        keyboard_set_keyarr(row, col, 1);
        log_message(mcp_tools_log, "Key pressed: row=%d, col=%d", row, col);

        /* If hold duration specified, log a warning - alarm-based release is disabled
         * because it breaks trap-based dispatch. Caller should send explicit release.
         * TODO: Investigate alarm/trap conflict in VICE's scheduling system.
         */
        if (hold_cycles > 0) {
            log_warning(mcp_tools_log, "hold_ms/hold_frames ignored - send explicit release instead");
        }
    } else {
        keyboard_set_keyarr(row, col, 0);
        log_message(mcp_tools_log, "Key released: row=%d, col=%d", row, col);
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "row", row);
    cJSON_AddNumberToObject(response, "col", col);
    cJSON_AddBoolToObject(response, "pressed", pressed);
    if (hold_frames > 0) {
        cJSON_AddNumberToObject(response, "hold_frames", hold_frames);
    }
    if (hold_ms > 0) {
        cJSON_AddNumberToObject(response, "hold_ms", hold_ms);
    }

    return response;
}

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

/* =========================================================================
 * Snapshot Management Tools
 * ========================================================================= */

cJSON* mcp_tool_snapshot_save(cJSON *params)
{
    cJSON *response;
    cJSON *name_item, *desc_item, *roms_item, *disks_item;
    const char *name;
    const char *description = NULL;
    int include_roms = 0;
    int include_disks = 0;
    char *snapshots_dir;
    char *vsf_path;
    int result;

    log_message(mcp_tools_log, "Handling vice.snapshot.save");

    /* Get required name parameter */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: name (string) - a descriptive name for this snapshot");
    }
    name = name_item->valuestring;

    /* Validate name - alphanumeric, underscore, hyphen only */
    {
        const char *p;
        for (p = name; *p; p++) {
            if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
                return mcp_error(MCP_ERROR_INVALID_PARAMS,
                    "Invalid name: use only alphanumeric characters, underscores, and hyphens");
            }
        }
    }

    /* Get optional parameters */
    desc_item = cJSON_GetObjectItem(params, "description");
    if (desc_item != NULL && cJSON_IsString(desc_item)) {
        description = desc_item->valuestring;
    }

    roms_item = cJSON_GetObjectItem(params, "include_roms");
    if (roms_item != NULL && cJSON_IsBool(roms_item)) {
        include_roms = cJSON_IsTrue(roms_item) ? 1 : 0;
    }

    disks_item = cJSON_GetObjectItem(params, "include_disks");
    if (disks_item != NULL && cJSON_IsBool(disks_item)) {
        include_disks = cJSON_IsTrue(disks_item) ? 1 : 0;
    }

    /* Get/create snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR,
            "Failed to access snapshots directory");
    }

    /* Build full path */
    vsf_path = util_join_paths(snapshots_dir, name, NULL);
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
        sprintf(full_path, "%s.vsf", vsf_path);
        lib_free(vsf_path);
        vsf_path = full_path;
    }

    /* Save snapshot */
    result = machine_write_snapshot(vsf_path, include_roms, include_disks, 0);
    if (result != 0) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
            "Failed to save snapshot - check if emulator state is valid");
    }

    /* Write metadata sidecar */
    mcp_write_snapshot_metadata(vsf_path, name, description, include_roms, include_disks);

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(vsf_path);
        return NULL;
    }

    cJSON_AddStringToObject(response, "name", name);
    cJSON_AddStringToObject(response, "path", vsf_path);
    if (description) {
        cJSON_AddStringToObject(response, "description", description);
    }

    lib_free(vsf_path);
    return response;
}

cJSON* mcp_tool_snapshot_load(cJSON *params)
{
    cJSON *response;
    cJSON *name_item;
    const char *name;
    char *snapshots_dir;
    char *vsf_path;
    cJSON *metadata;
    int result;

    log_message(mcp_tools_log, "Handling vice.snapshot.load");

    /* Get required name parameter */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: name (string) - the snapshot name to load");
    }
    name = name_item->valuestring;

    /* Validate name */
    for (const char *p = name; *p; p++) {
        if (!isalnum(*p) && *p != '_' && *p != '-') {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Invalid name: use only alphanumeric characters, underscores, and hyphens");
        }
    }

    /* Get snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR,
            "Failed to access snapshots directory");
    }

    /* Build full path */
    vsf_path = util_join_paths(snapshots_dir, name, NULL);
    lib_free(snapshots_dir);
    if (vsf_path == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to build snapshot path");
    }

    /* Add .vsf extension */
    {
        size_t len = strlen(vsf_path);
        char *full_path = lib_malloc(len + 5);
        if (full_path == NULL) {
            lib_free(vsf_path);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }
        sprintf(full_path, "%s.vsf", vsf_path);
        lib_free(vsf_path);
        vsf_path = full_path;
    }

    /* Load snapshot */
    result = machine_read_snapshot(vsf_path, 0);
    if (result != 0) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_SNAPSHOT_FAILED,
            "Failed to load snapshot - file may not exist or is incompatible");
    }

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(vsf_path);
        return NULL;
    }

    cJSON_AddStringToObject(response, "name", name);
    cJSON_AddStringToObject(response, "path", vsf_path);

    /* Include metadata if available */
    metadata = mcp_read_snapshot_metadata(vsf_path);
    if (metadata != NULL) {
        cJSON *desc = cJSON_GetObjectItem(metadata, "description");
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(response, "description", desc->valuestring);
        }
        cJSON *created = cJSON_GetObjectItem(metadata, "created");
        if (created && cJSON_IsString(created)) {
            cJSON_AddStringToObject(response, "created", created->valuestring);
        }
        cJSON *machine = cJSON_GetObjectItem(metadata, "machine");
        if (machine && cJSON_IsString(machine)) {
            cJSON_AddStringToObject(response, "machine", machine->valuestring);
        }
        cJSON_Delete(metadata);
    }

    lib_free(vsf_path);
    return response;
}

cJSON* mcp_tool_snapshot_list(cJSON *params)
{
    cJSON *response, *snapshots_array, *snapshot_obj;
    char *snapshots_dir;
    DIR *dir;
    struct dirent *entry;

    (void)params;

    log_message(mcp_tools_log, "Handling vice.snapshot.list");

    /* Get snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR,
            "Failed to access snapshots directory");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(snapshots_dir);
        return NULL;
    }

    cJSON_AddStringToObject(response, "snapshots_directory", snapshots_dir);

    snapshots_array = cJSON_AddArrayToObject(response, "snapshots");
    if (snapshots_array == NULL) {
        lib_free(snapshots_dir);
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to create snapshots array");
    }

    /* List .vsf files */
    dir = opendir(snapshots_dir);
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);

            /* Check for .vsf extension */
            if (name_len > 4 && strcmp(entry->d_name + name_len - 4, ".vsf") == 0) {
                char *vsf_path;
                char *name;
                cJSON *metadata;

                /* Extract name (without .vsf) */
                name = lib_strdup(entry->d_name);
                if (name == NULL) continue;
                name[name_len - 4] = '\0';

                /* Build full path */
                vsf_path = util_join_paths(snapshots_dir, entry->d_name, NULL);
                if (vsf_path == NULL) {
                    lib_free(name);
                    continue;
                }

                /* Create snapshot entry */
                snapshot_obj = cJSON_CreateObject();
                if (snapshot_obj == NULL) {
                    lib_free(vsf_path);
                    lib_free(name);
                    continue;
                }

                cJSON_AddStringToObject(snapshot_obj, "name", name);
                cJSON_AddStringToObject(snapshot_obj, "path", vsf_path);

                /* Add metadata if available */
                metadata = mcp_read_snapshot_metadata(vsf_path);
                if (metadata != NULL) {
                    cJSON *desc = cJSON_GetObjectItem(metadata, "description");
                    if (desc && cJSON_IsString(desc)) {
                        cJSON_AddStringToObject(snapshot_obj, "description", desc->valuestring);
                    }
                    cJSON *created = cJSON_GetObjectItem(metadata, "created");
                    if (created && cJSON_IsString(created)) {
                        cJSON_AddStringToObject(snapshot_obj, "created", created->valuestring);
                    }
                    cJSON *machine = cJSON_GetObjectItem(metadata, "machine");
                    if (machine && cJSON_IsString(machine)) {
                        cJSON_AddStringToObject(snapshot_obj, "machine", machine->valuestring);
                    }
                    cJSON_Delete(metadata);
                }

                cJSON_AddItemToArray(snapshots_array, snapshot_obj);

                lib_free(vsf_path);
                lib_free(name);
            }
        }
        closedir(dir);
    }

    lib_free(snapshots_dir);
    return response;
}

/* ========================================================================= */
/* Phase 5.1: Cycles Stopwatch Tool                                          */
/* ========================================================================= */

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

/* ========================================================================= */
/* Phase 5.1: Memory Fill Tool                                               */
/* ========================================================================= */

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
    int start_addr, end_addr;
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

/* -------------------------------------------------------------------------
 * vice.memory.compare - Compare memory ranges or against snapshot
 * -------------------------------------------------------------------------
 *
 * Modes:
 *   "ranges"   - Compare two memory ranges byte-by-byte
 *   "snapshot" - Compare current memory against saved snapshot (Phase 5.2 Task 5)
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
    int range1_start, range1_end, range2_start;
    int max_differences = 100;  /* Default max differences to return */
    int total_differences = 0;
    int differences_returned = 0;
    long range_size;
    long offset;
    const char *error_msg;
    char addr_str[8];
    uint8_t byte1, byte2;

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

        /* Create differences array */
        differences_array = cJSON_CreateArray();
        if (differences_array == NULL) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        /* Compare byte-by-byte */
        for (offset = 0; offset < range_size; offset++) {
            uint16_t addr1 = (uint16_t)(range1_start + offset);
            uint16_t addr2 = (uint16_t)(range2_start + offset);

            byte1 = mem_read(addr1);
            byte2 = mem_read(addr2);

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
        int start_addr, end_addr;
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
            sprintf(full_path, "%s.vsf", vsf_path);
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

            byte1 = mem_read(addr);       /* Current memory */
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
 * Auto-Snapshot on Checkpoint Hit Tools
 *
 * These tools configure automatic snapshot saving when a checkpoint is hit.
 * The actual snapshot-on-hit behavior requires VICE integration via a callback
 * from the checkpoint system. This MCP-side code stores the configuration and
 * provides a helper function that could be called from such a callback.
 *
 * Ring buffer behavior:
 * - Snapshots are named {prefix}_{hit_count:03d}.vsf
 * - When max_snapshots is reached, the oldest snapshot is deleted
 * - hit_count wraps at max_snapshots and overwrites old files
 *
 * Example: prefix="ai_move", max_snapshots=10
 * Creates: ai_move_001.vsf, ai_move_002.vsf, ..., ai_move_010.vsf
 * On 11th hit: deletes ai_move_001.vsf, creates ai_move_001.vsf (wraps)
 * ========================================================================= */

/* vice.checkpoint.set_auto_snapshot
 *
 * Configure automatic snapshot on checkpoint hit.
 *
 * Parameters:
 *   - checkpoint_id (number, required): Checkpoint to configure
 *   - snapshot_prefix (string, required): Filename prefix (alphanumeric, underscore, hyphen)
 *   - max_snapshots (number, optional): Ring buffer size (default: 10, max: 999)
 *   - include_disks (boolean, optional): Include disk state (default: false)
 *
 * Returns:
 *   - enabled (boolean): true if configuration was set
 *   - checkpoint_id (number): The configured checkpoint ID
 *   - snapshot_prefix (string): The configured prefix
 *   - max_snapshots (number): The configured ring buffer size
 *   - include_disks (boolean): Whether disks will be included
 *   - note (string): Message about VICE integration requirement
 */
cJSON* mcp_tool_checkpoint_set_auto_snapshot(cJSON *params)
{
    cJSON *response;
    cJSON *cp_id_item, *prefix_item, *max_item, *disks_item;
    int checkpoint_id;
    const char *prefix;
    int max_snapshots = 10;  /* Default */
    int include_disks = 0;   /* Default */
    int config_idx;
    const char *p;

    log_message(mcp_tools_log, "Handling vice.checkpoint.set_auto_snapshot");

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: checkpoint_id (number) and snapshot_prefix (string) required");
    }

    /* Get required checkpoint_id parameter */
    cp_id_item = cJSON_GetObjectItem(params, "checkpoint_id");
    if (cp_id_item == NULL || !cJSON_IsNumber(cp_id_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: checkpoint_id (number)");
    }
    checkpoint_id = cp_id_item->valueint;

    if (checkpoint_id < 1) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid checkpoint_id: must be a positive integer");
    }

    /* Get required snapshot_prefix parameter */
    prefix_item = cJSON_GetObjectItem(params, "snapshot_prefix");
    if (prefix_item == NULL || !cJSON_IsString(prefix_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: snapshot_prefix (string)");
    }
    prefix = prefix_item->valuestring;

    /* Validate prefix - alphanumeric, underscore, hyphen only */
    if (prefix[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid snapshot_prefix: cannot be empty");
    }
    for (p = prefix; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Invalid snapshot_prefix: use only alphanumeric characters, underscores, and hyphens");
        }
    }
    if (strlen(prefix) >= MCP_MAX_SNAPSHOT_PREFIX_LEN) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid snapshot_prefix: too long (max 63 characters)");
    }

    /* Get optional max_snapshots parameter */
    max_item = cJSON_GetObjectItem(params, "max_snapshots");
    if (max_item != NULL && cJSON_IsNumber(max_item)) {
        max_snapshots = max_item->valueint;
        if (max_snapshots < 1) {
            max_snapshots = 1;
        }
        if (max_snapshots > 999) {
            max_snapshots = 999;  /* Limit for %03d format */
        }
    }

    /* Get optional include_disks parameter */
    disks_item = cJSON_GetObjectItem(params, "include_disks");
    if (disks_item != NULL && cJSON_IsBool(disks_item)) {
        include_disks = cJSON_IsTrue(disks_item) ? 1 : 0;
    }

    /* Check if this checkpoint already has an auto-snapshot config */
    config_idx = mcp_auto_snapshot_find(checkpoint_id);
    if (config_idx < 0) {
        /* Need a new slot */
        config_idx = mcp_auto_snapshot_find_free();
        if (config_idx < 0) {
            return mcp_error(MCP_ERROR_INTERNAL_ERROR,
                "Maximum auto-snapshot configurations reached");
        }
    }

    /* Store the configuration */
    auto_snapshot_configs[config_idx].checkpoint_id = checkpoint_id;
    strncpy(auto_snapshot_configs[config_idx].prefix, prefix, MCP_MAX_SNAPSHOT_PREFIX_LEN - 1);
    auto_snapshot_configs[config_idx].prefix[MCP_MAX_SNAPSHOT_PREFIX_LEN - 1] = '\0';
    auto_snapshot_configs[config_idx].max_snapshots = max_snapshots;
    auto_snapshot_configs[config_idx].include_disks = include_disks;
    auto_snapshot_configs[config_idx].hit_count = 0;  /* Reset counter */
    auto_snapshot_configs[config_idx].active = 1;

    log_message(mcp_tools_log, "Auto-snapshot configured: checkpoint %d -> %s_xxx.vsf (max %d)",
                checkpoint_id, prefix, max_snapshots);

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(response, "enabled", 1);
    cJSON_AddNumberToObject(response, "checkpoint_id", checkpoint_id);
    cJSON_AddStringToObject(response, "snapshot_prefix", prefix);
    cJSON_AddNumberToObject(response, "max_snapshots", max_snapshots);
    cJSON_AddBoolToObject(response, "include_disks", include_disks);
    cJSON_AddStringToObject(response, "note",
        "Config stored. Actual snapshot-on-hit requires VICE checkpoint callback integration.");

    return response;
}

/* vice.checkpoint.clear_auto_snapshot
 *
 * Remove auto-snapshot configuration from a checkpoint.
 *
 * Parameters:
 *   - checkpoint_id (number, required): Checkpoint to clear configuration from
 *
 * Returns:
 *   - cleared (boolean): true if configuration was removed, false if none existed
 *   - checkpoint_id (number): The checkpoint ID that was cleared
 */
cJSON* mcp_tool_checkpoint_clear_auto_snapshot(cJSON *params)
{
    cJSON *response;
    cJSON *cp_id_item;
    int checkpoint_id;
    int config_idx;
    int was_active;

    log_message(mcp_tools_log, "Handling vice.checkpoint.clear_auto_snapshot");

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: checkpoint_id (number) required");
    }

    /* Get required checkpoint_id parameter */
    cp_id_item = cJSON_GetObjectItem(params, "checkpoint_id");
    if (cp_id_item == NULL || !cJSON_IsNumber(cp_id_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: checkpoint_id (number)");
    }
    checkpoint_id = cp_id_item->valueint;

    if (checkpoint_id < 1) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid checkpoint_id: must be a positive integer");
    }

    /* Find and clear the configuration */
    config_idx = mcp_auto_snapshot_find(checkpoint_id);
    was_active = (config_idx >= 0);

    if (was_active) {
        /* Clear the slot */
        auto_snapshot_configs[config_idx].checkpoint_id = -1;
        auto_snapshot_configs[config_idx].prefix[0] = '\0';
        auto_snapshot_configs[config_idx].max_snapshots = 10;
        auto_snapshot_configs[config_idx].include_disks = 0;
        auto_snapshot_configs[config_idx].hit_count = 0;
        auto_snapshot_configs[config_idx].active = 0;

        log_message(mcp_tools_log, "Auto-snapshot cleared for checkpoint %d", checkpoint_id);
    } else {
        log_message(mcp_tools_log, "No auto-snapshot config found for checkpoint %d", checkpoint_id);
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddBoolToObject(response, "cleared", was_active);
    cJSON_AddNumberToObject(response, "checkpoint_id", checkpoint_id);

    return response;
}

/* =========================================================================
 * Phase 5.4: Execution Tracing Tools
 *
 * These tools configure execution trace recording. The actual tracing
 * requires CPU hook integration with VICE - these tools manage the config.
 *
 * Output format (plain text):
 *   $C000: LDA #$00
 *   $C002: STA $D020
 *
 * With registers:
 *   $C000: LDA #$00    [A=00 X=FF Y=00 SP=FF P=32]
 * ========================================================================= */

/* vice.trace.start
 *
 * Start execution trace recording to a file.
 *
 * Parameters:
 *   - output_file (string, required): Path to output file for trace data
 *   - pc_filter_start (number, optional): Start address for PC filter (default 0)
 *   - pc_filter_end (number, optional): End address for PC filter (default 0xFFFF)
 *   - max_instructions (number, optional): Max instructions to record (default 10000)
 *   - include_registers (boolean, optional): Include register state (default false)
 *
 * Returns:
 *   - trace_id (string): Unique identifier for this trace session
 *   - output_file (string): Path to output file
 *   - pc_filter (object): {start, end} for PC filter range
 *   - max_instructions (number): Maximum instructions to record
 *   - include_registers (boolean): Whether register state is included
 *   - note (string): Integration status note
 */
cJSON* mcp_tool_trace_start(cJSON *params)
{
    cJSON *response, *pc_filter;
    cJSON *file_item, *start_item, *end_item, *max_item, *regs_item;
    const char *output_file;
    uint16_t pc_filter_start = 0;
    uint16_t pc_filter_end = 0xFFFF;
    int max_instructions = 10000;
    int include_registers = 0;
    int config_idx;
    char trace_id[32];

    log_message(mcp_tools_log, "Handling vice.trace.start");

    mcp_trace_configs_init();

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: output_file (string) required");
    }

    /* Get required output_file parameter */
    file_item = cJSON_GetObjectItem(params, "output_file");
    if (file_item == NULL || !cJSON_IsString(file_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: output_file (string)");
    }
    output_file = file_item->valuestring;

    /* Validate output_file - not empty */
    if (output_file[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid output_file: cannot be empty");
    }
    if (strlen(output_file) >= MCP_MAX_TRACE_FILE_LEN) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid output_file: path too long (max 255 characters)");
    }

    /* Get optional pc_filter_start parameter */
    start_item = cJSON_GetObjectItem(params, "pc_filter_start");
    if (start_item != NULL && cJSON_IsNumber(start_item)) {
        int val = start_item->valueint;
        if (val < 0 || val > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Invalid pc_filter_start: must be 0-65535");
        }
        pc_filter_start = (uint16_t)val;
    }

    /* Get optional pc_filter_end parameter */
    end_item = cJSON_GetObjectItem(params, "pc_filter_end");
    if (end_item != NULL && cJSON_IsNumber(end_item)) {
        int val = end_item->valueint;
        if (val < 0 || val > 0xFFFF) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Invalid pc_filter_end: must be 0-65535");
        }
        pc_filter_end = (uint16_t)val;
    }

    /* Validate filter range */
    if (pc_filter_start > pc_filter_end) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid PC filter: start must be <= end");
    }

    /* Get optional max_instructions parameter */
    max_item = cJSON_GetObjectItem(params, "max_instructions");
    if (max_item != NULL && cJSON_IsNumber(max_item)) {
        max_instructions = max_item->valueint;
        if (max_instructions < 1) {
            max_instructions = 1;
        }
        if (max_instructions > 1000000) {
            max_instructions = 1000000;  /* Safety limit */
        }
    }

    /* Get optional include_registers parameter */
    regs_item = cJSON_GetObjectItem(params, "include_registers");
    if (regs_item != NULL && cJSON_IsBool(regs_item)) {
        include_registers = cJSON_IsTrue(regs_item) ? 1 : 0;
    }

    /* Find a free slot */
    config_idx = mcp_trace_find_free();
    if (config_idx < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR,
            "Maximum trace configurations reached");
    }

    /* Generate unique trace ID */
    snprintf(trace_id, sizeof(trace_id), "trace_%d", ++trace_id_counter);

    /* Store the configuration */
    strncpy(trace_configs[config_idx].trace_id, trace_id, sizeof(trace_configs[config_idx].trace_id) - 1);
    trace_configs[config_idx].trace_id[sizeof(trace_configs[config_idx].trace_id) - 1] = '\0';
    strncpy(trace_configs[config_idx].output_file, output_file, MCP_MAX_TRACE_FILE_LEN - 1);
    trace_configs[config_idx].output_file[MCP_MAX_TRACE_FILE_LEN - 1] = '\0';
    trace_configs[config_idx].pc_filter_start = pc_filter_start;
    trace_configs[config_idx].pc_filter_end = pc_filter_end;
    trace_configs[config_idx].max_instructions = max_instructions;
    trace_configs[config_idx].include_registers = include_registers;
    trace_configs[config_idx].instructions_recorded = 0;
    trace_configs[config_idx].start_cycles = maincpu_clk;  /* Capture current cycle count */
    trace_configs[config_idx].active = 1;

    log_message(mcp_tools_log, "Trace started: %s -> %s (PC filter $%04X-$%04X, max %d, regs=%d)",
                trace_id, output_file, pc_filter_start, pc_filter_end,
                max_instructions, include_registers);

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "trace_id", trace_id);
    cJSON_AddStringToObject(response, "output_file", output_file);

    pc_filter = cJSON_CreateObject();
    if (pc_filter != NULL) {
        cJSON_AddNumberToObject(pc_filter, "start", pc_filter_start);
        cJSON_AddNumberToObject(pc_filter, "end", pc_filter_end);
        cJSON_AddItemToObject(response, "pc_filter", pc_filter);
    }

    cJSON_AddNumberToObject(response, "max_instructions", max_instructions);
    cJSON_AddBoolToObject(response, "include_registers", include_registers);
    cJSON_AddStringToObject(response, "note",
        "Config stored. Actual tracing requires VICE CPU hook integration.");

    return response;
}

/* vice.trace.stop
 *
 * Stop an active execution trace and get recording statistics.
 *
 * Parameters:
 *   - trace_id (string, required): The trace ID returned from trace.start
 *
 * Returns:
 *   - trace_id (string): The trace that was stopped
 *   - instructions_recorded (number): Number of instructions captured
 *   - output_file (string): Path to the trace output file
 *   - cycles_elapsed (number): CPU cycles elapsed during trace
 *   - stopped (boolean): true if trace was stopped, false if not found
 */
cJSON* mcp_tool_trace_stop(cJSON *params)
{
    cJSON *response;
    cJSON *id_item;
    const char *trace_id;
    int config_idx;
    int instructions_recorded = 0;
    unsigned long cycles_elapsed = 0;
    char output_file[MCP_MAX_TRACE_FILE_LEN] = "";
    int was_active;

    log_message(mcp_tools_log, "Handling vice.trace.stop");

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: trace_id (string) required");
    }

    /* Get required trace_id parameter */
    id_item = cJSON_GetObjectItem(params, "trace_id");
    if (id_item == NULL || !cJSON_IsString(id_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: trace_id (string)");
    }
    trace_id = id_item->valuestring;

    if (trace_id[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid trace_id: cannot be empty");
    }

    /* Find the trace config */
    config_idx = mcp_trace_find(trace_id);
    was_active = (config_idx >= 0);

    if (was_active) {
        /* Capture statistics before clearing */
        instructions_recorded = trace_configs[config_idx].instructions_recorded;
        cycles_elapsed = maincpu_clk - trace_configs[config_idx].start_cycles;
        strncpy(output_file, trace_configs[config_idx].output_file, MCP_MAX_TRACE_FILE_LEN - 1);
        output_file[MCP_MAX_TRACE_FILE_LEN - 1] = '\0';

        /* Clear the slot */
        trace_configs[config_idx].trace_id[0] = '\0';
        trace_configs[config_idx].output_file[0] = '\0';
        trace_configs[config_idx].pc_filter_start = 0;
        trace_configs[config_idx].pc_filter_end = 0xFFFF;
        trace_configs[config_idx].max_instructions = 10000;
        trace_configs[config_idx].include_registers = 0;
        trace_configs[config_idx].instructions_recorded = 0;
        trace_configs[config_idx].start_cycles = 0;
        trace_configs[config_idx].active = 0;

        log_message(mcp_tools_log, "Trace stopped: %s (recorded %d instructions, %lu cycles)",
                    trace_id, instructions_recorded, cycles_elapsed);
    } else {
        log_message(mcp_tools_log, "Trace not found: %s", trace_id);
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "trace_id", trace_id);
    cJSON_AddBoolToObject(response, "stopped", was_active);

    if (was_active) {
        cJSON_AddNumberToObject(response, "instructions_recorded", instructions_recorded);
        cJSON_AddStringToObject(response, "output_file", output_file);
        cJSON_AddNumberToObject(response, "cycles_elapsed", (double)cycles_elapsed);
    }

    return response;
}

/* =========================================================================
 * Phase 5.4: Interrupt Logging Tools
 *
 * These tools configure interrupt event logging. The actual logging
 * requires interrupt dispatch hooks in VICE - these tools manage the config
 * and store logged entries.
 *
 * Entry format:
 *   {type: "irq"|"nmi"|"brk", cycle: <number>, pc: <number>,
 *    vector_address: <number>, handler_address: <number>}
 *
 * Vector addresses:
 *   - IRQ: $FFFE/$FFFF
 *   - NMI: $FFFA/$FFFB
 *   - BRK: $FFFE/$FFFF (same as IRQ but can be distinguished by processor)
 * ========================================================================= */

#define MCP_MAX_INTERRUPT_LOGS 16
#define MCP_MAX_INTERRUPT_ENTRIES 10000
#define MCP_INTERRUPT_TYPE_IRQ 0x01
#define MCP_INTERRUPT_TYPE_NMI 0x02
#define MCP_INTERRUPT_TYPE_BRK 0x04
#define MCP_INTERRUPT_TYPE_ALL (MCP_INTERRUPT_TYPE_IRQ | MCP_INTERRUPT_TYPE_NMI | MCP_INTERRUPT_TYPE_BRK)

/* Single interrupt log entry */
typedef struct {
    uint8_t type;              /* MCP_INTERRUPT_TYPE_* */
    unsigned long cycle;       /* CPU cycle when interrupt occurred */
    uint16_t pc;               /* Program counter when interrupted */
    uint16_t vector_address;   /* Vector address ($FFFE, $FFFA) */
    uint16_t handler_address;  /* What the vector pointed to */
} mcp_interrupt_entry_t;

/* Interrupt log configuration */
typedef struct {
    char log_id[32];                                    /* Unique log identifier */
    uint8_t type_filter;                                /* Bitmask of types to log */
    int max_entries;                                    /* Maximum entries (default 1000) */
    int entry_count;                                    /* Current entry count */
    mcp_interrupt_entry_t *entries;                     /* Dynamic array of entries */
    unsigned long start_cycles;                         /* Cycle count at log start */
    int active;                                         /* 1 if log is active */
} mcp_interrupt_log_config_t;

static mcp_interrupt_log_config_t interrupt_log_configs[MCP_MAX_INTERRUPT_LOGS];
static int interrupt_log_configs_initialized = 0;
static int interrupt_log_id_counter = 0;

/* Initialize interrupt log configs (called on first use) */
static void mcp_interrupt_log_configs_init(void)
{
    int i;
    if (!interrupt_log_configs_initialized) {
        for (i = 0; i < MCP_MAX_INTERRUPT_LOGS; i++) {
            interrupt_log_configs[i].log_id[0] = '\0';
            interrupt_log_configs[i].type_filter = MCP_INTERRUPT_TYPE_ALL;
            interrupt_log_configs[i].max_entries = 1000;
            interrupt_log_configs[i].entry_count = 0;
            interrupt_log_configs[i].entries = NULL;
            interrupt_log_configs[i].start_cycles = 0;
            interrupt_log_configs[i].active = 0;
        }
        interrupt_log_configs_initialized = 1;
    }
}

/* Reset all interrupt log configs (for testing) */
extern void mcp_interrupt_log_configs_reset(void);
void mcp_interrupt_log_configs_reset(void)
{
    int i;
    for (i = 0; i < MCP_MAX_INTERRUPT_LOGS; i++) {
        if (interrupt_log_configs[i].entries != NULL) {
            lib_free(interrupt_log_configs[i].entries);
            interrupt_log_configs[i].entries = NULL;
        }
        interrupt_log_configs[i].log_id[0] = '\0';
        interrupt_log_configs[i].type_filter = MCP_INTERRUPT_TYPE_ALL;
        interrupt_log_configs[i].max_entries = 1000;
        interrupt_log_configs[i].entry_count = 0;
        interrupt_log_configs[i].start_cycles = 0;
        interrupt_log_configs[i].active = 0;
    }
    interrupt_log_configs_initialized = 1;
    interrupt_log_id_counter = 0;
}

/* Find an interrupt log config by log_id. Returns index or -1 if not found */
static int mcp_interrupt_log_find(const char *log_id)
{
    int i;
    mcp_interrupt_log_configs_init();
    for (i = 0; i < MCP_MAX_INTERRUPT_LOGS; i++) {
        if (interrupt_log_configs[i].active &&
            strcmp(interrupt_log_configs[i].log_id, log_id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find a free interrupt log config slot. Returns index or -1 if full */
static int mcp_interrupt_log_find_free(void)
{
    int i;
    mcp_interrupt_log_configs_init();
    for (i = 0; i < MCP_MAX_INTERRUPT_LOGS; i++) {
        if (!interrupt_log_configs[i].active) {
            return i;
        }
    }
    return -1;
}

/* Parse interrupt type string. Returns bitmask or 0 on error */
static uint8_t mcp_interrupt_type_from_string(const char *type_str)
{
    if (strcmp(type_str, "irq") == 0) {
        return MCP_INTERRUPT_TYPE_IRQ;
    } else if (strcmp(type_str, "nmi") == 0) {
        return MCP_INTERRUPT_TYPE_NMI;
    } else if (strcmp(type_str, "brk") == 0) {
        return MCP_INTERRUPT_TYPE_BRK;
    }
    return 0;
}

/* Convert interrupt type bitmask to string */
static const char* mcp_interrupt_type_to_string(uint8_t type)
{
    switch (type) {
        case MCP_INTERRUPT_TYPE_IRQ: return "irq";
        case MCP_INTERRUPT_TYPE_NMI: return "nmi";
        case MCP_INTERRUPT_TYPE_BRK: return "brk";
        default: return "unknown";
    }
}

/* vice.interrupt.log.start
 *
 * Start logging interrupt events.
 *
 * Parameters:
 *   - types (array of strings, optional): Interrupt types to log ("irq", "nmi", "brk")
 *                                          Default: all types
 *   - max_entries (number, optional): Maximum entries to store (default 1000, max 10000)
 *
 * Returns:
 *   - log_id (string): Unique identifier for this log session
 *   - types (array): Interrupt types being logged
 *   - max_entries (number): Maximum entries configured
 *   - note (string): Integration status note
 */
cJSON* mcp_tool_interrupt_log_start(cJSON *params)
{
    cJSON *response, *types_array, *type_item;
    cJSON *types_param, *max_param;
    uint8_t type_filter = MCP_INTERRUPT_TYPE_ALL;
    int max_entries = 1000;
    int config_idx;
    char log_id[32];
    int i, arr_size;

    log_message(mcp_tools_log, "Handling vice.interrupt.log.start");

    mcp_interrupt_log_configs_init();

    /* Get optional types parameter */
    if (params != NULL) {
        types_param = cJSON_GetObjectItem(params, "types");
        if (types_param != NULL && cJSON_IsArray(types_param)) {
            type_filter = 0;  /* Start with no types */
            arr_size = cJSON_GetArraySize(types_param);
            for (i = 0; i < arr_size; i++) {
                type_item = cJSON_GetArrayItem(types_param, i);
                if (cJSON_IsString(type_item)) {
                    uint8_t t = mcp_interrupt_type_from_string(type_item->valuestring);
                    if (t == 0) {
                        return mcp_error(MCP_ERROR_INVALID_PARAMS,
                            "Invalid interrupt type: must be 'irq', 'nmi', or 'brk'");
                    }
                    type_filter |= t;
                }
            }
            if (type_filter == 0) {
                return mcp_error(MCP_ERROR_INVALID_PARAMS,
                    "Invalid types array: must contain at least one valid type");
            }
        }

        /* Get optional max_entries parameter */
        max_param = cJSON_GetObjectItem(params, "max_entries");
        if (max_param != NULL && cJSON_IsNumber(max_param)) {
            max_entries = max_param->valueint;
            if (max_entries < 1) {
                max_entries = 1;
            }
            if (max_entries > MCP_MAX_INTERRUPT_ENTRIES) {
                max_entries = MCP_MAX_INTERRUPT_ENTRIES;
            }
        }
    }

    /* Find a free slot */
    config_idx = mcp_interrupt_log_find_free();
    if (config_idx < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR,
            "Maximum interrupt log configurations reached");
    }

    /* Allocate entries array */
    interrupt_log_configs[config_idx].entries =
        (mcp_interrupt_entry_t *)lib_malloc(max_entries * sizeof(mcp_interrupt_entry_t));
    if (interrupt_log_configs[config_idx].entries == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Generate unique log ID */
    snprintf(log_id, sizeof(log_id), "intlog_%d", ++interrupt_log_id_counter);

    /* Store the configuration */
    strncpy(interrupt_log_configs[config_idx].log_id, log_id,
            sizeof(interrupt_log_configs[config_idx].log_id) - 1);
    interrupt_log_configs[config_idx].log_id[sizeof(interrupt_log_configs[config_idx].log_id) - 1] = '\0';
    interrupt_log_configs[config_idx].type_filter = type_filter;
    interrupt_log_configs[config_idx].max_entries = max_entries;
    interrupt_log_configs[config_idx].entry_count = 0;
    interrupt_log_configs[config_idx].start_cycles = maincpu_clk;
    interrupt_log_configs[config_idx].active = 1;

    log_message(mcp_tools_log, "Interrupt log started: %s (types=%02X, max=%d)",
                log_id, type_filter, max_entries);

    /* Build success response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "log_id", log_id);

    /* Build types array for response */
    types_array = cJSON_CreateArray();
    if (types_array != NULL) {
        if (type_filter & MCP_INTERRUPT_TYPE_IRQ) {
            cJSON_AddItemToArray(types_array, cJSON_CreateString("irq"));
        }
        if (type_filter & MCP_INTERRUPT_TYPE_NMI) {
            cJSON_AddItemToArray(types_array, cJSON_CreateString("nmi"));
        }
        if (type_filter & MCP_INTERRUPT_TYPE_BRK) {
            cJSON_AddItemToArray(types_array, cJSON_CreateString("brk"));
        }
        cJSON_AddItemToObject(response, "types", types_array);
    }

    cJSON_AddNumberToObject(response, "max_entries", max_entries);
    cJSON_AddStringToObject(response, "note",
        "Config stored. Actual logging requires VICE interrupt hook integration.");

    return response;
}

/* vice.interrupt.log.stop
 *
 * Stop an active interrupt log and get all recorded entries.
 *
 * Parameters:
 *   - log_id (string, required): The log ID returned from interrupt.log.start
 *
 * Returns:
 *   - log_id (string): The log that was stopped
 *   - entries (array): Array of interrupt entries
 *   - total_interrupts (number): Total interrupts logged
 *   - stopped (boolean): true if log was stopped
 */
cJSON* mcp_tool_interrupt_log_stop(cJSON *params)
{
    cJSON *response, *entries_array, *entry_obj;
    cJSON *id_item;
    const char *log_id;
    int config_idx;
    int was_active;
    int entry_count = 0;
    int i;

    log_message(mcp_tools_log, "Handling vice.interrupt.log.stop");

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: log_id (string) required");
    }

    /* Get required log_id parameter */
    id_item = cJSON_GetObjectItem(params, "log_id");
    if (id_item == NULL || !cJSON_IsString(id_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: log_id (string)");
    }
    log_id = id_item->valuestring;

    if (log_id[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid log_id: cannot be empty");
    }

    /* Find the log config */
    config_idx = mcp_interrupt_log_find(log_id);
    was_active = (config_idx >= 0);

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "log_id", log_id);

    if (was_active) {
        entry_count = interrupt_log_configs[config_idx].entry_count;

        /* Build entries array */
        entries_array = cJSON_CreateArray();
        if (entries_array != NULL) {
            for (i = 0; i < entry_count; i++) {
                mcp_interrupt_entry_t *e = &interrupt_log_configs[config_idx].entries[i];
                entry_obj = cJSON_CreateObject();
                if (entry_obj != NULL) {
                    cJSON_AddStringToObject(entry_obj, "type",
                        mcp_interrupt_type_to_string(e->type));
                    cJSON_AddNumberToObject(entry_obj, "cycle", (double)e->cycle);
                    cJSON_AddNumberToObject(entry_obj, "pc", e->pc);
                    cJSON_AddNumberToObject(entry_obj, "vector_address", e->vector_address);
                    cJSON_AddNumberToObject(entry_obj, "handler_address", e->handler_address);
                    cJSON_AddItemToArray(entries_array, entry_obj);
                }
            }
            cJSON_AddItemToObject(response, "entries", entries_array);
        }

        cJSON_AddNumberToObject(response, "total_interrupts", entry_count);

        /* Free the entries array */
        if (interrupt_log_configs[config_idx].entries != NULL) {
            lib_free(interrupt_log_configs[config_idx].entries);
            interrupt_log_configs[config_idx].entries = NULL;
        }

        /* Clear the slot */
        interrupt_log_configs[config_idx].log_id[0] = '\0';
        interrupt_log_configs[config_idx].type_filter = MCP_INTERRUPT_TYPE_ALL;
        interrupt_log_configs[config_idx].max_entries = 1000;
        interrupt_log_configs[config_idx].entry_count = 0;
        interrupt_log_configs[config_idx].start_cycles = 0;
        interrupt_log_configs[config_idx].active = 0;

        log_message(mcp_tools_log, "Interrupt log stopped: %s (logged %d entries)",
                    log_id, entry_count);
    } else {
        /* Return empty entries for non-existent log */
        entries_array = cJSON_CreateArray();
        if (entries_array != NULL) {
            cJSON_AddItemToObject(response, "entries", entries_array);
        }
        cJSON_AddNumberToObject(response, "total_interrupts", 0);
        log_message(mcp_tools_log, "Interrupt log not found: %s", log_id);
    }

    cJSON_AddBoolToObject(response, "stopped", was_active);

    return response;
}

/* vice.interrupt.log.read
 *
 * Read entries from an active interrupt log without stopping it.
 *
 * Parameters:
 *   - log_id (string, required): The log ID returned from interrupt.log.start
 *   - since_index (number, optional): Return only entries from this index onwards (for incremental reads)
 *
 * Returns:
 *   - log_id (string): The log being read
 *   - entries (array): Array of interrupt entries
 *   - next_index (number): Index to use for next incremental read
 *   - total_entries (number): Total entries in the log so far
 */
cJSON* mcp_tool_interrupt_log_read(cJSON *params)
{
    cJSON *response, *entries_array, *entry_obj;
    cJSON *id_item, *since_item;
    const char *log_id;
    int config_idx;
    int since_index = 0;
    int entry_count;
    int i;

    log_message(mcp_tools_log, "Handling vice.interrupt.log.read");

    /* Validate params object */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing parameters: log_id (string) required");
    }

    /* Get required log_id parameter */
    id_item = cJSON_GetObjectItem(params, "log_id");
    if (id_item == NULL || !cJSON_IsString(id_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: log_id (string)");
    }
    log_id = id_item->valuestring;

    if (log_id[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid log_id: cannot be empty");
    }

    /* Get optional since_index parameter */
    since_item = cJSON_GetObjectItem(params, "since_index");
    if (since_item != NULL && cJSON_IsNumber(since_item)) {
        since_index = since_item->valueint;
        if (since_index < 0) {
            since_index = 0;
        }
    }

    /* Find the log config */
    config_idx = mcp_interrupt_log_find(log_id);
    if (config_idx < 0) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Invalid log_id: log not found or not active");
    }

    entry_count = interrupt_log_configs[config_idx].entry_count;

    /* Clamp since_index to entry_count */
    if (since_index > entry_count) {
        since_index = entry_count;
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "log_id", log_id);

    /* Build entries array (from since_index to end) */
    entries_array = cJSON_CreateArray();
    if (entries_array != NULL) {
        for (i = since_index; i < entry_count; i++) {
            mcp_interrupt_entry_t *e = &interrupt_log_configs[config_idx].entries[i];
            entry_obj = cJSON_CreateObject();
            if (entry_obj != NULL) {
                cJSON_AddStringToObject(entry_obj, "type",
                    mcp_interrupt_type_to_string(e->type));
                cJSON_AddNumberToObject(entry_obj, "cycle", (double)e->cycle);
                cJSON_AddNumberToObject(entry_obj, "pc", e->pc);
                cJSON_AddNumberToObject(entry_obj, "vector_address", e->vector_address);
                cJSON_AddNumberToObject(entry_obj, "handler_address", e->handler_address);
                cJSON_AddItemToArray(entries_array, entry_obj);
            }
        }
        cJSON_AddItemToObject(response, "entries", entries_array);
    }

    cJSON_AddNumberToObject(response, "next_index", entry_count);
    cJSON_AddNumberToObject(response, "total_entries", entry_count);

    log_message(mcp_tools_log, "Interrupt log read: %s (returned %d entries from index %d)",
                log_id, entry_count - since_index, since_index);

    return response;
}

/* ------------------------------------------------------------------------- */
/* Phase 5.5: Memory Map Tool
 *
 * Display memory region layout with optional symbol-based content hints.
 * Uses a static C64 memory map for now. content_hint is populated when
 * symbols are loaded by scanning the symbol table for addresses in each region.
 * ------------------------------------------------------------------------- */

/* C64 memory region types */
typedef enum {
    MCP_MEM_TYPE_RAM,
    MCP_MEM_TYPE_ROM,
    MCP_MEM_TYPE_IO,
    MCP_MEM_TYPE_UNMAPPED,
    MCP_MEM_TYPE_CARTRIDGE
} mcp_mem_region_type_t;

/* C64 memory region definition */
typedef struct {
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
    const char *error_msg;
    int i;
    char addr_str[8];

    log_message(mcp_tools_log, "Handling vice.memory.map");

    /* Parse optional parameters */
    if (params != NULL) {
        /* Get start address if provided */
        start_item = cJSON_GetObjectItem(params, "start");
        if (start_item != NULL) {
            int resolved = mcp_resolve_address(start_item, &error_msg);
            if (resolved < 0) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Cannot resolve start address: %s", error_msg);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
            }
            start_addr = resolved;
        }

        /* Get end address if provided */
        end_item = cJSON_GetObjectItem(params, "end");
        if (end_item != NULL) {
            int resolved = mcp_resolve_address(end_item, &error_msg);
            if (resolved < 0) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "Cannot resolve end address: %s", error_msg);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, err_buf);
            }
            end_addr = resolved;
        }

        /* Get granularity if provided */
        granularity_item = cJSON_GetObjectItem(params, "granularity");
        if (granularity_item != NULL && cJSON_IsNumber(granularity_item)) {
            granularity = granularity_item->valueint;
            if (granularity < 1) {
                granularity = 1;
            }
            if (granularity > 65536) {
                granularity = 65536;
            }
        }
    }

    /* Validate range */
    if (start_addr > end_addr) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "start address must be <= end address");
    }
    if (start_addr < 0 || start_addr > 0xFFFF) {
        return mcp_error(MCP_ERROR_INVALID_ADDRESS, "start address out of range (0x0000-0xFFFF)");
    }
    if (end_addr < 0 || end_addr > 0xFFFF) {
        return mcp_error(MCP_ERROR_INVALID_ADDRESS, "end address out of range (0x0000-0xFFFF)");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    regions_array = cJSON_CreateArray();
    if (regions_array == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add machine info */
    cJSON_AddStringToObject(response, "machine", machine_get_name());

    /* Iterate through the C64 memory map and add regions that overlap with requested range */
    for (i = 0; c64_memory_map[i].name != NULL; i++) {
        const mcp_mem_region_t *region = &c64_memory_map[i];
        uint16_t region_start, region_end;
        char *content_hint;

        /* Skip regions that don't overlap with requested range */
        if (region->end < start_addr || region->start > end_addr) {
            continue;
        }

        /* Clamp region to requested range */
        region_start = (region->start < start_addr) ? start_addr : region->start;
        region_end = (region->end > end_addr) ? end_addr : region->end;

        /* Skip regions smaller than granularity (unless they're the only region) */
        if ((region_end - region_start + 1) < (uint16_t)granularity &&
            cJSON_GetArraySize(regions_array) > 0) {
            continue;
        }

        /* Create region object */
        region_obj = cJSON_CreateObject();
        if (region_obj == NULL) {
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
