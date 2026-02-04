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

/* Memory search tool declaration */
extern cJSON* mcp_tool_memory_search(cJSON *params);

/* Cycles stopwatch tool declaration */
extern cJSON* mcp_tool_cycles_stopwatch(cJSON *params);

/* Memory fill tool declaration */
extern cJSON* mcp_tool_memory_fill(cJSON *params);

/* Test stopwatch helpers from vice_stubs.c */
extern void test_stopwatch_reset(void);
extern void test_stopwatch_set_cycles(unsigned long cycles);
extern unsigned long test_stopwatch_get_cycles(void);

/* Test memory helpers from vice_stubs.c */
extern void test_memory_set(uint16_t addr, const uint8_t *data, size_t len);
extern void test_memory_set_byte(uint16_t addr, uint8_t value);
extern void test_memory_clear(void);
extern uint8_t test_memory_get_byte(uint16_t addr);

/* Test checkpoint helpers from vice_stubs.c */
extern void test_checkpoint_reset(void);
extern int test_checkpoint_get_last_num(void);
extern int test_checkpoint_has_condition(void);

/* Checkpoint group tool declarations */
extern cJSON* mcp_tool_checkpoint_group_create(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_add(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_toggle(cJSON *params);
extern cJSON* mcp_tool_checkpoint_group_list(cJSON *params);

/* Checkpoint group reset function - implemented here because it needs libmcp.a */
extern void mcp_checkpoint_groups_reset(void);  /* From libmcp.a */

static void test_checkpoint_groups_reset(void)
{
    mcp_checkpoint_groups_reset();
}

/* Auto-snapshot config tool declarations */
extern cJSON* mcp_tool_checkpoint_set_auto_snapshot(cJSON *params);
extern cJSON* mcp_tool_checkpoint_clear_auto_snapshot(cJSON *params);

/* Auto-snapshot config reset function - from libmcp.a */
extern void mcp_auto_snapshot_configs_reset(void);

static void test_auto_snapshot_configs_reset(void)
{
    mcp_auto_snapshot_configs_reset();
}

/* Stub function for creating checkpoints in tests */
extern int mon_breakpoint_add_checkpoint(unsigned int start, unsigned int end,
                                         int stop, int operation,
                                         int temporary, int do_print);

/* Include stdbool for true/false */
#include <stdbool.h>

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

/* Test: watch_add with condition parameter */
TEST(watch_add_with_condition)
{
    cJSON *response, *params, *status_item, *condition_item, *checkpoint_num_item;

    /* Reset checkpoint tracking */
    test_checkpoint_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "$D020");
    cJSON_AddStringToObject(params, "type", "write");
    cJSON_AddStringToObject(params, "condition", "A == $02");

    response = mcp_tools_dispatch("vice.watch.add", params);
    ASSERT_NOT_NULL(response);

    /* Should succeed */
    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Should include the condition in response */
    condition_item = cJSON_GetObjectItem(response, "condition");
    ASSERT_NOT_NULL(condition_item);
    ASSERT_TRUE(cJSON_IsString(condition_item));
    ASSERT_STR_EQ(condition_item->valuestring, "A == $02");

    /* Should have created a checkpoint */
    checkpoint_num_item = cJSON_GetObjectItem(response, "checkpoint_num");
    ASSERT_NOT_NULL(checkpoint_num_item);
    ASSERT_TRUE(cJSON_IsNumber(checkpoint_num_item));
    ASSERT_TRUE(checkpoint_num_item->valueint > 0);

    /* Verify condition was set on the checkpoint */
    ASSERT_TRUE(test_checkpoint_has_condition());

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: watch_add with invalid condition returns error */
TEST(watch_add_with_invalid_condition)
{
    cJSON *response, *params, *code_item;

    /* Reset checkpoint tracking */
    test_checkpoint_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "$D020");
    cJSON_AddStringToObject(params, "type", "write");
    cJSON_AddStringToObject(params, "condition", "invalid_condition_syntax");

    response = mcp_tools_dispatch("vice.watch.add", params);
    ASSERT_NOT_NULL(response);

    /* Should return error for invalid condition (code field present = error response) */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    /* Verify no checkpoint was created (the broken one should have been deleted) */
    /* Note: checkpoint_counter still incremented, but the checkpoint was deleted */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: watch_add without condition still works (backward compatible) */
TEST(watch_add_without_condition)
{
    cJSON *response, *params, *status_item, *condition_item;

    /* Reset checkpoint tracking */
    test_checkpoint_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "$D020");
    cJSON_AddStringToObject(params, "type", "write");
    /* No condition parameter */

    response = mcp_tools_dispatch("vice.watch.add", params);
    ASSERT_NOT_NULL(response);

    /* Should succeed */
    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Should NOT have condition in response when not provided */
    condition_item = cJSON_GetObjectItem(response, "condition");
    ASSERT_TRUE(condition_item == NULL);

    /* Verify no condition was set */
    ASSERT_TRUE(!test_checkpoint_has_condition());

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: watch_add with hex PC condition */
TEST(watch_add_with_pc_condition)
{
    cJSON *response, *params, *status_item, *condition_item;

    /* Reset checkpoint tracking */
    test_checkpoint_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "address", "$0400");
    cJSON_AddNumberToObject(params, "size", 1000);
    cJSON_AddStringToObject(params, "type", "write");
    cJSON_AddStringToObject(params, "condition", "PC == $1000");

    response = mcp_tools_dispatch("vice.watch.add", params);
    ASSERT_NOT_NULL(response);

    /* Should succeed */
    status_item = cJSON_GetObjectItem(response, "status");
    ASSERT_NOT_NULL(status_item);
    ASSERT_STR_EQ(status_item->valuestring, "ok");

    /* Should include the condition in response */
    condition_item = cJSON_GetObjectItem(response, "condition");
    ASSERT_NOT_NULL(condition_item);
    ASSERT_STR_EQ(condition_item->valuestring, "PC == $1000");

    /* Verify condition was set */
    ASSERT_TRUE(test_checkpoint_has_condition());

    cJSON_Delete(params);
    cJSON_Delete(response);
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

/* Test: Snapshot load with valid name succeeds */
TEST(snapshot_load_with_name_succeeds)
{
    cJSON *response, *params;
    cJSON *name_item;

    test_snapshot_reset();
    test_snapshot_set_load_result(0);  /* Success */

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_debug_state");

    response = mcp_tool_snapshot_load(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return name */
    name_item = cJSON_GetObjectItem(response, "name");
    ASSERT_NOT_NULL(name_item);
    ASSERT_STR_EQ(name_item->valuestring, "test_debug_state");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot load without name returns error */
TEST(snapshot_load_without_name_returns_error)
{
    cJSON *response, *params, *code_item;

    test_snapshot_reset();

    params = cJSON_CreateObject();

    response = mcp_tool_snapshot_load(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, -32602);  /* Invalid params */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot load failure returns error */
TEST(snapshot_load_failure_returns_error)
{
    cJSON *response, *params, *code_item;

    test_snapshot_reset();
    test_snapshot_set_load_result(-1);  /* Simulate failure */

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "nonexistent");

    response = mcp_tool_snapshot_load(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, -32004);  /* Snapshot failed */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: Snapshot list returns array */
TEST(snapshot_list_returns_array)
{
    cJSON *response;
    cJSON *snapshots_item;

    response = mcp_tool_snapshot_list(NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return snapshots array */
    snapshots_item = cJSON_GetObjectItem(response, "snapshots");
    ASSERT_NOT_NULL(snapshots_item);
    ASSERT_TRUE(cJSON_IsArray(snapshots_item));

    cJSON_Delete(response);
}

/* Test: Snapshot list returns directory path */
TEST(snapshot_list_returns_directory)
{
    cJSON *response;
    cJSON *dir_item;

    response = mcp_tool_snapshot_list(NULL);
    ASSERT_NOT_NULL(response);

    /* Should return snapshots_directory */
    dir_item = cJSON_GetObjectItem(response, "snapshots_directory");
    ASSERT_NOT_NULL(dir_item);
    ASSERT_TRUE(cJSON_IsString(dir_item));
    ASSERT_TRUE(strstr(dir_item->valuestring, "mcp_snapshots") != NULL);

    cJSON_Delete(response);
}

/* ========================================================================= */
/* Memory Search Tests                                                       */
/* ========================================================================= */

/* Test: memory.search requires start parameter */
TEST(memory_search_requires_start)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "end", 0x1000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x4C}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search requires end parameter */
TEST(memory_search_requires_end)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x4C}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search requires pattern parameter */
TEST(memory_search_requires_pattern)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x1000);

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search finds single byte pattern */
TEST(memory_search_finds_single_byte)
{
    cJSON *response, *params, *matches_item, *match_item;

    /* Set up memory with pattern at known locations */
    test_memory_clear();
    test_memory_set_byte(0x1000, 0xEA);
    test_memory_set_byte(0x2000, 0xEA);
    test_memory_set_byte(0x3000, 0xEA);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x4000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_TRUE(cJSON_IsArray(matches_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(matches_item), 3);

    /* Check first match is $1000 */
    match_item = cJSON_GetArrayItem(matches_item, 0);
    ASSERT_NOT_NULL(match_item);
    ASSERT_TRUE(cJSON_IsString(match_item));
    ASSERT_STR_EQ(match_item->valuestring, "$1000");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search finds multi-byte pattern (JMP instruction) */
TEST(memory_search_finds_jmp_instruction)
{
    cJSON *response, *params, *matches_item, *total_item;
    /* JMP $A000 = 4C 00 A0 */
    uint8_t pattern[] = { 0x4C, 0x00, 0xA0 };

    test_memory_clear();
    /* Place JMP $A000 at a few locations */
    test_memory_set(0x1000, pattern, 3);
    test_memory_set(0x2000, pattern, 3);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x3000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x4C, 0x00, 0xA0}, 3));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_INT_EQ(cJSON_GetArraySize(matches_item), 2);

    total_item = cJSON_GetObjectItem(response, "total_matches");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 2);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search with wildcard mask */
TEST(memory_search_with_wildcard_mask)
{
    cJSON *response, *params, *matches_item;
    /* Search for JMP to any low-byte, high-byte A0 */
    /* 4C xx A0 where xx is wildcard */
    uint8_t jmp1[] = { 0x4C, 0x00, 0xA0 };  /* JMP $A000 */
    uint8_t jmp2[] = { 0x4C, 0x50, 0xA0 };  /* JMP $A050 */
    uint8_t jmp3[] = { 0x4C, 0xFF, 0xA0 };  /* JMP $A0FF */

    test_memory_clear();
    test_memory_set(0x1000, jmp1, 3);
    test_memory_set(0x2000, jmp2, 3);
    test_memory_set(0x3000, jmp3, 3);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x4000);
    /* Pattern: 4C xx A0 with mask FF 00 FF (00 = wildcard) */
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x4C, 0x00, 0xA0}, 3));
    cJSON_AddItemToObject(params, "mask", cJSON_CreateIntArray((const int[]){0xFF, 0x00, 0xFF}, 3));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_INT_EQ(cJSON_GetArraySize(matches_item), 3);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search respects max_results limit */
