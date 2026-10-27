"""The BOOM family's look, drawn with Pillow; BOOM8's colours.

BOOM6, BOOM8 and BOOM9 share this file line for line except the THEME block
below: the panel, lettering, accents and knob caps. BOOM8 is in the colours
of the classic 1980 rhythm machine: a charcoal panel with cream lettering,
cream knob and fader caps, and the bars over the instruments in its red,
orange, yellow and cream.

Everything is drawn at SS times its size and scaled down for smooth edges.
skin_post.py places it all.
"""
import math
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 2

# ---------------------------------------------------------------- THEME
# Everything that differs between the machines.

RED, ORANGE, YELLOW, CREAM = (214, 52, 40), (238, 126, 34), (242, 196, 24), (236, 228, 206)
ACCENT = ORANGE                       # knob arcs, lit fader slots, lamps
VALUE_INK = (246, 193, 58)            # values in the display windows
INK = CREAM                           # lettering and scale ticks on the panel
INK_DIM = (150, 144, 128)
RULE = (78, 78, 80)                   # the lines between columns
PANEL_STOPS = [(0, (50, 50, 52)), (1, (36, 36, 38))]          # the page, top to bottom
SECTION_STOPS = [(0, (40, 40, 42)), (1, (33, 33, 35))]        # SETUP's framed areas
TEXTURE = "grain"                     # the panel's surface: "grain" (paint) or "brushed" (metal)
KEY = CREAM                           # key caps
TRACK = (22, 22, 22)                  # knob arcs' and fader slots' unlit rim
CAP = "cream"                         # knob and fader caps: "black", "cream" or "metal"
POINTER = (28, 26, 24)                # the line on knob and fader caps


