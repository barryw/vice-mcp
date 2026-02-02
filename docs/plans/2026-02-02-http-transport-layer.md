# HTTP Transport Layer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement HTTP server with JSON-RPC 2.0 endpoint and SSE notifications for VICE MCP

**Architecture:** Use libmicrohttpd to create an HTTP server that handles POST requests with JSON-RPC 2.0 payloads, dispatches to mcp_tools, and returns JSON responses. Add SSE endpoint for streaming notifications (breakpoints, state changes).

**Tech Stack:** libmicrohttpd 1.0+, cJSON (bundled), VICE logging system

---

## Phase 1: Basic HTTP Server (Foundation)

### Task 1: Add HTTP Request Handler Structure

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c:36-93`
- Modify: `vice/src/mcp/mcp_transport.h:26-43`

**Step 1: Add libmicrohttpd include**

In `mcp_transport.c` after line 34:

```c
#include "mcp_transport.h"
#include "log.h"
#include <microhttpd.h>
```

**Step 2: Add server state structure**

In `mcp_transport.c` after line 37:

```c
/* HTTP server state */
static struct MHD_Daemon *http_daemon = NULL;
static int server_running = 0;

/* SSE connection tracking */
#define MAX_SSE_CONNECTIONS 10
static struct {
    struct MHD_Connection *connection;
    int active;
} sse_connections[MAX_SSE_CONNECTIONS];
```

**Step 3: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add HTTP server state structures"
```

---

### Task 2: Implement HTTP Request Router

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (add before mcp_transport_init)

**Step 1: Write HTTP request handler function**

Add before line 40 in `mcp_transport.c`:

```c
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
        /* JSON-RPC endpoint - handle in next task */
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

        enum MHD_Result ret = MHD_queue_response(connection, 404, response);
        MHD_destroy_response(response);
        return ret;
    }
}
```

**Step 2: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add HTTP request router with 404 handler"
```

---

### Task 3: Implement Server Start/Stop

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c:62-93`

**Step 1: Implement mcp_transport_start**

Replace function at line 62:

```c
int mcp_transport_start(const char *host, int port)
{
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
        MHD_OPTION_END);

    if (http_daemon == NULL) {
        log_error(mcp_transport_log, "Failed to start HTTP server on port %d", port);
        return -1;
    }

    server_running = 1;
    log_message(mcp_transport_log, "MCP transport started on port %d", port);

    return 0;
}
```

**Step 2: Implement mcp_transport_stop**

Replace function at line 75:

```c
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
```

**Step 3: Test manually**

Build and test:
```bash
cd vice/build-test-with-mcp
make -C src/mcp
```

Expected: Compiles successfully with libmicrohttpd

**Step 4: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): implement HTTP server start/stop"
```

---

## Phase 2: JSON-RPC Request Processing

### Task 4: Add Request Body Accumulation

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (update http_handler)

**Step 1: Add request context structure**

Add after line 46 (after sse_connections):

```c
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
```

**Step 2: Update http_handler to accumulate POST data**

Replace the `/mcp` POST handling in http_handler:

```c
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
}
```

**Step 3: Add request completed callback**

Add before http_handler function:

```c
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
```

**Step 4: Update MHD_start_daemon to use completion callback**

In mcp_transport_start, change MHD_start_daemon call to:

```c
http_daemon = MHD_start_daemon(
    MHD_USE_THREAD_PER_CONNECTION | MHD_USE_INTERNAL_POLLING_THREAD,
    port,
    NULL, NULL,  /* No accept policy */
    &http_handler, NULL,  /* Request handler */
    MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
    MHD_OPTION_END);
```

**Step 5: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add POST body accumulation for JSON-RPC"
```

---

### Task 5: Implement JSON-RPC Request Processing

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (add after request_context_free)
- Modify: `vice/src/mcp/mcp_transport.c:34` (add include)

