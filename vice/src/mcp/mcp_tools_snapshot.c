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

#include <ctype.h>
#include <dirent.h>
#include <string.h>

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

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get required name parameter */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: name (string) - a descriptive name for this snapshot");
    }
    name = name_item->valuestring;

    /* Validate name is not empty */
    if (name[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "'name' must not be empty");
    }

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

    /* Get/create snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not create snapshots directory");
    }

    /* Build vsf path from name */
    vsf_path = mcp_build_vsf_path(snapshots_dir, name);
    lib_free(snapshots_dir);

    if (vsf_path == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not build snapshot path");
    }

    /* Check if file already exists */
    {
        FILE *f = fopen(vsf_path, "r");
        if (f != NULL) {
            fclose(f);
            lib_free(vsf_path);
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "Snapshot with this name already exists. Use a different name or delete the existing one first.");
        }
    }

    /* Ensure the path ends with .vsf for machine_write_snapshot */
    {
        size_t len = strlen(vsf_path);
        if (len < 4 || strcmp(vsf_path + len - 4, ".vsf") != 0) {
            char *full_path = lib_malloc(len + 5);
            if (full_path == NULL) {
                lib_free(vsf_path);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            snprintf(full_path, len + 5, "%s.vsf", vsf_path);
            lib_free(vsf_path);
            vsf_path = full_path;
        }
    }

    log_message(mcp_tools_log, "Saving snapshot: %s (roms=%d, disks=%d)",
                vsf_path, include_roms, include_disks);

    /* Save the snapshot */
    result = machine_write_snapshot(vsf_path, include_roms, include_disks, 0);

    if (result != 0) {
        /* Clean up partial file on failure */
        remove(vsf_path);
        lib_free(vsf_path);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Failed to save snapshot");
    }

    /* Write metadata sidecar file */
    mcp_write_snapshot_metadata(vsf_path, name, description, include_roms, include_disks);

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
    int result;

    log_message(mcp_tools_log, "Handling vice.snapshot.load");

    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "Missing parameters");
    }

    /* Get required name parameter */
    name_item = cJSON_GetObjectItem(params, "name");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
            "Missing required parameter: name (string) - name of the snapshot to load");
    }
    name = name_item->valuestring;

    /* Validate name */
    if (name[0] == '\0') {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "'name' must not be empty");
    }

    /* Get snapshots directory */
    snapshots_dir = mcp_get_snapshots_dir();
    if (snapshots_dir == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not access snapshots directory");
    }

    /* Build vsf path from name */
    vsf_path = mcp_build_vsf_path(snapshots_dir, name);
    lib_free(snapshots_dir);

    if (vsf_path == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Could not build snapshot path");
    }

    /* Check if file exists */
    {
        FILE *f = fopen(vsf_path, "r");
        if (f == NULL) {
            /* Try with .vsf extension */
            size_t len = strlen(vsf_path);
            char *full_path = lib_malloc(len + 5);
            if (full_path == NULL) {
                lib_free(vsf_path);
                return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
            }
            snprintf(full_path, len + 5, "%s.vsf", vsf_path);
            lib_free(vsf_path);
            vsf_path = full_path;

            f = fopen(vsf_path, "r");
            if (f == NULL) {
                lib_free(vsf_path);
                return mcp_error(MCP_ERROR_INVALID_PARAMS,
                    "Snapshot not found. Use vice.snapshot.list to see available snapshots.");
            }
        }
        fclose(f);
    }

    log_message(mcp_tools_log, "Loading snapshot: %s", vsf_path);

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

cJSON* mcp_tool_snapshot_list(cJSON *params)
{
    cJSON *response, *snapshots_array;
    char *snapshots_dir;
    DIR *dir;

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
    dir = opendir(snapshots_dir);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            const char *ext;
            size_t name_len = strlen(entry->d_name);

            /* Skip if not a .vsf file */
            if (name_len < 5) continue;
            ext = entry->d_name + name_len - 4;
            if (strcmp(ext, ".vsf") != 0) continue;

            /* Found a snapshot file */
            {
                cJSON *snapshot_obj = cJSON_CreateObject();
                if (snapshot_obj != NULL) {
                    /* Extract name (without .vsf extension) */
                    char *name = lib_malloc(name_len - 3);
                    if (name != NULL) {
                        strncpy(name, entry->d_name, name_len - 4);
                        name[name_len - 4] = '\0';
                        cJSON_AddStringToObject(snapshot_obj, "name", name);
                    }

                    /* Add full path */
                    {
                        char *vsf_path = mcp_build_vsf_path(snapshots_dir, entry->d_name);
                        if (vsf_path != NULL) {
                            cJSON_AddStringToObject(snapshot_obj, "path", vsf_path);
                            lib_free(vsf_path);
                        }
                    }

                    /* Try to read metadata from sidecar */
                    {
                        char *vsf_path = mcp_build_vsf_path(snapshots_dir, entry->d_name);
                        cJSON *metadata = mcp_read_snapshot_metadata(vsf_path);
                        lib_free(vsf_path);

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

                        cJSON_AddItemToArray(snapshots_array, snapshot_obj);

                        lib_free(vsf_path);
                        lib_free(name);
                    }
                }
            }
        }
        closedir(dir);
    }

    lib_free(snapshots_dir);
    return response;
}
