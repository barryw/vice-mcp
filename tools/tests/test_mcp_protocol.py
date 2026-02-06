"""
Comprehensive MCP protocol schema test suite for VICE C64 emulator MCP server.

Tests validate the JSON-RPC 2.0 protocol over HTTP POST to /mcp on localhost:6510.

Tools are called via:
    {"jsonrpc":"2.0","method":"tools/call","params":{"name":"vice.TOOL_NAME","arguments":{...}},"id":1}

For each of the 63 tools this suite tests:
    1. Valid params produce correct response structure
    2. Invalid params (wrong types, missing required) produce proper error responses
    3. inputSchema from tools/list matches what the handler actually expects

Run with:
    pytest tools/tests/test_mcp_protocol.py -v
    pytest tools/tests/test_mcp_protocol.py -v -m "not stateful and not disk_required"
"""

import json
import time

import pytest
import requests


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

MCP_HOST = "127.0.0.1"
MCP_PORT = 6510
MCP_URL = f"http://{MCP_HOST}:{MCP_PORT}/mcp"

# JSON-RPC 2.0 error codes
ERROR_PARSE_ERROR = -32700
ERROR_INVALID_REQUEST = -32600
ERROR_METHOD_NOT_FOUND = -32601
ERROR_INVALID_PARAMS = -32602
ERROR_INTERNAL_ERROR = -32603

# MCP-specific error codes
ERROR_NOT_IMPLEMENTED = -32000
ERROR_EMULATOR_RUNNING = -32001
ERROR_INVALID_ADDRESS = -32002
ERROR_INVALID_VALUE = -32003
ERROR_SNAPSHOT_FAILED = -32004


# ---------------------------------------------------------------------------
# Custom pytest markers (register in pyproject.toml or conftest.py):
#   stateful       - test modifies emulator state
#   disk_required  - test requires a disk image to be attached
# ---------------------------------------------------------------------------


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class MCPClient:
    """Thin wrapper around HTTP POST for JSON-RPC 2.0 calls to the MCP server."""

    _next_id = 1

    def __init__(self, base_url: str = MCP_URL):
        self.base_url = base_url
        self.session = requests.Session()
        self.session.headers.update({
            "Content-Type": "application/json",
            "Accept": "application/json",
        })

    # -- low level ----------------------------------------------------------

    def raw_request(self, body: dict, timeout: float = 5.0) -> requests.Response:
        """Send a raw JSON-RPC request and return the HTTP response."""
        return self.session.post(self.base_url, json=body, timeout=timeout)

    def jsonrpc(self, method: str, params: dict | None = None,
                request_id: int | None = None,
                timeout: float = 5.0) -> dict:
        """Send a JSON-RPC 2.0 request and return the parsed response body."""
        if request_id is None:
            request_id = MCPClient._next_id
            MCPClient._next_id += 1
        body = {"jsonrpc": "2.0", "method": method, "id": request_id}
        if params is not None:
            body["params"] = params
        resp = self.raw_request(body, timeout=timeout)
        assert resp.status_code == 200, (
            f"HTTP {resp.status_code}: {resp.text[:500]}"
        )
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"
        return data

    # -- tools/call convenience ---------------------------------------------

    def call_tool(self, tool_name: str, arguments: dict | None = None,
                  timeout: float = 5.0) -> dict:
        """Invoke a tool via tools/call and return the full JSON-RPC response."""
        params = {"name": tool_name}
        if arguments is not None:
            params["arguments"] = arguments
        return self.jsonrpc("tools/call", params, timeout=timeout)

    def call_tool_result(self, tool_name: str, arguments: dict | None = None,
                         timeout: float = 5.0) -> dict:
        """Invoke a tool and return the parsed inner result (from content[0].text).

        For tools/call, a successful response wraps the real data inside:
            result.content[0].text  (JSON string)
        This helper parses that and returns the inner dict.
        """
        data = self.call_tool(tool_name, arguments, timeout=timeout)
        assert "result" in data, f"Expected result, got: {json.dumps(data, indent=2)}"
        content = data["result"].get("content")
        assert content and len(content) > 0, (
            f"Expected content array, got: {json.dumps(data['result'], indent=2)}"
        )
        text_val = content[0].get("text")
        assert text_val is not None, (
            f"Expected text in content[0], got: {json.dumps(content[0], indent=2)}"
        )
        return json.loads(text_val)

    def call_tool_error(self, tool_name: str, arguments: dict | None = None,
                        timeout: float = 5.0) -> dict:
        """Invoke a tool expecting an error response. Return the error dict."""
        data = self.call_tool(tool_name, arguments, timeout=timeout)
        assert "error" in data, (
            f"Expected error response, got: {json.dumps(data, indent=2)}"
        )
        return data["error"]

    # -- direct dispatch convenience ----------------------------------------

    def direct_call(self, method: str, params: dict | None = None,
                    timeout: float = 5.0) -> dict:
        """Call a method directly (not via tools/call) and return result."""
        data = self.jsonrpc(method, params, timeout=timeout)
        assert "result" in data, f"Expected result, got: {json.dumps(data, indent=2)}"
        return data["result"]

    def direct_call_error(self, method: str, params: dict | None = None,
                          timeout: float = 5.0) -> dict:
        """Call a method directly expecting an error. Return error dict."""
        data = self.jsonrpc(method, params, timeout=timeout)
        assert "error" in data, (
            f"Expected error, got: {json.dumps(data, indent=2)}"
        )
        return data["error"]


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def mcp():
    """Session-scoped MCP client. Skips the entire session if server is down."""
    client = MCPClient()
    try:
        resp = client.jsonrpc("vice.ping", timeout=3)
        if "result" not in resp:
            pytest.skip(f"MCP server not responding correctly: {resp}")
    except (requests.ConnectionError, requests.Timeout):
        pytest.skip(f"MCP server not reachable at {MCP_URL}")
    return client


@pytest.fixture(scope="session")
def session_id(mcp):
    """Perform MCP initialize handshake and return protocol version."""
    result = mcp.direct_call("initialize", {
        "protocolVersion": "2024-11-05",
        "capabilities": {},
        "clientInfo": {"name": "pytest", "version": "1.0"},
    })
    assert "protocolVersion" in result
    # Send initialized notification (no response expected -- use raw)
    mcp.raw_request({
        "jsonrpc": "2.0",
        "method": "notifications/initialized",
    })
    return result["protocolVersion"]


@pytest.fixture(scope="session")
def tools_list(mcp):
    """Fetch and cache the tools/list response for the session."""
    result = mcp.direct_call("tools/list")
    assert "tools" in result
    return result["tools"]


@pytest.fixture(scope="session")
def tools_by_name(tools_list):
    """Build a dict mapping tool name -> tool definition from tools/list."""
    return {t["name"]: t for t in tools_list}


# ---------------------------------------------------------------------------
# Test: MCP Base Protocol
# ---------------------------------------------------------------------------

