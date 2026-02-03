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
#include <stdint.h>

/* POSIX-specific headers - MCP server only builds on POSIX systems.
 * Windows support is planned for a future release.
 * See configure.ac for the POSIX system detection logic.
 */
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <microhttpd.h>

#include "mcp_transport.h"
#include "mcp_tools.h"
#include "cJSON.h"
#include "log.h"
#include "interrupt.h"
#include "monitor.h"

/* From mcp_tools.c */
extern const char *CATASTROPHIC_ERROR_JSON;

/* Maximum request body size - 10MB for MCP JSON-RPC requests */
#define MAX_REQUEST_BODY_SIZE (10 * 1024 * 1024)

/* Maximum concurrent connections - prevents DoS via thread exhaustion */
#define MAX_CONNECTIONS 100

/* CORS policy - change to specific origin (e.g., "http://localhost:3000") or
 * set to NULL to disable CORS headers entirely */
#define CORS_ALLOW_ORIGIN "*"

/* Initial request body buffer size - most JSON-RPC requests are small */
#define INITIAL_BODY_CAPACITY 1024

/* Connection timeout in seconds - prevents slow/stalled connections from holding resources */
#define CONNECTION_TIMEOUT_SEC 30

static log_t mcp_transport_log = LOG_DEFAULT;

/* Mutex for thread-safe access to static state (server, connections) */
static pthread_mutex_t transport_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* ============================================================================
 * D3: Trap-based dispatch for thread-safe tool execution
 *
 * The emulator runs continuously. MCP requests are dispatched via VICE's trap
 * mechanism, which schedules execution on the main CPU thread. This ensures:
 * - Thread safety: all tool logic runs on main thread
 * - Live emulator: no pause/resume bouncing
 * - Consistent state: reads/writes happen at a defined point
 * ============================================================================ */

/* Request structure for trap-based dispatch */
typedef struct mcp_trap_request_s {
    const char *tool_name;
    cJSON *params;
    cJSON *response;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int complete;
} mcp_trap_request_t;

/* Trap handler - executes on VICE main thread */
static void mcp_trap_handler(uint16_t addr, void *data)
{
    mcp_trap_request_t *req = (mcp_trap_request_t *)data;

    (void)addr;  /* Unused - trap address not relevant for MCP */

    /* Execute tool on main thread - thread safe! */
    req->response = mcp_tools_dispatch(req->tool_name, req->params);

    /* Signal completion to waiting HTTP thread */
    pthread_mutex_lock(&req->mutex);
    req->complete = 1;
    pthread_cond_signal(&req->cond);
    pthread_mutex_unlock(&req->mutex);
}

/* Dispatch tool via trap mechanism for thread safety
 *
 * If emulator is running: queue trap, wait for execution on main thread
 * If in monitor mode: direct dispatch (safe because main loop is blocked)
 */
