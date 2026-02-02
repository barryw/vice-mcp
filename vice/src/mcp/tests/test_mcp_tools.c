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

int main(void)
{
    printf("=== MCP Tools Test Suite ===\n\n");

    /* Note: We don't call mcp_tools_init() because it requires
     * VICE's logging system to be initialized, which requires
     * the full VICE runtime environment. These are unit tests
     * of the individual tool functions. */

    RUN_TEST(ping_tool_returns_valid_response);
    RUN_TEST(invalid_tool_name_returns_error);
    RUN_TEST(null_tool_name_returns_error);
    RUN_TEST(empty_tool_name_returns_error);
    RUN_TEST(tool_name_too_long_returns_error);
    RUN_TEST(valid_tool_dispatch_works);

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