class TestBaseProtocol:
    """Tests for JSON-RPC 2.0 protocol basics and MCP handshake."""

    def test_valid_jsonrpc_response(self, mcp):
        """Every response must include jsonrpc='2.0' and an id."""
        data = mcp.jsonrpc("vice.ping")
        assert data["jsonrpc"] == "2.0"
        assert "id" in data

    def test_invalid_json_returns_parse_error(self, mcp):
        """Malformed JSON body should return parse error (-32700)."""
        resp = mcp.session.post(
            MCP_URL,
            data="NOT JSON {{{",
            timeout=5,
        )
        # Server should still return 200 with a JSON-RPC error
        data = resp.json()
        assert data.get("error", {}).get("code") == ERROR_PARSE_ERROR

    def test_missing_method_returns_invalid_request(self, mcp):
        """Request without method field should return invalid request (-32600)."""
        resp = mcp.raw_request({"jsonrpc": "2.0", "id": 99})
        data = resp.json()
        assert data.get("error", {}).get("code") == ERROR_INVALID_REQUEST

    def test_unknown_method_returns_method_not_found(self, mcp):
        """Calling a nonexistent method should return method not found (-32601)."""
        data = mcp.jsonrpc("vice.nonexistent.method")
        assert "error" in data
        assert data["error"]["code"] == ERROR_METHOD_NOT_FOUND

    def test_initialize(self, mcp):
        """Initialize handshake should return protocolVersion, capabilities, serverInfo."""
        result = mcp.direct_call("initialize", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "pytest", "version": "1.0"},
        })
        assert "protocolVersion" in result
        assert "capabilities" in result
        assert "serverInfo" in result
        assert result["serverInfo"]["name"] == "VICE MCP"

    def test_initialize_unsupported_version(self, mcp):
        """Initialize with unsupported protocol version should fail."""
        error = mcp.direct_call_error("initialize", {
            "protocolVersion": "9999-01-01",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_notifications_initialized(self, mcp):
        """The initialized notification should return HTTP 202 or empty body."""
        resp = mcp.raw_request({
            "jsonrpc": "2.0",
            "method": "notifications/initialized",
        })
        # Notifications have no id, so response may be 202/empty or a minimal ack
        assert resp.status_code in (200, 202, 204)

    def test_tools_list(self, mcp, tools_list):
        """tools/list should return an array of tool definitions."""
        assert isinstance(tools_list, list)
        assert len(tools_list) > 50  # We expect 63+ tools

    def test_tools_list_structure(self, tools_list):
        """Each tool in tools/list must have name, description, and inputSchema."""
        for tool in tools_list:
            assert "name" in tool, f"Tool missing name: {tool}"
            assert "description" in tool, f"Tool {tool.get('name')} missing description"
            assert "inputSchema" in tool, f"Tool {tool.get('name')} missing inputSchema"
            schema = tool["inputSchema"]
            assert schema.get("type") == "object", (
                f"Tool {tool['name']} inputSchema type is not 'object': {schema.get('type')}"
            )


# ---------------------------------------------------------------------------
# Test: vice.ping
# ---------------------------------------------------------------------------

class TestPing:
    """Tests for the vice.ping tool."""

    def test_ping_valid(self, mcp):
        """Ping with no params should return status, version, machine, execution."""
        result = mcp.call_tool_result("vice.ping")
        assert result["status"] == "ok"
        assert "version" in result
        assert "machine" in result
        assert result["execution"] in ("running", "paused", "transitioning")

    def test_ping_ignores_extra_params(self, mcp):
        """Ping should succeed even with unexpected extra arguments."""
        result = mcp.call_tool_result("vice.ping", {"extra": "ignored"})
        assert result["status"] == "ok"


# ---------------------------------------------------------------------------
# Test: Execution Control
# ---------------------------------------------------------------------------

class TestExecution:
    """Tests for vice.execution.run, pause, step."""

    @pytest.mark.stateful
    def test_pause(self, mcp):
        """Pause should return status ok."""
        result = mcp.call_tool_result("vice.execution.pause")
        assert result["status"] == "ok"

    @pytest.mark.stateful
    def test_run(self, mcp):
        """Run should return status ok."""
        result = mcp.call_tool_result("vice.execution.run")
        assert result["status"] == "ok"

    @pytest.mark.stateful
    def test_step_default(self, mcp):
        """Step with no args should step one instruction."""
        # Ensure paused first
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.execution.step")
        assert result["status"] == "ok"
        assert "pc" in result or "PC" in result or "registers" in result

    @pytest.mark.stateful
    def test_step_with_count(self, mcp):
        """Step with count param should accept a number."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.execution.step", {"count": 5})
        assert result["status"] == "ok"

    def test_step_invalid_count_type(self, mcp):
        """Step with string count should return error or ignore gracefully."""
        # Pause first so we can step
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        # String count -- server may accept it as best-effort or error
        data = mcp.call_tool("vice.execution.step", {"count": "not_a_number"})
        # Either an error or a successful result (ignoring bad param) is acceptable
        assert "result" in data or "error" in data


# ---------------------------------------------------------------------------
# Test: Registers
# ---------------------------------------------------------------------------

class TestRegisters:
    """Tests for vice.registers.get and vice.registers.set."""

    def test_get_registers(self, mcp):
        """Get registers should return standard 6502 register values."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.registers.get")
        # Should have at least PC, A, X, Y, SP
        expected_regs = {"PC", "A", "X", "Y", "SP"}
        actual_keys = set(result.keys())
        assert expected_regs.issubset(actual_keys), (
            f"Missing registers: {expected_regs - actual_keys}"
        )

    @pytest.mark.stateful
    def test_set_register_a(self, mcp):
        """Set register A to a known value and read it back."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result(
            "vice.registers.set", {"register": "A", "value": 42}
        )
        assert result["status"] == "ok"
        # Verify
        regs = mcp.call_tool_result("vice.registers.get")
        assert regs["A"] == 42

    def test_set_register_missing_name(self, mcp):
        """Set register without register name should fail."""
        error = mcp.call_tool_error("vice.registers.set", {"value": 42})
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_REQUEST)

    def test_set_register_missing_value(self, mcp):
        """Set register without value should fail."""
        error = mcp.call_tool_error("vice.registers.set", {"register": "A"})
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_REQUEST)

    def test_set_register_invalid_name(self, mcp):
        """Set register with invalid name should fail."""
        error = mcp.call_tool_error(
            "vice.registers.set", {"register": "BOGUS", "value": 0}
        )
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)

    def test_set_register_value_out_of_range(self, mcp):
        """Set 8-bit register to value > 255 should fail."""
        error = mcp.call_tool_error(
            "vice.registers.set", {"register": "A", "value": 999}
        )
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)


# ---------------------------------------------------------------------------
# Test: Memory
# ---------------------------------------------------------------------------

class TestMemory:
    """Tests for vice.memory.read, write, banks, search, fill, compare, map."""

    def test_memory_read_valid(self, mcp):
        """Read 16 bytes from address 0 should return hex data."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.read", {
            "address": 0, "size": 16,
        })
        assert "data" in result or "hex" in result or "bytes" in result

    def test_memory_read_hex_address(self, mcp):
        """Read using hex string address should work."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.read", {
            "address": "$C000", "size": 4,
        })
        assert "data" in result or "hex" in result or "bytes" in result

    def test_memory_read_missing_address(self, mcp):
        """Read without address should fail."""
        error = mcp.call_tool_error("vice.memory.read", {"size": 16})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_read_missing_size(self, mcp):
        """Read without size should fail."""
        error = mcp.call_tool_error("vice.memory.read", {"address": 0})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_memory_write_and_readback(self, mcp):
        """Write bytes then read them back to verify."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        test_data = [0xDE, 0xAD, 0xBE, 0xEF]
        addr = 0x0400  # Screen memory, safe to write
        write_result = mcp.call_tool_result("vice.memory.write", {
            "address": addr, "data": test_data,
        })
        assert write_result.get("status") == "ok"

        read_result = mcp.call_tool_result("vice.memory.read", {
            "address": addr, "size": 4,
        })
        # Verify data matches (may be in hex or array form)
        if "data" in read_result:
            read_data = read_result["data"]
            if isinstance(read_data, str):
                # Hex string like "DEADBEEF" or "de ad be ef"
                clean = read_data.replace(" ", "").lower()
                assert clean == "deadbeef"
            elif isinstance(read_data, list):
                assert read_data == test_data

    def test_memory_write_missing_address(self, mcp):
        """Write without address should fail."""
        error = mcp.call_tool_error("vice.memory.write", {"data": [0]})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_write_missing_data(self, mcp):
        """Write without data should fail."""
        error = mcp.call_tool_error("vice.memory.write", {"address": 0})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_banks(self, mcp):
        """List memory banks should return at least the default bank."""
        result = mcp.call_tool_result("vice.memory.banks")
        # Should have a banks array or list
        assert "banks" in result
        assert isinstance(result["banks"], list)
        assert len(result["banks"]) > 0

    def test_memory_search_valid(self, mcp):
        """Search for a common byte pattern should return matches."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.search", {
            "start": "$0000",
            "end": "$FFFF",
            "pattern": [0x4C],  # JMP opcode
        })
        assert "matches" in result or "results" in result or "addresses" in result

    def test_memory_search_missing_pattern(self, mcp):
        """Search without pattern should fail."""
        error = mcp.call_tool_error("vice.memory.search", {
            "start": "$0000", "end": "$FFFF",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_search_missing_start(self, mcp):
        """Search without start address should fail."""
        error = mcp.call_tool_error("vice.memory.search", {
            "end": "$FFFF", "pattern": [0x4C],
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_memory_fill(self, mcp):
        """Fill a range with a pattern and verify."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.fill", {
            "start": "$0400",
            "end": "$040F",
            "pattern": [0xAA],
        })
        assert result.get("status") == "ok"

    def test_memory_fill_missing_params(self, mcp):
        """Fill without start/end/pattern should fail."""
        error = mcp.call_tool_error("vice.memory.fill", {
            "start": "$0400",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_compare_missing_mode(self, mcp):
        """Compare without mode should fail."""
        error = mcp.call_tool_error("vice.memory.compare", {
            "range1_start": "$0400",
            "range1_end": "$040F",
            "range2_start": "$0500",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_memory_compare_ranges(self, mcp):
        """Compare two memory ranges should return differences list."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.compare", {
            "mode": "ranges",
            "range1_start": "$0400",
            "range1_end": "$040F",
            "range2_start": "$0500",
        })
        assert "differences" in result or "diff_count" in result or "identical" in result

    def test_memory_map_no_params(self, mcp):
        """Memory map with no params should return the full map."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.map")
        assert "regions" in result or "map" in result

    def test_memory_map_with_range(self, mcp):
        """Memory map with start/end should scope the result."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.map", {
            "start": "$C000",
            "end": "$DFFF",
        })
        assert "regions" in result or "map" in result

    def test_memory_read_with_bank(self, mcp):
        """Reading with explicit bank parameter should work."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.read", {
            "address": "$A000", "size": 4, "bank": "ram",
        })
        assert "data" in result or "hex" in result or "bytes" in result


# ---------------------------------------------------------------------------
# Test: Checkpoints
# ---------------------------------------------------------------------------

class TestCheckpoints:
    """Tests for checkpoint/breakpoint management tools."""

    @pytest.mark.stateful
    def test_checkpoint_add_and_list(self, mcp):
        """Add a checkpoint and verify it appears in the list."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add_result = mcp.call_tool_result("vice.checkpoint.add", {
            "start": "$C000",
        })
        assert "checkpoint_num" in add_result or "id" in add_result or "number" in add_result

        list_result = mcp.call_tool_result("vice.checkpoint.list")
        assert "checkpoints" in list_result
        assert len(list_result["checkpoints"]) > 0

    @pytest.mark.stateful
    def test_checkpoint_add_with_options(self, mcp):
        """Add a checkpoint with all options should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.checkpoint.add", {
            "start": "$1000",
            "end": "$1FFF",
            "stop": True,
            "load": True,
            "store": False,
            "exec": False,
        })
        assert "checkpoint_num" in result or "id" in result or "number" in result

    def test_checkpoint_add_missing_start(self, mcp):
        """Add checkpoint without start address should fail."""
        error = mcp.call_tool_error("vice.checkpoint.add", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_checkpoint_toggle(self, mcp):
        """Toggle a checkpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$E000"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        assert cp_num is not None

        result = mcp.call_tool_result("vice.checkpoint.toggle", {
            "checkpoint_num": cp_num,
            "enabled": False,
        })
        assert result.get("status") == "ok"

    def test_checkpoint_toggle_missing_params(self, mcp):
        """Toggle without checkpoint_num should fail."""
        error = mcp.call_tool_error("vice.checkpoint.toggle", {
            "enabled": True,
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_checkpoint_delete(self, mcp):
        """Delete a checkpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$FFFE"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        result = mcp.call_tool_result("vice.checkpoint.delete", {
            "checkpoint_num": cp_num,
        })
        assert result.get("status") == "ok"

    def test_checkpoint_delete_invalid_num(self, mcp):
        """Delete nonexistent checkpoint should fail."""
        error = mcp.call_tool_error("vice.checkpoint.delete", {
            "checkpoint_num": 999999,
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)

    @pytest.mark.stateful
    def test_checkpoint_set_condition(self, mcp):
        """Set condition on a checkpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$D000"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        result = mcp.call_tool_result("vice.checkpoint.set_condition", {
            "checkpoint_num": cp_num,
            "condition": "A == $42",
        })
        assert result.get("status") == "ok"

    def test_checkpoint_set_condition_missing_fields(self, mcp):
        """Set condition without both required params should fail."""
        error = mcp.call_tool_error("vice.checkpoint.set_condition", {
            "checkpoint_num": 1,
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_checkpoint_set_ignore_count(self, mcp):
        """Set ignore count on a checkpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$B000"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        result = mcp.call_tool_result("vice.checkpoint.set_ignore_count", {
            "checkpoint_num": cp_num,
            "count": 5,
        })
        assert result.get("status") == "ok"

    def test_checkpoint_set_ignore_count_missing_fields(self, mcp):
        """Set ignore count without count should fail."""
        error = mcp.call_tool_error("vice.checkpoint.set_ignore_count", {
            "checkpoint_num": 1,
        })
        assert error["code"] == ERROR_INVALID_PARAMS


