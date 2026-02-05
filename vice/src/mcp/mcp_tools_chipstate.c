/*
 * mcp_tools_chipstate.c - MCP hardware state tool handlers
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

#include "lib.h"
#include "mem.h"
#include "machine.h"

/* ========================================================================= */
/* Machine Validation Helper                                                */
/* ========================================================================= */

/* Check if the current machine has VIC-II (sprites, VIC-II registers).
 * Returns 1 if VIC-II is available, 0 otherwise. */
static int machine_has_vicii(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
            return 1;
        default:
            return 0;
    }
}

/* Check if the current machine has SID.
 * Returns 1 if SID is available, 0 otherwise. */
static int machine_has_sid(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
            return 1;
        default:
            return 0;
    }
}

/* Check if the current machine has CIA chips.
 * Returns 1 if CIAs are available, 0 otherwise. */
static int machine_has_cia(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
            return 1;
        default:
            return 0;
    }
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
    int i;
    int start;
    int end;

    log_message(mcp_tools_log, "Handling vice.sprite.get");

    /* Validate machine has VIC-II (sprites only available on C64/C128/DTV) */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "Sprites not available on this machine");
    }

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

    /* Read global VIC-II sprite registers once before the loop */
    {
        uint8_t enable_reg = mem_bank_peek(0, VICII_SPRITE_ENABLE, NULL);
        uint8_t x_msb_reg = mem_bank_peek(0, VICII_SPRITE_X_MSB, NULL);
        uint8_t multicolor_reg = mem_bank_peek(0, VICII_SPRITE_MULTICOLOR, NULL);
        uint8_t expand_x_reg = mem_bank_peek(0, VICII_SPRITE_EXPAND_X, NULL);
        uint8_t expand_y_reg = mem_bank_peek(0, VICII_SPRITE_EXPAND_Y, NULL);
        uint8_t priority_reg = mem_bank_peek(0, VICII_SPRITE_PRIORITY, NULL);

        for (i = start; i <= end; i++) {
            uint16_t x;
            uint16_t y;
            uint8_t color;
            char sprite_key[16];

            /* Read per-sprite VIC-II registers */
            x = mem_bank_peek(0, VICII_BASE + (i * 2), NULL);  /* $D000, $D002, $D004, ... */
            y = mem_bank_peek(0, VICII_BASE + (i * 2) + 1, NULL);  /* $D001, $D003, $D005, ... */
            color = mem_bank_peek(0, VICII_SPRITE_COLOR_BASE + i, NULL);  /* $D027-$D02E */

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
                snprintf(sprite_key, sizeof(sprite_key), "sprite_%d", i);
                cJSON_AddItemToObject(response, sprite_key, sprite_obj);
            }
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
    uint8_t enable_reg;
    uint8_t x_msb_reg;
    uint8_t multicolor_reg;
    uint8_t expand_x_reg;
    uint8_t expand_y_reg;
    uint8_t priority_reg;

    log_message(mcp_tools_log, "Handling vice.sprite.set");

    /* Validate machine has VIC-II (sprites only available on C64/C128/DTV) */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "Sprites not available on this machine");
    }

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
    enable_reg = mem_bank_peek(0, VICII_SPRITE_ENABLE, NULL);
    x_msb_reg = mem_bank_peek(0, VICII_SPRITE_X_MSB, NULL);
    multicolor_reg = mem_bank_peek(0, VICII_SPRITE_MULTICOLOR, NULL);
    expand_x_reg = mem_bank_peek(0, VICII_SPRITE_EXPAND_X, NULL);
    expand_y_reg = mem_bank_peek(0, VICII_SPRITE_EXPAND_Y, NULL);
    priority_reg = mem_bank_peek(0, VICII_SPRITE_PRIORITY, NULL);

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
    uint8_t d011;
    uint8_t d012;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.vicii.get_state");

    /* Validate machine has VIC-II */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "VIC-II not available on this machine");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Read key VIC-II registers using peek to avoid side effects */
    d011 = mem_bank_peek(0, VICII_BASE + 0x11, NULL);  /* Control register 1 */
    d012 = mem_bank_peek(0, VICII_BASE + 0x12, NULL);  /* Raster line */

    /* Add computed/useful state */
    cJSON_AddNumberToObject(response, "raster_line", d012 | ((d011 & 0x80) << 1));  /* 9-bit raster */
    cJSON_AddNumberToObject(response, "video_mode", ((d011 & 0x60) | (mem_bank_peek(0, VICII_BASE + 0x16, NULL) & 0x10)) >> 4);
    cJSON_AddBoolToObject(response, "screen_enabled", (d011 & 0x10) != 0);
    cJSON_AddBoolToObject(response, "25_rows", (d011 & 0x08) != 0);
    cJSON_AddNumberToObject(response, "y_scroll", d011 & 0x07);
    cJSON_AddNumberToObject(response, "x_scroll", mem_bank_peek(0, VICII_BASE + 0x16, NULL) & 0x07);

    /* Border colors */
    cJSON_AddNumberToObject(response, "border_color", mem_bank_peek(0, VICII_BASE + 0x20, NULL));
    cJSON_AddNumberToObject(response, "background_color_0", mem_bank_peek(0, VICII_BASE + 0x21, NULL));
    cJSON_AddNumberToObject(response, "background_color_1", mem_bank_peek(0, VICII_BASE + 0x22, NULL));
    cJSON_AddNumberToObject(response, "background_color_2", mem_bank_peek(0, VICII_BASE + 0x23, NULL));
    cJSON_AddNumberToObject(response, "background_color_3", mem_bank_peek(0, VICII_BASE + 0x24, NULL));

    /* Sprite collisions - peek to avoid clearing on read */
    cJSON_AddNumberToObject(response, "sprite_sprite_collision", mem_bank_peek(0, VICII_BASE + 0x1E, NULL));
    cJSON_AddNumberToObject(response, "sprite_background_collision", mem_bank_peek(0, VICII_BASE + 0x1F, NULL));

    /* IRQ status - peek to avoid clearing on read */
    cJSON_AddNumberToObject(response, "irq_status", mem_bank_peek(0, VICII_BASE + 0x19, NULL));
    cJSON_AddNumberToObject(response, "irq_enabled", mem_bank_peek(0, VICII_BASE + 0x1A, NULL));

    /* Memory pointers */
    cJSON_AddNumberToObject(response, "memory_pointers", mem_bank_peek(0, VICII_BASE + 0x18, NULL));

    /* Add all registers as array for completeness */
    registers_obj = cJSON_CreateArray();
    if (registers_obj == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    for (i = 0; i < 0x2F; i++) {
        cJSON_AddItemToArray(registers_obj, cJSON_CreateNumber(mem_bank_peek(0, VICII_BASE + i, NULL)));
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

    /* Validate machine has SID */
    if (!machine_has_sid()) {
        return mcp_error(-32000, "SID not available on this machine");
    }

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
        uint16_t freq;
        uint16_t pulse_width;
        uint8_t control;
        uint8_t attack_decay;
        uint8_t sustain_release;
        int base = SID_BASE + (v * 7);

        freq = mem_bank_peek(0, base + 0, NULL) | (mem_bank_peek(0, base + 1, NULL) << 8);
        pulse_width = mem_bank_peek(0, base + 2, NULL) | (mem_bank_peek(0, base + 3, NULL) << 8);
        control = mem_bank_peek(0, base + 4, NULL);
        attack_decay = mem_bank_peek(0, base + 5, NULL);
        sustain_release = mem_bank_peek(0, base + 6, NULL);

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

    /* Filter and volume - use mem_bank_peek since SID registers are write-only */
    {
        uint8_t reg17 = mem_bank_peek(0, SID_BASE + 0x17, NULL);
        uint8_t reg18 = mem_bank_peek(0, SID_BASE + 0x18, NULL);

        cJSON_AddNumberToObject(response, "filter_cutoff_low", mem_bank_peek(0, SID_BASE + 0x15, NULL));
        cJSON_AddNumberToObject(response, "filter_cutoff_high", mem_bank_peek(0, SID_BASE + 0x16, NULL));
        cJSON_AddNumberToObject(response, "filter_resonance", (reg17 >> 4) & 0x0F);
        cJSON_AddBoolToObject(response, "filter_voice3", (reg17 & 0x04) != 0);
        cJSON_AddBoolToObject(response, "filter_voice2", (reg17 & 0x02) != 0);
        cJSON_AddBoolToObject(response, "filter_voice1", (reg17 & 0x01) != 0);
        cJSON_AddBoolToObject(response, "filter_ext", (reg18 & 0x08) != 0);
        cJSON_AddBoolToObject(response, "voice3_off", (reg18 & 0x80) != 0);
        cJSON_AddBoolToObject(response, "highpass", (reg18 & 0x40) != 0);
        cJSON_AddBoolToObject(response, "bandpass", (reg18 & 0x20) != 0);
        cJSON_AddBoolToObject(response, "lowpass", (reg18 & 0x10) != 0);
        cJSON_AddNumberToObject(response, "volume", reg18 & 0x0F);
    }

    return response;
}

cJSON* mcp_tool_cia_get_state(cJSON *params)
{
    cJSON *response, *cia1_obj, *cia2_obj, *cia_item;
    int cia;

    log_message(mcp_tools_log, "Handling vice.cia.get_state");

    /* Validate machine has CIA chips */
    if (!machine_has_cia()) {
        return mcp_error(-32000, "CIA not available on this machine");
    }

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

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Helper macro to build CIA state - uses mem_bank_peek to avoid
     * side effects (CIA timers latch on read via mem_read) */
    #define BUILD_CIA_STATE(obj, base) do { \
        uint16_t timer_a; \
        uint16_t timer_b; \
        obj = cJSON_CreateObject(); \
        if (obj == NULL) { \
            cJSON_Delete(response); \
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory"); \
        } \
        cJSON_AddNumberToObject(obj, "port_a", mem_bank_peek(0, base + 0, NULL)); \
        cJSON_AddNumberToObject(obj, "port_b", mem_bank_peek(0, base + 1, NULL)); \
        cJSON_AddNumberToObject(obj, "ddr_a", mem_bank_peek(0, base + 2, NULL)); \
        cJSON_AddNumberToObject(obj, "ddr_b", mem_bank_peek(0, base + 3, NULL)); \
        timer_a = mem_bank_peek(0, base + 4, NULL) | (mem_bank_peek(0, base + 5, NULL) << 8); \
        timer_b = mem_bank_peek(0, base + 6, NULL) | (mem_bank_peek(0, base + 7, NULL) << 8); \
        cJSON_AddNumberToObject(obj, "timer_a", timer_a); \
        cJSON_AddNumberToObject(obj, "timer_b", timer_b); \
        cJSON_AddNumberToObject(obj, "tod_10ths", mem_bank_peek(0, base + 8, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_seconds", mem_bank_peek(0, base + 9, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_minutes", mem_bank_peek(0, base + 10, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_hours", mem_bank_peek(0, base + 11, NULL)); \
        cJSON_AddNumberToObject(obj, "serial_data", mem_bank_peek(0, base + 12, NULL)); \
        cJSON_AddNumberToObject(obj, "interrupt_control", mem_bank_peek(0, base + 13, NULL)); \
        cJSON_AddNumberToObject(obj, "control_a", mem_bank_peek(0, base + 14, NULL)); \
        cJSON_AddNumberToObject(obj, "control_b", mem_bank_peek(0, base + 15, NULL)); \
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

    log_message(mcp_tools_log, "Handling vice.vicii.set_state");

    /* Validate machine has VIC-II */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "VIC-II not available on this machine");
    }

    /* All parameters optional - set only what's provided */

    /* Generic register array - allows setting any VIC-II register by offset */
    registers_array = cJSON_GetObjectItem(params, "registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i;
        int array_size = cJSON_GetArraySize(registers_array);
        for (i = 0; i < array_size; i++) {
            cJSON *reg_obj = cJSON_GetArrayItem(registers_array, i);
            if (reg_obj != NULL && cJSON_IsObject(reg_obj)) {
                cJSON *offset_item = cJSON_GetObjectItem(reg_obj, "offset");
                cJSON *value_item = cJSON_GetObjectItem(reg_obj, "value");
                if (offset_item != NULL && value_item != NULL &&
                    cJSON_IsNumber(offset_item) && cJSON_IsNumber(value_item)) {
                    int offset = offset_item->valueint;
                    if (offset >= 0 && offset <= 0x2E) {  /* VIC-II has 47 registers (0x00-0x2E) */
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
        uint8_t d011;
        mem_store(VICII_BASE + 0x12, line & 0xFF);
        d011 = mem_bank_peek(0, VICII_BASE + 0x11, NULL);
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

    log_message(mcp_tools_log, "Handling vice.sid.set_state");

    /* Validate machine has SID */
    if (!machine_has_sid()) {
        return mcp_error(-32000, "SID not available on this machine");
    }

    /* Generic register array - allows setting any SID register by offset */
    registers_array = cJSON_GetObjectItem(params, "registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i;
        int array_size = cJSON_GetArraySize(registers_array);
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
        snprintf(voice_key, sizeof(voice_key), "voice%d", v);

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

    log_message(mcp_tools_log, "Handling vice.cia.set_state");

    /* Validate machine has CIA chips */
    if (!machine_has_cia()) {
        return mcp_error(-32000, "CIA not available on this machine");
    }

    /* Generic register arrays for CIA1 and CIA2 */
    registers_array = cJSON_GetObjectItem(params, "cia1_registers");
    if (registers_array != NULL && cJSON_IsArray(registers_array)) {
        int i;
        int array_size = cJSON_GetArraySize(registers_array);
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
        int i;
        int array_size = cJSON_GetArraySize(registers_array);
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

/* ==========================================================================
 * Phase 5.5: Sprite Inspect Tool
 * ==========================================================================
 * Provides visual representation of C64 sprite bitmap data.
 *
 * Sprite Layout on C64:
 * - Sprite pointers at $07F8-$07FF (one byte per sprite in default screen memory)
 * - Data address = pointer * 64 (within VIC bank)
 * - Each sprite is 24x21 pixels = 63 bytes (3 bytes/row x 21 rows)
 * - Multicolor flag from $D01C (one bit per sprite)
 * - Sprite colors from $D027-$D02E (one per sprite)
 * - Multicolor 1 from $D025, Multicolor 2 from $D026
 *
 * ASCII Legend:
 * - '.' = transparent (00 in hires, or multicolor)
 * - '#' = sprite color (1 in hires, 10 in multicolor)
 * - '@' = multicolor 1 (01 in multicolor mode)
 * - '%' = multicolor 2 (11 in multicolor mode)
 */

/* Multicolor color registers */
#define VICII_SPRITE_MULTI1 0xD025
#define VICII_SPRITE_MULTI2 0xD026

/* Sprite dimensions */
#define SPRITE_WIDTH  24
#define SPRITE_HEIGHT 21
#define SPRITE_BYTES  63

cJSON* mcp_tool_sprite_inspect(cJSON *params)
{
    cJSON *response, *dimensions, *colors, *raw_data, *sprite_num_item, *format_item;
    int sprite_number;
    const char *format_str = "ascii";  /* Default format */
    uint8_t pointer_value;
    uint16_t pointer_addr;
    uint16_t data_addr;
    uint8_t multicolor_reg;
    uint8_t sprite_color;
    uint8_t multi1_color;
    uint8_t multi2_color;
    int is_multicolor;
    uint8_t sprite_data[SPRITE_BYTES];
    char addr_str[8];
    char *bitmap_str;
    char *ptr;
    size_t bitmap_size;
    int row;
    int col;
    int byte_idx;
    int bit_idx;
    int i;

    log_message(mcp_tools_log, "Handling vice.sprite.inspect");

    /* Validate machine has VIC-II (sprites only available on C64/C128/DTV) */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "Sprites not available on this machine");
    }

    /* Validate required parameters */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite_number (0-7) is required");
    }

    sprite_num_item = cJSON_GetObjectItem(params, "sprite_number");
    if (sprite_num_item == NULL || !cJSON_IsNumber(sprite_num_item)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite_number (0-7) is required");
    }

    sprite_number = sprite_num_item->valueint;
    if (sprite_number < 0 || sprite_number > 7) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, "sprite_number must be 0-7");
    }

    /* Get optional format parameter */
    format_item = cJSON_GetObjectItem(params, "format");
    if (format_item != NULL && cJSON_IsString(format_item)) {
        format_str = format_item->valuestring;
        if (strcmp(format_str, "ascii") != 0 &&
            strcmp(format_str, "binary") != 0 &&
            strcmp(format_str, "png_base64") != 0) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "format must be 'ascii', 'binary', or 'png_base64'");
        }
    }

    /* Calculate sprite pointer address from current VIC bank and screen base.
     * VIC bank is determined by CIA2 port A ($DD00) bits 0-1,
     * screen base by VIC-II register $D018 bits 4-7. */
    {
        uint8_t cia2_port_a = mem_bank_peek(0, 0xDD00, NULL);
        uint16_t vic_bank = (uint16_t)((3 - (cia2_port_a & 0x03)) * 0x4000);
        uint8_t d018 = mem_bank_peek(0, VICII_BASE + 0x18, NULL);
        uint16_t screen_base = vic_bank + (uint16_t)((d018 >> 4) * 0x0400);
        pointer_addr = screen_base + 0x03F8 + sprite_number;
        pointer_value = mem_bank_peek(0, pointer_addr, NULL);
        data_addr = vic_bank + (uint16_t)(pointer_value * 64);
    }

    /* Read multicolor register */
    multicolor_reg = mem_bank_peek(0, VICII_SPRITE_MULTICOLOR, NULL);
    is_multicolor = (multicolor_reg & (1 << sprite_number)) != 0;

    /* Read sprite colors */
    sprite_color = mem_bank_peek(0, VICII_SPRITE_COLOR_BASE + sprite_number, NULL);
    multi1_color = mem_bank_peek(0, VICII_SPRITE_MULTI1, NULL);
    multi2_color = mem_bank_peek(0, VICII_SPRITE_MULTI2, NULL);

    /* Read sprite data (63 bytes) */
    for (i = 0; i < SPRITE_BYTES; i++) {
        sprite_data[i] = mem_bank_peek(0, data_addr + i, NULL);
    }

    /* Build response object */
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Pointer address */
    snprintf(addr_str, sizeof(addr_str), "$%04X", pointer_addr);
    cJSON_AddStringToObject(response, "pointer", addr_str);

    /* Data address */
    snprintf(addr_str, sizeof(addr_str), "$%04X", data_addr);
    cJSON_AddStringToObject(response, "data_address", addr_str);

    /* Dimensions */
    dimensions = cJSON_CreateObject();
    if (dimensions == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddNumberToObject(dimensions, "width", SPRITE_WIDTH);
    cJSON_AddNumberToObject(dimensions, "height", SPRITE_HEIGHT);
    cJSON_AddItemToObject(response, "dimensions", dimensions);

    /* Multicolor flag */
    cJSON_AddBoolToObject(response, "multicolor", is_multicolor);

    /* Colors */
    colors = cJSON_CreateObject();
    if (colors == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddNumberToObject(colors, "main", sprite_color);
    if (is_multicolor) {
        cJSON_AddNumberToObject(colors, "multi1", multi1_color);
        cJSON_AddNumberToObject(colors, "multi2", multi2_color);
    }
    cJSON_AddItemToObject(response, "colors", colors);

    /* Raw data as array */
    raw_data = cJSON_CreateArray();
    if (raw_data == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    for (i = 0; i < SPRITE_BYTES; i++) {
        cJSON_AddItemToArray(raw_data, cJSON_CreateNumber(sprite_data[i]));
    }
    cJSON_AddItemToObject(response, "raw_data", raw_data);

    /* Generate bitmap string based on format */
    if (strcmp(format_str, "png_base64") == 0) {
        /* PNG encoding not implemented in this phase - return placeholder */
        cJSON_AddStringToObject(response, "bitmap", "(png_base64 not implemented)");
    } else {
        /* ASCII or binary format */
        /* Each row is 24 chars (hires) or 12 double-wide chars (multicolor)
         * + newline, times 21 rows, plus null terminator.
         * Allocate generously for both formats. */
        bitmap_size = (SPRITE_WIDTH + 1) * SPRITE_HEIGHT + 1;
        bitmap_str = lib_malloc(bitmap_size);
        if (bitmap_str == NULL) {
            cJSON_Delete(response);
            return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
        }

        ptr = bitmap_str;

        if (is_multicolor) {
            /* Multicolor mode: 2 bits per pixel, 12 pixels per row */
            /* Bit pairs: 00=transparent, 01=multi1, 10=sprite, 11=multi2 */
            for (row = 0; row < SPRITE_HEIGHT; row++) {
                byte_idx = row * 3;  /* 3 bytes per row */

                for (col = 0; col < 12; col++) {
                    /* Each byte has 4 pixel pairs (2 bits each) */
                    int byte_offset;
                    int pair_in_byte;
                    uint8_t byte_val;
                    int pixel_pair;
                    char c1, c2;

                    byte_offset = col / 4;
                    pair_in_byte = 3 - (col % 4);  /* Pairs are high to low */
                    byte_val = sprite_data[byte_idx + byte_offset];
                    pixel_pair = (byte_val >> (pair_in_byte * 2)) & 0x03;

                    if (strcmp(format_str, "binary") == 0) {
                        /* Binary format: show as 00, 01, 10, 11 */
                        c1 = (pixel_pair & 0x02) ? '1' : '0';
                        c2 = (pixel_pair & 0x01) ? '1' : '0';
                    } else {
                        /* ASCII format: use legend characters */
                        switch (pixel_pair) {
                            case 0: c1 = '.'; c2 = '.'; break;  /* Transparent */
                            case 1: c1 = '@'; c2 = '@'; break;  /* Multi1 */
                            case 2: c1 = '#'; c2 = '#'; break;  /* Sprite color */
                            case 3: c1 = '%'; c2 = '%'; break;  /* Multi2 */
                            default: c1 = '?'; c2 = '?'; break;
                        }
                    }
                    *ptr++ = c1;
                    *ptr++ = c2;
                }
                *ptr++ = '\n';
            }
        } else {
            /* Hires mode: 1 bit per pixel, 24 pixels per row */
            for (row = 0; row < SPRITE_HEIGHT; row++) {
                byte_idx = row * 3;  /* 3 bytes per row */

                for (col = 0; col < SPRITE_WIDTH; col++) {
                    int byte_offset;
                    uint8_t byte_val;
                    int pixel;
                    char c;

                    byte_offset = col / 8;
                    bit_idx = 7 - (col % 8);  /* Bits are MSB first */
                    byte_val = sprite_data[byte_idx + byte_offset];
                    pixel = (byte_val >> bit_idx) & 0x01;

                    if (strcmp(format_str, "binary") == 0) {
                        c = pixel ? '1' : '0';
                    } else {
                        c = pixel ? '#' : '.';
                    }
                    *ptr++ = c;
                }
                *ptr++ = '\n';
            }
        }
        /* Remove trailing newline for cleaner output */
        if (ptr > bitmap_str && *(ptr - 1) == '\n') {
            ptr--;
        }
        *ptr = '\0';

        cJSON_AddStringToObject(response, "bitmap", bitmap_str);
        lib_free(bitmap_str);
    }

    log_message(mcp_tools_log, "Sprite %d inspect complete: %s mode, data at $%04X",
                sprite_number, is_multicolor ? "multicolor" : "hires",
                (unsigned int)data_addr);

    return response;
}
