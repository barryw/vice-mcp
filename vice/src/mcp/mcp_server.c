/*
 * mcp_server.c - MCP server implementation for VICE
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

#include "mcp_server.h"
#include "mcp_transport.h"
#include "mcp_tools.h"
#include "resources.h"
#include "cmdline.h"
#include "log.h"
#include "lib.h"

/* MCP server state */
static int mcp_enabled = 0;
static int mcp_port = MCP_DEFAULT_PORT;
static char *mcp_host = NULL;
static int mcp_running = 0;

static log_t mcp_log = LOG_DEFAULT;

/* ------------------------------------------------------------------------- */
/* Resources */

static int set_mcp_enabled(int val, void *param)
{
    (void)param;

    int old_enabled = mcp_enabled;
    mcp_enabled = val ? 1 : 0;

    /* Only start/stop if server is already initialized and running.
     * During startup, the server will be started in mcp_server_init()
     * if mcp_enabled is true.
     * mcp_log being non-null indicates we've been through mcp_server_init()
     */
    if (old_enabled != mcp_enabled && mcp_log != LOG_DEFAULT) {
        if (mcp_enabled && !mcp_running) {
            int result = mcp_server_start(mcp_host ? mcp_host : MCP_DEFAULT_HOST, mcp_port);
            if (result < 0) {
                mcp_enabled = 0;  /* Reset on failure to maintain consistent state */
                return result;
            }
        } else if (!mcp_enabled && mcp_running) {
            mcp_server_stop();
        }
    }

    return 0;
}

static int set_mcp_port(int val, void *param)
{
    (void)param;

    if (val < 1024 || val > 65535) {
        return -1;
    }

    mcp_port = val;

    /* Restart server if running */
    if (mcp_running) {
        mcp_server_stop();
        return mcp_server_start(mcp_host ? mcp_host : MCP_DEFAULT_HOST, mcp_port);
    }

    return 0;
}

static int set_mcp_host(const char *val, void *param)
{
    (void)param;

    if (mcp_host) {
        lib_free(mcp_host);
    }

    mcp_host = lib_strdup(val);

    /* Restart server if running */
    if (mcp_running) {
        mcp_server_stop();
        return mcp_server_start(mcp_host, mcp_port);
    }

    return 0;
}

static const resource_string_t resources_string[] = {
    { "MCPServerHost", MCP_DEFAULT_HOST, RES_EVENT_NO, NULL,
      &mcp_host, set_mcp_host, NULL },
    RESOURCE_STRING_LIST_END
};

static const resource_int_t resources_int[] = {
    { "MCPServerEnabled", 0, RES_EVENT_NO, NULL,
      &mcp_enabled, set_mcp_enabled, NULL },
    { "MCPServerPort", MCP_DEFAULT_PORT, RES_EVENT_NO, NULL,
      &mcp_port, set_mcp_port, NULL },
    RESOURCE_INT_LIST_END
};

int mcp_server_register_resources(void)
{
    if (resources_register_string(resources_string) < 0) {
        return -1;
    }

    return resources_register_int(resources_int);
}

/* ------------------------------------------------------------------------- */
/* Command line options */

static const cmdline_option_t cmdline_options[] =
{
    { "-mcpserver", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "MCPServerEnabled", (void *)1,
      NULL, "Enable MCP server" },
    { "+mcpserver", SET_RESOURCE, CMDLINE_ATTRIB_NONE,
      NULL, NULL, "MCPServerEnabled", (void *)0,
      NULL, "Disable MCP server" },
    { "-mcpserverport", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "MCPServerPort", NULL,
      "<port>", "Set MCP server port (default 6510)" },
    { "-mcpserverhost", SET_RESOURCE, CMDLINE_ATTRIB_NEED_ARGS,
      NULL, NULL, "MCPServerHost", NULL,
      "<host>", "Set MCP server host (default 127.0.0.1)" },
    CMDLINE_LIST_END
};

int mcp_server_register_cmdline_options(void)
{
    return cmdline_register_options(cmdline_options);
}

/* ------------------------------------------------------------------------- */
/* MCP Server API */

int mcp_server_init(void)
{
    mcp_log = log_open("MCP");

    log_message(mcp_log, "MCP server initializing...");

    /* Initialize transport layer */
    if (mcp_transport_init() < 0) {
        log_error(mcp_log, "Failed to initialize MCP transport");
        return -1;
    }

    /* Initialize tools */
    if (mcp_tools_init() < 0) {
        log_error(mcp_log, "Failed to initialize MCP tools");
        return -1;
    }

    log_message(mcp_log, "MCP server initialized");

    /* If server was enabled via config/cmdline, start it now */
    if (mcp_enabled && !mcp_running) {
        if (mcp_server_start(mcp_host ? mcp_host : MCP_DEFAULT_HOST, mcp_port) < 0) {
            log_error(mcp_log, "Failed to start MCP server");
            /* Non-fatal - server is initialized, just not started */
        }
    }

    return 0;
}

void mcp_server_shutdown(void)
{
    log_message(mcp_log, "MCP server shutting down...");

    if (mcp_running) {
        mcp_server_stop();
    }

    mcp_tools_shutdown();
    mcp_transport_shutdown();

    if (mcp_host) {
        lib_free(mcp_host);
        mcp_host = NULL;
    }

    log_message(mcp_log, "MCP server shut down");
}

int mcp_server_start(const char *host, int port)
{
    if (mcp_running) {
        log_warning(mcp_log, "MCP server already running");
        return 0;
    }

    log_message(mcp_log, "Starting MCP server on %s:%d", host, port);

    if (mcp_transport_start(host, port) < 0) {
        log_error(mcp_log, "Failed to start MCP transport");
        return -1;
    }

    mcp_running = 1;

    log_message(mcp_log, "MCP server started on %s:%d", host, port);

    return 0;
}

void mcp_server_stop(void)
{
    if (!mcp_running) {
        return;
    }

    log_message(mcp_log, "Stopping MCP server");

    mcp_transport_stop();

    mcp_running = 0;

    log_message(mcp_log, "MCP server stopped");
}

int mcp_server_is_running(void)
{
    return mcp_running;
}

/* Configuration API */

int mcp_server_set_enabled(int enabled)
{
    return resources_set_int("MCPServerEnabled", enabled);
}

int mcp_server_get_enabled(void)
{
    return mcp_enabled;
}

int mcp_server_set_port(int port)
{
    return resources_set_int("MCPServerPort", port);
}

int mcp_server_get_port(void)
{
    return mcp_port;
}

int mcp_server_set_host(const char *host)
{
    return resources_set_string("MCPServerHost", host);
}

const char *mcp_server_get_host(void)
{
    return mcp_host ? mcp_host : MCP_DEFAULT_HOST;
}
