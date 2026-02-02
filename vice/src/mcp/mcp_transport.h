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

/* Transport initialization and cleanup */
extern int mcp_transport_init(void);
extern void mcp_transport_shutdown(void);

/* Transport control */
extern int mcp_transport_start(const char *host, int port);
extern void mcp_transport_stop(void);

/* SSE (Server-Sent Events) for notifications */
extern int mcp_transport_sse_send_event(const char *event_type, const char *data);

/* HTTP endpoint handlers (internal) */
int mcp_http_handler(void *cls,
                     struct MHD_Connection *connection,
                     const char *url,
                     const char *method,
                     const char *version,
                     const char *upload_data,
                     size_t *upload_data_size,
                     void **con_cls);

#endif /* VICE_MCP_TRANSPORT_H */
