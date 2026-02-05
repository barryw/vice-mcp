/*
 * mcp_tools_disk.c - MCP disk, autostart, and machine reset tool handlers
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

#include "attach.h"
#include "autostart.h"
#include "charset.h"
#include "imagecontents.h"
#include "imagecontents/diskcontents.h"
#include "imagecontents/diskcontents-block.h"
#include "machine.h"
#include "monitor.h"
#include "vdrive/vdrive.h"

/* =========================================================================
 * Phase 2.4: Disk Management Tools
 * ========================================================================= */

cJSON* mcp_tool_disk_attach(cJSON *params)
{
    cJSON *response, *unit_item, *drive_item, *path_item;
    unsigned int unit;
    unsigned int drive;
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
    unsigned int unit;
    unsigned int drive;

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
    vdrive_t *vdrive;

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
    vdrive = file_system_get_vdrive(unit);
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
    unsigned int unit;
    unsigned int track;
    unsigned int sector;
    vdrive_t *vdrive;
    uint8_t sector_buf[256];
    char hex_buf[768];  /* 256 bytes * 3 chars (XX ) = 768 */
    char *p;
    int result;
    int i;

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
        snprintf(p, (size_t)(hex_buf + sizeof(hex_buf) - p), "%02X ", sector_buf[i]);
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

/* =========================================================================
 * Autostart Tools
 * ========================================================================= */

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

/* =========================================================================
 * Machine Control Tools
 * ========================================================================= */

/* Reset the machine (soft or hard reset)
 *
 * Parameters:
 *   mode (optional): "soft" (default) or "hard"
 *     - soft: CPU reset only (like pressing reset button)
 *     - hard: Full power cycle (resets all chips and memory)
 */
cJSON* mcp_tool_machine_reset(cJSON *params)
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
