/*
 * test_mcp_tools.c - Basic tests for MCP tool implementation
 *
 * Written by:
 *  Barry Walker <barrywalker@gmail.com>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../cJSON.h"

/* Forward declarations - avoid full VICE header dependencies */
#define MCP_ERROR_PARSE_ERROR      -32700
#define MCP_ERROR_INVALID_REQUEST  -32600
#define MCP_ERROR_METHOD_NOT_FOUND -32601
#define MCP_ERROR_INVALID_PARAMS   -32602
#define MCP_ERROR_INTERNAL_ERROR   -32603

/* MCP tool function declarations */
extern cJSON* mcp_tool_ping(cJSON *params);
extern cJSON* mcp_tools_dispatch(const char *tool_name, cJSON *params);

/* MCP Base Protocol tool declarations */
extern cJSON* mcp_tool_initialize(cJSON *params);
extern cJSON* mcp_tool_initialized_notification(cJSON *params);

/* Phase 3.1: Input Control tool declarations */
extern cJSON* mcp_tool_keyboard_type(cJSON *params);
extern cJSON* mcp_tool_keyboard_key_press(cJSON *params);
extern cJSON* mcp_tool_keyboard_key_release(cJSON *params);
extern cJSON* mcp_tool_joystick_set(cJSON *params);

/* Test symbol table helpers from vice_stubs.c */
extern void test_symbol_table_clear(void);
extern int test_symbol_table_get_count(void);
extern const char *test_symbol_table_get_name(int index);
extern unsigned int test_symbol_table_get_addr(int index);

/* Test snapshot helpers from vice_stubs.c */
extern const char *test_snapshot_get_last_saved(void);
extern const char *test_snapshot_get_last_loaded(void);
extern void test_snapshot_set_save_result(int result);
extern void test_snapshot_set_load_result(int result);
extern void test_snapshot_reset(void);

