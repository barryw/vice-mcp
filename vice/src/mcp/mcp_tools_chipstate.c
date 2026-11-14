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

/* SID base address: $D400 on the C64 family, $DA00 on CBM-II. */
static uint16_t sid_base(void)
{
    if (machine_class == VICE_MACHINE_CBM5x0 || machine_class == VICE_MACHINE_CBM6x0) {
        return 0xDA00;
    }
    return 0xD400;
}

/* The VIC-II drives only the low nibble of its color registers (the high
 * nibble reads back as 1s); the DTV's color registers are 8 bits wide. */
static int vicii_color_max(void)
{
    return machine_class == VICE_MACHINE_C64DTV ? 0xFF : 0x0F;
}

static int vicii_color(uint8_t value)
{
    return value & vicii_color_max();
}

/* ------------------------------------------------------------------------- */
/* Chip write plan: every requested register write is validated first, then  */
/* all are applied through the I/O bank, so a bad value writes nothing and   */
/* the writes reach the chips whatever $01 maps at $D000.                    */
/* ------------------------------------------------------------------------- */

#define CHIP_PLAN_MAX 256

typedef struct chip_plan_s {
    int io_bank;
    int count;
    int failed;
    uint16_t addr[CHIP_PLAN_MAX];
    uint8_t value[CHIP_PLAN_MAX];
    char error[128];
} chip_plan_t;

static int chip_plan_init(chip_plan_t *plan)
{
    plan->count = 0;
    plan->failed = 0;
    plan->error[0] = '\0';
    plan->io_bank = mem_bank_from_name("io");
    return plan->io_bank >= 0;
}

static void chip_plan_fail(chip_plan_t *plan, const char *what, int max)
{
    if (!plan->failed) {
        snprintf(plan->error, sizeof(plan->error), "%s must be an integer 0-%d", what, max);
        plan->failed = 1;
    }
}

static void chip_plan_add(chip_plan_t *plan, uint16_t addr, int value)
{
    if (plan->failed) {
        return;
    }
    if (plan->count >= CHIP_PLAN_MAX) {
        snprintf(plan->error, sizeof(plan->error), "Too many register writes in one request (max %d)",
                 CHIP_PLAN_MAX);
        plan->failed = 1;
        return;
    }
    plan->addr[plan->count] = addr;
    plan->value[plan->count] = (uint8_t)value;
    plan->count++;
}

/* Value a register will have: its last pending write, else the chip's. */
static uint8_t chip_plan_peek(const chip_plan_t *plan, uint16_t addr)
{
    int i;

    for (i = plan->count - 1; i >= 0; i--) {
        if (plan->addr[i] == addr) {
            return plan->value[i];
        }
    }
    return mem_bank_peek(plan->io_bank, addr, NULL);
}

/* Optional integer field 0..max. Returns 1 and sets *out when present and
 * valid, 0 when absent; a present but invalid value fails the plan. */
static int chip_plan_number(chip_plan_t *plan, cJSON *obj, const char *key, int max, int *out)
{
    cJSON *item = (obj != NULL) ? cJSON_GetObjectItem(obj, key) : NULL;

    if (item == NULL || plan->failed) {
        return 0;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint
        || item->valueint < 0 || item->valueint > max) {
        chip_plan_fail(plan, key, max);
        return 0;
    }
    *out = item->valueint;
    return 1;
}

static void chip_plan_byte(chip_plan_t *plan, cJSON *obj, const char *key, uint16_t addr, int max)
{
    int value;

    if (chip_plan_number(plan, obj, key, max, &value)) {
        chip_plan_add(plan, addr, value);
    }
}

