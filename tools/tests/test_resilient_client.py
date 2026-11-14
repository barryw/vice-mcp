"""Unit tests for tools/resilience/vice_mcp_resilient.py.

No VICE server is needed: ``_http_post`` is replaced by a fake that records
every JSON-RPC request and replays scripted replies, and ``time.sleep`` is
replaced so retry back-off costs nothing. The C sources under vice/src/mcp
are parsed for the tool registry and handler parameter names, so these tests
fail when the server changes and the client does not.

Run from the repository root::

    python3 -m pytest tools/tests/test_resilient_client.py -q
"""

import inspect
import json
import re
from pathlib import Path

import pytest

from tools.resilience import vice_mcp_resilient as vmr
from tools.resilience.vice_mcp_resilient import (
    TOOL_SCHEMAS,
    ViceMCPClient,
    ViceMCPConnectionError,
    ViceMCPProtocolError,
    ViceMCPToolError,
)

MCP_SRC = Path(__file__).resolve().parents[2] / "vice" / "src" / "mcp"


# ---------------------------------------------------------------------------
# Fake transport
# ---------------------------------------------------------------------------

def ok(result=None):
    """A tools/call success reply carrying *result* as MCP text content."""
    text = json.dumps({"status": "ok"} if result is None else result)
    return {"result": {"content": [{"type": "text", "text": text}]}}


def rpc_error(code, message="error"):
    return {"error": {"code": code, "message": message}}


class FakeServer:
    """Stands in for ``_http_post``: records requests, replays replies.

    ``replies`` is consumed in order; each entry is a reply dict (merged into
    a JSON-RPC envelope) or an exception to raise. When it runs dry every
    request succeeds with ``ok()``.
    """

    def __init__(self):
        self.requests = []
        self.timeouts = []
        self.replies = []

    def __call__(self, url, payload, timeout):
        request = json.loads(payload)
        self.requests.append(request)
        self.timeouts.append(timeout)
        reply = self.replies.pop(0) if self.replies else ok()
        if isinstance(reply, BaseException):
            raise reply
        return 200, json.dumps({"jsonrpc": "2.0", "id": request["id"], **reply})

    def last_call(self):
        """(tool name, arguments) of the last tools/call request.

        Also checks every argument sent is declared in TOOL_SCHEMAS, so a
        method cannot send a parameter its schema entry does not know.
        """
        request = self.requests[-1]
        assert request["method"] == "tools/call"
        name = request["params"]["name"]
        args = request["params"]["arguments"]
        assert set(args) <= _schema_names(name), (
            f"{name} sent params missing from TOOL_SCHEMAS: "
            f"{sorted(set(args) - _schema_names(name))}"
        )
        return name, args


@pytest.fixture
def server(monkeypatch):
    fake = FakeServer()
    monkeypatch.setattr(vmr, "_http_post", fake)
    return fake


@pytest.fixture
def sleeps(monkeypatch):
    delays = []
    monkeypatch.setattr(vmr.time, "sleep", delays.append)
    return delays


@pytest.fixture
def client(server, sleeps, tmp_path):
    return ViceMCPClient(port=1, log_path=str(tmp_path / "reliability.jsonl"))


def _schema_names(tool):
    schema = TOOL_SCHEMAS.get(tool, {"required": [], "optional": []})
    return {name for name, _ in schema["required"] + schema["optional"]}


# ---------------------------------------------------------------------------
# C source parsing
# ---------------------------------------------------------------------------

def _registry():
    """{tool name: handler function} for the vice.* entries of tool_registry[]."""
    src = (MCP_SRC / "mcp_tools.c").read_text()
    table = src[src.index("tool_registry[] = {"):]
    table = table[: table.index("};")]
    # Some entries put the name alone on the line: { "vice.snapshot.save",
    tools = dict(re.findall(r'\{\s*"(vice\.[^"]+)",.*?(mcp_tool_\w+)\s*\}', table, re.S))
    assert tools, "no vice.* tools parsed from tool_registry[]"
    return tools