TEST(memory_search_respects_max_results)
{
    cJSON *response, *params, *matches_item, *truncated_item;
    int i;

    test_memory_clear();
    /* Place pattern at many locations */
    for (i = 0; i < 200; i++) {
        test_memory_set_byte(i * 256, 0xEA);
    }

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0xFFFF);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));
    cJSON_AddNumberToObject(params, "max_results", 10);

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_INT_EQ(cJSON_GetArraySize(matches_item), 10);

    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsTrue(truncated_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search returns truncated=false when not truncated */
TEST(memory_search_truncated_false_when_not_truncated)
{
    cJSON *response, *params, *truncated_item;

    test_memory_clear();
    test_memory_set_byte(0x1000, 0xEA);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x2000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsFalse(truncated_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search with hex string addresses */
TEST(memory_search_hex_string_addresses)
{
    cJSON *response, *params, *matches_item;

    test_memory_clear();
    test_memory_set_byte(0xC100, 0xEA);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "start", "$C000");
    cJSON_AddStringToObject(params, "end", "$C200");
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_INT_EQ(cJSON_GetArraySize(matches_item), 1);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search end must be greater than start */
TEST(memory_search_invalid_range_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x2000);
    cJSON_AddNumberToObject(params, "end", 0x1000);  /* end < start */
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search empty pattern returns error */
TEST(memory_search_empty_pattern_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x1000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateArray());  /* Empty array */

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search mask length must match pattern length */
TEST(memory_search_mask_length_mismatch_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x1000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x4C, 0x00, 0xA0}, 3));
    cJSON_AddItemToObject(params, "mask", cJSON_CreateIntArray((const int[]){0xFF, 0xFF}, 2));  /* Wrong length */

    response = mcp_tool_memory_search(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.search via dispatch */
TEST(memory_search_dispatch_works)
{
    cJSON *response, *params, *matches_item;

    test_memory_clear();
    test_memory_set_byte(0x1000, 0xEA);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0000);
    cJSON_AddNumberToObject(params, "end", 0x2000);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tools_dispatch("vice.memory.search", params);
    ASSERT_NOT_NULL(response);

    matches_item = cJSON_GetObjectItem(response, "matches");
    ASSERT_NOT_NULL(matches_item);
    ASSERT_TRUE(cJSON_IsArray(matches_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Cycles Stopwatch Tests
 * ================================================================= */

/* Test: cycles.stopwatch requires action parameter */
TEST(cycles_stopwatch_requires_action)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    /* No action parameter */

    response = mcp_tool_cycles_stopwatch(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: cycles.stopwatch with null params returns error */
TEST(cycles_stopwatch_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_cycles_stopwatch(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: cycles.stopwatch with invalid action returns error */
TEST(cycles_stopwatch_invalid_action_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "action", "invalid");

    response = mcp_tool_cycles_stopwatch(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: cycles.stopwatch reset action works */
TEST(cycles_stopwatch_reset_works)
{
    cJSON *response, *params, *cycles_item, *memspace_item;

    /* Set some cycles first */
    test_stopwatch_set_cycles(12345);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "action", "reset");

    response = mcp_tool_cycles_stopwatch(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return cycles = 0 after reset */
    cycles_item = cJSON_GetObjectItem(response, "cycles");
    ASSERT_NOT_NULL(cycles_item);
    ASSERT_INT_EQ(cycles_item->valueint, 0);

    /* Should return memspace */
    memspace_item = cJSON_GetObjectItem(response, "memspace");
    ASSERT_NOT_NULL(memspace_item);
    ASSERT_STR_EQ(memspace_item->valuestring, "computer");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: cycles.stopwatch read action returns cycle count */
TEST(cycles_stopwatch_read_returns_cycles)
{
    cJSON *response, *params, *cycles_item, *memspace_item;

    /* Set specific cycle count */
    test_stopwatch_reset();
    test_stopwatch_set_cycles(19656);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "action", "read");

    response = mcp_tool_cycles_stopwatch(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return cycles count */
    cycles_item = cJSON_GetObjectItem(response, "cycles");
    ASSERT_NOT_NULL(cycles_item);
    ASSERT_INT_EQ(cycles_item->valueint, 19656);

    /* Should return memspace */
    memspace_item = cJSON_GetObjectItem(response, "memspace");
    ASSERT_NOT_NULL(memspace_item);
    ASSERT_STR_EQ(memspace_item->valuestring, "computer");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: cycles.stopwatch reset_and_read action works */
TEST(cycles_stopwatch_reset_and_read_works)
{
    cJSON *response, *params, *cycles_item, *previous_item, *memspace_item;

    /* Set specific cycle count */
    test_stopwatch_reset();
    test_stopwatch_set_cycles(5000);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "action", "reset_and_read");

    response = mcp_tool_cycles_stopwatch(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return previous_cycles with old value */
    previous_item = cJSON_GetObjectItem(response, "previous_cycles");
    ASSERT_NOT_NULL(previous_item);
    ASSERT_INT_EQ(previous_item->valueint, 5000);

    /* Should return cycles = 0 after reset */
    cycles_item = cJSON_GetObjectItem(response, "cycles");
    ASSERT_NOT_NULL(cycles_item);
    ASSERT_INT_EQ(cycles_item->valueint, 0);

    /* Should return memspace */
    memspace_item = cJSON_GetObjectItem(response, "memspace");
    ASSERT_NOT_NULL(memspace_item);
    ASSERT_STR_EQ(memspace_item->valuestring, "computer");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: cycles.stopwatch dispatch works */
TEST(cycles_stopwatch_dispatch_works)
{
    cJSON *response, *params, *cycles_item;

    test_stopwatch_reset();
    test_stopwatch_set_cycles(1000);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "action", "read");

    response = mcp_tools_dispatch("vice.cycles.stopwatch", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cycles_item = cJSON_GetObjectItem(response, "cycles");
    ASSERT_NOT_NULL(cycles_item);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Memory Fill Tests
 * ================================================================= */

/* Test: memory.fill requires start parameter */
TEST(memory_fill_requires_start)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "end", 0x07FF);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill requires end parameter */
TEST(memory_fill_requires_end)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill requires pattern parameter */
TEST(memory_fill_requires_pattern)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddNumberToObject(params, "end", 0x07FF);

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill null params returns error */
TEST(memory_fill_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_memory_fill(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: memory.fill with single byte pattern (zero-fill) */
TEST(memory_fill_single_byte_pattern)
{
    cJSON *response, *params, *bytes_item, *reps_item;

    /* Clear memory first */
    test_memory_clear();
    /* Set some non-zero values */
    test_memory_set_byte(0x0400, 0xFF);
    test_memory_set_byte(0x0401, 0xFF);
    test_memory_set_byte(0x0402, 0xFF);

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddNumberToObject(params, "end", 0x0403);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return bytes_written */
    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);
    ASSERT_INT_EQ(bytes_item->valueint, 4);  /* 0x0400-0x0403 inclusive = 4 bytes */

    /* Should return pattern_repetitions */
    reps_item = cJSON_GetObjectItem(response, "pattern_repetitions");
    ASSERT_NOT_NULL(reps_item);
    ASSERT_INT_EQ(reps_item->valueint, 4);  /* Single byte pattern repeated 4 times */

    /* Verify memory was actually filled */
    ASSERT_INT_EQ(test_memory_get_byte(0x0400), 0);
    ASSERT_INT_EQ(test_memory_get_byte(0x0401), 0);
    ASSERT_INT_EQ(test_memory_get_byte(0x0402), 0);
    ASSERT_INT_EQ(test_memory_get_byte(0x0403), 0);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with screen space pattern */
TEST(memory_fill_screen_spaces)
{
    cJSON *response, *params, *bytes_item;

    test_memory_clear();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddNumberToObject(params, "end", 0x07FF);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){32}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* 0x0400-0x07FF = 1024 bytes */
    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);
    ASSERT_INT_EQ(bytes_item->valueint, 1024);

    /* Verify first and last bytes */
    ASSERT_INT_EQ(test_memory_get_byte(0x0400), 32);
    ASSERT_INT_EQ(test_memory_get_byte(0x07FF), 32);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with two-byte alternating pattern */
TEST(memory_fill_alternating_pattern)
{
    cJSON *response, *params, *bytes_item, *reps_item;

    test_memory_clear();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x2000);
    cJSON_AddNumberToObject(params, "end", 0x2007);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xAA, 0x55}, 2));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* 8 bytes filled */
    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);
    ASSERT_INT_EQ(bytes_item->valueint, 8);

    /* 4 complete repetitions of 2-byte pattern */
    reps_item = cJSON_GetObjectItem(response, "pattern_repetitions");
    ASSERT_NOT_NULL(reps_item);
    ASSERT_INT_EQ(reps_item->valueint, 4);

    /* Verify pattern */
    ASSERT_INT_EQ(test_memory_get_byte(0x2000), 0xAA);
    ASSERT_INT_EQ(test_memory_get_byte(0x2001), 0x55);
    ASSERT_INT_EQ(test_memory_get_byte(0x2002), 0xAA);
    ASSERT_INT_EQ(test_memory_get_byte(0x2003), 0x55);
    ASSERT_INT_EQ(test_memory_get_byte(0x2004), 0xAA);
    ASSERT_INT_EQ(test_memory_get_byte(0x2005), 0x55);
    ASSERT_INT_EQ(test_memory_get_byte(0x2006), 0xAA);
    ASSERT_INT_EQ(test_memory_get_byte(0x2007), 0x55);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with partial pattern repetition */
TEST(memory_fill_partial_pattern)
{
    cJSON *response, *params, *bytes_item, *reps_item;

    test_memory_clear();

    /* 3-byte pattern into 7 bytes = 2 full reps + 1 partial byte */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x3000);
    cJSON_AddNumberToObject(params, "end", 0x3006);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x11, 0x22, 0x33}, 3));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* 7 bytes filled */
    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);
    ASSERT_INT_EQ(bytes_item->valueint, 7);

    /* 2 complete repetitions */
    reps_item = cJSON_GetObjectItem(response, "pattern_repetitions");
    ASSERT_NOT_NULL(reps_item);
    ASSERT_INT_EQ(reps_item->valueint, 2);

    /* Verify pattern including partial */
    ASSERT_INT_EQ(test_memory_get_byte(0x3000), 0x11);
    ASSERT_INT_EQ(test_memory_get_byte(0x3001), 0x22);
    ASSERT_INT_EQ(test_memory_get_byte(0x3002), 0x33);
    ASSERT_INT_EQ(test_memory_get_byte(0x3003), 0x11);
    ASSERT_INT_EQ(test_memory_get_byte(0x3004), 0x22);
    ASSERT_INT_EQ(test_memory_get_byte(0x3005), 0x33);
    ASSERT_INT_EQ(test_memory_get_byte(0x3006), 0x11);  /* Partial repeat */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with hex string addresses */
TEST(memory_fill_hex_string_addresses)
{
    cJSON *response, *params, *bytes_item;

    test_memory_clear();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "start", "$C000");
    cJSON_AddStringToObject(params, "end", "$C0FF");
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0xEA}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* 256 bytes */
    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);
    ASSERT_INT_EQ(bytes_item->valueint, 256);

    /* Verify NOP sled */
    ASSERT_INT_EQ(test_memory_get_byte(0xC000), 0xEA);
    ASSERT_INT_EQ(test_memory_get_byte(0xC0FF), 0xEA);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill empty pattern returns error */
TEST(memory_fill_empty_pattern_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddNumberToObject(params, "end", 0x07FF);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateArray());

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with start > end returns error */
TEST(memory_fill_invalid_range_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x07FF);
    cJSON_AddNumberToObject(params, "end", 0x0400);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill with invalid byte value returns error */
TEST(memory_fill_invalid_byte_value_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x0400);
    cJSON_AddNumberToObject(params, "end", 0x07FF);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){256}, 1));

    response = mcp_tool_memory_fill(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.fill dispatch works */
TEST(memory_fill_dispatch_works)
{
    cJSON *response, *params, *bytes_item;

    test_memory_clear();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "start", 0x1000);
    cJSON_AddNumberToObject(params, "end", 0x1003);
    cJSON_AddItemToObject(params, "pattern", cJSON_CreateIntArray((const int[]){0x42}, 1));

    response = mcp_tools_dispatch("vice.memory.fill", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    bytes_item = cJSON_GetObjectItem(response, "bytes_written");
    ASSERT_NOT_NULL(bytes_item);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Memory Compare Tests (ranges mode)
 * ================================================================= */

/* Memory compare tool declaration */
extern cJSON* mcp_tool_memory_compare(cJSON *params);

/* Test: memory.compare ranges mode requires mode parameter */
TEST(memory_compare_ranges_requires_mode)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1010);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);
    /* No mode parameter */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode requires range1_start */