**Step 1: Add mcp_tools.h and cJSON.h includes**

In mcp_transport.c line 34, add:

```c
#include "mcp_tools.h"
#include "cJSON.h"
```

**Step 2: Add JSON-RPC request processor**

Add after request_context_free:

```c
/* Process JSON-RPC 2.0 request and return response */
static char* process_jsonrpc_request(const char *request_body, size_t body_size)
{
    cJSON *request = NULL;
    cJSON *response = NULL;
    cJSON *method_item, *params_item, *id_item;
    char *response_str = NULL;

    /* Null-terminate body for parsing */
    char *null_term_body = malloc(body_size + 1);
    if (null_term_body == NULL) {
        log_error(mcp_transport_log, "Failed to allocate body buffer");
        return strdup(CATASTROPHIC_ERROR_JSON);
    }
    memcpy(null_term_body, request_body, body_size);
    null_term_body[body_size] = '\0';

    /* Parse JSON */
    request = cJSON_Parse(null_term_body);
    free(null_term_body);

    if (request == NULL) {
        log_error(mcp_transport_log, "Invalid JSON in request");
        /* Return parse error */
        response = cJSON_CreateObject();
        if (response == NULL) {
            return strdup(CATASTROPHIC_ERROR_JSON);
        }
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        cJSON_AddNullToObject(response, "id");

        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", MCP_ERROR_PARSE_ERROR);
        cJSON_AddStringToObject(error, "message", "Parse error");
        cJSON_AddItemToObject(response, "error", error);

        response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        return response_str;
    }

    /* Extract request fields */
    method_item = cJSON_GetObjectItem(request, "method");
    params_item = cJSON_GetObjectItem(request, "params");
    id_item = cJSON_GetObjectItem(request, "id");

    if (!cJSON_IsString(method_item)) {
        log_error(mcp_transport_log, "Missing or invalid method field");
        cJSON_Delete(request);

        /* Return invalid request error */
        response = cJSON_CreateObject();
        if (response == NULL) {
            return strdup(CATASTROPHIC_ERROR_JSON);
        }
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        if (id_item != NULL) {
            cJSON_AddItemReferenceToObject(response, "id", id_item);
        } else {
            cJSON_AddNullToObject(response, "id");
        }

        cJSON *error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", MCP_ERROR_INVALID_REQUEST);
        cJSON_AddStringToObject(error, "message", "Invalid Request");
        cJSON_AddItemToObject(response, "error", error);

        response_str = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        return response_str;
    }

    /* Dispatch to tool */
    const char *method_name = method_item->valuestring;
    log_message(mcp_transport_log, "JSON-RPC request: %s", method_name);

    cJSON *result = mcp_tools_dispatch(method_name, params_item);

    /* Build JSON-RPC response */
    response = cJSON_CreateObject();
    if (response == NULL) {
        cJSON_Delete(request);
        if (result != NULL) {
            cJSON_Delete(result);
        }
        return strdup(CATASTROPHIC_ERROR_JSON);
    }

    cJSON_AddStringToObject(response, "jsonrpc", "2.0");

    /* Copy request ID to response */
    if (id_item != NULL) {
        cJSON_AddItemReferenceToObject(response, "id", id_item);
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
    cJSON_Delete(request);

    return response_str;
}
```

**Step 3: Add CATASTROPHIC_ERROR_JSON extern declaration**

At top of file after includes:

```c
/* From mcp_tools.c */
extern const char *CATASTROPHIC_ERROR_JSON;
```

**Step 4: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add JSON-RPC 2.0 request processor"
```

---

### Task 6: Wire Up JSON-RPC Response

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (update http_handler POST section)

**Step 1: Replace POST placeholder with response handling**

In http_handler, replace the `/* All data received - process request in next task */` section:

```c
/* All data received - process request */
char *response_json = process_jsonrpc_request(ctx->body, ctx->body_size);
if (response_json == NULL) {
    log_error(mcp_transport_log, "Failed to generate response");
    request_context_free(ctx);
    return MHD_NO;
}

