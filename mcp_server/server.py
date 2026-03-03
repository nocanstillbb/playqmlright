#!/usr/bin/env python3
"""
Qt Quick MCP Inspector Server (Background Automation)
=====================================================
Exposes the running Qt Quick application to Claude Code via the Model Context
Protocol (MCP).  Communicates with the Qt app over TCP (127.0.0.1) using
a simple newline-delimited JSON-RPC protocol.

All UI interactions (click, hover, focus, input) are performed via synthetic
Qt events injected directly into the QQuickWindow event loop.  No real
mouse/keyboard control is used and the window does NOT need to be in the
foreground.

Usage
-----
Run the Qt application first (it will start listening on port 37521), then
start this server:

    uv run server.py          # or: python server.py

Configure in Claude Code's MCP settings (see mcp_config.json).
"""

import asyncio
import base64
import json
import os
import sys
from typing import Any

from mcp.server.fastmcp import FastMCP, Image

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

TCP_HOST: str = "127.0.0.1"
TCP_PORT: int = int(os.environ.get("QML_INSPECTOR_PORT", "37521"))

mcp = FastMCP(
    "qt-quick-inspector",
    instructions=(
        "Tools for inspecting and controlling a running Qt Quick / QML application. "
        "All interactions are background-only – no real mouse/keyboard is used and "
        "the window does not need to be in the foreground. "
        "Use dump_qt_tree to understand the UI structure, take_screenshot to see "
        "the current visual state, and the input tools to interact with the UI."
    ),
)

# ---------------------------------------------------------------------------
# Low-level transport: send one JSON-RPC request, return the parsed response.
# ---------------------------------------------------------------------------

_request_id: int = 0


