"""
Resilience layer for the VICE C64 Emulator MCP Server.

Provides a Python client with retry logic, fallback call paths, client-side
schema validation, and reliability monitoring for the VICE MCP server at
localhost:6510. It has one method per registered tool; any tool can also be
called by name with ``ViceMCPClient.call("vice.tool.name", **params)``.

Usage::

    # from the repository root
    from tools.resilience import ViceMCPClient, connect

    # Quick start
    vice = connect()
    print(vice.ping())
    regs = vice.registers_get()

    # Context manager
    with ViceMCPClient(port=6510) as vice:
        vice.execution_pause()
        mem = vice.memory_read(0xC000, 256)

    # With monitoring
    vice = connect()
    vice.memory_read(0x0400, 1000)
    stats = vice.monitor.get_stats()
    print(f"Failure rate: {stats['failure_rate']:.2%}")

Requires: Python 3.8+, requests (falls back to urllib if unavailable)
"""

from __future__ import annotations

import json
import logging
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Sequence,
    Tuple,
    Type,
    Union,
)

# ---------------------------------------------------------------------------
# HTTP backend: prefer requests, fall back to urllib
# ---------------------------------------------------------------------------
try:
    import requests as _requests_lib

    _HAS_REQUESTS = True
except ImportError:
    _requests_lib = None  # type: ignore[assignment]
    _HAS_REQUESTS = False
    import urllib.request
    import urllib.error

logger = logging.getLogger("vice_mcp_resilient")

# ---------------------------------------------------------------------------
# JSON-RPC / MCP error codes (mirrors mcp_tools.h)
# ---------------------------------------------------------------------------
JSONRPC_PARSE_ERROR = -32700
JSONRPC_INVALID_REQUEST = -32600
JSONRPC_METHOD_NOT_FOUND = -32601
JSONRPC_INVALID_PARAMS = -32602
JSONRPC_INTERNAL_ERROR = -32603

MCP_ERROR_NOT_IMPLEMENTED = -32000
MCP_ERROR_EMULATOR_RUNNING = -32001
MCP_ERROR_INVALID_ADDRESS = -32002
MCP_ERROR_INVALID_VALUE = -32003
MCP_ERROR_SNAPSHOT_FAILED = -32004

# Protocol-level error codes that trigger fallback to direct HTTP
_PROTOCOL_ERROR_CODES = frozenset(range(-32799, -32599))

# The same request gets the same answer: never retried or re-routed
_FATAL_ERROR_CODES = frozenset(
    {JSONRPC_PARSE_ERROR, JSONRPC_INVALID_REQUEST, JSONRPC_INVALID_PARAMS}
)


# ---------------------------------------------------------------------------
# Exceptions
# ---------------------------------------------------------------------------
class ViceMCPError(Exception):
    """Base exception for VICE MCP client errors."""

    def __init__(self, message: str, code: int = 0, data: Any = None) -> None:
        super().__init__(message)
        self.code = code
        self.data = data


class ViceMCPConnectionError(ViceMCPError):
    """Connection to VICE MCP server failed."""


class ViceMCPTimeoutError(ViceMCPError):
    """Request to VICE MCP server timed out."""


class ViceMCPProtocolError(ViceMCPError):
    """JSON-RPC protocol-level error from the server."""


class ViceMCPToolError(ViceMCPError):
    """Tool returned an error result."""


class ViceMCPValidationError(ViceMCPError):
    """Client-side parameter validation failed."""


