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
 * THREAD SAFETY:
 *
 * The MCP server runs an HTTP server on a separate thread using
 * libmicrohttpd. Thread safety is achieved via trap-based dispatch:
 * tool calls are queued and executed on VICE's main emulation thread
 * through the monitor trap mechanism, similar to how the built-in
 * monitor works. This ensures all tool handlers access emulator state
 * safely without concurrent modification.
 */

#ifndef VICE_MCP_SERVER_H
#define VICE_MCP_SERVER_H

#include "types.h"

/* Default MCP server settings */
#define MCP_DEFAULT_HOST "127.0.0.1"
#define MCP_DEFAULT_PORT 6510

/** @brief Initialize the MCP server subsystem.
 *
 *  Must be called once during VICE startup before using other MCP functions.
 *  Allocates resources and registers with VICE's configuration system.
 *
 *  @return 0 on success, -1 on failure
 */
extern int mcp_server_init(void);

/** @brief Shutdown the MCP server and free all resources.
 *
 *  Should be called during VICE shutdown. Stops the server if running
 *  and deallocates all MCP-related resources.
 */
extern void mcp_server_shutdown(void);

/** @brief Start the MCP HTTP server.
 *
 *  Starts listening for MCP JSON-RPC requests on the specified host and port.
 *  The server runs in a separate thread using libmicrohttpd.
 *
 *  @param host  IP address to bind to (e.g., "127.0.0.1" or "0.0.0.0")
 *  @param port  TCP port number to listen on
 *  @return 0 on success, -1 on failure
 */
extern int mcp_server_start(const char *host, int port);

/** @brief Stop the MCP HTTP server.
 *
 *  Stops accepting new connections and shuts down the HTTP server thread.
 *  Safe to call even if server is not running.
 */
extern void mcp_server_stop(void);

/** @brief Check if the MCP server is currently running.
 *
 *  @return 1 if server is running, 0 otherwise
 */
extern int mcp_server_is_running(void);

/** @brief Enable or disable the MCP server.
 *
 *  When enabled, the server will start automatically on VICE startup
 *  if configured via resources.
 *
 *  @param enabled  1 to enable, 0 to disable
 *  @return 0 on success
 */
extern int mcp_server_set_enabled(int enabled);

/** @brief Get the MCP server enabled state.
 *
 *  @return 1 if enabled, 0 if disabled
 */
extern int mcp_server_get_enabled(void);

/** @brief Set the MCP server port number.
 *
 *  @param port  TCP port number (1-65535)
 *  @return 0 on success, -1 if port is invalid
 */
extern int mcp_server_set_port(int port);

/** @brief Get the configured MCP server port.
 *
 *  @return The port number
 */
extern int mcp_server_get_port(void);

/** @brief Set the MCP server bind address.
 *
 *  @param host  IP address string (e.g., "127.0.0.1")
 *  @return 0 on success
 */
extern int mcp_server_set_host(const char *host);

/** @brief Get the configured MCP server bind address.
 *
 *  @return The host address string
 */
extern const char *mcp_server_get_host(void);

/** @brief Register MCP server VICE resources.
 *
 *  Registers MCPServer, MCPServerPort, MCPServerHost resources
 *  with VICE's resource system for configuration persistence.
 *
 *  @return 0 on success, -1 on failure
 */
extern int mcp_server_register_resources(void);

/** @brief Register MCP server command-line options.
 *
 *  Registers -mcpserver, -mcpserverport, -mcpserverhost options.
 *
 *  @return 0 on success, -1 on failure
 */
extern int mcp_server_register_cmdline_options(void);

#endif /* VICE_MCP_SERVER_H */