def _c_functions():
    """{name: body} for every top-level function in mcp_tools_*.c."""
    funcs = {}
    for path in MCP_SRC.glob("mcp_tools_*.c"):
        src = path.read_text()
        for m in re.finditer(r"^[A-Za-z][\w \t*]*?\b(\w+)\s*\([^;{)]*\)\s*\{", src, re.M):
            funcs[m.group(1)] = src[m.end(): src.find("\n}\n", m.end())]
    return funcs


# ponytail: keys a handler builds at run time are invisible to the regex, so
# they are listed by hand and not checked against the C source.
_RUNTIME_KEYS = {
    "vice.sid.set_state": {"voice0", "voice1", "voice2"},  # "voice%d"
    "vice.cia.set_state": {"cia1", "cia2", "cia1_registers", "cia2_registers"},  # key table
}


def _params_read(body):
    """Literal keys looked up on ``params``: cJSON_GetObjectItem(params, "x")
    or a helper such as chip_plan_byte(&plan, params, "x", ...)."""
    return set(re.findall(r'\bparams,\s*"([^"]+)"', body))


def _handler_params(funcs, tool, handler):
    """Top-level param names a handler reads, including via static helpers
    it hands ``params`` to (parse_joystick_value)."""
    body = funcs[handler]
    names = _params_read(body) | _RUNTIME_KEYS.get(tool, set())
    for helper in re.findall(r"\b(\w+)\(params\b", body):
        if helper in funcs and helper != handler:
            names |= _params_read(funcs[helper])
    return names


# ---------------------------------------------------------------------------
# Schemas and methods vs. the server (bugs 6, 7, 8)
# ---------------------------------------------------------------------------

def test_tool_schemas_cover_exactly_the_registered_tools():
    assert sorted(TOOL_SCHEMAS) == sorted(_registry())


def test_every_registered_tool_has_a_method_and_no_method_calls_an_unregistered_one():
    called = set(re.findall(r'self\.call\(\s*"(vice\.[^"]+)"', inspect.getsource(ViceMCPClient)))
    assert sorted(called) == sorted(_registry())


def test_tool_schema_params_match_the_c_handlers():
    funcs = _c_functions()
    mismatches = {}
    for tool, handler in _registry().items():
        ours, theirs = _schema_names(tool), _handler_params(funcs, tool, handler)
        if ours != theirs:
            mismatches[tool] = {
                "client_only": sorted(ours - theirs),
                "server_only": sorted(theirs - ours),
            }
    assert mismatches == {}


@pytest.mark.parametrize(
    "tool, params",
    [
        ("vice.memory.read", {"address": 0, "size": 1, "bank": 1}),
        ("vice.memory.read", {"address": "$0400", "size": 1, "encoding": "hex"}),
        ("vice.keyboard.key_press", {"key": 65}),
        ("vice.keyboard.key_release", {"key": 65}),
        ("vice.keyboard.type", {"text": "x", "petscii_upper": 0}),
        ("vice.joystick.set", {"direction": ["up", "left"]}),
        ("vice.joystick.tap", {"direction": ["down"], "duration_ms": 100}),
        ("vice.disk.attach", {"unit": 8, "path": "a.d64", "drive": 1}),
        ("vice.disk.detach", {"unit": 8, "drive": 1}),
        ("vice.cia.get_state", {"cia": 2}),
        ("vice.vicii.set_state", {"border_color": 0, "irq_raster_line": 300}),
        ("vice.sid.set_state", {"voice0": {"frequency": 1000}, "filter_mode_volume": 15}),
        ("vice.cia.set_state", {"cia1": {"port_a": 0xFF}, "cia2": {"ddr_a": 0x3F}}),
        ("vice.watch.add", {"address": 0xD020, "stop": False, "load": True}),
        ("vice.keyboard.chord", {"keys": ["LSHIFT", {"key": "A"}, {"row": 1, "col": 2}]}),
        ("vice.machine.config.set", {"resources": {"WarpMode": 1}}),
    ],
)
def test_server_accepted_params_pass_client_validation(tool, params):
    assert vmr.validate_params(tool, params) == []


