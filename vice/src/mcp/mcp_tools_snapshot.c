/*
 * mcp_tools_snapshot.c - MCP snapshot (save state) tool handlers
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

#include "archdep_dir.h"
#include "archdep_rename.h"
#include "util.h"

#include <ctype.h>
#include <string.h>

/* =========================================================================
 * Snapshot Management Tools
 * ========================================================================= */

static int mcp_snapshot_name_is_valid(const char *name)
{
    const char *p;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    for (p = name; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') {
            return 0;
        }
    }

    return 1;
}

static cJSON *mcp_snapshot_invalid_name_error(void)
{
    return mcp_error(MCP_ERROR_INVALID_PARAMS,
        "Invalid name: use only alphanumeric characters, underscores, and hyphens");
}

static char *mcp_build_snapshot_vsf_path(const char *dir, const char *name)
{
    char *path;
    size_t len;

    path = mcp_build_vsf_path(dir, name);
    if (path == NULL) {
        return NULL;
    }

    len = strlen(path);
    if (len < 4 || strcmp(path + len - 4, ".vsf") != 0) {
        char *full_path = lib_malloc(len + 5);
        if (full_path == NULL) {
            lib_free(path);
            return NULL;
        }
        snprintf(full_path, len + 5, "%s.vsf", path);
        lib_free(path);
        path = full_path;
    }

    return path;
}

/* The .vsf path of the snapshot params' "name" names; NULL with *error set
 * to the reply when the name is missing or invalid. Free with lib_free(). */
static char *mcp_snapshot_path_from_params(cJSON *params, const char **name_out, cJSON **error)
{
    cJSON *name_item = params ? cJSON_GetObjectItem(params, "name") : NULL;
    char *snapshots_dir;
    char *vsf_path;

    *error = NULL;
    if (!cJSON_IsString(name_item) || name_item->valuestring[0] == '\0') {
        *error = mcp_error(MCP_ERROR_INVALID_PARAMS,
                           "Missing required parameter: name (non-empty string)");
        return NULL;
    }
    if (!mcp_snapshot_name_is_valid(name_item->valuestring)) {
        *error = mcp_snapshot_invalid_name_error();
        return NULL;
    }
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        *error = mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not access snapshots directory");
        return NULL;
    }
    vsf_path = mcp_build_snapshot_vsf_path(snapshots_dir, name_item->valuestring);
    lib_free(snapshots_dir);
    if (vsf_path == NULL) {
        *error = mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not build snapshot path");
        return NULL;
    }
    *name_out = name_item->valuestring;
    return vsf_path;
}

static int mcp_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

/* The metadata sidecar of a .vsf path: name.vsf -> name.json */
static char *mcp_snapshot_json_path(const char *vsf_path)
{
    size_t stem = strlen(vsf_path) - 4;
    char *json_path = lib_malloc(stem + 6);

    if (json_path != NULL) {
        memcpy(json_path, vsf_path, stem);
        strcpy(json_path + stem, ".json");
    }
    return json_path;
}