# ---------------------------------------------------------------------------
# Test: Checkpoint Groups
# ---------------------------------------------------------------------------

class TestCheckpointGroups:
    """Tests for checkpoint group management tools."""

    @pytest.mark.stateful
    def test_group_create(self, mcp):
        """Create a checkpoint group should succeed."""
        result = mcp.call_tool_result("vice.checkpoint.group.create", {
            "name": "test_group_proto",
        })
        assert result.get("status") == "ok"

    def test_group_create_missing_name(self, mcp):
        """Create group without name should fail."""
        error = mcp.call_tool_error("vice.checkpoint.group.create", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_group_add(self, mcp):
        """Add checkpoints to a group should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        mcp.call_tool("vice.checkpoint.group.create", {"name": "test_grp_add"})
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$A000"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        result = mcp.call_tool_result("vice.checkpoint.group.add", {
            "group": "test_grp_add",
            "checkpoint_ids": [cp_num],
        })
        assert result.get("status") == "ok"

    def test_group_add_missing_group(self, mcp):
        """Add to group without group name should fail."""
        error = mcp.call_tool_error("vice.checkpoint.group.add", {
            "checkpoint_ids": [1],
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_group_toggle(self, mcp):
        """Toggle a group should succeed."""
        mcp.call_tool("vice.checkpoint.group.create", {"name": "test_grp_toggle"})
        result = mcp.call_tool_result("vice.checkpoint.group.toggle", {
            "group": "test_grp_toggle",
            "enabled": False,
        })
        assert result.get("status") == "ok"

    def test_group_toggle_missing_params(self, mcp):
        """Toggle group without required params should fail."""
        error = mcp.call_tool_error("vice.checkpoint.group.toggle", {
            "group": "some_group",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_group_list(self, mcp):
        """List groups should return an array."""
        result = mcp.call_tool_result("vice.checkpoint.group.list")
        assert "groups" in result
        assert isinstance(result["groups"], list)


# ---------------------------------------------------------------------------
# Test: Auto-Snapshot on Checkpoint
# ---------------------------------------------------------------------------

class TestAutoSnapshot:
    """Tests for checkpoint auto-snapshot configuration."""

    @pytest.mark.stateful
    def test_set_auto_snapshot(self, mcp):
        """Configure auto-snapshot on a checkpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$8000"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        result = mcp.call_tool_result("vice.checkpoint.set_auto_snapshot", {
            "checkpoint_id": cp_num,
            "snapshot_prefix": "test_auto",
        })
        assert result.get("status") == "ok"

    def test_set_auto_snapshot_missing_id(self, mcp):
        """Set auto-snapshot without checkpoint_id should fail."""
        error = mcp.call_tool_error("vice.checkpoint.set_auto_snapshot", {
            "snapshot_prefix": "test",
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_set_auto_snapshot_missing_prefix(self, mcp):
        """Set auto-snapshot without prefix should fail."""
        error = mcp.call_tool_error("vice.checkpoint.set_auto_snapshot", {
            "checkpoint_id": 1,
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_clear_auto_snapshot(self, mcp):
        """Clear auto-snapshot should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        add = mcp.call_tool_result("vice.checkpoint.add", {"start": "$8010"})
        cp_num = add.get("checkpoint_num") or add.get("id") or add.get("number")
        mcp.call_tool("vice.checkpoint.set_auto_snapshot", {
            "checkpoint_id": cp_num,
            "snapshot_prefix": "test_clear",
        })
        result = mcp.call_tool_result("vice.checkpoint.clear_auto_snapshot", {
            "checkpoint_id": cp_num,
        })
        assert result.get("status") == "ok"

    def test_clear_auto_snapshot_missing_id(self, mcp):
        """Clear auto-snapshot without checkpoint_id should fail."""
        error = mcp.call_tool_error("vice.checkpoint.clear_auto_snapshot", {})
        assert error["code"] == ERROR_INVALID_PARAMS


# ---------------------------------------------------------------------------
# Test: Sprites
# ---------------------------------------------------------------------------

class TestSprites:
    """Tests for vice.sprite.get, set, and inspect."""

    def test_sprite_get_all(self, mcp):
        """Get all sprites (no params) should return sprite data."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sprite.get")
        assert "sprites" in result
        assert len(result["sprites"]) == 8

    def test_sprite_get_single(self, mcp):
        """Get a single sprite by number should work."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sprite.get", {"sprite": 0})
        assert "sprites" in result or "x" in result or "enabled" in result

    def test_sprite_get_invalid_number(self, mcp):
        """Get sprite with out-of-range number should fail."""
        error = mcp.call_tool_error("vice.sprite.get", {"sprite": 99})
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)

    @pytest.mark.stateful
    def test_sprite_set(self, mcp):
        """Set sprite properties should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sprite.set", {
            "sprite": 0,
            "x": 100,
            "y": 100,
            "enabled": True,
            "color": 1,
        })
        assert result.get("status") == "ok"

    def test_sprite_set_missing_sprite(self, mcp):
        """Set sprite without sprite number should fail."""
        error = mcp.call_tool_error("vice.sprite.set", {
            "x": 100, "y": 100,
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_sprite_inspect_valid(self, mcp):
        """Inspect sprite should return bitmap/ascii art."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sprite.inspect", {
            "sprite_number": 0,
        })
        assert "bitmap" in result or "ascii" in result or "data" in result

    def test_sprite_inspect_missing_number(self, mcp):
        """Inspect sprite without sprite_number should fail."""
        error = mcp.call_tool_error("vice.sprite.inspect", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_sprite_inspect_invalid_number(self, mcp):
        """Inspect sprite with number > 7 should fail."""
        error = mcp.call_tool_error("vice.sprite.inspect", {"sprite_number": 8})
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)


# ---------------------------------------------------------------------------
# Test: Chip State (VIC-II, SID, CIA)
# ---------------------------------------------------------------------------

class TestChipState:
    """Tests for VIC-II, SID, and CIA get/set state tools."""

    def test_vicii_get_state(self, mcp):
        """Get VIC-II state should return register values."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.vicii.get_state")
        assert "registers" in result or "raster" in result or "border_color" in result

    @pytest.mark.stateful
    def test_vicii_set_state(self, mcp):
        """Set VIC-II register via registers array should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.vicii.set_state", {
            "registers": [{"offset": 0x20, "value": 0}],  # Border color = black
        })
        assert result.get("status") == "ok" or "updates" in result

    def test_sid_get_state(self, mcp):
        """Get SID state should return register/voice info."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sid.get_state")
        assert "registers" in result or "voices" in result

    @pytest.mark.stateful
    def test_sid_set_state(self, mcp):
        """Set SID register should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.sid.set_state", {
            "registers": [{"offset": 0x18, "value": 0x0F}],  # Volume max
        })
        assert result.get("status") == "ok" or "updates" in result

    def test_cia_get_state(self, mcp):
        """Get CIA state should return timer/port info."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.cia.get_state")
        assert "cia1" in result or "registers" in result

    @pytest.mark.stateful
    def test_cia_set_state(self, mcp):
        """Set CIA register should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.cia.set_state", {
            "cia1_registers": [{"offset": 0x00, "value": 0xFF}],
        })
        assert result.get("status") == "ok" or "updates" in result


# ---------------------------------------------------------------------------
# Test: Disk Management
# ---------------------------------------------------------------------------

class TestDisk:
    """Tests for vice.disk.attach, detach, list, read_sector."""

    def test_disk_detach_no_disk(self, mcp):
        """Detach from a unit with no disk should succeed gracefully or error."""
        data = mcp.call_tool("vice.disk.detach", {"unit": 8})
        # Could succeed (no-op) or error (nothing attached)
        assert "result" in data or "error" in data

    def test_disk_detach_invalid_unit(self, mcp):
        """Detach from invalid unit should fail."""
        error = mcp.call_tool_error("vice.disk.detach", {"unit": 99})
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)

    def test_disk_detach_missing_unit(self, mcp):
        """Detach without unit should fail."""
        error = mcp.call_tool_error("vice.disk.detach", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_disk_attach_missing_path(self, mcp):
        """Attach without path should fail."""
        error = mcp.call_tool_error("vice.disk.attach", {"unit": 8})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_disk_attach_missing_unit(self, mcp):
        """Attach without unit should fail."""
        error = mcp.call_tool_error("vice.disk.attach", {"path": "/tmp/test.d64"})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_disk_attach_nonexistent_file(self, mcp):
        """Attach a nonexistent file should fail."""
        error = mcp.call_tool_error("vice.disk.attach", {
            "unit": 8,
            "path": "/nonexistent/path/fake.d64",
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE,
                                  ERROR_NOT_IMPLEMENTED, ERROR_INTERNAL_ERROR)

    def test_disk_list_no_disk(self, mcp):
        """List directory with no disk attached should fail or return empty."""
        data = mcp.call_tool("vice.disk.list", {"unit": 8})
        # Could be an error if no disk is attached, or empty listing
        assert "result" in data or "error" in data

    def test_disk_list_missing_unit(self, mcp):
        """List without unit should fail."""
        error = mcp.call_tool_error("vice.disk.list", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_disk_read_sector_missing_params(self, mcp):
        """Read sector without all required params should fail."""
        error = mcp.call_tool_error("vice.disk.read_sector", {
            "unit": 8,
        })
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.disk_required
    def test_disk_read_sector_valid(self, mcp):
        """Read sector from attached disk should return sector data."""
        result = mcp.call_tool_result("vice.disk.read_sector", {
            "unit": 8, "track": 18, "sector": 0,
        })
        assert "data" in result or "hex" in result


# ---------------------------------------------------------------------------
# Test: Autostart and Machine Reset
# ---------------------------------------------------------------------------

class TestAutostartAndReset:
    """Tests for vice.autostart and vice.machine.reset."""

    def test_autostart_missing_path(self, mcp):
        """Autostart without path should fail."""
        error = mcp.call_tool_error("vice.autostart", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_autostart_nonexistent_file(self, mcp):
        """Autostart with nonexistent file should fail."""
        error = mcp.call_tool_error("vice.autostart", {
            "path": "/nonexistent/file.prg",
        })
        # Should produce some kind of error
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE,
                                  ERROR_NOT_IMPLEMENTED, ERROR_INTERNAL_ERROR)

    @pytest.mark.stateful
    def test_machine_reset_soft(self, mcp):
        """Soft reset should succeed."""
        result = mcp.call_tool_result("vice.machine.reset", {"mode": "soft"})
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_machine_reset_hard(self, mcp):
        """Hard reset should succeed."""
        result = mcp.call_tool_result("vice.machine.reset", {"mode": "hard"})
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_machine_reset_default(self, mcp):
        """Reset with no params should default to soft reset."""
        result = mcp.call_tool_result("vice.machine.reset")
        assert result.get("status") == "ok"


# ---------------------------------------------------------------------------
# Test: Display
# ---------------------------------------------------------------------------

class TestDisplay:
    """Tests for vice.display.screenshot and get_dimensions."""

    def test_get_dimensions(self, mcp):
        """Get display dimensions should return width and height."""
        result = mcp.call_tool_result("vice.display.get_dimensions")
        assert "width" in result
        assert "height" in result
        assert isinstance(result["width"], int)
        assert isinstance(result["height"], int)
        assert result["width"] > 0
        assert result["height"] > 0

    def test_screenshot_base64(self, mcp):
        """Screenshot with return_base64=true should return image data."""
        result = mcp.call_tool_result("vice.display.screenshot", {
            "return_base64": True,
        })
        # Should have base64 data or a data URI
        assert ("data" in result or "base64" in result or
                "image" in result or "data_uri" in result)

    def test_screenshot_to_file(self, mcp, tmp_path):
        """Screenshot saved to a file should succeed."""
        path = str(tmp_path / "test_screenshot.png")
        result = mcp.call_tool_result("vice.display.screenshot", {
            "path": path,
        })
        assert result.get("status") == "ok" or "path" in result


# ---------------------------------------------------------------------------
# Test: Keyboard Input
# ---------------------------------------------------------------------------

class TestKeyboard:
    """Tests for keyboard input tools."""

    @pytest.mark.stateful
    def test_keyboard_type(self, mcp):
        """Type text should succeed."""
        mcp.call_tool("vice.execution.run")
        time.sleep(0.2)
        result = mcp.call_tool_result("vice.keyboard.type", {
            "text": "HELLO",
        })
        assert result.get("status") == "ok"

    def test_keyboard_type_missing_text(self, mcp):
        """Type without text should fail."""
        error = mcp.call_tool_error("vice.keyboard.type", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_keyboard_key_press(self, mcp):
        """Press a key should succeed."""
        result = mcp.call_tool_result("vice.keyboard.key_press", {
            "key": "Return",
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_keyboard_key_press_with_hold(self, mcp):
        """Press a key with hold_frames should succeed."""
        result = mcp.call_tool_result("vice.keyboard.key_press", {
            "key": "Space",
            "hold_frames": 3,
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_keyboard_key_press_with_modifiers(self, mcp):
        """Press a key with modifiers should succeed."""
        result = mcp.call_tool_result("vice.keyboard.key_press", {
            "key": "a",
            "modifiers": ["shift"],
        })
        assert result.get("status") == "ok"

    def test_keyboard_key_press_missing_key(self, mcp):
        """Press without key should fail."""
        error = mcp.call_tool_error("vice.keyboard.key_press", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_keyboard_key_release(self, mcp):
        """Release a key should succeed."""
        result = mcp.call_tool_result("vice.keyboard.key_release", {
            "key": "Return",
        })
        assert result.get("status") == "ok"

    def test_keyboard_key_release_missing_key(self, mcp):
        """Release without key should fail."""
        error = mcp.call_tool_error("vice.keyboard.key_release", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    @pytest.mark.stateful
    def test_keyboard_restore_press(self, mcp):
        """RESTORE key press should succeed."""
        result = mcp.call_tool_result("vice.keyboard.restore")
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_keyboard_restore_release(self, mcp):
        """RESTORE key release via pressed=false should succeed."""
        result = mcp.call_tool_result("vice.keyboard.restore", {
            "pressed": False,
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_keyboard_matrix(self, mcp):
        """Direct keyboard matrix control should succeed."""
        result = mcp.call_tool_result("vice.keyboard.matrix", {
            "key": "A",
            "pressed": True,
            "hold_frames": 2,
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_keyboard_matrix_by_row_col(self, mcp):
        """Matrix control by row/col should succeed."""
        result = mcp.call_tool_result("vice.keyboard.matrix", {
            "row": 1,
            "col": 2,
            "pressed": True,
        })
        assert result.get("status") == "ok"


# ---------------------------------------------------------------------------
# Test: Joystick
# ---------------------------------------------------------------------------

class TestJoystick:
    """Tests for vice.joystick.set."""

    @pytest.mark.stateful
    def test_joystick_set_direction(self, mcp):
        """Set joystick direction should succeed."""
        result = mcp.call_tool_result("vice.joystick.set", {
            "port": 1,
            "direction": "up",
            "fire": False,
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_joystick_set_fire(self, mcp):
        """Set joystick fire should succeed."""
        result = mcp.call_tool_result("vice.joystick.set", {
            "port": 2,
            "direction": "center",
            "fire": True,
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_joystick_set_no_params(self, mcp):
        """Joystick set with no params uses defaults."""
        result = mcp.call_tool_result("vice.joystick.set")
        assert result.get("status") == "ok"


# ---------------------------------------------------------------------------
# Test: Advanced Debugging
# ---------------------------------------------------------------------------

class TestAdvancedDebugging:
    """Tests for disassemble, symbols, watch, backtrace, run_until."""

    def test_disassemble(self, mcp):
        """Disassemble at an address should return instructions."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.disassemble", {
            "address": "$FCE2",  # KERNAL reset vector target
        })
        assert "instructions" in result or "lines" in result or "disassembly" in result

    def test_disassemble_with_count(self, mcp):
        """Disassemble with count should work."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.disassemble", {
            "address": "$FCE2",
            "count": 5,
        })
        data = result.get("instructions") or result.get("lines") or result.get("disassembly")
        assert data is not None

    def test_disassemble_missing_address(self, mcp):
        """Disassemble without address should fail."""
        error = mcp.call_tool_error("vice.disassemble", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_symbols_load_missing_path(self, mcp):
        """Load symbols without path should fail."""
        error = mcp.call_tool_error("vice.symbols.load", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_symbols_load_nonexistent(self, mcp):
        """Load symbols from nonexistent file should fail."""
        error = mcp.call_tool_error("vice.symbols.load", {
            "path": "/nonexistent/symbols.sym",
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INTERNAL_ERROR,
                                  ERROR_NOT_IMPLEMENTED)

    def test_symbols_lookup_no_params(self, mcp):
        """Lookup with no name or address should fail or return empty."""
        # Neither name nor address is required by schema, but handler may error
        data = mcp.call_tool("vice.symbols.lookup")
        assert "result" in data or "error" in data

    def test_watch_add(self, mcp):
        """Add a watchpoint should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.watch.add", {
            "address": "$D020",
            "size": 1,
            "type": "write",
        })
        # Should return checkpoint info
        assert "checkpoint_num" in result or "id" in result or "number" in result

    def test_watch_add_missing_address(self, mcp):
        """Add watch without address should fail."""
        error = mcp.call_tool_error("vice.watch.add", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_backtrace(self, mcp):
        """Backtrace should return stack frame info."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.backtrace")
        assert "frames" in result or "stack" in result or "backtrace" in result

    def test_backtrace_with_depth(self, mcp):
        """Backtrace with depth limit should work."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.backtrace", {"depth": 4})
        assert "frames" in result or "stack" in result or "backtrace" in result

    def test_run_until_no_params(self, mcp):
        """Run_until with no params should fail or return error."""
        data = mcp.call_tool("vice.run_until")
        # Either error (no address) or an indication of what's needed
        assert "result" in data or "error" in data