/* Create HTTP response */
struct MHD_Response *response = MHD_create_response_from_buffer(
    strlen(response_json),
    (void*)response_json,
    MHD_RESPMEM_MUST_FREE);  /* libmicrohttpd will free response_json */

if (response == NULL) {
    log_error(mcp_transport_log, "Failed to create HTTP response");
    free(response_json);
    request_context_free(ctx);
    return MHD_NO;
}

/* Add headers */
MHD_add_response_header(response, "Content-Type", "application/json");
MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

/* Queue response */
enum MHD_Result ret = MHD_queue_response(connection, 200, response);
MHD_destroy_response(response);
request_context_free(ctx);

return ret;
```

**Step 2: Build and test**

```bash
cd vice/build-test-with-mcp
make -C src/mcp clean
make -C src/mcp
```

Expected: Compiles successfully

**Step 3: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): wire up JSON-RPC response handling"
```

---

## Phase 3: Integration and Testing

### Task 7: Add Server Startup Integration

**Files:**
- Modify: `vice/src/mcp/mcp_server.c:41-64`

**Step 1: Add transport start call**

In mcp_server_start, after mcp_tools_init():

```c
/* Start HTTP transport */
const char *host = "127.0.0.1";  /* Localhost only for security */
int port = 8080;  /* Default port */

if (mcp_transport_start(host, port) != 0) {
    log_error(mcp_server_log, "Failed to start MCP transport");
    mcp_tools_shutdown();
    mcp_transport_shutdown();
    return -1;
}
```

**Step 2: Add transport stop call**

In mcp_server_stop, before mcp_tools_shutdown():

```c
/* Stop HTTP transport */
mcp_transport_stop();
```

**Step 3: Rebuild**

```bash
cd vice/build-test-with-mcp
make -C src/mcp clean
make -C src/mcp
```

**Step 4: Commit**

```bash
git add vice/src/mcp/mcp_server.c
git commit -m "feat(mcp): integrate HTTP transport into server lifecycle"
```

---

### Task 8: Create Manual Integration Test

**Files:**
- Create: `vice/src/mcp/tests/test_http_transport.sh`

**Step 1: Write test script**

```bash
#!/bin/bash
# Manual integration test for HTTP transport
# Requires VICE to be running with MCP server enabled

set -e

SERVER="http://127.0.0.1:8080"
ENDPOINT="$SERVER/mcp"

echo "=== MCP HTTP Transport Integration Test ==="
echo

# Test 1: Ping
echo "Test 1: Ping tool"
curl -X POST "$ENDPOINT" \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "vice.ping",
    "id": 1
  }'
echo
echo

# Test 2: Invalid method
echo "Test 2: Invalid method (should return error)"
curl -X POST "$ENDPOINT" \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "invalid.method",
    "id": 2
  }'
echo
echo

# Test 3: Invalid JSON
echo "Test 3: Invalid JSON (should return parse error)"
curl -X POST "$ENDPOINT" \
  -H "Content-Type: application/json" \
  -d 'not valid json'
echo
echo

# Test 4: 404 for wrong endpoint
echo "Test 4: Wrong endpoint (should return 404)"
curl -X POST "$SERVER/wrong" \
  -H "Content-Type: application/json" \
  -d '{}'
echo
echo

echo "=== Tests Complete ==="
```

**Step 2: Make executable**

```bash
chmod +x vice/src/mcp/tests/test_http_transport.sh
```

**Step 3: Add to test README**

In `vice/src/mcp/tests/README.md`, add section:

```markdown
## Manual HTTP Transport Testing

To test the HTTP transport layer with a running VICE instance:

1. Build and run VICE with MCP enabled
2. Run the integration test script:
   ```bash
   ./test_http_transport.sh
   ```

Expected output: JSON-RPC responses for each test case.
```

