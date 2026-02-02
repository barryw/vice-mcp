/*
 * mcp_transport.c - MCP Streamable HTTP transport implementation
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

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>

#include "mcp_transport.h"
#include "log.h"

static log_t mcp_transport_log = LOG_DEFAULT;

/* HTTP server state - owned by mcp_transport, cleaned up in shutdown */
static struct MHD_Daemon *http_daemon = NULL;
static int server_running = 0;  /* 1 when HTTP server is active */

/* SSE connection tracking (Phase 2 requires manual pause before use)
 * Lifecycle:
 * - Slots set to {NULL, 0} when freed
 * - Must call MHD_Connection cleanup before setting to NULL
 * - All connections must be closed before daemon shutdown
 */
/* Maximum concurrent SSE connections. Typical usage expects 1-3 clients
 * (Claude, web UI, debug tools). 10 provides headroom without excessive
 * memory overhead (10 * sizeof(struct) = 160 bytes on 64-bit). */
#define MAX_SSE_CONNECTIONS 10
static struct {
    struct MHD_Connection *connection;  /* libmicrohttpd connection handle */
    int active;                          /* 1 if connection is open, 0 if free slot */
} sse_connections[MAX_SSE_CONNECTIONS] = {{NULL, 0}};

/* Request context for POST body accumulation */
struct request_context {
    char *body;
    size_t body_size;
    size_t body_capacity;
};

static void request_context_free(struct request_context *ctx)
{
    if (ctx != NULL) {
        if (ctx->body != NULL) {
            free(ctx->body);
        }
        free(ctx);
    }
}

/* Called when request processing is complete */
static void request_completed(void *cls,
                              struct MHD_Connection *connection,
                              void **con_cls,
                              enum MHD_RequestTerminationCode toe)
{
    (void)cls;
    (void)connection;
    (void)toe;

    struct request_context *ctx = *con_cls;
    if (ctx != NULL) {
        request_context_free(ctx);
        *con_cls = NULL;
    }
}

/* HTTP request handler callback for libmicrohttpd */
static enum MHD_Result http_handler(void *cls,
                                     struct MHD_Connection *connection,
                                     const char *url,
                                     const char *method,
                                     const char *version,
                                     const char *upload_data,
                                     size_t *upload_data_size,
                                     void **con_cls)
{
    (void)cls;
    (void)version;

    log_message(mcp_transport_log, "HTTP %s %s", method, url);

    /* Route requests */
    if (strcmp(url, "/mcp") == 0 && strcmp(method, "POST") == 0) {
        struct request_context *ctx = *con_cls;

        /* First call - initialize context */
        if (ctx == NULL) {
            ctx = calloc(1, sizeof(struct request_context));
            if (ctx == NULL) {
                log_error(mcp_transport_log, "Failed to allocate request context");
                return MHD_NO;
            }
            *con_cls = ctx;
            return MHD_YES;  /* Wait for upload data */
        }

        /* Accumulate upload data */
        if (*upload_data_size > 0) {
            size_t new_size = ctx->body_size + *upload_data_size;

            /* Resize buffer if needed */
            if (new_size > ctx->body_capacity) {
                size_t new_capacity = ctx->body_capacity * 2;
                if (new_capacity < new_size) {
                    new_capacity = new_size + 1024;
                }

                char *new_body = realloc(ctx->body, new_capacity);
                if (new_body == NULL) {
                    log_error(mcp_transport_log, "Failed to allocate request body buffer");
                    request_context_free(ctx);
                    return MHD_NO;
                }

                ctx->body = new_body;
                ctx->body_capacity = new_capacity;
            }

            /* Append data */
            memcpy(ctx->body + ctx->body_size, upload_data, *upload_data_size);
            ctx->body_size += *upload_data_size;
            *upload_data_size = 0;  /* Mark as processed */

            return MHD_YES;  /* Continue receiving */
        }

        /* All data received - process request in next task */
        return MHD_NO;  /* Placeholder */
    } else if (strcmp(url, "/events") == 0 && strcmp(method, "GET") == 0) {
        /* SSE endpoint - handle in later task */
        return MHD_NO;  /* Placeholder */
    } else {
        /* 404 Not Found */
        const char *not_found = "{\"error\":\"Not Found\"}";
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(not_found),
            (void*)not_found,
            MHD_RESPMEM_PERSISTENT);

        MHD_add_response_header(response, "Content-Type", "application/json");
        enum MHD_Result ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }
}

int mcp_transport_init(void)
{
    mcp_transport_log = log_open("MCP-Transport");

    log_message(mcp_transport_log, "MCP transport initializing...");

    /* TODO: Initialize HTTP server library */

    log_message(mcp_transport_log, "MCP transport initialized");

    return 0;
}

void mcp_transport_shutdown(void)
{
    log_message(mcp_transport_log, "MCP transport shutting down...");

    /* TODO: Shutdown HTTP server library */

    log_message(mcp_transport_log, "MCP transport shut down");
}

int mcp_transport_start(const char *host, int port)
{
    if (host == NULL) {
        log_error(mcp_transport_log, "Invalid host parameter (NULL)");
        return -1;
    }

    if (port < 0 || port > 65535) {
        log_error(mcp_transport_log, "Invalid port number: %d (must be 0-65535)", port);
        return -1;
    }

    log_message(mcp_transport_log, "Starting MCP transport on %s:%d", host, port);

    if (server_running) {
        log_warning(mcp_transport_log, "Server already running");
        return -1;
    }

    /* Start HTTP daemon */
    http_daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
        port,
        NULL, NULL,  /* No accept policy */
        &http_handler, NULL,  /* Request handler */
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END);

    if (http_daemon == NULL) {
        server_running = 0;  /* Reset state on failure */
        log_error(mcp_transport_log, "Failed to start HTTP server on port %d", port);
        return -1;
    }

    server_running = 1;
    log_message(mcp_transport_log, "MCP transport started on port %d", port);

    return 0;
}

void mcp_transport_stop(void)
{
    log_message(mcp_transport_log, "Stopping MCP transport");

    if (!server_running) {
        log_warning(mcp_transport_log, "Server not running");
        return;
    }

    /* Stop HTTP daemon */
    if (http_daemon != NULL) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
    }

    server_running = 0;
    log_message(mcp_transport_log, "MCP transport stopped");
}

int mcp_transport_sse_send_event(const char *event_type, const char *data)
{
    /* TODO: Send SSE event to all connected clients */
    /* Format: "event: <event_type>\ndata: <data>\n\n" */

    log_message(mcp_transport_log, "SSE event: %s", event_type);

    return 0;
}