/* Snapshot tool declarations */
extern cJSON* mcp_tool_snapshot_save(cJSON *params);
extern cJSON* mcp_tool_snapshot_load(cJSON *params);
extern cJSON* mcp_tool_snapshot_list(cJSON *params);

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("Running test: %s ... ", #name); \
        fflush(stdout); \
        tests_run++; \
        test_##name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } \
    static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        run_test_##name(); \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            printf("FAIL: Assertion failed: %s is NULL at %s:%d\n", \
                   #ptr, __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            printf("FAIL: Assertion failed: %s at %s:%d\n", \
                   #expr, __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(s1, s2) \
    do { \
        if (strcmp((s1), (s2)) != 0) { \
            printf("FAIL: Strings not equal: '%s' != '%s' at %s:%d\n", \
                   (s1), (s2), __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_INT_EQ(i1, i2) \
    do { \
        if ((i1) != (i2)) { \
            printf("FAIL: Integers not equal: %d != %d at %s:%d\n", \
                   (i1), (i2), __FILE__, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

/* Test: Ping tool returns valid response */
TEST(ping_tool_returns_valid_response)
{
    cJSON *response;
    cJSON *status_item;

    response = mcp_tool_ping(NULL);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_TRUE(cJSON_IsString(status_item));
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(response);
}

/* =================================================================
 * MCP Base Protocol Tests
 * ================================================================= */

/* Test: Initialize with valid protocol version returns success */
TEST(initialize_with_valid_version_succeeds)
{
    cJSON *response, *params, *protocol_item, *capabilities_item, *server_info_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2025-11-25");

    response = mcp_tool_initialize(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    /* Should have protocolVersion */
    protocol_item = cJSON_GetObjectItem(response, "protocolVersion");
    ASSERT_NOT_NULL(protocol_item);
    ASSERT_TRUE(cJSON_IsString(protocol_item));

    /* Should have capabilities */
    capabilities_item = cJSON_GetObjectItem(response, "capabilities");
    ASSERT_NOT_NULL(capabilities_item);
    ASSERT_TRUE(cJSON_IsObject(capabilities_item));

    /* Should have serverInfo */
    server_info_item = cJSON_GetObjectItem(response, "serverInfo");
    ASSERT_NOT_NULL(server_info_item);
    ASSERT_TRUE(cJSON_IsObject(server_info_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialize with 2025-06-18 version succeeds */
TEST(initialize_with_june_version_succeeds)
{
    cJSON *response, *params, *protocol_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2025-06-18");

    response = mcp_tool_initialize(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    protocol_item = cJSON_GetObjectItem(response, "protocolVersion");
    ASSERT_NOT_NULL(protocol_item);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialize with unsupported version returns error */
TEST(initialize_with_unsupported_version_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2099-99-99");

    response = mcp_tool_initialize(params);
    ASSERT_NOT_NULL(response);

    /* Should be an error response */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialize without protocol version succeeds (defaults to 2024-11-05) */
TEST(initialize_without_version_succeeds)
{
    cJSON *response, *params;

    params = cJSON_CreateObject();

    response = mcp_tool_initialize(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialize returns proper capabilities with logging and tools.listChanged */
TEST(initialize_returns_proper_capabilities)
{
    cJSON *response, *params, *capabilities, *logging_cap, *tools_cap, *list_changed;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2025-11-25");

    response = mcp_tool_initialize(params);
    ASSERT_NOT_NULL(response);

    /* Should have capabilities */
    capabilities = cJSON_GetObjectItem(response, "capabilities");
    ASSERT_NOT_NULL(capabilities);
    ASSERT_TRUE(cJSON_IsObject(capabilities));

    /* Should have logging capability (empty object) */
    logging_cap = cJSON_GetObjectItem(capabilities, "logging");
    ASSERT_NOT_NULL(logging_cap);
    ASSERT_TRUE(cJSON_IsObject(logging_cap));

    /* Should have tools capability */
    tools_cap = cJSON_GetObjectItem(capabilities, "tools");
    ASSERT_NOT_NULL(tools_cap);
    ASSERT_TRUE(cJSON_IsObject(tools_cap));

    /* tools.listChanged should be true */
    list_changed = cJSON_GetObjectItem(tools_cap, "listChanged");
    ASSERT_NOT_NULL(list_changed);
    ASSERT_TRUE(cJSON_IsBool(list_changed) || cJSON_IsNumber(list_changed));
    /* In cJSON, booleans are stored as valueint */
    if (cJSON_IsNumber(list_changed)) {
        ASSERT_TRUE(list_changed->valueint != 0);  /* true */
    }

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialized notification returns NULL (no response per JSON-RPC 2.0) */
TEST(initialized_notification_returns_null)
{
    cJSON *response;

    response = mcp_tool_initialized_notification(NULL);

    /* Per JSON-RPC 2.0, notifications get no response - should return NULL */
    ASSERT_TRUE(response == NULL);
}

/* Test: Initialize dispatch works */
TEST(initialize_dispatch_works)
{
    cJSON *response, *params;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "protocolVersion", "2025-11-25");

    response = mcp_tools_dispatch("initialize", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Initialized notification dispatch returns NULL (no response per JSON-RPC 2.0) */
TEST(initialized_notification_dispatch_returns_null)
{
    cJSON *response;

    response = mcp_tools_dispatch("notifications/initialized", NULL);

    /* Per JSON-RPC 2.0, notifications get no response - should return NULL */
    ASSERT_TRUE(response == NULL);
}

/* =================================================================
 * General Tool Tests
 * ================================================================= */

/* Test: Invalid tool name returns error */
TEST(invalid_tool_name_returns_error)
{
    cJSON *response;
    cJSON *code_item;

    response = mcp_tools_dispatch("invalid.tool.name", NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_METHOD_NOT_FOUND);

    cJSON_Delete(response);
}

/* Test: NULL tool name returns error */
TEST(null_tool_name_returns_error)
{
    cJSON *response;
    cJSON *code_item;

    response = mcp_tools_dispatch(NULL, NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_REQUEST);

    cJSON_Delete(response);
}

/* Test: Empty tool name returns error */
TEST(empty_tool_name_returns_error)
{
    cJSON *response;
    cJSON *code_item;

    response = mcp_tools_dispatch("", NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_REQUEST);

    cJSON_Delete(response);
}

/* Test: Tool name too long returns error */
TEST(tool_name_too_long_returns_error)
{
    cJSON *response;
    cJSON *code_item;
    char long_name[300];
    int i;

    /* Create a 257-character tool name */
    for (i = 0; i < 257; i++) {
        long_name[i] = 'a';
    }
    long_name[257] = '\0';

    response = mcp_tools_dispatch(long_name, NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_REQUEST);

    cJSON_Delete(response);
}

/* Test: Valid tool dispatch works */
TEST(valid_tool_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.ping", NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    ASSERT_TRUE(cJSON_GetObjectItem(response, "code") == NULL);

    cJSON_Delete(response);
}

/* ===================================================================
 * Phase 3.1: Input Control Tests
 * =================================================================== */

/* Test: keyboard.type with missing text parameter returns error */
TEST(keyboard_type_missing_text_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    response = mcp_tool_keyboard_type(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.type with empty text returns error */
TEST(keyboard_type_empty_text_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text", "");

    response = mcp_tool_keyboard_type(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.type with NULL params returns error */
TEST(keyboard_type_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_keyboard_type(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: keyboard.type with non-string text returns error */
TEST(keyboard_type_non_string_text_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "text", 123);

    response = mcp_tool_keyboard_type(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_press with missing key parameter returns error */
TEST(keyboard_key_press_missing_key_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    response = mcp_tool_keyboard_key_press(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_press with NULL params returns error */
TEST(keyboard_key_press_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_keyboard_key_press(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: keyboard.key_press with invalid key name returns error */
TEST(keyboard_key_press_invalid_key_name_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "InvalidKeyName123");

    response = mcp_tool_keyboard_key_press(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_press with multi-character key name returns error */
TEST(keyboard_key_press_multi_char_key_name_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "ABC");  /* Multi-char, not a valid key name */

    response = mcp_tool_keyboard_key_press(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_press with boolean key returns error */
TEST(keyboard_key_press_boolean_key_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "key", 1);

    response = mcp_tool_keyboard_key_press(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_release with missing key parameter returns error */
TEST(keyboard_key_release_missing_key_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    response = mcp_tool_keyboard_key_release(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.key_release with NULL params returns error */
TEST(keyboard_key_release_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_keyboard_key_release(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: joystick.set with NULL params works (uses defaults) */
TEST(joystick_set_null_params_works)
{
    cJSON *response, *code_item;

    response = mcp_tool_joystick_set(NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    cJSON_Delete(response);
}

/* Test: joystick.set with invalid port (0) returns error */
TEST(joystick_set_port_zero_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "port", 0);

    response = mcp_tool_joystick_set(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: joystick.set with invalid port (3) returns error */
TEST(joystick_set_port_three_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "port", 3);

    response = mcp_tool_joystick_set(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: joystick.set with invalid direction returns error */
TEST(joystick_set_invalid_direction_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "direction", "invalid");

    response = mcp_tool_joystick_set(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: joystick.set with port 1 valid */
TEST(joystick_set_port_one_valid)
{
    cJSON *response, *params, *port_item, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "port", 1);
    cJSON_AddStringToObject(params, "direction", "up");

    response = mcp_tool_joystick_set(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    port_item = cJSON_GetObjectItem(response, "port");
    if (port_item != NULL && cJSON_IsNumber(port_item)) {
        ASSERT_INT_EQ(port_item->valueint, 1);
    }

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: joystick.set with port 2 valid */
TEST(joystick_set_port_two_valid)
{
    cJSON *response, *params, *port_item, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "port", 2);
    cJSON_AddStringToObject(params, "direction", "right");

    response = mcp_tool_joystick_set(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL || code_item->valueint >= 0);

    port_item = cJSON_GetObjectItem(response, "port");
    if (port_item != NULL && cJSON_IsNumber(port_item)) {
        ASSERT_INT_EQ(port_item->valueint, 2);
    }

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard.type dispatch works */
TEST(keyboard_type_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.keyboard.type", NULL);
    ASSERT_NOT_NULL(response);

    /* Will error due to missing text, but dispatch works */
    ASSERT_TRUE(cJSON_GetObjectItem(response, "code") != NULL ||
                cJSON_GetObjectItem(response, "status") != NULL);

    cJSON_Delete(response);
}

/* Test: keyboard.key_press dispatch works */
TEST(keyboard_key_press_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.keyboard.key_press", NULL);
    ASSERT_NOT_NULL(response);

    /* Will error due to missing key, but dispatch works */
    ASSERT_TRUE(cJSON_GetObjectItem(response, "code") != NULL ||
                cJSON_GetObjectItem(response, "status") != NULL);

    cJSON_Delete(response);
}

/* Test: keyboard.key_release dispatch works */
TEST(keyboard_key_release_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.keyboard.key_release", NULL);
    ASSERT_NOT_NULL(response);

    /* Will error due to missing key, but dispatch works */
    ASSERT_TRUE(cJSON_GetObjectItem(response, "code") != NULL ||
                cJSON_GetObjectItem(response, "status") != NULL);

    cJSON_Delete(response);
}

/* Test: joystick.set dispatch works */
TEST(joystick_set_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.joystick.set", NULL);
    ASSERT_NOT_NULL(response);

    /* NULL params should work with defaults */
    ASSERT_NOT_NULL(response);

    cJSON_Delete(response);
}

/* ===================================================================
 * Execution Control Tests
 * =================================================================== */

/* Forward declarations for execution control tools */
extern cJSON* mcp_tool_execution_run(cJSON *params);
extern cJSON* mcp_tool_execution_pause(cJSON *params);
extern cJSON* mcp_tool_execution_step(cJSON *params);

/* Test: execution.run returns ok status */
TEST(execution_run_returns_ok)
{
    cJSON *response, *status_item;

    response = mcp_tool_execution_run(NULL);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(response);
}

/* Test: execution.pause returns ok status */
TEST(execution_pause_returns_ok)
{
    cJSON *response, *status_item;

    response = mcp_tool_execution_pause(NULL);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(response);
}

/* Test: execution.step with default parameters */
TEST(execution_step_default_params)
{
    cJSON *response, *status_item, *instructions_item;

    response = mcp_tool_execution_step(NULL);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Default should be 1 instruction */
    instructions_item = cJSON_GetObjectItem(response, "instructions");
    ASSERT_NOT_NULL(instructions_item);
    ASSERT_INT_EQ(instructions_item->valueint, 1);

    cJSON_Delete(response);
}

/* Test: execution.step with count parameter */
TEST(execution_step_with_count)
{
    cJSON *response, *params, *instructions_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "count", 10);

    response = mcp_tool_execution_step(params);
    ASSERT_NOT_NULL(response);

    instructions_item = cJSON_GetObjectItem(response, "instructions");
    ASSERT_NOT_NULL(instructions_item);
    ASSERT_INT_EQ(instructions_item->valueint, 10);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: execution.step with stepOver parameter */
TEST(execution_step_with_step_over)
{
    cJSON *response, *params, *step_over_item;

    params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "stepOver", 1);

    response = mcp_tool_execution_step(params);
    ASSERT_NOT_NULL(response);

    step_over_item = cJSON_GetObjectItem(response, "step_over");
    ASSERT_NOT_NULL(step_over_item);
    ASSERT_TRUE(cJSON_IsTrue(step_over_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: execution.step dispatch works */
TEST(execution_step_dispatch_works)
{
    cJSON *response;

    response = mcp_tools_dispatch("vice.execution.step", NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error response */
    ASSERT_TRUE(cJSON_GetObjectItem(response, "code") == NULL);

    cJSON_Delete(response);
}

/* ===================================================================
 * MCP tools/call Tests (Claude Code integration)
 * =================================================================== */

/* Test: tools/call with valid tool invokes correctly */
TEST(tools_call_with_valid_tool_works)
{
    cJSON *response, *params, *content, *first_item, *type_item, *text_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "vice.ping");
    /* No arguments needed for ping */

    response = mcp_tools_dispatch("tools/call", params);
    ASSERT_NOT_NULL(response);

    /* Should have content array */
    content = cJSON_GetObjectItem(response, "content");
    ASSERT_NOT_NULL(content);
    ASSERT_TRUE(cJSON_IsArray(content));
    ASSERT_TRUE(cJSON_GetArraySize(content) > 0);

    /* First item should have type: "text" */
    first_item = cJSON_GetArrayItem(content, 0);
    ASSERT_NOT_NULL(first_item);
    type_item = cJSON_GetObjectItem(first_item, "type");
    ASSERT_NOT_NULL(type_item);
    ASSERT_STR_EQ(type_item->valuestring, "text");

    /* Should have text field */
    text_item = cJSON_GetObjectItem(first_item, "text");
    ASSERT_NOT_NULL(text_item);
    ASSERT_TRUE(cJSON_IsString(text_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: tools/call with missing name returns error */
TEST(tools_call_missing_name_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    /* Don't add "name" parameter */

    response = mcp_tools_dispatch("tools/call", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: tools/call with unknown tool returns error */
TEST(tools_call_unknown_tool_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "nonexistent.tool");

    response = mcp_tools_dispatch("tools/call", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_METHOD_NOT_FOUND);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: tools/call with arguments passes them correctly */
TEST(tools_call_passes_arguments)
{
    cJSON *response, *params, *args, *content, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "vice.keyboard.type");
    args = cJSON_CreateObject();
    cJSON_AddStringToObject(args, "text", "HELLO");
    cJSON_AddItemToObject(params, "arguments", args);

    response = mcp_tools_dispatch("tools/call", params);
    ASSERT_NOT_NULL(response);

    /* Should succeed (content array) not error (code field) */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    content = cJSON_GetObjectItem(response, "content");
    ASSERT_NOT_NULL(content);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* ===================================================================
 * MCP tools/list Tests
 * =================================================================== */

/* Test: tools/list returns tools array */
TEST(tools_list_returns_tools_array)
{
    cJSON *response, *tools;

    response = mcp_tools_dispatch("tools/list", NULL);
    ASSERT_NOT_NULL(response);

    tools = cJSON_GetObjectItem(response, "tools");
    ASSERT_NOT_NULL(tools);
    ASSERT_TRUE(cJSON_IsArray(tools));
    ASSERT_TRUE(cJSON_GetArraySize(tools) > 0);

    cJSON_Delete(response);
}

/* Test: tools/list tools have required fields */
TEST(tools_list_tools_have_required_fields)
{
    cJSON *response, *tools, *first_tool, *name, *desc, *schema;

    response = mcp_tools_dispatch("tools/list", NULL);
    ASSERT_NOT_NULL(response);

    tools = cJSON_GetObjectItem(response, "tools");
    ASSERT_NOT_NULL(tools);
    ASSERT_TRUE(cJSON_GetArraySize(tools) > 0);

    first_tool = cJSON_GetArrayItem(tools, 0);
    ASSERT_NOT_NULL(first_tool);

    /* Each tool must have name, description, inputSchema */
    name = cJSON_GetObjectItem(first_tool, "name");
    ASSERT_NOT_NULL(name);
    ASSERT_TRUE(cJSON_IsString(name));

    desc = cJSON_GetObjectItem(first_tool, "description");
    ASSERT_NOT_NULL(desc);
    ASSERT_TRUE(cJSON_IsString(desc));

    schema = cJSON_GetObjectItem(first_tool, "inputSchema");
    ASSERT_NOT_NULL(schema);
    ASSERT_TRUE(cJSON_IsObject(schema));

    cJSON_Delete(response);
}

/* Test: tools/list schemas are valid JSON Schema objects */
TEST(tools_list_schemas_are_valid)
{
    cJSON *response, *tools, *first_tool, *schema, *type_item;

    response = mcp_tools_dispatch("tools/list", NULL);
    ASSERT_NOT_NULL(response);

    tools = cJSON_GetObjectItem(response, "tools");
    ASSERT_NOT_NULL(tools);

    first_tool = cJSON_GetArrayItem(tools, 0);
    ASSERT_NOT_NULL(first_tool);

    schema = cJSON_GetObjectItem(first_tool, "inputSchema");
    ASSERT_NOT_NULL(schema);

    /* Schema should have type: "object" */
    type_item = cJSON_GetObjectItem(schema, "type");
    ASSERT_NOT_NULL(type_item);
    ASSERT_STR_EQ(type_item->valuestring, "object");

    cJSON_Delete(response);
}

/* ===================================================================
 * Symbol Loading Tests
 * =================================================================== */

/* Helper: Write content to a temp file and return the path */
static const char* write_temp_symbol_file(const char *content, const char *suffix)
{
    static char temp_path[256];
    FILE *fp;

    snprintf(temp_path, sizeof(temp_path), "/tmp/test_symbols_%s.sym", suffix);
    fp = fopen(temp_path, "w");
    if (fp == NULL) return NULL;
    fputs(content, fp);
    fclose(fp);
    return temp_path;
}

/* Test: symbols.load with KickAssembler format */
TEST(symbols_load_kickasm_format)
{
    cJSON *response, *params, *status_item, *format_item, *count_item;
    const char *path;

    /* KickAssembler format with simple labels */
    path = write_temp_symbol_file(
        ".label START=$0801\n"
        ".label MAIN=$0810\n"
        ".label SCREEN=$0400\n",
        "kickasm");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    format_item = cJSON_GetObjectItem(response, "format_detected");
    ASSERT_NOT_NULL(format_item);
    ASSERT_STR_EQ(format_item->valuestring, "kickasm");

    count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 3);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with KickAssembler namespace */
TEST(symbols_load_kickasm_namespace)
{
    cJSON *response, *params, *status_item, *count_item;
    const char *path;

    /* KickAssembler format with namespace */
    path = write_temp_symbol_file(
        ".label MAIN=$0810\n"
        ".namespace vic {\n"
        "  .label BORDER=$d020\n"
        "  .label BGCOL=$d021\n"
        "}\n"
        ".label END=$0900\n",
        "kickasm_ns");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Should load: MAIN, vic.BORDER, vic.BGCOL, END = 4 symbols */
    count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 4);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with KickAssembler label blocks */
TEST(symbols_load_kickasm_label_blocks)
{
    cJSON *response, *params, *status_item, *count_item;
    const char *path;

    /* KickAssembler format with label blocks (empty { }) */
    path = write_temp_symbol_file(
        ".label Routine1=$1000 {\n"
        "}\n"
        ".label Routine2=$1100 {\n"
        "}\n"
        ".namespace myns {\n"
        "  .label Inner=$2000\n"
        "}\n"
        ".label AfterNs=$3000\n",
        "kickasm_blocks");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Should load: Routine1, Routine2, myns.Inner, AfterNs = 4 symbols */
    count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 4);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with VICE format */
TEST(symbols_load_vice_format)
{
    cJSON *response, *params, *status_item, *format_item, *count_item;
    const char *path;

    /* VICE label format */
    path = write_temp_symbol_file(
        "al C:0801 .start\n"
        "al C:0810 .main\n"
        "al C:d020 .border\n",
        "vice");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    format_item = cJSON_GetObjectItem(response, "format_detected");
    ASSERT_NOT_NULL(format_item);
    ASSERT_STR_EQ(format_item->valuestring, "vice");

    count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 3);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with simple format */
TEST(symbols_load_simple_format)
{
    cJSON *response, *params, *status_item, *format_item, *count_item;
    const char *path;

    /* Simple format: label = $xxxx */
    path = write_temp_symbol_file(
        "start = $0801\n"
        "main = $0810\n"
        "screen = $0400\n",
        "simple");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    format_item = cJSON_GetObjectItem(response, "format_detected");
    ASSERT_NOT_NULL(format_item);
    ASSERT_STR_EQ(format_item->valuestring, "simple");

    count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 3);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with explicit format override */
TEST(symbols_load_format_override)
{
    cJSON *response, *params, *status_item, *format_item;
    const char *path;

    /* KickAssembler content but force 'vice' format (should parse nothing) */
    path = write_temp_symbol_file(
        ".label START=$0801\n"
        ".label MAIN=$0810\n",
        "override");
    ASSERT_NOT_NULL(path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", path);
    cJSON_AddStringToObject(params, "format", "vice");

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    format_item = cJSON_GetObjectItem(response, "format_detected");
    ASSERT_NOT_NULL(format_item);
    ASSERT_STR_EQ(format_item->valuestring, "vice");

    /* vice format won't recognize .label, so count should be 0 */
    cJSON *count_item = cJSON_GetObjectItem(response, "symbols_loaded");
    ASSERT_NOT_NULL(count_item);
    ASSERT_INT_EQ(count_item->valueint, 0);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(path);
}

/* Test: symbols.load with missing path returns error */
TEST(symbols_load_missing_path_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    /* Don't add path */

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: symbols.load with nonexistent file returns error */
TEST(symbols_load_nonexistent_file_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", "/nonexistent/path/symbols.sym");

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: symbols.load stores valid heap-allocated strings (not stack pointers)
 * This test catches use-after-free bugs where stack variables are passed
 * to mon_add_name_to_symbol_table without being copied first.
 * The stub mimics VICE's behavior of storing pointers directly. */
TEST(symbols_load_stores_valid_pointers)
{
    cJSON *response, *params;
    FILE *fp;
    const char *test_file = "/tmp/test_symbols_ptr.sym";
    const char *name;
    int i;

    /* Create test file with multiple symbols */
    fp = fopen(test_file, "w");
    ASSERT_NOT_NULL(fp);
    fprintf(fp, ".label first=$1000\n");
    fprintf(fp, ".label second=$2000\n");
    fprintf(fp, ".namespace Test {\n");
    fprintf(fp, ".label nested=$3000\n");
    fprintf(fp, "}\n");
    fclose(fp);

    /* Clear symbol table before test */
    test_symbol_table_clear();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", test_file);

    response = mcp_tools_dispatch("vice.symbols.load", params);
    ASSERT_NOT_NULL(response);

    /* Verify symbols were stored */
    ASSERT_INT_EQ(test_symbol_table_get_count(), 3);

    /* Verify stored pointers are valid strings, not garbage
     * If lib_strdup wasn't used, these would be dangling pointers
     * pointing to overwritten stack memory */
    for (i = 0; i < test_symbol_table_get_count(); i++) {
        name = test_symbol_table_get_name(i);
        ASSERT_NOT_NULL(name);
        /* Verify it's a valid string (not garbage) by checking length and content */
        ASSERT_TRUE(strlen(name) > 0);
        ASSERT_TRUE(strlen(name) < 256);  /* Sanity check */
    }

    /* Verify specific symbol names are correct */
    name = test_symbol_table_get_name(0);
    ASSERT_STR_EQ(name, "first");

    name = test_symbol_table_get_name(1);
    ASSERT_STR_EQ(name, "second");

    name = test_symbol_table_get_name(2);
    ASSERT_STR_EQ(name, "Test.nested");

    /* Verify addresses */
    ASSERT_INT_EQ(test_symbol_table_get_addr(0) & 0xFFFF, 0x1000);
    ASSERT_INT_EQ(test_symbol_table_get_addr(1) & 0xFFFF, 0x2000);
    ASSERT_INT_EQ(test_symbol_table_get_addr(2) & 0xFFFF, 0x3000);

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(test_file);
}

/* ========================================================================= */
/* Machine Reset Tests                                                       */
/* ========================================================================= */

/* Test: machine.reset with default params returns ok and run_after=true */
TEST(machine_reset_default_params)
{
    cJSON *response, *status_item, *mode_item, *run_after_item;

    response = mcp_tools_dispatch("vice.machine.reset", NULL);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    mode_item = cJSON_GetObjectItem(response, "mode");
    ASSERT_NOT_NULL(mode_item);
    ASSERT_STR_EQ(mode_item->valuestring, "soft");

    run_after_item = cJSON_GetObjectItem(response, "run_after");
    ASSERT_NOT_NULL(run_after_item);
    ASSERT_TRUE(cJSON_IsTrue(run_after_item));

    cJSON_Delete(response);
}

/* Test: machine.reset with mode=hard returns ok */
TEST(machine_reset_hard_mode)
{
    cJSON *response, *params, *status_item, *mode_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "hard");

    response = mcp_tools_dispatch("vice.machine.reset", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    mode_item = cJSON_GetObjectItem(response, "mode");
    ASSERT_NOT_NULL(mode_item);
    ASSERT_STR_EQ(mode_item->valuestring, "hard");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: machine.reset with run_after=false returns run_after=false */
TEST(machine_reset_run_after_false)
{
    cJSON *response, *params, *status_item, *run_after_item;

    params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "run_after", 0);

    response = mcp_tools_dispatch("vice.machine.reset", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    run_after_item = cJSON_GetObjectItem(response, "run_after");
    ASSERT_NOT_NULL(run_after_item);
    ASSERT_TRUE(cJSON_IsFalse(run_after_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: machine.reset with invalid mode returns error */
TEST(machine_reset_invalid_mode_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "invalid");

    response = mcp_tools_dispatch("vice.machine.reset", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* ========================================================================= */
/* Phase 5: Enhanced Debugging Tests                                         */
/* ========================================================================= */

/* Test: backtrace returns valid response */
TEST(backtrace_returns_valid_response)
{
    cJSON *response, *frames_item, *sp_item;

    response = mcp_tools_dispatch("vice.backtrace", NULL);
    ASSERT_NOT_NULL(response);

    sp_item = cJSON_GetObjectItem(response, "sp");
    ASSERT_NOT_NULL(sp_item);
    ASSERT_TRUE(cJSON_IsNumber(sp_item));

    frames_item = cJSON_GetObjectItem(response, "frames");
    ASSERT_NOT_NULL(frames_item);
    ASSERT_TRUE(cJSON_IsArray(frames_item));

    cJSON_Delete(response);
}

/* Test: backtrace with depth parameter */
TEST(backtrace_with_depth_param)
{
    cJSON *response, *params, *count_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "depth", 8);

    response = mcp_tools_dispatch("vice.backtrace", params);
    ASSERT_NOT_NULL(response);

    count_item = cJSON_GetObjectItem(response, "frame_count");
    ASSERT_NOT_NULL(count_item);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: run_until requires address or cycles */
TEST(run_until_requires_params)
{
    cJSON *response, *code_item;

    response = mcp_tools_dispatch("vice.run_until", NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: run_until with address */
TEST(run_until_with_address)
{
    cJSON *response, *params, *status_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "address", 0x1000);

    response = mcp_tools_dispatch("vice.run_until", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: run_until with symbol name */
TEST(run_until_with_symbol)
{
    cJSON *response, *params, *status_item;
    FILE *fp;
    const char *test_file = "/tmp/test_run_until.sym";

    /* Create and load symbol file */
    fp = fopen(test_file, "w");
    ASSERT_NOT_NULL(fp);
    fprintf(fp, ".label TestTarget=$1234\n");
    fclose(fp);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", test_file);
    response = mcp_tools_dispatch("vice.symbols.load", params);
    cJSON_Delete(response);
    cJSON_Delete(params);

    /* Now test run_until with symbol */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "TestTarget");

    response = mcp_tools_dispatch("vice.run_until", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(test_file);
}

/* Test: keyboard_matrix with key name */
TEST(keyboard_matrix_with_key_name)
{
    cJSON *response, *params, *status_item, *row_item, *col_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "SPACE");

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    row_item = cJSON_GetObjectItem(response, "row");
    ASSERT_NOT_NULL(row_item);
    ASSERT_INT_EQ(row_item->valueint, 7);  /* SPACE is row 7 */

    col_item = cJSON_GetObjectItem(response, "col");
    ASSERT_NOT_NULL(col_item);
    ASSERT_INT_EQ(col_item->valueint, 4);  /* SPACE is col 4 */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix with row/col */
TEST(keyboard_matrix_with_row_col)
{
    cJSON *response, *params, *status_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "row", 3);
    cJSON_AddNumberToObject(params, "col", 5);

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix requires key or row/col */
TEST(keyboard_matrix_requires_params)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();  /* Empty params */

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix with hold_frames */
TEST(keyboard_matrix_with_hold_frames)
{
    cJSON *response, *params, *status_item, *hold_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "A");
    cJSON_AddNumberToObject(params, "hold_frames", 3);

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    hold_item = cJSON_GetObjectItem(response, "hold_frames");
    ASSERT_NOT_NULL(hold_item);
    ASSERT_INT_EQ(hold_item->valueint, 3);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix with hold_ms */
TEST(keyboard_matrix_with_hold_ms)
{
    cJSON *response, *params, *status_item, *hold_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "SPACE");
    cJSON_AddNumberToObject(params, "hold_ms", 100);

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    hold_item = cJSON_GetObjectItem(response, "hold_ms");
    ASSERT_NOT_NULL(hold_item);
    ASSERT_INT_EQ(hold_item->valueint, 100);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix hold_frames validation */
TEST(keyboard_matrix_hold_frames_invalid)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "A");
    cJSON_AddNumberToObject(params, "hold_frames", 500);  /* Over max 300 */

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix hold_ms validation */
TEST(keyboard_matrix_hold_ms_invalid)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "A");
    cJSON_AddNumberToObject(params, "hold_ms", 10000);  /* Over max 5000 */

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: keyboard_matrix key release (pressed=false) */
TEST(keyboard_matrix_key_release)
{
    cJSON *response, *params, *status_item, *pressed_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "A");
    cJSON_AddBoolToObject(params, "pressed", 0);

    response = mcp_tools_dispatch("vice.keyboard.matrix", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    pressed_item = cJSON_GetObjectItem(response, "pressed");
    ASSERT_NOT_NULL(pressed_item);
    ASSERT_TRUE(cJSON_IsFalse(pressed_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: watch_add with symbol name */
TEST(watch_add_with_symbol)
{
    cJSON *response, *params, *status_item;
    FILE *fp;
    const char *test_file = "/tmp/test_watch.sym";

    /* Create and load symbol file */
    fp = fopen(test_file, "w");
    ASSERT_NOT_NULL(fp);
    fprintf(fp, ".label WatchVar=$2000\n");
    fclose(fp);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "path", test_file);
    response = mcp_tools_dispatch("vice.symbols.load", params);
    cJSON_Delete(response);
    cJSON_Delete(params);

    /* Now test watch_add with symbol */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "WatchVar");
    cJSON_AddNumberToObject(params, "size", 2);

    response = mcp_tools_dispatch("vice.watch.add", params);
    ASSERT_NOT_NULL(response);

    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    cJSON_Delete(params);
    cJSON_Delete(response);
    remove(test_file);
}

/* Test: disassemble with hex string address */
TEST(disassemble_with_hex_address)
{
    cJSON *response, *params, *lines_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "$1000");
    cJSON_AddNumberToObject(params, "count", 3);

    response = mcp_tools_dispatch("vice.disassemble", params);
    ASSERT_NOT_NULL(response);

    lines_item = cJSON_GetObjectItem(response, "lines");
    ASSERT_NOT_NULL(lines_item);
    ASSERT_TRUE(cJSON_IsArray(lines_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Snapshot Tool Tests
 * ================================================================= */

/* Test: Snapshot save with valid name returns success */
TEST(snapshot_save_with_name_succeeds)
{
    cJSON *response, *params;
    cJSON *name_item, *path_item;

    test_snapshot_reset();
    test_snapshot_set_save_result(0);  /* Success */

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_debug_state");
    cJSON_AddStringToObject(params, "description", "Before sprite collision bug");

    response = mcp_tool_snapshot_save(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return name */
    name_item = cJSON_GetObjectItem(response, "name");
    ASSERT_NOT_NULL(name_item);
    ASSERT_STR_EQ(name_item->valuestring, "test_debug_state");

    /* Should return path */
    path_item = cJSON_GetObjectItem(response, "path");
    ASSERT_NOT_NULL(path_item);
    ASSERT_TRUE(strstr(path_item->valuestring, "test_debug_state.vsf") != NULL);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot save without name returns error */
TEST(snapshot_save_without_name_returns_error)
{
    cJSON *response, *params, *code_item;

    test_snapshot_reset();

    params = cJSON_CreateObject();
    /* No name provided */

    response = mcp_tool_snapshot_save(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, -32602);  /* Invalid params */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot save with options includes ROMs and disks */
TEST(snapshot_save_with_options_succeeds)
{
    cJSON *response, *params;

    test_snapshot_reset();
    test_snapshot_set_save_result(0);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "full_state");
    cJSON_AddBoolToObject(params, "include_roms", 1);
    cJSON_AddBoolToObject(params, "include_disks", 1);

    response = mcp_tool_snapshot_save(params);
    ASSERT_NOT_NULL(response);

    /* Should succeed */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot save failure returns error */
TEST(snapshot_save_failure_returns_error)
{
    cJSON *response, *params, *code_item;

    test_snapshot_reset();
    test_snapshot_set_save_result(-1);  /* Simulate failure */

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "will_fail");

    response = mcp_tool_snapshot_save(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, -32004);  /* Snapshot failed */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

int main(void)
{
    printf("=== MCP Tools Test Suite ===\n\n");

    /* Note: We don't call mcp_tools_init() because it requires
     * VICE's logging system to be initialized, which requires
     * the full VICE runtime environment. These are unit tests
     * of the individual tool functions. */

    /* Core functionality tests */
    RUN_TEST(ping_tool_returns_valid_response);
    RUN_TEST(invalid_tool_name_returns_error);
    RUN_TEST(null_tool_name_returns_error);
    RUN_TEST(empty_tool_name_returns_error);
    RUN_TEST(tool_name_too_long_returns_error);
    RUN_TEST(valid_tool_dispatch_works);

    /* MCP Base Protocol tests */
    RUN_TEST(initialize_with_valid_version_succeeds);
    RUN_TEST(initialize_with_june_version_succeeds);
    RUN_TEST(initialize_with_unsupported_version_returns_error);
    RUN_TEST(initialize_without_version_succeeds);
    RUN_TEST(initialize_returns_proper_capabilities);
    RUN_TEST(initialized_notification_returns_null);
    RUN_TEST(initialize_dispatch_works);
    RUN_TEST(initialized_notification_dispatch_returns_null);

    /* Phase 3.1: keyboard.type negative tests */
    RUN_TEST(keyboard_type_missing_text_returns_error);
    RUN_TEST(keyboard_type_empty_text_returns_error);
    RUN_TEST(keyboard_type_null_params_returns_error);
    RUN_TEST(keyboard_type_non_string_text_returns_error);

    /* Phase 3.1: keyboard.key_press negative tests */
    RUN_TEST(keyboard_key_press_missing_key_returns_error);
    RUN_TEST(keyboard_key_press_null_params_returns_error);
    RUN_TEST(keyboard_key_press_invalid_key_name_returns_error);
    RUN_TEST(keyboard_key_press_multi_char_key_name_returns_error);
    RUN_TEST(keyboard_key_press_boolean_key_returns_error);

    /* Phase 3.1: keyboard.key_release negative tests */
    RUN_TEST(keyboard_key_release_missing_key_returns_error);
    RUN_TEST(keyboard_key_release_null_params_returns_error);

    /* Phase 3.1: joystick.set negative tests */
    RUN_TEST(joystick_set_port_zero_returns_error);
    RUN_TEST(joystick_set_port_three_returns_error);
    RUN_TEST(joystick_set_invalid_direction_returns_error);

    /* Phase 3.1: joystick.set positive tests */
    RUN_TEST(joystick_set_null_params_works);
    RUN_TEST(joystick_set_port_one_valid);
    RUN_TEST(joystick_set_port_two_valid);

    /* Phase 3.1: dispatch tests */
    RUN_TEST(keyboard_type_dispatch_works);
    RUN_TEST(keyboard_key_press_dispatch_works);
    RUN_TEST(keyboard_key_release_dispatch_works);
    RUN_TEST(joystick_set_dispatch_works);

    /* Execution control tests */
    RUN_TEST(execution_run_returns_ok);
    RUN_TEST(execution_pause_returns_ok);
    RUN_TEST(execution_step_default_params);
    RUN_TEST(execution_step_with_count);
    RUN_TEST(execution_step_with_step_over);
    RUN_TEST(execution_step_dispatch_works);

    /* MCP tools/call tests (Claude Code integration) */
    RUN_TEST(tools_call_with_valid_tool_works);
    RUN_TEST(tools_call_missing_name_returns_error);
    RUN_TEST(tools_call_unknown_tool_returns_error);
    RUN_TEST(tools_call_passes_arguments);

    /* MCP tools/list tests */
    RUN_TEST(tools_list_returns_tools_array);
    RUN_TEST(tools_list_tools_have_required_fields);
    RUN_TEST(tools_list_schemas_are_valid);

    /* Symbol loading tests */
    RUN_TEST(symbols_load_kickasm_format);
    RUN_TEST(symbols_load_kickasm_namespace);
    RUN_TEST(symbols_load_kickasm_label_blocks);
    RUN_TEST(symbols_load_vice_format);
    RUN_TEST(symbols_load_simple_format);
    RUN_TEST(symbols_load_format_override);
    RUN_TEST(symbols_load_missing_path_returns_error);
    RUN_TEST(symbols_load_nonexistent_file_returns_error);
    RUN_TEST(symbols_load_stores_valid_pointers);

    /* Machine reset tests */
    RUN_TEST(machine_reset_default_params);
    RUN_TEST(machine_reset_hard_mode);
    RUN_TEST(machine_reset_run_after_false);
    RUN_TEST(machine_reset_invalid_mode_returns_error);

    /* Phase 5: Enhanced debugging tools tests */
    RUN_TEST(backtrace_returns_valid_response);
    RUN_TEST(backtrace_with_depth_param);
    RUN_TEST(run_until_requires_params);
    RUN_TEST(run_until_with_address);
    RUN_TEST(run_until_with_symbol);
    RUN_TEST(keyboard_matrix_with_key_name);
    RUN_TEST(keyboard_matrix_with_row_col);
    RUN_TEST(keyboard_matrix_requires_params);
    RUN_TEST(keyboard_matrix_with_hold_frames);
    RUN_TEST(keyboard_matrix_with_hold_ms);
    RUN_TEST(keyboard_matrix_hold_frames_invalid);
    RUN_TEST(keyboard_matrix_hold_ms_invalid);
    RUN_TEST(keyboard_matrix_key_release);
    RUN_TEST(watch_add_with_symbol);
    RUN_TEST(disassemble_with_hex_address);

    /* Snapshot tests */
    RUN_TEST(snapshot_save_with_name_succeeds);
    RUN_TEST(snapshot_save_without_name_returns_error);
    RUN_TEST(snapshot_save_with_options_succeeds);
    RUN_TEST(snapshot_save_failure_returns_error);

    printf("\n=== Test Results ===\n");
    printf("Tests run:    %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    if (tests_failed > 0) {
        printf("\nFAILURE: %d test(s) failed\n", tests_failed);
        return 1;
    }

    printf("\nSUCCESS: All tests passed\n");
    return 0;
}