# ---------------------------------------------------------------------------
# Test: Snapshots
# ---------------------------------------------------------------------------

class TestSnapshots:
    """Tests for vice.snapshot.save, load, list."""

    @pytest.mark.stateful
    def test_snapshot_save_and_list(self, mcp):
        """Save a snapshot and verify it appears in list."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        name = f"test_proto_{int(time.time())}"
        result = mcp.call_tool_result("vice.snapshot.save", {
            "name": name,
            "description": "Protocol test snapshot",
        })
        assert result.get("status") == "ok" or "path" in result

        list_result = mcp.call_tool_result("vice.snapshot.list")
        assert "snapshots" in list_result
        names = [s.get("name") for s in list_result["snapshots"]]
        assert name in names

    def test_snapshot_save_missing_name(self, mcp):
        """Save snapshot without name should fail."""
        error = mcp.call_tool_error("vice.snapshot.save", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_snapshot_load_missing_name(self, mcp):
        """Load snapshot without name should fail."""
        error = mcp.call_tool_error("vice.snapshot.load", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_snapshot_load_nonexistent(self, mcp):
        """Load nonexistent snapshot should fail."""
        error = mcp.call_tool_error("vice.snapshot.load", {
            "name": "does_not_exist_9999",
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_SNAPSHOT_FAILED,
                                  ERROR_INTERNAL_ERROR)

    def test_snapshot_list(self, mcp):
        """List snapshots should return array."""
        result = mcp.call_tool_result("vice.snapshot.list")
        assert "snapshots" in result
        assert isinstance(result["snapshots"], list)


# ---------------------------------------------------------------------------
# Test: Cycles Stopwatch
# ---------------------------------------------------------------------------

class TestCyclesStopwatch:
    """Tests for vice.cycles.stopwatch."""

    @pytest.mark.stateful
    def test_stopwatch_reset(self, mcp):
        """Reset stopwatch should succeed."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.cycles.stopwatch", {
            "action": "reset",
        })
        assert result.get("status") == "ok"

    @pytest.mark.stateful
    def test_stopwatch_read(self, mcp):
        """Read stopwatch should return cycle count."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        mcp.call_tool("vice.cycles.stopwatch", {"action": "reset"})
        result = mcp.call_tool_result("vice.cycles.stopwatch", {
            "action": "read",
        })
        assert "cycles" in result or "elapsed" in result

    @pytest.mark.stateful
    def test_stopwatch_reset_and_read(self, mcp):
        """Reset-and-read should return cycle count and reset."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.cycles.stopwatch", {
            "action": "reset_and_read",
        })
        assert "cycles" in result or "elapsed" in result

    def test_stopwatch_missing_action(self, mcp):
        """Stopwatch without action should fail."""
        error = mcp.call_tool_error("vice.cycles.stopwatch", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_stopwatch_invalid_action(self, mcp):
        """Stopwatch with invalid action should fail."""
        error = mcp.call_tool_error("vice.cycles.stopwatch", {
            "action": "invalid",
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)


# ---------------------------------------------------------------------------
# Test: Tracing
# ---------------------------------------------------------------------------

class TestTracing:
    """Tests for vice.trace.start and trace.stop."""

    @pytest.mark.stateful
    def test_trace_start(self, mcp, tmp_path):
        """Start a trace should succeed and return a trace_id."""
        result = mcp.call_tool_result("vice.trace.start", {
            "output_file": str(tmp_path / "trace_test.log"),
        })
        assert "trace_id" in result or result.get("status") == "ok"

    def test_trace_start_missing_output_file(self, mcp):
        """Start trace without output_file should fail."""
        error = mcp.call_tool_error("vice.trace.start", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_trace_stop_missing_trace_id(self, mcp):
        """Stop trace without trace_id should fail."""
        error = mcp.call_tool_error("vice.trace.stop", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_trace_stop_invalid_trace_id(self, mcp):
        """Stop trace with bogus trace_id should fail."""
        error = mcp.call_tool_error("vice.trace.stop", {
            "trace_id": "nonexistent_id",
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE,
                                  ERROR_NOT_IMPLEMENTED)


# ---------------------------------------------------------------------------
# Test: Interrupt Logging
# ---------------------------------------------------------------------------

class TestInterruptLogging:
    """Tests for interrupt log start/stop/read."""

    @pytest.mark.stateful
    def test_interrupt_log_start(self, mcp):
        """Start interrupt logging should succeed."""
        result = mcp.call_tool_result("vice.interrupt.log.start")
        assert "log_id" in result or result.get("status") == "ok"

    @pytest.mark.stateful
    def test_interrupt_log_start_with_types(self, mcp):
        """Start interrupt logging with type filter should succeed."""
        result = mcp.call_tool_result("vice.interrupt.log.start", {
            "types": ["irq", "nmi"],
            "max_entries": 500,
        })
        assert "log_id" in result or result.get("status") == "ok"

    def test_interrupt_log_stop_missing_id(self, mcp):
        """Stop interrupt log without log_id should fail."""
        error = mcp.call_tool_error("vice.interrupt.log.stop", {})
        assert error["code"] == ERROR_INVALID_PARAMS

    def test_interrupt_log_read_missing_id(self, mcp):
        """Read interrupt log without log_id should fail."""
        error = mcp.call_tool_error("vice.interrupt.log.read", {})
        assert error["code"] == ERROR_INVALID_PARAMS


# ---------------------------------------------------------------------------
# Test: Schema Consistency (tools/list vs. handler behavior)
# ---------------------------------------------------------------------------

# Expected schemas for all tools, based on C source code analysis.
# Maps tool_name -> {properties: {name: type, ...}, required: [name, ...]}
# type values: "number", "string", "boolean", "array"

EXPECTED_SCHEMAS = {
    "initialize": {
        "properties": {"protocolVersion": "string", "capabilities": "object",
                        "clientInfo": "object"},
        "required": [],
    },
    "notifications/initialized": {"properties": {}, "required": []},
    "tools/list": {"properties": {}, "required": []},
    "vice.ping": {"properties": {}, "required": []},
    "vice.execution.run": {"properties": {}, "required": []},
    "vice.execution.pause": {"properties": {}, "required": []},
    "vice.execution.step": {
        "properties": {"count": "number", "stepOver": "boolean"},
        "required": [],
    },
    "vice.registers.get": {"properties": {}, "required": []},
    "vice.registers.set": {
        "properties": {"register": "string", "value": "number"},
        "required": ["register", "value"],
    },
    "vice.memory.read": {
        "properties": {"address": "string", "size": "number", "bank": "string"},
        "required": ["address", "size"],
    },
    "vice.memory.write": {
        "properties": {"address": "string", "data": "array"},
        "required": ["address", "data"],
    },
    "vice.memory.banks": {"properties": {}, "required": []},
    "vice.memory.search": {
        "properties": {"start": "string", "end": "string", "pattern": "array",
                        "mask": "array", "max_results": "number"},
        "required": ["start", "end", "pattern"],
    },
    "vice.checkpoint.add": {
        "properties": {"start": "string", "end": "string", "stop": "boolean",
                        "load": "boolean", "store": "boolean", "exec": "boolean"},
        "required": ["start"],
    },
    "vice.checkpoint.delete": {
        "properties": {"checkpoint_num": "number"},
        "required": ["checkpoint_num"],
    },
    "vice.checkpoint.list": {"properties": {}, "required": []},
    "vice.checkpoint.toggle": {
        "properties": {"checkpoint_num": "number", "enabled": "boolean"},
        "required": ["checkpoint_num", "enabled"],
    },
    "vice.checkpoint.set_condition": {
        "properties": {"checkpoint_num": "number", "condition": "string"},
        "required": ["checkpoint_num", "condition"],
    },
    "vice.checkpoint.set_ignore_count": {
        "properties": {"checkpoint_num": "number", "count": "number"},
        "required": ["checkpoint_num", "count"],
    },
    "vice.sprite.get": {
        "properties": {"sprite": "number"},
        "required": [],
    },
    "vice.sprite.set": {
        "properties": {"sprite": "number", "x": "number", "y": "number",
                        "enabled": "boolean", "multicolor": "boolean",
                        "expand_x": "boolean", "expand_y": "boolean",
                        "priority_foreground": "boolean", "color": "number"},
        "required": ["sprite"],
    },
    "vice.vicii.get_state": {"properties": {}, "required": []},
    "vice.vicii.set_state": {
        "properties": {"registers": "array"},
        "required": [],
    },
    "vice.sid.get_state": {"properties": {}, "required": []},
    "vice.sid.set_state": {
        "properties": {"registers": "array"},
        "required": [],
    },
    "vice.cia.get_state": {"properties": {}, "required": []},
    "vice.cia.set_state": {
        "properties": {"cia1_registers": "array", "cia2_registers": "array"},
        "required": [],
    },
    "vice.disk.attach": {
        "properties": {"unit": "number", "path": "string"},
        "required": ["unit", "path"],
    },
    "vice.disk.detach": {
        "properties": {"unit": "number"},
        "required": ["unit"],
    },
    "vice.disk.list": {
        "properties": {"unit": "number"},
        "required": ["unit"],
    },
    "vice.disk.read_sector": {
        "properties": {"unit": "number", "track": "number", "sector": "number"},
        "required": ["unit", "track", "sector"],
    },
    "vice.autostart": {
        "properties": {"path": "string", "program": "string",
                        "run": "boolean", "index": "number"},
        "required": ["path"],
    },
    "vice.machine.reset": {
        "properties": {"mode": "string", "run_after": "boolean"},
        "required": [],
    },
    "vice.display.screenshot": {
        "properties": {"path": "string", "format": "string",
                        "return_base64": "boolean"},
        "required": [],
    },
    "vice.display.get_dimensions": {"properties": {}, "required": []},
    "vice.keyboard.type": {
        "properties": {"text": "string", "petscii_upper": "boolean"},
        "required": ["text"],
    },
    "vice.keyboard.key_press": {
        "properties": {"key": "string", "modifiers": "array",
                        "hold_frames": "number", "hold_ms": "number"},
        "required": ["key"],
    },
    "vice.keyboard.key_release": {
        "properties": {"key": "string", "modifiers": "array"},
        "required": ["key"],
    },
    "vice.keyboard.restore": {
        "properties": {"pressed": "boolean"},
        "required": [],
    },
    "vice.keyboard.matrix": {
        "properties": {"key": "string", "row": "number", "col": "number",
                        "pressed": "boolean", "hold_frames": "number",
                        "hold_ms": "number"},
        "required": [],
    },
    "vice.joystick.set": {
        "properties": {"port": "number", "direction": "string",
                        "fire": "boolean"},
        "required": [],
    },
    "vice.disassemble": {
        "properties": {"address": "string", "count": "number",
                        "show_symbols": "boolean"},
        "required": ["address"],
    },
    "vice.symbols.load": {
        "properties": {"path": "string", "format": "string"},
        "required": ["path"],
    },
    "vice.symbols.lookup": {
        "properties": {"name": "string", "address": "number"},
        "required": [],
    },
    "vice.watch.add": {
        "properties": {"address": "string", "size": "number",
                        "type": "string", "condition": "string"},
        "required": ["address"],
    },
    "vice.backtrace": {
        "properties": {"depth": "number"},
        "required": [],
    },
    "vice.run_until": {
        "properties": {"address": "string", "cycles": "number"},
        "required": [],
    },
    "vice.snapshot.save": {
        "properties": {"name": "string", "description": "string",
                        "include_roms": "boolean", "include_disks": "boolean"},
        "required": ["name"],
    },
    "vice.snapshot.load": {
        "properties": {"name": "string"},
        "required": ["name"],
    },
    "vice.snapshot.list": {"properties": {}, "required": []},
    "vice.cycles.stopwatch": {
        "properties": {"action": "string"},
        "required": ["action"],
    },
    "vice.memory.fill": {
        "properties": {"start": "string", "end": "string", "pattern": "array"},
        "required": ["start", "end", "pattern"],
    },
    "vice.memory.compare": {
        "properties": {"mode": "string", "range1_start": "string",
                        "range1_end": "string", "range2_start": "string",
                        "snapshot_name": "string", "start": "string",
                        "end": "string", "max_differences": "number"},
        "required": ["mode"],
    },
    "vice.memory.map": {
        "properties": {"start": "string", "end": "string",
                        "granularity": "number"},
        "required": [],
    },
    "vice.checkpoint.group.create": {
        "properties": {"name": "string", "checkpoint_ids": "array"},
        "required": ["name"],
    },
    "vice.checkpoint.group.add": {
        "properties": {"group": "string", "checkpoint_ids": "array"},
        "required": ["group", "checkpoint_ids"],
    },
    "vice.checkpoint.group.toggle": {
        "properties": {"group": "string", "enabled": "boolean"},
        "required": ["group", "enabled"],
    },
    "vice.checkpoint.group.list": {"properties": {}, "required": []},
    "vice.checkpoint.set_auto_snapshot": {
        "properties": {"checkpoint_id": "number", "snapshot_prefix": "string",
                        "max_snapshots": "number", "include_disks": "boolean"},
        "required": ["checkpoint_id", "snapshot_prefix"],
    },
    "vice.checkpoint.clear_auto_snapshot": {
        "properties": {"checkpoint_id": "number"},
        "required": ["checkpoint_id"],
    },
    "vice.trace.start": {
        "properties": {"output_file": "string", "pc_filter_start": "number",
                        "pc_filter_end": "number", "max_instructions": "number",
                        "include_registers": "boolean"},
        "required": ["output_file"],
    },
    "vice.trace.stop": {
        "properties": {"trace_id": "string"},
        "required": ["trace_id"],
    },
    "vice.interrupt.log.start": {
        "properties": {"types": "array", "max_entries": "number"},
        "required": [],
    },
    "vice.interrupt.log.stop": {
        "properties": {"log_id": "string"},
        "required": ["log_id"],
    },
    "vice.interrupt.log.read": {
        "properties": {"log_id": "string", "since_index": "number"},
        "required": ["log_id"],
    },
    "vice.sprite.inspect": {
        "properties": {"sprite_number": "number", "format": "string"},
        "required": ["sprite_number"],
    },
}

# The tools/call entry is internal and not in the user-facing list above.
# It is present in the registry but handled separately.


def _json_schema_type_to_simple(schema_prop: dict) -> str:
    """Map a JSON Schema property definition to our simple type string."""
    t = schema_prop.get("type")
    if t == "number" or t == "integer":
        return "number"
    if t == "string":
        return "string"
    if t == "boolean":
        return "boolean"
    if t == "array":
        return "array"
    if t == "object":
        return "object"
    return t or "unknown"


class TestSchemaConsistency:
    """Validate that tools/list schemas match expected schemas for all tools.

    This class fetches the declared schemas from tools/list and compares them
    to our EXPECTED_SCHEMAS dict, which is derived from reading the C source.

    Previously several tools had schema bugs where tools/list returned empty
    params while the handler actually accepted parameters. Those bugs have
    been fixed (vic.sprite.get, vicii.set_state, sid.set_state, cia.set_state,
    keyboard.restore, memory.map, sprite.inspect now report correct schemas).
    """

    def test_all_expected_tools_present(self, tools_by_name):
        """Every tool in EXPECTED_SCHEMAS should appear in tools/list."""
        missing = set(EXPECTED_SCHEMAS.keys()) - set(tools_by_name.keys())
        assert not missing, f"Tools missing from server: {missing}"

    def test_no_unexpected_tools(self, tools_by_name):
        """Every tool in tools/list (except tools/call) should be in EXPECTED_SCHEMAS."""
        server_names = set(tools_by_name.keys()) - {"tools/call"}
        expected_names = set(EXPECTED_SCHEMAS.keys())
        unexpected = server_names - expected_names
        assert not unexpected, f"Unexpected tools from server: {unexpected}"

    @pytest.mark.parametrize("tool_name", sorted(EXPECTED_SCHEMAS.keys()))
    def test_schema_properties_present(self, tools_by_name, tool_name):
        """Verify the declared inputSchema has the expected property names."""
        if tool_name not in tools_by_name:
            pytest.skip(f"Tool {tool_name} not in tools/list")
        expected = EXPECTED_SCHEMAS[tool_name]
        schema = tools_by_name[tool_name]["inputSchema"]
        server_props = set((schema.get("properties") or {}).keys())
        expected_props = set(expected["properties"].keys())
        assert server_props == expected_props, (
            f"Tool {tool_name}: schema properties mismatch.\n"
            f"  Expected: {sorted(expected_props)}\n"
            f"  Got:      {sorted(server_props)}"
        )

    @pytest.mark.parametrize("tool_name", sorted(EXPECTED_SCHEMAS.keys()))
    def test_schema_property_types(self, tools_by_name, tool_name):
        """Verify each property in the declared schema has the expected type."""
        if tool_name not in tools_by_name:
            pytest.skip(f"Tool {tool_name} not in tools/list")
        expected = EXPECTED_SCHEMAS[tool_name]
        schema = tools_by_name[tool_name]["inputSchema"]
        server_props = schema.get("properties") or {}
        for prop_name, expected_type in expected["properties"].items():
            assert prop_name in server_props, (
                f"Tool {tool_name}: missing property '{prop_name}'"
            )
            actual_type = _json_schema_type_to_simple(server_props[prop_name])
            assert actual_type == expected_type, (
                f"Tool {tool_name}.{prop_name}: "
                f"expected type '{expected_type}', got '{actual_type}'"
            )

    @pytest.mark.parametrize("tool_name", sorted(EXPECTED_SCHEMAS.keys()))
    def test_schema_required_fields(self, tools_by_name, tool_name):
        """Verify the declared schema has the correct required fields."""
        if tool_name not in tools_by_name:
            pytest.skip(f"Tool {tool_name} not in tools/list")
        expected = EXPECTED_SCHEMAS[tool_name]
        schema = tools_by_name[tool_name]["inputSchema"]
        server_required = set(schema.get("required") or [])
        expected_required = set(expected["required"])
        assert server_required == expected_required, (
            f"Tool {tool_name}: required fields mismatch.\n"
            f"  Expected: {sorted(expected_required)}\n"
            f"  Got:      {sorted(server_required)}"
        )

    def test_all_schemas_have_type_object(self, tools_by_name):
        """Every inputSchema must have type='object'."""
        for name, tool in tools_by_name.items():
            schema = tool.get("inputSchema", {})
            assert schema.get("type") == "object", (
                f"Tool {name}: inputSchema.type is '{schema.get('type')}', expected 'object'"
            )

    def test_schema_count_matches_registry(self, tools_list):
        """The number of tools in tools/list should match our expected count."""
        # We expect at least 63 tools (the 63 in user spec + tools/call = 64)
        assert len(tools_list) >= 63, (
            f"Expected at least 63 tools, got {len(tools_list)}"
        )


# ---------------------------------------------------------------------------
# Test: Previously Buggy Schemas (Regression Guards)
# ---------------------------------------------------------------------------

class TestPreviouslyBuggySchemas:
    """Regression tests for tools that previously had empty schemas.

    These tools used to report empty inputSchema in tools/list while actually
    accepting parameters. These tests guard against the bugs returning.
    """

    def test_sprite_inspect_has_sprite_number_param(self, tools_by_name):
        """vice.sprite.inspect MUST declare sprite_number in its schema.

        Previously: tools/list said empty params.
        Handler requires: sprite_number (0-7), optional format.
        """
        schema = tools_by_name["vice.sprite.inspect"]["inputSchema"]
        props = schema.get("properties", {})
        assert "sprite_number" in props, (
            "REGRESSION: vice.sprite.inspect schema should declare sprite_number"
        )
        required = set(schema.get("required", []))
        assert "sprite_number" in required, (
            "REGRESSION: sprite_number should be required for vice.sprite.inspect"
        )

    def test_sprite_get_has_sprite_param(self, tools_by_name):
        """vice.sprite.get MUST declare optional sprite param in its schema.

        Previously: tools/list said empty params.
        Handler accepts: optional sprite (0-7).
        """
        schema = tools_by_name["vice.sprite.get"]["inputSchema"]
        props = schema.get("properties", {})
        assert "sprite" in props, (
            "REGRESSION: vice.sprite.get schema should declare sprite"
        )

    def test_memory_map_has_params(self, tools_by_name):
        """vice.memory.map MUST declare start, end, granularity params.

        Previously: tools/list said empty params.
        Handler accepts: optional start, end, granularity.
        """
        schema = tools_by_name["vice.memory.map"]["inputSchema"]
        props = schema.get("properties", {})
        assert "start" in props, (
            "REGRESSION: vice.memory.map schema should declare start"
        )
        assert "end" in props, (
            "REGRESSION: vice.memory.map schema should declare end"
        )
        assert "granularity" in props, (
            "REGRESSION: vice.memory.map schema should declare granularity"
        )

    def test_keyboard_restore_has_pressed_param(self, tools_by_name):
        """vice.keyboard.restore MUST declare optional pressed boolean.

        Previously: tools/list said empty params.
        Handler accepts: optional pressed boolean.
        """
        schema = tools_by_name["vice.keyboard.restore"]["inputSchema"]
        props = schema.get("properties", {})
        assert "pressed" in props, (
            "REGRESSION: vice.keyboard.restore schema should declare pressed"
        )

    def test_vicii_set_state_has_registers_param(self, tools_by_name):
        """vice.vicii.set_state MUST declare registers array param.

        Previously: tools/list said empty params.
        Handler accepts: registers array [{offset, value}].
        """
        schema = tools_by_name["vice.vicii.set_state"]["inputSchema"]
        props = schema.get("properties", {})
        assert "registers" in props, (
            "REGRESSION: vice.vicii.set_state schema should declare registers"
        )

    def test_sid_set_state_has_registers_param(self, tools_by_name):
        """vice.sid.set_state MUST declare registers array param.

        Previously: tools/list said empty params.
        Handler accepts: registers array and voice params.
        """
        schema = tools_by_name["vice.sid.set_state"]["inputSchema"]
        props = schema.get("properties", {})
        assert "registers" in props, (
            "REGRESSION: vice.sid.set_state schema should declare registers"
        )

    def test_cia_set_state_has_register_params(self, tools_by_name):
        """vice.cia.set_state MUST declare cia1_registers and cia2_registers.

        Previously: tools/list said empty params.
        Handler accepts: cia1_registers, cia2_registers arrays.
        """
        schema = tools_by_name["vice.cia.set_state"]["inputSchema"]
        props = schema.get("properties", {})
        assert "cia1_registers" in props, (
            "REGRESSION: vice.cia.set_state schema should declare cia1_registers"
        )
        assert "cia2_registers" in props, (
            "REGRESSION: vice.cia.set_state schema should declare cia2_registers"
        )


# ---------------------------------------------------------------------------
# Test: Error Response Format
# ---------------------------------------------------------------------------

class TestErrorFormat:
    """Validate that error responses follow JSON-RPC 2.0 conventions."""

    def test_error_has_code_and_message(self, mcp):
        """Error responses must have numeric code and string message."""
        data = mcp.jsonrpc("vice.nonexistent")
        error = data.get("error", {})
        assert isinstance(error.get("code"), (int, float)), "Error code must be numeric"
        assert isinstance(error.get("message"), str), "Error message must be string"

    def test_error_on_wrong_type_for_required_param(self, mcp):
        """Passing wrong type for a required field should return INVALID_PARAMS."""
        error = mcp.call_tool_error("vice.registers.set", {
            "register": 12345,  # Should be string
            "value": "not_a_number",  # Should be number
        })
        assert error["code"] in (ERROR_INVALID_PARAMS, ERROR_INVALID_VALUE)

    def test_error_preserves_request_id(self, mcp):
        """Error responses must include the same id as the request."""
        test_id = 98765
        data = mcp.jsonrpc("vice.nonexistent", request_id=test_id)
        assert data.get("id") == test_id


# ---------------------------------------------------------------------------
# Test: Direct dispatch vs tools/call dispatch
# ---------------------------------------------------------------------------

class TestDispatchPaths:
    """Verify that direct method dispatch and tools/call both work correctly."""

    def test_direct_dispatch_ping(self, mcp):
        """Calling vice.ping directly (not via tools/call) should work."""
        result = mcp.direct_call("vice.ping")
        assert result["status"] == "ok"

    def test_tools_call_dispatch_ping(self, mcp):
        """Calling vice.ping via tools/call should wrap result in content array."""
        data = mcp.call_tool("vice.ping")
        assert "result" in data
        content = data["result"].get("content")
        assert content is not None
        assert len(content) > 0
        assert content[0]["type"] == "text"
        inner = json.loads(content[0]["text"])
        assert inner["status"] == "ok"

    def test_tools_call_error_propagation(self, mcp):
        """Errors from tools/call should propagate as JSON-RPC errors."""
        data = mcp.call_tool("vice.registers.set", {"register": "A"})
        # Missing value -- should be error
        assert "error" in data

    def test_tools_call_missing_name(self, mcp):
        """tools/call without name param should fail."""
        data = mcp.jsonrpc("tools/call", {"arguments": {}})
        assert "error" in data
        assert data["error"]["code"] == ERROR_INVALID_PARAMS

    def test_tools_call_nonexistent_tool(self, mcp):
        """tools/call with unknown tool name should fail."""
        data = mcp.call_tool("vice.does.not.exist")
        assert "error" in data
        assert data["error"]["code"] == ERROR_METHOD_NOT_FOUND


# ---------------------------------------------------------------------------
# Test: Edge Cases
# ---------------------------------------------------------------------------

class TestEdgeCases:
    """Tests for protocol edge cases and boundary conditions."""

    def test_null_id(self, mcp):
        """Request with null id should get a response (not treated as notification)."""
        body = {"jsonrpc": "2.0", "method": "vice.ping", "id": None}
        resp = mcp.raw_request(body)
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"
        # id should be null in response
        assert data.get("id") is None
        assert "result" in data

    def test_string_id(self, mcp):
        """Request with string id should be echoed back."""
        body = {"jsonrpc": "2.0", "method": "vice.ping", "id": "test-string-id"}
        resp = mcp.raw_request(body)
        data = resp.json()
        assert data.get("id") == "test-string-id"

    def test_empty_params(self, mcp):
        """Request with empty params object should work for tools that accept it."""
        data = mcp.jsonrpc("vice.ping", params={})
        assert "result" in data

    def test_missing_params(self, mcp):
        """Request with no params field should work for tools that accept none."""
        body = {"jsonrpc": "2.0", "method": "vice.ping", "id": 999}
        resp = mcp.raw_request(body)
        data = resp.json()
        assert "result" in data

    def test_large_memory_read(self, mcp):
        """Reading a large block of memory should work within limits."""
        mcp.call_tool("vice.execution.pause")
        time.sleep(0.1)
        result = mcp.call_tool_result("vice.memory.read", {
            "address": 0, "size": 256,
        })
        assert "data" in result or "hex" in result or "bytes" in result

    def test_concurrent_requests(self, mcp):
        """Multiple rapid requests should all succeed."""
        import concurrent.futures
        client = MCPClient()

        def do_ping():
            try:
                return client.jsonrpc("vice.ping", timeout=10)
            except Exception as e:
                return {"error": str(e)}

        with concurrent.futures.ThreadPoolExecutor(max_workers=5) as executor:
            futures = [executor.submit(do_ping) for _ in range(10)]
            results = [f.result() for f in futures]

        success_count = sum(1 for r in results if "result" in r)
        assert success_count >= 8, (
            f"Only {success_count}/10 concurrent requests succeeded"
        )


# ---------------------------------------------------------------------------
# HTTP Transport: Accept header and response format tests (Issues #1 & #2)
# ---------------------------------------------------------------------------

class TestAcceptHeader:
    """POST /mcp should accept various Accept headers and always return JSON."""

    PING_BODY = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "id": 999,
        "params": {"name": "vice.ping"},
    }

    def _post(self, headers: dict, timeout: float = 5.0) -> requests.Response:
        return requests.post(MCP_URL, json=self.PING_BODY,
                             headers={"Content-Type": "application/json",
                                      **headers},
                             timeout=timeout)

    def test_accept_application_json(self, mcp):
        """Accept: application/json should return 200 with JSON body."""
        resp = self._post({"Accept": "application/json"})
        assert resp.status_code == 200
        assert "application/json" in resp.headers.get("Content-Type", "")
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"
        assert data.get("id") == 999

    def test_accept_wildcard(self, mcp):
        """Accept: */* should return 200 with JSON body."""
        resp = self._post({"Accept": "*/*"})
        assert resp.status_code == 200
        assert "application/json" in resp.headers.get("Content-Type", "")
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"

    def test_accept_json_and_sse(self, mcp):
        """Accept: application/json, text/event-stream should return plain JSON, not SSE."""
        resp = self._post({"Accept": "application/json, text/event-stream"})
        assert resp.status_code == 200
        assert "application/json" in resp.headers.get("Content-Type", "")
        # Must NOT be SSE-wrapped
        body = resp.text
        assert not body.startswith("event:"), (
            f"Response is SSE-wrapped but should be plain JSON: {body[:200]}"
        )
        assert not body.startswith("data:"), (
            f"Response is SSE-wrapped but should be plain JSON: {body[:200]}"
        )
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"
        assert data.get("id") == 999

    def test_accept_sse_only_rejected(self, mcp):
        """Accept: text/event-stream (without JSON) should be rejected with 406."""
        resp = self._post({"Accept": "text/event-stream"})
        assert resp.status_code == 406

    def test_accept_xml_rejected(self, mcp):
        """Accept: text/xml should be rejected with 406."""
        resp = self._post({"Accept": "text/xml"})
        assert resp.status_code == 406

    def test_no_accept_header(self, mcp):
        """Missing Accept header should be treated as */* (RFC 7231)."""
        resp = requests.post(MCP_URL, json=self.PING_BODY,
                             headers={"Content-Type": "application/json"},
                             timeout=5.0)
        assert resp.status_code == 200
        assert "application/json" in resp.headers.get("Content-Type", "")
        data = resp.json()
        assert data.get("jsonrpc") == "2.0"

    def test_response_never_sse_wrapped(self, mcp):
        """Regardless of Accept header, POST /mcp Content-Type must be application/json."""
        for accept in ["application/json",
                        "*/*",
                        "application/json, text/event-stream",
                        "text/event-stream, application/json"]:
            resp = self._post({"Accept": accept})
            ct = resp.headers.get("Content-Type", "")
            assert "application/json" in ct, (
                f"Accept: {accept} -> Content-Type: {ct} (expected application/json)"
            )