def test_validation_rejects_a_missing_required_param(server, client):
    with pytest.raises(vmr.ViceMCPValidationError):
        client.call("vice.keyboard.petscii")
    assert server.requests == []


# ---------------------------------------------------------------------------
# Bug 1: execution_step(step_over=True) must send stepOver
# ---------------------------------------------------------------------------

def test_execution_step_over_sends_camel_case_step_over(server, client):
    client.execution_step(count=3, step_over=True)
    assert server.last_call() == ("vice.execution.step", {"count": 3, "stepOver": True})


def test_execution_step_defaults_send_nothing(server, client):
    client.execution_step()
    assert server.last_call() == ("vice.execution.step", {})


# ---------------------------------------------------------------------------
# Bug 2: memory_compare speaks the server's modes and params
# ---------------------------------------------------------------------------

def test_memory_compare_ranges(server, client):
    client.memory_compare(
        "ranges", range1_start=0x1000, range1_end="$10FF", range2_start=0x2000,
        max_differences=10,
    )
    assert server.last_call() == ("vice.memory.compare", {
        "mode": "ranges", "range1_start": 0x1000, "range1_end": "$10FF",
        "range2_start": 0x2000, "max_differences": 10,
    })


def test_memory_compare_snapshot(server, client):
    client.memory_compare("snapshot", snapshot_name="before", start=0x0400, end=0x07E7)
    assert server.last_call() == ("vice.memory.compare", {
        "mode": "snapshot", "snapshot_name": "before", "start": 0x0400, "end": 0x07E7,
    })


def test_memory_compare_rejects_the_old_param_names(client):
    with pytest.raises(TypeError):
        client.memory_compare("ranges", start1=0, end1=1, start2=2)


# ---------------------------------------------------------------------------
# Bug 3: sprite_set takes exactly the server's params
# ---------------------------------------------------------------------------

def test_sprite_set_sends_priority_foreground(server, client):
    client.sprite_set(
        2, enabled=True, x=300, y=100, color=7, multicolor=False,
        expand_x=True, expand_y=False, priority_foreground=False,
    )
    assert server.last_call() == ("vice.sprite.set", {
        "sprite": 2, "enabled": True, "x": 300, "y": 100, "color": 7,
        "multicolor": False, "expand_x": True, "expand_y": False,
        "priority_foreground": False,
    })


def test_sprite_set_sends_only_what_was_given(server, client):
    client.sprite_set(0, x=24)
    assert server.last_call() == ("vice.sprite.set", {"sprite": 0, "x": 24})


@pytest.mark.parametrize("bogus", ["priority", "multicolor0", "multicolor1", "pointer"])
def test_sprite_set_rejects_params_the_server_ignores(client, bogus):
    with pytest.raises(TypeError):
        client.sprite_set(0, **{bogus: 1})


# ---------------------------------------------------------------------------
# Bug 4: deterministic JSON-RPC errors fail at once; transport errors retry
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("code", [-32700, -32600, -32602])
def test_deterministic_protocol_errors_fail_without_retry_or_fallback(server, sleeps, client, code):
    server.replies = [rpc_error(code, "bad")]
    with pytest.raises(ViceMCPProtocolError) as excinfo:
        client.registers_get()
    assert excinfo.value.code == code
    assert len(server.requests) == 1
    assert sleeps == []
    failure = client.monitor.get_recent_failures(1)[0]
    assert failure["error_code"] == code
    assert (failure["retry_count"], failure["fallback_used"]) == (0, False)


