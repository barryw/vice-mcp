/*
 * mcp_tools_input.c - MCP keyboard and joystick tool handlers
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

#include "arch/shared/hotkeys/vhkkeysyms.h"
#include "charset.h"
#include "joyport/joystick.h"
#include "kbdbuf.h"
#include "keyboard.h"
#include "machine.h"
#include "vsync.h"

/* Forward declarations for keyboard auto-release functions (defined later with matrix code) */
static void mcp_keyboard_vsync_callback(void *unused);
static void mcp_keyboard_schedule_vsync_check(void);
static int add_pending_vhk_key_release(signed long key_code, int modifiers, int frames);

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

/* Parse a key name or code from JSON to a VHK key code.
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

/* Parse keyboard modifiers from a JSON array.
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
    cJSON *key_item, *hold_frames_item, *hold_ms_item;
    signed long key_code = 0;
    int modifiers = 0;
    int hold_frames = 0;
    int hold_ms = 0;
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

    /* Get hold duration parameters */
    hold_frames_item = cJSON_GetObjectItem(params, "hold_frames");
    hold_ms_item = cJSON_GetObjectItem(params, "hold_ms");

    if (hold_frames_item != NULL && cJSON_IsNumber(hold_frames_item)) {
        hold_frames = hold_frames_item->valueint;
        if (hold_frames < 1 || hold_frames > 300) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "hold_frames must be 1-300");
        }
    }

    if (hold_ms_item != NULL && cJSON_IsNumber(hold_ms_item)) {
        hold_ms = hold_ms_item->valueint;
        if (hold_ms < 1 || hold_ms > 5000) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "hold_ms must be 1-5000");
        }
        /* Convert ms to frames: 50fps (PAL) = 20ms/frame, round up, minimum 1 */
        hold_frames = (hold_ms + 19) / 20;
        if (hold_frames < 1) hold_frames = 1;
    }

    log_message(mcp_tools_log, "Pressing key: code=%ld, modifiers=0x%04x", key_code, (unsigned int)modifiers);

    /* Press the key */
    keyboard_key_pressed(key_code, modifiers);

    /* Schedule auto-release if hold duration specified */
    if (hold_frames > 0) {
        if (add_pending_vhk_key_release(key_code, modifiers, hold_frames) < 0) {
            log_warning(mcp_tools_log, "Failed to schedule auto-release (no slots)");
        }
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "key_code", key_code);
    cJSON_AddNumberToObject(response, "modifiers", modifiers);

    /* Report scheduled auto-release if applicable */
    if (hold_frames > 0) {
        cJSON_AddNumberToObject(response, "hold_frames", hold_frames);
        if (hold_ms > 0) {
            cJSON_AddNumberToObject(response, "hold_ms", hold_ms);
        }
        cJSON_AddBoolToObject(response, "auto_release_scheduled", 1);
    }

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

/* Press or release the RESTORE key.
 *
 * RESTORE is special - it's not in the keyboard matrix but directly triggers
 * an NMI (Non-Maskable Interrupt). This is used for:
 * - RUN/STOP + RESTORE: Soft reset (returns to READY prompt)
 * - RESTORE alone: Triggers NMI handler
 *
 * Parameters:
 *   pressed: boolean - true to press, false to release (default: true)
 *
 * For RUN/STOP + RESTORE combination:
 *   1. Press RUN/STOP via keyboard.matrix (row 7, col 7)
 *   2. Press RESTORE via this tool
 *   3. Release both
 */