# ---------------------------------------------------------------------------
# TOOL_SCHEMAS - The CORRECT schemas based on reading the C handler source.
#
# Each entry maps a tool name to:
#   required: list of (param_name, type_string) that MUST be present
#   optional: list of (param_name, type_string) that MAY be present
#
# Type strings: "number", "string", "boolean", "array", "object", "any"
#
# These reflect what the C handlers actually parse.
# ---------------------------------------------------------------------------
TOOL_SCHEMAS: Dict[str, Dict[str, Any]] = {
    "vice.ping": {
        "required": [],
        "optional": [],
    },
    "vice.execution.run": {
        "required": [],
        "optional": [],
    },
    "vice.execution.pause": {
        "required": [],
        "optional": [],
    },
    "vice.execution.step": {
        "required": [],
        "optional": [
            ("count", "number"),
            ("stepOver", "boolean"),
        ],
    },
    "vice.frame.advance": {
        "required": [],
        "optional": [
            ("frames", "number"),
        ],
    },
    "vice.registers.get": {
        "required": [],
        "optional": [],
    },
    "vice.registers.set": {
        "required": [
            ("register", "string"),
            ("value", "number"),
        ],
        "optional": [],
    },
    "vice.memory.read": {
        "required": [
            ("address", "any"),   # number or string (symbol / $hex)
            ("size", "number"),
        ],
        "optional": [
            ("bank", "any"),      # name or number
            ("encoding", "string"),
        ],
    },
    "vice.memory.write": {
        "required": [
            ("address", "any"),   # number or string
            ("data", "array"),
        ],
        "optional": [],
    },
    "vice.memory.banks": {
        "required": [],
        "optional": [],
    },
    "vice.memory.search": {
        "required": [
            ("start", "any"),
            ("end", "any"),
            ("pattern", "array"),
        ],
        "optional": [
            ("mask", "array"),
            ("max_results", "number"),
        ],
    },
    "vice.checkpoint.add": {
        "required": [
            ("start", "any"),
        ],
        "optional": [
            ("end", "any"),
            ("stop", "boolean"),
            ("load", "boolean"),
            ("store", "boolean"),
            ("exec", "boolean"),
        ],
    },
    "vice.checkpoint.delete": {
        "required": [
            ("checkpoint_num", "number"),
        ],
        "optional": [],
    },
    "vice.checkpoint.list": {
        "required": [],
        "optional": [],
    },
    "vice.checkpoint.toggle": {
        "required": [
            ("checkpoint_num", "number"),
            ("enabled", "boolean"),
        ],
        "optional": [],
    },
    "vice.checkpoint.set_condition": {
        "required": [
            ("checkpoint_num", "number"),
            ("condition", "string"),
        ],
        "optional": [],
    },
    "vice.checkpoint.set_ignore_count": {
        "required": [
            ("checkpoint_num", "number"),
            ("count", "number"),
        ],
        "optional": [],
    },
    "vice.sprite.get": {
        "required": [],
        "optional": [
            ("sprite", "number"),
        ],
    },
    "vice.sprite.set": {
        "required": [
            ("sprite", "number"),
        ],
        "optional": [
            ("enabled", "boolean"),
            ("x", "number"),
            ("y", "number"),
            ("color", "number"),
            ("multicolor", "boolean"),
            ("expand_x", "boolean"),
            ("expand_y", "boolean"),
            ("priority_foreground", "boolean"),
        ],
    },
    "vice.vicii.get_state": {
        "required": [],
        "optional": [],
    },
    "vice.vicii.set_state": {
        "required": [],
        "optional": [
            ("registers", "array"),
            ("border_color", "number"),
            ("background_color", "number"),
            ("background_color_1", "number"),
            ("background_color_2", "number"),
            ("background_color_3", "number"),
            ("control_1", "number"),
            ("control_2", "number"),
            ("memory_pointers", "number"),
            ("irq_raster_line", "number"),
            ("interrupt_enable", "number"),
        ],
    },
    "vice.sid.get_state": {
        "required": [],
        "optional": [],
    },
    "vice.sid.set_state": {
        "required": [],
        "optional": [
            ("registers", "array"),
            # each voiceN: frequency, pulse_width, control, attack_decay,
            # sustain_release
            ("voice0", "object"),
            ("voice1", "object"),
            ("voice2", "object"),
            ("filter_cutoff", "number"),
            ("filter_resonance", "number"),
            ("filter_mode_volume", "number"),
        ],
    },
    "vice.cia.get_state": {
        "required": [],
        "optional": [
            ("cia", "number"),
        ],
    },
    "vice.cia.set_state": {
        "required": [],
        "optional": [
            ("cia1_registers", "array"),
            ("cia2_registers", "array"),
            # port_a, port_b, ddr_a, ddr_b, timer_a_low, timer_a_high,
            # timer_b_low, timer_b_high, interrupt_control, control_a,
            # control_b
            ("cia1", "object"),
            ("cia2", "object"),
        ],
    },
    "vice.disk.attach": {
        "required": [
            ("unit", "number"),
            ("path", "string"),
        ],
        "optional": [
            ("drive", "number"),
        ],
    },
    "vice.disk.detach": {
        "required": [
            ("unit", "number"),
        ],
        "optional": [
            ("drive", "number"),
        ],
    },
    "vice.disk.list": {
        "required": [
            ("unit", "number"),
        ],
        "optional": [],
    },
    "vice.disk.read_sector": {
        "required": [
            ("unit", "number"),
            ("track", "number"),
            ("sector", "number"),
        ],
        "optional": [],
    },
    "vice.autostart": {
        "required": [
            ("path", "string"),
        ],
        "optional": [
            ("program", "string"),
            ("run", "boolean"),
            ("index", "number"),
        ],
    },
    "vice.machine.reset": {
        "required": [],
        "optional": [
            ("mode", "string"),
            ("run_after", "boolean"),
        ],
    },
    "vice.display.screenshot": {
        "required": [],
        "optional": [
            ("path", "string"),
            ("format", "string"),
            ("return_base64", "boolean"),
            ("display", "string"),
        ],
    },
    "vice.display.get_dimensions": {
        "required": [],
        "optional": [
            ("display", "string"),
        ],
    },
    "vice.keyboard.type": {
        "required": [
            ("text", "string"),
        ],
        "optional": [
            ("petscii_upper", "any"),   # boolean or number
        ],
    },
    "vice.keyboard.petscii": {
        "required": [
            ("data", "array"),
        ],
        "optional": [],
    },
    "vice.keyboard.key_press": {
        "required": [
            ("key", "any"),   # name or key code
        ],
        "optional": [
            ("modifiers", "array"),
            ("hold_frames", "number"),
            ("hold_ms", "number"),
        ],
    },
    "vice.keyboard.key_release": {
        "required": [
            ("key", "any"),   # name or key code
        ],
        "optional": [
            ("modifiers", "array"),
        ],
    },
    "vice.keyboard.restore": {
        "required": [],
        "optional": [
            ("pressed", "boolean"),
        ],
    },
    "vice.keyboard.matrix": {
        "required": [],
        "optional": [
            ("key", "string"),
            ("row", "number"),
            ("col", "number"),
            ("pressed", "boolean"),
            ("hold_frames", "number"),
            ("hold_ms", "number"),
        ],
    },
    "vice.keyboard.chord": {
        "required": [
            ("keys", "array"),
        ],
        "optional": [
            ("hold_frames", "number"),
            ("hold_ms", "number"),
        ],
    },
    "vice.joystick.set": {
        "required": [],
        "optional": [
            ("port", "number"),
            ("direction", "any"),   # string or array of strings
            ("fire", "boolean"),
        ],
    },
    "vice.joystick.tap": {
        "required": [],
        "optional": [
            ("port", "number"),
            ("direction", "any"),   # string or array of strings
            ("fire", "boolean"),
            ("duration_frames", "number"),
            ("duration_ms", "number"),
        ],
    },
    "vice.paddle.set": {
        "required": [("port", "number"), ("x", "number"), ("y", "number")],
        "optional": [],
    },
    "vice.disassemble": {
        "required": [
            ("address", "any"),
        ],
        "optional": [
            ("count", "number"),
            ("show_symbols", "boolean"),
        ],
    },
    "vice.symbols.load": {
        "required": [
            ("path", "string"),
        ],
        "optional": [
            ("format", "string"),
        ],
    },
    "vice.symbols.lookup": {
        "required": [],
        "optional": [
            ("name", "string"),
            ("address", "number"),
        ],
    },
    "vice.watch.add": {
        "required": [
            ("address", "any"),
        ],
        "optional": [
            ("size", "number"),
            ("type", "string"),
            ("load", "boolean"),    # alternative to type
            ("store", "boolean"),   # alternative to type
            ("stop", "boolean"),
            ("condition", "string"),
        ],
    },
    "vice.backtrace": {
        "required": [],
        "optional": [
            ("depth", "number"),
        ],
    },
    "vice.run_until": {
        "required": [],
        "optional": [
            ("address", "any"),
            ("cycles", "number"),
        ],
    },
    "vice.snapshot.save": {
        "required": [
            ("name", "string"),
        ],
        "optional": [
            ("description", "string"),
            ("include_roms", "boolean"),
            ("include_disks", "boolean"),
        ],
    },
    "vice.snapshot.load": {
        "required": [
            ("name", "string"),
        ],
        "optional": [],
    },
    "vice.snapshot.list": {
        "required": [],
        "optional": [],
    },
    "vice.cycles.stopwatch": {
        "required": [
            ("action", "string"),
        ],
        "optional": [],
    },
    "vice.memory.fill": {
        "required": [
            ("start", "any"),
            ("end", "any"),
            ("pattern", "array"),
        ],
        "optional": [],
    },
    "vice.memory.compare": {
        "required": [
            ("mode", "string"),
        ],
        "optional": [
            # mode "ranges"
            ("range1_start", "any"),
            ("range1_end", "any"),
            ("range2_start", "any"),
            # mode "snapshot"
            ("snapshot_name", "string"),
            ("start", "any"),
            ("end", "any"),
            # both
            ("max_differences", "number"),
        ],
    },
    "vice.checkpoint.group.create": {
        "required": [
            ("name", "string"),
        ],
        "optional": [
            ("checkpoint_ids", "array"),
        ],
    },
    "vice.checkpoint.group.add": {
        "required": [
            ("group", "string"),
            ("checkpoint_ids", "array"),
        ],
        "optional": [],
    },
    "vice.checkpoint.group.toggle": {
        "required": [
            ("group", "string"),
            ("enabled", "boolean"),
        ],
        "optional": [],
    },
    "vice.checkpoint.group.list": {
        "required": [],
        "optional": [],
    },
    "vice.machine.config.get": {
        "required": [],
        "optional": [],
    },
    "vice.machine.config.set": {
        "required": [
            ("resources", "object"),
        ],
        "optional": [],
    },
    "vice.sprite.inspect": {
        "required": [
            ("sprite_number", "number"),
        ],
        "optional": [
            ("format", "string"),
        ],
    },
}


def _validate_type(value: Any, type_str: str) -> bool:
    """Check that *value* is compatible with the declared schema type."""
    if type_str == "any":
        return True
    if type_str == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if type_str == "string":
        return isinstance(value, str)
    if type_str == "boolean":
        return isinstance(value, bool)
    if type_str == "array":
        return isinstance(value, (list, tuple))
    if type_str == "object":
        return isinstance(value, dict)
    return True


