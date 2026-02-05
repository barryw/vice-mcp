/*
 * mcp_tools_checkpoint.c - MCP checkpoint/breakpoint tool handlers
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "monitor.h"
#include "monitor/mon_breakpoint.h"

/* -------------------------------------------------------------------------
 * Checkpoint Group Management (MCP-side bookkeeping)
 *
 * VICE checkpoints are individual - no native grouping support.
 * We track membership here and iterate when toggling.
 *
 * System Limits:
 *   - Maximum number of groups: 32 (MCP_MAX_GROUPS)
 *   - Maximum checkpoints per group: 64 (MCP_MAX_CHECKPOINTS_PER_GROUP)
 *   - Maximum group name length: 63 chars + null (MCP_MAX_GROUP_NAME_LEN)
 *
 * These are static limits. When exceeded:
 *   - group.create returns error -32603 "Maximum groups reached"
 *   - group.add silently stops adding when group is full (partial success)
 *
 * TODO: Consider dynamic allocation if limits prove insufficient in practice.
 * ------------------------------------------------------------------------- */
#define MCP_MAX_GROUPS 32
#define MCP_MAX_CHECKPOINTS_PER_GROUP 64
#define MCP_MAX_GROUP_NAME_LEN 64

typedef struct mcp_checkpoint_group_s {
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

typedef struct mcp_auto_snapshot_config_s {
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
    unsigned int count;
    unsigned int i;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.checkpoint.list");

    /* Get checkpoint list from VICE */
    checkpoint_list = mon_breakpoint_checkpoint_list_get(&count);

    /* Handle NULL return (no checkpoints or allocation failure) */
    if (checkpoint_list == NULL) {
        count = 0;
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(checkpoint_list);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    checkpoints_array = cJSON_CreateArray();
    if (checkpoints_array == NULL) {
        cJSON_Delete(response);
        lib_free(checkpoint_list);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Add each checkpoint to array */
    for (i = 0; i < count; i++) {
        mon_checkpoint_t *cp = checkpoint_list[i];
        cJSON *cp_obj = cJSON_CreateObject();
        char *start_symbol, *end_symbol;
        uint16_t start_loc;
        uint16_t end_loc;

        if (cp_obj == NULL) {
            cJSON_Delete(checkpoints_array);
            cJSON_Delete(response);
            lib_free(checkpoint_list);
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

    lib_free(checkpoint_list);
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

/* parse_simple_condition is in mcp_tools_helpers.c */

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
    int checkpoint_num;
    int ignore_count;
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
    int group_idx;
    int i;

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
    int group_idx;
    int i;
    int array_size;
    int added_count = 0;

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
    array_size = cJSON_GetArraySize(ids_item);
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
    int group_idx;
    int i;
    int affected_count = 0;
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
            int j;
            int enabled_count = 0;
            int disabled_count = 0;

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
                    mon_checkpoint_t *cp;
                    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(cp_num));

                    /* Check if checkpoint exists and is enabled */
                    cp = mon_breakpoint_find_checkpoint(cp_num);
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
