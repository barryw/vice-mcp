/*
 * mcp_tools_internal.h - Internal header for MCP tool domain files
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

/*
 * This internal header is included by all mcp_tools_*.c domain files.
 * It mirrors the montypes.h pattern from src/monitor/.
 *
 * External callers should use mcp_tools.h (the public API).
 */

#ifndef VICE_MCP_TOOLS_INTERNAL_H
#define VICE_MCP_TOOLS_INTERNAL_H

/* Public API (types, error codes, handler typedefs) */
#include "mcp_tools.h"

/* VICE core */
#include "vice.h"

/* Standard C */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* VICE libraries */
#include "cJSON.h"
#include "log.h"
#include "lib.h"
#include "monitor/montypes.h"  /* cond_node_t, new_cond, MON_ADDR, etc. */

/* -------------------------------------------------------------------------
 * Shared constants
 * ------------------------------------------------------------------------- */

/** @brief C64 RAM size in bytes. */
#define C64_RAM_SIZE 65536

/* -------------------------------------------------------------------------
 * Shared state (defined in mcp_tools.c)
 * ------------------------------------------------------------------------- */

/** @brief Log handle shared by all MCP tool files. */
extern log_t mcp_tools_log;

/** @brief Tool registry array (name, description, handler). Sentinel-terminated. */
extern const mcp_tool_t tool_registry[];

/* -------------------------------------------------------------------------
 * Shared helper functions (defined in mcp_tools_helpers.c)
 * ------------------------------------------------------------------------- */

/** @brief Create a JSON-RPC error response.
 *  @return JSON object or NULL on catastrophic OOM. */
extern cJSON *mcp_error(int code, const char *message);

/** @brief Create empty JSON Schema object (no parameters). */
extern cJSON *mcp_schema_empty(void);

/** @brief Create JSON Schema object with properties.
 *  Ownership of properties and required_array is transferred. */
extern cJSON *mcp_schema_object(cJSON *properties, cJSON *required_array);

/** @brief JSON Schema property helpers. */
extern cJSON *mcp_prop_number(const char *desc);
extern cJSON *mcp_prop_string(const char *desc);
extern cJSON *mcp_prop_boolean(const char *desc);
extern cJSON *mcp_prop_array(const char *item_type, const char *desc);

/** @brief Resolve address from number, hex string ($1000), or symbol name.
 *  @return Resolved 16-bit address, or -1 on failure (sets error_msg). */
extern int mcp_resolve_address(cJSON *value, const char **error_msg);

/** @brief Get (and create if needed) the MCP snapshots directory.
 *  @return Allocated path string (caller must free), or NULL on failure. */
extern char *mcp_get_snapshots_dir(void);

/** @brief Build a full path by combining directory and filename.
 *  @return Allocated path string (caller must free), or NULL on failure. */
extern char *mcp_build_vsf_path(const char *dir, const char *name);

/** @brief Extract C64 RAM from a VSF snapshot file.
 *  @return 0 on success, negative error code on failure. */
extern int vsf_extract_c64_ram(const char *vsf_path, uint8_t *ram_buffer);

/** @brief Write JSON metadata sidecar file for snapshot. */
extern int mcp_write_snapshot_metadata(const char *vsf_path, const char *name,
                                       const char *description, int include_roms,
                                       int include_disks);

/** @brief Read JSON metadata sidecar file for snapshot.
 *  @return cJSON object (caller must free), or NULL on failure. */
extern cJSON *mcp_read_snapshot_metadata(const char *vsf_path);

/** @brief Parse simple condition string for checkpoint conditions.
 *  Supports: A == $xx, X == $xx, Y == $xx, PC == $xxxx, SP == $xx
 *  @return Allocated condition node tree, or NULL on parse failure. */
extern cond_node_t *parse_simple_condition(const char *condition_str);

/* -------------------------------------------------------------------------
 * Handler prototypes - Protocol (mcp_tools_protocol.c)
 * ------------------------------------------------------------------------- */
extern cJSON *mcp_tool_tools_call(cJSON *params);

/* -------------------------------------------------------------------------
 * Handler prototypes - Execution (mcp_tools_execution.c)
 * ------------------------------------------------------------------------- */
/* (All declared in mcp_tools.h already) */

/* -------------------------------------------------------------------------
 * Handler prototypes - Memory (mcp_tools_memory.c)
 * ------------------------------------------------------------------------- */
/* (All declared in mcp_tools.h already) */

/* -------------------------------------------------------------------------
 * Handler prototypes - Checkpoint (mcp_tools_checkpoint.c)
 * ------------------------------------------------------------------------- */

/* Exposed reset functions for test harness */
extern void mcp_checkpoint_groups_reset(void);
extern void mcp_auto_snapshot_configs_reset(void);

/* -------------------------------------------------------------------------
 * Handler prototypes - Chip State (mcp_tools_chipstate.c)
 * ------------------------------------------------------------------------- */
/* (All declared in mcp_tools.h already) */

/* -------------------------------------------------------------------------
 * Handler prototypes - Disk (mcp_tools_disk.c)
 * ------------------------------------------------------------------------- */
extern cJSON *mcp_tool_machine_reset(cJSON *params);

/* -------------------------------------------------------------------------
 * Handler prototypes - Display (mcp_tools_display.c)
 * ------------------------------------------------------------------------- */
/* (All declared in mcp_tools.h already) */

/* -------------------------------------------------------------------------
 * Handler prototypes - Input (mcp_tools_input.c)
 * ------------------------------------------------------------------------- */
extern cJSON *mcp_tool_keyboard_matrix(cJSON *params);

/* -------------------------------------------------------------------------
 * Handler prototypes - Debug (mcp_tools_debug.c)
 * ------------------------------------------------------------------------- */
extern cJSON *mcp_tool_disassemble(cJSON *params);
extern cJSON *mcp_tool_symbols_load(cJSON *params);
extern cJSON *mcp_tool_symbols_lookup(cJSON *params);
extern cJSON *mcp_tool_watch_add(cJSON *params);
extern cJSON *mcp_tool_backtrace(cJSON *params);
extern cJSON *mcp_tool_run_until(cJSON *params);

/* -------------------------------------------------------------------------
 * Handler prototypes - Snapshot (mcp_tools_snapshot.c)
 * ------------------------------------------------------------------------- */
/* (All declared in mcp_tools.h already) */

/* -------------------------------------------------------------------------
 * Handler prototypes - Trace (mcp_tools_trace.c)
 * ------------------------------------------------------------------------- */

/* Exposed reset functions for test harness */
extern void mcp_trace_configs_reset(void);
extern void mcp_interrupt_log_configs_reset(void);

#endif /* VICE_MCP_TOOLS_INTERNAL_H */
