/*
 * mcp_transport.h - MCP Streamable HTTP transport for VICE
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

#ifndef VICE_MCP_TRANSPORT_H
#define VICE_MCP_TRANSPORT_H

#include "types.h"

/* Forward declarations for libmicrohttpd types */
struct MHD_Connection;

/** @brief Initialize the MCP transport layer.
 *
 *  Allocates resources for the HTTP server and SSE connection tracking.
 *  Must be called before mcp_transport_start().
 *
 *  @return 0 on success, -1 on failure
 */
extern int mcp_transport_init(void);

/** @brief Shutdown the MCP transport layer and free resources.
 *
 *  Stops the HTTP server if running and deallocates all transport resources.
 */
extern void mcp_transport_shutdown(void);

/** @brief Start the HTTP server for MCP requests.
 *
 *  Starts libmicrohttpd daemon listening for HTTP POST requests at /mcp
 *  and SSE connections at /sse. Requests are dispatched via VICE's trap
 *  mechanism for thread-safe execution on the main emulator thread.
 *
 *  @param host  IP address to bind to
 *  @param port  TCP port number
 *  @return 0 on success, -1 on failure
 */
extern int mcp_transport_start(const char *host, int port);

/** @brief Stop the HTTP server.
 *
 *  Closes all SSE connections and stops the libmicrohttpd daemon.
 */
extern void mcp_transport_stop(void);

/** @brief Send an event to all connected SSE clients.
 *
 *  Broadcasts a Server-Sent Event to all active SSE connections.
 *  Used for asynchronous notifications (breakpoints, execution state).
 *
 *  @param event_type  Event type string (e.g., "breakpoint", "state")
 *  @param data        JSON data payload
 *  @return 0 on success, -1 if no clients connected or on error
 */
extern int mcp_transport_sse_send_event(const char *event_type, const char *data);

#endif /* VICE_MCP_TRANSPORT_H */
