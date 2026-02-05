AM_CPPFLAGS = \
	@VICE_CPPFLAGS@ \
	@ARCH_INCLUDES@ \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/lib/cjson

AM_CFLAGS = @VICE_CFLAGS@

if HAVE_MCP_SERVER
noinst_LIBRARIES = libmcp.a

libmcp_a_SOURCES = \
    mcp_server.c \
    mcp_server.h \
    mcp_transport.c \
    mcp_transport.h \
    mcp_tools.c \
    mcp_tools.h \
    mcp_tools_internal.h \
    mcp_tools_helpers.c \
    mcp_tools_display.c \
    mcp_tools_disk.c \
    mcp_tools_snapshot.c \
    mcp_tools_input.c \
    mcp_tools_execution.c \
    mcp_tools_memory.c \
    mcp_tools_debug.c \
    mcp_tools_chipstate.c \
    mcp_tools_checkpoint.c \
    mcp_tools_trace.c \
    mcp_tools_protocol.c

libmcp_a_CFLAGS = $(AM_CFLAGS) $(LIBMICROHTTPD_CFLAGS)
endif
