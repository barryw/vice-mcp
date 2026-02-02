# Makefile for MCP test suite
#
# Usage:
#   make BUILD_DIR=<path>  - Build tests (requires MCP library built first)
#   make test BUILD_DIR=<path> - Build and run all tests
#   make clean             - Remove build artifacts
#
# Example:
#   cd src/mcp/tests
#   make BUILD_DIR=../../../../build test
#
# Prerequisites:
#   Build VICE with MCP support first:
#     cd <build-dir>
#     make -C src/mcp

CC = cc
# BUILD_DIR must point to your VICE build directory (relative to this Makefile)
# Override with: make BUILD_DIR=path/to/build
BUILD_DIR ?= ../../../build-test-with-mcp
CFLAGS = -Wall -Wextra -I..
LDFLAGS =

# Source files
TEST_SRCS = test_mcp_tools.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_BINS = $(TEST_SRCS:.c=)

# Link against pre-compiled MCP library from build directory
MCP_LIB = $(BUILD_DIR)/src/mcp/libmcp.a

# Stub implementations for VICE dependencies
STUB_SRCS = vice_stubs.c
STUB_OBJS = $(STUB_SRCS:.c=.o)

.PHONY: all test clean

all: check-mcp-lib $(TEST_BINS)

check-mcp-lib:
	@if [ ! -f "$(MCP_LIB)" ]; then \
		echo "Error: MCP library not found at $(MCP_LIB)"; \
		echo "Build it first: cd $(BUILD_DIR) && make -C src/mcp"; \
		exit 1; \
	fi

test: all
	@echo "Running MCP test suite..."
	@for test in $(TEST_BINS); do \
		./$$test || exit 1; \
	done

test_mcp_tools: test_mcp_tools.o $(STUB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(MCP_LIB)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TEST_BINS) $(TEST_OBJS) $(STUB_OBJS)