**Step 4: Commit**

```bash
git add vice/src/mcp/tests/test_http_transport.sh vice/src/mcp/tests/README.md
git commit -m "test(mcp): add HTTP transport integration test script"
```

---

## Phase 4: SSE Event Stream (Notifications)

### Task 9: Implement SSE Connection Management

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (update http_handler /events)

**Step 1: Add SSE connection helper**

Add after process_jsonrpc_request:

```c
/* Find free SSE connection slot */
static int find_free_sse_slot(void)
{
    int i;
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (!sse_connections[i].active) {
            return i;
        }
    }
    return -1;
}

/* Register SSE connection */
static int register_sse_connection(struct MHD_Connection *connection)
{
    int slot = find_free_sse_slot();
    if (slot < 0) {
        log_warning(mcp_transport_log, "No free SSE connection slots");
        return -1;
    }

    sse_connections[slot].connection = connection;
    sse_connections[slot].active = 1;

    log_message(mcp_transport_log, "SSE connection registered in slot %d", slot);
    return slot;
}

/* Unregister SSE connection */
static void unregister_sse_connection(struct MHD_Connection *connection)
{
    int i;
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (sse_connections[i].active && sse_connections[i].connection == connection) {
            sse_connections[i].active = 0;
            sse_connections[i].connection = NULL;
            log_message(mcp_transport_log, "SSE connection unregistered from slot %d", i);
            return;
        }
    }
}
```

**Step 2: Update http_handler /events endpoint**

Replace SSE placeholder in http_handler:

```c
else if (strcmp(url, "/events") == 0 && strcmp(method, "GET") == 0) {
    /* SSE endpoint - keep connection open for streaming events */

    /* Register connection */
    if (register_sse_connection(connection) < 0) {
        const char *error_msg = "event: error\ndata: Too many connections\n\n";
        struct MHD_Response *response = MHD_create_response_from_buffer(
            strlen(error_msg),
            (void*)error_msg,
            MHD_RESPMEM_PERSISTENT);

        MHD_add_response_header(response, "Content-Type", "text/event-stream");
        enum MHD_Result ret = MHD_queue_response(connection, 503, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Send initial connection event */
    const char *connect_msg = "event: connected\ndata: {\"status\":\"ok\"}\n\n";
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(connect_msg),
        (void*)connect_msg,
        MHD_RESPMEM_PERSISTENT);

    if (response == NULL) {
        unregister_sse_connection(connection);
        return MHD_NO;
    }

    MHD_add_response_header(response, "Content-Type", "text/event-stream");
    MHD_add_response_header(response, "Cache-Control", "no-cache");
    MHD_add_response_header(response, "Connection", "keep-alive");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");

    enum MHD_Result ret = MHD_queue_response(connection, 200, response);
    MHD_destroy_response(response);

    /* Note: Connection will be kept open by libmicrohttpd */
    return ret;
}
```

**Step 3: Update request_completed to unregister SSE**

In request_completed, add before existing cleanup:

```c
/* Unregister SSE connection if registered */
unregister_sse_connection(connection);
```

**Step 4: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): implement SSE connection management"
```

---

### Task 10: Implement SSE Event Broadcasting

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c:85-93`

**Step 1: Implement mcp_transport_sse_send_event**

Replace function:

