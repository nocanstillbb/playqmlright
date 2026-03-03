#!/usr/bin/env python3
"""
Inspector Test Bench — automated visual test for all MCP inspector features.

Usage:
    python3 example/test_bench.py [--fast]

Requires the Qt app to be running with --test flag:
    ./build/appqml_mcp --test
"""
import asyncio
import json
import sys

HOST, PORT = "127.0.0.1", 37521
_id = 0
DELAY = 1.5 if "--fast" not in sys.argv else 0.3


async def req(method, params=None):
    global _id
    _id += 1
    payload = json.dumps({"id": _id, "method": method, "params": params or {}})
    r, w = await asyncio.open_connection(HOST, PORT, limit=64 * 1024 * 1024)
    w.write(payload.encode() + b"\n")
    await w.drain()
    line = await asyncio.wait_for(r.readline(), timeout=10)
    w.close()
    resp = json.loads(line)
    if "error" in resp:
        raise RuntimeError(resp["error"])
    return resp.get("result", resp)


async def step(desc):
    print(f"\n>>> {desc}", flush=True)
    await asyncio.sleep(DELAY)


async def find_center(name):
    r = await req("find_item", {"objectName": name})
    g = r["matches"][0]["sceneGeometry"]
    return g["x"] + g["width"] / 2, g["y"] + g["height"] / 2, r["matches"][0]["ptr"]


async def find_geom(name):
    r = await req("find_item", {"objectName": name})
    m = r["matches"][0]
    return m["sceneGeometry"], m["ptr"], m.get("props", {})


