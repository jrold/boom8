"""BOOM8 skin touch-ups, run by gen_vst.py after the skin is built:

    python3 skin/skin_post.py <skin dir>

1. Big on/off keys. A layout `button` fires Toggle Switch twice per tap (the
   key's Mouse Down action and its Button child), which suits one-shot
   triggers but leaves an on/off parameter where it was. A `toggle` flips
   once but is a 51x27 pill. So the layout places toggles, and each one in
   KEYS becomes a key of that size: its own definition with key images,
   keeping the toggle's actions, placed around the toggle's centre.
2. Knob values in warm yellow, names in cream (TUI.json label colours).
3. The artwork, repainted as the rhythm machine's panel (hardware.py): the
   MACHINE page's column headings and dividers, the instrument keys in their
   four colours over a matching stripe, the knobs and value windows; the
   SETUP page's sections. Geometry and bindings stay as generated.

Needs Pillow, the Titillium Web fonts build.sh copies to build/fonts/, and
mpc-vst-plugins' tools (layout parser, control geometry) in $MPC_VST or
mpc/.mpc-vst-plugins.
"""
import copy
import glob
import json
import os
import sys

from PIL import Image

SKIN_DIR = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.dirname(SKIN_DIR)   # mpc/
sys.path.insert(0, SKIN_DIR)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.environ.get("MPC_VST") or os.path.join(HERE, ".mpc-vst-plugins"), "tools"))
import gen_layout as gl  # noqa: E402
import hardware as hw  # noqa: E402
import shadow_skin  # noqa: E402

W, H, Y_OFF = shadow_skin.W, shadow_skin.H, shadow_skin.Y_OFF
SS = hw.SS
TOGGLE_TOP = 18   # shadow_skin.py places a toggle at cy - 18

# toggle key -> (caption, width, height)
KEYS = {
    "learn": ("PAD LEARN", 400, 90),
    "choke": ("HAT CHOKE", 240, 80),
}

# The MACHINE page's column headings: abbreviation, full names.
HEADINGS = [
    ("AC", ["ACCENT"]),
    ("BD", ["BASS DRUM"]),
    ("SD", ["SNARE DRUM"]),
    ("LT·LC", ["LOW TOM", "LOW CONGA"]),
    ("MT·MC", ["MID TOM", "MID CONGA"]),
    ("HT·HC", ["HI TOM", "HI CONGA"]),
    ("RS·CL", ["RIM SHOT", "CLAVES"]),
    ("CP·MA", ["HAND CLAP", "MARACAS"]),
    ("CB", ["COWBELL"]),
    ("CY", ["CYMBAL"]),
    ("OH", ["OPEN HAT"]),
    ("CH", ["CLOSED HAT"]),
    ("VOL", ["MASTER"]),
]
HEAD_Y = 98          # top of the headings
KNOBS_BOTTOM = 556   # where the knob section ends and the key section starts


# ---------------------------------------------------------------- TUI.json

def find_defs(node):
    """The list of {"key", "value"} component definitions."""
    if isinstance(node, list):
        if any(isinstance(x, dict) and str(x.get("key", "")).startswith("sh") for x in node):
            return node
        for x in node:
            found = find_defs(x)
            if found is not None:
                return found
    elif isinstance(node, dict):
        for v in node.values():
            found = find_defs(v)
            if found is not None:
                return found
    return None


def placements(node, prefix):
    if isinstance(node, dict):
        cd = node.get("componentData")
        if isinstance(cd, dict) and str(cd.get("type", "")).startswith(prefix):
            yield node
        for v in node.values():
            yield from placements(v, prefix)
    elif isinstance(node, list):
        for x in node:
            yield from placements(x, prefix)


