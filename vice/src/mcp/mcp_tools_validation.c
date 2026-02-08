/*
 * mcp_tools_validation.c - Address validation and memory map for MCP tools
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

#include "mcp_tools_validation.h"
#include "machine.h"
#include "resources.h"

/* ========================================================================= */
/* Machine capability queries                                                */
/* ========================================================================= */

int mcp_machine_has_vicii(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
        case VICE_MACHINE_CBM5x0:
            return 1;
        default:
            return 0;
    }
}

int mcp_machine_has_sid(void)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_C128:
        case VICE_MACHINE_C64DTV:
        case VICE_MACHINE_SCPU64:
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
        case VICE_MACHINE_VSID:
            return 1;
        default:
            return 0;
    }
}

int mcp_machine_has_cia(void)
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

int mcp_machine_has_ted(void)
{
    return machine_class == VICE_MACHINE_PLUS4;
}

int mcp_machine_has_vdc(void)
{
    return machine_class == VICE_MACHINE_C128;
}

int mcp_machine_has_crtc(void)
{
    if (machine_class == VICE_MACHINE_CBM6x0) {
        return 1;
    }
    if (machine_class == VICE_MACHINE_PET) {
        int crtc = 0;
        if (resources_get_int("Crtc", &crtc) == 0 && crtc) {
            return 1;
        }
    }
    return 0;
}

int mcp_machine_has_vic(void)
{
    return machine_class == VICE_MACHINE_VIC20;
}

int mcp_machine_has_via(void)
{
    switch (machine_class) {
        case VICE_MACHINE_VIC20:
        case VICE_MACHINE_PET:
            return 1;
        default:
            return 0;
    }
}

int mcp_machine_has_pia(void)
{
    return machine_class == VICE_MACHINE_PET;
}

int mcp_machine_has_acia(void)
{
    switch (machine_class) {
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
            return 1;
        case VICE_MACHINE_PLUS4:
        {
            int acia = 0;
            if (resources_get_int("Acia1Enable", &acia) == 0 && acia) {
                return 1;
            }
            return 0;
        }
        default:
            return 0;
    }
}

/* ========================================================================= */
/* Region type to string                                                     */
/* ========================================================================= */

const char *mcp_mem_type_to_string(mcp_mem_region_type_t type)
{
    switch (type) {
        case MCP_MEM_TYPE_RAM:       return "ram";
        case MCP_MEM_TYPE_ROM:       return "rom";
        case MCP_MEM_TYPE_IO:        return "io";
        case MCP_MEM_TYPE_UNMAPPED:  return "unmapped";
        case MCP_MEM_TYPE_CARTRIDGE: return "cartridge";
        default:                     return "unknown";
    }
}

/* ========================================================================= */
/* Static helper: push a region into the map array                           */
/* ========================================================================= */

/** @brief Add a region entry to the map array.
 *
 *  Checks against MCP_MAX_MEMORY_REGIONS - 1 to leave room for the
 *  sentinel entry (name == NULL).
 *
 *  @param regions  The output array
 *  @param count    Pointer to current count (updated on success)
 *  @param start    Region start address
 *  @param end      Region end address (inclusive)
 *  @param type     Region type enum
 *  @param name     Human-readable name (static string, not freed)
 */
static void add_region(mcp_mem_region_t *regions, int *count,
                       uint16_t start, uint16_t end,
                       mcp_mem_region_type_t type, const char *name)
{
    if (*count >= MCP_MAX_MEMORY_REGIONS - 1) {
        return;  /* No room; leave space for sentinel */
    }
    regions[*count].start = start;
    regions[*count].end   = end;
    regions[*count].type  = type;
    regions[*count].name  = name;
    (*count)++;
}

/* ========================================================================= */
/* Per-machine memory map builders                                           */
/* ========================================================================= */

