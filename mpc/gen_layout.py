"""Writes params.json and layout.conf: the plug-in's parameters and where each
control sits on the MPC's screen. Run it after changing either:

    python3 gen_layout.py
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# Knob columns of the main page, left to right, as on the hardware's panel.
COLS = [
    ("ACCENT", [("accent", "LEVEL")]),
    ("BD", [("bd_level", "LEVEL"), ("bd_tone", "TONE"), ("bd_decay", "DECAY")]),
    ("SD", [("sd_level", "LEVEL"), ("sd_tone", "TONE"), ("sd_snappy", "SNAPPY")]),
    ("LT", [("lt_level", "LEVEL"), ("lt_tuning", "TUNING")]),
    ("MT", [("mt_level", "LEVEL"), ("mt_tuning", "TUNING")]),
    ("HT", [("ht_level", "LEVEL"), ("ht_tuning", "TUNING")]),
    ("RS", [("rs_level", "LEVEL")]),
    ("CP", [("cp_level", "LEVEL")]),
    ("CB", [("cb_level", "LEVEL")]),
    ("CY", [("cy_level", "LEVEL"), ("cy_tone", "TONE"), ("cy_decay", "DECAY")]),
    ("OH", [("oh_level", "LEVEL"), ("oh_decay", "DECAY")]),
    ("CH", [("ch_level", "LEVEL")]),
    ("VOLUME", [("volume", "MASTER")]),
]
COL_X0, COL_W = 16, 96          # skin_post.py draws the column headings from these
ROWS = [199, 333, 467]
KEY_X0, KEY_PITCH, KEY_CY, KEY_W, KEY_H = 16, 78, 620, 72, 76

VOICES = ["bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb", "cy", "oh", "ch", "lc", "mc", "hc", "cl", "ma"]
NAMES = {"bd": "Bass Drum", "sd": "Snare Drum", "lt": "Low Tom", "mt": "Mid Tom", "ht": "Hi Tom", "rs": "Rim Shot",
         "cp": "Hand Clap", "cb": "Cowbell", "cy": "Cymbal", "oh": "Open Hat", "ch": "Closed Hat", "lc": "Low Conga",
         "mc": "Mid Conga", "hc": "Hi Conga", "cl": "Claves", "ma": "Maracas"}

# The defaults: engine/Boom8.h Controls.
DEFAULTS = dict(accent=60, volume=0, bd_level=80, bd_tone=45, bd_decay=55, sd_level=70, sd_tone=50, sd_snappy=60,
                lt_level=70, lt_tuning=50, mt_level=70, mt_tuning=50, ht_level=70, ht_tuning=50, rs_level=70,
                cp_level=70, cb_level=60, cy_level=60, cy_tone=50, cy_decay=60, oh_level=65, oh_decay=40,
                ch_level=65, velocity=40, bd_tune=0)


def key_cx(i):
    return KEY_X0 + KEY_PITCH // 2 + KEY_PITCH * i


def params():
    names = dict(accent="Accent", volume="Volume", velocity="Velocity", bd_tune="BD Tune")
    for v in VOICES[:11]:
        for what in ("level", "tone", "decay", "snappy", "tuning"):
            names["%s_%s" % (v, what)] = "%s %s" % (v.upper(), what.capitalize())
    out = []
    for k, d in DEFAULTS.items():
        lo, hi = (-24, 6) if k == "volume" else (-12, 12) if k == "bd_tune" else (0, 100)
        out.append({"key": k, "name": names[k], "min": lo, "max": hi, "default": d, "dynamic_display": True})
    out.append({"key": "choke", "name": "Hat Choke", "options": ["Off", "On"], "default": 1})
    out.append({"key": "learn", "name": "Pad Learn", "options": ["Off", "On"], "default": 0})
    out.append({"key": "pads_clear", "name": "Clear Pads", "min": 0, "max": 1, "momentary": True, "hold_ms": 120})
    out.append({"key": "pads_info", "name": "Pads", "min": 0, "max": 1, "display": "string", "type": "readout"})
    for v in VOICES:
        out.append({"key": "hit_" + v, "name": "Play " + NAMES[v], "min": 0, "max": 1, "momentary": True, "hold_ms": 60})
    sections = [
        {"label": "Drums", "keys": ["accent"] + [k for k in DEFAULTS if k[:2] in ("bd", "sd", "lt", "mt", "ht")]},
        {"label": "Percussion", "keys": [k for k in DEFAULTS if k[:2] in ("rs", "cp", "cb")]},
        {"label": "Metal", "keys": [k for k in DEFAULTS if k[:2] in ("cy", "oh", "ch")] + ["choke"]},
        {"label": "Master", "keys": ["volume", "accent", "velocity", "bd_tune", "choke"]},
        {"label": "Setup", "keys": ["learn", "pads_clear", "pads_info"]},
    ]
    return {"name": "BOOM8", "params": out, "sections": sections}


def layout():
    L = []
    w = L.append
    w("# BOOM8 for MPC OS touchscreens (MPC Live III and other 1280x800 units).")
    w("# Plugin area: x 0..1280, y 86..714 (tools/shadow_skin.py). Written by")
    w("# gen_layout.py; skin/skin_post.py paints the panel round these controls.")
    for k, v in dict(theme_bg="1c1c1e", theme_ink="f1ead8", theme_ink_dim="b9b1a0", theme_accent="f08a24",
                     theme_accent_hi="f6c13a", theme_seg_active="f08a24", theme_seg_inactive="2a2a2c",
                     theme_seg_active_tx="1c1c1e", theme_lcd="0e0e0f", theme_knob_dot="e8402a",
                     theme_btn_on="e8402a").items():
        w("%s=%s" % (k, v))
    w("qlinks_track = bd_level,bd_tone,bd_decay,sd_tone,sd_snappy,cy_decay,oh_decay,volume")
    w("")
    w("[tab MACHINE]")
    w("# Thirteen columns as on the hardware's panel: ACCENT, the eleven instruments")
    w("# (the tom, rim shot and clap columns also set the congas, claves and")
    w("# maracas), and the master VOLUME. The keys below play the sixteen sounds.")
    for i, (_, knobs) in enumerate(COLS):
        cx = COL_X0 + COL_W // 2 + COL_W * i
        for (key, label), cy in zip(knobs, ROWS):
            w("knob    cx=%d cy=%d r=32 label=\"%s\" key=%s bw=92 ns=14 vs=18" % (cx, cy, label, key))
    for i, v in enumerate(VOICES):
        w("button  cx=%d cy=%d w=%d h=%d label=\"%s\" key=hit_%s" % (key_cx(i), KEY_CY, KEY_W, KEY_H, v.upper(), v))
    w("qlinks \"DRUMS\" = bd_level,bd_tone,bd_decay,sd_level,sd_tone,sd_snappy,accent,volume")
    w("qlinks \"TOMS\" = lt_level,lt_tuning,mt_level,mt_tuning,ht_level,ht_tuning,rs_level,cp_level")
    w("qlinks \"METAL\" = cb_level,cy_level,cy_tone,cy_decay,oh_level,oh_decay,ch_level,volume")
    w("")
    w("[tab SETUP]")
    w("# PAD LEARN: tap it, then hit the pads you want for the sixteen sounds in the")
    w("# order of the instrument keys (BD to MA); it finishes after the sixteenth.")
    w("# CLEAR PADS goes back to General MIDI drum notes.")
    w("frame   x=16 y=96 w=1248 h=300 title=\"PADS\"")
    w("toggle  cx=400 cy=230 label=\"PAD LEARN\" key=learn bw=400")
    w("button  cx=880 cy=230 w=400 h=90 label=\"CLEAR PADS\" key=pads_clear")
    w("readout cx=640 cy=330 w=1180 h=52 label=\"\" key=pads_info tsize=19 tcolor=f6c13a")
    w("frame   x=16 y=412 w=1248 h=296 title=\"EXTRAS\"")
    w("knob    cx=200 cy=560 r=54 label=\"VELOCITY\" key=velocity bw=140 ns=20 vs=26")
    w("knob    cx=420 cy=560 r=54 label=\"BD TUNE\" key=bd_tune bw=140 ns=20 vs=26")
    w("# HAT CHOKE: the closed hat cuts the open hat (skin/skin_post.py draws the key).")
    w("toggle  cx=700 cy=560 label=\"HAT CHOKE\" key=choke bw=240")
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    json.dump(params(), open(os.path.join(HERE, "params.json"), "w"), indent=1)
    open(os.path.join(HERE, "layout.conf"), "w").write(layout())