/* Optional boolean field that sets (or, with invert, clears) one bit. */
static void chip_plan_bit(chip_plan_t *plan, cJSON *obj, const char *key,
                          uint16_t addr, int bit, int invert)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    uint8_t reg;

    if (item == NULL || plan->failed) {
        return;
    }
    if (!cJSON_IsBool(item)) {
        snprintf(plan->error, sizeof(plan->error), "%s must be a boolean", key);
        plan->failed = 1;
        return;
    }
    reg = chip_plan_peek(plan, addr);
    if (cJSON_IsTrue(item) != invert) {
        reg |= (uint8_t)(1 << bit);
    } else {
        reg &= (uint8_t)~(1 << bit);
    }
    chip_plan_add(plan, addr, reg);
}

/* Optional array of {offset, value} entries written at base + offset. */
static void chip_plan_registers(chip_plan_t *plan, cJSON *params, const char *key,
                                uint16_t base, int max_offset)
{
    cJSON *array = (params != NULL) ? cJSON_GetObjectItem(params, key) : NULL;
    cJSON *entry;

    if (array == NULL || plan->failed) {
        return;
    }
    if (!cJSON_IsArray(array)) {
        snprintf(plan->error, sizeof(plan->error), "%s must be an array of {offset, value}", key);
        plan->failed = 1;
        return;
    }
    cJSON_ArrayForEach(entry, array) {
        int offset;
        int value;

        if (!cJSON_IsObject(entry)
            || !chip_plan_number(plan, entry, "offset", max_offset, &offset)
            || !chip_plan_number(plan, entry, "value", 0xFF, &value)) {
            if (!plan->failed) {
                snprintf(plan->error, sizeof(plan->error),
                         "%s entries need offset 0-%d and value 0-255", key, max_offset);
                plan->failed = 1;
            }
            return;
        }
        chip_plan_add(plan, (uint16_t)(base + offset), value);
    }
}

/* Optional nested object (e.g. "voice0", "cia1"); NULL when absent. */
static cJSON *chip_plan_object(chip_plan_t *plan, cJSON *params, const char *key)
{
    cJSON *item = (params != NULL) ? cJSON_GetObjectItem(params, key) : NULL;

    if (item != NULL && !cJSON_IsObject(item) && !plan->failed) {
        snprintf(plan->error, sizeof(plan->error), "%s must be an object", key);
        plan->failed = 1;
        return NULL;
    }
    return item;
}

/* Apply the plan. Returns an error response, or NULL after writing. */
static cJSON *chip_plan_commit(const chip_plan_t *plan)
{
    int i;

    if (plan->failed) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS, plan->error);
    }
    for (i = 0; i < plan->count; i++) {
        mem_bank_write(plan->io_bank, plan->addr[i], plan->value[i], NULL);
    }
    return NULL;
}

