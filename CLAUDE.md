# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This repo is a **Qt Quick UI automation toolkit** with two components:

1. **`qmlinspector/`** — A Qt 6 C++ dynamic library (`libqmlinspector`) that embeds a TCP inspector server inside any QML app, enabling external tools to inspect and control the UI via synthetic events.
2. **`playqmlright/`** — A Python MCP server that bridges Claude Code to the Qt app's TCP inspector protocol.
3. **`qmlapp/`** — An example/test QML application (optional build target).

## Build & Run (Standalone)

```bash
# Build only the library (default)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Build library + example app
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DPLAYQMLRIGHT_BUILD_EXAMPLE_QMLAPP=ON
cmake --build build

# Run example app (empty window + inspector on :37521)
./build/qmlapp/appqmlapp.app/Contents/MacOS/appqmlapp

# Run test bench UI
./build/qmlapp/appqmlapp.app/Contents/MacOS/appqmlapp --test

# Kill
pkill -f appqmlapp
```

CMake requires: `Qt6::Quick Qt6::Network` (Qt 6.2+).

## Standalone Test Bench

A Python script that exercises every inspector feature end-to-end against the example app.

```bash
# 1. Start the example app in test mode
./build/qmlapp/appqmlapp.app/Contents/MacOS/appqmlapp --test &

# 2. Run the test script
python3 qmlapp/example/test_bench.py
python3 qmlapp/example/test_bench.py --fast   # shorter delays between steps
```

## Install (library for use in other projects)

```bash
cmake --install build --prefix /usr/local
```

Installed layout:
- `include/qmlinspector/InspectorServer.h`
- `lib/libqmlinspector.dylib`
- `lib/cmake/qmlinspector/qmlinspectorConfig.cmake`

---

## Integrating into Another QML Project via Git Submodule

### Step 1 — Add as submodule

In the **target project** root:

```bash
git submodule add https://github.com/yourorg/playqmlright.git third_party/playqmlright
git submodule update --init --recursive
```

### Step 2 — Add to CMakeLists.txt

In the target project's **root `CMakeLists.txt`**, add the submodule before the executable target:

```cmake
# Only build the library, not the example app
add_subdirectory(third_party/playqmlright/qmlinspector)
```

Then link the inspector to the app target:

```cmake
target_link_libraries(myapp PRIVATE qmlinspector)
```

### Step 3 — Embed InspectorServer in main.cpp

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <InspectorServer.h>   // from the submodule

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // ... load your QML ...

    InspectorServer inspector(&engine);
    const quint16 port = []() -> quint16 {
        const QByteArray env = qgetenv("QML_INSPECTOR_PORT");
        if (!env.isEmpty()) {
            bool ok = false; int p = env.toInt(&ok);
            if (ok && p > 0 && p < 65536) return static_cast<quint16>(p);
        }
        return 37521;
    }();
    if (!inspector.start(port))
        qWarning("InspectorServer failed to start.");

    return app.exec();
}
```

### Step 4 — Configure .mcp.json in the target project

Create (or edit) `.mcp.json` at the **target project root** to point the MCP server at the submodule's Python package:

```json
{
  "mcpServers": {
    "playqmlright": {
      "type": "stdio",
      "command": "uv",
      "args": [
        "run",
        "--project",
        "third_party/playqmlright/playqmlright",
        "python",
        "third_party/playqmlright/playqmlright/server.py"
      ],
      "env": {
        "QML_INSPECTOR_PORT": "37521"
      }
    }
  }
}
```

> The `QML_INSPECTOR_PORT` env var is read by both `server.py` (MCP side) and the Qt app (`qgetenv("QML_INSPECTOR_PORT")`). Change it if port 37521 conflicts.

### Step 5 — Open the target project in Claude Code

```bash
cd /path/to/your-qml-project
claude
```

Claude Code will auto-launch the MCP server via `.mcp.json`. The `mcp__playqmlright__*` tools (`dump_qt_tree`, `take_screenshot`, `click`, etc.) will then control your running QML app.

---

## Architecture

```
Claude Code
    │  MCP tools (mcp__playqmlright__*)
    ▼
playqmlright/server.py   (FastMCP, stdio transport)
    │  newline-delimited JSON-RPC over TCP 127.0.0.1:37521
    ▼
qmlinspector/InspectorServer.cpp   (QTcpServer embedded in any Qt app)
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

## Adding New QML to the Example App

`qmlapp/main.qml` is the production window; `qmlapp/example/TestBench.qml` is the test UI (`--test` flag). Both are registered in `qmlapp/CMakeLists.txt` via `qt_add_qml_module` (URI `qmlapp`). Add new QML files to the `QML_FILES` list there.

To add new inspector commands: add a handler method to `qmlinspector/InspectorServer.h/.cpp`, register it in the `dispatch` table in `handleRequest()`, then add a corresponding `@mcp.tool()` in `playqmlright/server.py`.