static cJSON* mcp_dispatch_via_trap(const char *tool_name, cJSON *params)
{
    /* If we're inside the monitor, the main loop is blocked waiting for input.
     * In this case, we can safely dispatch directly since there's no concurrent
     * execution. This also handles the case where traps won't fire. */
    if (monitor_is_inside_monitor()) {
        log_message(mcp_transport_log, "Monitor active - using direct dispatch for: %s", tool_name);
        return mcp_tools_dispatch(tool_name, params);
    }

    /* Emulator is running - use trap for thread-safe dispatch */
    mcp_trap_request_t req;
    struct timespec timeout;
    int wait_result;

    /* Initialize request */
    req.tool_name = tool_name;
    req.params = params;
    req.response = NULL;
    req.complete = 0;
    pthread_mutex_init(&req.mutex, NULL);
    pthread_cond_init(&req.cond, NULL);

    /* Queue trap to execute on main thread */
    log_message(mcp_transport_log, "Queuing trap dispatch for: %s", tool_name);
    interrupt_maincpu_trigger_trap(mcp_trap_handler, &req);

    /* Wait for trap to complete with timeout (5 seconds) */
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 5;

    pthread_mutex_lock(&req.mutex);
    while (!req.complete) {
        wait_result = pthread_cond_timedwait(&req.cond, &req.mutex, &timeout);
        if (wait_result != 0) {
            /* Timeout or error */
            pthread_mutex_unlock(&req.mutex);
            log_error(mcp_transport_log, "Trap dispatch timeout for: %s (emulator may be paused)", tool_name);

            /* Cleanup */
            pthread_mutex_destroy(&req.mutex);
            pthread_cond_destroy(&req.cond);

            /* Return error response */
            cJSON *error = cJSON_CreateObject();
            if (error) {
                cJSON_AddNumberToObject(error, "code", -32000);
                cJSON_AddStringToObject(error, "message", "Timeout: emulator may be paused or unresponsive");
            }
            return error;
        }
    }
    pthread_mutex_unlock(&req.mutex);

    log_message(mcp_transport_log, "Trap dispatch completed for: %s", tool_name);

    /* Cleanup */
    pthread_mutex_destroy(&req.mutex);
    pthread_cond_destroy(&req.cond);

    return req.response;
}