def test_method_not_found_from_tools_call_falls_back_to_direct_dispatch(server, sleeps, client):
    server.replies = [rpc_error(-32601, "Method not found"), {"result": {"A": 1}}]
    assert client.registers_get() == {"A": 1}
    first, second = server.requests
    assert first["method"] == "tools/call"
    assert (second["method"], second["params"]) == ("vice.registers.get", {})
    assert sleeps == []
    assert client.monitor.get_stats()["failures"] == 0


def test_direct_dispatch_fallback_sends_the_same_arguments(server, client):
    server.replies = [rpc_error(-32601), {"result": {"status": "ok"}}]
    client.memory_read(0x0400, 8)
    assert server.requests[1]["params"] == {"address": 0x0400, "size": 8}


def test_method_not_found_on_both_paths_fails_without_back_off(server, sleeps, client):
    server.replies = [rpc_error(-32601), rpc_error(-32601)]
    with pytest.raises(ViceMCPProtocolError) as excinfo:
        client.ping()
    assert excinfo.value.code == -32601
    assert len(server.requests) == 2
    assert sleeps == []


def test_invalid_params_on_the_fallback_path_is_not_retried(server, sleeps, client):
    server.replies = [rpc_error(-32601), rpc_error(-32602)]
    with pytest.raises(ViceMCPProtocolError) as excinfo:
        client.ping()
    assert excinfo.value.code == -32602
    assert len(server.requests) == 2
    assert sleeps == []


def test_internal_error_still_falls_back_and_retries(server, sleeps, client):
    server.replies = [rpc_error(-32603)] * 3 + [{"result": {"status": "ok"}}]
    assert client.ping() == {"status": "ok"}
    assert [r["method"] for r in server.requests] == ["tools/call"] + ["vice.ping"] * 3
    assert sleeps == [1.0, 2.0]


def test_connection_errors_are_retried_with_back_off(server, sleeps, client):
    server.replies = [ConnectionRefusedError("refused"), ConnectionResetError("reset")]
    assert client.ping() == {"status": "ok"}
    assert len(server.requests) == 3
    assert sleeps == [0.5, 1.0]


def test_timeouts_are_retried_with_a_growing_timeout(server, sleeps, client):
    server.replies = [TimeoutError("timed out")] * 2
    assert client.ping() == {"status": "ok"}
    assert server.timeouts == [10.0, 15.0, 22.5]
    assert sleeps == [0.5, 1.0]


def test_connection_errors_exhaust_retries(server, sleeps, client):
    server.replies = [ConnectionRefusedError("refused")] * 4
    with pytest.raises(ViceMCPConnectionError):
        client.ping()
    assert len(server.requests) == 4
    assert sleeps == [0.5, 1.0, 2.0]


def test_tool_errors_fail_at_once_and_are_recorded(server, sleeps, client):
    server.replies = [rpc_error(-32001, "Emulator must be paused")]
    with pytest.raises(ViceMCPToolError):
        client.registers_get()
    assert len(server.requests) == 1
    failure = client.monitor.get_recent_failures(1)[0]
    assert (failure["error"], failure["error_code"]) == ("Emulator must be paused", -32001)


# ---------------------------------------------------------------------------
# Bug 5: checkpoint_add sends only the flags the caller set
# ---------------------------------------------------------------------------

def test_checkpoint_add_defaults_send_only_start(server, client):
    client.checkpoint_add(0x1000)
    assert server.last_call() == ("vice.checkpoint.add", {"start": 0x1000})


def test_checkpoint_add_load_alone_leaves_exec_to_the_server(server, client):
    client.checkpoint_add(0x1000, load=True)
    assert server.last_call() == ("vice.checkpoint.add", {"start": 0x1000, "load": True})


def test_checkpoint_add_explicit_exec_is_sent(server, client):
    client.checkpoint_add(0x1000, end=0x10FF, load=True, exec_=True)
    assert server.last_call() == ("vice.checkpoint.add", {
        "start": 0x1000, "end": 0x10FF, "load": True, "exec": True,
    })