TEST(memory_compare_ranges_requires_range1_start)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_end", 0x1010);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode requires range1_end */
TEST(memory_compare_ranges_requires_range1_end)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode requires range2_start */
TEST(memory_compare_ranges_requires_range2_start)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1010);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode null params returns error */
TEST(memory_compare_ranges_null_params_returns_error)
{
    cJSON *response, *code_item;

    response = mcp_tool_memory_compare(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode invalid mode value */
TEST(memory_compare_invalid_mode_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "invalid_mode");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1010);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode range1_end < range1_start returns error */
TEST(memory_compare_ranges_invalid_range1_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x2000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1000);  /* end < start */
    cJSON_AddNumberToObject(params, "range2_start", 0x3000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode identical regions returns no differences */
TEST(memory_compare_ranges_identical_returns_no_differences)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *truncated_item;

    test_memory_clear();
    /* Set same data in both ranges */
    test_memory_set_byte(0x1000, 0x42);
    test_memory_set_byte(0x1001, 0x43);
    test_memory_set_byte(0x1002, 0x44);
    test_memory_set_byte(0x2000, 0x42);
    test_memory_set_byte(0x2001, 0x43);
    test_memory_set_byte(0x2002, 0x44);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1002);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return empty differences array */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_TRUE(cJSON_IsArray(differences_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(differences_item), 0);

    /* Should return total_differences = 0 */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 0);

    /* Should return truncated = false */
    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsFalse(truncated_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode finds differences */
TEST(memory_compare_ranges_finds_differences)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *diff0, *addr_item, *cur_item, *ref_item;

    test_memory_clear();
    /* Set different data in ranges */
    test_memory_set_byte(0x1000, 0x42);
    test_memory_set_byte(0x1001, 0xAA);  /* Different */
    test_memory_set_byte(0x1002, 0x44);
    test_memory_set_byte(0x2000, 0x42);
    test_memory_set_byte(0x2001, 0xBB);  /* Different */
    test_memory_set_byte(0x2002, 0x44);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1002);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return 1 difference */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 1);

    /* Should have 1 item in differences array */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_TRUE(cJSON_IsArray(differences_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(differences_item), 1);

    /* Check difference details */
    diff0 = cJSON_GetArrayItem(differences_item, 0);
    ASSERT_NOT_NULL(diff0);

    addr_item = cJSON_GetObjectItem(diff0, "address");
    ASSERT_NOT_NULL(addr_item);
    ASSERT_STR_EQ(addr_item->valuestring, "$1001");

    cur_item = cJSON_GetObjectItem(diff0, "current");
    ASSERT_NOT_NULL(cur_item);
    ASSERT_INT_EQ(cur_item->valueint, 0xAA);

    ref_item = cJSON_GetObjectItem(diff0, "reference");
    ASSERT_NOT_NULL(ref_item);
    ASSERT_INT_EQ(ref_item->valueint, 0xBB);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode respects max_differences */
TEST(memory_compare_ranges_respects_max_differences)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *truncated_item;
    int i;

    test_memory_clear();
    /* Set 10 different bytes in range 1 vs range 2 */
    for (i = 0; i < 10; i++) {
        test_memory_set_byte(0x1000 + i, 0x00);  /* zeros in range 1 */
        test_memory_set_byte(0x2000 + i, 0xFF);  /* 0xFF in range 2 */
    }

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1009);  /* 10 bytes */
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);
    cJSON_AddNumberToObject(params, "max_differences", 5);  /* Limit to 5 */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return 10 total differences */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 10);

    /* But differences array should be limited to 5 */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_TRUE(cJSON_IsArray(differences_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(differences_item), 5);

    /* Should return truncated = true */
    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsTrue(truncated_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode with hex string addresses */
TEST(memory_compare_ranges_hex_string_addresses)
{
    cJSON *response, *params;
    cJSON *differences_item;

    test_memory_clear();
    /* Set same data */
    test_memory_set_byte(0x1000, 0x42);
    test_memory_set_byte(0x2000, 0x42);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddStringToObject(params, "range1_start", "$1000");
    cJSON_AddStringToObject(params, "range1_end", "$1000");
    cJSON_AddStringToObject(params, "range2_start", "$2000");

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return empty differences (same data) */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_TRUE(cJSON_IsArray(differences_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(differences_item), 0);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode dispatch works */
TEST(memory_compare_ranges_dispatch_works)
{
    cJSON *response, *params;
    cJSON *differences_item;

    test_memory_clear();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1003);
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tools_dispatch("vice.memory.compare", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_TRUE(cJSON_IsArray(differences_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare ranges mode single byte compare */
TEST(memory_compare_ranges_single_byte)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *diff0, *addr_item;

    test_memory_clear();
    test_memory_set_byte(0x1000, 0xAA);
    test_memory_set_byte(0x2000, 0xBB);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "ranges");
    cJSON_AddNumberToObject(params, "range1_start", 0x1000);
    cJSON_AddNumberToObject(params, "range1_end", 0x1000);  /* Single byte */
    cJSON_AddNumberToObject(params, "range2_start", 0x2000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should return 1 difference */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 1);

    /* Check address format is $1000 */
    differences_item = cJSON_GetObjectItem(response, "differences");
    diff0 = cJSON_GetArrayItem(differences_item, 0);
    addr_item = cJSON_GetObjectItem(diff0, "address");
    ASSERT_STR_EQ(addr_item->valuestring, "$1000");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Memory Compare Tests (snapshot mode)
 * ================================================================= */

/* Test helper declarations for snapshot memory */
extern void test_snapshot_memory_set_byte(uint16_t addr, uint8_t value);
extern void test_snapshot_memory_clear(void);
extern void test_snapshot_memory_invalidate(void);
extern int test_create_mock_vsf(const char *path);

/* Test: memory.compare snapshot mode requires snapshot_name */
TEST(memory_compare_snapshot_requires_snapshot_name)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddNumberToObject(params, "start", 0x1000);
    cJSON_AddNumberToObject(params, "end", 0x1010);
    /* Missing snapshot_name */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode requires start address */
TEST(memory_compare_snapshot_requires_start)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_snap");
    cJSON_AddNumberToObject(params, "end", 0x1010);
    /* Missing start */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode requires end address */
TEST(memory_compare_snapshot_requires_end)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_snap");
    cJSON_AddNumberToObject(params, "start", 0x1000);
    /* Missing end */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode validates range order */
TEST(memory_compare_snapshot_validates_range_order)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_snap");
    cJSON_AddNumberToObject(params, "start", 0x2000);  /* end < start */
    cJSON_AddNumberToObject(params, "end", 0x1000);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode fails gracefully with nonexistent snapshot */
TEST(memory_compare_snapshot_nonexistent_returns_error)
{
    cJSON *response, *params, *code_item;

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "nonexistent_snapshot_12345");
    cJSON_AddNumberToObject(params, "start", 0x1000);
    cJSON_AddNumberToObject(params, "end", 0x1010);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should fail because snapshot doesn't exist */
    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    /* Could be INVALID_PARAMS or SNAPSHOT_FAILED depending on implementation */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode finds differences */
TEST(memory_compare_snapshot_finds_differences)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *diff0;
    cJSON *addr_item, *current_item, *reference_item;
    char vsf_path[256];

    /* Set up current memory */
    test_memory_clear();
    test_memory_set_byte(0x1000, 0xAA);
    test_memory_set_byte(0x1001, 0xBB);
    test_memory_set_byte(0x1002, 0xCC);

    /* Set up snapshot memory (different values) */
    test_snapshot_memory_clear();
    test_snapshot_memory_set_byte(0x1000, 0x11);  /* Different */
    test_snapshot_memory_set_byte(0x1001, 0xBB);  /* Same */
    test_snapshot_memory_set_byte(0x1002, 0x33);  /* Different */

    /* Create mock VSF file */
    snprintf(vsf_path, sizeof(vsf_path), "/tmp/vice-test-config/mcp_snapshots/test_compare.vsf");
    /* Ensure directory exists - archdep_mkdir is stubbed */
    test_create_mock_vsf(vsf_path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_compare");
    cJSON_AddNumberToObject(params, "start", 0x1000);
    cJSON_AddNumberToObject(params, "end", 0x1002);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should find 2 differences (0x1000 and 0x1002) */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 2);

    /* Check first difference */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    diff0 = cJSON_GetArrayItem(differences_item, 0);
    ASSERT_NOT_NULL(diff0);

    addr_item = cJSON_GetObjectItem(diff0, "address");
    ASSERT_NOT_NULL(addr_item);
    ASSERT_STR_EQ(addr_item->valuestring, "$1000");

    current_item = cJSON_GetObjectItem(diff0, "current");
    ASSERT_NOT_NULL(current_item);
    ASSERT_INT_EQ(current_item->valueint, 0xAA);

    reference_item = cJSON_GetObjectItem(diff0, "reference");
    ASSERT_NOT_NULL(reference_item);
    ASSERT_INT_EQ(reference_item->valueint, 0x11);

    /* Clean up */
    remove(vsf_path);
    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode identical memory returns no differences */
TEST(memory_compare_snapshot_identical_returns_no_differences)
{
    cJSON *response, *params;
    cJSON *total_item, *truncated_item;
    char vsf_path[256];

    /* Set up current memory */
    test_memory_clear();
    test_memory_set_byte(0x2000, 0x42);
    test_memory_set_byte(0x2001, 0x43);
    test_memory_set_byte(0x2002, 0x44);

    /* Set up snapshot memory with same values */
    test_snapshot_memory_clear();
    test_snapshot_memory_set_byte(0x2000, 0x42);
    test_snapshot_memory_set_byte(0x2001, 0x43);
    test_snapshot_memory_set_byte(0x2002, 0x44);

    /* Create mock VSF file */
    snprintf(vsf_path, sizeof(vsf_path), "/tmp/vice-test-config/mcp_snapshots/test_identical.vsf");
    test_create_mock_vsf(vsf_path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_identical");
    cJSON_AddNumberToObject(params, "start", 0x2000);
    cJSON_AddNumberToObject(params, "end", 0x2002);

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should find 0 differences */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 0);

    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsFalse(truncated_item));

    /* Clean up */
    remove(vsf_path);
    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode respects max_differences */
TEST(memory_compare_snapshot_respects_max_differences)
{
    cJSON *response, *params;
    cJSON *differences_item, *total_item, *truncated_item;
    char vsf_path[256];
    int i;

    /* Set up current memory with different values than snapshot */
    test_memory_clear();
    test_snapshot_memory_clear();
    for (i = 0; i < 20; i++) {
        test_memory_set_byte(0x3000 + i, (uint8_t)(0x10 + i));
        test_snapshot_memory_set_byte(0x3000 + i, (uint8_t)(0x80 + i));  /* All different */
    }

    /* Create mock VSF file */
    snprintf(vsf_path, sizeof(vsf_path), "/tmp/vice-test-config/mcp_snapshots/test_max_diff.vsf");
    test_create_mock_vsf(vsf_path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_max_diff");
    cJSON_AddNumberToObject(params, "start", 0x3000);
    cJSON_AddNumberToObject(params, "end", 0x3013);  /* 20 bytes */
    cJSON_AddNumberToObject(params, "max_differences", 5);  /* Only return 5 */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should find 20 total differences */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 20);

    /* But only return 5 */
    differences_item = cJSON_GetObjectItem(response, "differences");
    ASSERT_NOT_NULL(differences_item);
    ASSERT_INT_EQ(cJSON_GetArraySize(differences_item), 5);

    /* Should be truncated */
    truncated_item = cJSON_GetObjectItem(response, "truncated");
    ASSERT_NOT_NULL(truncated_item);
    ASSERT_TRUE(cJSON_IsTrue(truncated_item));

    /* Clean up */
    remove(vsf_path);
    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: memory.compare snapshot mode supports hex string addresses */
TEST(memory_compare_snapshot_hex_string_addresses)
{
    cJSON *response, *params;
    cJSON *total_item;
    char vsf_path[256];

    /* Set up current memory */
    test_memory_clear();
    test_memory_set_byte(0x4000, 0xFF);

    /* Set up snapshot memory (different) */
    test_snapshot_memory_clear();
    test_snapshot_memory_set_byte(0x4000, 0x00);

    /* Create mock VSF file */
    snprintf(vsf_path, sizeof(vsf_path), "/tmp/vice-test-config/mcp_snapshots/test_hex.vsf");
    test_create_mock_vsf(vsf_path);

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "mode", "snapshot");
    cJSON_AddStringToObject(params, "snapshot_name", "test_hex");
    cJSON_AddStringToObject(params, "start", "$4000");  /* Hex string */
    cJSON_AddStringToObject(params, "end", "$4000");    /* Single byte */

    response = mcp_tool_memory_compare(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should find 1 difference */
    total_item = cJSON_GetObjectItem(response, "total_differences");
    ASSERT_NOT_NULL(total_item);
    ASSERT_INT_EQ(total_item->valueint, 1);

    /* Clean up */
    remove(vsf_path);
    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Checkpoint Group Tests
 * ================================================================= */

/* Test: checkpoint.group.create requires name */
TEST(checkpoint_group_create_requires_name)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();  /* Missing name */

    response = mcp_tool_checkpoint_group_create(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.create returns null params error */
TEST(checkpoint_group_create_null_params_returns_error)
{
    cJSON *response, *code_item;

    test_checkpoint_groups_reset();

    response = mcp_tool_checkpoint_group_create(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(response);
}

/* Test: checkpoint.group.create with name succeeds */
TEST(checkpoint_group_create_with_name_succeeds)
{
    cJSON *response, *params;
    cJSON *created_item, *name_item;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_group");

    response = mcp_tool_checkpoint_group_create(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have created:true */
    created_item = cJSON_GetObjectItem(response, "created");
    ASSERT_NOT_NULL(created_item);
    ASSERT_TRUE(cJSON_IsTrue(created_item));

    /* Should have name */
    name_item = cJSON_GetObjectItem(response, "name");
    ASSERT_NOT_NULL(name_item);
    ASSERT_STR_EQ(name_item->valuestring, "test_group");

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.create with checkpoint_ids */
TEST(checkpoint_group_create_with_checkpoint_ids)
{
    cJSON *response, *params, *ids_array;
    cJSON *created_item;

    test_checkpoint_groups_reset();
    test_checkpoint_reset();

    /* Create some checkpoints first */
    mon_breakpoint_add_checkpoint(0x1000, 0x1000, 1, 4, 0, 1);  /* Creates checkpoint 1 */
    mon_breakpoint_add_checkpoint(0x2000, 0x2000, 1, 4, 0, 1);  /* Creates checkpoint 2 */

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "my_breakpoints");
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(2));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);

    response = mcp_tool_checkpoint_group_create(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have created:true */
    created_item = cJSON_GetObjectItem(response, "created");
    ASSERT_NOT_NULL(created_item);
    ASSERT_TRUE(cJSON_IsTrue(created_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.create with duplicate name returns error */
TEST(checkpoint_group_create_duplicate_name_returns_error)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    /* Create first group */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "dup_group");
    response = mcp_tool_checkpoint_group_create(params);
    ASSERT_NOT_NULL(response);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Try to create second group with same name */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "dup_group");
    response = mcp_tool_checkpoint_group_create(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.add requires group */
TEST(checkpoint_group_add_requires_group)
{
    cJSON *response, *params, *code_item, *ids_array;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);
    /* Missing group */

    response = mcp_tool_checkpoint_group_add(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.add requires checkpoint_ids */
TEST(checkpoint_group_add_requires_checkpoint_ids)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    /* Create a group first */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_group");
    response = mcp_tool_checkpoint_group_create(params);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Try to add without checkpoint_ids */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "test_group");
    /* Missing checkpoint_ids */

    response = mcp_tool_checkpoint_group_add(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.add with valid params succeeds */
TEST(checkpoint_group_add_succeeds)
{
    cJSON *response, *params, *ids_array;
    cJSON *added_item;

    test_checkpoint_groups_reset();
    test_checkpoint_reset();

    /* Create some checkpoints */
    mon_breakpoint_add_checkpoint(0x1000, 0x1000, 1, 4, 0, 1);  /* Creates checkpoint 1 */
    mon_breakpoint_add_checkpoint(0x2000, 0x2000, 1, 4, 0, 1);  /* Creates checkpoint 2 */

    /* Create a group first */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_group");
    response = mcp_tool_checkpoint_group_create(params);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Add checkpoints to the group */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "test_group");
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(2));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);

    response = mcp_tool_checkpoint_group_add(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have added:2 */
    added_item = cJSON_GetObjectItem(response, "added");
    ASSERT_NOT_NULL(added_item);
    ASSERT_INT_EQ(added_item->valueint, 2);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.add nonexistent group returns error */
TEST(checkpoint_group_add_nonexistent_group_returns_error)
{
    cJSON *response, *params, *code_item, *ids_array;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "nonexistent_group");
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);

    response = mcp_tool_checkpoint_group_add(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.toggle requires group */
TEST(checkpoint_group_toggle_requires_group)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "enabled", true);
    /* Missing group */

    response = mcp_tool_checkpoint_group_toggle(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.toggle requires enabled */
TEST(checkpoint_group_toggle_requires_enabled)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    /* Create a group first */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_group");
    response = mcp_tool_checkpoint_group_create(params);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Try to toggle without enabled */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "test_group");
    /* Missing enabled */

    response = mcp_tool_checkpoint_group_toggle(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.toggle succeeds */
TEST(checkpoint_group_toggle_succeeds)
{
    cJSON *response, *params, *ids_array;
    cJSON *affected_item;

    test_checkpoint_groups_reset();
    test_checkpoint_reset();

    /* Create some checkpoints */
    mon_breakpoint_add_checkpoint(0x1000, 0x1000, 1, 4, 0, 1);  /* Creates checkpoint 1 */
    mon_breakpoint_add_checkpoint(0x2000, 0x2000, 1, 4, 0, 1);  /* Creates checkpoint 2 */

    /* Create group with checkpoints */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "test_group");
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(2));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);
    response = mcp_tool_checkpoint_group_create(params);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Toggle the group off */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "test_group");
    cJSON_AddBoolToObject(params, "enabled", false);

    response = mcp_tool_checkpoint_group_toggle(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have affected_count:2 */
    affected_item = cJSON_GetObjectItem(response, "affected_count");
    ASSERT_NOT_NULL(affected_item);
    ASSERT_INT_EQ(affected_item->valueint, 2);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.toggle nonexistent group returns error */
TEST(checkpoint_group_toggle_nonexistent_group_returns_error)
{
    cJSON *response, *params, *code_item;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "group", "nonexistent");
    cJSON_AddBoolToObject(params, "enabled", true);

    response = mcp_tool_checkpoint_group_toggle(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_INT_EQ(code_item->valueint, MCP_ERROR_INVALID_PARAMS);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.group.list returns empty array when no groups */
TEST(checkpoint_group_list_returns_empty_array)
{
    cJSON *response;
    cJSON *groups_item;

    test_checkpoint_groups_reset();

    response = mcp_tool_checkpoint_group_list(NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have groups array */
    groups_item = cJSON_GetObjectItem(response, "groups");
    ASSERT_NOT_NULL(groups_item);
    ASSERT_TRUE(cJSON_IsArray(groups_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(groups_item), 0);

    cJSON_Delete(response);
}

/* Test: checkpoint.group.list returns groups with details */
TEST(checkpoint_group_list_returns_groups_with_details)
{
    cJSON *response, *params, *ids_array;
    cJSON *groups_item, *group_obj, *name_item, *ids_item, *enabled_item, *disabled_item;

    test_checkpoint_groups_reset();
    test_checkpoint_reset();

    /* Create some checkpoints */
    mon_breakpoint_add_checkpoint(0x1000, 0x1000, 1, 4, 0, 1);  /* Creates checkpoint 1 */
    mon_breakpoint_add_checkpoint(0x2000, 0x2000, 1, 4, 0, 1);  /* Creates checkpoint 2 */

    /* Create a group with checkpoints */
    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "my_group");
    ids_array = cJSON_CreateArray();
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(1));
    cJSON_AddItemToArray(ids_array, cJSON_CreateNumber(2));
    cJSON_AddItemToObject(params, "checkpoint_ids", ids_array);
    response = mcp_tool_checkpoint_group_create(params);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* List groups */
    response = mcp_tool_checkpoint_group_list(NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have groups array with one group */
    groups_item = cJSON_GetObjectItem(response, "groups");
    ASSERT_NOT_NULL(groups_item);
    ASSERT_TRUE(cJSON_IsArray(groups_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(groups_item), 1);

    /* Check the group details */
    group_obj = cJSON_GetArrayItem(groups_item, 0);
    ASSERT_NOT_NULL(group_obj);

    name_item = cJSON_GetObjectItem(group_obj, "name");
    ASSERT_NOT_NULL(name_item);
    ASSERT_STR_EQ(name_item->valuestring, "my_group");

    ids_item = cJSON_GetObjectItem(group_obj, "checkpoint_ids");
    ASSERT_NOT_NULL(ids_item);
    ASSERT_TRUE(cJSON_IsArray(ids_item));
    ASSERT_INT_EQ(cJSON_GetArraySize(ids_item), 2);

    enabled_item = cJSON_GetObjectItem(group_obj, "enabled_count");
    ASSERT_NOT_NULL(enabled_item);

    disabled_item = cJSON_GetObjectItem(group_obj, "disabled_count");
    ASSERT_NOT_NULL(disabled_item);

    cJSON_Delete(response);
}

/* Test: checkpoint.group.list dispatch works */
TEST(checkpoint_group_list_dispatch_works)
{
    cJSON *response;
    cJSON *groups_item;

    test_checkpoint_groups_reset();

    response = mcp_tools_dispatch("vice.checkpoint.group.list", NULL);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    /* Should have groups array */
    groups_item = cJSON_GetObjectItem(response, "groups");
    ASSERT_NOT_NULL(groups_item);
    ASSERT_TRUE(cJSON_IsArray(groups_item));

    cJSON_Delete(response);
}

/* Test: checkpoint.group.create dispatch works */
TEST(checkpoint_group_create_dispatch_works)
{
    cJSON *response, *params;
    cJSON *created_item;

    test_checkpoint_groups_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", "dispatch_test");

    response = mcp_tools_dispatch("vice.checkpoint.group.create", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    created_item = cJSON_GetObjectItem(response, "created");
    ASSERT_NOT_NULL(created_item);
    ASSERT_TRUE(cJSON_IsTrue(created_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* =================================================================
 * Auto-Snapshot Configuration Tests
 * ================================================================= */

/* Test: checkpoint.set_auto_snapshot requires checkpoint_id */
TEST(checkpoint_set_auto_snapshot_requires_checkpoint_id)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "snapshot_prefix", "test_snap");
    /* Missing checkpoint_id */

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_TRUE(code_item->valueint == -32602);  /* INVALID_PARAMS */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot requires snapshot_prefix */
TEST(checkpoint_set_auto_snapshot_requires_snapshot_prefix)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    /* Missing snapshot_prefix */

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_TRUE(code_item->valueint == -32602);  /* INVALID_PARAMS */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot with null params returns error */
TEST(checkpoint_set_auto_snapshot_null_params_returns_error)
{
    cJSON *response, *code_item;

    test_auto_snapshot_configs_reset();

    response = mcp_tool_checkpoint_set_auto_snapshot(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(cJSON_IsNumber(code_item));
    ASSERT_TRUE(code_item->valueint == -32602);  /* INVALID_PARAMS */

    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot rejects empty prefix */
TEST(checkpoint_set_auto_snapshot_rejects_empty_prefix)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "");

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot rejects invalid prefix characters */
TEST(checkpoint_set_auto_snapshot_rejects_invalid_prefix)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "test/snap");  /* Contains slash */

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot rejects invalid checkpoint_id */
TEST(checkpoint_set_auto_snapshot_rejects_invalid_checkpoint_id)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 0);  /* Invalid: must be >= 1 */
    cJSON_AddStringToObject(params, "snapshot_prefix", "test_snap");

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot with valid params succeeds */
TEST(checkpoint_set_auto_snapshot_with_valid_params_succeeds)
{
    cJSON *response, *params;
    cJSON *enabled_item, *cp_id_item, *prefix_item, *max_item, *disks_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "ai_move");

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    enabled_item = cJSON_GetObjectItem(response, "enabled");
    ASSERT_NOT_NULL(enabled_item);
    ASSERT_TRUE(cJSON_IsTrue(enabled_item));

    cp_id_item = cJSON_GetObjectItem(response, "checkpoint_id");
    ASSERT_NOT_NULL(cp_id_item);
    ASSERT_TRUE(cp_id_item->valueint == 1);

    prefix_item = cJSON_GetObjectItem(response, "snapshot_prefix");
    ASSERT_NOT_NULL(prefix_item);
    ASSERT_TRUE(strcmp(prefix_item->valuestring, "ai_move") == 0);

    max_item = cJSON_GetObjectItem(response, "max_snapshots");
    ASSERT_NOT_NULL(max_item);
    ASSERT_TRUE(max_item->valueint == 10);  /* Default */

    disks_item = cJSON_GetObjectItem(response, "include_disks");
    ASSERT_NOT_NULL(disks_item);
    ASSERT_TRUE(cJSON_IsFalse(disks_item));  /* Default */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot with all options */
TEST(checkpoint_set_auto_snapshot_with_all_options)
{
    cJSON *response, *params;
    cJSON *max_item, *disks_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 5);
    cJSON_AddStringToObject(params, "snapshot_prefix", "debug_trace");
    cJSON_AddNumberToObject(params, "max_snapshots", 25);
    cJSON_AddBoolToObject(params, "include_disks", true);

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    max_item = cJSON_GetObjectItem(response, "max_snapshots");
    ASSERT_NOT_NULL(max_item);
    ASSERT_TRUE(max_item->valueint == 25);

    disks_item = cJSON_GetObjectItem(response, "include_disks");
    ASSERT_NOT_NULL(disks_item);
    ASSERT_TRUE(cJSON_IsTrue(disks_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot clamps max_snapshots */
TEST(checkpoint_set_auto_snapshot_clamps_max_snapshots)
{
    cJSON *response, *params;
    cJSON *max_item;

    test_auto_snapshot_configs_reset();

    /* Test lower bound clamp */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "test");
    cJSON_AddNumberToObject(params, "max_snapshots", 0);

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    max_item = cJSON_GetObjectItem(response, "max_snapshots");
    ASSERT_NOT_NULL(max_item);
    ASSERT_TRUE(max_item->valueint == 1);  /* Clamped to minimum */

    cJSON_Delete(params);
    cJSON_Delete(response);

    test_auto_snapshot_configs_reset();

    /* Test upper bound clamp */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 2);
    cJSON_AddStringToObject(params, "snapshot_prefix", "test2");
    cJSON_AddNumberToObject(params, "max_snapshots", 5000);

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    max_item = cJSON_GetObjectItem(response, "max_snapshots");
    ASSERT_NOT_NULL(max_item);
    ASSERT_TRUE(max_item->valueint == 999);  /* Clamped to maximum */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot can update existing config */
TEST(checkpoint_set_auto_snapshot_updates_existing_config)
{
    cJSON *response, *params;
    cJSON *max_item;

    test_auto_snapshot_configs_reset();

    /* Create initial config */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "original");
    cJSON_AddNumberToObject(params, "max_snapshots", 10);

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Update with new values */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "updated");
    cJSON_AddNumberToObject(params, "max_snapshots", 20);

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cJSON *prefix_item = cJSON_GetObjectItem(response, "snapshot_prefix");
    ASSERT_NOT_NULL(prefix_item);
    ASSERT_TRUE(strcmp(prefix_item->valuestring, "updated") == 0);

    max_item = cJSON_GetObjectItem(response, "max_snapshots");
    ASSERT_NOT_NULL(max_item);
    ASSERT_TRUE(max_item->valueint == 20);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot requires checkpoint_id */
TEST(checkpoint_clear_auto_snapshot_requires_checkpoint_id)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();  /* Missing checkpoint_id */

    response = mcp_tool_checkpoint_clear_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot with null params returns error */
TEST(checkpoint_clear_auto_snapshot_null_params_returns_error)
{
    cJSON *response, *code_item;

    test_auto_snapshot_configs_reset();

    response = mcp_tool_checkpoint_clear_auto_snapshot(NULL);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot rejects invalid checkpoint_id */
TEST(checkpoint_clear_auto_snapshot_rejects_invalid_checkpoint_id)
{
    cJSON *response, *params, *code_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", -1);  /* Invalid */

    response = mcp_tool_checkpoint_clear_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_NOT_NULL(code_item);
    ASSERT_TRUE(code_item->valueint == -32602);

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot returns cleared=false for nonexistent */
TEST(checkpoint_clear_auto_snapshot_nonexistent_returns_false)
{
    cJSON *response, *params;
    cJSON *cleared_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 999);  /* No config exists */

    response = mcp_tool_checkpoint_clear_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cleared_item = cJSON_GetObjectItem(response, "cleared");
    ASSERT_NOT_NULL(cleared_item);
    ASSERT_TRUE(cJSON_IsFalse(cleared_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot clears existing config */
TEST(checkpoint_clear_auto_snapshot_clears_existing_config)
{
    cJSON *response, *params;
    cJSON *cleared_item;

    test_auto_snapshot_configs_reset();

    /* First create a config */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 5);
    cJSON_AddStringToObject(params, "snapshot_prefix", "to_clear");

    response = mcp_tool_checkpoint_set_auto_snapshot(params);
    ASSERT_NOT_NULL(response);
    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Now clear it */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 5);

    response = mcp_tool_checkpoint_clear_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cleared_item = cJSON_GetObjectItem(response, "cleared");
    ASSERT_NOT_NULL(cleared_item);
    ASSERT_TRUE(cJSON_IsTrue(cleared_item));

    cJSON_Delete(params);
    cJSON_Delete(response);

    /* Verify it's actually cleared by trying to clear again */
    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 5);

    response = mcp_tool_checkpoint_clear_auto_snapshot(params);
    ASSERT_NOT_NULL(response);

    cleared_item = cJSON_GetObjectItem(response, "cleared");
    ASSERT_NOT_NULL(cleared_item);
    ASSERT_TRUE(cJSON_IsFalse(cleared_item));  /* Now returns false */

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.set_auto_snapshot dispatch works */
TEST(checkpoint_set_auto_snapshot_dispatch_works)
{
    cJSON *response, *params;
    cJSON *enabled_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);
    cJSON_AddStringToObject(params, "snapshot_prefix", "dispatch_test");

    response = mcp_tools_dispatch("vice.checkpoint.set_auto_snapshot", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    enabled_item = cJSON_GetObjectItem(response, "enabled");
    ASSERT_NOT_NULL(enabled_item);
    ASSERT_TRUE(cJSON_IsTrue(enabled_item));

    cJSON_Delete(params);
    cJSON_Delete(response);
}

/* Test: checkpoint.clear_auto_snapshot dispatch works */
TEST(checkpoint_clear_auto_snapshot_dispatch_works)
{
    cJSON *response, *params;
    cJSON *cleared_item;

    test_auto_snapshot_configs_reset();

    params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "checkpoint_id", 1);

    response = mcp_tools_dispatch("vice.checkpoint.clear_auto_snapshot", params);
    ASSERT_NOT_NULL(response);

    /* Should not be an error */
    cJSON *code_item = cJSON_GetObjectItem(response, "code");
    ASSERT_TRUE(code_item == NULL);

    cleared_item = cJSON_GetObjectItem(response, "cleared");
    ASSERT_NOT_NULL(cleared_item);
    /* Note: cleared may be true or false depending on previous state */

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
    RUN_TEST(watch_add_with_condition);
    RUN_TEST(watch_add_with_invalid_condition);
    RUN_TEST(watch_add_without_condition);
    RUN_TEST(watch_add_with_pc_condition);
    RUN_TEST(disassemble_with_hex_address);

    /* Snapshot tests */
    RUN_TEST(snapshot_save_with_name_succeeds);
    RUN_TEST(snapshot_save_without_name_returns_error);
    RUN_TEST(snapshot_save_with_options_succeeds);
    RUN_TEST(snapshot_save_failure_returns_error);
    RUN_TEST(snapshot_load_with_name_succeeds);
    RUN_TEST(snapshot_load_without_name_returns_error);
    RUN_TEST(snapshot_load_failure_returns_error);
    RUN_TEST(snapshot_list_returns_array);
    RUN_TEST(snapshot_list_returns_directory);

    /* Memory search tests */
    RUN_TEST(memory_search_requires_start);
    RUN_TEST(memory_search_requires_end);
    RUN_TEST(memory_search_requires_pattern);
    RUN_TEST(memory_search_finds_single_byte);
    RUN_TEST(memory_search_finds_jmp_instruction);
    RUN_TEST(memory_search_with_wildcard_mask);
    RUN_TEST(memory_search_respects_max_results);
    RUN_TEST(memory_search_truncated_false_when_not_truncated);
    RUN_TEST(memory_search_hex_string_addresses);
    RUN_TEST(memory_search_invalid_range_returns_error);
    RUN_TEST(memory_search_empty_pattern_returns_error);
    RUN_TEST(memory_search_mask_length_mismatch_returns_error);
    RUN_TEST(memory_search_dispatch_works);

    /* Cycles stopwatch tests */
    RUN_TEST(cycles_stopwatch_requires_action);
    RUN_TEST(cycles_stopwatch_null_params_returns_error);
    RUN_TEST(cycles_stopwatch_invalid_action_returns_error);
    RUN_TEST(cycles_stopwatch_reset_works);
    RUN_TEST(cycles_stopwatch_read_returns_cycles);
    RUN_TEST(cycles_stopwatch_reset_and_read_works);
    RUN_TEST(cycles_stopwatch_dispatch_works);

    /* Memory fill tests */
    RUN_TEST(memory_fill_requires_start);
    RUN_TEST(memory_fill_requires_end);
    RUN_TEST(memory_fill_requires_pattern);
    RUN_TEST(memory_fill_null_params_returns_error);
    RUN_TEST(memory_fill_single_byte_pattern);
    RUN_TEST(memory_fill_screen_spaces);
    RUN_TEST(memory_fill_alternating_pattern);
    RUN_TEST(memory_fill_partial_pattern);
    RUN_TEST(memory_fill_hex_string_addresses);
    RUN_TEST(memory_fill_empty_pattern_returns_error);
    RUN_TEST(memory_fill_invalid_range_returns_error);
    RUN_TEST(memory_fill_invalid_byte_value_returns_error);
    RUN_TEST(memory_fill_dispatch_works);

    /* Memory compare tests (ranges mode) */
    RUN_TEST(memory_compare_ranges_requires_mode);
    RUN_TEST(memory_compare_ranges_requires_range1_start);
    RUN_TEST(memory_compare_ranges_requires_range1_end);
    RUN_TEST(memory_compare_ranges_requires_range2_start);
    RUN_TEST(memory_compare_ranges_null_params_returns_error);
    RUN_TEST(memory_compare_invalid_mode_returns_error);
    RUN_TEST(memory_compare_ranges_invalid_range1_returns_error);
    RUN_TEST(memory_compare_ranges_identical_returns_no_differences);
    RUN_TEST(memory_compare_ranges_finds_differences);
    RUN_TEST(memory_compare_ranges_respects_max_differences);
    RUN_TEST(memory_compare_ranges_hex_string_addresses);
    RUN_TEST(memory_compare_ranges_dispatch_works);
    RUN_TEST(memory_compare_ranges_single_byte);

    /* Memory compare tests (snapshot mode) */
    RUN_TEST(memory_compare_snapshot_requires_snapshot_name);
    RUN_TEST(memory_compare_snapshot_requires_start);
    RUN_TEST(memory_compare_snapshot_requires_end);
    RUN_TEST(memory_compare_snapshot_validates_range_order);
    RUN_TEST(memory_compare_snapshot_nonexistent_returns_error);
    RUN_TEST(memory_compare_snapshot_finds_differences);
    RUN_TEST(memory_compare_snapshot_identical_returns_no_differences);
    RUN_TEST(memory_compare_snapshot_respects_max_differences);
    RUN_TEST(memory_compare_snapshot_hex_string_addresses);

    /* Checkpoint group tests */
    RUN_TEST(checkpoint_group_create_requires_name);
    RUN_TEST(checkpoint_group_create_null_params_returns_error);
    RUN_TEST(checkpoint_group_create_with_name_succeeds);
    RUN_TEST(checkpoint_group_create_with_checkpoint_ids);
    RUN_TEST(checkpoint_group_create_duplicate_name_returns_error);
    RUN_TEST(checkpoint_group_add_requires_group);
    RUN_TEST(checkpoint_group_add_requires_checkpoint_ids);
    RUN_TEST(checkpoint_group_add_succeeds);
    RUN_TEST(checkpoint_group_add_nonexistent_group_returns_error);
    RUN_TEST(checkpoint_group_toggle_requires_group);
    RUN_TEST(checkpoint_group_toggle_requires_enabled);
    RUN_TEST(checkpoint_group_toggle_succeeds);
    RUN_TEST(checkpoint_group_toggle_nonexistent_group_returns_error);
    RUN_TEST(checkpoint_group_list_returns_empty_array);
    RUN_TEST(checkpoint_group_list_returns_groups_with_details);
    RUN_TEST(checkpoint_group_list_dispatch_works);
    RUN_TEST(checkpoint_group_create_dispatch_works);

    /* Auto-snapshot configuration tests */
    RUN_TEST(checkpoint_set_auto_snapshot_requires_checkpoint_id);
    RUN_TEST(checkpoint_set_auto_snapshot_requires_snapshot_prefix);
    RUN_TEST(checkpoint_set_auto_snapshot_null_params_returns_error);
    RUN_TEST(checkpoint_set_auto_snapshot_rejects_empty_prefix);
    RUN_TEST(checkpoint_set_auto_snapshot_rejects_invalid_prefix);
    RUN_TEST(checkpoint_set_auto_snapshot_rejects_invalid_checkpoint_id);
    RUN_TEST(checkpoint_set_auto_snapshot_with_valid_params_succeeds);
    RUN_TEST(checkpoint_set_auto_snapshot_with_all_options);
    RUN_TEST(checkpoint_set_auto_snapshot_clamps_max_snapshots);
    RUN_TEST(checkpoint_set_auto_snapshot_updates_existing_config);
    RUN_TEST(checkpoint_clear_auto_snapshot_requires_checkpoint_id);
    RUN_TEST(checkpoint_clear_auto_snapshot_null_params_returns_error);
    RUN_TEST(checkpoint_clear_auto_snapshot_rejects_invalid_checkpoint_id);
    RUN_TEST(checkpoint_clear_auto_snapshot_nonexistent_returns_false);
    RUN_TEST(checkpoint_clear_auto_snapshot_clears_existing_config);
    RUN_TEST(checkpoint_set_auto_snapshot_dispatch_works);
    RUN_TEST(checkpoint_clear_auto_snapshot_dispatch_works);

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
