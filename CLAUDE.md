# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repo is a **Qt Quick UI automation toolkit** with two components:

1. **`qmlinspector/`** — A Qt 6 C++ application that embeds an inspector TCP server inside a QML app, enabling external tools to inspect and control the UI via synthetic events.
2. **`playqmlright/`** — A Python MCP server that bridges Claude Code to the Qt app's TCP inspector protocol.

## Build & Run

```bash
# Configure + build (from repo root)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run app (empty window + inspector server on :37521)
./build/qmlapp/appqmlapp.app/Contents/MacOS/appqmlapp

# Run test bench UI
./build/qmlapp/appqmlapp.app/Contents/MacOS/appqmlapp --test

# Kill
pkill -f appqmlapp
```

CMake requires: `Qt6::Quick Qt6::Network` (Qt 6.2+).

## Install (library for external use)

```bash
cmake --install build --prefix /usr/local   # installs headers + dylib + CMake package
```

Installed layout:
- `include/qmlinspector/InspectorServer.h`
- `lib/libqmlinspector.dylib`
- `lib/cmake/qmlinspector/qmlinspectorConfig.cmake`  ← use with `find_package(qmlinspector)`

## MCP Server

```bash
# Start MCP server manually (normally launched automatically by Claude Code via .mcp.json)
uv run --project playqmlright python playqmlright/server.py
```

The `.mcp.json` at the repo root configures Claude Code to auto-launch the MCP server. Port defaults to `37521`; override with `QML_INSPECTOR_PORT` env var on both the Qt app and the MCP server.

## Test Bench

A standalone Python script that exercises every inspector feature end-to-end. Requires the Qt app running with `--test`:

```bash
python3 qmlapp/example/test_bench.py
python3 qmlapp/example/test_bench.py --fast   # shorter delays
```

## Architecture

```
Claude Code
    │  MCP tools (mcp__playqmlright__*)
    ▼
playqmlright/server.py   (FastMCP, stdio transport)
    │  newline-delimited JSON-RPC over TCP 127.0.0.1:37521
    ▼
qmlinspector/InspectorServer.cpp   (QTcpServer embedded in Qt app)
    │  synthetic QMouseEvent / QKeyEvent / QHoverEvent / QWheelEvent
    ▼
QQuickWindow / QML visual tree
```

**InspectorServer** (`InspectorServer.h/.cpp`) is a `QObject` that:
- Listens on `127.0.0.1:37521` (TCP, newline-delimited JSON-RPC)
- Dispatches requests to typed command handlers (`cmdDumpTree`, `cmdMouseClick`, etc.)
- Injects synthetic Qt events directly into `QQuickWindow` — the window never needs to be in the foreground

**server.py** wraps each inspector command as an `@mcp.tool()`, opens a fresh TCP connection per request, and returns results (or `Image` for screenshots) to Claude Code.

## Key Protocol Details

- All coordinates use **logical pixels** (`sceneGeometry` from `dump_qt_tree`), not physical pixels.
- Screenshot images are at physical resolution (DPR×logical); use `sceneGeometry` for click coordinates.
- Items are addressed by **hex pointer strings** (`ptr` field) for `get_item_properties`, `focus_item`, `set_property`.
- `find_item` matches by `objectName` (exact), `type` (class name substring, case-insensitive), or a property name/value pair.
- Double-click sends `Press→Release→Press→DblClick→Release` to support both `MouseArea` and `TapHandler`.

## Adding New QML to the App

`qmlapp/main.qml` is the production window; `qmlapp/example/TestBench.qml` is the test UI (`--test` flag). Both are registered in `qmlapp/CMakeLists.txt` via `qt_add_qml_module` (URI `qmlapp`). Add new QML files to the `QML_FILES` list there.

To add new inspector commands: add a handler method to `qmlinspector/InspectorServer.h/.cpp`, register it in the `dispatch` table in `handleRequest()`, then add a corresponding `@mcp.tool()` in `playqmlright/server.py`.

## Using the Library in Another Project

After installing, add to your `CMakeLists.txt`:

```cmake
find_package(qmlinspector REQUIRED)
target_link_libraries(myapp PRIVATE qmlinspector::qmlinspector)
```

Then in C++:
```cpp
#include <InspectorServer.h>
// ...
InspectorServer inspector(&engine);
inspector.start(37521);
```