def test_checkpoint_add_non_stopping_store_watch(server, client):
    client.checkpoint_add(0xD020, stop=False, store=True)
    assert server.last_call() == ("vice.checkpoint.add", {
        "start": 0xD020, "stop": False, "store": True,
    })


# ---------------------------------------------------------------------------
# Bug 6: methods for registered tools that had none
# ---------------------------------------------------------------------------

def test_frame_advance(server, client):
    client.frame_advance(5)
    assert server.last_call() == ("vice.frame.advance", {"frames": 5})
    client.frame_advance()
    assert server.last_call() == ("vice.frame.advance", {})


def test_keyboard_petscii_accepts_bytes(server, client):
    client.keyboard_petscii(b"RUN\r")
    assert server.last_call() == ("vice.keyboard.petscii", {"data": [82, 85, 78, 13]})


def test_joystick_tap(server, client):
    client.joystick_tap(port=1, direction=("up", "left"), fire=True, duration_frames=5)
    assert server.last_call() == ("vice.joystick.tap", {
        "port": 1, "direction": ["up", "left"], "fire": True, "duration_frames": 5,
    })
    client.joystick_tap(direction="down", duration_ms=100)
    assert server.last_call() == ("vice.joystick.tap", {"direction": "down", "duration_ms": 100})


def test_keyboard_chord(server, client):
    client.keyboard_chord(["LSHIFT", {"key": "A"}, {"row": 1, "col": 2}], hold_frames=4)
    assert server.last_call() == ("vice.keyboard.chord", {
        "keys": ["LSHIFT", {"key": "A"}, {"row": 1, "col": 2}], "hold_frames": 4,
    })
    client.keyboard_chord(["CBM", "SPACE"], hold_ms=200)
    assert server.last_call() == ("vice.keyboard.chord", {
        "keys": ["CBM", "SPACE"], "hold_ms": 200,
    })


def test_machine_config_get_and_set(server, client):
    client.machine_config_get()
    assert server.last_call() == ("vice.machine.config.get", {})
    client.machine_config_set({"WarpMode": 1, "Speed": 0})
    assert server.last_call() == ("vice.machine.config.set", {
        "resources": {"WarpMode": 1, "Speed": 0},
    })


# ---------------------------------------------------------------------------
# Bug 8: params the server accepts are reachable from the methods
# ---------------------------------------------------------------------------

def test_optional_params_reach_the_server(server, client):
    client.memory_read(0x0400, 16, encoding="hex")
    assert server.last_call() == ("vice.memory.read", {
        "address": 0x0400, "size": 16, "encoding": "hex",
    })
    client.disk_attach(8, "a.d64", drive=1)
    assert server.last_call() == ("vice.disk.attach", {"unit": 8, "path": "a.d64", "drive": 1})
    client.disk_detach(8, drive=1)
    assert server.last_call() == ("vice.disk.detach", {"unit": 8, "drive": 1})
    client.cia_get_state(cia=2)
    assert server.last_call() == ("vice.cia.get_state", {"cia": 2})
    client.watch_add(0xD020, stop=False)
    assert server.last_call() == ("vice.watch.add", {"address": 0xD020, "stop": False})
    client.vicii_set_state(border_color=0, background_color=6)
    assert server.last_call() == ("vice.vicii.set_state", {
        "border_color": 0, "background_color": 6,
    })
    client.sid_set_state(voice0={"frequency": 7493}, filter_mode_volume=15)
    assert server.last_call() == ("vice.sid.set_state", {
        "voice0": {"frequency": 7493}, "filter_mode_volume": 15,
    })


def test_defaults_send_no_optional_params(server, client):
    client.disk_attach(8, "a.d64")
    assert server.last_call() == ("vice.disk.attach", {"unit": 8, "path": "a.d64"})
    client.cia_get_state()
    assert server.last_call() == ("vice.cia.get_state", {})
    client.vicii_set_state()
    assert server.last_call() == ("vice.vicii.set_state", {})