cJSON* mcp_tool_snapshot_save(cJSON *params)
{
    cJSON *response;
    cJSON *desc_item, *roms_item, *disks_item, *overwrite_item;
    const char *name = NULL;
    const char *description = NULL;
    int include_roms = 0;
    int include_disks = 0;
    int replace;
    int metadata_ok;
    char *vsf_path;
    char *write_path;
    int result;

    log_message(mcp_tools_log, "Handling vice.snapshot.save");

    overwrite_item = params ? cJSON_GetObjectItem(params, "overwrite") : NULL;
    if (overwrite_item != NULL && !cJSON_IsBool(overwrite_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "overwrite must be a boolean");
    }

    vsf_path = mcp_snapshot_path_from_params(params, &name, &response);
    if (vsf_path == NULL) {
        return response;
    }

    /* Get optional description */
    desc_item = cJSON_GetObjectItem(params, "description");
    if (desc_item != NULL && cJSON_IsString(desc_item)) {
        description = desc_item->valuestring;
    }

    /* Get optional include_roms flag (default: false) */
    roms_item = cJSON_GetObjectItem(params, "include_roms");
    if (roms_item != NULL && cJSON_IsBool(roms_item)) {
        include_roms = cJSON_IsTrue(roms_item);
    }

    /* Get optional include_disks flag (default: false) */
    disks_item = cJSON_GetObjectItem(params, "include_disks");
    if (disks_item != NULL && cJSON_IsBool(disks_item)) {
        include_disks = cJSON_IsTrue(disks_item);
    }

    replace = mcp_file_exists(vsf_path);
    if (replace && !cJSON_IsTrue(overwrite_item)) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Snapshot with this name already exists. Pass overwrite: true to replace it, "
            "or delete it with vice.snapshot.delete.");
    }

    log_message(mcp_tools_log, "Saving snapshot: %s (roms=%d, disks=%d)",
                vsf_path, include_roms, include_disks);

    /* A replacement is written beside the old snapshot and renamed over
     * it, so a failed save leaves the old one intact */
    write_path = replace ? util_concat(vsf_path, ".tmp", NULL) : lib_strdup(vsf_path);
    if (write_path == NULL) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    result = machine_write_snapshot(write_path, include_roms, include_disks, 0);
    if (result == 0 && replace && archdep_rename(write_path, vsf_path) != 0) {
        /* rename() does not replace an existing file on Windows: move the
         * old snapshot aside, and back if the new one cannot take its place */
        char *old_path = util_concat(vsf_path, ".old", NULL);

        result = -1;
        if (old_path != NULL && archdep_rename(vsf_path, old_path) == 0) {
            if (archdep_rename(write_path, vsf_path) == 0) {
                remove(old_path);
                result = 0;
            } else {
                archdep_rename(old_path, vsf_path);
            }
        }
        lib_free(old_path);
    }
    if (result != 0) {
        /* Clean up the partial or unplaced file */
        remove(write_path);
        lib_free(write_path);
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to save snapshot");
    }
    lib_free(write_path);

    /* Write metadata sidecar file. If that fails, a replaced snapshot's
     * old sidecar would describe the wrong state: remove it. */
    metadata_ok = mcp_write_snapshot_metadata(vsf_path, name, description,
                                              include_roms, include_disks) == 0;
    if (!metadata_ok && replace) {
        char *json_path = mcp_snapshot_json_path(vsf_path);

        if (json_path != NULL) {
            remove(json_path);
            lib_free(json_path);
        }
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "name", name);
    cJSON_AddStringToObject(response, "path", vsf_path);
    if (description != NULL) {
        cJSON_AddStringToObject(response, "description", description);
    }
    cJSON_AddBoolToObject(response, "include_roms", include_roms);
    cJSON_AddBoolToObject(response, "include_disks", include_disks);
    cJSON_AddBoolToObject(response, "replaced", replace);
    if (!metadata_ok) {
        cJSON_AddStringToObject(response, "warning",
            "Snapshot saved, but its metadata file could not be written");
    }

    lib_free(vsf_path);
    return response;
}

cJSON* mcp_tool_snapshot_load(cJSON *params)
{
    cJSON *response;
    const char *name = NULL;
    char *vsf_path;
    int result;

    log_message(mcp_tools_log, "Handling vice.snapshot.load");

    vsf_path = mcp_snapshot_path_from_params(params, &name, &response);
    if (vsf_path == NULL) {
        return response;
    }
    if (!mcp_file_exists(vsf_path)) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Snapshot not found. Use vice.snapshot.list to see available snapshots.");
    }

    log_message(mcp_tools_log, "Loading snapshot: %s", vsf_path);

    /* The snapshot replaces the CPU clock a pending run_until cycle limit
     * counts on; the run it was set for ends here */
    mcp_run_until_cancel();

    /* Load the snapshot */
    result = machine_read_snapshot(vsf_path, 0);

    if (result != 0) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to load snapshot");
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "name", name);
    cJSON_AddStringToObject(response, "path", vsf_path);
    cJSON_AddStringToObject(response, "message", "Snapshot loaded successfully");

    lib_free(vsf_path);
    return response;
}

