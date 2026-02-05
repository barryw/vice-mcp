/*
 * mcp_tools_display.c - MCP display capture tool handlers
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
#include "machine-video.h"
#include "screenshot.h"
#include "video.h"
#include "videoarch.h"

#include <unistd.h>

/* =========================================================================
 * Phase 2.5: Display Capture Tools
 * ========================================================================= */

/* Base64 encoding table */
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Encode binary data to base64 - caller must free returned string */
static char* base64_encode(const uint8_t *data, size_t input_length, size_t *output_length)
{
    size_t i;
    size_t j;
    size_t mod;
    size_t out_len = 4 * ((input_length + 2) / 3);
    char *encoded = lib_malloc(out_len + 1);

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
    mod = input_length % 3;
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
            long file_size;

            fseek(f, 0, SEEK_END);
            file_size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (file_size > 0 && file_size < 50 * 1024 * 1024) {  /* Max 50MB */
                uint8_t *file_data = lib_malloc(file_size);
                if (file_data != NULL) {
                    size_t bytes_read = fread(file_data, 1, file_size, f);
                    if (bytes_read == (size_t)file_size) {
                        size_t b64_len;
                        char *b64_data = base64_encode(file_data, file_size, &b64_len);
                        if (b64_data != NULL) {
                            /* Add data URI prefix for easy use in HTML/web contexts */
                            const char *mime_type = strcmp(format, "PNG") == 0 ? "image/png" : "image/bmp";
                            size_t uri_size = strlen("data:") + strlen(mime_type) +
                                              strlen(";base64,") + b64_len + 1;
                            char *data_uri = lib_malloc(uri_size);
                            if (data_uri != NULL) {
                                snprintf(data_uri, uri_size, "data:%s;base64,%s", mime_type, b64_data);
                                cJSON_AddStringToObject(response, "data_uri", data_uri);
                                lib_free(data_uri);
                            }
                            cJSON_AddStringToObject(response, "base64", b64_data);
                            cJSON_AddNumberToObject(response, "size", file_size);
                            lib_free(b64_data);
                        }
                    }
                    lib_free(file_data);
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
    unsigned int width;
    unsigned int height;

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