/** @brief Build standard C64 64K memory map (also used for C64SC/SCPU64). */
static int build_c64_map(mcp_mem_region_t *regions)
{
    int n = 0;

    add_region(regions, &n, 0x0000, 0x00FF, MCP_MEM_TYPE_RAM,  "Zero Page");
    add_region(regions, &n, 0x0100, 0x01FF, MCP_MEM_TYPE_RAM,  "Stack");
    add_region(regions, &n, 0x0200, 0x03FF, MCP_MEM_TYPE_RAM,  "BASIC Work Area");
    add_region(regions, &n, 0x0400, 0x07FF, MCP_MEM_TYPE_RAM,  "Screen RAM");
    add_region(regions, &n, 0x0800, 0x9FFF, MCP_MEM_TYPE_RAM,  "BASIC Program Area");
    add_region(regions, &n, 0xA000, 0xBFFF, MCP_MEM_TYPE_ROM,  "BASIC ROM");
    add_region(regions, &n, 0xC000, 0xCFFF, MCP_MEM_TYPE_RAM,  "Upper RAM");
    add_region(regions, &n, 0xD000, 0xD3FF, MCP_MEM_TYPE_IO,   "VIC-II");
    add_region(regions, &n, 0xD400, 0xD7FF, MCP_MEM_TYPE_IO,   "SID");
    add_region(regions, &n, 0xD800, 0xDBFF, MCP_MEM_TYPE_IO,   "Color RAM");
    add_region(regions, &n, 0xDC00, 0xDCFF, MCP_MEM_TYPE_IO,   "CIA1");
    add_region(regions, &n, 0xDD00, 0xDDFF, MCP_MEM_TYPE_IO,   "CIA2");
    add_region(regions, &n, 0xDE00, 0xDEFF, MCP_MEM_TYPE_IO,   "IO1");
    add_region(regions, &n, 0xDF00, 0xDFFF, MCP_MEM_TYPE_IO,   "IO2");
    add_region(regions, &n, 0xE000, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM");

    /* Sentinel */
    regions[n].name = NULL;
    return n;
}

/** @brief Build C128 memory map (default CPU view is C64-compatible). */
static int build_c128_map(mcp_mem_region_t *regions)
{
    /* C128 has complex banking; the default CPU view matches C64 layout */
    return build_c64_map(regions);
}

/** @brief Build C64DTV memory map. */
static int build_c64dtv_map(mcp_mem_region_t *regions)
{
    /* DTV default view is C64-compatible */
    return build_c64_map(regions);
}

/** @brief Build VIC-20 memory map, querying RAM expansion resources. */
static int build_vic20_map(mcp_mem_region_t *regions)
{
    int n = 0;
    int block0 = 0, block1 = 0, block2 = 0, block3 = 0, block5 = 0;

    resources_get_int("RAMBlock0", &block0);
    resources_get_int("RAMBlock1", &block1);
    resources_get_int("RAMBlock2", &block2);
    resources_get_int("RAMBlock3", &block3);
    resources_get_int("RAMBlock5", &block5);

    /* Base system RAM: always present */
    add_region(regions, &n, 0x0000, 0x03FF, MCP_MEM_TYPE_RAM,  "System RAM");

    /* Block 0: $0400-$0FFF (3K expansion) */
    if (block0) {
        add_region(regions, &n, 0x0400, 0x0FFF, MCP_MEM_TYPE_RAM,  "Block 0 RAM");
    } else {
        add_region(regions, &n, 0x0400, 0x0FFF, MCP_MEM_TYPE_UNMAPPED, "Block 0 (unmapped)");
    }

    /* Base RAM: $1000-$1FFF always present */
    add_region(regions, &n, 0x1000, 0x1FFF, MCP_MEM_TYPE_RAM,  "Base RAM");

    /* Block 1: $2000-$3FFF (8K expansion) */
    if (block1) {
        add_region(regions, &n, 0x2000, 0x3FFF, MCP_MEM_TYPE_RAM,  "Block 1 RAM");
    } else {
        add_region(regions, &n, 0x2000, 0x3FFF, MCP_MEM_TYPE_UNMAPPED, "Block 1 (unmapped)");
    }

    /* Block 2: $4000-$5FFF (8K expansion) */
    if (block2) {
        add_region(regions, &n, 0x4000, 0x5FFF, MCP_MEM_TYPE_RAM,  "Block 2 RAM");
    } else {
        add_region(regions, &n, 0x4000, 0x5FFF, MCP_MEM_TYPE_UNMAPPED, "Block 2 (unmapped)");
    }

    /* Block 3: $6000-$7FFF (8K expansion) */
    if (block3) {
        add_region(regions, &n, 0x6000, 0x7FFF, MCP_MEM_TYPE_RAM,  "Block 3 RAM");
    } else {
        add_region(regions, &n, 0x6000, 0x7FFF, MCP_MEM_TYPE_UNMAPPED, "Block 3 (unmapped)");
    }

    /* Character ROM: $8000-$8FFF */
    add_region(regions, &n, 0x8000, 0x8FFF, MCP_MEM_TYPE_ROM,  "Character ROM");

    /* I/O area: $9000-$97FF (VIC, VIAs) */
    add_region(regions, &n, 0x9000, 0x97FF, MCP_MEM_TYPE_IO,   "VIC/VIA I/O");

    /* I/O expansion: $9800-$9FFF */
    add_region(regions, &n, 0x9800, 0x9FFF, MCP_MEM_TYPE_IO,   "I/O Expansion");

    /* Block 5: $A000-$BFFF (8K expansion) */
    if (block5) {
        add_region(regions, &n, 0xA000, 0xBFFF, MCP_MEM_TYPE_RAM,  "Block 5 RAM");
    } else {
        add_region(regions, &n, 0xA000, 0xBFFF, MCP_MEM_TYPE_UNMAPPED, "Block 5 (unmapped)");
    }

    /* BASIC ROM: $C000-$DFFF */
    add_region(regions, &n, 0xC000, 0xDFFF, MCP_MEM_TYPE_ROM,  "BASIC ROM");

    /* KERNAL ROM: $E000-$FFFF */
    add_region(regions, &n, 0xE000, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM");

    /* Sentinel */
    regions[n].name = NULL;
    return n;
}

/** @brief Build Plus/4 memory map, querying RamSize resource. */
static int build_plus4_map(mcp_mem_region_t *regions)
{
    int n = 0;
    int ram_size = 64;
    uint16_t ram_end;

    resources_get_int("RamSize", &ram_size);

    /* System area: $0000-$03FF */
    add_region(regions, &n, 0x0000, 0x03FF, MCP_MEM_TYPE_RAM,  "System RAM");

    /* Main RAM: $0400 up to RAM limit */
    switch (ram_size) {
        case 16:  ram_end = 0x3FFF; break;
        case 32:  ram_end = 0x7FFF; break;
        default:  ram_end = 0x7FFF; break;  /* 64K: RAM to $7FFF (ROM banked above) */
    }
    add_region(regions, &n, 0x0400, ram_end, MCP_MEM_TYPE_RAM,  "Main RAM");

    /* If less than 32K, mark gap as unmapped */
    if (ram_size == 16) {
        add_region(regions, &n, 0x4000, 0x7FFF, MCP_MEM_TYPE_UNMAPPED, "Unmapped (16K config)");
    }

    /* BASIC ROM: $8000-$BFFF */
    add_region(regions, &n, 0x8000, 0xBFFF, MCP_MEM_TYPE_ROM,  "BASIC ROM");

    /* KERNAL ROM: $C000-$FCFF */
    add_region(regions, &n, 0xC000, 0xFCFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM");

    /* TED I/O: $FD00-$FD3F */
    add_region(regions, &n, 0xFD00, 0xFD3F, MCP_MEM_TYPE_IO,   "TED I/O");

    /* I/O gap: $FD40-$FEFF */
    add_region(regions, &n, 0xFD40, 0xFEFF, MCP_MEM_TYPE_IO,   "I/O Area");

    /* TED registers: $FF00-$FF1F */
    add_region(regions, &n, 0xFF00, 0xFF1F, MCP_MEM_TYPE_IO,   "TED Registers");

    /* KERNAL high: $FF20-$FFFF */
    add_region(regions, &n, 0xFF20, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL High");

    /* Sentinel */
    regions[n].name = NULL;
    return n;
}

/** @brief Build PET memory map, querying RamSize resource. */
static int build_pet_map(mcp_mem_region_t *regions)
{
    int n = 0;
    int ram_size = 32;
    uint16_t ram_end;

    resources_get_int("RamSize", &ram_size);

    /* System RAM: $0000-$03FF */
    add_region(regions, &n, 0x0000, 0x03FF, MCP_MEM_TYPE_RAM,  "System RAM");

    /* Main RAM: $0400 up to RAM limit */
    switch (ram_size) {
        case 4:   ram_end = 0x0FFF; break;
        case 8:   ram_end = 0x1FFF; break;
        case 16:  ram_end = 0x3FFF; break;
        case 32:  ram_end = 0x7FFF; break;
        case 96:  ram_end = 0x7FFF; break;  /* SuperPET: main bank still 32K visible */
        case 128: ram_end = 0x7FFF; break;  /* 8296: bank-switched above 32K */
        default:  ram_end = 0x7FFF; break;
    }
    add_region(regions, &n, 0x0400, ram_end, MCP_MEM_TYPE_RAM,  "Main RAM");

    /* Unmapped gap if less than 32K */
    if (ram_end < 0x7FFF) {
        add_region(regions, &n, (uint16_t)(ram_end + 1), 0x7FFF,
                   MCP_MEM_TYPE_UNMAPPED, "Unmapped RAM area");
    }

    /* Screen RAM: $8000-$8FFF */
    add_region(regions, &n, 0x8000, 0x8FFF, MCP_MEM_TYPE_RAM,  "Screen RAM");

    /* ROM area: $9000-$E7FF (BASIC + extension ROMs) */
    add_region(regions, &n, 0x9000, 0xE7FF, MCP_MEM_TYPE_ROM,  "BASIC/Extension ROM");

    /* I/O: $E800-$E84F (PIA1, PIA2, VIA, CRTC) */
    add_region(regions, &n, 0xE800, 0xE84F, MCP_MEM_TYPE_IO,   "PIA/VIA/CRTC I/O");

    /* I/O mirror/gap: $E850-$EFFF */
    add_region(regions, &n, 0xE850, 0xEFFF, MCP_MEM_TYPE_IO,   "I/O Mirror");

    /* KERNAL ROM: $F000-$FFFF */
    add_region(regions, &n, 0xF000, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM");

    /* Sentinel */
    regions[n].name = NULL;
    return n;
}

/** @brief Build CBM-II (CBM 6x0/7x0 and CBM 5x0) memory map. */
static int build_cbm2_map(mcp_mem_region_t *regions)
{
    int n = 0;
    int ram_size = 128;
    uint16_t ram_end;

    resources_get_int("RamSize", &ram_size);

    /* System RAM: $0000-$00FF */
    add_region(regions, &n, 0x0000, 0x00FF, MCP_MEM_TYPE_RAM,  "Zero Page");

    /* Stack + system: $0100-$03FF */
    add_region(regions, &n, 0x0100, 0x03FF, MCP_MEM_TYPE_RAM,  "Stack/System");

    /* Main RAM */
    switch (ram_size) {
        case 64:   ram_end = 0xCFFF; break;
        case 128:  ram_end = 0xCFFF; break;  /* Bank-switched above */
        case 256:  ram_end = 0xCFFF; break;  /* Bank-switched above */
        case 1024: ram_end = 0xCFFF; break;  /* Bank-switched above */
        default:   ram_end = 0xCFFF; break;
    }
    add_region(regions, &n, 0x0400, ram_end, MCP_MEM_TYPE_RAM,  "Main RAM");

    /* I/O area: $D000-$DBFF */
    add_region(regions, &n, 0xD000, 0xD7FF, MCP_MEM_TYPE_IO,   "VIC-II/SID I/O");
    add_region(regions, &n, 0xD800, 0xDBFF, MCP_MEM_TYPE_IO,   "CIA/ACIA I/O");

    /* ROM gap: $DC00-$DFFF */
    add_region(regions, &n, 0xDC00, 0xDFFF, MCP_MEM_TYPE_ROM,  "Character ROM");

    /* KERNAL ROM: $E000-$FFFF */
    add_region(regions, &n, 0xE000, 0xFFFF, MCP_MEM_TYPE_ROM,  "KERNAL ROM");

    /* Sentinel */
    regions[n].name = NULL;
    return n;
}

/* ========================================================================= */
/* Public: build memory map for current machine                              */
/* ========================================================================= */

int mcp_build_memory_map(mcp_mem_region_t *regions)
{
    switch (machine_class) {
        case VICE_MACHINE_C64:
        case VICE_MACHINE_C64SC:
        case VICE_MACHINE_SCPU64:
            return build_c64_map(regions);

        case VICE_MACHINE_C128:
            return build_c128_map(regions);

        case VICE_MACHINE_C64DTV:
            return build_c64dtv_map(regions);

        case VICE_MACHINE_VIC20:
            return build_vic20_map(regions);

        case VICE_MACHINE_PLUS4:
            return build_plus4_map(regions);

        case VICE_MACHINE_PET:
            return build_pet_map(regions);

        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
            return build_cbm2_map(regions);

        case VICE_MACHINE_VSID:
            /* VSID uses C64 memory layout for SID playback */
            return build_c64_map(regions);

        default:
            /* Unknown machine: return empty map with just sentinel */
            regions[0].name = NULL;
            return 0;
    }
}

/* ========================================================================= */
/* Validation helpers                                                        */
/* ========================================================================= */

/** @brief Get a short config description for the current machine.
 *
 *  Queries machine resources to build a string like "unexpanded, PAL"
 *  or "32K, NTSC". Writes into the caller-provided buffer.
 */
static void get_machine_config_desc(char *buf, size_t buf_size)
{
    int video = 0;
    const char *video_str = "";

    resources_get_int("MachineVideoStandard", &video);
    switch (video) {
        case 1:  video_str = "PAL";     break;  /* MACHINE_SYNC_PAL */
        case 2:  video_str = "NTSC";    break;  /* MACHINE_SYNC_NTSC */
        case 3:  video_str = "NTSC-O";  break;  /* MACHINE_SYNC_NTSCOLD */
        case 4:  video_str = "PAL-N";   break;  /* MACHINE_SYNC_PALN */
        default: video_str = "";        break;
    }

    switch (machine_class) {
        case VICE_MACHINE_VIC20:
        {
            int b0 = 0, b1 = 0, b2 = 0, b3 = 0, b5 = 0;
            int total_kb;

            resources_get_int("RAMBlock0", &b0);
            resources_get_int("RAMBlock1", &b1);
            resources_get_int("RAMBlock2", &b2);
            resources_get_int("RAMBlock3", &b3);
            resources_get_int("RAMBlock5", &b5);
            total_kb = 5 + (b0 ? 3 : 0) + (b1 ? 8 : 0) + (b2 ? 8 : 0)
                         + (b3 ? 8 : 0) + (b5 ? 8 : 0);
            if (total_kb == 5) {
                snprintf(buf, buf_size, "unexpanded, %s", video_str);
            } else {
                snprintf(buf, buf_size, "%dK, %s", total_kb, video_str);
            }
            break;
        }
        case VICE_MACHINE_PLUS4:
        {
            int ram = 64;
            resources_get_int("RamSize", &ram);
            snprintf(buf, buf_size, "%dK, %s", ram, video_str);
            break;
        }
        case VICE_MACHINE_PET:
        {
            int ram = 32;
            resources_get_int("RamSize", &ram);
            snprintf(buf, buf_size, "%dK, %s", ram, video_str);
            break;
        }
        case VICE_MACHINE_CBM5x0:
        case VICE_MACHINE_CBM6x0:
        {
            int ram = 128;
            resources_get_int("RamSize", &ram);
            snprintf(buf, buf_size, "%dK, %s", ram, video_str);
            break;
        }
        default:
            /* C64/C128/DTV/SCPU64/VSID: standard 64K */
            if (video_str[0] != '\0') {
                snprintf(buf, buf_size, "64K, %s", video_str);
            } else {
                snprintf(buf, buf_size, "64K");
            }
            break;
    }
}

/** @brief Build a summary of valid regions grouped by type.
 *
 *  Produces a string like:
 *  "RAM $0000-$03FF, $1000-$1FFF | ROM $C000-$DFFF | I/O $9000-$97FF"
 */
static void build_valid_summary(const mcp_mem_region_t *regions, int count,
                                char *buf, size_t buf_size)
{
    /* We group by type in the order: RAM, ROM, I/O, Cartridge */
    static const mcp_mem_region_type_t type_order[] = {
        MCP_MEM_TYPE_RAM, MCP_MEM_TYPE_ROM, MCP_MEM_TYPE_IO, MCP_MEM_TYPE_CARTRIDGE
    };
    static const char *type_labels[] = { "RAM", "ROM", "I/O", "Cart" };
    int t;
    int i;
    size_t pos = 0;
    int first_type = 1;

    buf[0] = '\0';

    for (t = 0; t < 4; t++) {
        int first_range = 1;

        for (i = 0; i < count; i++) {
            if (regions[i].type != type_order[t]) {
                continue;
            }
            if (first_range) {
                /* Add separator between type groups */
                if (!first_type) {
                    pos += (size_t)snprintf(buf + pos, buf_size - pos, " | ");
                }
                pos += (size_t)snprintf(buf + pos, buf_size - pos, "%s ", type_labels[t]);
                first_range = 0;
                first_type = 0;
            } else {
                pos += (size_t)snprintf(buf + pos, buf_size - pos, ", ");
            }
            pos += (size_t)snprintf(buf + pos, buf_size - pos,
                                    "$%04X-$%04X", regions[i].start, regions[i].end);
            if (pos >= buf_size - 1) {
                return;
            }
        }
    }
}

/** @brief Build a fix suggestion based on the unmapped region name.
 *
 *  Detects "Block N" patterns for VIC-20 and RAM size issues for other
 *  machines, returning actionable text.
 */
static void build_fix_suggestion(const char *region_name, char *buf, size_t buf_size)
{
    /* VIC-20 block pattern: "Block N (unmapped)" */
    if (strstr(region_name, "Block 0") != NULL) {
        snprintf(buf, buf_size, "Enable 3K expansion: vice.machine.config.set RAMBlock0=1");
    } else if (strstr(region_name, "Block 1") != NULL) {
        snprintf(buf, buf_size, "Enable 8K expansion: vice.machine.config.set RAMBlock1=1");
    } else if (strstr(region_name, "Block 2") != NULL) {
        snprintf(buf, buf_size, "Enable 8K expansion: vice.machine.config.set RAMBlock2=1");
    } else if (strstr(region_name, "Block 3") != NULL) {
        snprintf(buf, buf_size, "Enable 8K expansion: vice.machine.config.set RAMBlock3=1");
    } else if (strstr(region_name, "Block 5") != NULL) {
        snprintf(buf, buf_size, "Enable 8K expansion: vice.machine.config.set RAMBlock5=1");
    } else if (strstr(region_name, "16K") != NULL ||
               strstr(region_name, "Unmapped RAM") != NULL) {
        snprintf(buf, buf_size, "Increase RAM: vice.machine.config.set RamSize=32 (or 64)");
    } else {
        snprintf(buf, buf_size, "This region is not accessible in the current configuration.");
    }
}

/* ========================================================================= */
/* Public: validate address range                                            */
/* ========================================================================= */

mcp_addr_check_t mcp_validate_address_range(uint16_t address, uint16_t size)
{
    mcp_addr_check_t result;
    mcp_mem_region_t regions[MCP_MAX_MEMORY_REGIONS];
    int count;
    uint32_t end_addr;
    int i;

    result.valid = 1;
    result.error_msg[0] = '\0';

    if (size == 0) {
        return result;  /* Zero-length range is trivially valid */
    }

    count = mcp_build_memory_map(regions);
    if (count == 0) {
        /* Unknown machine or no map available -- allow everything */
        return result;
    }

    /* Calculate end address. Use uint32_t to handle the case where
     * address + size - 1 would overflow uint16_t (e.g., $FFFF + 1). */
    end_addr = (uint32_t)address + (uint32_t)size - 1;
    if (end_addr > 0xFFFF) {
        end_addr = 0xFFFF;
    }

    /* Check each region for overlap with the requested range */
    for (i = 0; i < count; i++) {
        /* Does the requested range overlap with this region? */
        if (address <= regions[i].end && (uint16_t)end_addr >= regions[i].start) {
            if (regions[i].type == MCP_MEM_TYPE_UNMAPPED) {
                /* Found overlap with an unmapped region -- invalid */
                char config_desc[64];
                char valid_summary[256];
                char fix_suggestion[128];
                const char *mach_name;

                mach_name = machine_get_name();
                get_machine_config_desc(config_desc, sizeof(config_desc));
                build_valid_summary(regions, count, valid_summary, sizeof(valid_summary));
                build_fix_suggestion(regions[i].name, fix_suggestion, sizeof(fix_suggestion));

                result.valid = 0;
                snprintf(result.error_msg, sizeof(result.error_msg),
                         "Address $%04X-$%04X is in unmapped region \"%s\" on %s (%s).\n"
                         "Valid regions: %s\n"
                         "%s",
                         address, (unsigned)end_addr,
                         regions[i].name,
                         mach_name ? mach_name : "unknown",
                         config_desc,
                         valid_summary,
                         fix_suggestion);
                return result;
            }
        }
    }

    return result;
}