def big_keys(tui, params, save):
    defs = find_defs(tui)
    by_name = {d["key"]: d for d in defs}
    done = set()
    for placed in list(placements(tui, "shToggle")):
        index = int(placed["handle remapping"]["map"][0]["value"].split()[-1])
        name = params[index]["key"]
        if name not in KEYS:
            continue
        caption, w, h = KEYS[name]
        toggle = by_name[placed["componentData"]["type"]]
        new_key = "sfKey_%s" % name
        if new_key not in by_name:
            for lit in (False, True):
                save(hw.key(w, h, caption, lit, hw.ORANGE if lit else hw.CREAM, led=True),
                     "sf_key_%s_%s.png" % (name, "on" if lit else "off"))
            value = copy.deepcopy(toggle["value"])
            focus = next(c for c in value["componentsData"] if c["componentData"]["type"] == "Focus")
            button = next(c for c in value["componentsData"] if c["componentData"]["type"] == "Button")
            focus["bounds"]["bounds"] = "0 0 %d %d" % (w, h)
            button["componentData"]["data"]["onImage"] = "sf_key_%s_on.png" % name
            button["componentData"]["data"]["offImage"] = "sf_key_%s_off.png" % name
            button["bounds"]["bounds"] = "0 0 %d %d" % (w, h)
            value["componentsData"] = [focus, button]   # the caption is in the image
            entry = {"key": new_key, "value": value}
            defs.append(entry)
            by_name[new_key] = entry
        x, y, cw, _ = (int(v) for v in placed["bounds"]["bounds"].split())
        cx, cy = x + cw // 2, y + TOGGLE_TOP
        placed["componentData"]["type"] = new_key
        placed["bounds"]["bounds"] = "%d %d %d %d" % (cx - w // 2, cy - h // 2, w, h)
        done.add(name)
    missing = set(KEYS) - done
    if missing:
        sys.exit("skin_post: no toggle placed for %s" % ", ".join(sorted(missing)))


# MPC slices a knob filmstrip as the generator's 128 square frames whatever
# the skin says, and a Live III mis-sliced a 16384 px strip; keep 128 frames,
# under 16384 px.
def check_knob_strip(r, frame, frames):
    if frames != 128 or frame * frames >= 16384:
        sys.exit("skin_post: knob r=%d: a %d-frame, %d px strip won't slice right on MPC (keep r <= 58)"
                 % (r, frame, frame * frames))


def colour_knob_labels(tui):
    for d in find_defs(tui):
        if not d["key"].startswith("shKnob"):
            continue
        labels = [c for c in d["value"]["componentsData"] if c["componentData"]["type"] == "Label"]
        if len(labels) >= 2:   # name, then value
            labels[-1]["componentData"]["data"]["textStyle"]["colour"] = "ff%02x%02x%02x" % hw.VALUE_INK
            labels[0]["componentData"]["data"]["textStyle"]["colour"] = "ff%02x%02x%02x" % hw.INK


# ---------------------------------------------------------------- artwork

def knob_value_box(w):
    """Where shadow_skin puts a knob's value text (layout coordinates)."""
    r = w["r"]
    s = 2 * r + 10
    ns = w.get("ns")
    name_h = 20 if ns is None else (0 if ns == 0 else max(20, round(ns * 1.2)))
    vs = w.get("vs")
    value_h = 26 if vs is None else max(26, round(vs * 1.2))
    top = w["cy"] - s // 2
    vy = top + (s // 2 + r + 2) + name_h + (2 if name_h else 0)
    bw = max(s, w.get("bw") or 130)
    return (w["cx"] - bw // 2 + 4, vy + 1, w["cx"] + bw // 2 - 4, vy + value_h - 1)


def machine_page(bg):
    """The knob section's headings and column rules, the stripe and step
    numbers of the key section, and the name plate."""
    for i, (abbrev, names) in enumerate(HEADINGS):
        cx = gl.COL_X0 + gl.COL_W // 2 + gl.COL_W * i
        colour = hw.ORANGE if i in (0, len(HEADINGS) - 1) else hw.INK
        hw.heading(bg, cx, HEAD_Y - Y_OFF, abbrev, names, colour)
        if i:
            x = gl.COL_X0 + gl.COL_W * i
            hw.rule(bg, x, HEAD_Y - Y_OFF + 4, x, KNOBS_BOTTOM - Y_OFF - 8)
    hw.rule(bg, gl.COL_X0, HEAD_Y - Y_OFF + 53, gl.COL_X0 + gl.COL_W * len(HEADINGS), HEAD_Y - Y_OFF + 53)
    # The ACCENT and VOLUME columns are set apart, as on the hardware.
    for x in (gl.COL_X0 + gl.COL_W, gl.COL_X0 + gl.COL_W * (len(HEADINGS) - 1)):
        hw.rule(bg, x, HEAD_Y - Y_OFF + 4, x, KNOBS_BOTTOM - Y_OFF - 8, colour=hw.INK_DIM, width=2)

    # Key section: a stripe in each group's colour over its four keys, step
    # numbers under the keys, the name plate at the right of the numbers.
    top = gl.KEY_CY - gl.KEY_H // 2 - 14 - Y_OFF
    for g in range(4):
        x0 = gl.key_cx(4 * g) - gl.KEY_W // 2
        x1 = gl.key_cx(4 * g + 3) + gl.KEY_W // 2
        hw.bar(bg, x0, top, x1, top + 6, hw.GROUP_COLOURS[g])
    for i in range(16):
        hw.label(bg, gl.key_cx(i), gl.KEY_CY + gl.KEY_H // 2 + 14 - Y_OFF, str(i + 1), 13, hw.INK_DIM)


def page(tab, t, params):
    bg = hw.plate(W, H)
    widgets = tab["widgets"]

    def box(x0, y0, x1, y1):
        return (x0, y0 - Y_OFF, x1, y1 - Y_OFF)

    for w in widgets:
        if w["kind"] == "frame":
            hw.section(bg, box(w["x"], w["y"], w["x"] + w["w"], w["y"] + w["h"]), w.get("title"))
    if t == 0:
        machine_page(bg)
        hw.label(bg, 1262, 702 - Y_OFF, "BOOM8  ANALOG RHYTHM MACHINE", 12, hw.ORANGE, anchor="rm", spacing=2)

    for w in widgets:
        k = w["kind"]
        if k == "readout":
            hw.well(bg, box(w["cx"] - w["w"] // 2, w["cy"] - w["h"] // 2, w["cx"] + w["w"] // 2, w["cy"] + w["h"] // 2))
        elif k == "knob":
            hw.knob_base(bg, w["cx"], w["cy"] - Y_OFF, 2 * w["r"] + 10)
            hw.well(bg, box(*knob_value_box(w)))
    return bg.resize((W, H), Image.Resampling.LANCZOS).convert("RGB")


def key_colour(key):
    """An instrument key's colour: the four groups of four, left to right."""
    if key.startswith("hit_"):
        i = gl.VOICES.index(key[4:])
        return hw.GROUP_COLOURS[i // 4]
    return hw.CREAM


def main():
    skin = sys.argv[1]
    pdir = skin if os.path.exists(os.path.join(skin, "TUI.json")) else os.path.join(skin, "Plugin Skins")
    tui_path = os.path.join(pdir, "TUI.json")
    tui = json.load(open(tui_path))
    params = json.load(open(os.path.join(HERE, "params.json")))["params"]
    tabs, _ = shadow_skin.parse_layout(os.path.join(HERE, "layout.conf"))
    fonts = os.path.join(HERE, "build", "fonts")
    hw.use_fonts(os.path.join(fonts, "TitilliumWeb-Bold.ttf"), os.path.join(fonts, "TitilliumWeb-SemiBold.ttf"))
    written = []

    def save(img, name):
        img.save(os.path.join(pdir, name), optimize=True)
        written.append(name)

    big_keys(tui, params, save)
    colour_knob_labels(tui)
    json.dump(tui, open(tui_path, "w"), indent=1)

    knob_sizes = set()
    for t, tab in enumerate(tabs):
        save(page(tab, t, params), "sh_bg_%d.png" % t)
        for w in tab["widgets"]:
            k = w["kind"]
            if k == "button":
                paths = glob.glob(os.path.join(pdir, "sh_btn_%s_*_off.png" % w["key"]))
                if not paths:
                    sys.exit("skin_post: no button image for %s" % w["key"])
                for path in paths:
                    bw, bh = Image.open(path).size
                    caption = w.get("label", "")
                    colour = key_colour(w["key"])
                    save(hw.key(bw, bh, caption, False, colour), os.path.basename(path))
                    save(hw.key(bw, bh, caption, True, colour), os.path.basename(path)[:-len("_off.png")] + "_on.png")
            elif k == "knob":
                knob_sizes.add(w["r"])

    for path in glob.glob(os.path.join(pdir, "sh_tile_*_off.png")):
        tw, th = Image.open(path).size
        save(hw.tile(tw, th, False), os.path.basename(path))
        save(hw.tile(tw, th, True), os.path.basename(path)[:-len("_off.png")] + "_on.png")

    for r in knob_sizes:
        name = "sh_knob_r%d.png" % r
        strip = Image.open(os.path.join(pdir, name))
        frame, frames = strip.width, strip.height // strip.width
        check_knob_strip(r, frame, frames)
        save(hw.knob_strip(frame, frames), name)

    print("skin_post: %d big keys, %d images repainted" % (len(KEYS), len(written)))


if __name__ == "__main__":
    main()
