"""tools/android_multitouch.py -- write a uinput multi-touch script for `adb shell uinput FILE` (a virtual direct touchscreen).

usage: python tools/android_multitouch.py OUT ROT GESTURE...   (docs/reference/android_mouse.md "Multi-touch")
The S25 in landscape takes ROT=270. drag takes an optional fifth number: the hold before the lift in ms.
Coordinates are given in the landscape screen (2340 x 1080); ROT maps them to the panel's natural portrait axes:
  ROT=90 : px = Y, py = 2339 - X      ROT=270: px = 1079 - Y, py = X      ROT=0: px = X, py = Y (no mapping)
GESTURE: tap:X,Y | pinch:CX,CY,R0,R1 | spin:CX,CY,R,DEG | pan:X1,Y1,X2,Y2,DX,DY | drag:X0,Y0,X1,Y1 | wait:MS
"""
import json
import math
import sys

out, rot = sys.argv[1], int(sys.argv[2])
W, H = 2340, 1080


def map_pt(x, y):
    x = int(round(x))
    y = int(round(y))
    if rot == 90:
        return y, W - 1 - x
    if rot == 270:
        return H - 1 - y, x
    return x, y


maxx, maxy = (H - 1, W - 1) if rot in (90, 270) else (W - 1, H - 1)
lines = []
lines.append({"id": 1, "command": "register", "name": "p11 touch", "vid": 4660, "pid": 22137, "bus": "usb",
              "configuration": [{"type": "UI_SET_EVBIT", "data": [1, 3]},
                                {"type": "UI_SET_KEYBIT", "data": [330]},
                                {"type": "UI_SET_ABSBIT", "data": [47, 53, 54, 57]},
                                {"type": "UI_SET_PROPBIT", "data": [1]}],
              "abs_info": [{"code": 47, "info": {"value": 0, "minimum": 0, "maximum": 9, "fuzz": 0, "flat": 0, "resolution": 0}},
                           {"code": 53, "info": {"value": 0, "minimum": 0, "maximum": maxx, "fuzz": 0, "flat": 0, "resolution": 0}},
                           {"code": 54, "info": {"value": 0, "minimum": 0, "maximum": maxy, "fuzz": 0, "flat": 0, "resolution": 0}},
                           {"code": 57, "info": {"value": 0, "minimum": 0, "maximum": 65535, "fuzz": 0, "flat": 0, "resolution": 0}}]})
lines.append({"id": 1, "command": "delay", "duration": 1500})
track = [100]


def inject(ev):
    lines.append({"id": 1, "command": "inject", "events": ev})


def delay(ms):
    lines.append({"id": 1, "command": "delay", "duration": ms})


def frame(points, down_first=False):
    """points: list of (slot, x, y) for every finger down this frame."""
    ev = []
    for slot, x, y in points:
        px, py = map_pt(x, y)
        ev += [3, 47, slot, 3, 53, px, 3, 54, py]
    ev += [0, 0, 0]
    inject(ev)


def down(fingers):
    ev = []
    for slot, x, y in fingers:
        track[0] += 1
        px, py = map_pt(x, y)
        ev += [3, 47, slot, 3, 57, track[0], 3, 53, px, 3, 54, py]
    ev += [1, 330, 1, 0, 0, 0]
    inject(ev)


def up(slots):
    ev = []
    for slot in slots:
        ev += [3, 47, slot, 3, 57, -1]
    ev += [1, 330, 0, 0, 0, 0]
    inject(ev)


def two(path, steps=30, ms=16):
    """path(t) -> ((x1, y1), (x2, y2)) for t in 0..1."""
    (a, b) = path(0.0)
    down([(0, a[0], a[1]), (1, b[0], b[1])])
    delay(ms)
    for i in range(1, steps + 1):
        (a, b) = path(i / steps)
        frame([(0, a[0], a[1]), (1, b[0], b[1])])
        delay(ms)
    delay(100)
    up([0, 1])
    delay(400)


for g in sys.argv[3:]:
    kind, _, args = g.partition(":")
    v = [float(t) for t in args.split(",")] if args else []
    if kind == "tap":
        down([(0, v[0], v[1])])
        delay(60)
        up([0])
        delay(400)
    elif kind == "drag":
        down([(0, v[0], v[1])])
        delay(16)
        for i in range(1, 31):
            t = i / 30
            frame([(0, v[0] + (v[2] - v[0]) * t, v[1] + (v[3] - v[1]) * t)])
            delay(16)
        delay(int(v[4]) if len(v) > 4 else 200)
        up([0])
        delay(400)
    elif kind == "pinch":
        cx, cy, r0, r1 = v
        two(lambda t: ((cx - (r0 + (r1 - r0) * t), cy), (cx + (r0 + (r1 - r0) * t), cy)))
    elif kind == "spin":
        cx, cy, r, deg = v
        def p(t):
            a = math.radians(deg * t)
            return ((cx - r * math.cos(a), cy - r * math.sin(a)), (cx + r * math.cos(a), cy + r * math.sin(a)))
        two(p)
    elif kind == "pan":
        x1, y1, x2, y2, dx, dy = v
        two(lambda t: ((x1 + dx * t, y1 + dy * t), (x2 + dx * t, y2 + dy * t)))
    elif kind == "wait":
        delay(int(v[0]))
delay(500)
with open(out, "w", newline="\n") as f:
    for l in lines:
        f.write(json.dumps(l) + "\n")
