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
#include <time.h>

/* POSIX-specific headers - MCP server only builds on POSIX systems.
 * Windows support is planned for a future release.
 * See configure.ac for the POSIX system detection logic.
 */
#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#include <errno.h>  /* For ETIMEDOUT */
#else
#error "MCP transport requires pthreads"
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
#include "lib.h"
#include "interrupt.h"
#include "monitor.h"
#include "ui.h"  /* For ui_pause_active/enable/disable */
#include "mainlock.h"  /* For mainlock_obtain/release during UI pause */

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

/* SSE connection tracking */
typedef struct sse_connection_s {
    struct MHD_Connection *connection;  /* libmicrohttpd connection handle */
    int active;                          /* 1 if connection is open, 0 if free slot */
    time_t registered_time;              /* When connection was registered (for staleness detection) */
} sse_connection_t;

/* SSE connection staleness timeout in seconds - connections older than this
 * without activity may be considered stale. Used for cleanup on unclean disconnect. */
#define SSE_STALE_TIMEOUT_SEC 300

static sse_connection_t sse_connections[MAX_SSE_CONNECTIONS] = {{0}};

/* Request context for POST body accumulation */
struct request_context_s {
    char *body;
    size_t body_size;
    size_t body_capacity;
};

static void request_context_free(struct request_context_s *ctx)
{
    if (ctx != NULL) {
        if (ctx->body != NULL) {
            lib_free(ctx->body);
        }
        lib_free(ctx);
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

/* Request structure for trap-based dispatch.
 * MUST be heap-allocated because the trap may fire after the HTTP thread
 * has timed out. The last accessor (HTTP thread or trap handler) frees it. */
typedef struct mcp_trap_request_s {
    char *tool_name;       /* lib_strdup'd copy, freed by mcp_trap_request_free */
    cJSON *params;
    cJSON *response;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int complete;
    int abandoned;  /* Set by HTTP thread on timeout; trap handler frees req */
} mcp_trap_request_t;

/* Free a heap-allocated trap request */
static void mcp_trap_request_free(mcp_trap_request_t *req)
{
    if (req == NULL) {
        return;
    }
    pthread_mutex_destroy(&req->mutex);
    pthread_cond_destroy(&req->cond);
    if (req->tool_name != NULL) {
        lib_free(req->tool_name);
    }
    if (req->params != NULL) {
        cJSON_Delete(req->params);
    }
    if (req->response != NULL) {
        cJSON_Delete(req->response);
    }
    lib_free(req);
}

/* Trap handler - executes on VICE main thread */
static void mcp_trap_handler(uint16_t addr, void *data)
{
    mcp_trap_request_t *req = (mcp_trap_request_t *)data;

    (void)addr;  /* Unused - trap address not relevant for MCP */

    /* Execute tool on main thread - thread safe! */
    req->response = mcp_tools_dispatch(req->tool_name, req->params);

    /* Signal completion to waiting HTTP thread */
    pthread_mutex_lock(&req->mutex);
    if (req->abandoned) {
        /* HTTP thread timed out and is gone. We own the request now. */
        pthread_mutex_unlock(&req->mutex);
        mcp_trap_request_free(req);
        return;
    }
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

    /* If UI pause is active, the emulator main loop is paused but not in
     * monitor mode. In this state, traps won't fire because the CPU loop
     * isn't running. We need to acquire the mainlock to safely access
     * emulator state while the pause loop periodically yields it.
     *
     * Note: There's a potential TOCTOU race between checking ui_pause_active()
     * and acquiring mainlock. We re-verify the state after obtaining the lock
     * to handle the case where pause was disabled in between. */
    if (ui_pause_active()) {
        cJSON *response;
        log_message(mcp_transport_log, "UI paused - acquiring mainlock for dispatch: %s", tool_name);
        mainlock_obtain();

        /* Re-verify pause is still active after obtaining lock */
        if (!ui_pause_active()) {
            mainlock_release();
            log_message(mcp_transport_log, "Pause disabled during lock acquisition - falling through to trap dispatch");
            /* Fall through to trap-based dispatch below */
        } else {
            response = mcp_tools_dispatch(tool_name, params);
            mainlock_release();
            return response;
        }
    }

    /* Emulator is running - use trap for thread-safe dispatch.
     * The request MUST be heap-allocated because the trap handler may fire
     * after this function has timed out. On timeout, we mark the request
     * as abandoned and the trap handler will free it. */
    {
        mcp_trap_request_t *req;
        struct timespec timeout;
        int wait_result;
        cJSON *response;

        req = (mcp_trap_request_t *)lib_malloc(sizeof(mcp_trap_request_t));
        req->tool_name = lib_strdup(tool_name);

        /* Duplicate params if provided, with OOM check */
        if (params != NULL) {
            req->params = cJSON_Duplicate(params, 1);
            if (req->params == NULL) {
                /* OOM during params duplication - cleanup and return error */
                lib_free(req->tool_name);
                lib_free(req);
                log_error(mcp_transport_log, "OOM duplicating params for trap dispatch: %s", tool_name);
                {
                    cJSON *error = cJSON_CreateObject();
                    if (error) {
                        cJSON_AddNumberToObject(error, "code", -32603);
                        cJSON_AddStringToObject(error, "message", "Internal error: out of memory");
                    }
                    return error;
                }
            }
        } else {
            req->params = NULL;
        }

        req->response = NULL;
        req->complete = 0;
        req->abandoned = 0;
        pthread_mutex_init(&req->mutex, NULL);
        pthread_cond_init(&req->cond, NULL);

        /* Queue trap to execute on main thread */
        log_message(mcp_transport_log, "Queuing trap dispatch for: %s", tool_name);
        interrupt_maincpu_trigger_trap(mcp_trap_handler, req);

        /* Wait for trap to complete with timeout (5 seconds).
         * CLOCK_REALTIME is required here because pthread_cond_timedwait()
         * uses an absolute CLOCK_REALTIME deadline by default. Using
         * CLOCK_MONOTONIC would require pthread_condattr_setclock() which
         * is not universally available on all POSIX platforms. */
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5;

        pthread_mutex_lock(&req->mutex);
        while (!req->complete) {
            wait_result = pthread_cond_timedwait(&req->cond, &req->mutex, &timeout);
            if (wait_result == ETIMEDOUT) {
                /* Timeout - mark abandoned so trap handler frees req */
                req->abandoned = 1;
                pthread_mutex_unlock(&req->mutex);
                log_error(mcp_transport_log, "Trap dispatch timeout for: %s (emulator may be paused)", tool_name);

                /* Return error response - req will be freed by trap handler */
                {
                    cJSON *error = cJSON_CreateObject();
                    if (error) {
                        cJSON_AddNumberToObject(error, "code", -32000);
                        cJSON_AddStringToObject(error, "message", "Timeout: emulator may be paused or unresponsive");
                    }
                    return error;
                }
            } else if (wait_result != 0) {
                /* Unexpected error (EINVAL, etc.) - log and abandon */
                req->abandoned = 1;
                pthread_mutex_unlock(&req->mutex);
                log_error(mcp_transport_log, "pthread_cond_timedwait failed with error %d for: %s", wait_result, tool_name);

                /* Return error response - req will be freed by trap handler */
                {
                    cJSON *error = cJSON_CreateObject();
                    if (error) {
                        cJSON_AddNumberToObject(error, "code", -32603);
                        cJSON_AddStringToObject(error, "message", "Internal error: condition wait failed");
                    }
                    return error;
                }
            }
        }
        pthread_mutex_unlock(&req->mutex);

        log_message(mcp_transport_log, "Trap dispatch completed for: %s", tool_name);

        /* Extract response and free the request */
        response = req->response;
        req->response = NULL;  /* Prevent mcp_trap_request_free from deleting it */
        mcp_trap_request_free(req);

        return response;
    }
}

/* Process JSON-RPC 2.0 request and return response.
 *
 * Return value ownership:
 * - Normal response: malloc'd by cJSON_PrintUnformatted, caller must free() it
 * - OOM fallback: returns CATASTROPHIC_ERROR_JSON (static), caller must NOT free
 * - Notification (no id): returns "" (static literal), caller must NOT free
 *
 * Caller should check:
 *   if (result[0] == '\0')        -> notification, HTTP 202, no body
 *   if (result == CATASTROPHIC_ERROR_JSON) -> static, use MHD_RESPMEM_PERSISTENT
 *   otherwise                     -> malloc'd, use free() or MHD_RESPMEM_MUST_FREE
 */
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
        cJSON *error;

        log_error(mcp_transport_log, "Invalid JSON in request");
        /* Return parse error */
        response = cJSON_CreateObject();
        if (response == NULL) {
            /* Return static catastrophic error - no allocation needed */
            return (char *)CATASTROPHIC_ERROR_JSON;
        }
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        cJSON_AddNullToObject(response, "id");

        error = cJSON_CreateObject();
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
        cJSON *id_copy = NULL;
        cJSON *error;

        log_error(mcp_transport_log, "Missing or invalid method field");

        /* Copy ID before deleting request */
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

        error = cJSON_CreateObject();
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
    {
        const char *method_name = method_item->valuestring;
        cJSON *result;
        cJSON *id_copy = NULL;
        cJSON *error;
        cJSON *code_item;

        log_message(mcp_transport_log, "JSON-RPC request: %s", method_name);

        result = mcp_dispatch_via_trap(method_name, params_item);

        /* Copy ID before deleting request */
        if (id_item != NULL) {
            if (cJSON_IsNumber(id_item)) {
                id_copy = cJSON_CreateNumber(id_item->valuedouble);
            } else if (cJSON_IsString(id_item)) {
                id_copy = cJSON_CreateString(id_item->valuestring);
            } else if (cJSON_IsNull(id_item) || cJSON_IsBool(id_item)) {
                id_copy = cJSON_Duplicate(id_item, 1);
            } else {
                /* id present but unrecognized type - return JSON-RPC error */
                cJSON_Delete(request);
                if (result != NULL) {
                    cJSON_Delete(result);
                }

                response = cJSON_CreateObject();
                if (response == NULL) {
                    return (char *)CATASTROPHIC_ERROR_JSON;
                }
                cJSON_AddStringToObject(response, "jsonrpc", "2.0");
                cJSON_AddNullToObject(response, "id");

                error = cJSON_CreateObject();
                cJSON_AddNumberToObject(error, "code", MCP_ERROR_INVALID_REQUEST);
                cJSON_AddStringToObject(error, "message", "Invalid Request: unsupported id type");
                cJSON_AddItemToObject(response, "error", error);

                response_str = cJSON_PrintUnformatted(response);
                cJSON_Delete(response);
                if (response_str == NULL) {
                    return (char *)CATASTROPHIC_ERROR_JSON;
                }
                return response_str;
            }
        }

        /* OOM check: id was present but cJSON failed to copy it */
        if (id_item != NULL && id_copy == NULL) {
            cJSON_Delete(request);
            if (result != NULL) {
                cJSON_Delete(result);
            }
            return (char *)CATASTROPHIC_ERROR_JSON;
        }

        cJSON_Delete(request);

        /* JSON-RPC 2.0: Notifications (no ID) get no response */
        if (id_copy == NULL) {
            /* Free any result the tool returned for a notification */
            if (result != NULL) {
                cJSON_Delete(result);
            }
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

        /* Add ID to response (id_copy is always non-NULL at this point:
         * NULL id_copy returns early as notification or OOM fallback above) */
        cJSON_AddItemToObject(response, "id", id_copy);

        /* Add result or error */
        if (result == NULL) {
            /* NULL result = catastrophic error */
            error = cJSON_CreateObject();
            cJSON_AddNumberToObject(error, "code", MCP_ERROR_INTERNAL_ERROR);
            cJSON_AddStringToObject(error, "message", "Internal error: out of memory");
            cJSON_AddItemToObject(response, "error", error);
        } else {
            /* Check if result is an error object (has "code" field) */
            code_item = cJSON_GetObjectItem(result, "code");
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
}

/* Register SSE connection in tracking array */
static int register_sse_connection(struct MHD_Connection *connection)
{
    int i;
    time_t now;

    pthread_mutex_lock(&transport_mutex);

    now = time(NULL);

    /* Find free slot */
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (!sse_connections[i].active) {
            sse_connections[i].connection = connection;
            sse_connections[i].active = 1;
            sse_connections[i].registered_time = now;
            pthread_mutex_unlock(&transport_mutex);
            log_message(mcp_transport_log, "SSE connection registered in slot %d", i);
            return i;
        }
    }

    pthread_mutex_unlock(&transport_mutex);
    log_warning(mcp_transport_log, "SSE connection limit reached (%d)", MAX_SSE_CONNECTIONS);
    return -1;
}

/* Clean up stale SSE connections that may have disconnected without proper cleanup.
 * This handles the case where a client disconnects uncleanly and request_completed
 * callback doesn't fire. Called periodically or when registering new connections. */
static void cleanup_stale_sse_connections(void)
{
    int i;
    time_t now;
    int cleaned = 0;

    pthread_mutex_lock(&transport_mutex);

    now = time(NULL);

    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (sse_connections[i].active) {
            time_t age = now - sse_connections[i].registered_time;
            if (age > SSE_STALE_TIMEOUT_SEC) {
                /* Connection is stale - mark as inactive.
                 * Note: We can't actually close the MHD connection here as that
                 * requires MHD_Connection cleanup which happens in request_completed.
                 * We just mark the slot as free so a new connection can use it. */
                log_warning(mcp_transport_log, "Cleaning up stale SSE connection in slot %d (age: %ld sec)",
                           i, (long)age);
                sse_connections[i].connection = NULL;
                sse_connections[i].active = 0;
                sse_connections[i].registered_time = 0;
                cleaned++;
            }
        }
    }

    pthread_mutex_unlock(&transport_mutex);

    if (cleaned > 0) {
        log_message(mcp_transport_log, "Cleaned up %d stale SSE connections", cleaned);
    }
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
            sse_connections[i].registered_time = 0;
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
    struct request_context_s *ctx;

    (void)cls;
    (void)toe;

    /* Unregister SSE connection if registered */
    unregister_sse_connection(connection);

    ctx = (struct request_context_s *)*con_cls;
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
        struct MHD_Response *response;
        enum MHD_Result ret;

        response = MHD_create_response_from_buffer(
            0,
            (void *)empty_response,
            MHD_RESPMEM_PERSISTENT);

        /* Add CORS headers for preflight */
#ifdef CORS_ALLOW_ORIGIN
        MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        MHD_add_response_header(response, "Access-Control-Max-Age", "86400");  /* 24 hours */
#endif

        ret = MHD_queue_response(connection, 204, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Route requests */
    if (strcmp(url, "/mcp") == 0 && strcmp(method, "POST") == 0) {
        struct request_context_s *ctx = (struct request_context_s *)*con_cls;

        /* First call - initialize context and validate headers */
        if (ctx == NULL) {
            const char *content_type;
            const char *accept;

            /* Validate Content-Type header */
            content_type = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Content-Type");

            if (content_type == NULL ||
                strncasecmp(content_type, "application/json", 16) != 0) {
                const char *error_msg = "{\"error\":\"Unsupported Media Type\",\"message\":\"Content-Type must be application/json\"}";
                struct MHD_Response *response;
                enum MHD_Result ret;

                log_warning(mcp_transport_log, "Invalid Content-Type for /mcp: %s (expected application/json)",
                           content_type ? content_type : "(none)");

                response = MHD_create_response_from_buffer(
                    strlen(error_msg),
                    (void *)error_msg,
                    MHD_RESPMEM_PERSISTENT);

                MHD_add_response_header(response, "Content-Type", "application/json");
                ret = MHD_queue_response(connection, 415, response);
                MHD_destroy_response(response);
                return ret;
            }

            /* Validate Accept header - POST /mcp returns JSON, so only require application/json.
             * A missing Accept header implies the client accepts any media type (RFC 7231). */
            accept = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Accept");

            if (accept != NULL &&
                strstr(accept, "application/json") == NULL &&
                strstr(accept, "*/*") == NULL) {
                const char *error_msg = "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,\"message\":\"Not Acceptable: Client must accept application/json\"}}";
                struct MHD_Response *response;
                enum MHD_Result ret;

                log_warning(mcp_transport_log, "Invalid Accept header for POST /mcp: %s (must accept application/json)",
                           accept ? accept : "(none)");

                response = MHD_create_response_from_buffer(
                    strlen(error_msg),
                    (void *)error_msg,
                    MHD_RESPMEM_PERSISTENT);

                MHD_add_response_header(response, "Content-Type", "application/json");
                ret = MHD_queue_response(connection, 406, response);
                MHD_destroy_response(response);
                return ret;
            }

            /* lib_calloc/lib_malloc abort on OOM - no NULL check needed */
            ctx = (struct request_context_s *)lib_calloc(1, sizeof(struct request_context_s));

            /* Allocate initial buffer to avoid realloc on first chunk */
            ctx->body = (char *)lib_malloc(INITIAL_BODY_CAPACITY);
            ctx->body_capacity = INITIAL_BODY_CAPACITY;
            ctx->body_size = 0;

            *con_cls = ctx;
            return MHD_YES;  /* Wait for upload data */
        }

        /* Accumulate upload data */
        if (*upload_data_size > 0) {
            size_t new_size;

            /* Check for integer overflow */
            if (*upload_data_size > SIZE_MAX - ctx->body_size) {
                log_error(mcp_transport_log, "Request body size overflow");
                return MHD_NO;
            }

            new_size = ctx->body_size + *upload_data_size;

            /* Enforce maximum request size */
            if (new_size > MAX_REQUEST_BODY_SIZE) {
                log_error(mcp_transport_log, "Request body too large: %zu bytes (max %d)",
                          new_size, MAX_REQUEST_BODY_SIZE);
                return MHD_NO;
            }

            /* Resize buffer if needed (+1 for null terminator) */
            if (new_size + 1 > ctx->body_capacity) {
                size_t new_capacity;
                char *new_body;

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

                /* lib_realloc aborts on OOM - no NULL check needed */
                new_body = (char *)lib_realloc(ctx->body, new_capacity);
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
        {
            char *response_json;
            const char *accept;
            int wants_sse;
            struct MHD_Response *response;
            enum MHD_Result ret;

            response_json = process_jsonrpc_request(ctx->body, ctx->body_size);
            if (response_json == NULL) {
                log_error(mcp_transport_log, "Failed to generate response");
                return MHD_NO;
            }

            /* Check for notification (empty string = no response body) */
            if (response_json[0] == '\0') {
                /* Return HTTP 202 Accepted with no body for notifications */
                response = MHD_create_response_from_buffer(
                    0, NULL, MHD_RESPMEM_PERSISTENT);
                ret = MHD_queue_response(connection, 202, response);
                MHD_destroy_response(response);
                return ret;
            }

            /* Check if client accepts SSE */
            accept = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Accept");
            wants_sse = (accept != NULL && strstr(accept, "text/event-stream") != NULL);

            if (wants_sse) {
                /* Format as SSE: event: message\ndata: {json}\n\n */
                size_t json_len;
                size_t sse_len;
                char *sse_buf;

                json_len = strlen(response_json);
                /* "event: message\n" (15) + "data: " (6) + json + "\n\n" (2) + null (1) */
                sse_len = 15 + 6 + json_len + 2 + 1;

                sse_buf = (char *)lib_malloc(sse_len);
                snprintf(sse_buf, sse_len, "event: message\ndata: %s\n\n", response_json);

                /* Free the JSON (if not static) since we've copied it into SSE buffer.
                 * cJSON_PrintUnformatted returns malloc'd memory - must use free(), not lib_free(). */
                if (response_json != CATASTROPHIC_ERROR_JSON) {
                    free(response_json);
                }

                /* Use MHD_RESPMEM_MUST_COPY because sse_buf was allocated with
                 * lib_malloc (VICE allocator), but MHD would call stdlib free().
                 * MHD copies the data, then we free our buffer with lib_free(). */
                response = MHD_create_response_from_buffer(
                    strlen(sse_buf),
                    (void *)sse_buf,
                    MHD_RESPMEM_MUST_COPY);

                lib_free(sse_buf);

                if (response == NULL) {
                    log_error(mcp_transport_log, "Failed to create HTTP response");
                    return MHD_NO;
                }
            } else {
                /* Return plain JSON.
                 * response_json is either:
                 * - malloc'd by cJSON_PrintUnformatted: MHD_RESPMEM_MUST_FREE is correct
                 *   (MHD will call free(), matching cJSON's malloc)
                 * - static CATASTROPHIC_ERROR_JSON: MHD_RESPMEM_PERSISTENT is correct */
                int is_static = (response_json == CATASTROPHIC_ERROR_JSON);

                response = MHD_create_response_from_buffer(
                    strlen(response_json),
                    (void *)response_json,
                    is_static ? MHD_RESPMEM_PERSISTENT : MHD_RESPMEM_MUST_FREE);

                if (response == NULL) {
                    log_error(mcp_transport_log, "Failed to create HTTP response");
                    if (!is_static) {
                        free(response_json);
                    }
                    return MHD_NO;
                }
            }

            /* Add headers */
            if (wants_sse) {
                MHD_add_response_header(response, "Content-Type", "text/event-stream");
                MHD_add_response_header(response, "Cache-Control", "no-cache, no-store");
                MHD_add_response_header(response, "Connection", "keep-alive");
            } else {
                MHD_add_response_header(response, "Content-Type", "application/json");
            }

#ifdef CORS_ALLOW_ORIGIN
            MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
#endif

            /* Queue response */
            ret = MHD_queue_response(connection, 200, response);
            MHD_destroy_response(response);

            return ret;
        }
    } else if ((strcmp(url, "/mcp") == 0 || strcmp(url, "/events") == 0) && strcmp(method, "GET") == 0) {
        /* SSE endpoint for server-sent events - MCP spec requires GET /mcp support */
        const char *accept;
        int slot;
        const char *sse_init;
        struct MHD_Response *response;
        enum MHD_Result ret;

        /* Validate Accept header for GET /mcp - must accept text/event-stream */
        accept = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Accept");
        if (accept == NULL ||
            (strstr(accept, "text/event-stream") == NULL && strstr(accept, "*/*") == NULL)) {
            const char *error_msg = "{\"jsonrpc\":\"2.0\",\"id\":\"\",\"error\":{\"code\":-32000,\"message\":\"Not Acceptable: Client must accept text/event-stream\"}}";

            log_warning(mcp_transport_log, "Invalid Accept header for GET /mcp: %s (must accept text/event-stream)",
                       accept ? accept : "(none)");

            response = MHD_create_response_from_buffer(
                strlen(error_msg),
                (void *)error_msg,
                MHD_RESPMEM_PERSISTENT);

            MHD_add_response_header(response, "Content-Type", "application/json; charset=utf-8");
            ret = MHD_queue_response(connection, 406, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* Register this connection for SSE streaming */
        slot = register_sse_connection(connection);
        if (slot < 0) {
            /* Connection limit reached */
            const char *error_msg = "{\"error\":\"SSE connection limit reached\"}";

            response = MHD_create_response_from_buffer(
                strlen(error_msg),
                (void *)error_msg,
                MHD_RESPMEM_PERSISTENT);

            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, 503, response);
            MHD_destroy_response(response);
            return ret;
        }

        /* Create SSE response with keep-alive message */
        sse_init = ": SSE connection established\n\n";
        response = MHD_create_response_from_buffer(
            strlen(sse_init),
            (void *)sse_init,
            MHD_RESPMEM_PERSISTENT);

        /* Add SSE headers */
        MHD_add_response_header(response, "Content-Type", "text/event-stream");
        MHD_add_response_header(response, "Cache-Control", "no-cache");
        MHD_add_response_header(response, "Connection", "keep-alive");
#ifdef CORS_ALLOW_ORIGIN
        MHD_add_response_header(response, "Access-Control-Allow-Origin", CORS_ALLOW_ORIGIN);
#endif

        /* Note: libmicrohttpd limitation - cannot push additional events after
         * initial response. This implementation:
         * 1. Registers the connection for tracking
         * 2. Sends initial SSE headers and comment
         * 3. Connection is unregistered in request_completed callback
         *
         * Phase 2 will upgrade to MHD_create_response_from_callback for true
         * streaming, or switch to WebSockets for bi-directional communication.
         */

        ret = MHD_queue_response(connection, 200, response);
        MHD_destroy_response(response);
        return ret;
    } else {
        /* 404 Not Found */
        const char *not_found = "{\"error\":\"Not Found\"}";
        struct MHD_Response *response;
        enum MHD_Result ret;

        response = MHD_create_response_from_buffer(
            strlen(not_found),
            (void *)not_found,
            MHD_RESPMEM_PERSISTENT);

        MHD_add_response_header(response, "Content-Type", "application/json");
        ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }
}

int mcp_transport_init(void)
{
    mcp_transport_log = log_open("MCP-Transport");

    log_message(mcp_transport_log, "MCP transport initializing...");

    /* Re-initialize mutex in case shutdown was previously called.
     * PTHREAD_MUTEX_INITIALIZER only works for static init; after
     * pthread_mutex_destroy() we must use pthread_mutex_init(). */
    pthread_mutex_init(&transport_mutex, NULL);

    /* HTTP server initialization happens in mcp_transport_start() */

    log_message(mcp_transport_log, "MCP transport initialized");

    return 0;
}

void mcp_transport_shutdown(void)
{
    int i;

    log_message(mcp_transport_log, "MCP transport shutting down...");

    /* Clear SSE connection tracking to prevent stale pointers on restart */
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        sse_connections[i].connection = NULL;
        sse_connections[i].active = 0;
    }

    /* Destroy mutex */
    pthread_mutex_destroy(&transport_mutex);

    log_message(mcp_transport_log, "MCP transport shut down");
}

int mcp_transport_start(const char *host, int port)
{
    struct sockaddr_in addr;

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
    struct MHD_Daemon *local_daemon = NULL;

    log_message(mcp_transport_log, "Stopping MCP transport");

    pthread_mutex_lock(&transport_mutex);

    if (!server_running) {
        pthread_mutex_unlock(&transport_mutex);
        log_warning(mcp_transport_log, "Server not running");
        return;
    }

    /* Capture daemon pointer and clear global state while holding lock.
     * MHD_stop_daemon must be called outside the lock because it waits
     * for connection handler threads to finish, and those threads may
     * need transport_mutex (e.g., unregister_sse_connection). */
    local_daemon = http_daemon;
    http_daemon = NULL;
    server_running = 0;

    pthread_mutex_unlock(&transport_mutex);

    /* Stop HTTP daemon outside the lock to avoid deadlock */
    if (local_daemon != NULL) {
        MHD_stop_daemon(local_daemon);
    }

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
    int i;
    int active_count = 0;
    int running;
    char *event_message;
    size_t msg_len;

    /* Check server_running under lock to avoid racing with start/stop */
    pthread_mutex_lock(&transport_mutex);
    running = server_running;
    pthread_mutex_unlock(&transport_mutex);

    if (!running) {
        log_warning(mcp_transport_log, "Cannot send SSE event - server not running");
        return -1;
    }

    /* Clean up any stale connections before attempting to send */
    cleanup_stale_sse_connections();

    /* Format SSE message: "event: <type>\ndata: <data>\n\n" */
    /* Message format overhead: "event: " (7) + "\n" (1) + "data: " (6) + "\n\n" (2) = 16 bytes
     * Plus null terminator (1) and safety margin (4) = 21 bytes */
    msg_len = strlen(event_type) + strlen(data) + 21;
    event_message = (char *)lib_malloc(msg_len);

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

    lib_free(event_message);

    if (active_count > 0) {
        log_message(mcp_transport_log, "SSE event broadcast to %d connections (logged only - Phase 2 needed for actual send)", active_count);
    } else {
        log_message(mcp_transport_log, "No active SSE connections - event not sent");
    }

    return active_count;
}