async def main():
    # ── Click ──
    await step("1. Click 'Say Hello'")
    cx, cy, _ = await find_center("btnHello")
    await req("mouse_click", {"x": cx, "y": cy})
    print(f"   clicked @ ({cx:.0f}, {cy:.0f})")

    await step("2. Click 'Count' x3")
    cx, cy, _ = await find_center("btnCount")
    for i in range(3):
        await req("mouse_click", {"x": cx, "y": cy})
        print(f"   click #{i + 1}")
        await asyncio.sleep(0.6)

    await step("3. Right-click")
    cx, cy, _ = await find_center("btnRightClick")
    await req("mouse_click", {"x": cx, "y": cy, "button": "right"})
    print("   right-clicked!")

    await step("4. Double-click")
    cx, cy, _ = await find_center("btnDouble")
    await req("mouse_click", {"x": cx, "y": cy, "clicks": 2})
    print("   double-clicked!")

    # ── Hover ──
    await step("5. Hover → green")
    cx, cy, _ = await find_center("hoverRect")
    await req("mouse_move", {"x": cx, "y": cy})

    await step("6. Hover away → grey")
    await req("mouse_move", {"x": 0, "y": 0})

    await step("7. Hover → HoverHandler blue")
    cx, cy, _ = await find_center("hoverHandlerRect")
    await req("mouse_move", {"x": cx, "y": cy})

    await step("8. Move away")
    await req("mouse_move", {"x": 0, "y": 0})

    # ── Focus & Input ──
    await step("9. Focus Name input")
    _, _, ptr = await find_center("inputName")
    r = await req("focus_item", {"ptr": ptr})
    print(f"   activeFocus={r.get('activeFocus')}")

    await step("10. Type 'Alice'")
    await req("type_text", {"text": "Alice"})

    await step("11. Tab → Email, type address")
    await req("key_press", {"key": "Tab"})
    await asyncio.sleep(0.4)
    await req("type_text", {"text": "alice@test.com"})

    await step("12. Click Submit")
    cx, cy, _ = await find_center("btnSubmit")
    await req("mouse_click", {"x": cx, "y": cy})

    await step("13. Focus TextArea, type 2 lines")
    _, _, ptr = await find_center("textArea")
    await req("focus_item", {"ptr": ptr})
    await asyncio.sleep(0.3)
    await req("type_text", {"text": "Hello"})
    await req("key_press", {"key": "Return"})
    await req("type_text", {"text": "World"})

    # ── Key Press ──
    await step("14. Focus key area, send Ctrl+A / F5 / Space")
    _, _, ptr = await find_center("keyArea")
    await req("focus_item", {"ptr": ptr})
    await asyncio.sleep(0.5)
    for key, mods, label in [("a", ["Ctrl"], "Ctrl+A"), ("F5", [], "F5"), ("Space", [], "Space")]:
        await req("key_press", {"key": key, "modifiers": mods})
        print(f"   sent {label}")
        await asyncio.sleep(1.0)

    # ── Scroll ──
    await step("15. Scroll list down 5 notches")
    geom, _, _ = await find_geom("scrollList")
    sx, sy = geom["x"] + geom["width"] / 2, geom["y"] + geom["height"] / 2
    for i in range(5):
        await req("mouse_scroll", {"x": sx, "y": sy, "dy": -120})
        print(f"   scroll {i + 1}/5")
        await asyncio.sleep(0.5)

    # ── Drag (Slider) ──
    await step("16. Drag slider: 50 → 80")
    geom, _, props = await find_geom("slider")
    sx, w = geom["x"], geom["width"]
    cy = geom["y"] + geom["height"] / 2
    val = props.get("value", 50)
    handle_x = sx + w * (val / 100)
    target_x = sx + w * 0.8
    await req("mouse_drag", {"startX": handle_x, "startY": cy, "endX": target_x, "endY": cy, "steps": 20, "stepMs": 30})
    _, _, props = await find_geom("slider")
    print(f"   value = {props.get('value', '?'):.0f}")

    await step("17. Drag slider: → 20")
    val = props.get("value", 80)
    handle_x = sx + w * (val / 100)
    target_x = sx + w * 0.2
    await req("mouse_drag", {"startX": handle_x, "startY": cy, "endX": target_x, "endY": cy, "steps": 20, "stepMs": 30})
    _, _, props = await find_geom("slider")
    print(f"   value = {props.get('value', '?'):.0f}")

    await step("18. Drag slider: → 50 (reset)")
    val = props.get("value", 20)
    handle_x = sx + w * (val / 100)
    target_x = sx + w * 0.5
    await req("mouse_drag", {"startX": handle_x, "startY": cy, "endX": target_x, "endY": cy, "steps": 20, "stepMs": 30})

    # ── set_property ──
    await step("19. set_property: colorBox → blue")
    _, _, ptr = await find_center("colorBox")
    await req("set_property", {"ptr": ptr, "property": "color", "value": "#2196F3"})

    await step("20. set_property: opacityBox → 0.2")
    _, optr, _ = await find_geom("opacityBox")
    await req("set_property", {"ptr": optr, "property": "opacity", "value": 0.2})

    await step("21. set_property: visibleBox → hidden")
    _, vptr, _ = await find_geom("visibleBox")
    await req("set_property", {"ptr": vptr, "property": "visible", "value": False})

    await step("22. set_property: label text")
    _, _, lptr = await find_center("dynamicLabel")
    await req("set_property", {"ptr": lptr, "property": "text", "value": "Inspector Works!"})

    await step("23. Restore visibility & opacity")
    await req("set_property", {"ptr": vptr, "property": "visible", "value": True})
    await req("set_property", {"ptr": optr, "property": "opacity", "value": 1.0})

    # ── CheckBox ──
    await step("24. Toggle checkbox ON / OFF")
    cx, cy, _ = await find_center("checkBox")
    await req("mouse_click", {"x": cx, "y": cy})
    print("   ON")
    await asyncio.sleep(1.2)
    await req("mouse_click", {"x": cx, "y": cy})
    print("   OFF")

    print("\n" + "=" * 40)
    print("  All tests passed!")
    print("=" * 40, flush=True)


if __name__ == "__main__":
    asyncio.run(main())