cJSON* mcp_tool_snapshot_delete(cJSON *params)
{
    cJSON *response;
    const char *name = NULL;
    char *vsf_path;
    char *json_path;
    int failed;

    log_message(mcp_tools_log, "Handling vice.snapshot.delete");

    vsf_path = mcp_snapshot_path_from_params(params, &name, &response);
    if (vsf_path == NULL) {
        return response;
    }
    if (!mcp_file_exists(vsf_path)) {
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Snapshot not found. Use vice.snapshot.list to see available snapshots.");
    }

    failed = remove(vsf_path) != 0;
    json_path = mcp_snapshot_json_path(vsf_path);
    if (!failed && json_path != NULL) {
        remove(json_path);  /* the sidecar may be missing */
    }
    lib_free(json_path);
    lib_free(vsf_path);
    if (failed) {
        return mcp_error(MCP_ERROR_SNAPSHOT_FAILED, "Could not delete the snapshot file");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddStringToObject(response, "name", name);
    cJSON_AddBoolToObject(response, "deleted", true);
    return response;
}

cJSON* mcp_tool_snapshot_list(cJSON *params)
{
    cJSON *response, *snapshots_array;
    char *snapshots_dir;
    archdep_dir_t *dir;

    (void)params;

    log_message(mcp_tools_log, "Handling vice.snapshot.list");

    /* Get snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not access snapshots directory");
    }

    /* Create response object */
    response = cJSON_CreateObject();
    if (response == NULL) {
        lib_free(snapshots_dir);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    snapshots_array = cJSON_CreateArray();
    if (snapshots_array == NULL) {
        cJSON_Delete(response);
        lib_free(snapshots_dir);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddItemToObject(response, "snapshots", snapshots_array);
    cJSON_AddStringToObject(response, "directory", snapshots_dir);

    /* Open directory and enumerate .vsf files */
    dir = archdep_opendir(snapshots_dir, ARCHDEP_OPENDIR_ALL_FILES);
    if (dir != NULL) {
        const char *entry_name;
        while ((entry_name = archdep_readdir(dir)) != NULL) {
            const char *ext;
            size_t name_len = strlen(entry_name);

            /* Skip if not a .vsf file */
            if (name_len < 5) continue;
            ext = entry_name + name_len - 4;
            if (strcmp(ext, ".vsf") != 0) continue;

            /* Found a snapshot file */
            {
                cJSON *snapshot_obj = cJSON_CreateObject();
                if (snapshot_obj != NULL) {
                    /* Extract name (without .vsf extension) */
                    char *name = lib_malloc(name_len - 3);
                    char *vsf_path = NULL;
                    if (name != NULL) {
                        strncpy(name, entry_name, name_len - 4);
                        name[name_len - 4] = '\0';
                        cJSON_AddStringToObject(snapshot_obj, "name", name);
                    }

                    /* Add full path */
                    vsf_path = mcp_build_vsf_path(snapshots_dir, entry_name);
                    if (vsf_path != NULL) {
                        cJSON_AddStringToObject(snapshot_obj, "path", vsf_path);
                    }

                    /* Try to read metadata from sidecar */
                    if (vsf_path != NULL) {
                        cJSON *metadata = mcp_read_snapshot_metadata(vsf_path);

                        if (metadata != NULL) {
                            cJSON *desc, *created, *machine;

                            desc = cJSON_GetObjectItem(metadata, "description");
                            if (desc && cJSON_IsString(desc)) {
                                cJSON_AddStringToObject(snapshot_obj, "description", desc->valuestring);
                            }
                            created = cJSON_GetObjectItem(metadata, "created");
                            if (created && cJSON_IsString(created)) {
                                cJSON_AddStringToObject(snapshot_obj, "created", created->valuestring);
                            }
                            machine = cJSON_GetObjectItem(metadata, "machine");
                            if (machine && cJSON_IsString(machine)) {
                                cJSON_AddStringToObject(snapshot_obj, "machine", machine->valuestring);
                            }
                            cJSON_Delete(metadata);
                        }
                    }

                    cJSON_AddItemToArray(snapshots_array, snapshot_obj);
                    if (vsf_path != NULL) {
                        lib_free(vsf_path);
                    }
                    if (name != NULL) {
                        lib_free(name);
                    }
                }
            }
        }
        archdep_closedir(dir);
    }

    lib_free(snapshots_dir);
    return response;
}