def validate_params(tool_name: str, params: Dict[str, Any]) -> List[str]:
    """Validate *params* against the known schema for *tool_name*.

    Returns a list of error strings (empty if valid).
    """
    schema = TOOL_SCHEMAS.get(tool_name)
    if schema is None:
        return []  # Unknown tool -- skip validation

    errors: List[str] = []

    # Check required params
    for param_name, param_type in schema["required"]:
        if param_name not in params:
            errors.append(
                f"Missing required parameter '{param_name}' for {tool_name}"
            )
        elif not _validate_type(params[param_name], param_type):
            errors.append(
                f"Parameter '{param_name}' for {tool_name} must be "
                f"{param_type}, got {type(params[param_name]).__name__}"
            )

    # Check optional params types (if provided)
    known_names = {n for n, _ in schema["required"]} | {
        n for n, _ in schema["optional"]
    }
    for param_name, param_type in schema["optional"]:
        if param_name in params and not _validate_type(
            params[param_name], param_type
        ):
            errors.append(
                f"Parameter '{param_name}' for {tool_name} must be "
                f"{param_type}, got {type(params[param_name]).__name__}"
            )

    # Warn about unexpected params (not an error, but logged)
    for key in params:
        if key not in known_names:
            logger.warning(
                "Unexpected parameter '%s' for %s (will be sent anyway)",
                key,
                tool_name,
            )

    return errors


# ---------------------------------------------------------------------------
# MCPReliabilityMonitor
# ---------------------------------------------------------------------------
class MCPReliabilityMonitor:
    """Records every MCP tool call and provides aggregate statistics.

    Log entries are appended as one JSON object per line to *log_path*
    (default ``~/.vice-mcp/reliability.jsonl``).
    """

    def __init__(self, log_path: Optional[str] = None) -> None:
        if log_path is None:
            base = Path.home() / ".vice-mcp"
            base.mkdir(parents=True, exist_ok=True)
            self.log_path = str(base / "reliability.jsonl")
        else:
            self.log_path = log_path
            parent = Path(log_path).parent
            parent.mkdir(parents=True, exist_ok=True)

        self._entries: List[Dict[str, Any]] = []

    # -- recording ----------------------------------------------------------

    def record(
        self,
        tool: str,
        duration_ms: float,
        success: bool,
        error: Optional[str] = None,
        error_code: Optional[int] = None,
        retry_count: int = 0,
        fallback_used: bool = False,
    ) -> None:
        entry: Dict[str, Any] = {
            "ts": datetime.now(timezone.utc).isoformat(),
            "tool": tool,
            "duration_ms": round(duration_ms, 2),
            "success": success,
            "error": error,
            "error_code": error_code,
            "retry_count": retry_count,
            "fallback_used": fallback_used,
        }
        self._entries.append(entry)
        try:
            with open(self.log_path, "a", encoding="utf-8") as fh:
                fh.write(json.dumps(entry, separators=(",", ":")) + "\n")
        except OSError as exc:
            logger.debug("Could not write reliability log: %s", exc)

    # -- statistics ---------------------------------------------------------

    def get_stats(self) -> Dict[str, Any]:
        """Return aggregate statistics across all recorded calls."""
        total = len(self._entries)
        if total == 0:
            return {
                "total_calls": 0,
                "failures": 0,
                "avg_latency_ms": 0.0,
                "failure_rate": 0.0,
                "failures_by_tool": {},
            }
        failures = [e for e in self._entries if not e["success"]]
        durations = [e["duration_ms"] for e in self._entries]
        failures_by_tool: Dict[str, int] = {}
        for e in failures:
            failures_by_tool[e["tool"]] = failures_by_tool.get(e["tool"], 0) + 1

        return {
            "total_calls": total,
            "failures": len(failures),
            "avg_latency_ms": round(sum(durations) / total, 2),
            "failure_rate": round(len(failures) / total, 4),
            "failures_by_tool": failures_by_tool,
        }

    def get_recent_failures(self, n: int = 10) -> List[Dict[str, Any]]:
        """Return the last *n* failure entries with full context."""
        failures = [e for e in self._entries if not e["success"]]
        return failures[-n:]


# ---------------------------------------------------------------------------
# Low-level HTTP helpers
# ---------------------------------------------------------------------------

def _post_requests(
    url: str,
    payload: bytes,
    timeout: float,
) -> Tuple[int, str]:
    """POST using the ``requests`` library."""
    assert _requests_lib is not None
    resp = _requests_lib.post(
        url,
        data=payload,
        headers={"Content-Type": "application/json"},
        timeout=timeout,
    )
    return resp.status_code, resp.text


def _post_urllib(
    url: str,
    payload: bytes,
    timeout: float,
) -> Tuple[int, str]:
    """POST using stdlib ``urllib.request``."""
    req = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    resp = urllib.request.urlopen(req, timeout=timeout)
    body = resp.read().decode("utf-8", errors="replace")
    return resp.status, body


def _http_post(url: str, payload: bytes, timeout: float) -> Tuple[int, str]:
    """Send an HTTP POST, preferring *requests* when available."""
    if _HAS_REQUESTS:
        return _post_requests(url, payload, timeout)
    return _post_urllib(url, payload, timeout)


