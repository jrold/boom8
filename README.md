# BOOM8

An analog drum machine for the MPC Live III (standalone, modded unit), modelled
on the classic 1980 analog rhythm machine: its sixteen sounds, its panel of
knobs, and its colours. An Open MPC VST2 plug-in with a native touchscreen
skin. One of the BOOM family with BOOM6 and BOOM9: the same pages and
layout, each in its own original's colours.

BOOM8 is an independent model. It contains no samples, code or artwork from
the original machine's maker and isn't affiliated with them.

## The sounds

Each voice follows its circuit on the original: what feeds what, which
envelopes and filters, at the frequencies and times published for the
hardware.

| Sound | How it's made | Knobs |
|---|---|---|
| Bass drum | A resonator (the bridged-T) at 49 Hz, its pitch bent up at the hit; the trigger pulse's edge as the click; a low-pass after both | LEVEL, TONE (the low-pass), DECAY |
| Snare drum | Two resonators at 180 and 330 Hz and high-passed noise | LEVEL, TONE, SNAPPY (the noise) |
| Toms | A resonator falling slightly to its pitch, with a little pink noise: low 80–100 Hz, mid 120–160 Hz, hi 165–220 Hz | LEVEL, TUNING |
| Congas | The same resonators higher and damped harder: 165–220, 250–310, 370–455 Hz | the toms' knobs |
| Rim shot | Resonances at 455 and 1667 Hz, high-passed and squared off | LEVEL |
| Claves | A resonance at 2.5 kHz | the rim shot's LEVEL |
| Hand clap | Band-passed noise through a sawtooth envelope: three quick claps, then the reverb tail | LEVEL |
| Maracas | High-passed noise with a soft attack | the clap's LEVEL |
| Cowbell | The 540 and 800 Hz square oscillators, a two-stage envelope, a band-pass | LEVEL |
| Cymbal | Six square oscillators (205, 304, 370, 523, 540, 800 Hz) into a low and a high band, each with its own envelope | LEVEL, TONE, DECAY |
| Open / closed hat | The same six oscillators, band- and high-passed | LEVEL, DECAY (open) / LEVEL (closed) |

The six square oscillators run all the time and are shared by the cymbal,
hats and cowbell, as on the hardware, so no two hits start alike. The closed
hat cuts the open hat. A hit at velocity 100 or more is an accented step: the
**ACCENT** knob sets how much louder (up to +6 dB) and how much further the
drums' pitch bends. On the hardware a tom and its conga share a circuit; here
they share their knobs but can play together.

Not on the hardware: **BD TUNE** (±12 semitones), the snare's and clap's
**DECAY** (at 5.0 they are the hardware's length; turn down to tighten them,
to about a quarter, or up to almost double), a **PAN** per instrument, and on
**SETUP** **VELOCITY** (how much softer pad hits play, 0 = the hardware's two
levels) and **HAT CHOKE** (on, as on the hardware).

## Pages

**MACHINE**: ACCENT and a column for each instrument with its sound knobs top
to bottom (instruments with only a level are on MIX).

**MIX**: a LEVEL fader (in dB) and a PAN knob for each instrument column, and
the MASTER fader.

On both pages a small dot left of each instrument's name lights for a moment
when a pad plays it, to see which pad plays what.

**SETUP**: PAD LEARN, CLEAR PADS, VELOCITY and HAT CHOKE.

Pads play General MIDI drum notes (BD 36, SD 38, LT 41, MT 45, HT 48, RS 37,
CP 39, CB 56, CY 49, OH 46, CH 42, LC 64, MC 63, HC 62, CL 75, MA 70, with a
few neighbours). Or tap **PAD LEARN** and hit the pads you want for the
sixteen sounds in the order BD, SD, LT, MT, HT, RS, CP, CB, CY, OH, CH, LC,
MC, HC, CL, MA; it finishes after the
sixteenth, saves the map with the project, and new BOOM8 tracks start with it
(`data/padmap.txt`). **STOP twice** stops all sound. Everything is saved with
the MPC project.

## Build and install

```bash
bash mpc/build.sh      # needs zig, cmake, ninja, python3; no Docker
bash mpc/deploy.sh root@mpc-live-iii.local 2222
```

Installing restarts the MPC app, so save your project first.
`mpc/gen_layout.py` writes `mpc/params.json` and `mpc/layout.conf`; run it
after changing either. It, `mpc/skin/skin_post.py` and `mpc/skin/hardware.py`
are the same files in every BOOM except gen_layout.py's MACHINE block and
hardware.py's THEME block.

## Tests

```bash
cmake -S mpc -B build-host -G Ninja -DMPC_VST_DIR=<mpc-vst-plugins checkout>
cmake --build build-host
build-host/boom8-mpc-test                     # the voices and the adapter
build-host/boom8-demo pattern demo.wav        # a four-bar pattern
build-host/boom8-demo voices <dir>            # each sound, and its measurements
```

`mpc/build.sh` also cross-builds both for the MPC (`mpc/build/aarch64/`).
