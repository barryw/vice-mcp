/*
 * mcp_tools_trace.c - MCP execution tracing and interrupt logging tool handlers
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

/* =========================================================================
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
 * ========================================================================= */
#define MCP_MAX_TRACE_CONFIGS 16
#define MCP_MAX_TRACE_FILE_LEN 256

typedef struct mcp_trace_config_s {
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

/* =========================================================================
 * Trace Tool Handlers
 * ========================================================================= */

/* vice.trace.start
 *
 * Start recording CPU instruction execution trace.
 *
 * Parameters:
 *   - output_file (string, required): Path to write trace output
 *   - pc_filter_start (number, optional): Start of PC address filter range
 *   - pc_filter_end (number, optional): End of PC address filter range
 *   - max_instructions (number, optional): Maximum instructions to record (default 10000, max 1000000)
 *   - include_registers (boolean, optional): Include register state (default false)
 *
 * Returns:
 *   - trace_id (string): Unique identifier for this trace session
 *   - output_file (string): Confirmed output file path
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
typedef struct mcp_interrupt_entry_s {
    uint8_t type;              /* MCP_INTERRUPT_TYPE_* */
    unsigned long cycle;       /* CPU cycle when interrupt occurred */
    uint16_t pc;               /* Program counter when interrupted */
    uint16_t vector_address;   /* Vector address ($FFFE, $FFFA) */
    uint16_t handler_address;  /* What the vector pointed to */
} mcp_interrupt_entry_t;

/* Interrupt log configuration */
typedef struct mcp_interrupt_log_config_s {
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
    int i;
    int arr_size;

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

    /* Allocate entries array.
     * Note: max_entries is capped at MCP_MAX_INTERRUPT_ENTRIES (10000) above,
     * and sizeof(mcp_interrupt_entry_t) ~20 bytes, so max allocation is ~200KB.
     * No overflow risk on any reasonable platform. */
    interrupt_log_configs[config_idx].entries =
        (mcp_interrupt_entry_t *)lib_malloc((size_t)max_entries * sizeof(mcp_interrupt_entry_t));
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
        interrupt_log_configs[config_idx].active = 0;
        lib_free(interrupt_log_configs[config_idx].entries);
        interrupt_log_configs[config_idx].entries = NULL;
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