# ---------------------------------------------------------------------------
# ViceMCPClient
# ---------------------------------------------------------------------------
class ViceMCPClient:
    """Resilient Python client for the VICE C64 Emulator MCP Server.

    Parameters
    ----------
    host : str
        Hostname of the MCP server (default ``"127.0.0.1"``).
    port : int
        Port of the MCP server (default ``6510``).
    max_retries : int
        Maximum number of retries per call (default ``3``).
    retry_delay : float
        Base delay in seconds between retries (default ``0.5``).
        Exponential back-off is applied: ``delay * 2**attempt``.
    timeout : float
        HTTP request timeout in seconds (default ``10.0``).
    validate : bool
        If ``True`` (default), validate parameters client-side before
        sending them to the server.
    monitor : MCPReliabilityMonitor or None
        If ``None`` a default monitor is created.
    log_path : str or None
        Path forwarded to the default ``MCPReliabilityMonitor``.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 6510,
        max_retries: int = 3,
        retry_delay: float = 0.5,
        timeout: float = 10.0,
        validate: bool = True,
        monitor: Optional[MCPReliabilityMonitor] = None,
        log_path: Optional[str] = None,
    ) -> None:
        self.host = host
        self.port = port
        self.max_retries = max_retries
        self.retry_delay = retry_delay
        self.timeout = timeout
        self.validate = validate
        self._base_url = f"http://{host}:{port}/mcp"
        self._id_counter = 0
        self.monitor: MCPReliabilityMonitor = (
            monitor if monitor is not None else MCPReliabilityMonitor(log_path)
        )

    # -- context manager ----------------------------------------------------

    def __enter__(self) -> "ViceMCPClient":
        return self

    def __exit__(
        self,
        exc_type: Optional[Type[BaseException]],
        exc_val: Optional[BaseException],
        exc_tb: Any,
    ) -> None:
        pass  # No persistent connection to close

    # -- internal helpers ---------------------------------------------------

    def _next_id(self) -> int:
        self._id_counter += 1
        return self._id_counter

    def _build_mcp_payload(
        self, tool_name: str, arguments: Dict[str, Any]
    ) -> bytes:
        """Build a JSON-RPC 2.0 payload wrapped in the MCP tools/call method."""
        obj = {
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": tool_name,
                "arguments": arguments,
            },
            "id": self._next_id(),
        }
        return json.dumps(obj, separators=(",", ":")).encode("utf-8")

    def _build_direct_payload(
        self, tool_name: str, arguments: Dict[str, Any]
    ) -> bytes:
        """Build a JSON-RPC 2.0 payload with the tool name as the method."""
        obj = {
            "jsonrpc": "2.0",
            "method": tool_name,
            "params": arguments,
            "id": self._next_id(),
        }
        return json.dumps(obj, separators=(",", ":")).encode("utf-8")

    @staticmethod
    def _parse_response(body: str) -> Dict[str, Any]:
        """Parse a JSON-RPC response body into a dict."""
        try:
            data = json.loads(body)
        except json.JSONDecodeError as exc:
            raise ViceMCPProtocolError(
                f"Invalid JSON in response: {exc}", code=JSONRPC_PARSE_ERROR
            ) from exc
        if not isinstance(data, dict):
            raise ViceMCPProtocolError(
                "Response is not a JSON object", code=JSONRPC_INVALID_REQUEST
            )
        return data

    @staticmethod
    def _unwrap_result(data: Dict[str, Any]) -> Any:
        """Extract the result from a JSON-RPC response, raising on error."""
        if "error" in data:
            err = data["error"]
            code = err.get("code", -1) if isinstance(err, dict) else -1
            message = err.get("message", str(err)) if isinstance(err, dict) else str(err)
            err_data = err.get("data") if isinstance(err, dict) else None
            if code in _PROTOCOL_ERROR_CODES:
                raise ViceMCPProtocolError(message, code=code, data=err_data)
            raise ViceMCPToolError(message, code=code, data=err_data)
        # MCP tools/call wraps the tool result inside result.content
        result = data.get("result")
        if isinstance(result, dict) and "content" in result:
            content = result["content"]
            # MCP content is an array of content blocks
            if isinstance(content, list) and len(content) == 1:
                block = content[0]
                if isinstance(block, dict) and block.get("type") == "text":
                    text = block.get("text", "")
                    try:
                        return json.loads(text)
                    except (json.JSONDecodeError, TypeError):
                        return text
            return content
        return result

    # -- core call with retry -----------------------------------------------

    def _call_raw(
        self,
        tool_name: str,
        arguments: Dict[str, Any],
    ) -> Any:
        """Execute a tool call with retry and fallback logic.

        1. Try MCP protocol path (tools/call wrapper).
        2. On protocol-level error (-32xxx), retry via direct HTTP.
           Parse error, invalid request and invalid params fail at once;
           method not found fails at once on the direct path.
        3. On connection error, retry with exponential back-off.
        4. On timeout, retry with increased timeout.
        """
        start = time.monotonic()
        last_error: Optional[Exception] = None
        fallback_used = False
        attempts = 0

        for attempt in range(self.max_retries + 1):
            attempts = attempt + 1
            current_timeout = self.timeout * (1.5 ** attempt)

            # Choose payload: first attempt uses MCP wrapper, retries on
            # protocol error use direct path
            if fallback_used:
                payload = self._build_direct_payload(tool_name, arguments)
            else:
                payload = self._build_mcp_payload(tool_name, arguments)

            try:
                status_code, body = _http_post(
                    self._base_url, payload, current_timeout
                )
                data = self._parse_response(body)
                result = self._unwrap_result(data)

                # Success
                elapsed = (time.monotonic() - start) * 1000
                self.monitor.record(
                    tool=tool_name,
                    duration_ms=elapsed,
                    success=True,
                    retry_count=attempt,
                    fallback_used=fallback_used,
                )
                return result

            except ViceMCPProtocolError as exc:
                last_error = exc
                logger.info(
                    "Protocol error on %s (attempt %d/%d, code=%d): %s",
                    tool_name,
                    attempt + 1,
                    self.max_retries + 1,
                    exc.code,
                    exc,
                )
                if exc.code in _FATAL_ERROR_CODES or (
                    fallback_used and exc.code == JSONRPC_METHOD_NOT_FOUND
                ):
                    break
                # Switch to direct fallback path on next retry
                if not fallback_used:
                    fallback_used = True
                    # Immediate retry with fallback, no delay
                    continue
                # Already on fallback, apply back-off
                if attempt < self.max_retries:
                    time.sleep(self.retry_delay * (2 ** attempt))

            except ViceMCPToolError as exc:
                # Tool-level errors are not retriable -- propagate immediately
                elapsed = (time.monotonic() - start) * 1000
                self.monitor.record(
                    tool=tool_name,
                    duration_ms=elapsed,
                    success=False,
                    error=str(exc),
                    error_code=exc.code,
                    retry_count=attempt,
                    fallback_used=fallback_used,
                )
                raise

            except (OSError, ConnectionError) as exc:
                last_error = exc
                logger.info(
                    "Connection error on %s (attempt %d/%d): %s",
                    tool_name,
                    attempt + 1,
                    self.max_retries + 1,
                    exc,
                )
                if attempt < self.max_retries:
                    time.sleep(self.retry_delay * (2 ** attempt))

            except Exception as exc:
                # Catch requests.exceptions.* or urllib errors
                exc_name = type(exc).__name__
                is_timeout = "timeout" in exc_name.lower() or "Timeout" in str(exc)
                is_connection = "connect" in exc_name.lower() or "Connect" in str(exc)

                last_error = exc
                logger.info(
                    "%s on %s (attempt %d/%d): %s",
                    exc_name,
                    tool_name,
                    attempt + 1,
                    self.max_retries + 1,
                    exc,
                )

                if is_timeout and attempt < self.max_retries:
                    # Retry with increased timeout (already handled by current_timeout)
                    time.sleep(self.retry_delay * (2 ** attempt))
                elif is_connection and attempt < self.max_retries:
                    time.sleep(self.retry_delay * (2 ** attempt))
                elif attempt < self.max_retries:
                    time.sleep(self.retry_delay * (2 ** attempt))
                else:
                    break

        # All retries exhausted
        elapsed = (time.monotonic() - start) * 1000
        error_msg = str(last_error) if last_error else "Unknown error"
        error_code = getattr(last_error, "code", None)
        self.monitor.record(
            tool=tool_name,
            duration_ms=elapsed,
            success=False,
            error=error_msg,
            error_code=error_code,
            retry_count=attempts - 1,
            fallback_used=fallback_used,
        )

        if isinstance(last_error, ViceMCPError):
            raise last_error
        if isinstance(last_error, OSError):
            raise ViceMCPConnectionError(
                f"Failed to connect after {attempts} attempts: {last_error}"
            ) from last_error
        raise ViceMCPError(
            f"Call to {tool_name} failed after {attempts} attempts: {error_msg}"
        )

    def call(self, tool_name: str, **kwargs: Any) -> Any:
        """Call a VICE MCP tool by its full dotted name.

        Parameters are validated client-side (if ``self.validate`` is True)
        before being sent to the server.

        Raises
        ------
        ViceMCPValidationError
            If client-side validation fails.
        ViceMCPToolError
            If the server returns a tool-level error.
        ViceMCPConnectionError
            If all connection attempts fail or time out.
        """
        if self.validate:
            errors = validate_params(tool_name, kwargs)
            if errors:
                raise ViceMCPValidationError(
                    "; ".join(errors), code=JSONRPC_INVALID_PARAMS
                )
        return self._call_raw(tool_name, kwargs)

    # =====================================================================
    # Convenience methods - one per tool, snake_case
    # =====================================================================

    # -- Meta / Connectivity ------------------------------------------------

    def ping(self) -> Any:
        """Check if VICE is responding."""
        return self.call("vice.ping")

    # -- Execution Control --------------------------------------------------

    def execution_run(self) -> Any:
        """Resume execution."""
        return self.call("vice.execution.run")

    def execution_pause(self) -> Any:
        """Pause execution."""
        return self.call("vice.execution.pause")

    def execution_step(
        self,
        count: int = 1,
        step_over: bool = False,
    ) -> Any:
        """Step one or more instructions.

        Parameters
        ----------
        count : int
            Number of instructions to step (default 1).
        step_over : bool
            If True, step over JSR calls (default False).
        """
        args: Dict[str, Any] = {}
        if count != 1:
            args["count"] = count
        if step_over:
            args["stepOver"] = step_over
        return self.call("vice.execution.step", **args)

    def frame_advance(self, frames: Optional[int] = None) -> Any:
        """Run whole frames from a stopped machine, then stop again.

        Parameters
        ----------
        frames : int, optional
            Frames to run, 1-1000 (server default 1).
        """
        args: Dict[str, Any] = {}
        if frames is not None:
            args["frames"] = frames
        return self.call("vice.frame.advance", **args)

    # -- Registers ----------------------------------------------------------

    def registers_get(self) -> Any:
        """Get CPU registers."""
        return self.call("vice.registers.get")

    def registers_set(self, register: str, value: int) -> Any:
        """Set a CPU register value.

        Parameters
        ----------
        register : str
            Register name, upper case: "PC", "A", "X", "Y", "SP", or a
            flag "N", "V", "B", "D", "I", "Z", "C".
        value : int
            New value for the register.
        """
        return self.call("vice.registers.set", register=register, value=value)

    # -- Memory -------------------------------------------------------------

    def memory_read(
        self,
        address: Union[int, str],
        size: int,
        bank: Optional[Union[str, int]] = None,
        encoding: Optional[str] = None,
    ) -> Any:
        """Read a range of memory bytes.

        Parameters
        ----------
        address : int or str
            Start address (number, "$hex", or symbol name).
        size : int
            Number of bytes to read.
        bank : str or int, optional
            Memory bank name (e.g. "cpu", "ram", "rom", "io") or number.
        encoding : str, optional
            "array" (default) or "hex".
        """
        args: Dict[str, Any] = {"address": address, "size": size}
        if bank is not None:
            args["bank"] = bank
        if encoding is not None:
            args["encoding"] = encoding
        return self.call("vice.memory.read", **args)

    def memory_write(
        self,
        address: Union[int, str],
        data: Sequence[int],
    ) -> Any:
        """Write bytes to memory.

        Parameters
        ----------
        address : int or str
            Start address.
        data : list of int
            Bytes to write.
        """
        return self.call("vice.memory.write", address=address, data=list(data))

    def memory_banks(self) -> Any:
        """List available memory banks."""
        return self.call("vice.memory.banks")

    def memory_search(
        self,
        start: Union[int, str],
        end: Union[int, str],
        pattern: Sequence[int],
        mask: Optional[Sequence[int]] = None,
        max_results: Optional[int] = None,
    ) -> Any:
        """Search for byte patterns in memory.

        Parameters
        ----------
        start : int or str
            Start address of search range.
        end : int or str
            End address of search range.
        pattern : list of int
            Byte pattern to search for.
        mask : list of int, optional
            Bitmask for wildcard matching (0xFF = exact match).
        max_results : int, optional
            Maximum number of matches to return.
        """
        args: Dict[str, Any] = {
            "start": start,
            "end": end,
            "pattern": list(pattern),
        }
        if mask is not None:
            args["mask"] = list(mask)
        if max_results is not None:
            args["max_results"] = max_results
        return self.call("vice.memory.search", **args)

    def memory_fill(
        self,
        start: Union[int, str],
        end: Union[int, str],
        pattern: Sequence[int],
    ) -> Any:
        """Fill a memory range with a repeating pattern.

        Parameters
        ----------
        start : int or str
            Start address.
        end : int or str
            End address (inclusive).
        pattern : list of int
            Byte pattern to repeat.
        """
        return self.call(
            "vice.memory.fill", start=start, end=end, pattern=list(pattern)
        )

    def memory_compare(
        self,
        mode: str,
        range1_start: Optional[Union[int, str]] = None,
        range1_end: Optional[Union[int, str]] = None,
        range2_start: Optional[Union[int, str]] = None,
        snapshot_name: Optional[str] = None,
        start: Optional[Union[int, str]] = None,
        end: Optional[Union[int, str]] = None,
        max_differences: Optional[int] = None,
    ) -> Any:
        """Compare two memory ranges, or memory against a saved snapshot.

        Parameters
        ----------
        mode : str
            "ranges" (needs range1_start, range1_end, range2_start) or
            "snapshot" (needs snapshot_name, start, end).
        range1_start, range1_end : int or str, optional
            First range, inclusive.
        range2_start : int or str, optional
            Start of the second range; it is as long as the first.
        snapshot_name : str, optional
            Name of a snapshot saved with snapshot_save.
        start, end : int or str, optional
            Range to compare against the snapshot, inclusive.
        max_differences : int, optional
            Differences to list (server default 100, clamped to 1-10000).
        """
        args: Dict[str, Any] = {"mode": mode}
        for key, value in (
            ("range1_start", range1_start),
            ("range1_end", range1_end),
            ("range2_start", range2_start),
            ("snapshot_name", snapshot_name),
            ("start", start),
            ("end", end),
            ("max_differences", max_differences),
        ):
            if value is not None:
                args[key] = value
        return self.call("vice.memory.compare", **args)

    # -- Checkpoints / Breakpoints ------------------------------------------

    def checkpoint_add(
        self,
        start: Union[int, str],
        end: Optional[Union[int, str]] = None,
        stop: bool = True,
        load: bool = False,
        store: bool = False,
        exec_: Optional[bool] = None,
    ) -> Any:
        """Add a checkpoint (breakpoint / watchpoint).

        Parameters
        ----------
        start : int or str
            Start address.
        end : int or str, optional
            End address (defaults to start).
        stop : bool
            Stop execution when hit (default True).
        load : bool
            Trigger on memory load (default False).
        store : bool
            Trigger on memory store (default False).
        exec_ : bool, optional
            Trigger on execution. Unset leaves it to the server, which
            makes an exec checkpoint only when neither load nor store is
            set; pass True with load/store for a combined checkpoint.
        """
        args: Dict[str, Any] = {"start": start}
        if end is not None:
            args["end"] = end
        if not stop:
            args["stop"] = stop
        if load:
            args["load"] = load
        if store:
            args["store"] = store
        if exec_ is not None:
            args["exec"] = exec_
        return self.call("vice.checkpoint.add", **args)

    def checkpoint_delete(self, checkpoint_num: int) -> Any:
        """Delete a checkpoint by its number.

        Parameters
        ----------
        checkpoint_num : int
            Checkpoint ID to delete.
        """
        return self.call(
            "vice.checkpoint.delete", checkpoint_num=checkpoint_num
        )

    def checkpoint_list(self) -> Any:
        """List all checkpoints."""
        return self.call("vice.checkpoint.list")

    def checkpoint_toggle(self, checkpoint_num: int, enabled: bool) -> Any:
        """Enable or disable a checkpoint.

        Parameters
        ----------
        checkpoint_num : int
            Checkpoint ID.
        enabled : bool
            True to enable, False to disable.
        """
        return self.call(
            "vice.checkpoint.toggle",
            checkpoint_num=checkpoint_num,
            enabled=enabled,
        )

    def checkpoint_set_condition(
        self, checkpoint_num: int, condition: str
    ) -> Any:
        """Set a condition expression on a checkpoint.

        Parameters
        ----------
        checkpoint_num : int
            Checkpoint ID.
        condition : str
            Condition expression (e.g. "A == $ff").
        """
        return self.call(
            "vice.checkpoint.set_condition",
            checkpoint_num=checkpoint_num,
            condition=condition,
        )

    def checkpoint_set_ignore_count(
        self, checkpoint_num: int, count: int
    ) -> Any:
        """Set how many times a checkpoint is ignored before stopping.

        Parameters
        ----------
        checkpoint_num : int
            Checkpoint ID.
        count : int
            Number of hits to ignore.
        """
        return self.call(
            "vice.checkpoint.set_ignore_count",
            checkpoint_num=checkpoint_num,
            count=count,
        )

    def checkpoint_group_create(
        self,
        name: str,
        checkpoint_ids: Optional[Sequence[int]] = None,
    ) -> Any:
        """Create a named checkpoint group.

        Parameters
        ----------
        name : str
            Group name.
        checkpoint_ids : list of int, optional
            Initial checkpoint IDs to add.
        """
        args: Dict[str, Any] = {"name": name}
        if checkpoint_ids is not None:
            args["checkpoint_ids"] = list(checkpoint_ids)
        return self.call("vice.checkpoint.group.create", **args)

    def checkpoint_group_add(
        self, group: str, checkpoint_ids: Sequence[int]
    ) -> Any:
        """Add checkpoints to an existing group.

        Parameters
        ----------
        group : str
            Group name.
        checkpoint_ids : list of int
            Checkpoint IDs to add to the group.
        """
        return self.call(
            "vice.checkpoint.group.add",
            group=group,
            checkpoint_ids=list(checkpoint_ids),
        )

    def checkpoint_group_toggle(self, group: str, enabled: bool) -> Any:
        """Enable or disable all checkpoints in a group.

        Parameters
        ----------
        group : str
            Group name.
        enabled : bool
            True to enable, False to disable.
        """
        return self.call(
            "vice.checkpoint.group.toggle", group=group, enabled=enabled
        )

    def checkpoint_group_list(self) -> Any:
        """List all checkpoint groups."""
        return self.call("vice.checkpoint.group.list")

    # -- Sprites ------------------------------------------------------------

    def sprite_get(self, sprite: Optional[int] = None) -> Any:
        """Get sprite state.

        Parameters
        ----------
        sprite : int, optional
            Sprite number (0-7). If None, returns all sprites.
        """
        args: Dict[str, Any] = {}
        if sprite is not None:
            args["sprite"] = sprite
        return self.call("vice.sprite.get", **args)

    def sprite_set(
        self,
        sprite: int,
        enabled: Optional[bool] = None,
        x: Optional[int] = None,
        y: Optional[int] = None,
        color: Optional[int] = None,
        multicolor: Optional[bool] = None,
        expand_x: Optional[bool] = None,
        expand_y: Optional[bool] = None,
        priority_foreground: Optional[bool] = None,
    ) -> Any:
        """Set sprite properties; unset ones keep their current value.

        Parameters
        ----------
        sprite : int
            Sprite number (0-7).
        enabled, multicolor, expand_x, expand_y : bool, optional
            Per-sprite VIC-II register bits.
        x : int, optional
            X position (0-511).
        y : int, optional
            Y position (0-255).
        color : int, optional
            Sprite color (0-15; 0-255 on the DTV).
        priority_foreground : bool, optional
            True draws the sprite in front of the background.
        """
        args: Dict[str, Any] = {"sprite": sprite}
        for key, value in (
            ("enabled", enabled),
            ("x", x),
            ("y", y),
            ("color", color),
            ("multicolor", multicolor),
            ("expand_x", expand_x),
            ("expand_y", expand_y),
            ("priority_foreground", priority_foreground),
        ):
            if value is not None:
                args[key] = value
        return self.call("vice.sprite.set", **args)

    def sprite_inspect(
        self,
        sprite_number: int,
        format: Optional[str] = None,
    ) -> Any:
        """Visual representation of sprite bitmap data.

        Reads sprite pointer, data, and multicolor settings to render
        ASCII art or binary.

        Parameters
        ----------
        sprite_number : int
            Sprite number (0-7).
        format : str, optional
            Output format: "ascii" (default) or "binary". "png_base64" is
            accepted but the server returns a placeholder string.
        """
        args: Dict[str, Any] = {"sprite_number": sprite_number}
        if format is not None:
            args["format"] = format
        return self.call("vice.sprite.inspect", **args)

    # -- Chip State ---------------------------------------------------------

    def vicii_get_state(self) -> Any:
        """Get VIC-II chip internal state."""
        return self.call("vice.vicii.get_state")

    def vicii_set_state(
        self,
        registers: Optional[Sequence[Dict[str, int]]] = None,
        **named: int,
    ) -> Any:
        """Set VIC-II registers.

        Parameters
        ----------
        registers : list of dict, optional
            Each dict has ``offset`` and ``value`` keys.
        **named
            border_color, background_color, background_color_1..3,
            control_1, control_2, memory_pointers, irq_raster_line,
            interrupt_enable.
        """
        args: Dict[str, Any] = dict(named)
        if registers is not None:
            args["registers"] = list(registers)
        return self.call("vice.vicii.set_state", **args)

    def sid_get_state(self) -> Any:
        """Get SID chip state (voices, filter)."""
        return self.call("vice.sid.get_state")

    def sid_set_state(
        self,
        registers: Optional[Sequence[Dict[str, int]]] = None,
        **named: Any,
    ) -> Any:
        """Set SID registers.

        Parameters
        ----------
        registers : list of dict, optional
            Each dict has ``offset`` and ``value`` keys.
        **named
            voice0, voice1, voice2 (dicts of frequency, pulse_width,
            control, attack_decay, sustain_release), filter_cutoff,
            filter_resonance, filter_mode_volume.
        """
        args: Dict[str, Any] = dict(named)
        if registers is not None:
            args["registers"] = list(registers)
        return self.call("vice.sid.set_state", **args)

    def cia_get_state(self, cia: Optional[int] = None) -> Any:
        """Get CIA chip state (timers, ports).

        Parameters
        ----------
        cia : int, optional
            1 or 2; both when unset.
        """
        args: Dict[str, Any] = {}
        if cia is not None:
            args["cia"] = cia
        return self.call("vice.cia.get_state", **args)

    def cia_set_state(self, **kwargs: Any) -> Any:
        """Set CIA registers.

        Parameters
        ----------
        **kwargs
            ``cia1_registers`` and/or ``cia2_registers``, each a list of
            dicts with ``offset`` and ``value`` keys; and/or ``cia1`` /
            ``cia2`` dicts of port_a, port_b, ddr_a, ddr_b, timer_a_low,
            timer_a_high, timer_b_low, timer_b_high, interrupt_control,
            control_a, control_b.
        """
        return self.call("vice.cia.set_state", **kwargs)

    # -- Disk Management ----------------------------------------------------

    def disk_attach(
        self, unit: int, path: str, drive: Optional[int] = None
    ) -> Any:
        """Attach a disk image to a drive unit.

        Parameters
        ----------
        unit : int
            Drive unit number (8-11).
        path : str
            Path to the disk image file.
        drive : int, optional
            Drive 0 (default) or 1 of a dual-drive unit.
        """
        args: Dict[str, Any] = {"unit": unit, "path": path}
        if drive is not None:
            args["drive"] = drive
        return self.call("vice.disk.attach", **args)

    def disk_detach(self, unit: int, drive: Optional[int] = None) -> Any:
        """Detach disk image from a drive unit.

        Parameters
        ----------
        unit : int
            Drive unit number (8-11).
        drive : int, optional
            Drive 0 (default) or 1 of a dual-drive unit.
        """
        args: Dict[str, Any] = {"unit": unit}
        if drive is not None:
            args["drive"] = drive
        return self.call("vice.disk.detach", **args)

    def disk_list(self, unit: int) -> Any:
        """List directory contents of a disk.

        Parameters
        ----------
        unit : int
            Drive unit number (8-11).
        """
        return self.call("vice.disk.list", unit=unit)

    def disk_read_sector(self, unit: int, track: int, sector: int) -> Any:
        """Read raw sector data from a disk.

        Parameters
        ----------
        unit : int
            Drive unit number (8-11).
        track : int
            Track number.
        sector : int
            Sector number.
        """
        return self.call(
            "vice.disk.read_sector", unit=unit, track=track, sector=sector
        )

    # -- Autostart ----------------------------------------------------------

    def autostart(
        self,
        path: str,
        program: Optional[str] = None,
        run: bool = True,
        index: Optional[int] = None,
    ) -> Any:
        """Autostart a PRG or disk image.

        Parameters
        ----------
        path : str
            Path to the file to autostart.
        program : str, optional
            Program name on the disk to load.
        run : bool
            Whether to RUN after loading (default True).
        index : int, optional
            File index on multi-file images.
        """
        args: Dict[str, Any] = {"path": path}
        if program is not None:
            args["program"] = program
        if not run:
            args["run"] = run
        if index is not None:
            args["index"] = index
        return self.call("vice.autostart", **args)

    # -- Machine Control ----------------------------------------------------

    def machine_reset(
        self,
        mode: Optional[str] = None,
        run_after: Optional[bool] = None,
    ) -> Any:
        """Reset the machine.

        Parameters
        ----------
        mode : str, optional
            Reset mode: "soft" (default) or "hard"/"power".
        run_after : bool, optional
            Whether to resume execution after reset (default True).
        """
        args: Dict[str, Any] = {}
        if mode is not None:
            args["mode"] = mode
        if run_after is not None:
            args["run_after"] = run_after
        return self.call("vice.machine.reset", **args)

    def machine_config_get(self) -> Any:
        """Get machine type, chips, memory map and settable resources."""
        return self.call("vice.machine.config.get")

    def machine_config_set(self, resources: Dict[str, int]) -> Any:
        """Set whitelisted machine resources.

        Parameters
        ----------
        resources : dict
            Resource name to integer value, e.g. ``{"WarpMode": 1}``.
        """
        return self.call("vice.machine.config.set", resources=dict(resources))

    # -- Display ------------------------------------------------------------

    def display_screenshot(
        self,
        path: Optional[str] = None,
        format: Optional[str] = None,
        return_base64: bool = False,
        display: Optional[str] = None,
    ) -> Any:
        """Capture a screenshot.

        Parameters
        ----------
        path : str, optional
            File path to save the screenshot.
        format : str, optional
            Image format (e.g. "png", "bmp").
        return_base64 : bool
            If True, return the image as base64 instead of saving to file.
        display : str, optional
            "vicii" (40-column VIC-II) or "vdc" (x128's 80-column VDC).
            Default: the machine's main screen, the VIC-II on x128.
        """
        args: Dict[str, Any] = {}
        if path is not None:
            args["path"] = path
        if format is not None:
            args["format"] = format
        if return_base64:
            args["return_base64"] = return_base64
        if display is not None:
            args["display"] = display
        return self.call("vice.display.screenshot", **args)

    def display_get_dimensions(self, display: Optional[str] = None) -> Any:
        """Get display dimensions.

        Parameters
        ----------
        display : str, optional
            "vicii" or "vdc", as for display_screenshot.
        """
        args: Dict[str, Any] = {}
        if display is not None:
            args["display"] = display
        return self.call("vice.display.get_dimensions", **args)

    # -- Keyboard Input -----------------------------------------------------

    def keyboard_type(
        self,
        text: str,
        petscii_upper: bool = True,
    ) -> Any:
        """Type text into the emulator.

        Parameters
        ----------
        text : str
            Text to type.
        petscii_upper : bool
            If True (default), uppercase ASCII maps to uppercase PETSCII.
        """
        args: Dict[str, Any] = {"text": text}
        if not petscii_upper:
            args["petscii_upper"] = petscii_upper
        return self.call("vice.keyboard.type", **args)

    def keyboard_petscii(self, data: Sequence[int]) -> Any:
        """Feed exact PETSCII bytes into the KERNAL keyboard buffer.

        Parameters
        ----------
        data : bytes or list of int
            1-4096 byte values, each 1-255 (e.g. ``b"RUN\\r"``).
        """
        return self.call("vice.keyboard.petscii", data=list(data))

    def keyboard_key_press(
        self,
        key: Union[str, int],
        modifiers: Optional[Sequence[str]] = None,
        hold_frames: Optional[int] = None,
        hold_ms: Optional[int] = None,
    ) -> Any:
        """Press a specific key.

        Parameters
        ----------
        key : str or int
            Key name (e.g. "Return", "Space", "a") or key code.
        modifiers : list of str, optional
            Modifier keys (e.g. ["shift", "control"]).
        hold_frames : int, optional
            Number of frames to hold the key before auto-release.
        hold_ms : int, optional
            Milliseconds to hold the key before auto-release.
        """
        args: Dict[str, Any] = {"key": key}
        if modifiers is not None:
            args["modifiers"] = list(modifiers)
        if hold_frames is not None:
            args["hold_frames"] = hold_frames
        if hold_ms is not None:
            args["hold_ms"] = hold_ms
        return self.call("vice.keyboard.key_press", **args)

    def keyboard_key_release(
        self,
        key: Union[str, int],
        modifiers: Optional[Sequence[str]] = None,
    ) -> Any:
        """Release a specific key.

        Parameters
        ----------
        key : str
            Key name.
        modifiers : list of str, optional
            Modifier keys.
        """
        args: Dict[str, Any] = {"key": key}
        if modifiers is not None:
            args["modifiers"] = list(modifiers)
        return self.call("vice.keyboard.key_release", **args)

    def keyboard_restore(self, pressed: bool = True) -> Any:
        """Press or release the RESTORE key (triggers NMI).

        Parameters
        ----------
        pressed : bool
            True to press, False to release (default True).
        """
        args: Dict[str, Any] = {}
        if not pressed:
            args["pressed"] = pressed
        return self.call("vice.keyboard.restore", **args)

    def keyboard_matrix(
        self,
        key: Optional[str] = None,
        row: Optional[int] = None,
        col: Optional[int] = None,
        pressed: bool = True,
        hold_frames: Optional[int] = None,
        hold_ms: Optional[int] = None,
    ) -> Any:
        """Direct keyboard matrix control.

        Use this for games that scan the keyboard matrix directly rather
        than using the keyboard buffer.

        Parameters
        ----------
        key : str, optional
            Named key (alternative to row/col).
        row : int, optional
            Keyboard matrix row.
        col : int, optional
            Keyboard matrix column.
        pressed : bool
            True to press, False to release (default True).
        hold_frames : int, optional
            Frames to hold before auto-release.
        hold_ms : int, optional
            Milliseconds to hold before auto-release.
        """
        args: Dict[str, Any] = {}
        if key is not None:
            args["key"] = key
        if row is not None:
            args["row"] = row
        if col is not None:
            args["col"] = col
        if not pressed:
            args["pressed"] = pressed
        if hold_frames is not None:
            args["hold_frames"] = hold_frames
        if hold_ms is not None:
            args["hold_ms"] = hold_ms
        return self.call("vice.keyboard.matrix", **args)

    def keyboard_chord(
        self,
        keys: Sequence[Union[str, Dict[str, Any]]],
        hold_frames: Optional[int] = None,
        hold_ms: Optional[int] = None,
    ) -> Any:
        """Press several matrix keys together, then release them all.

        Parameters
        ----------
        keys : list
            1-16 keys, each a key name ("LSHIFT", "A"), ``{"key": name}``
            or ``{"row": r, "col": c}`` (0-7).
        hold_frames : int, optional
            Frames to hold, 1-300 (server default 3).
        hold_ms : int, optional
            Milliseconds to hold, 1-5000; overrides hold_frames.
        """
        args: Dict[str, Any] = {"keys": list(keys)}
        if hold_frames is not None:
            args["hold_frames"] = hold_frames
        if hold_ms is not None:
            args["hold_ms"] = hold_ms
        return self.call("vice.keyboard.chord", **args)

    # -- Joystick -----------------------------------------------------------

    @staticmethod
    def _joystick_args(
        port: Optional[int],
        direction: Optional[Union[str, Sequence[str]]],
        fire: Optional[bool],
    ) -> Dict[str, Any]:
        args: Dict[str, Any] = {}
        if port is not None:
            args["port"] = port
        if direction is not None:
            args["direction"] = (
                direction if isinstance(direction, str) else list(direction)
            )
        if fire is not None:
            args["fire"] = fire
        return args

    def joystick_set(
        self,
        port: Optional[int] = None,
        direction: Optional[Union[str, Sequence[str]]] = None,
        fire: Optional[bool] = None,
    ) -> Any:
        """Set joystick state.

        Parameters
        ----------
        port : int, optional
            Joystick port, 1 or 2 (server default 1).
        direction : str or list of str, optional
            "up", "down", "left", "right", "none"/"center", or a list
            such as ``["up", "left"]`` for a diagonal.
        fire : bool, optional
            Fire button state.
        """
        return self.call(
            "vice.joystick.set", **self._joystick_args(port, direction, fire)
        )

    def joystick_tap(
        self,
        port: Optional[int] = None,
        direction: Optional[Union[str, Sequence[str]]] = None,
        fire: Optional[bool] = None,
        duration_frames: Optional[int] = None,
        duration_ms: Optional[int] = None,
    ) -> Any:
        """Hold a joystick state for a fixed time, then center it.

        Parameters
        ----------
        port, direction, fire
            As for :meth:`joystick_set`.
        duration_frames : int, optional
            Frames to hold, 1-300 (server default 3).
        duration_ms : int, optional
            Milliseconds to hold, 1-5000; overrides duration_frames.
        """
        args = self._joystick_args(port, direction, fire)
        if duration_frames is not None:
            args["duration_frames"] = duration_frames
        if duration_ms is not None:
            args["duration_ms"] = duration_ms
        return self.call("vice.joystick.tap", **args)

    def paddle_set(self, port: int, x: int, y: int) -> Any:
        """Set X and Y paddle positions (0-255) on control port 1 or 2."""
        return self.call("vice.paddle.set", port=port, x=x, y=y)

    # -- Advanced Debugging -------------------------------------------------

    def disassemble(
        self,
        address: Union[int, str],
        count: Optional[int] = None,
        show_symbols: Optional[bool] = None,
    ) -> Any:
        """Disassemble memory to 6502 instructions.

        Parameters
        ----------
        address : int or str
            Start address.
        count : int, optional
            Number of instructions to disassemble.
        show_symbols : bool, optional
            Show symbolic names for addresses.
        """
        args: Dict[str, Any] = {"address": address}
        if count is not None:
            args["count"] = count
        if show_symbols is not None:
            args["show_symbols"] = show_symbols
        return self.call("vice.disassemble", **args)

    def symbols_load(
        self,
        path: str,
        format: Optional[str] = None,
    ) -> Any:
        """Load a symbol/label file.

        Parameters
        ----------
        path : str
            Path to the symbol file.
        format : str, optional
            "auto" (default), "kickasm", "vice", or "simple".
        """
        args: Dict[str, Any] = {"path": path}
        if format is not None:
            args["format"] = format
        return self.call("vice.symbols.load", **args)

    def symbols_lookup(
        self,
        name: Optional[str] = None,
        address: Optional[int] = None,
    ) -> Any:
        """Look up a symbol by name or address.

        Parameters
        ----------
        name : str, optional
            Symbol name to look up.
        address : int, optional
            Address to find symbols for (the server accepts numbers only).
        """
        args: Dict[str, Any] = {}
        if name is not None:
            args["name"] = name
        if address is not None:
            args["address"] = address
        return self.call("vice.symbols.lookup", **args)

    def watch_add(
        self,
        address: Union[int, str],
        size: Optional[int] = None,
        type: Optional[str] = None,
        condition: Optional[str] = None,
        stop: Optional[bool] = None,
    ) -> Any:
        """Add a memory watchpoint.

        Shorthand for creating a checkpoint with load/store triggers.

        Parameters
        ----------
        address : int or str
            Memory address to watch.
        size : int, optional
            Number of bytes to watch.
        type : str, optional
            Watch type: "read", "write" (default), or "both".
        condition : str, optional
            Condition expression.
        stop : bool, optional
            False counts hits without stopping (server default True).
        """
        args: Dict[str, Any] = {"address": address}
        if size is not None:
            args["size"] = size
        if type is not None:
            args["type"] = type
        if condition is not None:
            args["condition"] = condition
        if stop is not None:
            args["stop"] = stop
        return self.call("vice.watch.add", **args)

    def backtrace(self, depth: Optional[int] = None) -> Any:
        """Show call stack (JSR return addresses).

        Parameters
        ----------
        depth : int, optional
            Maximum stack depth to examine.
        """
        args: Dict[str, Any] = {}
        if depth is not None:
            args["depth"] = depth
        return self.call("vice.backtrace", **args)

    def run_until(
        self,
        address: Optional[Union[int, str]] = None,
        cycles: Optional[int] = None,
    ) -> Any:
        """Resume until an address is reached or N cycles have run.

        Returns at once; the machine stops later, at whichever comes
        first. At least one of the two is required.

        Parameters
        ----------
        address : int or str, optional
            Stop at this address (a temporary breakpoint).
        cycles : int, optional
            Stop after this many CPU cycles, 1 to 100000000.
        """
        args: Dict[str, Any] = {}
        if address is not None:
            args["address"] = address
        if cycles is not None:
            args["cycles"] = cycles
        return self.call("vice.run_until", **args)

    # -- Snapshots ----------------------------------------------------------

    def snapshot_save(
        self,
        name: str,
        description: Optional[str] = None,
        include_roms: Optional[bool] = None,
        include_disks: Optional[bool] = None,
    ) -> Any:
        """Save a snapshot of the current machine state.

        Parameters
        ----------
        name : str
            Snapshot name.
        description : str, optional
            Human-readable description.
        include_roms : bool, optional
            Include ROM data in the snapshot.
        include_disks : bool, optional
            Include disk image data in the snapshot.
        """
        args: Dict[str, Any] = {"name": name}
        if description is not None:
            args["description"] = description
        if include_roms is not None:
            args["include_roms"] = include_roms
        if include_disks is not None:
            args["include_disks"] = include_disks
        return self.call("vice.snapshot.save", **args)

    def snapshot_load(self, name: str) -> Any:
        """Load a previously saved snapshot.

        Parameters
        ----------
        name : str
            Snapshot name.
        """
        return self.call("vice.snapshot.load", name=name)

    def snapshot_list(self) -> Any:
        """List available snapshots."""
        return self.call("vice.snapshot.list")

    # -- Cycles / Stopwatch -------------------------------------------------

    def cycles_stopwatch(self, action: str) -> Any:
        """Control the cycle stopwatch.

        Parameters
        ----------
        action : str
            One of "reset", "read", or "reset_and_read".
        """
        return self.call("vice.cycles.stopwatch", action=action)


# ---------------------------------------------------------------------------
# Module-level convenience
# ---------------------------------------------------------------------------

def connect(
    host: str = "127.0.0.1",
    port: int = 6510,
    **kwargs: Any,
) -> ViceMCPClient:
    """Create a ViceMCPClient (no connection is made until the first call).

    Parameters
    ----------
    host : str
        Server hostname (default ``"127.0.0.1"``).
    port : int
        Server port (default ``6510``).
    **kwargs
        Forwarded to :class:`ViceMCPClient` (max_retries, retry_delay,
        timeout, validate, monitor, log_path).

    Returns
    -------
    ViceMCPClient
        A ready-to-use client instance.
    """
    return ViceMCPClient(host=host, port=port, **kwargs)
