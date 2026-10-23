#!/usr/bin/env python3
"""Regenerate qml/icons/*.svg from lucide-static (ISC).

  npm pack lucide-static && tar xzf lucide-static-*.tgz
  python3 tools/build_icons.py package/icons

Every icon is normalised to a 24x24 viewBox, stroke="currentColor",
stroke-width 1.6, round caps and joins, and <path> elements only (circles,
rects, lines and polys are converted) so the C++ IconSet can feed the path
data straight into a QML Shape. Icons listed in FILLED are tagged
fill="currentColor" and drawn solid with a matching soft stroke.
"""
import math, os, re, sys
import xml.etree.ElementTree as ET

# noch name -> lucide name (or None for a hand-drawn path in CUSTOM)
MAP = {
    "app": "app-window", "code": "code-xml", "music": "music",
    "close": "x", "plus": "plus", "minus": "minus", "check": "check",
    "open": "arrow-up-right", "external": "external-link", "folder": "folder",
    "pin": "pin", "unpin": "pin-off", "left": "chevron-left", "right": "chevron-right",
    "up": "chevron-up", "down": "chevron-down", "chevron-down": "chevron-down",
    "power": "power", "reset": "rotate-ccw", "settings": "settings-2", "list": "list",
    "play": "play", "pause": "pause", "prev": "skip-back", "next": "skip-forward",
    "stop": "square", "note": "music-2", "text": "text", "notes": "music", "heart": "heart",
    "volume": "volume-2", "mute": "volume-x", "shuffle": "shuffle", "repeat": "repeat",
    "more": "ellipsis", "edit": "pencil", "activity": "activity", "timer": "timer",
    "clipboard": "clipboard", "wifi": "wifi", "bluetooth": "bluetooth", "moon": "moon",
    "mic": "mic", "mic-off": "mic-off", "lock": "lock", "search": "search",
    "globe": "globe", "sliders": "sliders-horizontal", "equals": "equal", "eye": "eye",
    "eye-off": "eye-off", "expand": "maximize-2", "history": "history", "copy": "copy",
    "trash": "trash-2", "droplet": "droplet", "send": "send-horizontal", "spark": "sparkles",
    "chart": "chart-column", "doc": "file-text", "tasks": "list-checks",
    "terminal": "square-terminal", "computer": "monitor", "cursor": "mouse-pointer-2",
    "memory": "brain", "mail": "mail", "advisor": "lightbulb", "tool-search": "package-search",
    "code-search": "file-search", "web-fetch": "link", "shield": "shield",
    "shield-check": "shield-check", "shield-alert": "shield-alert", "bolt": "zap",
    "alert": "circle-alert", "info": "info", "check-circle": "circle-check",
    "x-circle": "circle-x", "loader": "loader-circle", "clock": "clock",
    "hand": "hand", "handoff": "git-fork", "trace": "waypoints", "chat": "message-square",
    "layers": "layers", "gauge": "gauge", "key": "key-round", "cpu": "cpu",
    "zzz": None, "user": "user-round", "keyboard": "keyboard", "filter": "list-filter",
}
CUSTOM = {
    "zzz": "M5 7.5h5l-5 6h5 M13 4h5l-5 6h5 M14 15h4l-4 5h4",
}
FILLED = {"play", "pause", "prev", "next", "stop"}

def f(v):
    v = round(float(v), 3)
    return ("%g" % v)

NUM = r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?"

def absolute_start(d):
    """Make a path's leading moveto absolute. Lucide paths start with 'm',
    which is only absolute while it is the first command of a document; once
    several <path>s are joined into one path string it would be relative to
    the previous subpath's end. Implicit pairs after 'm' are relative
    linetos, so they become an explicit 'l'."""
    d = d.strip()
    if not d.startswith("m"):
        return d
    m = re.match(r"m\s*(%s)[\s,]*(%s)[\s,]*" % (NUM, NUM), d)
    rest = d[m.end():]
    head = "M%s %s" % (m.group(1), m.group(2))
    if rest and not rest[0].isalpha():
        return head + " l" + rest
    return head + (" " + rest if rest else "")

def el_to_d(e):
    tag = e.tag.split('}')[-1]
    a = e.attrib
    if tag == "path":
        return absolute_start(a["d"])
    if tag == "line":
        return "M%s %s L%s %s" % (f(a["x1"]), f(a["y1"]), f(a["x2"]), f(a["y2"]))
    if tag in ("polyline", "polygon"):
        pts = re.findall(r"-?[\d.]+", a["points"])
        pairs = [(pts[i], pts[i + 1]) for i in range(0, len(pts), 2)]
        d = "M" + " L".join("%s %s" % (f(x), f(y)) for x, y in pairs)
        return d + (" Z" if tag == "polygon" else "")
    if tag in ("circle", "ellipse"):
        cx, cy = float(a.get("cx", 0)), float(a.get("cy", 0))
        rx = float(a.get("r", a.get("rx", 0))); ry = float(a.get("r", a.get("ry", rx)))
        return ("M%s %s A%s %s 0 1 0 %s %s A%s %s 0 1 0 %s %s Z" %
                (f(cx - rx), f(cy), f(rx), f(ry), f(cx + rx), f(cy), f(rx), f(ry), f(cx - rx), f(cy)))
    if tag == "rect":
        x, y = float(a.get("x", 0)), float(a.get("y", 0))
        w, h = float(a["width"]), float(a["height"])
        rx = float(a.get("rx", a.get("ry", 0))); ry = float(a.get("ry", rx))
        rx = min(rx, w / 2); ry = min(ry, h / 2)
        if rx <= 0:
            return "M%s %s H%s V%s H%s Z" % (f(x), f(y), f(x + w), f(y + h), f(x))
        return ("M%s %s H%s A%s %s 0 0 1 %s %s V%s A%s %s 0 0 1 %s %s H%s A%s %s 0 0 1 %s %s V%s A%s %s 0 0 1 %s %s Z" % (
            f(x + rx), f(y), f(x + w - rx), f(rx), f(ry), f(x + w), f(y + ry), f(y + h - ry),
            f(rx), f(ry), f(x + w - rx), f(y + h), f(x + rx), f(rx), f(ry), f(x), f(y + h - ry),
            f(y + ry), f(rx), f(ry), f(x + rx), f(y)))
    raise ValueError("unsupported element " + tag)

def main(src, out):
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        if old.endswith(".svg"):
            os.remove(os.path.join(out, old))
    for name, lucide in sorted(MAP.items()):
        if lucide is None:
            ds = [CUSTOM[name]]
        else:
            root = ET.parse(os.path.join(src, lucide + ".svg")).getroot()
            ds = [el_to_d(e) for e in root.iter() if e.tag.split('}')[-1] != "svg"]
        fill = "currentColor" if name in FILLED else "none"
        body = "\n".join('  <path d="%s"/>' % d for d in ds)
        src_note = ("lucide:" + lucide) if lucide else "noch"
        svg = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" '
               'fill="%s" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" '
               'stroke-linejoin="round" data-source="%s">\n%s\n</svg>\n' % (fill, src_note, body))
        open(os.path.join(out, name + ".svg"), "w").write(svg)
    print("wrote", len(MAP), "icons to", out)

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1] if len(sys.argv) > 1 else "package/icons",
         os.path.join(here, "..", "qml", "icons"))