cJSON* mcp_tool_keyboard_restore(cJSON *params)
{
    cJSON *response;
    cJSON *pressed_item;
    int pressed = 1;  /* Default: press */

    log_message(mcp_tools_log, "RESTORE key request");

    /* Get optional pressed parameter */
    if (params != NULL) {
        pressed_item = cJSON_GetObjectItem(params, "pressed");
        if (pressed_item != NULL && cJSON_IsBool(pressed_item)) {
            pressed = cJSON_IsTrue(pressed_item);
        }
    }

    log_message(mcp_tools_log, "RESTORE key %s", pressed ? "pressed" : "released");

    /* Trigger RESTORE via machine API - this triggers NMI */
    machine_set_restore_key(pressed ? 1 : 0);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddBoolToObject(response, "pressed", pressed);
    cJSON_AddStringToObject(response, "message", pressed ? "RESTORE pressed (NMI triggered)" : "RESTORE released");

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

/* Direct keyboard matrix control for games that scan the keyboard directly
 * Instead of going through KERNAL's keyboard buffer, this sets the CIA
 * keyboard matrix state directly.
 *
 * The C64 keyboard matrix:
 * - CIA1 port A ($DC00) selects rows (active low)
 * - CIA1 port B ($DC01) reads columns (active low)
 * - See C64 keyboard matrix diagram for key positions
 */

/* Structure to track pending matrix key releases using FRAME counting */
#define MAX_PENDING_KEY_RELEASES 16
typedef struct pending_key_release_s {
    int row;
    int col;
    int frames_remaining;  /* Frames until release (decremented each vsync) */
    int active;
} pending_key_release_t;

/* Structure to track pending VHK key releases (for key_press) */
typedef struct pending_vhk_key_release_s {
    signed long key_code;
    int modifiers;
    int frames_remaining;  /* Frames until release (decremented each vsync) */
    int active;
} pending_vhk_key_release_t;

static pending_key_release_t pending_key_releases[MAX_PENDING_KEY_RELEASES];
static pending_vhk_key_release_t pending_vhk_releases[MAX_PENDING_KEY_RELEASES];
static int keyboard_pending_releases_initialized = 0;
static int vsync_callback_registered = 0;

/* Forward declarations */
static void mcp_keyboard_vsync_callback(void *unused);
static void mcp_keyboard_schedule_vsync_check(void);
static int add_pending_vhk_key_release(signed long key_code, int modifiers, int frames);

/**
 * Vsync callback to check and release pending keys.
 *
 * This runs at frame boundaries (50/60 Hz), which is a safe context for
 * releasing keys. Unlike alarms, vsync callbacks don't conflict with
 * trap handlers.
 *
 * Uses FRAME COUNTING instead of clock comparison to avoid infinite loops.
 * Each vsync decrements the frame counter. When it reaches 0, release the key.
 * This is simpler and more reliable than clock-based timing.
 */
static void mcp_keyboard_vsync_callback(void *unused)
{
    int i;
    int still_pending = 0;

    (void)unused;

    /* Release all matrix keys whose frames have elapsed */
    for (i = 0; i < MAX_PENDING_KEY_RELEASES; i++) {
        if (pending_key_releases[i].active) {
            pending_key_releases[i].frames_remaining--;
            if (pending_key_releases[i].frames_remaining <= 0) {
                keyboard_set_keyarr(pending_key_releases[i].row,
                                   pending_key_releases[i].col, 0);
                log_message(mcp_tools_log, "Auto-released matrix key: row=%d, col=%d",
                           pending_key_releases[i].row, pending_key_releases[i].col);
                pending_key_releases[i].active = 0;
            } else {
                still_pending = 1;
            }
        }
    }

    /* Release all VHK keys whose frames have elapsed */
    for (i = 0; i < MAX_PENDING_KEY_RELEASES; i++) {
        if (pending_vhk_releases[i].active) {
            pending_vhk_releases[i].frames_remaining--;
            if (pending_vhk_releases[i].frames_remaining <= 0) {
                keyboard_key_released(pending_vhk_releases[i].key_code,
                                     pending_vhk_releases[i].modifiers);
                log_message(mcp_tools_log, "Auto-released VHK key: code=%ld, modifiers=0x%04x",
                           pending_vhk_releases[i].key_code,
                           (unsigned int)pending_vhk_releases[i].modifiers);
                pending_vhk_releases[i].active = 0;
            } else {
                still_pending = 1;
            }
        }
    }

    /* Clear the flag - we've processed this vsync */
    vsync_callback_registered = 0;

    /* If there are still pending releases, register for the NEXT vsync.
     * IMPORTANT: This registration happens AFTER we clear vsync_callback_registered,
     * so the next vsync will trigger a new callback invocation. */
    if (still_pending) {
        mcp_keyboard_schedule_vsync_check();
    }
}

/**
 * Schedule a vsync callback to check pending key releases.
 *
 * This is called after adding a pending release. It ensures we have
 * a vsync callback registered that will eventually release the key.
 */
static void mcp_keyboard_schedule_vsync_check(void)
{
    if (!vsync_callback_registered) {
        vsync_on_vsync_do(mcp_keyboard_vsync_callback, NULL);
        vsync_callback_registered = 1;
    }
}

/* Initialize the pending releases arrays (lazy init) */
static void mcp_keyboard_init_pending_releases(void)
{
    if (!keyboard_pending_releases_initialized) {
        memset(pending_key_releases, 0, sizeof(pending_key_releases));
        memset(pending_vhk_releases, 0, sizeof(pending_vhk_releases));
        vsync_callback_registered = 0;
        keyboard_pending_releases_initialized = 1;
        log_message(mcp_tools_log, "Keyboard pending releases initialized (vsync-based)");
    }
}

/* Add a pending matrix key release with frame count */
static int add_pending_key_release(int row, int col, int frames)
{
    int i;

    mcp_keyboard_init_pending_releases();

    for (i = 0; i < MAX_PENDING_KEY_RELEASES; i++) {
        if (!pending_key_releases[i].active) {
            pending_key_releases[i].row = row;
            pending_key_releases[i].col = col;
            pending_key_releases[i].frames_remaining = frames;
            pending_key_releases[i].active = 1;
            mcp_keyboard_schedule_vsync_check();
            log_message(mcp_tools_log, "Scheduled matrix key release: row=%d, col=%d, frames=%d",
                       row, col, frames);
            return 0;
        }
    }
    return -1;  /* No free slots */
}

/* Add a pending VHK key release with frame count */
static int add_pending_vhk_key_release(signed long key_code, int modifiers, int frames)
{
    int i;

    mcp_keyboard_init_pending_releases();

    for (i = 0; i < MAX_PENDING_KEY_RELEASES; i++) {
        if (!pending_vhk_releases[i].active) {
            pending_vhk_releases[i].key_code = key_code;
            pending_vhk_releases[i].modifiers = modifiers;
            pending_vhk_releases[i].frames_remaining = frames;
            pending_vhk_releases[i].active = 1;
            mcp_keyboard_schedule_vsync_check();
            log_message(mcp_tools_log, "Scheduled VHK key release: code=%ld, modifiers=0x%04x, frames=%d",
                       key_code, (unsigned int)modifiers, frames);
            return 0;
        }
    }
    return -1;  /* No free slots */
}

cJSON* mcp_tool_keyboard_matrix(cJSON *params)
{
    cJSON *response, *row_item, *col_item, *pressed_item, *key_item;
    cJSON *hold_frames_item, *hold_ms_item;
    int row;
    int col;
    int pressed = 1;  /* Default: press the key */
    int hold_frames = 0;
    int hold_ms = 0;

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
        /* Modifier keys */
        else if (strcmp(key, "LSHIFT") == 0) { row = 1; col = 7; }
        else if (strcmp(key, "RSHIFT") == 0) { row = 6; col = 4; }
        else if (strcmp(key, "CTRL") == 0)   { row = 7; col = 2; }
        else if (strcmp(key, "CBM") == 0 || strcmp(key, "C=") == 0) { row = 7; col = 5; }
        /* Special keys */
        else if (strcmp(key, "HOME") == 0)   { row = 6; col = 3; }
        else if (strcmp(key, "CLR") == 0)    { row = 6; col = 3; }  /* Same as HOME, but shifted */
        else if (strcmp(key, "DEL") == 0 || strcmp(key, "INST") == 0) { row = 0; col = 0; }
        else if (strcmp(key, "POUND") == 0)  { row = 6; col = 0; }  /* £ key */
        else if (strcmp(key, "ARROWUP") == 0){ row = 6; col = 6; }  /* ↑ key */
        else if (strcmp(key, "ARROWLEFT") == 0) { row = 7; col = 1; } /* ← key */
        else if (strcmp(key, "PLUS") == 0)   { row = 5; col = 0; }
        else if (strcmp(key, "MINUS") == 0)  { row = 5; col = 3; }
        else if (strcmp(key, "ASTERISK") == 0) { row = 6; col = 1; }
        else if (strcmp(key, "AT") == 0)     { row = 5; col = 6; }  /* @ key */
        else if (strcmp(key, "COLON") == 0)  { row = 5; col = 5; }
        else if (strcmp(key, "SEMICOLON") == 0) { row = 6; col = 2; }
        else if (strcmp(key, "EQUALS") == 0) { row = 6; col = 5; }
        else if (strcmp(key, "COMMA") == 0)  { row = 5; col = 7; }
        else if (strcmp(key, "PERIOD") == 0) { row = 5; col = 4; }
        else if (strcmp(key, "SLASH") == 0)  { row = 6; col = 7; }
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
    }

    if (hold_ms_item != NULL && cJSON_IsNumber(hold_ms_item)) {
        hold_ms = hold_ms_item->valueint;
        if (hold_ms < 0 || hold_ms > 5000) {  /* Max 5 seconds */
            return mcp_error(MCP_ERROR_INVALID_PARAMS, "hold_ms must be 0-5000");
        }
        /* Convert ms to frames: 50fps (PAL) = 20ms/frame, round up, minimum 1 */
        hold_frames = (hold_ms + 19) / 20;
        if (hold_frames < 1) hold_frames = 1;
    }

    /* Set or release the key in VICE's keyboard matrix */
    if (pressed) {
        keyboard_set_keyarr(row, col, 1);
        log_message(mcp_tools_log, "Key pressed: row=%d, col=%d", row, col);

        /* Schedule auto-release if hold duration specified */
        if (hold_frames > 0) {
            if (add_pending_key_release(row, col, hold_frames) < 0) {
                log_warning(mcp_tools_log, "Failed to schedule auto-release (no slots)");
            }
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

    /* Report scheduled auto-release if applicable */
    if (hold_frames > 0 && pressed) {
        cJSON_AddNumberToObject(response, "hold_frames", hold_frames);
        if (hold_ms > 0) {
            cJSON_AddNumberToObject(response, "hold_ms", hold_ms);
        }
        cJSON_AddBoolToObject(response, "auto_release_scheduled", 1);
    }

    return response;
}