async def qt_request(method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    """Send a single JSON-RPC request to the Qt inspector server."""
    global _request_id
    _request_id += 1

    payload = json.dumps({"id": _request_id, "method": method, "params": params or {}})

    # Use a 64 MB read-buffer so that large responses (e.g. base64 screenshots)
    # don't hit asyncio's default 64 KB readline() limit.
    LIMIT = 64 * 1024 * 1024  # 64 MB
    try:
        reader, writer = await asyncio.open_connection(TCP_HOST, TCP_PORT, limit=LIMIT)
    except ConnectionRefusedError:
        return {"error": f"Qt app not running – connection refused on {TCP_HOST}:{TCP_PORT}"}
    except OSError as exc:
        return {"error": f"Cannot connect to Qt app: {exc}"}

    try:
        writer.write(payload.encode() + b"\n")
        await writer.drain()
        line = await asyncio.wait_for(reader.readline(), timeout=30.0)
        response = json.loads(line)
        return response
    except asyncio.TimeoutError:
        return {"error": "Timeout waiting for Qt response"}
    except json.JSONDecodeError as exc:
        return {"error": f"Invalid JSON from Qt: {exc}"}
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


def _unwrap(response: dict[str, Any]) -> Any:
    """Return response['result'] or raise RuntimeError on error."""
    if "error" in response:
        raise RuntimeError(response["error"])
    return response.get("result", response)


# ---------------------------------------------------------------------------
# MCP Tools
# ---------------------------------------------------------------------------


@mcp.tool()
async def dump_qt_tree(max_depth: int = 50) -> str:
    """
    Dump the full Qt Quick / QML visual object tree.

    Returns a JSON string describing every visible UI element with its:
    - type (QML / C++ class name)
    - objectName
    - ptr (unique address, use with get_item_properties / focus_item)
    - geometry (parent-relative x/y/width/height)
    - sceneGeometry (window-relative x/y/width/height) – use these for click coords
    - visible / enabled / opacity / z
    - props (all simple Q_PROPERTY values: text, color, font, etc.)
    - children (nested list)

    Args:
        max_depth: Maximum tree depth to traverse (default 50).
    """
    resp = await qt_request("dump_tree", {"maxDepth": max_depth})
    result = _unwrap(resp)
    return json.dumps(result, ensure_ascii=False, indent=2)


@mcp.tool()
async def take_screenshot() -> Image:
    """
    Capture a screenshot of the Qt Quick window.

    Returns a PNG image.  Note: on Retina / HiDPI displays the image pixel
    dimensions are larger than the logical window size (dpr > 1).  The 'dpr'
    field in dump_qt_tree / get_window_info tells you the scale factor.
    Use logical coordinates (sceneGeometry) for click / key operations.
    """
    resp = await qt_request("screenshot")
    result = _unwrap(resp)
    b64 = result["image"]
    return Image(data=b64, format="png")


@mcp.tool()
async def get_window_info() -> str:
    """
    Get geometry and display information about the main Qt Quick window.

    Returns title, x/y/width/height (logical pixels), device pixel ratio,
    and screen details.
    """
    resp = await qt_request("get_window_info")
    result = _unwrap(resp)
    return json.dumps(result, ensure_ascii=False, indent=2)


@mcp.tool()
async def find_item(
    object_name: str = "",
    item_type: str = "",
    property_name: str = "",
    property_value: str = "",
) -> str:
    """
    Search the QML object tree for items matching the given criteria.

    At least one of object_name, item_type, or property_name must be provided.
    Results include ptr, sceneGeometry, and visible for each match.

    Args:
        object_name:    Exact objectName to match.
        item_type:      Class name substring to match (case-insensitive),
                        e.g. "Button", "Text", "Rectangle".
        property_name:  Q_PROPERTY name to filter on.
        property_value: Expected string value of property_name.
    """
    params: dict[str, Any] = {}
    if object_name:    params["objectName"] = object_name
    if item_type:      params["type"]       = item_type
    if property_name:  params["property"]   = property_name
    if property_value: params["value"]      = property_value

    if not params:
        return json.dumps({"error": "Provide at least one search criterion"})

    resp = await qt_request("find_item", params)
    result = _unwrap(resp)
    return json.dumps(result, ensure_ascii=False, indent=2)


@mcp.tool()
async def get_item_properties(ptr: str) -> str:
    """
    Get all serialisable properties of a specific QML item by its pointer.

    Use 'ptr' values from dump_qt_tree or find_item output.

    Args:
        ptr: Hex pointer string, e.g. "0x7f9a1234abc0".
    """
    resp = await qt_request("get_properties", {"ptr": ptr})
    result = _unwrap(resp)
    return json.dumps(result, ensure_ascii=False, indent=2)


@mcp.tool()
async def focus_item(
    ptr: str,
    reason: str = "OtherFocusReason",
) -> str:
    """
    Programmatically set keyboard focus on a QML item.

    Uses QQuickItem::forceActiveFocus() to give the item active focus,
    without needing a real mouse click or the window to be in the foreground.
    This is useful before typing text into a TextInput/TextField.

    Args:
        ptr:    Hex pointer string of the target item (from dump_qt_tree / find_item).
        reason: Focus reason – "OtherFocusReason" (default), "MouseFocusReason",
                or "TabFocusReason".
    """
    resp = await qt_request("focus_item", {"ptr": ptr, "reason": reason})
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def set_property(
    ptr: str,
    property_name: str,
    value: str | float | bool,
) -> str:
    """
    Set a Q_PROPERTY value on a QML item programmatically.

    Supports bool, number, and string values.  This allows direct
    manipulation of QML item state without UI interaction.

    Args:
        ptr:           Hex pointer string of the target item.
        property_name: Name of the property to set (e.g. "text", "visible", "opacity").
        value:         New value (bool, number, or string).
    """
    resp = await qt_request("set_property", {
        "ptr": ptr,
        "property": property_name,
        "value": value,
    })
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def click(
    x: float,
    y: float,
    button: str = "left",
    clicks: int = 1,
    modifiers: list[str] | None = None,
) -> str:
    """
    Simulate a mouse click at window-relative coordinates (background, no real cursor).

    Uses synthetic QMouseEvent injected via sendEvent – no real mouse is moved
    and the window does not need to be in the foreground.

    Use sceneGeometry values from dump_qt_tree for the x/y coordinates
    (these are in logical pixels, not physical screen pixels).

    Args:
        x:         Window-relative X coordinate (logical pixels).
        y:         Window-relative Y coordinate (logical pixels).
        button:    "left" | "right" | "middle"  (default "left").
        clicks:    1 for single-click, 2 for double-click  (default 1).
        modifiers: List of modifier keys, e.g. ["Ctrl"], ["Shift", "Alt"].
    """
    resp = await qt_request("mouse_click", {
        "x": x, "y": y,
        "button": button,
        "clicks": clicks,
        "modifiers": modifiers or [],
    })
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def mouse_drag(
    start_x: float,
    start_y: float,
    end_x: float,
    end_y: float,
    steps: int = 10,
    step_ms: int = 16,
    modifiers: list[str] | None = None,
) -> str:
    """
    Simulate a mouse drag from one point to another (background, no real cursor).

    Useful for dragging sliders, resizing, reordering, etc.  Performs a press at
    (start_x, start_y), interpolates movement in `steps` increments with `step_ms`
    delay each, then releases at (end_x, end_y).

    Args:
        start_x:   Start X (window logical pixels).
        start_y:   Start Y.
        end_x:     End X.
        end_y:     End Y.
        steps:     Number of intermediate move events (default 10).
        step_ms:   Milliseconds between each step (default 16, ~60fps).
        modifiers: Modifier keys list.
    """
    resp = await qt_request("mouse_drag", {
        "startX": start_x, "startY": start_y,
        "endX": end_x, "endY": end_y,
        "steps": steps, "stepMs": step_ms,
        "modifiers": modifiers or [],
    })
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def mouse_move(x: float, y: float) -> str:
    """
    Simulate mouse movement to window-relative coordinates (background, no real cursor).

    Triggers hover/enter effects via synthetic QMouseEvent + QHoverEvent.
    The real OS cursor is NOT moved.

    Args:
        x: Window-relative X coordinate (logical pixels).
        y: Window-relative Y coordinate (logical pixels).
    """
    resp = await qt_request("mouse_move", {"x": x, "y": y})
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def scroll(
    x: float,
    y: float,
    dy: int = 120,
    dx: int = 0,
    modifiers: list[str] | None = None,
) -> str:
    """
    Simulate a mouse wheel scroll event at window-relative coordinates.

    Args:
        x:         Window-relative X coordinate.
        y:         Window-relative Y coordinate.
        dy:        Vertical scroll amount in angle deltas (120 = one notch up,
                   -120 = one notch down).  Default 120.
        dx:        Horizontal scroll amount (default 0).
        modifiers: Modifier keys list.
    """
    resp = await qt_request("mouse_scroll", {
        "x": x, "y": y,
        "dy": dy, "dx": dx,
        "modifiers": modifiers or [],
    })
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def key_press(
    key: str,
    modifiers: list[str] | None = None,
) -> str:
    """
    Simulate a keyboard key press and release (background, no real keyboard).

    The key event is sent to the currently focused QML item.  Use focus_item
    to direct focus to a specific item first if needed.

    Special key names: Return, Enter, Escape, Tab, Backspace, Delete, Insert,
    Left, Right, Up, Down, Home, End, PageUp, PageDown, Space, F1–F12.
    Single printable characters can be passed directly, e.g. "a", "A", "1".

    Args:
        key:       Key name (see above).
        modifiers: List of modifiers: "Ctrl", "Shift", "Alt", "Meta".
                   Example: key="a", modifiers=["Ctrl"]  →  Ctrl+A (select all).
    """
    resp = await qt_request("key_press", {
        "key": key,
        "modifiers": modifiers or [],
    })
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def type_text(text: str) -> str:
    """
    Type a string of characters into the focused widget (background, no real keyboard).

    Sends synthetic QKeyEvents one character at a time.  First use focus_item
    or click on the field to give it focus, then call this tool.

    Args:
        text: The string to type.
    """
    resp = await qt_request("type_text", {"text": text})
    result = _unwrap(resp)
    return json.dumps(result)


@mcp.tool()
async def check_connection() -> str:
    """
    Check whether the Qt inspector server is reachable.

    Returns connection status and the TCP address being used.
    """
    addr = f"{TCP_HOST}:{TCP_PORT}"
    try:
        resp = await qt_request("get_window_info")
        if "error" in resp:
            return json.dumps({
                "connected": False,
                "address": addr,
                "error": resp["error"],
            })
        return json.dumps({
            "connected": True,
            "address": addr,
            "window": resp.get("result", {}).get("title", "?"),
        })
    except Exception as exc:
        return json.dumps({"connected": False, "address": addr, "error": str(exc)})


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    mcp.run(transport="stdio")