/* Process JSON-RPC 2.0 request and return response */
static char* process_jsonrpc_request(const char *request_body, size_t body_size)
{
    cJSON *request = NULL;
    cJSON *response = NULL;
    cJSON *method_item, *params_item, *id_item;
    char *response_str = NULL;

    (void)body_size;  /* Currently unused - body is already null-terminated */

    /* Parse JSON */
    request = cJSON_Parse(request_body);

    if (request == NULL) {
        log_error(mcp_transport_log, "Invalid JSON in request");
        /* Return parse error */
        response = cJSON_CreateObject();
        if (response == NULL) {
            /* Return static catastrophic error - no allocation needed */
            return (char *)CATASTROPHIC_ERROR_JSON;
        }
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        cJSON_AddNullToObject(response, "id");

        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", MCP_ERROR_PARSE_ERROR);
        cJSON_AddStringToObject(error, "message", "Parse error");
        cJSON_AddItemToObject(response, "error", error);

        response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);

        if (response_str == NULL) {
            /* Return static catastrophic error - no allocation needed */
            return (char *)CATASTROPHIC_ERROR_JSON;
        }

        return response_str;
    }

    /* Extract request fields */
    method_item = cJSON_GetObjectItem(request, "method");
    params_item = cJSON_GetObjectItem(request, "params");
    id_item = cJSON_GetObjectItem(request, "id");

    if (!cJSON_IsString(method_item)) {
        log_error(mcp_transport_log, "Missing or invalid method field");

        /* Copy ID before deleting request */
        cJSON *id_copy = NULL;
        if (id_item != NULL) {
            if (cJSON_IsNumber(id_item)) {
                id_copy = cJSON_CreateNumber(id_item->valuedouble);
            } else if (cJSON_IsString(id_item)) {
                id_copy = cJSON_CreateString(id_item->valuestring);
            } else if (cJSON_IsNull(id_item) || cJSON_IsBool(id_item)) {
                id_copy = cJSON_Duplicate(id_item, 1);
            }
        }

        cJSON_Delete(request);

        /* Return invalid request error */
        response = cJSON_CreateObject();
        if (response == NULL) {
            if (id_copy != NULL) {
                cJSON_Delete(id_copy);
            }
            /* Return static catastrophic error - no allocation needed */
            return (char *)CATASTROPHIC_ERROR_JSON;
        }
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        if (id_copy != NULL) {
            cJSON_AddItemToObject(response, "id", id_copy);
        } else {
            cJSON_AddNullToObject(response, "id");
        }

        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", MCP_ERROR_INVALID_REQUEST);
        cJSON_AddStringToObject(error, "message", "Invalid Request");
        cJSON_AddItemToObject(response, "error", error);

        response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);

        if (response_str == NULL) {
            /* Return static catastrophic error - no allocation needed */
            return (char *)CATASTROPHIC_ERROR_JSON;
        }

        return response_str;
    }

    /* Dispatch to tool via trap mechanism for thread safety */
    const char *method_name = method_item->valuestring;
    log_message(mcp_transport_log, "JSON-RPC request: %s", method_name);

    cJSON *result = mcp_dispatch_via_trap(method_name, params_item);

    /* Copy ID before deleting request */
    cJSON *id_copy = NULL;
    if (id_item != NULL) {
        if (cJSON_IsNumber(id_item)) {
            id_copy = cJSON_CreateNumber(id_item->valuedouble);
        } else if (cJSON_IsString(id_item)) {
            id_copy = cJSON_CreateString(id_item->valuestring);
        } else if (cJSON_IsNull(id_item) || cJSON_IsBool(id_item)) {
            id_copy = cJSON_Duplicate(id_item, 1);
        }
    }

    cJSON_Delete(request);

    /* JSON-RPC 2.0: Notifications (no ID) get no response */
    if (id_copy == NULL && result == NULL) {
        /* Return special marker for HTTP 202 with no body */
        return (char *)"";  /* Empty string signals no response body */
    }

    /* Build JSON-RPC response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        if (id_copy != NULL) {
            cJSON_Delete(id_copy);
        }
        if (result != NULL) {
            cJSON_Delete(result);
        }
        /* Return static catastrophic error - no allocation needed */
        return (char *)CATASTROPHIC_ERROR_JSON;
    }

    cJSON_AddStringToObject(response, "jsonrpc", "2.0");

    /* Add ID to response */
    if (id_copy != NULL) {
        cJSON_AddItemToObject(response, "id", id_copy);
    } else {
        cJSON_AddNullToObject(response, "id");
    }

    /* Add result or error */
    if (result == NULL) {
        /* NULL result = catastrophic error */
        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", MCP_ERROR_INTERNAL_ERROR);
        cJSON_AddStringToObject(error, "message", "Internal error: out of memory");
        cJSON_AddItemToObject(response, "error", error);
    } else {
        /* Check if result is an error object (has "code" field) */
        cJSON *code_item = cJSON_GetObjectItem(result, "code");
        if (code_item != NULL && cJSON_IsNumber(code_item)) {
            /* It's an error */
            cJSON_AddItemToObject(response, "error", result);
        } else {
            /* It's a success result */
            cJSON_AddItemToObject(response, "result", result);
        }
    }

    response_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    if (response_str == NULL) {
        /* Return static catastrophic error - no allocation needed */
        return (char *)CATASTROPHIC_ERROR_JSON;
    }

    return response_str;
}

/* Register SSE connection in tracking array */
static int register_sse_connection(struct MHD_Connection *connection)
{
    int i;

    pthread_mutex_lock(&transport_mutex);

    /* Find free slot */
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (!sse_connections[i].active) {
            sse_connections[i].connection = connection;
            sse_connections[i].active = 1;
            pthread_mutex_unlock(&transport_mutex);
            log_message(mcp_transport_log, "SSE connection registered in slot %d", i);
            return i;
        }
    }

    pthread_mutex_unlock(&transport_mutex);
    log_warning(mcp_transport_log, "SSE connection limit reached (%d)", MAX_SSE_CONNECTIONS);
    return -1;
}

/* Unregister SSE connection from tracking array */
static void unregister_sse_connection(struct MHD_Connection *connection)
{
    int i;

    pthread_mutex_lock(&transport_mutex);

    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (sse_connections[i].active && sse_connections[i].connection == connection) {
            sse_connections[i].connection = NULL;
            sse_connections[i].active = 0;
            pthread_mutex_unlock(&transport_mutex);
            log_message(mcp_transport_log, "SSE connection unregistered from slot %d", i);
            return;
        }
    }

    pthread_mutex_unlock(&transport_mutex);
}

