# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目简介

这是一个基于 **Qt 6 / QML** 的桌面 GUI 应用程序，使用 CMake + Ninja 构建，面向 macOS（Apple Silicon）平台。

## 构建与运行

**依赖：**
- Qt 6.2+
- CMake 3.30+

**构建命令：**
```bash
# 配置（首次或修改 CMakeLists.txt 后）
cmake -B build -DCMAKE_BUILD_TYPE=Debug 

# 编译
cmake --build build
```

**运行应用：**
```bash
# 直接运行可执行文件

```

**清理重构建：**
```bash
cmake --build build --target clean
```

## 代码架构

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | 构建配置，定义 QML 模块（URI: `qml_mcp` v1.0）、macOS bundle 属性和安装目标 |
| `main.cpp` | C++ 入口，创建 `QGuiApplication` + `QQmlApplicationEngine`，启动 InspectorServer |
| `main.qml` | QML UI 定义，使用 `QtQuick.Window` 组件 |
| `InspectorServer.h/.cpp` | Unix socket JSON-RPC 服务，暴露 UI 树、截图、输入注入接口 |
| `mcp_server/server.py` | Python MCP server，通过 stdio 与 Claude Code 通信，代理请求到 InspectorServer |

**架构要点：**
- UI 完全由 QML 定义，C++ 侧负责启动引擎、资源加载和 InspectorServer
- QML 文件通过 `qt_add_qml_module()` 编译嵌入到可执行文件的 Qt 资源系统（QRC）中
- 模块 URI `qml_mcp`，版本 `1.0`，可在 QML 文件中通过 `import qml_mcp` 导入注册的自定义类型

## Qt Inspector MCP Server

### 架构

```
Claude Code ←→ mcp_server/server.py (stdio MCP) ←→ InspectorServer (Unix socket)
```


### 可用 MCP 工具

| 工具 | 说明 |
|------|------|
| `check_connection` | 检查 Qt 应用是否正在运行 |
| `dump_qt_tree` | 获取完整 QML 对象树（含几何、属性、可见性） |
| `take_screenshot` | 截取 Qt 窗口截图（返回 PNG 图像） |
| `get_window_info` | 获取窗口尺寸、DPI、屏幕信息 |
| `find_item` | 按 objectName / 类型 / 属性值搜索 UI 元素 |
| `get_item_properties` | 获取指定 ptr 元素的所有属性 |
| `click` | 在窗口坐标点击（支持左/右/中键、双击、修饰键） |
| `mouse_move` | 移动鼠标（触发 hover 效果） |
| `scroll` | 滚轮事件 |
| `key_press` | 按键注入（支持特殊键和修饰键） |
| `type_text` | 逐字符输入文本 |

**坐标系**：工具使用**窗口逻辑坐标**（logical pixels）。`dump_qt_tree` 中的 `sceneGeometry` 字段直接对应点击坐标。

### 启用 MCP Server

项目根目录已有 `.mcp.json`，Claude Code 重启后自动加载 `qt-inspector`。

**首次使用需安装依赖：**
```bash
cd mcp_server && uv sync
```

**工作流：**
1. 先构建并运行 Qt 应用（会自动创建 socket）
2. 调用 `check_connection` 确认连接
3. 调用 `dump_qt_tree` 了解 UI 结构
4. 调用 `take_screenshot` 查看视觉效果
5. 用 `click` / `key_press` / `type_text` 操控 UI

## MCP 工具（文档查询）

本项目配置了 **context7** MCP 服务器，可在开发时直接获取 Qt 6 最新官方文档。

使用方式（在对话中直接描述需求，Claude 会自动调用）：
- 查询 Qt Quick Controls 组件用法
- 查找 QML 类型 API 参考
- 获取 Qt 6 模块的最新示例代码

> context7 已作为项目级 MCP 配置于 `~/.claude.json`，重启 Claude Code 后生效。

## Qt 编码规范

- CMake 中新增 QML 文件需在 `qt_add_qml_module()` 的 `QML_FILES` 列表中注册
- 新增 C++ 类型若需暴露给 QML，需使用 `QML_ELEMENT` 宏并在同一 `qt_add_qml_module()` 调用中的 `SOURCES` 中声明
- 代码风格：4 空格缩进，UTF-8 编码（遵循 Qt Creator 项目设置）