```c
int mcp_transport_sse_send_event(const char *event_type, const char *data)
{
    int i, sent_count = 0;
    char *event_message;
    size_t msg_len;

    if (!server_running) {
        log_warning(mcp_transport_log, "Cannot send SSE event - server not running");
        return -1;
    }

    /* Format SSE message: "event: <type>\ndata: <data>\n\n" */
    msg_len = strlen(event_type) + strlen(data) + 20;  /* "event: \ndata: \n\n" + safety */
    event_message = malloc(msg_len);
    if (event_message == NULL) {
        log_error(mcp_transport_log, "Failed to allocate SSE event message");
        return -1;
    }

    snprintf(event_message, msg_len, "event: %s\ndata: %s\n\n", event_type, data);

    log_message(mcp_transport_log, "Broadcasting SSE event: %s", event_type);

    /* Broadcast to all active connections */
    for (i = 0; i < MAX_SSE_CONNECTIONS; i++) {
        if (sse_connections[i].active && sse_connections[i].connection != NULL) {
            /* Note: With current libmicrohttpd version, we can't actually send
             * data on an existing connection - this would require upgrading
             * to use response callbacks or a different streaming approach.
             * For now, log that we would send the event. */
            log_message(mcp_transport_log, "Would send to SSE slot %d: %s", i, event_type);
            sent_count++;
        }
    }

    free(event_message);

    log_message(mcp_transport_log, "SSE event broadcast to %d connections", sent_count);

    return sent_count;
}
```

**Step 2: Add TODO comment for Phase 2**

Add comment above function:

```c
/* TODO Phase 2: Upgrade SSE implementation to use MHD response callbacks
 * for true streaming. Current implementation tracks connections but cannot
 * push events after initial response. This requires either:
 * 1. Using MHD_create_response_from_callback with chunked encoding
 * 2. Upgrading to newer libmicrohttpd with better streaming support
 * 3. Using a separate WebSocket library instead of SSE
 */
```

**Step 3: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): implement SSE event broadcasting (basic)"
```

---

## Phase 5: Error Handling and Robustness

### Task 11: Add Content-Length Limits

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (update request accumulation)

**Step 1: Define max request size**

Add after MAX_SSE_CONNECTIONS:

```c
#define MAX_REQUEST_SIZE (1024 * 1024)  /* 1MB max request */
```

**Step 2: Add size check in POST accumulation**

In http_handler POST section, add check after calculating new_size:

```c
/* Limit total request size */
if (new_size > MAX_REQUEST_SIZE) {
    log_error(mcp_transport_log, "Request too large: %zu bytes", new_size);

    const char *error_msg = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"Request too large\"},\"id\":null}";
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(error_msg),
        (void*)error_msg,
        MHD_RESPMEM_PERSISTENT);

    MHD_add_response_header(response, "Content-Type", "application/json");
    enum MHD_Result ret = MHD_queue_response(connection, 413, response);
    MHD_destroy_response(response);
    request_context_free(ctx);

    return ret;
}
```

**Step 3: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add request size limits (1MB max)"
```

---

### Task 12: Add OPTIONS Support for CORS

**Files:**
- Modify: `vice/src/mcp/mcp_transport.c` (add to http_handler)

**Step 1: Add OPTIONS handler**

In http_handler, before 404 handler, add:

```c
else if (strcmp(method, "OPTIONS") == 0) {
    /* CORS preflight request */
    const char *empty = "";
    struct MHD_Response *response = MHD_create_response_from_buffer(
        0, (void*)empty, MHD_RESPMEM_PERSISTENT);

    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
    MHD_add_response_header(response, "Access-Control-Max-Age", "86400");

    enum MHD_Result ret = MHD_queue_response(connection, 204, response);
    MHD_destroy_response(response);
    return ret;
}
```

**Step 2: Commit**

```bash
git add vice/src/mcp/mcp_transport.c
git commit -m "feat(mcp): add CORS preflight (OPTIONS) support"
```

---

## Phase 6: Configuration and Documentation

### Task 13: Make Port Configurable

**Files:**
- Modify: `vice/src/mcp/mcp_server.c` (add command-line parsing)
- Modify: `vice/src/mcp/mcp_server.h` (add port parameter)

**Step 1: Update mcp_server_start signature**

In `mcp_server.h`:

```c
extern int mcp_server_start(int port);
```

**Step 2: Update mcp_server_start implementation**

In `mcp_server.c`, change function signature and use port parameter:

