/*
 * mcp_tools_validation.h - Address validation for MCP tools
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

#ifndef VICE_MCP_TOOLS_VALIDATION_H
#define VICE_MCP_TOOLS_VALIDATION_H

#include "types.h"
#include "cJSON.h"

/* Memory region types (shared with machine config) */
typedef enum mcp_mem_region_type_e {
    MCP_MEM_TYPE_RAM,
    MCP_MEM_TYPE_ROM,
    MCP_MEM_TYPE_IO,
    MCP_MEM_TYPE_UNMAPPED,
    MCP_MEM_TYPE_CARTRIDGE
} mcp_mem_region_type_t;

/* Memory region definition */
typedef struct mcp_mem_region_s {
    uint16_t start;
    uint16_t end;
    mcp_mem_region_type_t type;
    const char *name;
} mcp_mem_region_t;

/* Maximum regions in a dynamically-built memory map */
#define MCP_MAX_MEMORY_REGIONS 32

/* Address validation result */
typedef struct {
    int valid;              /* 1 if valid, 0 if not */
    char error_msg[512];    /* Populated when valid == 0 */
} mcp_addr_check_t;

/** @brief Validate that an address range is accessible on the current machine.
 *
 *  Checks the current machine type and configuration (RAM expansion, etc.)
 *  to determine if the address range falls within mapped memory.
 *
 *  @param address  Start address
 *  @param size     Number of bytes (1-65536)
 *  @return Validation result with error message if invalid
 */
extern mcp_addr_check_t mcp_validate_address_range(uint16_t address, uint16_t size);

/** @brief Build the memory map for the current machine configuration.
 *
 *  Queries VICE resources to build a dynamic memory map. The map is written
 *  into the provided array (must have room for MCP_MAX_MEMORY_REGIONS entries).
 *  The array is sentinel-terminated (last entry has name == NULL).
 *
 *  @param regions  Output array (caller provides, at least MCP_MAX_MEMORY_REGIONS entries)
 *  @return Number of regions written (excluding sentinel)
 */
extern int mcp_build_memory_map(mcp_mem_region_t *regions);

/** @brief Convert region type enum to string. */
extern const char *mcp_mem_type_to_string(mcp_mem_region_type_t type);

/* -------------------------------------------------------------------------
 * Shared machine_has_* helpers (used by chipstate, machine config, validation)
 * ------------------------------------------------------------------------- */

extern int mcp_machine_has_vicii(void);
extern int mcp_machine_has_sid(void);
extern int mcp_machine_has_cia(void);
extern int mcp_machine_has_ted(void);
extern int mcp_machine_has_vdc(void);
extern int mcp_machine_has_crtc(void);
extern int mcp_machine_has_vic(void);
extern int mcp_machine_has_via(void);
extern int mcp_machine_has_pia(void);
extern int mcp_machine_has_acia(void);

#endif /* VICE_MCP_TOOLS_VALIDATION_H */
