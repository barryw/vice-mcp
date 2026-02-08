/*
 * mcp_tools_machine.c - MCP machine configuration tool handlers
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
#include "monitor.h"
#include "resources.h"
#include "version.h"
#include "vsync.h"

#include "mcp_tools_validation.h"

/* Forward declarations for public handler functions */
cJSON *mcp_tool_machine_config_get(cJSON *params);
cJSON *mcp_tool_machine_config_set(cJSON *params);

/* ========================================================================= */
/* Resource whitelist types                                                   */
/* ========================================================================= */

typedef struct {
    const char *name;
    const char *description;
    int needs_reset;  /* 1 = requires power-cycle after change */
} mcp_resource_info_t;

/* ========================================================================= */
/* Per-machine resource whitelists                                           */
/* ========================================================================= */

/** @brief Resources common to all machines. */
static const mcp_resource_info_t resources_common[] = {
    { "MachineVideoStandard", "Video standard (1=PAL, 2=NTSC, 3=NTSC-Old, 4=PAL-N)", 1 },
    { "WarpMode", "Warp mode (0=off, 1=on) - disables speed limiting and frame rendering", 0 },
    { "Speed", "CPU speed percentage (1-10000, 0=unlimited)", 0 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to VIC-20. */
static const mcp_resource_info_t resources_vic20[] = {
    { "RAMBlock0", "3K expansion at $0400-$0FFF (0=off, 1=on)", 1 },
    { "RAMBlock1", "8K expansion at $2000-$3FFF (0=off, 1=on)", 1 },
    { "RAMBlock2", "8K expansion at $4000-$5FFF (0=off, 1=on)", 1 },
    { "RAMBlock3", "8K expansion at $6000-$7FFF (0=off, 1=on)", 1 },
    { "RAMBlock5", "8K expansion at $A000-$BFFF (0=off, 1=on)", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to C64/C64SC/SCPU64. */
static const mcp_resource_info_t resources_c64[] = {
    { "SidModel", "SID chip model (0=6581, 1=8580, ...)", 1 },
    { "CIA1Model", "CIA1 model (0=6526 old, 1=6526A new)", 1 },
    { "CIA2Model", "CIA2 model (0=6526 old, 1=6526A new)", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to C128. */
static const mcp_resource_info_t resources_c128[] = {
    { "SidModel", "SID chip model (0=6581, 1=8580, ...)", 1 },
    { "CIA1Model", "CIA1 model (0=6526 old, 1=6526A new)", 1 },
    { "CIA2Model", "CIA2 model (0=6526 old, 1=6526A new)", 1 },
    { "VDCRevision", "VDC revision (0-2)", 1 },
    { "VDC64KB", "VDC has 64KB VRAM (0=16KB, 1=64KB)", 1 },
    { "C128FullBanks", "Full RAM banking (0=off, 1=on)", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to C64DTV. */
static const mcp_resource_info_t resources_c64dtv[] = {
    { "SidModel", "SID chip model", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to Plus/4. */
static const mcp_resource_info_t resources_plus4[] = {
    { "RamSize", "RAM size in KB (16, 32, or 64)", 1 },
    { "Acia1Enable", "Enable ACIA at $FD00 (0=off, 1=on)", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to PET. */
static const mcp_resource_info_t resources_pet[] = {
    { "RamSize", "RAM size in KB (4, 8, 16, 32, 96, 128)", 1 },
    { "IOSize", "I/O size (256 or 2048)", 1 },
    { "Crtc", "CRTC chip present (0=no, 1=yes)", 1 },
    { "VideoSize", "Video RAM size (0=auto, 1=40col, 2=80col)", 1 },
    { "SuperPET", "SuperPET mode (0=off, 1=on)", 1 },
    { NULL, NULL, 0 }
};

/** @brief Resources specific to CBM-II (CBM5x0 and CBM6x0). */
static const mcp_resource_info_t resources_cbm2[] = {
    { "RamSize", "RAM size in KB (64, 128, 256, 1024)", 1 },
    { "ModelLine", "Model line (0=7x0, 1=6x0, 2=5x0)", 1 },
    { "SidModel", "SID chip model", 1 },
    { NULL, NULL, 0 }
};

/* VSID has no machine-specific settable resources */

/* ========================================================================= */
/* Helper: get machine-specific resource list                                */
/* ========================================================================= */

/** @brief Return the machine-specific resource info list for the current machine.
 *  @return Pointer to sentinel-terminated array, or NULL if none. */
static const mcp_resource_info_t *get_machine_resources(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_SCPU64:
            return resources_c64;
        case VICE_MACHINE_C128:
            return resources_c128;
        case VICE_MACHINE_C64DTV:
            return resources_c64dtv;
        case VICE_MACHINE_VIC20:
            return resources_vic20;
        case VICE_MACHINE_PLUS4:
            return resources_plus4;
        case VICE_MACHINE_PET:
            return resources_pet;
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
            return resources_cbm2;
        default:
            return NULL;
    }
}

/** @brief Check if a resource name is in the whitelist for the current machine.
 *  @param name          Resource name to check.
 *  @param needs_reset   If non-NULL, set to 1 if resource requires power-cycle, 0 otherwise.
 *  @return 1 if valid, 0 if not. */
static int is_resource_whitelisted(const char *name, int *needs_reset)
{
    const mcp_resource_info_t *machine_res;
    int i;

    /* Check common resources */
    for (i = 0; resources_common[i].name != NULL; i++) {
        if (strcmp(name, resources_common[i].name) == 0) {
            if (needs_reset != NULL) {
                *needs_reset = resources_common[i].needs_reset;
            }
            return 1;
        }
    }

    /* Check machine-specific resources */
    machine_res = get_machine_resources();
    if (machine_res != NULL) {
        for (i = 0; machine_res[i].name != NULL; i++) {
            if (strcmp(name, machine_res[i].name) == 0) {
                if (needs_reset != NULL) {
                    *needs_reset = machine_res[i].needs_reset;
                }
                return 1;
            }
        }
    }

    return 0;
}

/** @brief Build a comma-separated list of valid resource names for the current machine.
 *  @param buf       Output buffer.
 *  @param buf_size  Size of output buffer. */
static void build_valid_resources_list(char *buf, size_t buf_size)
{
    const mcp_resource_info_t *machine_res;
    size_t pos = 0;
    int first = 1;
    int i;

    buf[0] = '\0';

    /* Common resources */
    for (i = 0; resources_common[i].name != NULL; i++) {
        if (!first) {
            pos += (size_t)snprintf(buf + pos, buf_size - pos, ", ");
        }
        pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s", resources_common[i].name);
        first = 0;
        if (pos >= buf_size - 1) {
            return;
        }
    }

    /* Machine-specific resources */
    machine_res = get_machine_resources();
    if (machine_res != NULL) {
        for (i = 0; machine_res[i].name != NULL; i++) {
            if (!first) {
                pos += (size_t)snprintf(buf + pos, buf_size - pos, ", ");
            }
            pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s", machine_res[i].name);
            first = 0;
            if (pos >= buf_size - 1) {
                return;
            }
        }
    }
}

/* ========================================================================= */
/* Helper: build video standard string                                       */
/* ========================================================================= */

/** @brief Read MachineVideoStandard resource and return a human-readable string.
 *  @return Static string "PAL", "NTSC", etc. */
static const char *build_video_standard(void)
{
    int video = 0;

    resources_get_int("MachineVideoStandard", &video);
    switch (video) {
        case 1:  return "PAL";
        case 2:  return "NTSC";
        case 3:  return "NTSC-Old";
        case 4:  return "PAL-N";
        default: return "unknown";
    }
}

/* ========================================================================= */
/* Helper: build memory info object                                          */
/* ========================================================================= */

/** @brief Build memory information JSON object for the current machine.
 *  @return cJSON object (caller owns), or NULL on OOM. */
static cJSON *build_memory_info(void)
{
    cJSON *memory;

    memory = cJSON_CreateObject();
    if (memory == NULL) {
        return NULL;
    }

    switch (machine_class) {
        case VICE_MACHINE_VIC20:
        {
            cJSON *ram_blocks;
            int b0 = 0, b1 = 0, b2 = 0, b3 = 0, b5 = 0;
            int total_kb;

            resources_get_int("RAMBlock0", &b0);
            resources_get_int("RAMBlock1", &b1);
            resources_get_int("RAMBlock2", &b2);
            resources_get_int("RAMBlock3", &b3);
            resources_get_int("RAMBlock5", &b5);

            total_kb = 5 + (b0 ? 3 : 0) + (b1 ? 8 : 0) + (b2 ? 8 : 0)
                         + (b3 ? 8 : 0) + (b5 ? 8 : 0);

            cJSON_AddNumberToObject(memory, "ram_kb", total_kb);
            cJSON_AddStringToObject(memory, "expansion",
                                    total_kb == 5 ? "unexpanded" : "expanded");

            ram_blocks = cJSON_CreateObject();
            if (ram_blocks != NULL) {
                cJSON_AddBoolToObject(ram_blocks, "block0_3k", b0);
                cJSON_AddBoolToObject(ram_blocks, "block1_8k", b1);
                cJSON_AddBoolToObject(ram_blocks, "block2_8k", b2);
                cJSON_AddBoolToObject(ram_blocks, "block3_8k", b3);
                cJSON_AddBoolToObject(ram_blocks, "block5_8k", b5);
                cJSON_AddItemToObject(memory, "ram_blocks", ram_blocks);
            }
            break;
        }
        case VICE_MACHINE_PLUS4:
        {
            int ram = 64;
            resources_get_int("RamSize", &ram);
            cJSON_AddNumberToObject(memory, "ram_kb", ram);
            cJSON_AddStringToObject(memory, "expansion",
                                    ram <= 16 ? "16K" :
                                    ram <= 32 ? "32K" : "64K");
            break;
        }
        case VICE_MACHINE_PET:
        {
            int ram = 32;
            int superpet = 0;
            resources_get_int("RamSize", &ram);
            resources_get_int("SuperPET", &superpet);
            cJSON_AddNumberToObject(memory, "ram_kb", ram);
            if (superpet) {
                cJSON_AddStringToObject(memory, "expansion", "SuperPET");
            } else if (ram >= 128) {
                cJSON_AddStringToObject(memory, "expansion", "8296");
            } else {
                cJSON_AddStringToObject(memory, "expansion", "standard");
            }
            break;
        }
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
        {
            int ram = 128;
            resources_get_int("RamSize", &ram);
            cJSON_AddNumberToObject(memory, "ram_kb", ram);
            cJSON_AddStringToObject(memory, "expansion", "standard");
            break;
        }
        default:
            /* C64/C128/DTV/SCPU64/VSID: standard 64K */
            cJSON_AddNumberToObject(memory, "ram_kb", 64);
            cJSON_AddStringToObject(memory, "expansion", "standard");
            break;
    }

    return memory;
}

/* ========================================================================= */
/* Helper: build chips array                                                 */
/* ========================================================================= */

/** @brief Add a chip entry to a JSON array.
 *  @param arr       Target array.
 *  @param name      Chip name (e.g. "VIC-II").
 *  @param model     Chip model (e.g. "6567/6569").
 *  @param function  Chip function description.
 *  @param registers Register address range string (e.g. "D000-D03F"). */
static void add_chip(cJSON *arr, const char *name, const char *model,
                     const char *function, const char *registers)
{
    cJSON *chip = cJSON_CreateObject();
    if (chip == NULL) {
        return;
    }
    cJSON_AddStringToObject(chip, "name", name);
    cJSON_AddStringToObject(chip, "model", model);
    cJSON_AddStringToObject(chip, "function", function);
    cJSON_AddStringToObject(chip, "registers", registers);
    cJSON_AddItemToArray(arr, chip);
}

/** @brief Build JSON array of chips present on the current machine.
 *  @return cJSON array (caller owns), or NULL on OOM. */
static cJSON *build_chips_array(void)
{
    cJSON *chips;
    int video = 0;

    chips = cJSON_CreateArray();
    if (chips == NULL) {
        return NULL;
    }

    resources_get_int("MachineVideoStandard", &video);

    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_SCPU64:
            add_chip(chips, "VIC-II",
                     (video == 2 || video == 3) ? "6567" : "6569",
                     "video, sprites",
                     "D000-D3FF");
            add_chip(chips, "SID", "6581", "audio", "D400-D7FF");
            add_chip(chips, "CIA1", "6526", "I/O, keyboard, joystick",
                     "DC00-DCFF");
            add_chip(chips, "CIA2", "6526", "I/O, serial bus, user port",
                     "DD00-DDFF");
            break;

        case VICE_MACHINE_C128:
            add_chip(chips, "VIC-II",
                     (video == 2 || video == 3) ? "8564" : "8566",
                     "video, sprites",
                     "D000-D3FF");
            add_chip(chips, "VDC", "8563", "80-column video",
                     "D600-D601");
            add_chip(chips, "SID", "6581", "audio", "D400-D7FF");
            add_chip(chips, "CIA1", "6526", "I/O, keyboard, joystick",
                     "DC00-DCFF");
            add_chip(chips, "CIA2", "6526", "I/O, serial bus, user port",
                     "DD00-DDFF");
            break;

        case VICE_MACHINE_C64DTV:
            add_chip(chips, "VIC-II", "DTV", "video, sprites, DTV extensions",
                     "D000-D3FF");
            add_chip(chips, "SID", "DTV", "audio (DTV variant)", "D400-D7FF");
            break;

        case VICE_MACHINE_VIC20:
            add_chip(chips, "VIC",
                     (video == 2 || video == 3) ? "6560" : "6561",
                     "video, audio",
                     "9000-900F");
            add_chip(chips, "VIA1", "6522", "I/O, NMI", "9110-911F");
            add_chip(chips, "VIA2", "6522", "I/O, keyboard, joystick",
                     "9120-912F");
            break;

        case VICE_MACHINE_PLUS4:
            add_chip(chips, "TED",
                     (video == 2 || video == 3) ? "7360" : "8360",
                     "video, audio, I/O",
                     "FF00-FF1F");
            break;

        case VICE_MACHINE_PET:
        {
            int crtc_present = 0;
            add_chip(chips, "PIA1", "6520", "keyboard, cassette",
                     "E810-E81F");
            add_chip(chips, "PIA2", "6520", "IEEE-488 bus",
                     "E820-E82F");
            add_chip(chips, "VIA", "6522", "user port, CB2 sound",
                     "E840-E84F");
            resources_get_int("Crtc", &crtc_present);
            if (crtc_present) {
                add_chip(chips, "CRTC", "6545", "video controller",
                         "E880-E881");
            }
            break;
        }
        case VICE_MACHINE_CBM5x0:
            add_chip(chips, "VIC-II",
                     (video == 2 || video == 3) ? "6567" : "6569",
                     "video, sprites",
                     "D000-D3FF");
            add_chip(chips, "SID", "6581", "audio", "D400-D7FF");
            add_chip(chips, "CIA", "6526", "I/O", "D800-D8FF");
            add_chip(chips, "ACIA", "6551", "serial communication",
                     "D900-D9FF");
            break;

        case VICE_MACHINE_CBM6x0:
            add_chip(chips, "CRTC", "6545", "video controller",
                     "D800-D801");
            add_chip(chips, "SID", "6581", "audio", "DA00-DAFF");
            add_chip(chips, "CIA", "6526", "I/O", "DB00-DBFF");
            add_chip(chips, "ACIA", "6551", "serial communication",
                     "DC00-DCFF");
            break;

        case VICE_MACHINE_VSID:
            add_chip(chips, "SID", "6581", "audio", "D400-D7FF");
            break;

        default:
            break;
    }

    return chips;
}

/* ========================================================================= */
/* Helper: build chips_not_present array                                     */
/* ========================================================================= */

/** @brief Add a not-present chip entry to a JSON array.
 *  @param arr         Target array.
 *  @param name        Chip name that is absent.
 *  @param use_instead Alternative chip name (or NULL).
 *  @param note        Additional note (or NULL). */
static void add_absent_chip(cJSON *arr, const char *name,
                            const char *use_instead, const char *note)
{
    cJSON *chip = cJSON_CreateObject();
    if (chip == NULL) {
        return;
    }
    cJSON_AddStringToObject(chip, "name", name);
    if (use_instead != NULL) {
        cJSON_AddStringToObject(chip, "use_instead", use_instead);
    }
    if (note != NULL) {
        cJSON_AddStringToObject(chip, "note", note);
    }
    cJSON_AddItemToArray(arr, chip);
}

/** @brief Build JSON array of chips NOT present on the current machine.
 *  @return cJSON array (caller owns), or NULL on OOM. */
static cJSON *build_chips_not_present(void)
{
    cJSON *absent;

    absent = cJSON_CreateArray();
    if (absent == NULL) {
        return NULL;
    }

    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_SCPU64:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_CBM5x0:
            /* These machines have VIC-II, SID, and CIA -- nothing notable absent */
            break;

        case VICE_MACHINE_C64DTV:
            /* CIAs are DTV-internal -- nothing notable absent */
            break;

        case VICE_MACHINE_VIC20:
            add_absent_chip(absent, "VIC-II", "VIC",
                            "No sprites; VIC has 16 colors, lower resolution");
            add_absent_chip(absent, "SID", "VIC",
                            "VIC has basic square wave audio only");
            add_absent_chip(absent, "CIA", "VIA1/VIA2",
                            "VIAs provide I/O; different timer architecture");
            break;

        case VICE_MACHINE_PLUS4:
            add_absent_chip(absent, "VIC-II", "TED",
                            "No sprites; TED has 121 colors but lower resolution");
            add_absent_chip(absent, "SID", "TED",
                            "TED has 2-voice square wave + noise");
            add_absent_chip(absent, "CIA", "TED",
                            "TED integrates timers and I/O");
            break;

        case VICE_MACHINE_PET:
            add_absent_chip(absent, "VIC-II", NULL,
                            "No bitmap graphics; text-mode only");
            add_absent_chip(absent, "SID", NULL,
                            "No dedicated sound chip; CB2 shift register only");
            add_absent_chip(absent, "CIA", "VIA",
                            "VIA provides user port; different timer architecture");
            break;

        case VICE_MACHINE_CBM6x0:
            add_absent_chip(absent, "VIC-II", "CRTC",
                            "No sprites; CRTC is text/bitmap controller");
            break;

        case VICE_MACHINE_VSID:
            add_absent_chip(absent, "VIC-II", NULL,
                            "VSID is SID-player only; no video");
            add_absent_chip(absent, "CIA", NULL,
                            "VSID does not emulate CIAs");
            break;

        default:
            break;
    }

    return absent;
}

/* ========================================================================= */
/* Helper: build resources object                                            */
/* ========================================================================= */

/** @brief Build JSON object with current values of machine-relevant resources.
 *  @return cJSON object (caller owns), or NULL on OOM. */
static cJSON *build_resources_object(void)
{
    cJSON *res;
    const mcp_resource_info_t *machine_res;
    int val;
    int i;

    res = cJSON_CreateObject();
    if (res == NULL) {
        return NULL;
    }

    /* Common resources */
    for (i = 0; resources_common[i].name != NULL; i++) {
        val = 0;
        if (strcmp(resources_common[i].name, "WarpMode") == 0) {
            /* WarpMode is not a VICE resource; use vsync API */
            val = vsync_get_warp_mode();
            cJSON_AddNumberToObject(res, "WarpMode", val);
        } else if (resources_get_int(resources_common[i].name, &val) == 0) {
            cJSON_AddNumberToObject(res, resources_common[i].name, val);
        }
    }

    /* Machine-specific resources */
    machine_res = get_machine_resources();
    if (machine_res != NULL) {
        for (i = 0; machine_res[i].name != NULL; i++) {
            val = 0;
            if (resources_get_int(machine_res[i].name, &val) == 0) {
                cJSON_AddNumberToObject(res, machine_res[i].name, val);
            }
        }
    }

    return res;
}

/* ========================================================================= */
/* Helper: build memory map JSON array                                       */
/* ========================================================================= */

/** @brief Build JSON array from the memory map for the current machine.
 *  @return cJSON array (caller owns), or NULL on OOM. */
static cJSON *build_memory_map_json(void)
{
    cJSON *map_array;
    mcp_mem_region_t regions[MCP_MAX_MEMORY_REGIONS];
    int count;
    int i;

    map_array = cJSON_CreateArray();
    if (map_array == NULL) {
        return NULL;
    }

    count = mcp_build_memory_map(regions);

    for (i = 0; i < count; i++) {
        cJSON *entry;
        char start_hex[8];
        char end_hex[8];

        entry = cJSON_CreateObject();
        if (entry == NULL) {
            cJSON_Delete(map_array);
            return NULL;
        }

        snprintf(start_hex, sizeof(start_hex), "%04X", regions[i].start);
        snprintf(end_hex, sizeof(end_hex), "%04X", regions[i].end);

        cJSON_AddStringToObject(entry, "start", start_hex);
        cJSON_AddStringToObject(entry, "end", end_hex);
        cJSON_AddStringToObject(entry, "type",
                                mcp_mem_type_to_string(regions[i].type));
        cJSON_AddStringToObject(entry, "name", regions[i].name);
        cJSON_AddItemToArray(map_array, entry);
    }

    return map_array;
}

/* ========================================================================= */
/* Helper: get joystick port count                                           */
/* ========================================================================= */

/** @brief Return the number of native joystick ports on the current machine. */
static int get_joystick_ports(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
        case VICE_MACHINE_CBM5x0:
            return 2;

        case VICE_MACHINE_VIC20:
            return 1;

        case VICE_MACHINE_PET:
        case VICE_MACHINE_PLUS4:
        case VICE_MACHINE_CBM6x0:
            return 2;  /* Via adapter */

        case VICE_MACHINE_VSID:
            return 0;

        default:
            return 0;
    }
}

/* ========================================================================= */
/* Public: machine.config.get handler                                        */
/* ========================================================================= */

cJSON *mcp_tool_machine_config_get(cJSON *params)
{
    cJSON *response;
    cJSON *memory;
    cJSON *chips;
    cJSON *chips_absent;
    cJSON *resources;
    cJSON *memory_map;
    const char *mach_name;

    (void)params;

    log_message(mcp_tools_log, "Handling vice.machine.config.get");

    response = cJSON_CreateObject();
    if (response == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    /* Machine name */
    mach_name = machine_get_name();
    cJSON_AddStringToObject(response, "machine",
                            mach_name ? mach_name : "unknown");

    /* Video standard */
    cJSON_AddStringToObject(response, "video_standard", build_video_standard());

    /* Memory info */
    memory = build_memory_info();
    if (memory != NULL) {
        cJSON_AddItemToObject(response, "memory", memory);
    }

    /* Chips present */
    chips = build_chips_array();
    if (chips != NULL) {
        cJSON_AddItemToObject(response, "chips", chips);
    }

    /* Chips not present */
    chips_absent = build_chips_not_present();
    if (chips_absent != NULL) {
        cJSON_AddItemToObject(response, "chips_not_present", chips_absent);
    }

    /* Current resource values */
    resources = build_resources_object();
    if (resources != NULL) {
        cJSON_AddItemToObject(response, "resources", resources);
    }

    /* Memory map */
    memory_map = build_memory_map_json();
    if (memory_map != NULL) {
        cJSON_AddItemToObject(response, "memory_map", memory_map);
    }

    /* Joystick ports */
    cJSON_AddNumberToObject(response, "joystick_ports", get_joystick_ports());

    /* Keyboard type */
    cJSON_AddStringToObject(response, "keyboard",
                            mach_name ? mach_name : "unknown");

    return response;
}

/* ========================================================================= */
/* Public: machine.config.set handler                                        */
/* ========================================================================= */

cJSON *mcp_tool_machine_config_set(cJSON *params)
{
    cJSON *response;
    cJSON *resources_obj;
    cJSON *changes_array;
    cJSON *child;
    cJSON *new_config;
    int change_count = 0;
    int any_needs_reset = 0;
    const char *mach_name;

    log_message(mcp_tools_log, "Handling vice.machine.config.set");

    /* Validate params */
    if (params == NULL) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
                         "Missing parameters. Expected: { \"resources\": { ... } }");
    }

    resources_obj = cJSON_GetObjectItem(params, "resources");
    if (resources_obj == NULL || !cJSON_IsObject(resources_obj)) {
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
                         "Missing or invalid 'resources' object. "
                         "Expected: { \"resources\": { \"ResourceName\": value, ... } }");
    }

    /* Create changes array to track what we set */
    changes_array = cJSON_CreateArray();
    if (changes_array == NULL) {
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    mach_name = machine_get_name();

    /* Iterate each key-value pair in the resources object */
    child = NULL;
    cJSON_ArrayForEach(child, resources_obj) {
        const char *res_name;
        int old_val = 0;
        int new_val;
        int rc;
        int res_needs_reset = 0;
        cJSON *change_entry;

        res_name = child->string;

        /* 1. Validate resource name is whitelisted */
        if (!is_resource_whitelisted(res_name, &res_needs_reset)) {
            char valid_list[512];
            char err_msg[768];

            build_valid_resources_list(valid_list, sizeof(valid_list));
            snprintf(err_msg, sizeof(err_msg),
                     "Unknown resource '%s' for %s. Valid resources: %s.",
                     res_name,
                     mach_name ? mach_name : "unknown",
                     valid_list);

            cJSON_Delete(changes_array);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_msg);
        }

        /* 2. Validate value is a number */
        if (!cJSON_IsNumber(child)) {
            char err_msg[256];

            snprintf(err_msg, sizeof(err_msg),
                     "Resource value for '%s' must be a number.", res_name);

            cJSON_Delete(changes_array);
            return mcp_error(MCP_ERROR_INVALID_PARAMS, err_msg);
        }

        new_val = child->valueint;

        /* 3. Read old value, 4. Set new value */
        if (strcmp(res_name, "WarpMode") == 0) {
            /* WarpMode is not a VICE resource; use vsync API */
            old_val = vsync_get_warp_mode();
            vsync_set_warp_mode(new_val);
        } else {
            resources_get_int(res_name, &old_val);

            rc = resources_set_int(res_name, new_val);
            if (rc != 0) {
                char err_msg[256];

                snprintf(err_msg, sizeof(err_msg),
                         "Failed to set '%s' to %d. Resource rejected the value.",
                         res_name, new_val);

                cJSON_Delete(changes_array);
                return mcp_error(MCP_ERROR_INVALID_PARAMS, err_msg);
            }
        }

        /* 5. Track change */
        change_entry = cJSON_CreateObject();
        if (change_entry != NULL) {
            cJSON_AddStringToObject(change_entry, "resource", res_name);
            cJSON_AddNumberToObject(change_entry, "old_value", old_val);
            cJSON_AddNumberToObject(change_entry, "new_value", new_val);
            cJSON_AddBoolToObject(change_entry, "needs_reset", res_needs_reset);
            cJSON_AddItemToArray(changes_array, change_entry);
        }
        if (res_needs_reset) {
            any_needs_reset = 1;
        }
        change_count++;
    }

    /* If no changes were requested, return early */
    if (change_count == 0) {
        cJSON_Delete(changes_array);
        return mcp_error(MCP_ERROR_INVALID_PARAMS,
                         "No resources specified. Provide at least one resource to change.");
    }

    /* Only trigger power-cycle reset if any changed resource requires it */
    if (any_needs_reset) {
        log_message(mcp_tools_log,
                    "Config changed (%d resources); triggering power cycle", change_count);
        machine_trigger_reset(MACHINE_RESET_MODE_POWER_CYCLE);

        /* Resume execution so the reset actually happens and the machine boots */
        exit_mon = exit_mon_continue;
    } else {
        log_message(mcp_tools_log,
                    "Config changed (%d resources); no reset needed", change_count);
    }

    /* Build response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        cJSON_Delete(changes_array);
        return mcp_error(MCP_ERROR_INTERNAL_ERROR, "Out of memory");
    }

    cJSON_AddItemToObject(response, "changes_applied", changes_array);
    cJSON_AddBoolToObject(response, "machine_reset", any_needs_reset);

    /* Include full config snapshot after changes */
    new_config = mcp_tool_machine_config_get(NULL);
    if (new_config != NULL) {
        cJSON_AddItemToObject(response, "new_config", new_config);
    }

    return response;
}