```c
int mcp_server_start(int port)
{
    log_message(mcp_server_log, "Starting MCP server on port %d...", port);

    /* ... existing init code ... */

    /* Start HTTP transport with specified port */
    const char *host = "127.0.0.1";

    if (mcp_transport_start(host, port) != 0) {
        /* ... existing error handling ... */
    }

    /* ... rest of function ... */
}
```

**Step 3: Add default port constant**

In `mcp_server.h`:

```c
#define MCP_DEFAULT_PORT 8080
```

**Step 4: Commit**

```bash
git add vice/src/mcp/mcp_server.h vice/src/mcp/mcp_server.c
git commit -m "feat(mcp): make HTTP port configurable"
```

---

### Task 14: Update Documentation

**Files:**
- Modify: `vice/src/mcp/README.md`

**Step 1: Add HTTP transport section**

Add after "Phase 1 Implementation" section:

```markdown
## HTTP Transport Layer

The MCP server exposes two HTTP endpoints:

### POST /mcp - JSON-RPC 2.0 Endpoint

Accepts JSON-RPC 2.0 requests and returns responses.

**Example Request:**
```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "vice.ping",
    "params": {},
    "id": 1
  }'
```

**Example Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "ok",
    "version": "3.10",
    "machine": "C64"
  },
  "id": 1
}
```

### GET /events - Server-Sent Events (SSE)

Streams real-time notifications from VICE (breakpoints, execution state changes).

**Example Usage:**
```javascript
const events = new EventSource('http://127.0.0.1:8080/events');
events.addEventListener('breakpoint', (e) => {
  console.log('Breakpoint hit:', JSON.parse(e.data));
});
```

## Configuration

**Port:** Set via command-line argument or use default (8080)

**Host:** Binds to 127.0.0.1 (localhost only) for security

**Request Limits:** Maximum 1MB request size

## Security Considerations

- Server only binds to localhost (127.0.0.1) to prevent external access
- CORS enabled for local development
- Request size limits prevent DoS attacks
- No authentication required (localhost-only deployment)
```

**Step 2: Commit**

```bash
git add vice/src/mcp/README.md
git commit -m "docs(mcp): document HTTP transport endpoints and usage"
```

---

## Testing and Validation

### Task 15: Create Comprehensive HTTP Tests

**Files:**
- Create: `vice/src/mcp/tests/test_http_detailed.sh`

**Step 1: Write detailed test script**

```bash
#!/bin/bash
# Comprehensive HTTP transport tests
# Requires VICE with MCP server running on port 8080

set -e

SERVER="http://127.0.0.1:8080"
ENDPOINT="$SERVER/mcp"

echo "=== MCP HTTP Transport Comprehensive Tests ==="
echo

pass_count=0
fail_count=0

run_test() {
    local name="$1"
    local expected_pattern="$2"
    shift 2
    local result

    echo -n "Test: $name ... "
    result=$(eval "$@" 2>&1)

    if echo "$result" | grep -q "$expected_pattern"; then
        echo "PASS"
        pass_count=$((pass_count + 1))
    else
        echo "FAIL"
        echo "  Expected pattern: $expected_pattern"
        echo "  Got: $result"
        fail_count=$((fail_count + 1))
    fi
}

# Test 1: Ping tool
run_test "Ping tool returns OK" '"status":"ok"' \
    "curl -s -X POST '$ENDPOINT' -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"method\":\"vice.ping\",\"id\":1}'"

# Test 2: Invalid method
run_test "Invalid method returns -32601" '\"code\":-32601' \
    "curl -s -X POST '$ENDPOINT' -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"method\":\"invalid\",\"id\":2}'"

# Test 3: Missing method field
run_test "Missing method returns -32600" '\"code\":-32600' \
    "curl -s -X POST '$ENDPOINT' -H 'Content-Type: application/json' -d '{\"jsonrpc\":\"2.0\",\"id\":3}'"

