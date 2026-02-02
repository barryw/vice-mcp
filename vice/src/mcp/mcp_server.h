/*
 * mcp_server.h - MCP server for VICE
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
 * THREAD SAFETY WARNING:
 *
 * Phase 1 implementation does NOT provide thread safety between the
 * HTTP server thread and the emulator thread. All MCP operations
 * should be performed while the emulator is paused.
 *
 * Race conditions can occur if:
 * - Reading memory or registers while emulation is running
 * - Writing memory or registers while emulation is running
 * - The emulated CPU is executing when MCP tools access state
 *
 * Phase 2 will implement proper synchronization using VICE's
 * interrupt system (IK_MONITOR) to pause emulation during MCP
 * operations, similar to how the built-in monitor works.
 *
 * For now, users should manually pause the emulator before using
 * MCP tools, or ensure MCP is only used during stopped state.
 */

#ifndef VICE_MCP_SERVER_H
#define VICE_MCP_SERVER_H

#include "types.h"

/* Default MCP server settings */
#define MCP_DEFAULT_HOST "127.0.0.1"
#define MCP_DEFAULT_PORT 6510

/* MCP server initialization and cleanup */
extern int mcp_server_init(void);
extern void mcp_server_shutdown(void);

/* MCP server control */
extern int mcp_server_start(const char *host, int port);
extern void mcp_server_stop(void);
extern int mcp_server_is_running(void);

/* MCP server configuration */
extern int mcp_server_set_enabled(int enabled);
extern int mcp_server_get_enabled(void);
extern int mcp_server_set_port(int port);
extern int mcp_server_get_port(void);
extern int mcp_server_set_host(const char *host);
extern const char *mcp_server_get_host(void);

/* Resource registration */
extern int mcp_server_register_resources(void);
extern int mcp_server_register_cmdline_options(void);

#endif /* VICE_MCP_SERVER_H */
