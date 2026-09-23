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

/* Platform-specific network and threading headers.
 *
 * Windows (MinGW-w64): winsock2.h/ws2tcpip.h for sockets, winpthreads for threading.
 * POSIX (Linux/macOS): native socket and pthread headers.
 *
 * winsock2.h MUST be included before windows.h to avoid winsock1 conflicts.
 */
#ifdef WINDOWS_COMPILE
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#  include <errno.h>  /* For ETIMEDOUT */
#else
#  error "MCP transport requires pthreads"
#endif

#ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
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

/* Initial request body buffer size - most JSON-RPC requests are small */
#define INITIAL_BODY_CAPACITY 1024

/* Connection timeout in seconds - prevents slow/stalled connections from holding resources */
#define CONNECTION_TIMEOUT_SEC 30

static log_t mcp_transport_log = LOG_DEFAULT;

/* Mutex for thread-safe access to static state (server, connections) */
static pthread_mutex_t transport_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Serialize MCP tool dispatches across libmicrohttpd worker threads.
 * The emulator trap queue can hold multiple traps, but tool handlers are not
 * designed to run concurrently against emulator state. Keeping one in-flight
 * MCP dispatch prevents high-frequency clients from building a trap backlog
 * that can time out under warp-mode polling.
 */
static pthread_mutex_t dispatch_mutex = PTHREAD_MUTEX_INITIALIZER;
static int test_count_trap_dispatches = 0;
static int test_trap_dispatch_count = 0;

/* HTTP server state - owned by mcp_transport, cleaned up in shutdown */
static struct MHD_Daemon *http_daemon = NULL;
static int server_running = 0;  /* 1 when HTTP server is active */
static char *transport_auth_token = NULL;       /* Optional bearer token, copied at start */
static char *transport_cors_origin = NULL;      /* Optional exact CORS origin, copied at start */

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

static int mcp_string_is_set(const char *value)
{
    return value != NULL && value[0] != '\0';
}

static int mcp_constant_time_streq(const char *a, const char *b)
{
    size_t a_len, b_len, max_len, i;
    unsigned char diff;

    if (a == NULL || b == NULL) {
        return 0;
    }

    a_len = strlen(a);
    b_len = strlen(b);
    max_len = (a_len > b_len) ? a_len : b_len;
    diff = (unsigned char)(a_len ^ b_len);

    for (i = 0; i < max_len; i++) {
        unsigned char ac = (i < a_len) ? (unsigned char)a[i] : 0;
        unsigned char bc = (i < b_len) ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(ac ^ bc);
    }

    return diff == 0;
}

static int mcp_auth_configured(void)
{
    return mcp_string_is_set(transport_auth_token);
}

static void mcp_transport_settings_clear(void)
{
    if (transport_auth_token != NULL) {
        lib_free(transport_auth_token);
        transport_auth_token = NULL;
    }
    if (transport_cors_origin != NULL) {
        lib_free(transport_cors_origin);
        transport_cors_origin = NULL;
    }
}

static int mcp_is_loopback_address(const struct in_addr *addr)
{
    uint32_t host_addr;

    if (addr == NULL) {
        return 0;
    }

    host_addr = ntohl(addr->s_addr);
    return (host_addr & 0xff000000U) == 0x7f000000U;
}

static int mcp_request_is_authorized(struct MHD_Connection *connection)
{
    const char *authorization;
    const char *prefix = "Bearer ";
    size_t prefix_len = 7;

    if (!mcp_auth_configured()) {
        return 1;
    }

    authorization = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Authorization");
    if (authorization == NULL || strncasecmp(authorization, prefix, prefix_len) != 0) {
        return 0;
    }

    return mcp_constant_time_streq(authorization + prefix_len, transport_auth_token);
}

static int mcp_cors_origin_allowed(struct MHD_Connection *connection)
{
    const char *origin;

    if (!mcp_string_is_set(transport_cors_origin)) {
        return 0;
    }

    origin = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Origin");
    return origin != NULL && strcmp(origin, transport_cors_origin) == 0;
}

static void mcp_add_cors_headers(struct MHD_Response *response,
                                 struct MHD_Connection *connection)
{
    if (mcp_cors_origin_allowed(connection)) {
        MHD_add_response_header(response, "Access-Control-Allow-Origin", transport_cors_origin);
        MHD_add_response_header(response, "Vary", "Origin");
    }
}

