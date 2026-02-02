AM_CFLAGS = @VICE_CFLAGS@

noinst_LIBRARIES = libmcp.a

libmcp_a_SOURCES = \
    cJSON.c \
    cJSON.h \
    mcp_server.c \
    mcp_server.h \
    mcp_transport.c \
    mcp_transport.h \
    mcp_tools.c \
    mcp_tools.h

# Add libmicrohttpd flags if available
if HAVE_MCP_SERVER
libmcp_a_CFLAGS = $(AM_CFLAGS) $(LIBMICROHTTPD_CFLAGS)
endif