static cJSON *chip_plan_response(const chip_plan_t *plan)
{
    cJSON *response = chip_plan_commit(plan);

    if (response != NULL) {
        return response;
    }
    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }
    cJSON_AddStringToObject(response, "status", "ok");
    cJSON_AddNumberToObject(response, "registers_updated", plan->count);
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
    int io_bank;
    int sprite_num = -1;  /* -1 = all sprites */
    int i;
    int start;
    int end;

    log_message(mcp_tools_log, "Handling vice.sprite.get");

    /* Validate machine has VIC-II (sprites only available on C64/C128/DTV) */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "Sprites not available on this machine");
    }

    io_bank = mem_bank_from_name("io");
    if (io_bank < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
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
        uint8_t enable_reg = mem_bank_peek(io_bank, VICII_SPRITE_ENABLE, NULL);
        uint8_t x_msb_reg = mem_bank_peek(io_bank, VICII_SPRITE_X_MSB, NULL);
        uint8_t multicolor_reg = mem_bank_peek(io_bank, VICII_SPRITE_MULTICOLOR, NULL);
        uint8_t expand_x_reg = mem_bank_peek(io_bank, VICII_SPRITE_EXPAND_X, NULL);
        uint8_t expand_y_reg = mem_bank_peek(io_bank, VICII_SPRITE_EXPAND_Y, NULL);
        uint8_t priority_reg = mem_bank_peek(io_bank, VICII_SPRITE_PRIORITY, NULL);

        for (i = start; i <= end; i++) {
            uint16_t x;
            uint16_t y;
            uint8_t color;
            char sprite_key[16];

            /* Read per-sprite VIC-II registers */
            x = mem_bank_peek(io_bank, VICII_BASE + (i * 2), NULL);  /* $D000, $D002, $D004, ... */
            y = mem_bank_peek(io_bank, VICII_BASE + (i * 2) + 1, NULL);  /* $D001, $D003, $D005, ... */
            color = mem_bank_peek(io_bank, VICII_SPRITE_COLOR_BASE + i, NULL);  /* $D027-$D02E */

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
            cJSON_AddNumberToObject(sprite_obj, "color", vicii_color(color));

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
    cJSON *response, *sprite_item;
    chip_plan_t plan;
    int sprite_num;
    int value;

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

    if (!chip_plan_init(&plan)) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }

    if (chip_plan_number(&plan, params, "x", 511, &value)) {
        uint8_t x_msb = chip_plan_peek(&plan, VICII_SPRITE_X_MSB);

        chip_plan_add(&plan, VICII_BASE + (sprite_num * 2), value & 0xFF);
        if (value & 0x100) {
            x_msb |= (uint8_t)(1 << sprite_num);
        } else {
            x_msb &= (uint8_t)~(1 << sprite_num);
        }
        chip_plan_add(&plan, VICII_SPRITE_X_MSB, x_msb);
    }
    chip_plan_byte(&plan, params, "y", VICII_BASE + (sprite_num * 2) + 1, 255);
    chip_plan_bit(&plan, params, "enabled", VICII_SPRITE_ENABLE, sprite_num, 0);
    chip_plan_bit(&plan, params, "multicolor", VICII_SPRITE_MULTICOLOR, sprite_num, 0);
    chip_plan_bit(&plan, params, "expand_x", VICII_SPRITE_EXPAND_X, sprite_num, 0);
    chip_plan_bit(&plan, params, "expand_y", VICII_SPRITE_EXPAND_Y, sprite_num, 0);
    /* $D01B bit set = behind the background, so foreground clears it */
    chip_plan_bit(&plan, params, "priority_foreground", VICII_SPRITE_PRIORITY, sprite_num, 1);
    chip_plan_byte(&plan, params, "color", VICII_SPRITE_COLOR_BASE + sprite_num, vicii_color_max());

    response = chip_plan_commit(&plan);
    if (response != NULL) {
        return response;
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

#define CIA1_BASE 0xDC00
#define CIA2_BASE 0xDD00

cJSON* mcp_tool_vicii_get_state(cJSON *params)
{
    cJSON *response, *registers_obj;
    int io_bank;
    int i;
    uint8_t d011;
    uint8_t d012;

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.vicii.get_state");

    /* Validate machine has VIC-II */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "VIC-II not available on this machine");
    }

    io_bank = mem_bank_from_name("io");
    if (io_bank < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Read key VIC-II registers using peek to avoid side effects */
    d011 = mem_bank_peek(io_bank, VICII_BASE + 0x11, NULL);  /* Control register 1 */
    d012 = mem_bank_peek(io_bank, VICII_BASE + 0x12, NULL);  /* Raster line */

    /* Add computed/useful state */
    cJSON_AddNumberToObject(response, "raster_line", d012 | ((d011 & 0x80) << 1));  /* 9-bit raster */
    cJSON_AddNumberToObject(response, "video_mode", ((d011 & 0x60) | (mem_bank_peek(io_bank, VICII_BASE + 0x16, NULL) & 0x10)) >> 4);
    cJSON_AddBoolToObject(response, "screen_enabled", (d011 & 0x10) != 0);
    cJSON_AddBoolToObject(response, "25_rows", (d011 & 0x08) != 0);
    cJSON_AddNumberToObject(response, "y_scroll", d011 & 0x07);
    cJSON_AddNumberToObject(response, "x_scroll", mem_bank_peek(io_bank, VICII_BASE + 0x16, NULL) & 0x07);

    /* Border colors */
    cJSON_AddNumberToObject(response, "border_color", vicii_color(mem_bank_peek(io_bank, VICII_BASE + 0x20, NULL)));
    cJSON_AddNumberToObject(response, "background_color_0", vicii_color(mem_bank_peek(io_bank, VICII_BASE + 0x21, NULL)));
    cJSON_AddNumberToObject(response, "background_color_1", vicii_color(mem_bank_peek(io_bank, VICII_BASE + 0x22, NULL)));
    cJSON_AddNumberToObject(response, "background_color_2", vicii_color(mem_bank_peek(io_bank, VICII_BASE + 0x23, NULL)));
    cJSON_AddNumberToObject(response, "background_color_3", vicii_color(mem_bank_peek(io_bank, VICII_BASE + 0x24, NULL)));

    /* Sprite collisions - peek to avoid clearing on read */
    cJSON_AddNumberToObject(response, "sprite_sprite_collision", mem_bank_peek(io_bank, VICII_BASE + 0x1E, NULL));
    cJSON_AddNumberToObject(response, "sprite_background_collision", mem_bank_peek(io_bank, VICII_BASE + 0x1F, NULL));

    /* IRQ status - peek to avoid clearing on read */
    cJSON_AddNumberToObject(response, "irq_status", mem_bank_peek(io_bank, VICII_BASE + 0x19, NULL));
    cJSON_AddNumberToObject(response, "irq_enabled", mem_bank_peek(io_bank, VICII_BASE + 0x1A, NULL));

    /* Memory pointers */
    cJSON_AddNumberToObject(response, "memory_pointers", mem_bank_peek(io_bank, VICII_BASE + 0x18, NULL));

    /* Add all registers as array for completeness */
    registers_obj = cJSON_CreateArray();
    if (registers_obj == NULL) {
        cJSON_Delete(response);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    for (i = 0; i < 0x2F; i++) {
        cJSON_AddItemToArray(registers_obj, cJSON_CreateNumber(mem_bank_peek(io_bank, VICII_BASE + i, NULL)));
    }

    cJSON_AddItemToObject(response, "registers", registers_obj);

    return response;
}

cJSON* mcp_tool_sid_get_state(cJSON *params)
{
    cJSON *response, *voices_array, *voice_obj;
    int io_bank;
    int v;
    uint16_t base_addr = sid_base();

    (void)params;  /* Unused */

    log_message(mcp_tools_log, "Handling vice.sid.get_state");

    /* Validate machine has SID */
    if (!machine_has_sid()) {
        return mcp_error(-32000, "SID not available on this machine");
    }

    io_bank = mem_bank_from_name("io");
    if (io_bank < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
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
        int base = base_addr + (v * 7);

        freq = mem_bank_peek(io_bank, base + 0, NULL) | (mem_bank_peek(io_bank, base + 1, NULL) << 8);
        pulse_width = mem_bank_peek(io_bank, base + 2, NULL) | (mem_bank_peek(io_bank, base + 3, NULL) << 8);
        control = mem_bank_peek(io_bank, base + 4, NULL);
        attack_decay = mem_bank_peek(io_bank, base + 5, NULL);
        sustain_release = mem_bank_peek(io_bank, base + 6, NULL);

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
        uint8_t reg17 = mem_bank_peek(io_bank, base_addr + 0x17, NULL);
        uint8_t reg18 = mem_bank_peek(io_bank, base_addr + 0x18, NULL);

        cJSON_AddNumberToObject(response, "filter_cutoff_low", mem_bank_peek(io_bank, base_addr + 0x15, NULL));
        cJSON_AddNumberToObject(response, "filter_cutoff_high", mem_bank_peek(io_bank, base_addr + 0x16, NULL));
        cJSON_AddNumberToObject(response, "filter_resonance", (reg17 >> 4) & 0x0F);
        cJSON_AddBoolToObject(response, "filter_voice3", (reg17 & 0x04) != 0);
        cJSON_AddBoolToObject(response, "filter_voice2", (reg17 & 0x02) != 0);
        cJSON_AddBoolToObject(response, "filter_voice1", (reg17 & 0x01) != 0);
        cJSON_AddBoolToObject(response, "filter_ext", (reg17 & 0x08) != 0);
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
    int io_bank;
    int cia;

    log_message(mcp_tools_log, "Handling vice.cia.get_state");

    /* Validate machine has CIA chips */
    if (!machine_has_cia()) {
        return mcp_error(-32000, "CIA not available on this machine");
    }

    io_bank = mem_bank_from_name("io");
    if (io_bank < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
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
        cJSON_AddNumberToObject(obj, "port_a", mem_bank_peek(io_bank, base + 0, NULL)); \
        cJSON_AddNumberToObject(obj, "port_b", mem_bank_peek(io_bank, base + 1, NULL)); \
        cJSON_AddNumberToObject(obj, "ddr_a", mem_bank_peek(io_bank, base + 2, NULL)); \
        cJSON_AddNumberToObject(obj, "ddr_b", mem_bank_peek(io_bank, base + 3, NULL)); \
        timer_a = mem_bank_peek(io_bank, base + 4, NULL) | (mem_bank_peek(io_bank, base + 5, NULL) << 8); \
        timer_b = mem_bank_peek(io_bank, base + 6, NULL) | (mem_bank_peek(io_bank, base + 7, NULL) << 8); \
        cJSON_AddNumberToObject(obj, "timer_a", timer_a); \
        cJSON_AddNumberToObject(obj, "timer_b", timer_b); \
        cJSON_AddNumberToObject(obj, "tod_10ths", mem_bank_peek(io_bank, base + 8, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_seconds", mem_bank_peek(io_bank, base + 9, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_minutes", mem_bank_peek(io_bank, base + 10, NULL)); \
        cJSON_AddNumberToObject(obj, "tod_hours", mem_bank_peek(io_bank, base + 11, NULL)); \
        cJSON_AddNumberToObject(obj, "serial_data", mem_bank_peek(io_bank, base + 12, NULL)); \
        cJSON_AddNumberToObject(obj, "interrupt_control", mem_bank_peek(io_bank, base + 13, NULL)); \
        cJSON_AddNumberToObject(obj, "control_a", mem_bank_peek(io_bank, base + 14, NULL)); \
        cJSON_AddNumberToObject(obj, "control_b", mem_bank_peek(io_bank, base + 15, NULL)); \
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
    chip_plan_t plan;
    int line;
    int color_max = vicii_color_max();

    log_message(mcp_tools_log, "Handling vice.vicii.set_state");

    /* Validate machine has VIC-II */
    if (!machine_has_vicii()) {
        return mcp_error(-32000, "VIC-II not available on this machine");
    }
    if (!chip_plan_init(&plan)) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }

    /* All parameters optional - set only what's provided */
    chip_plan_registers(&plan, params, "registers", VICII_BASE, 0x2E);
    chip_plan_byte(&plan, params, "border_color", VICII_BASE + 0x20, color_max);
    chip_plan_byte(&plan, params, "background_color", VICII_BASE + 0x21, color_max);
    chip_plan_byte(&plan, params, "background_color_1", VICII_BASE + 0x22, color_max);
    chip_plan_byte(&plan, params, "background_color_2", VICII_BASE + 0x23, color_max);
    chip_plan_byte(&plan, params, "background_color_3", VICII_BASE + 0x24, color_max);
    chip_plan_byte(&plan, params, "control_1", VICII_BASE + 0x11, 0xFF);
    chip_plan_byte(&plan, params, "control_2", VICII_BASE + 0x16, 0xFF);
    chip_plan_byte(&plan, params, "memory_pointers", VICII_BASE + 0x18, 0xFF);

    /* IRQ raster line: low 8 bits in $D012, bit 8 in $D011 bit 7 */
    if (chip_plan_number(&plan, params, "irq_raster_line", 0x1FF, &line)) {
        uint8_t d011 = chip_plan_peek(&plan, VICII_BASE + 0x11);

        chip_plan_add(&plan, VICII_BASE + 0x12, line & 0xFF);
        chip_plan_add(&plan, VICII_BASE + 0x11, (line & 0x100) ? (d011 | 0x80) : (d011 & 0x7F));
    }

    chip_plan_byte(&plan, params, "interrupt_enable", VICII_BASE + 0x1A, 0x0F);

    return chip_plan_response(&plan);
}

cJSON* mcp_tool_sid_set_state(cJSON *params)
{
    chip_plan_t plan;
    uint16_t base_addr = sid_base();
    int value;
    int v;

    log_message(mcp_tools_log, "Handling vice.sid.set_state");

    /* Validate machine has SID */
    if (!machine_has_sid()) {
        return mcp_error(-32000, "SID not available on this machine");
    }
    if (!chip_plan_init(&plan)) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }

    chip_plan_registers(&plan, params, "registers", base_addr, 0x1C);

    /* Voice parameters (voices 0-2) */
    for (v = 0; v < 3; v++) {
        char voice_key[10];
        cJSON *voice;
        uint16_t base = (uint16_t)(base_addr + (v * 7));

        snprintf(voice_key, sizeof(voice_key), "voice%d", v);
        voice = chip_plan_object(&plan, params, voice_key);
        if (voice == NULL) {
            continue;
        }
        if (chip_plan_number(&plan, voice, "frequency", 0xFFFF, &value)) {
            chip_plan_add(&plan, base + 0, value & 0xFF);
            chip_plan_add(&plan, base + 1, value >> 8);
        }
        if (chip_plan_number(&plan, voice, "pulse_width", 0x0FFF, &value)) {
            chip_plan_add(&plan, base + 2, value & 0xFF);
            chip_plan_add(&plan, base + 3, value >> 8);
        }
        chip_plan_byte(&plan, voice, "control", base + 4, 0xFF);
        chip_plan_byte(&plan, voice, "attack_decay", base + 5, 0xFF);
        chip_plan_byte(&plan, voice, "sustain_release", base + 6, 0xFF);
    }

    /* Filter cutoff (11-bit): low 3 bits in $15, high 8 bits in $16 */
    if (chip_plan_number(&plan, params, "filter_cutoff", 0x07FF, &value)) {
        chip_plan_add(&plan, base_addr + 0x15, value & 0x07);
        chip_plan_add(&plan, base_addr + 0x16, value >> 3);
    }
    chip_plan_byte(&plan, params, "filter_resonance", base_addr + 0x17, 0xFF);
    chip_plan_byte(&plan, params, "filter_mode_volume", base_addr + 0x18, 0xFF);

    return chip_plan_response(&plan);
}

cJSON* mcp_tool_cia_set_state(cJSON *params)
{
    static const struct {
        const char *key;
        int offset;
    } cia_fields[] = {
        { "port_a", 0 }, { "port_b", 1 }, { "ddr_a", 2 }, { "ddr_b", 3 },
        { "timer_a_low", 4 }, { "timer_a_high", 5 },
        { "timer_b_low", 6 }, { "timer_b_high", 7 },
        { "interrupt_control", 13 }, { "control_a", 14 }, { "control_b", 15 },
        { NULL, 0 }
    };
    static const struct {
        const char *object;
        const char *registers;
        uint16_t base;
    } cias[] = {
        { "cia1", "cia1_registers", CIA1_BASE },
        { "cia2", "cia2_registers", CIA2_BASE },
    };
    chip_plan_t plan;
    int c;
    int f;

    log_message(mcp_tools_log, "Handling vice.cia.set_state");

    /* Validate machine has CIA chips */
    if (!machine_has_cia()) {
        return mcp_error(-32000, "CIA not available on this machine");
    }
    if (!chip_plan_init(&plan)) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }

    for (c = 0; c < 2; c++) {
        cJSON *named;

        chip_plan_registers(&plan, params, cias[c].registers, cias[c].base, 0x0F);
        named = chip_plan_object(&plan, params, cias[c].object);
        for (f = 0; named != NULL && cia_fields[f].key != NULL; f++) {
            chip_plan_byte(&plan, named, cia_fields[f].key,
                           (uint16_t)(cias[c].base + cia_fields[f].offset), 0xFF);
        }
    }

    return chip_plan_response(&plan);
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

/* What the VIC-II sees at addr: RAM, except that on the C64 family the
 * character ROM appears at $1000-$1FFF of VIC banks 0 and 2.
 * ponytail: C128/DTV read plain RAM; their char-ROM mapping is not modelled. */
static uint8_t vic_peek(int ram_bank, int rom_bank, uint16_t addr)
{
    if (rom_bank >= 0 && (addr & 0x7000) == 0x1000) {
        return mem_bank_peek(rom_bank, (uint16_t)(0xD000 | (addr & 0x0FFF)), NULL);
    }
    return mem_bank_peek(ram_bank, addr, NULL);
}

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
    int io_bank;
    int ram_bank;
    int rom_bank = -1;

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
        if (strcmp(format_str, "png_base64") == 0) {
            return mcp_error(MCP_ERROR_NOT_IMPLEMENTED,
                "format 'png_base64' is not implemented; use 'ascii' or 'binary'");
        }
        if (strcmp(format_str, "ascii") != 0 &&
            strcmp(format_str, "binary") != 0) {
            return mcp_error(MCP_ERROR_INVALID_PARAMS,
                "format must be 'ascii' or 'binary'");
        }
    }

    io_bank = mem_bank_from_name("io");
    if (io_bank < 0) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "I/O bank unavailable");
    }
    ram_bank = mem_bank_from_name("ram");
    if (ram_bank < 0) {
        ram_bank = 0;
    }
    if (machine_class == VICE_MACHINE_C64 || machine_class == VICE_MACHINE_C64SC
        || machine_class == VICE_MACHINE_SCPU64) {
        rom_bank = mem_bank_from_name("rom");
    }

    /* Calculate sprite pointer address from current VIC bank and screen base.
     * VIC bank is determined by CIA2 port A ($DD00) bits 0-1,
     * screen base by VIC-II register $D018 bits 4-7. */
    {
        uint8_t cia2_port_a = mem_bank_peek(io_bank, 0xDD00, NULL);
        uint16_t vic_bank = (uint16_t)((3 - (cia2_port_a & 0x03)) * 0x4000);
        uint8_t d018 = mem_bank_peek(io_bank, VICII_BASE + 0x18, NULL);
        uint16_t screen_base = vic_bank + (uint16_t)((d018 >> 4) * 0x0400);
        pointer_addr = screen_base + 0x03F8 + sprite_number;
        pointer_value = vic_peek(ram_bank, rom_bank, pointer_addr);
        data_addr = vic_bank + (uint16_t)(pointer_value * 64);
    }

    /* Read multicolor register */
    multicolor_reg = mem_bank_peek(io_bank, VICII_SPRITE_MULTICOLOR, NULL);
    is_multicolor = (multicolor_reg & (1 << sprite_number)) != 0;

    /* Read sprite colors */
    sprite_color = (uint8_t)vicii_color(mem_bank_peek(io_bank, VICII_SPRITE_COLOR_BASE + sprite_number, NULL));
    multi1_color = (uint8_t)vicii_color(mem_bank_peek(io_bank, VICII_SPRITE_MULTI1, NULL));
    multi2_color = (uint8_t)vicii_color(mem_bank_peek(io_bank, VICII_SPRITE_MULTI2, NULL));

    /* Read sprite data (63 bytes) as the VIC-II sees it */
    for (i = 0; i < SPRITE_BYTES; i++) {
        sprite_data[i] = vic_peek(ram_bank, rom_bank, (uint16_t)(data_addr + i));
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
    {
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