static enum MHD_Result mcp_queue_json_response(struct MHD_Connection *connection,
                                               unsigned int status_code,
                                               const char *json_body)
{
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(
        strlen(json_body),
        (void *)json_body,
        MHD_RESPMEM_PERSISTENT);
    if (response == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(response, "Content-Type", "application/json");
    mcp_add_cors_headers(response, connection);
    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result mcp_queue_auth_required(struct MHD_Connection *connection)
{
    const char *error_msg = "{\"error\":\"Unauthorized\",\"message\":\"MCP bearer token required\"}";
    struct MHD_Response *response;
    enum MHD_Result ret;

    response = MHD_create_response_from_buffer(
        strlen(error_msg),
        (void *)error_msg,
        MHD_RESPMEM_PERSISTENT);
    if (response == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(response, "Content-Type", "application/json");
    MHD_add_response_header(response, "WWW-Authenticate", "Bearer");
    mcp_add_cors_headers(response, connection);
    ret = MHD_queue_response(connection, 401, response);
    MHD_destroy_response(response);
    return ret;
}

static enum MHD_Result mcp_handle_options_request(struct MHD_Connection *connection)
{
    struct MHD_Response *response;
    enum MHD_Result ret;

    if (!mcp_cors_origin_allowed(connection)) {
        return mcp_queue_json_response(connection, 403,
            "{\"error\":\"Forbidden\",\"message\":\"CORS origin is not allowed\"}");
    }

    response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
    if (response == NULL) {
        return MHD_NO;
    }

    MHD_add_response_header(response, "Access-Control-Allow-Origin", transport_cors_origin);
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    MHD_add_response_header(response, "Access-Control-Max-Age", "86400");
    MHD_add_response_header(response, "Vary", "Origin");

    ret = MHD_queue_response(connection, 204, response);
    MHD_destroy_response(response);
    return ret;
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

    /* If the HTTP thread timed out before this trap fired, do not execute a
     * stale tool call later against a newer emulator state.
     */
    pthread_mutex_lock(&req->mutex);
    if (req->abandoned) {
        pthread_mutex_unlock(&req->mutex);
        mcp_trap_request_free(req);
        return;
    }
    pthread_mutex_unlock(&req->mutex);

    /* Execute tool on main thread - thread safe! */
    if (test_count_trap_dispatches) {
        test_trap_dispatch_count++;
    }
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
    cJSON *serialized_response;

    log_message(mcp_transport_log, "Waiting for MCP dispatch slot: %s", tool_name);
    pthread_mutex_lock(&dispatch_mutex);
    log_message(mcp_transport_log, "Acquired MCP dispatch slot: %s", tool_name);

    /* If we're inside the monitor, the main loop is blocked waiting for input.
     * In this case, we can safely dispatch directly since there's no concurrent
     * execution. This also handles the case where traps won't fire. */
    if (monitor_is_inside_monitor()) {
        log_message(mcp_transport_log, "Monitor active - using direct dispatch for: %s", tool_name);
        serialized_response = mcp_tools_dispatch(tool_name, params);
        pthread_mutex_unlock(&dispatch_mutex);
        return serialized_response;
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
            pthread_mutex_unlock(&dispatch_mutex);
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
                    pthread_mutex_unlock(&dispatch_mutex);
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

        /* Queue trap to execute on main thread.
         *
         * interrupt_maincpu_trigger_trap() manipulates the CPU's trap queue
         * (traps_count, traps_next and a lib_realloc'd function/data array)
         * with no lock of its own. Every other caller in VICE runs on the
         * emulator thread, so upstream never needed one - but we are the
         * HTTP thread, and interrupt_do_trap() may be walking that very
         * array right now. Racing it hands the dispatcher a stale function
         * pointer or a stale data pointer, which then lands in
         * mcp_trap_request_free() as a bogus lib_free(): heap abort, or a
         * segfault inside mcp_trap_handler, or - once the free list is
         * damaged - an abort in some later, innocent allocation.
         *
         * The mainlock is VICE's own answer to "another thread needs to
         * touch emulator state": the emulator thread holds it while running
         * and yields it periodically. Hold it just for the enqueue, and
         * release it BEFORE waiting on the condition - otherwise the
         * emulator cannot run the trap we are waiting for. */
        log_message(mcp_transport_log, "Queuing trap dispatch for: %s", tool_name);
        mainlock_obtain();
        interrupt_maincpu_trigger_trap(mcp_trap_handler, req);
        mainlock_release();

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
                /* The trap may have completed in the instant between the
                 * deadline expiring and us re-acquiring the mutex. Then
                 * nobody owns the request any more: the handler has already
                 * returned without freeing (it saw abandoned == 0), and we
                 * would walk away from it. Re-check before giving up. */
                if (req->complete) {
                    break;
                }
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
                    pthread_mutex_unlock(&dispatch_mutex);
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
                    pthread_mutex_unlock(&dispatch_mutex);
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
        pthread_mutex_unlock(&dispatch_mutex);

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

/* Called when request processing is complete */
static void request_completed(void *cls,
                              struct MHD_Connection *connection,
                              void **con_cls,
                              enum MHD_RequestTerminationCode toe)
{
    struct request_context_s *ctx;

    (void)cls;
    (void)connection;
    (void)toe;

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
        return mcp_handle_options_request(connection);
    }

    /* Route requests */
    if (strcmp(url, "/mcp") == 0 && strcmp(method, "POST") == 0) {
        struct request_context_s *ctx = (struct request_context_s *)*con_cls;

        /* First call - initialize context and validate headers */
        if (ctx == NULL) {
            const char *content_type;
            const char *accept;

            if (!mcp_request_is_authorized(connection)) {
                log_warning(mcp_transport_log, "Unauthorized MCP POST request");
                return mcp_queue_auth_required(connection);
            }

            /* Validate Content-Type header */
            content_type = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Content-Type");

            if (content_type == NULL ||
                strncasecmp(content_type, "application/json", 16) != 0) {
                log_warning(mcp_transport_log, "Invalid Content-Type for /mcp: %s (expected application/json)",
                           content_type ? content_type : "(none)");

                return mcp_queue_json_response(connection, 415,
                    "{\"error\":\"Unsupported Media Type\",\"message\":\"Content-Type must be application/json\"}");
            }

            /* Validate Accept header - POST /mcp returns JSON, so only require application/json.
             * A missing Accept header implies the client accepts any media type (RFC 7231). */
            accept = MHD_lookup_connection_value(
                connection, MHD_HEADER_KIND, "Accept");

            if (accept != NULL &&
                strstr(accept, "application/json") == NULL &&
                strstr(accept, "*/*") == NULL) {
                log_warning(mcp_transport_log, "Invalid Accept header for POST /mcp: %s (must accept application/json)",
                           accept ? accept : "(none)");

                return mcp_queue_json_response(connection, 406,
                    "{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32000,\"message\":\"Not Acceptable: Client must accept application/json\"}}");
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
                mcp_add_cors_headers(response, connection);
                ret = MHD_queue_response(connection, 202, response);
                MHD_destroy_response(response);
                return ret;
            }

            /* POST /mcp always returns plain JSON.
             * response_json is either:
             * - malloc'd by cJSON_PrintUnformatted: MHD_RESPMEM_MUST_FREE is correct
             *   (MHD will call free(), matching cJSON's malloc)
             * - static CATASTROPHIC_ERROR_JSON: MHD_RESPMEM_PERSISTENT is correct */
            {
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

            MHD_add_response_header(response, "Content-Type", "application/json");
            mcp_add_cors_headers(response, connection);

            /* Queue response */
            ret = MHD_queue_response(connection, 200, response);
            MHD_destroy_response(response);

            return ret;
        }
    } else if ((strcmp(url, "/mcp") == 0 || strcmp(url, "/events") == 0) && strcmp(method, "GET") == 0) {
        if (!mcp_request_is_authorized(connection)) {
            log_warning(mcp_transport_log, "Unauthorized MCP GET request");
            return mcp_queue_auth_required(connection);
        }

        return mcp_queue_json_response(connection, 501,
            "{\"error\":\"Not Implemented\",\"message\":\"SSE streaming is not implemented in this transport\"}");
    } else {
        /* 404 Not Found */
        return mcp_queue_json_response(connection, 404, "{\"error\":\"Not Found\"}");
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
    pthread_mutex_init(&dispatch_mutex, NULL);

    /* HTTP server initialization happens in mcp_transport_start() */

    log_message(mcp_transport_log, "MCP transport initialized");

    return 0;
}

void mcp_transport_shutdown(void)
{
    log_message(mcp_transport_log, "MCP transport shutting down...");

    mcp_transport_settings_clear();

    /* Destroy mutex */
    pthread_mutex_destroy(&dispatch_mutex);
    pthread_mutex_destroy(&transport_mutex);

    log_message(mcp_transport_log, "MCP transport shut down");
}

int mcp_transport_start(const char *host, int port,
                        const char *auth_token, const char *cors_origin)
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

    mcp_transport_settings_clear();
    if (mcp_string_is_set(auth_token)) {
        transport_auth_token = lib_strdup(auth_token);
    }
    if (mcp_string_is_set(cors_origin)) {
        if (strcmp(cors_origin, "*") == 0) {
            mcp_transport_settings_clear();
            pthread_mutex_unlock(&transport_mutex);
            log_error(mcp_transport_log, "Wildcard CORS origin is not allowed");
            return -1;
        }
        transport_cors_origin = lib_strdup(cors_origin);
    }

    if (!mcp_is_loopback_address(&addr.sin_addr) && !mcp_auth_configured()) {
        log_warning(mcp_transport_log,
                    "MCP server binding to non-loopback address %s without MCPServerToken; "
                    "remote clients can control this emulator",
                    host);
    }

    if (mcp_string_is_set(transport_cors_origin) && !mcp_auth_configured()) {
        mcp_transport_settings_clear();
        pthread_mutex_unlock(&transport_mutex);
        log_error(mcp_transport_log,
                  "Refusing CORS-enabled MCP server without MCPServerToken");
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
        mcp_transport_settings_clear();
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
     * for connection handler threads to finish. */
    local_daemon = http_daemon;
    http_daemon = NULL;
    server_running = 0;

    pthread_mutex_unlock(&transport_mutex);

    /* Stop HTTP daemon outside the lock to avoid deadlock */
    if (local_daemon != NULL) {
        MHD_stop_daemon(local_daemon);
    }

    pthread_mutex_lock(&transport_mutex);
    mcp_transport_settings_clear();
    pthread_mutex_unlock(&transport_mutex);

    log_message(mcp_transport_log, "MCP transport stopped");
}

int mcp_transport_sse_send_event(const char *event_type, const char *data)
{
    (void)data;

    log_warning(mcp_transport_log,
                "SSE event '%s' not sent: SSE streaming is not implemented",
                event_type ? event_type : "(null)");
    return -1;
}

int mcp_transport_test_dispatch_mutex_serializes(void)
{
    if (pthread_mutex_trylock(&dispatch_mutex) != 0) {
        return 0;
    }

    if (pthread_mutex_trylock(&dispatch_mutex) == 0) {
        pthread_mutex_unlock(&dispatch_mutex);
        pthread_mutex_unlock(&dispatch_mutex);
        return 0;
    }

    pthread_mutex_unlock(&dispatch_mutex);

    if (pthread_mutex_trylock(&dispatch_mutex) != 0) {
        return 0;
    }

    pthread_mutex_unlock(&dispatch_mutex);
    return 1;
}

static mcp_trap_request_t *mcp_transport_test_request_new(const char *tool_name,
                                                          int abandoned)
{
    mcp_trap_request_t *req;

    req = (mcp_trap_request_t *)lib_malloc(sizeof(mcp_trap_request_t));
    req->tool_name = lib_strdup(tool_name);
    req->params = NULL;
    req->response = NULL;
    req->complete = 0;
    req->abandoned = abandoned ? 1 : 0;
    pthread_mutex_init(&req->mutex, NULL);
    pthread_cond_init(&req->cond, NULL);

    return req;
}

int mcp_transport_test_abandoned_trap_skips_dispatch(void)
{
    mcp_trap_request_t *req;
    int skipped;

    test_count_trap_dispatches = 1;
    test_trap_dispatch_count = 0;

    req = mcp_transport_test_request_new("vice.ping", 1);
    mcp_trap_handler(0, req);

    skipped = (test_trap_dispatch_count == 0);
    test_count_trap_dispatches = 0;
    test_trap_dispatch_count = 0;

    return skipped;
}

int mcp_transport_test_active_trap_dispatches_once(void)
{
    mcp_trap_request_t *req;
    int complete;
    int has_response;
    int dispatch_count;

    test_count_trap_dispatches = 1;
    test_trap_dispatch_count = 0;

    req = mcp_transport_test_request_new("vice.ping", 0);
    mcp_trap_handler(0, req);

    pthread_mutex_lock(&req->mutex);
    complete = req->complete;
    pthread_mutex_unlock(&req->mutex);

    has_response = (req->response != NULL);
    dispatch_count = test_trap_dispatch_count;

    mcp_trap_request_free(req);
    test_count_trap_dispatches = 0;
    test_trap_dispatch_count = 0;

    return complete && has_response && dispatch_count == 1;
}

int mcp_transport_test_all_interfaces_without_token_starts(void)
{
    int started;

    started = (mcp_transport_start("0.0.0.0", 0, NULL, NULL) == 0);
    if (started) {
        mcp_transport_stop();
    }

    return started;
}

int mcp_transport_test_cors_without_token_rejected(void)
{
    int rejected;

    rejected = (mcp_transport_start("127.0.0.1", 0, NULL, "http://localhost:3000") < 0);
    if (!rejected) {
        mcp_transport_stop();
    }

    return rejected;
}