def bar_colour(i, n):
    """The bar under column i of n's heading: the original's four key
    colours, left to right."""
    return (RED, ORANGE, YELLOW, CREAM)[min(3, i * 4 // n)]


# ---------------------------------------------------------------- end of THEME

INK_DARK = (30, 28, 26)               # lettering on light keys

# Knob travel: -0.8 pi to 0.8 pi, from the bottom-left round to the bottom-right.
ANGLE_START, ANGLE_END = -0.8 * math.pi, 0.8 * math.pi

FONT_BOLD = FONT_SEMI = None


def use_fonts(bold, semi):
    global FONT_BOLD, FONT_SEMI
    FONT_BOLD, FONT_SEMI = bold, semi


def font(px, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT_SEMI, int(round(px * SS)))


# ---------------------------------------------------------------- helpers

def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def vertical(w, h, stops):
    """RGB vertical gradient from [(position 0..1, colour), ...]."""
    col = Image.new("RGB", (1, h))
    px = col.load()
    for y in range(h):
        t = y / max(1, h - 1)
        for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
            if p0 <= t <= p1:
                px[0, y] = lerp(c0, c1, (t - p0) / (p1 - p0) if p1 > p0 else 0)
                break
        else:
            px[0, y] = stops[-1][1]
    return col.resize((w, h))


def grain(w, h, seed, strength):
    """Fine paint texture as an L image centred on 128."""
    random.seed(seed)
    fine = Image.effect_noise((w, h), 24).filter(ImageFilter.BoxBlur(1))
    return fine.point(lambda v: int(128 + (v - 128) * strength))


def apply_texture(img, tex):
    rgb = Image.merge("RGB", [tex, tex, tex])
    return ImageChops.add(img.convert("RGB"), rgb, 1.0, -128)


def shadow(size, box, radius, blur, alpha, ellipse=False):
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    (d.ellipse if ellipse else (lambda b, fill: d.rounded_rectangle(b, radius, fill=fill)))(box, fill=int(255 * alpha))
    m = m.filter(ImageFilter.GaussianBlur(blur))
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.putalpha(m)
    return out


def glow(size, box, colour, blur, alpha, ellipse=False, radius=0):
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    if ellipse:
        d.ellipse(box, fill=int(255 * alpha))
    else:
        d.rounded_rectangle(box, radius, fill=int(255 * alpha))
    m = m.filter(ImageFilter.GaussianBlur(blur))
    out = Image.new("RGBA", size, colour + (0,))
    out.putalpha(m)
    return out


def rounded_mask(w, h, r):
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=255)
    return m


def spaced(d, xy, text, f, fill, spacing, anchor="lm"):
    """Letter-spaced text; anchor "mm" centres the whole run on xy."""
    x, y = xy
    if anchor in ("mm", "rm"):
        width = sum(d.textlength(c, font=f) for c in text) + spacing * (len(text) - 1)
        x -= width / 2 if anchor == "mm" else width
        anchor = "lm"
    for c in text:
        d.text((x, y), c, font=f, fill=fill, anchor=anchor)
        x += d.textlength(c, font=f) + spacing
    return x


# ---------------------------------------------------------------- page parts

def brushed(w, h, seed, strength):
    """Horizontal brushed-metal streaks as an L image centred on 128."""
    random.seed(seed)
    noise = Image.effect_noise((max(1, w // 6), h), 60)
    streak = noise.resize((w, h), Image.Resampling.BILINEAR).filter(ImageFilter.BoxBlur(1))
    fine = Image.effect_noise((w, h), 20)
    out = ImageChops.add(streak, fine, 2.0, 0)
    return out.point(lambda v: int(128 + (v - 128) * strength))


def surface(w, h, stops, seed):
    """A stretch of panel in the THEME's colours and texture."""
    img = vertical(w, h, stops)
    tex = brushed(w, h, seed, 0.09) if TEXTURE == "brushed" else grain(w, h, seed, 0.07)
    return apply_texture(img, tex)


def plate(w, h):
    """The page behind everything: the panel."""
    return surface(w * SS, h * SS, PANEL_STOPS, 11).convert("RGBA")


def section(img, box, title=None):
    """A framed area of the panel: an inset with a fine rule and its title in
    capitals over an accent bar."""
    x0, y0, x1, y1 = (int(v * SS) for v in box)
    w, h = x1 - x0, y1 - y0
    r = 4 * SS
    img.paste(surface(w, h, SECTION_STOPS, x0 + y0), (x0, y0), rounded_mask(w, h, r))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x0, y0, x1 - 1, y1 - 1), r, outline=INK_DIM + (255,), width=SS)
    if title:
        f = font(22, bold=True)
        tx = x0 + 22 * SS
        end = spaced(d, (tx, y0 + 26 * SS), title.upper(), f, INK + (255,), 3 * SS)
        bar(img, (end + 14 * SS) / SS, (y0 + 22 * SS) / SS, (end + 14 * SS) / SS + 120, (y0 + 30 * SS) / SS, ACCENT)


def bar(img, x0, y0, x1, y1, colour):
    ImageDraw.Draw(img).rectangle((int(x0 * SS), int(y0 * SS), int(x1 * SS) - 1, int(y1 * SS)), fill=colour + (255,))


def heading_font(title, x0, x1):
    """The heading's font, shrunk to fit its column, and its text's width in
    page pixels (letter-spaced as heading() draws it)."""
    d = ImageDraw.Draw(Image.new("L", (1, 1)))
    px = 26
    f = font(px, bold=True)
    while d.textlength(title, font=f) + 2 * SS * (len(title) - 1) > (x1 - x0 - 10) * SS and px > 11:
        px -= 1
        f = font(px, bold=True)
    return f, (d.textlength(title, font=f) + 2 * SS * (len(title) - 1)) / SS


def heading(img, x0, x1, y, title, names=(), colour=None):
    """An instrument's name over its column: capitals over a coloured bar
    (and any full name small, under the bar)."""
    d = ImageDraw.Draw(img)
    cx = (x0 + x1) / 2
    f, _ = heading_font(title, x0, x1)
    spaced(d, (cx * SS, (y + 11) * SS), title, f, INK + (255,), 2 * SS, anchor="mm")
    bar(img, x0 + 5, y + 27, x1 - 5, y + 32, colour or ACCENT)
    for i, n in enumerate(names):
        spaced(d, (cx * SS, (y + 39 + 11 * i) * SS), n.upper(), font(10, bold=True), INK_DIM + (255,), SS, anchor="mm")


def rule(img, x0, y0, x1, y1, colour=RULE, width=1):
    d = ImageDraw.Draw(img)
    d.line((x0 * SS, y0 * SS, x1 * SS, y1 * SS), fill=colour + (255,), width=width * SS)


def label(img, x, y, text, size, colour=INK, anchor="mm", bold=True, spacing=0):
    d = ImageDraw.Draw(img)
    if spacing:
        spaced(d, (x * SS, y * SS), text, font(size, bold), colour + (255,), spacing * SS, anchor=anchor)
    else:
        d.text((x * SS, y * SS), text, font=font(size, bold), fill=colour + (255,), anchor=anchor)


def well(img, box, dividers=(), radius=4):
    """A small dark display window for a value."""
    x0, y0, x1, y1 = (int(v * SS) for v in box)
    r = radius * SS
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((x0 - SS, y0 - SS, x1 + SS, y1 + SS), r + SS, fill=(18, 18, 18, 255))
    d.rounded_rectangle((x0, y0, x1, y1), r, fill=(12, 10, 8, 255))
    d.rounded_rectangle((x0, y0, x1, y1), r, outline=(70, 62, 50, 255), width=SS)
    for dy in dividers:
        yy = int(dy * SS)
        d.line((x0 + 4 * SS, yy, x1 - 4 * SS, yy), fill=(70, 62, 50, 255), width=SS)


def knob_base(img, cx, cy, frame):
    """What sits round a knob on the panel: its shadow and the 0..10 scale
    ticks just outside the filmstrip (cream, the ends a little longer)."""
    cx, cy = cx * SS, cy * SS
    rb = frame * 0.40 * SS
    img.alpha_composite(shadow(img.size, (cx - rb, cy - rb + 4 * SS, cx + rb, cy + rb + 9 * SS), 0, 6 * SS, 0.45, True))
    d = ImageDraw.Draw(img)
    r0 = (frame / 2 + 2) * SS
    for i in range(11):
        a = ANGLE_START + i / 10 * (ANGLE_END - ANGLE_START)
        sx, sy = math.sin(a), -math.cos(a)
        r1 = r0 + (5 if i in (0, 5, 10) else 3) * SS
        d.line((cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1), fill=INK + (255,), width=int(1.4 * SS))


def key(w, h, label_text, lit, colour=KEY, led=True, size=None):
    """A key: a cap in the KEY colour with a lamp above its caption that
    lights in the accent. The body leaves a margin for the glow in both
    states so the two images line up."""
    s = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    m = 4 * SS
    box = (m, m, w * SS - m, h * SS - m - SS)
    bw, bh = box[2] - box[0], box[3] - box[1]
    r = 4 * SS
    hi = lerp(colour, (255, 255, 255), 0.30)
    lo = lerp(colour, (0, 0, 0), 0.22)
    if lit:   # pressed: a touch lighter, with a halo
        s.alpha_composite(glow(s.size, (box[0] - SS, box[1] - SS, box[2] + SS, box[3] + SS), ACCENT, 3 * SS, 0.6, radius=r))
        hi, colour, lo = lerp(hi, (255, 255, 255), 0.35), lerp(colour, (255, 255, 255), 0.25), colour
    s.alpha_composite(shadow(s.size, (box[0], box[1] + 2 * SS, box[2], box[3] + 3 * SS), r, 2 * SS, 0.8))
    body = vertical(bw, bh, [(0, hi), (0.18, colour), (1, lo)])
    s.paste(body, (box[0], box[1]), rounded_mask(bw, bh, r))
    d = ImageDraw.Draw(s)
    d.rounded_rectangle(box, r, outline=lerp(lo, (0, 0, 0), 0.4) + (255,), width=SS)
    d.line((box[0] + r, box[1] + SS * 1.5, box[2] - r, box[1] + SS * 1.5), fill=(255, 255, 255, 150), width=SS)

    dark_text = sum(colour) / 3 > 150
    ink = INK_DARK if dark_text else (255, 250, 240)
    ty = (box[1] + box[3]) / 2
    if led:   # a small lamp: dark red, or lit
        lx, ly = (box[0] + box[2]) / 2, box[1] + 11 * SS
        lr = 4 * SS
        if lit:
            s.alpha_composite(glow(s.size, (lx - 3 * lr, ly - 3 * lr, lx + 3 * lr, ly + 3 * lr), ACCENT, 3 * SS, 0.9, True))
        lamp = lerp(ACCENT, (255, 255, 255), 0.3) if lit else lerp(ACCENT, (0, 0, 0), 0.55)
        d.ellipse((lx - lr, ly - lr, lx + lr, ly + lr), fill=lamp + (255,), outline=lerp(ACCENT, (0, 0, 0), 0.8) + (255,),
                  width=SS)
        ty += 7 * SS
    if label_text:
        px = size or min(26, h * 0.34)
        f = font(px, bold=True)
        while d.textlength(label_text, font=f) > bw - 14 * SS and px > 11:
            px -= 1
            f = font(px, bold=True)
        shade = (255, 255, 255, 120) if dark_text else (0, 0, 0, 120)
        d.text(((box[0] + box[2]) / 2, ty + SS), label_text, font=f, fill=shade, anchor="mm")
        d.text(((box[0] + box[2]) / 2, ty), label_text, font=f, fill=ink + (255,), anchor="mm")
    return s.resize((w, h), Image.Resampling.LANCZOS)


def arrow_key(w, h, direction):
    s = key(w, h, "", False).resize((w * SS, h * SS), Image.Resampling.LANCZOS)
    d = ImageDraw.Draw(s)
    cx, cy = w * SS / 2, h * SS / 2 - SS
    a = min(w, h) * 0.18 * SS
    pts = [(cx - a * 0.7, cy - a), (cx - a * 0.7, cy + a), (cx + a * 0.9, cy)] if direction > 0 else \
          [(cx + a * 0.7, cy - a), (cx + a * 0.7, cy + a), (cx - a * 0.9, cy)]
    d.polygon(pts, fill=INK_DARK + (255,))
    return s.resize((w, h), Image.Resampling.LANCZOS)


def tile(w, h, lit):
    """A browser row: a dark slot, or an accent-edged one for the lit row."""
    s = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(s)
    r = 5 * SS
    box = (SS, SS, w * SS - SS, h * SS - SS)
    body = vertical(box[2] - box[0], box[3] - box[1],
                    [(0, (60, 40, 20)), (1, (36, 24, 12))] if lit else [(0, (34, 34, 36)), (1, (24, 24, 26))])
    s.paste(body, (box[0], box[1]), rounded_mask(body.width, body.height, r))
    d.rounded_rectangle(box, r, outline=ACCENT + (255,) if lit else (8, 8, 8, 255), width=SS * (2 if lit else 1))
    return s.resize((w, h), Image.Resampling.LANCZOS)


def cap_shade(x, y, rf):
    """The cap's colour at (x, y) from its centre, by the THEME's CAP: a
    domed black or cream cap, or spun metal whose sheen turns with it."""
    r = math.hypot(x, y)
    t = r / rf
    if CAP == "metal":
        sheen = abs(math.cos(math.atan2(y, x) - math.pi * 0.25)) ** 10
        v = int(max(0, min(255, 150 + 5 * math.sin(r * 1.7) + 80 * sheen - 22 * t)))
        return (v, v, max(0, v - 2))
    if CAP == "cream":
        k = 1.0 - 0.18 * t * t
        return tuple(int(v * k) for v in (236, 228, 206))
    v = int(44 - 18 * t * t)
    return (v, v, v)


def knob_cap(s, c, rk, rf):
    """The part of the knob that turns, drawn pointing up: a ribbed skirt and
    the THEME's cap, with a pointer line from the centre."""
    cap = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(cap)
    d.ellipse((c - rk - 2 * SS, c - rk - 2 * SS, c + rk + 2 * SS, c + rk + 2 * SS), fill=(4, 4, 4, 255))
    d.ellipse((c - rk, c - rk, c + rk, c + rk), fill=(70, 70, 70, 255))
    ridges = 40
    for i in range(ridges):
        for half, col in ((0.0, (120, 120, 118)), (0.5, (40, 40, 40))):
            a = (i + half) / ridges * 2 * math.pi
            sx, sy = math.cos(a), math.sin(a)
            d.line((c + sx * (rf + SS), c + sy * (rf + SS), c + sx * (rk - SS), c + sy * (rk - SS)),
                   fill=col + (255,), width=int(1.4 * SS))

    size = int(2 * rf)
    face = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = face.load()
    for y in range(size):
        dy = y + 0.5 - rf
        for x in range(size):
            dx = x + 0.5 - rf
            if math.hypot(dx, dy) > rf:
                continue
            px[x, y] = cap_shade(dx, dy, rf) + (255,)
    cap.alpha_composite(face, (int(c - rf), int(c - rf)))
    d.ellipse((c - rf, c - rf, c + rf, c + rf), outline=(10, 10, 10, 255), width=SS)

    # The pointer: a line from near the centre to the rim.
    d.line((c, c - rf * 0.15, c, c - rf + 2 * SS), fill=POINTER + (255,), width=int(3.5 * SS))
    return cap


def knob_light(s, c, rk):
    """Fixed lighting over the turning cap: a soft highlight from the top
    left and shade toward the bottom right."""
    light = glow((s, s), (c - rk * 0.9, c - rk * 0.95, c + rk * 0.1, c - rk * 0.1), (255, 255, 255), 6 * SS, 0.28, True)
    shade = glow((s, s), (c - rk * 0.1, c + rk * 0.05, c + rk * 1.0, c + rk * 1.0), (0, 0, 0), 8 * SS, 0.35, True)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse((c - rk, c - rk, c + rk, c + rk), fill=255)
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.alpha_composite(light)
    out.alpha_composite(shade)
    out.putalpha(ImageChops.multiply(out.getchannel("A"), mask))
    return out


def knob_strip(frame, frames):
    """The knob filmstrip: inside the accent-coloured value arc, the knob
    that turns with its value under fixed lighting. The scale ticks are on
    the page (knob_base), outside the frame."""
    s = frame * SS
    c = s / 2
    r_arc = c - 3 * SS        # value arc, at the frame's edge
    rk = c - 8 * SS           # skirt
    rf = rk - 5 * SS          # cap
    a0, a1 = math.degrees(ANGLE_START) - 90, math.degrees(ANGLE_END) - 90
    width = max(2, int(frame / 26)) * SS

    base = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(base).arc((c - r_arc, c - r_arc, c + r_arc, c + r_arc), a0, a1, fill=TRACK + (255,), width=width)
    cap = knob_cap(s, c, rk, rf)
    light = knob_light(s, c, rk)

    out = Image.new("RGBA", (s, s * frames), (0, 0, 0, 0))
    for i in range(frames):
        fr = base.copy()
        a = ANGLE_START + i / (frames - 1) * (ANGLE_END - ANGLE_START)
        if i > 0:
            ImageDraw.Draw(fr).arc((c - r_arc, c - r_arc, c + r_arc, c + r_arc), a0, math.degrees(a) - 90,
                                   fill=ACCENT + (255,), width=width)
        # PIL turns counter-clockwise; the knob turns clockwise as the value rises.
        fr.alpha_composite(cap.rotate(-math.degrees(a), resample=Image.Resampling.BICUBIC, center=(c, c)))
        fr.alpha_composite(light)
        out.paste(fr, (0, i * s))
    return out.resize((frame, frame * frames), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------- faders

FADER_CAP_H = 30


def fader_base(img, cx, cy, h):
    """What sits round a fader on the panel: its slot's shadow and a scale of
    ticks either side (0..10, the ends and middle longer)."""
    cx, top = cx * SS, (cy - h / 2) * SS
    d = ImageDraw.Draw(img)
    travel = (h - FADER_CAP_H) * SS
    y0 = top + FADER_CAP_H * SS / 2
    for i in range(11):
        y = y0 + travel * (1 - i / 10)
        long_ = i in (0, 5, 10)
        for side in (-1, 1):
            x0 = cx + side * 32 * SS
            x1 = x0 + side * (9 if long_ else 5) * SS
            d.line((x0, y, x1, y), fill=INK + (255,), width=int(1.4 * SS))


def fader_strip(w, h, frames):
    """The fader filmstrip, frames of w x h stacked top to bottom: a dark
    slot lit in the accent below the cap, and a cap like the knobs' with grip
    lines and the pointer line across it. Frame 0 is the bottom of the travel
    (as shadow_art)."""
    s_w, s_h = w * SS, h * SS
    cap_h = FADER_CAP_H * SS
    slot_w = 8 * SS
    cx = s_w / 2
    travel = s_h - cap_h

    base = Image.new("RGBA", (s_w, s_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(base)
    d.rounded_rectangle((cx - slot_w / 2 - SS, cap_h / 2 - 2 * SS, cx + slot_w / 2 + SS, s_h - cap_h / 2 + 2 * SS),
                        slot_w / 2, fill=TRACK + (255,))
    d.rounded_rectangle((cx - slot_w / 2, cap_h / 2 - SS, cx + slot_w / 2, s_h - cap_h / 2 + SS), slot_w / 2,
                        fill=(16, 16, 16, 255))

    cap_w = s_w - 6 * SS
    cap = Image.new("RGBA", (cap_w, cap_h), (0, 0, 0, 0))
    mid = cap_shade(0, 0, 1)
    body = vertical(cap_w, cap_h, [(0, lerp(mid, (255, 255, 255), 0.25)), (0.45, mid), (0.55, lerp(mid, (0, 0, 0), 0.25)),
                                   (1, lerp(mid, (255, 255, 255), 0.1))])
    cap.paste(body, (0, 0), rounded_mask(cap_w, cap_h, 4 * SS))
    cd = ImageDraw.Draw(cap)
    cd.rounded_rectangle((0, 0, cap_w - 1, cap_h - 1), 4 * SS, outline=(6, 6, 6, 255), width=SS)
    ridge = lerp(mid, (255, 255, 255), 0.3) if sum(mid) < 300 else lerp(mid, (0, 0, 0), 0.3)
    for gy in (cap_h * 0.22, cap_h * 0.32, cap_h * 0.68, cap_h * 0.78):   # grip ridges
        cd.line((5 * SS, gy, cap_w - 5 * SS, gy), fill=ridge + (255,), width=SS)
    cd.line((3 * SS, cap_h / 2, cap_w - 3 * SS, cap_h / 2), fill=POINTER + (255,), width=int(2.5 * SS))

    out = Image.new("RGBA", (s_w, s_h * frames), (0, 0, 0, 0))
    for k in range(frames):
        t = k / max(1, frames - 1)
        fr = base.copy()
        y = (1 - t) * travel   # the cap's top
        if t > 0:
            ImageDraw.Draw(fr).rounded_rectangle((cx - slot_w / 2 + SS, y + cap_h / 2, cx + slot_w / 2 - SS,
                                                  s_h - cap_h / 2), slot_w / 2, fill=ACCENT + (255,))
        fr.alpha_composite(shadow(fr.size, (3 * SS, y + 3 * SS, s_w - 3 * SS, y + cap_h + 4 * SS), 4 * SS, 2 * SS, 0.6))
        fr.alpha_composite(cap, (3 * SS, int(y)))
        out.paste(fr, (0, k * s_h))
    return out.resize((w, h * frames), Image.Resampling.LANCZOS)


# ---------------------------------------------------------------- hit dots

def dot_strip(frame, frames):
    """A hit dot's filmstrip (128 square frames, as a knob's): an unlit lamp
    in the first half, lit in the accent with a soft glow in the second, so
    a 0/1 value shows dark or lit."""
    s = frame * SS
    c = s / 2
    r = s * 0.17
    dark = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(dark)
    d.ellipse((c - r - SS, c - r - SS, c + r + SS, c + r + SS), fill=(0, 0, 0, 90))
    d.ellipse((c - r, c - r, c + r, c + r), fill=lerp(ACCENT, (40, 40, 40), 0.8) + (255,))
    lit = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    lit.alpha_composite(glow((s, s), (c - r * 1.8, c - r * 1.8, c + r * 1.8, c + r * 1.8), ACCENT, 2 * SS, 0.8, True))
    d = ImageDraw.Draw(lit)
    d.ellipse((c - r, c - r, c + r, c + r), fill=lerp(ACCENT, (255, 255, 255), 0.25) + (255,))
    d.ellipse((c - r * 0.45, c - r * 0.6, c, c - r * 0.15), fill=(255, 255, 255, 140))
    out = Image.new("RGBA", (s, s * frames), (0, 0, 0, 0))
    for i in range(frames):
        out.paste(lit if i >= frames // 2 else dark, (0, i * s))
    return out.resize((frame, frame * frames), Image.Resampling.LANCZOS)
