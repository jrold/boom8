"""Writes params.json and layout.conf: the plug-in's parameters and where each
control sits on the MPC's screen. Run it after changing either:

    python3 gen_layout.py

The BOOM machines share this file's code; the MACHINE block at the top is
what makes this one BOOM8.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- MACHINE

NAME = "BOOM8"
PLATE_LINES = ("ALL", "ANALOG", "", "16", "VOICES")   # small print on the name plate

# Two pages of columns, one per instrument, as the hardware's panel reads:
# each column's abbreviation is its heading (a tom's column also sets its
# conga, the rim shot's the claves, the clap's the maracas, as on the
# hardware). MACHINE: the sound knobs top to bottom; the first column also
# carries the name plate. MIX: a LEVEL fader per instrument with its PAN knob
# under it, and the MASTER fader with the name plate under it.
MACHINE = [
    ("AC", ["accent"]),
    ("BD", ["bd_tune", "bd_tone", "bd_decay"]),
    ("SD", ["sd_tone", "sd_snappy", "sd_decay"]),
    ("LT\u00b7LC", ["lt_tuning"]),
    ("MT\u00b7MC", ["mt_tuning"]),
    ("HT\u00b7HC", ["ht_tuning"]),
    ("CP\u00b7MA", ["cp_decay"]),
    ("CY", ["cy_tone", "cy_decay"]),
    ("OH", ["oh_decay"]),
]
MIX = [
    ("BD", ["bd_level", "bd_pan"]),
    ("SD", ["sd_level", "sd_pan"]),
    ("LT\u00b7LC", ["lt_level", "lt_pan"]),
    ("MT\u00b7MC", ["mt_level", "mt_pan"]),
    ("HT\u00b7HC", ["ht_level", "ht_pan"]),
    ("RS\u00b7CL", ["rs_level", "rs_pan"]),
    ("CP\u00b7MA", ["cp_level", "cp_pan"]),
    ("CB", ["cb_level", "cb_pan"]),
    ("CY", ["cy_level", "cy_pan"]),
    ("OH", ["oh_level", "oh_pan"]),
    ("CH", ["ch_level", "ch_pan"]),
    ("MST", ["volume"]),
]

# The knobs and faders, in params.json order (it never changes once
# released; params.lock checks), and their defaults: engine/Boom8.h Controls.
DEFAULTS = dict(accent=60, volume=0, bd_level=80, bd_tone=45, bd_decay=55, sd_level=70, sd_tone=50, sd_snappy=60,
                lt_level=70, lt_tuning=50, mt_level=70, mt_tuning=50, ht_level=70, ht_tuning=50, rs_level=70,
                cp_level=70, cb_level=60, cy_level=60, cy_tone=50, cy_decay=60, oh_level=65, oh_decay=40,
                ch_level=65, velocity=40, bd_tune=0)
TOGGLES = [("choke", "Hat Choke", 1)]   # key, name, default: on SETUP
# Added after the first release, after the others.
NEW = dict(sd_decay=50, cp_decay=50, bd_pan=50, sd_pan=50, lt_pan=50, mt_pan=50, ht_pan=50, rs_pan=50, cp_pan=50,
           cb_pan=50, cy_pan=50, oh_pan=50, ch_pan=50)
SWITCHES = []   # switches between options on SETUP (key, name, options, default, caption)
RANGES = {"volume": (-24, 6), "bd_tune": (-12, 12)}   # any knob not 0..100

# The labels painted under the knobs (the instrument is in the heading above);
# a pan knob's is PAN.
LABELS = dict(accent="ACCENT", bd_tune="TUNE", bd_tone="TONE", bd_decay="DECAY", sd_tone="TONE", sd_snappy="SNAPPY",
              sd_decay="DECAY", lt_tuning="TUNING", mt_tuning="TUNING", ht_tuning="TUNING", cp_decay="DECAY",
              cy_tone="TONE", cy_decay="DECAY", oh_decay="DECAY")
# The names MPC shows (Q-Links, automation) where "BD Tune"-style won't do.
PNAMES = dict(accent="Accent", volume="Volume", velocity="Velocity")
TOGGLE_CAPTIONS = {}   # SETUP key captions, where not the name in capitals

THEME = dict(theme_bg="2c2c2e", theme_ink="ece4ce", theme_ink_dim="968f80", theme_accent="ee7e22",
             theme_accent_hi="f6c13a", theme_seg_active="ee7e22", theme_seg_inactive="2a2a2c",
             theme_seg_active_tx="1c1c1e", theme_lcd="0e0e0f", theme_knob_dot="d63428", theme_btn_on="d63428")
QLINKS_TRACK = "bd_tone,bd_decay,sd_tone,sd_snappy,sd_decay,cy_decay,bd_level,volume"
PADS_NOTE = ("the sixteen sounds in the order BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, LC, MC, HC, CL, MA;"
             " it finishes after the sixteenth")
PADS_READOUT_INK = "f6c13a"

# ---------------------------------------------------------------- end of MACHINE

PAGES = {"MACHINE": MACHINE, "MIX": MIX}
PLATE = {"MACHINE": (0, 331), "MIX": (len(MIX) - 1, 532)}   # page -> (column, top y) of the name plate
X0, WIDTH = 16, 1248   # skin_post.py draws the headings from these
ROWS = [195, 331, 467, 603]
KNOB_R, KNOB_BW, VALUE_PX = 34, 100, 26
FADER_CY, FADER_W, FADER_H = 300, 56, 300   # MIX: the faders, their pan knobs below
PAN_CY = 560
LABEL_GAP = 20   # px between a knob and its value, for the painted label
SWITCH_W = 200   # SETUP: a switch's option keys' width
DOT_R = 6        # the hit dots' knob radius (no other knob may use it; skin_post.py repaints them)
HEAD_CY = 111    # the middle of the headings' capitals (skin_post.py draws the text at HEAD_Y + 11)
LABELS.update({k: "PAN" for k in list(DEFAULTS) + list(NEW) if k.endswith("_pan")})
# SETUP's on/off keys (skin_post.py draws them): key -> (caption, w, h).
BIG_KEYS = {"learn": ("PAD LEARN", 400, 70), "midi_learn": ("MIDI LEARN", 400, 70)}
BIG_KEYS.update({k: (TOGGLE_CAPTIONS.get(k, name.upper()), 240, 80) for k, name, _ in TOGGLES})


def col_w(page):
    return WIDTH / len(PAGES[page])


def col_cx(page, i):
    return round(X0 + col_w(page) * (i + 0.5))


def act_key(page, i):
    """The hit dot parameter of column i of a page: act_<voice> for an
    instrument (MACHINE finds it by the MIX column with the same heading), or
    None (ACCENT, MASTER)."""
    title, keys = PAGES[page][i]
    if page != "MIX":
        same = [j for j, (t, _) in enumerate(MIX) if t == title]
        return act_key("MIX", same[0]) if same else None
    return "act_" + keys[0][:-len("_level")] if keys[0].endswith("_level") else None


ACTS = [k for k in (act_key("MIX", i) for i in range(len(MIX))) if k]


def pname(k):
    if k in PNAMES:
        return PNAMES[k]
    v, what = k.split("_")
    return "%s %s" % (v.upper(), what.capitalize())


def params():
    def knob(k, d):
        lo, hi = RANGES.get(k, (0, 100))
        return {"key": k, "name": pname(k), "min": lo, "max": hi, "default": d, "dynamic_display": True}

    out = [knob(k, d) for k, d in DEFAULTS.items()]
    out += [{"key": k, "name": name, "options": ["Off", "On"], "default": d} for k, name, d in TOGGLES]
    out.append({"key": "learn", "name": "Pad Learn", "options": ["Off", "On"], "default": 0})
    out.append({"key": "pads_clear", "name": "Clear Pads", "min": 0, "max": 1, "momentary": True, "hold_ms": 120})
    out.append({"key": "pads_info", "name": "Pads", "min": 0, "max": 1, "display": "string", "type": "readout"})
    out += [knob(k, d) for k, d in NEW.items()]
    out += [{"key": k, "name": name, "options": opts, "default": d} for k, name, opts, d, _ in SWITCHES]
    # The hit dots, set by the engine (it reports them; nothing else does).
    out += [{"key": k, "name": "%s Hit" % k[4:].upper(), "min": 0, "max": 1, "default": 0, "display": "int"}
            for k in ACTS]
    # MIDI LEARN (SETUP's MIDI section).
    out.append({"key": "midi_learn", "name": "MIDI Learn", "options": ["Off", "On"], "default": 0})
    out.append({"key": "midi_clear", "name": "Clear MIDI", "min": 0, "max": 1, "momentary": True, "hold_ms": 120})
    out.append({"key": "midi_clear_all", "name": "Clear All MIDI", "min": 0, "max": 1, "momentary": True, "hold_ms": 120})
    out.append({"key": "midi_info", "name": "MIDI", "min": 0, "max": 1, "display": "string", "type": "readout"})
    mix = [k for _, keys in MIX for k in keys]
    sound = [k for _, keys in MACHINE for k in keys]
    missing = set(DEFAULTS) | set(NEW)
    missing -= set(sound) | set(mix) | {"velocity"}
    if missing:
        raise SystemExit("gen_layout: on no page: %s" % ", ".join(sorted(missing)))
    sections = [
        {"label": "Sound", "keys": sound},
        {"label": "Mix", "keys": mix},
        {"label": "Setup", "keys": ["velocity"] + [k for k, _, _ in TOGGLES] + [k for k, *_ in SWITCHES]
         + ["learn", "pads_clear", "pads_info", "midi_learn", "midi_clear", "midi_clear_all", "midi_info"]},
    ]
    return {"name": NAME, "params": out, "sections": sections}


def hit_dots(page, w):
    """A dot per instrument column that lights when a pad hits it: a tiny
    display-only knob (skin_post.py takes its touch away, paints it and moves
    it left of the heading's text)."""
    w("# Hit dots: the engine lights act_<voice> for a moment on each hit.")
    for i in range(len(PAGES[page])):
        k = act_key(page, i)
        if k:
            w("knob    cx=%d cy=%d r=%d label=\"\" key=%s bw=%d ns=0 vs=0" % (col_cx(page, i), HEAD_CY, DOT_R, k, 2 * DOT_R + 10))


def layout():
    L = []
    w = L.append
    w("# %s for MPC OS touchscreens (MPC Live III and other 1280x800 units)." % NAME)
    w("# Plugin area: x 0..1280, y 86..714 (tools/shadow_skin.py). Written by")
    w("# gen_layout.py; skin/skin_post.py paints the panel round these controls.")
    for k, v in THEME.items():
        w("%s=%s" % (k, v))
    # The track's Q-Links. No per-page banks: MPC shows a page with banks as
    # dots under its tab and names the tab after the chosen bank.
    w("qlinks_track = %s" % QLINKS_TRACK)
    w("")
    # ns=0: MPC draws control names small whatever size the skin asks for, so
    # skin_post.py paints the labels into the panel; vy= leaves them room.
    knob = "knob    cx=%d cy=%d r=%d label=\"%s\" key=%s bw=%d ns=0 vs=%d vy=%d"
    w("[tab MACHINE]")
    w("# A column per instrument with its sound knobs top to bottom. The levels")
    w("# are on MIX; the pads play the sounds.")
    for i, (_, keys) in enumerate(MACHINE):
        for key, cy in zip(keys, ROWS):
            w(knob % (col_cx("MACHINE", i), cy, KNOB_R, LABELS[key], key, KNOB_BW, VALUE_PX, LABEL_GAP))
    hit_dots("MACHINE", w)
    w("")
    w("[tab MIX]")
    w("# A LEVEL fader and a PAN knob per instrument, and the MASTER fader.")
    for i, (_, keys) in enumerate(MIX):
        cx = col_cx("MIX", i)
        w("slider_v cx=%d cy=%d w=%d h=%d label=\"\" key=%s bw=%d ns=0 vs=%d"
          % (cx, FADER_CY, FADER_W, FADER_H, keys[0], KNOB_BW, VALUE_PX))
        if len(keys) > 1:
            w(knob % (cx, PAN_CY, KNOB_R, LABELS[keys[1]], keys[1], KNOB_BW, VALUE_PX, LABEL_GAP))
    hit_dots("MIX", w)
    w("")
    w("[tab SETUP]")
    w("# PAD LEARN: tap it, then hit the pads you want for %s." % PADS_NOTE)
    w("# CLEAR PADS goes back to General MIDI drum notes.")
    w("frame   x=16 y=96 w=1248 h=196 title=\"PADS\"")
    w("toggle  cx=400 cy=180 label=\"PAD LEARN\" key=learn bw=400")
    w("button  cx=880 cy=180 w=400 h=70 label=\"CLEAR PADS\" key=pads_clear")
    w("readout cx=640 cy=257 w=1180 h=46 label=\"\" key=pads_info tsize=19 tcolor=%s" % PADS_READOUT_INK)
    w("# MIDI LEARN: tap it (it stays lit), nudge a knob or fader on MACHINE or MIX,")
    w("# then move a control on the MIDI controller: that CC, on that channel, now")
    w("# moves it. Nudge another, or tap MIDI LEARN again to finish. CLEAR forgets")
    w("# the control nudged or learned last, CLEAR ALL every mapping. Mappings are")
    w("# saved with the project.")
    w("frame   x=16 y=300 w=1248 h=196 title=\"MIDI\"")
    w("toggle  cx=400 cy=384 label=\"MIDI LEARN\" key=midi_learn bw=400")
    w("button  cx=790 cy=384 w=300 h=70 label=\"CLEAR\" key=midi_clear")
    w("button  cx=1100 cy=384 w=300 h=70 label=\"CLEAR ALL\" key=midi_clear_all")
    w("readout cx=640 cy=461 w=1180 h=46 label=\"\" key=midi_info tsize=19 tcolor=%s" % PADS_READOUT_INK)
    w("frame   x=16 y=504 w=1248 h=204 title=\"EXTRAS\"")
    w("knob    cx=200 cy=594 r=44 label=\"VELOCITY\" key=velocity bw=140 ns=20 vs=26")
    w("# The on/off extras (skin/skin_post.py draws them as keys).")
    for i, (k, _, _) in enumerate(TOGGLES):
        w("toggle  cx=%d cy=604 label=\"%s\" key=%s bw=240" % (520 + 300 * i, BIG_KEYS[k][0], k))
    # The switches after them (skin_post.py draws the options as keys and the
    # caption over them).
    x = 520 + 300 * len(TOGGLES) - 120 + 40   # 40 px right of the last toggle key
    for k, _, opts, _, caption in SWITCHES:
        width = len(opts) * (SWITCH_W + 2) - 2
        w("enum_h  cx=%d cy=604 sw=%d sh=80 label=\"%s\" key=%s options=%s"
          % (x + width // 2, SWITCH_W, caption, k, ",".join(opts)))
        x += width + 40
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    json.dump(params(), open(os.path.join(HERE, "params.json"), "w"), indent=1)
    open(os.path.join(HERE, "layout.conf"), "w").write(layout())