/* Called when request processing is complete */
static void request_completed(void *cls,
                              struct MHD_Connection *connection,
                              void **con_cls,
                              enum MHD_RequestTerminationCode toe)
{
    (void)cls;
    (void)toe;

    /* Unregister SSE connection if registered */
    unregister_sse_connection(connection);

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

    /* Handle CORS preflight for all endpoints */
    if (strcmp(method, "OPTIONS") == 0) {
        const char *empty_response = "";
        struct MHD_Response *response = MHD_create_response_from_buffer(
            0,
            (void*)empty_response,
            MHD_RESPMEM_PERSISTENT);

        /* Add CORS headers for preflight */
        if (CORS_ALLOW_ORIGIN != NULL) {
            MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
            MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
            MHD_add_response_header(response, "Access-Control-Max-Age", "86400");  /* 24 hours */
        }

        enum MHD_Result ret = MHD_queue_response(connection, 204, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Route requests */
    if (strcmp(url, "/mcp") == 0 && strcmp(method, "POST") == 0) {
        struct request_context *ctx = *con_cls;

        /* First call - initialize context and validate headers */
        if (ctx == NULL) {
            /* Validate Content-Type header */
            const char *content_type = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Content-Type");

            if (content_type == NULL ||
                (strncmp(content_type, "application/json", 16) != 0 &&
                 strncmp(content_type, "application/json; charset=utf-8", 31) != 0)) {

                log_warning(mcp_transport_log, "Invalid Content-Type for /mcp: %s (expected application/json)",
                           content_type ? content_type : "(none)");

                const char *error_msg = "{\"error\":\"Unsupported Media Type\",\"message\":\"Content-Type must be application/json\"}";
                struct MHD_Response *response = MHD_create_response_from_buffer(
                    strlen(error_msg),
                    (void*)error_msg,
                    MHD_RESPMEM_PERSISTENT);

                MHD_add_response_header(response, "Content-Type", "application/json");
                enum MHD_Result ret = MHD_queue_response(connection, 415, response);
                MHD_destroy_response(response);
                return ret;
            }

            /* Validate Accept header - MCP clients must accept both JSON and SSE */
            const char *accept = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Accept");

            if (accept == NULL ||
                (strstr(accept, "application/json") == NULL && strstr(accept, "*/*") == NULL) ||
                (strstr(accept, "text/event-stream") == NULL && strstr(accept, "*/*") == NULL)) {

                log_warning(mcp_transport_log, "Invalid Accept header for /mcp: %s (must accept both application/json and text/event-stream)",
                           accept ? accept : "(none)");

                const char *error_msg = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,\"message\":\"Not Acceptable: Client must accept both application/json and text/event-stream\"}}";
                struct MHD_Response *response = MHD_create_response_from_buffer(
                    strlen(error_msg),
                    (void*)error_msg,
                    MHD_RESPMEM_PERSISTENT);

                MHD_add_response_header(response, "Content-Type", "application/json");
                enum MHD_Result ret = MHD_queue_response(connection, 406, response);
                MHD_destroy_response(response);
                return ret;
            }

            ctx = calloc(1, sizeof(struct request_context));
            if (ctx == NULL) {
                log_error(mcp_transport_log, "Failed to allocate request context");
                return MHD_NO;
            }

            /* Allocate initial buffer to avoid realloc on first chunk */
            ctx->body = malloc(INITIAL_BODY_CAPACITY);
            if (ctx->body == NULL) {
                log_error(mcp_transport_log, "Failed to allocate initial body buffer");
                free(ctx);
                return MHD_NO;
            }
            ctx->body_capacity = INITIAL_BODY_CAPACITY;
            ctx->body_size = 0;

            *con_cls = ctx;
            return MHD_YES;  /* Wait for upload data */
        }

        /* Accumulate upload data */
        if (*upload_data_size > 0) {
            /* Check for integer overflow */
            if (*upload_data_size > SIZE_MAX - ctx->body_size) {
                log_error(mcp_transport_log, "Request body size overflow");
                return MHD_NO;
            }

            size_t new_size = ctx->body_size + *upload_data_size;

            /* Enforce maximum request size */
            if (new_size > MAX_REQUEST_BODY_SIZE) {
                log_error(mcp_transport_log, "Request body too large: %zu bytes (max %d)",
                          new_size, MAX_REQUEST_BODY_SIZE);
                return MHD_NO;
            }

            /* Resize buffer if needed (+1 for null terminator) */
            if (new_size + 1 > ctx->body_capacity) {
                size_t new_capacity;

                /* Check for overflow in exponential growth */
                if (ctx->body_capacity > SIZE_MAX / 2) {
                    /* Can't double - use exact size needed */
                    new_capacity = new_size + 1;
                } else {
                    new_capacity = ctx->body_capacity * 2;
                    if (new_capacity < new_size + 1) {
                        new_capacity = new_size + 1024;
                    }
                }

                char *new_body = realloc(ctx->body, new_capacity);
                if (new_body == NULL) {
                    log_error(mcp_transport_log, "Failed to allocate request body buffer");
                    return MHD_NO;
                }

                ctx->body = new_body;
                ctx->body_capacity = new_capacity;
            }

            /* Append data */
            memcpy(ctx->body + ctx->body_size, upload_data, *upload_data_size);
            ctx->body_size += *upload_data_size;
            ctx->body[ctx->body_size] = '\0';  /* Null terminate for JSON parsing */
            *upload_data_size = 0;  /* Mark as processed */

            return MHD_YES;  /* Continue receiving */
        }

        /* All data received - process request */
        char *response_json = process_jsonrpc_request(ctx->body, ctx->body_size);
        if (response_json == NULL) {
            log_error(mcp_transport_log, "Failed to generate response");
            return MHD_NO;
        }

        /* Check for notification (empty string = no response body) */
        if (response_json[0] == '\0') {
            /* Return HTTP 202 Accepted with no body for notifications */
            struct MHD_Response *response = MHD_create_response_from_buffer(
                0, NULL, MHD_RESPMEM_PERSISTENT);
            enum MHD_Result ret = MHD_queue_response(connection, 202, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* Check if client accepts SSE */
        const char *accept = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Accept");
        int wants_sse = (accept != NULL && strstr(accept, "text/event-stream") != NULL);

        char *response_data;
        int is_static_data;

        if (wants_sse) {
            /* Format as SSE: event: message\ndata: {json}\n\n */
            size_t json_len = strlen(response_json);
            size_t sse_len = 15 + 6 + json_len + 2 + 1;  /* "event: message\n" (15) + "data: " (6) + json + "\n\n" (2) + null (1) */

            response_data = malloc(sse_len);
            if (response_data == NULL) {
                int is_json_static = (response_json == CATASTROPHIC_ERROR_JSON);
                if (!is_json_static) {
                    free(response_json);
                }
                log_error(mcp_transport_log, "Failed to allocate SSE response");
                return MHD_NO;
            }

            snprintf(response_data, sse_len, "event: message\ndata: %s\n\n", response_json);

            /* Free the JSON (if not static) since we've copied it */
            if (response_json != CATASTROPHIC_ERROR_JSON) {
                free(response_json);
            }
            is_static_data = 0;
        } else {
            /* Return plain JSON */
            response_data = response_json;
            is_static_data = (response_json == CATASTROPHIC_ERROR_JSON);
        }

        /* Create HTTP response */
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(response_data),
            (void*)response_data,
            is_static_data ? MHD_RESPMEM_PERSISTENT : MHD_RESPMEM_MUST_FREE);

        if (response == NULL) {
            log_error(mcp_transport_log, "Failed to create HTTP response");
            if (!is_static_data) {
                free(response_data);
            }
            return MHD_NO;
        }

        /* Add headers */
        if (wants_sse) {
            MHD_add_response_header(response, "Content-Type", "text/event-stream");
            MHD_add_response_header(response, "Cache-Control", "no-cache, no-store");
            MHD_add_response_header(response, "Connection", "keep-alive");
        } else {
            MHD_add_response_header(response, "Content-Type", "application/json");
        }

        if (CORS_ALLOW_ORIGIN != NULL) {
            MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
        }

        /* Queue response */
        enum MHD_Result ret = MHD_queue_response(connection, 200, response);
        MHD_destroy_response(response);

        return ret;
    } else if ((strcmp(url, "/mcp") == 0 || strcmp(url, "/events") == 0) && strcmp(method, "GET") == 0) {
        /* SSE endpoint for server-sent events - MCP spec requires GET /mcp support */

        /* Validate Accept header for GET /mcp - must accept text/event-stream */
        const char *accept = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Accept");
        if (accept == NULL ||
            (strstr(accept, "text/event-stream") == NULL && strstr(accept, "*/*") == NULL)) {

            log_warning(mcp_transport_log, "Invalid Accept header for GET /mcp: %s (must accept text/event-stream)",
                       accept ? accept : "(none)");

            const char *error_msg = "{\"jsonrpc\":\"2.0\",\"id\":\"\",\"error\":{\"code\":-32000,\"message\":\"Not Acceptable: Client must accept text/event-stream\"}}";
            struct MHD_Response *response = MHD_create_response_from_buffer(
                strlen(error_msg),
                (void*)error_msg,
                MHD_RESPMEM_PERSISTENT);

            MHD_add_response_header(response, "Content-Type", "application/json; charset=utf-8");
            enum MHD_Result ret = MHD_queue_response(connection, 406, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* Register this connection for SSE streaming */
        int slot = register_sse_connection(connection);
        if (slot < 0) {
            /* Connection limit reached */
            const char *error_msg = "{\"error\":\"SSE connection limit reached\"}";
            struct MHD_Response *response = MHD_create_response_from_buffer(
                strlen(error_msg),
                (void*)error_msg,
                MHD_RESPMEM_PERSISTENT);

            MHD_add_response_header(response, "Content-Type", "application/json");
            enum MHD_Result ret = MHD_queue_response(connection, 503, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* Create SSE response with keep-alive message */
        const char *sse_init = ": SSE connection established\n\n";
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(sse_init),
            (void*)sse_init,
            MHD_RESPMEM_PERSISTENT);

        /* Add SSE headers */
        MHD_add_response_header(response, "Content-Type", "text/event-stream");
        MHD_add_response_header(response, "Cache-Control", "no-cache");
        MHD_add_response_header(response, "Connection", "keep-alive");
        if (CORS_ALLOW_ORIGIN != NULL) {
            MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
        }

        /* Note: libmicrohttpd limitation - cannot push additional events after
         * initial response. This implementation:
         * 1. Registers the connection for tracking
         * 2. Sends initial SSE headers and comment
         * 3. Connection is unregistered in request_completed callback
         *
         * Phase 2 will upgrade to MHD_create_response_from_callback for true
         * streaming, or switch to WebSockets for bi-directional communication.
         */

        enum MHD_Result ret = MHD_queue_response(connection, 200, response);
        MHD_destroy_response(response);
        return ret;
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

    /* HTTP server initialization happens in mcp_transport_start() */

    log_message(mcp_transport_log, "MCP transport initialized");

    return 0;
}

void mcp_transport_shutdown(void)
{
    log_message(mcp_transport_log, "MCP transport shutting down...");

    /* Destroy mutex */
    pthread_mutex_destroy(&transport_mutex);

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

    pthread_mutex_lock(&transport_mutex);

    if (server_running) {
        pthread_mutex_unlock(&transport_mutex);
        log_warning(mcp_transport_log, "Server already running");
        return -1;
    }

    /* Prepare socket address for binding to specified host */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    /* Convert host string to binary IP address */
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        pthread_mutex_unlock(&transport_mutex);
        log_error(mcp_transport_log, "Invalid host address: %s (must be IPv4)", host);
        return -1;
    }

    /* Start HTTP daemon bound to specified host */
    http_daemon = MHD_start_daemon(
        MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
        0,  /* Port from sockaddr */
        NULL, NULL,  /* No accept policy */
        &http_handler, NULL,  /* Request handler */
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_SOCK_ADDR, &addr,
        MHD_OPTION_CONNECTION_LIMIT, (unsigned int)MAX_CONNECTIONS,
        MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)CONNECTION_TIMEOUT_SEC,
        MHD_OPTION_END);

    if (http_daemon == NULL) {
        server_running = 0;  /* Reset state on failure */
        pthread_mutex_unlock(&transport_mutex);
        log_error(mcp_transport_log, "Failed to start HTTP server on port %d", port);
        return -1;
    }

    server_running = 1;
    pthread_mutex_unlock(&transport_mutex);
    log_message(mcp_transport_log, "MCP transport started on port %d", port);

    return 0;
}

void mcp_transport_stop(void)
{
    log_message(mcp_transport_log, "Stopping MCP transport");

    pthread_mutex_lock(&transport_mutex);

    if (!server_running) {
        pthread_mutex_unlock(&transport_mutex);
        log_warning(mcp_transport_log, "Server not running");
        return;
    }

    /* Stop HTTP daemon */
    if (http_daemon != NULL) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
    }

    server_running = 0;
    pthread_mutex_unlock(&transport_mutex);
    log_message(mcp_transport_log, "MCP transport stopped");
}

/* TODO Phase 2: Upgrade SSE implementation to use MHD response callbacks
 * for true streaming. Current implementation tracks connections but cannot
 * push events after initial response. This requires either:
 * 1. Using MHD_create_response_from_callback with chunked encoding
 * 2. Upgrading to newer libmicrohttpd with better streaming support
 * 3. Using a separate WebSocket library instead of SSE
 */
int mcp_transport_sse_send_event(const char *event_type, const char *data)
{
    int i, active_count = 0;
    char *event_message;
    size_t msg_len;

    if (!server_running) {
        log_warning(mcp_transport_log, "Cannot send SSE event - server not running");
        return -1;
    }

    /* Format SSE message: "event: <type>\ndata: <data>\n\n" */
    /* Message format overhead: "event: " (7) + "\n" (1) + "data: " (6) + "\n\n" (2) = 16 bytes
     * Plus null terminator (1) and safety margin (4) = 21 bytes */
    msg_len = strlen(event_type) + strlen(data) + 21;
    event_message = malloc(msg_len);
    if (event_message == NULL) {
        log_error(mcp_transport_log, "Failed to allocate SSE event message");
        return -1;
    }

    snprintf(event_message, msg_len, "event: %s\ndata: %s\n\n", event_type, data);

    log_message(mcp_transport_log, "Broadcasting SSE event: %s", event_type);

    pthread_mutex_lock(&transport_mutex);

    /* Count active connections and log broadcast intent */
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (sse_connections[i].active && sse_connections[i].connection != NULL) {
            /* Note: With current libmicrohttpd version, we can't actually send
             * data on an existing connection - this would require upgrading
             * to use response callbacks or a different streaming approach.
             * For now, log that we would send the event. */
            log_message(mcp_transport_log, "Would send to SSE slot %d: %s", i, event_type);
            active_count++;
        }
    }

    pthread_mutex_unlock(&transport_mutex);

    free(event_message);

    if (active_count > 0) {
        log_message(mcp_transport_log, "SSE event broadcast to %d connections (logged only - Phase 2 needed for actual send)", active_count);
    } else {
        log_message(mcp_transport_log, "No active SSE connections - event not sent");
    }

    return active_count;
}