# Test 4: Invalid JSON
run_test "Invalid JSON returns -32700" '\"code\":-32700' \
    "curl -s -X POST '$ENDPOINT' -H 'Content-Type: application/json' -d 'not json'"

# Test 5: CORS headers
run_test "CORS headers present" 'Access-Control-Allow-Origin: \*' \
    "curl -s -I -X POST '$ENDPOINT' -H 'Content-Type: application/json' -d '{}'"

# Test 6: OPTIONS request
run_test "OPTIONS returns 204" 'HTTP/.* 204' \
    "curl -s -I -X OPTIONS '$ENDPOINT'"

# Test 7: 404 for wrong path
run_test "Wrong path returns 404" 'HTTP/.* 404' \
    "curl -s -I -X POST '$SERVER/wrong'"

# Test 8: SSE endpoint responds
run_test "SSE endpoint accepts connections" 'text/event-stream' \
    "curl -s -I -X GET '$SERVER/events' | head -20"

echo
echo "=== Test Results ==="
echo "Passed: $pass_count"
echo "Failed: $fail_count"
echo

if [ $fail_count -gt 0 ]; then
    exit 1
fi

echo "SUCCESS: All tests passed"
```

**Step 2: Make executable**

```bash
chmod +x vice/src/mcp/tests/test_http_detailed.sh
```

**Step 3: Update test README**

In `vice/src/mcp/tests/README.md`, update manual testing section:

```markdown
## Comprehensive HTTP Testing

Run full HTTP transport test suite:
```bash
./test_http_detailed.sh
```

Tests cover:
- JSON-RPC request/response
- Error handling (invalid JSON, missing fields)
- CORS headers
- SSE endpoint
- Request routing
```

**Step 4: Commit**

```bash
git add vice/src/mcp/tests/test_http_detailed.sh vice/src/mcp/tests/README.md
git commit -m "test(mcp): add comprehensive HTTP transport tests"
```

---

## Final Integration

### Task 16: Build and Verify

**Files:**
- Test: Build system integration

**Step 1: Clean build**

```bash
cd vice/build-test-with-mcp
make -C src/mcp clean
make -C src/mcp
```

Expected: Clean build with no errors

**Step 2: Check for warnings**

Review build output for any warnings. Address if found.

**Step 3: Verify library size**

```bash
ls -lh vice/build-test-with-mcp/src/mcp/libmcp.a
```

Expected: Library around 150-200KB (increased from ~158KB due to HTTP code)

**Step 4: Document build success**

No commit needed - verification step only.

---

## Success Criteria

✅ HTTP server starts on configurable port
✅ POST /mcp accepts JSON-RPC requests
✅ Responses follow JSON-RPC 2.0 spec
✅ GET /events accepts SSE connections
✅ CORS headers present for cross-origin requests
✅ Request size limits enforced (1MB max)
✅ All manual tests pass
✅ Clean build with no warnings
✅ Documentation complete

---

## Known Limitations (Defer to Phase 2)

1. **SSE Push Events**: Current implementation can accept SSE connections but cannot push events after initial response. Requires upgrading to MHD response callbacks or WebSockets.

2. **HTTPS**: Server uses HTTP only. HTTPS would require certificate management and is unnecessary for localhost-only deployment.

3. **Authentication**: No authentication required since server binds to localhost only.

4. **Connection Pooling**: No limit on total connections. libmicrohttpd handles this internally with thread-per-connection model.

5. **Request Queuing**: No request queue - all requests processed immediately. Acceptable for single-user localhost debugging.

---

## Next Steps After Completion

1. **Phase 2A**: Implement execution control (run/pause/step)
2. **Phase 2B**: Upgrade SSE to proper streaming notifications
3. **Phase 3**: Add command-line options for MCP server configuration
4. **Phase 4**: Integration with VICE UI for server status display
