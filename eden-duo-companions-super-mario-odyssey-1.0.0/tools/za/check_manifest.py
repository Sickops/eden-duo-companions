#!/usr/bin/env python3
"""Static checks for packages/PokemonLegendsZA/dualscreen/manifest.json.

Nothing here needs the runtime: it catches the mistakes that would otherwise only show up as a
blank label on the second screen.

  * every `bind`, `bind_text`, `max_bind`, `x_bind`, `y_bind`, `need_bind` and `hide_bind` names a
    point the native module publishes (or a runtime point: `@flag:`, `@nav.`, `@scroll:`);
  * every `on_tap` / `on_hold` names an action in `actions`, every `page` action targets a page
    that exists, and every `nav_order` id exists on its page;
  * every label/value/button string is covered by the packaged Latin-1 font;
  * text width is measured from the actual MFNT advances and stays inside the canvas, and a rect stays inside
    too (a small negative rect is allowed only when an x_bind/y_bind places it).

Run: python tools/za/check_manifest.py
"""
import json
import re
import sys
import struct
import copy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "packages/PokemonLegendsZA/dualscreen/manifest.json"
MODULE = ROOT / "native/modules/0100F43008C44000.cpp"

CANVAS = (1240, 1080)
FONT = set(chr(c) for c in range(0x20, 0x180)) | set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:-./%+, ")

ENTITY_FIELDS = ["icon", "sname", "v", "name", "sp", "nat", "lvl", "hp", "hpmax", "g", "shiny", "item", "form",
                 "ball", "nature", "xp", "xpmax"]


def published_points() -> set[str]:
    """Every `za.*` point the module's Sample() publishes, with the slot templates expanded."""
    source = MODULE.read_text(encoding="utf-8")
    points = set(re.findall(r'"(za\.[A-Za-z0-9_.]+)"', source))
    # PublishEntity is called with "za.p" / "za.b" prefixes and builds "<prefix><n>.<field>".
    points.discard("za.p")
    points.discard("za.b")
    for prefix, slots in (("za.p", 6), ("za.b", 30)):
        for slot in range(slots):
            for field in ENTITY_FIELDS:
                points.add(f"{prefix}{slot}.{field}")
    for prefix, count, fields in [
        ("za.d",512,["number","selected","v","sp","icon","owned","state","available","name"]),
        ("za.i",1024,["selected","v","id","count","pouch","favorite","held","name"]),
        ("za.q",1024,["v","title","description","state","id"]),
        ("za.zp",128,["v","icon","name","levels","state"]),
        ("za.zone",20,["v","number","x","y"]),("za.center",12,["v","x","y"])]:
        for i in range(count):
            for field in fields: points.add(f"{prefix}{i}.{field}")
    for field in ["v","icon","name","sname","lvl","hp","hpmax","g","nature","sp","item","type1","type2","moves_ready"]:
        points.add(f"za.sel.{field}")
    for i in range(4):
        for field in ["id","name","type","pp","ppmax"]: points.add(f"za.sel.m{i}.{field}")
    for field in ["sp","icon","name","state","owned","type1","type2","height","weight","description"]:
        points.add(f"za.detail.{field}")
    # The per-generation counters are formatted at runtime ("za.dex.g%d" / "za.dex.g%d.max").
    for gen in range(9):
        points.add(f"za.dex.g{gen}")
        points.add(f"za.dex.g{gen}.max")
    return points



