CC ?= cc
TOP_SRCDIR ?= ../../..
TOP_BUILDDIR ?= ../../../build-headless
LIBMCP ?= $(TOP_BUILDDIR)/src/mcp/libmcp.a

LIBMICROHTTPD_CFLAGS ?= $(shell pkg-config --cflags libmicrohttpd 2>/dev/null)
LIBMICROHTTPD_LIBS ?= $(shell pkg-config --libs libmicrohttpd 2>/dev/null)

CPPFLAGS += -DHAVE_CONFIG_H \
	-I$(TOP_BUILDDIR)/src \
	-I.. \
	-I../.. \
	-I$(TOP_SRCDIR)/src/lib/cjson \
	$(LIBMICROHTTPD_CFLAGS)
CFLAGS ?= -Wall -Wextra -Werror=implicit-function-declaration
LDLIBS += $(LIBMICROHTTPD_LIBS) -pthread

OBJS = test_mcp_tools.o vice_stubs.o

.PHONY: all test clean

all: test_mcp_tools

$(LIBMCP):
	@if [ -f "$(TOP_BUILDDIR)/src/mcp/Makefile" ]; then \
		$(MAKE) -C "$(TOP_BUILDDIR)/src/mcp"; \
	else \
		echo "Missing $(LIBMCP). Run configure with --enable-mcp-server first."; \
		exit 1; \
	fi

test_mcp_tools: $(OBJS) $(LIBMCP)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBMCP) $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: test_mcp_tools
	./test_mcp_tools

clean:
	rm -f $(OBJS) test_mcp_tools