def expanded_widgets(widgets):
    """Validate the actual repeated cells, not only the first row's template."""
    def substitute(value, index):
        if isinstance(value,str):
            return re.sub(r"\{i([+-]\d+)?\}",lambda m:str(index+int(m.group(1) or 0)),value)
        if isinstance(value,list): return [substitute(v,index) for v in value]
        if isinstance(value,dict): return {k:substitute(v,index) for k,v in value.items()}
        return value
    for source in widgets:
        count=source.get("repeat",0)
        if not count:
            yield source
            continue
        if not isinstance(count,int) or not 0<count<=2048:
            raise ValueError(f"invalid static repeat {count!r}")
        for i in range(count):
            w=substitute(copy.deepcopy(source),i)
            x,y,rw,rh=w["rect"]
            cols=source.get("repeat_cols",0)
            if cols:
                x+=(i%cols)*source.get("repeat_dx",0)
                y+=(i//cols)*source.get("repeat_row_dy",0)
            else:
                x+=i*source.get("repeat_dx",0)
                y+=i*source.get("repeat_dy",0)
            w["rect"]=[x,y,rw,rh]
            yield w

def check(manifest=None):
    if manifest is None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    points = published_points()
    metrics_path = MANIFEST.parent / manifest['font'].removeprefix('file:')
    metrics = metrics_path.read_bytes()
    line_height = struct.unpack_from('<I', metrics, 0x1c)[0]
    first = struct.unpack_from('<I', metrics, 0x28)[0]
    glyph_count = struct.unpack_from('<I', metrics, 0x20)[0] - 1
    if line_height <= 0 or first + (glyph_count+1)*14 > len(metrics):
        return ["invalid MFNT metrics"]
    font = {chr(c) for c in range(0x20,0x20+glyph_count)}
    def text_width(text, scale):
        return sum(struct.unpack_from('<H', metrics, first + (ord(c)-0x20+1)*14 + 12)[0]
                   for c in text if c in font) * scale * 5 / line_height
    errors = []

    def is_runtime_point(name: str) -> bool:
        return (name.startswith("@flag:") or name.startswith("@nav.") or
                name.startswith("@scroll:") or name.startswith("@scroll_"))

    def check_point(name, where, allow_missing=False):
        if not name:
            return
        base = name[1:] if name.startswith("!") else name
        base = base.replace("{i}", "0")
        if is_runtime_point(base):
            return
        if base not in points and not allow_missing:
            errors.append(f"{where}: unknown point {name!r}")

    def check_text(text, where):
        bad = sorted({c for c in text if c not in font and c not in "\n\r"})
        if bad:
            errors.append(f"{where}: glyphs the packaged font lacks: {bad!r} in {text!r}")

    pages = {page["id"]: page for page in manifest["pages"]}
    if len(pages) != len(manifest["pages"]):
        errors.append("duplicate page id")
    actions = manifest.get("actions", {})

    for page in manifest["pages"]:
        pid = page["id"]
        ids = set()
        for w in expanded_widgets(page["widgets"]):
            wid = w.get("id")
            if wid:
                if wid in ids: errors.append(f"{pid}: duplicate widget id {wid!r}")
                ids.add(wid)
            for key in ("bind", "bind_text", "max_bind", "x_bind", "y_bind"):
                if key in w:
                    check_point(w[key], f"{pid}/{wid or w.get('type')}.{key}")
            if isinstance(w.get("payload"),str) and w["payload"].startswith("$") and w["payload"]!="$tap":
                check_point(w["payload"][1:],f"{pid}/{wid}.payload")
            for key in ("need_bind", "hide_bind"):
                if key in w:
                    check_point(w[key], f"{pid}/{wid or w.get('type')}.{key}")
            for key in ("on_tap", "on_hold"):
                if key in w:
                    if w[key] not in actions:
                        errors.append(f"{pid}/{wid or w.get('type')}: unknown action {w[key]!r}")
            if "hide_eq" in w and not isinstance(w["hide_eq"], int):
                errors.append(f"{pid}/{wid or w.get('type')}: hide_eq must be an integer")
            text = w.get("text")
            if isinstance(text, str) and text:
                check_text(text, f"{pid}/{wid or w.get('type')}.text")
                scale = w.get("text_scale", 3)
                width = text_width(text, scale)
                x, y, rw, rh = w.get("rect", [0, 0, 0, 0])
                if w.get("type") == "button":
                    # DrawButton centres the caption in the rect, it does not use align's anchor.
                    if width > rw - 8:
                        errors.append(f"{pid}/{wid}: caption {text!r} is {width} px wide in a "
                                      f"{rw} px button")
                elif "x_bind" not in w:
                    align = w.get("align", "left")
                    left = x - (width // 2 if align == "center" else width if align == "right" else 0)
                    if left < 0 or left + width > CANVAS[0]:
                        errors.append(f"{pid}/{wid or w.get('type')}: text {text!r} spans "
                                      f"{left}..{left + width} px")
            rect = w.get("rect")
            if rect and "x_bind" not in w and "y_bind" not in w and not w.get("scroll"):
                x, y, rw, rh = rect
                if x < 0 or y < 0 or x + rw > CANVAS[0] or y + rh > CANVAS[1]:
                    errors.append(f"{pid}/{wid or w.get('type')}: rect {rect} leaves the canvas")
        for wid in page.get("nav_order", []):
            if wid not in ids:
                errors.append(f"{pid}: nav_order names {wid!r}, which no widget has")
        # the page's own tab must exist on every page
        active_tab = {"dex_detail": "dex", "box_controls":"box", "mission_detail": "missions"}.get(pid, pid)
        if f"tab_{active_tab}" not in ids:
            errors.append(f"{pid}: no tab button for its own page")

    for name, action in actions.items():
        kind = action.get("kind")
        if kind == "page":
            target = action.get("page")
            if target not in pages:
                errors.append(f"action {name}: page {target!r} does not exist")
        elif kind == "module":
            if action.get("action") not in {"box_prev", "box_next", "box_set", "box_follow",
                                            "refresh", "dex_gen", "dex_filter", "dex_prev", "dex_next", "bag_category", "bag_prev", "bag_next",
                                            "box_pick", "box_swap_pick", "box_swap_confirm", "box_swap_cancel", "bag_pick", "zone_pick", "map_tap", "team_select", "send_out", "center_pick", "center_close", "center_travel", "travel_cancel", "dex_pick", "quest_pick", "quest_category", "quest_prev", "quest_next"}:
                errors.append(f"action {name}: module action {action.get('action')!r} is not "
                              f"implemented by the module")

    for bind in manifest.get("page_binds",[]):
        check_point(bind.get("point"),"page_bind")
        check_point(bind.get("ready_bind"),"page_bind.ready_bind")
        for branch in ["when_equal","when_not_equal"]:
            target=bind.get(branch,{}).get("page")
            if target and target not in pages: errors.append(f"page_bind: unknown page {target!r}")
    for table, names in manifest.get("tables", {}).items():
        for entry in names:
            check_text(entry, f"table {table}")

    if manifest.get("title_id") != "0100F43008C44000":
        errors.append("title_id is not Pokemon Legends Z-A")

    return errors


def main() -> int:
    errors = check()
    if errors:
        for line in errors:
            print("ERROR", line)
        print(f"{len(errors)} problem(s)")
        return 1
    print("za-manifest: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
