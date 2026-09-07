#!/usr/bin/env python3
"""
drumlogue MIDI translator
=========================

A small, standalone MIDI processor to work around the drumlogue MIDI limitations
that cannot be fixed from the logue SDK (see the co-located
midi-routing-and-sdk-limits.md). It sits between your controller/DAW and the
drumlogue, and:

  * fans a single "drums" channel out to the drumlogue's per-track channels,
    with a note map you define (fixes the illogical single-channel note map),
  * passes a separate "synth" channel straight through to the MULTI ENGINE
    track so you can play it chromatically at the same time (the missing
    "two-channel" split mode),
  * emulates choke / exclusive groups by injecting note-offs when a group
    member fires (works only if the drum voices honour note-off -- run the
    5-minute test in the docs to find out).

It is designed to run headless on a Raspberry Pi (or any computer) with the
drumlogue connected over USB-MIDI or a DIN MIDI interface.

Requirements
------------
    pip install mido python-rtmidi

Quick start
-----------
    # 1. see what ports exist
    python3 midi_translator.py --list

    # 2. learn what your controller sends (note numbers, channels)
    python3 midi_translator.py --monitor --in "Your Controller"

    # 3. find which drumlogue channel drives which track (put drumlogue in
    #    multi-channel mode first), then fill CONFIG["drum_map"] accordingly
    python3 midi_translator.py --probe --out drumlogue

    # 4. edit the CONFIG block below, then run
    python3 midi_translator.py --in "Your Controller" --out drumlogue

Everything under CONFIG is meant to be edited by hand. Channels are written as
1..16 (human numbering) everywhere in this file and converted internally.
"""

import argparse
import signal
import sys
import time

try:
    import mido
except ImportError:  # pragma: no cover - environment hint
    sys.stderr.write(
        "error: the 'mido' package is required.\n"
        "       install it with:  pip install mido python-rtmidi\n"
    )
    sys.exit(1)


# =============================================================================
# CONFIG  --  edit this block for your setup
# =============================================================================

CONFIG = {
    # ---- Ports -------------------------------------------------------------
    # Substring, case-insensitive, matched against the names shown by --list.
    # Leave `input_port` as None to create a virtual input port that your DAW /
    # controller connects TO (recommended on a Pi). Set it to a substring to
    # read from an existing hardware/DAW port instead.
    "input_port": None,
    "input_port_name": "drumlogue-xlate",   # name of the virtual input port
    "output_port": "drumlogue",             # substring of the drumlogue's port

    # ---- Input layout (what YOU play / sequence) ---------------------------
    "drums_in_channel": 10,   # you trigger drums here (GM drums = channel 10)
    "synth_in_channel": 1,    # you play the MULTI ENGINE synth here, chromatic

    # ---- drumlogue side (multi-channel mode) -------------------------------
    # The drumlogue's MULTI ENGINE (Noise/VPM/user-synth) track channel. Notes
    # on `synth_in_channel` are forwarded here unchanged (full pitch/CC/bend).
    "synth_out_channel": 1,

    # Drum fan-out. Each incoming note on `drums_in_channel` is looked up here
    # and re-emitted as (out_channel, out_note) on the drumlogue.
    #
    #   label       : just for readability / probe output
    #   in_note     : the note YOU send (design your own sane layout here)
    #   out_channel : the drumlogue channel that drives this track
    #                 (discover it with --probe)
    #   out_note    : note sent to the track. For most drum tracks the note is
    #                 ignored (any note triggers the sound) so the value only
    #                 needs to be consistent; set it deliberately for pitched
    #                 tracks.
    #   choke_group : None = no choke. Tracks sharing the same integer are
    #                 mutually exclusive: a new hit in the group note-offs the
    #                 one currently sounding.
    #
    # >>> THE VALUES BELOW ARE A PLACEHOLDER EXAMPLE. <<<
    # Replace them with your own note choices and your drumlogue's real
    # channels (from --probe). The example uses a GM-ish input layout.
    "drum_map": [
        # label,        in_note, out_channel, out_note, choke_group
        ("Bass Drum",     36,       2,          36,       None),
        ("Snare",         38,       3,          38,       None),
        ("Closed Hat",    42,       4,          42,       1),
        ("Open Hat",      46,       5,          46,       1),   # 1 -> CH chokes OH
        ("Low Tom",       41,       6,          41,       None),
        ("Mid Tom",       45,       7,          45,       None),
        ("High Tom",      48,       8,          48,       None),
        ("Clap",          39,       9,          39,       None),
    ],

    # ---- Choke behaviour ---------------------------------------------------
    "choke_enabled": True,
    # Milliseconds to wait between the choke note-off and the new note-on.
    # 0 is usually right; raise a little only if a voice needs time to release.
    "choke_note_off_delay_ms": 0,
    # Also send note-off + note-on when the SAME member retriggers, for a clean
    # restart of a ringing voice (harmless for one-shots).
    "retrigger_same": True,

    # ---- Passthrough -------------------------------------------------------
    "pass_clock": True,            # MIDI clock / start / stop / continue / spp
    "pass_program_change": True,   # forward Program Change untouched
    "pass_sysex": True,
    # Forward notes on `drums_in_channel` that have no drum_map entry (as-is).
    "forward_unmapped_drum_notes": False,
    # Forward channel messages on channels that are neither drums nor synth.
    "forward_other_channels": False,
}


# =============================================================================
# Implementation
# =============================================================================

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n: int) -> str:
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def ch_to_midi(ch_1_16: int) -> int:
    """Human channel (1..16) -> mido channel (0..15)."""
    if not 1 <= ch_1_16 <= 16:
        raise ValueError(f"channel out of range: {ch_1_16} (expected 1..16)")
    return ch_1_16 - 1


def find_port(substr, names, kind):
    """Resolve a port by exact name or unique case-insensitive substring."""
    if substr in names:
        return substr
    matches = [n for n in names if substr and substr.lower() in n.lower()]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        sys.stderr.write(
            f"error: no {kind} port matches {substr!r}.\n"
            f"available {kind} ports:\n" + _fmt_ports(names)
        )
        sys.exit(2)
    sys.stderr.write(
        f"error: {kind} substring {substr!r} is ambiguous, matches:\n"
        + _fmt_ports(matches)
        + "       make it more specific.\n"
    )
    sys.exit(2)


def _fmt_ports(names):
    return "".join(f"    - {n}\n" for n in names) or "    (none)\n"


class Translator:
    """Stateful MIDI rewriter. Feed it messages; it sends to `out`."""

    def __init__(self, cfg, out):
        self.cfg = cfg
        self.out = out
        self.drums_in = cfg["drums_in_channel"]
        self.synth_in = cfg["synth_in_channel"]
        self.synth_out = ch_to_midi(cfg["synth_out_channel"])
        self.choke_enabled = cfg["choke_enabled"]
        self.choke_delay = cfg["choke_note_off_delay_ms"] / 1000.0
        self.retrigger_same = cfg["retrigger_same"]

        # in_note -> (out_channel_midi, out_note, choke_group)
        self.drum_lookup = {}
        for label, in_note, out_ch, out_note, group in cfg["drum_map"]:
            self.drum_lookup[in_note] = (ch_to_midi(out_ch), out_note, group)

        # choke_group -> (out_channel_midi, out_note) currently sounding
        self.group_state = {}
        # set of (out_channel_midi, out_note) we have turned on (for panic)
        self.active = set()

    # -- public ----------------------------------------------------------
    def handle(self, msg):
        # System / real-time messages carry no channel.
        if msg.type in ("clock", "start", "stop", "continue", "songpos",
                        "reset", "tick"):
            if self.cfg["pass_clock"]:
                self.out.send(msg)
            return
        if msg.type == "sysex":
            if self.cfg["pass_sysex"]:
                self.out.send(msg)
            return
        if msg.type == "program_change":
            if self.cfg["pass_program_change"]:
                self.out.send(msg)
            return
        if not hasattr(msg, "channel"):
            return

        ch = msg.channel + 1  # back to 1..16
        if ch == self.drums_in and msg.type in ("note_on", "note_off"):
            self._route_drum(msg)
        elif ch == self.synth_in:
            # Full passthrough to the MULTI ENGINE track: notes, pitch bend,
            # CC, channel/poly pressure -- everything, channel rewritten.
            self.out.send(msg.copy(channel=self.synth_out))
            if msg.type == "note_on" and msg.velocity > 0:
                self.active.add((self.synth_out, msg.note))
            elif msg.type in ("note_off",) or (
                msg.type == "note_on" and msg.velocity == 0
            ):
                self.active.discard((self.synth_out, msg.note))
        elif self.cfg["forward_other_channels"]:
            self.out.send(msg)

    def panic(self):
        """All-notes-off on every channel we touched, plus CC123."""
        for out_ch, out_note in list(self.active):
            self._send_off(out_ch, out_note)
        self.active.clear()
        self.group_state.clear()
        for ch in range(16):
            self.out.send(mido.Message("control_change", channel=ch,
                                       control=123, value=0))

    # -- internals -------------------------------------------------------
    def _route_drum(self, msg):
        entry = self.drum_lookup.get(msg.note)
        if entry is None:
            if self.cfg["forward_unmapped_drum_notes"]:
                self.out.send(msg)
            return
        out_ch, out_note, group = entry
        is_on = msg.type == "note_on" and msg.velocity > 0

        if is_on:
            if self.choke_enabled and group is not None:
                prev = self.group_state.get(group)
                if prev is not None and (prev != (out_ch, out_note)
                                         or self.retrigger_same):
                    self._send_off(*prev)
                    self.active.discard(prev)
                    if self.choke_delay:
                        time.sleep(self.choke_delay)
                self.group_state[group] = (out_ch, out_note)
            self.out.send(mido.Message("note_on", channel=out_ch,
                                       note=out_note, velocity=msg.velocity))
            self.active.add((out_ch, out_note))
        else:
            self._send_off(out_ch, out_note)
            self.active.discard((out_ch, out_note))
            if group is not None and self.group_state.get(group) == (out_ch, out_note):
                del self.group_state[group]

    def _send_off(self, out_ch, out_note):
        self.out.send(mido.Message("note_off", channel=out_ch,
                                   note=out_note, velocity=0))


# -- modes ---------------------------------------------------------------

def do_list():
    print("Input ports:")
    print(_fmt_ports(mido.get_input_names()), end="")
    print("Output ports:")
    print(_fmt_ports(mido.get_output_names()), end="")


def do_monitor(cfg, in_name):
    names = mido.get_input_names()
    if in_name:
        in_name = find_port(in_name, names, "input")
        port = mido.open_input(in_name)
        print(f"monitoring {in_name!r} -- Ctrl-C to stop")
    else:
        port = mido.open_input(cfg["input_port_name"], virtual=True)
        print(f"monitoring virtual input {cfg['input_port_name']!r} "
              "-- connect your controller to it. Ctrl-C to stop")
    with port:
        for msg in port:
            extra = ""
            if msg.type in ("note_on", "note_off") and hasattr(msg, "note"):
                extra = f"  [{note_name(msg.note)}]"
            ch = f" ch{msg.channel + 1}" if hasattr(msg, "channel") else ""
            print(f"{msg.type:16s}{ch}{extra}  {msg}")


def do_probe(cfg, out_name, channels, note, hold_ms):
    out_name = find_port(out_name or cfg["output_port"],
                         mido.get_output_names(), "output")
    print(f"probing {out_name!r}: one note per channel, "
          f"note {note} ({note_name(note)}). Listen for which track fires.")
    hold = hold_ms / 1000.0
    with mido.open_output(out_name) as out:
        try:
            for ch in channels:
                print(f"  -> channel {ch:2d}")
                out.send(mido.Message("note_on", channel=ch_to_midi(ch),
                                      note=note, velocity=100))
                time.sleep(hold)
                out.send(mido.Message("note_off", channel=ch_to_midi(ch),
                                      note=note, velocity=0))
                time.sleep(0.15)
        finally:
            for ch in channels:
                out.send(mido.Message("control_change", channel=ch_to_midi(ch),
                                      control=123, value=0))
    print("done.")


def do_run(cfg, in_name, out_name):
    out_name = find_port(out_name or cfg["output_port"],
                        mido.get_output_names(), "output")
    out = mido.open_output(out_name)

    in_name = in_name if in_name is not None else cfg["input_port"]
    if in_name:
        in_name = find_port(in_name, mido.get_input_names(), "input")
        inp = mido.open_input(in_name)
        src = f"input {in_name!r}"
    else:
        inp = mido.open_input(cfg["input_port_name"], virtual=True)
        src = f"virtual input {cfg['input_port_name']!r} (connect to it)"

    tr = Translator(cfg, out)

    stopping = {"flag": False}

    def stop(*_):
        stopping["flag"] = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    print(f"translating {src}  ->  output {out_name!r}")
    print(f"  drums  in ch{cfg['drums_in_channel']}  ({len(cfg['drum_map'])} mapped)")
    print(f"  synth  in ch{cfg['synth_in_channel']}  ->  drumlogue ch{cfg['synth_out_channel']}")
    print(f"  choke  {'on' if cfg['choke_enabled'] else 'off'}")
    print("  Ctrl-C to stop.")
    try:
        with inp, out:
            while not stopping["flag"]:
                for msg in inp.iter_pending():
                    tr.handle(msg)
                time.sleep(0.001)  # ~1 ms poll; keeps latency low, CPU idle
            tr.panic()
    finally:
        print("\nstopped, all notes off.")


def build_argparser():
    p = argparse.ArgumentParser(
        description="drumlogue MIDI translator (fan-out, split, choke).")
    p.add_argument("--list", action="store_true",
                   help="list MIDI ports and exit")
    p.add_argument("--monitor", action="store_true",
                   help="print incoming messages (learn your controller)")
    p.add_argument("--probe", action="store_true",
                   help="send one note per output channel to find tracks")
    p.add_argument("--in", dest="in_port", default=argparse.SUPPRESS,
                   help="input port substring (overrides CONFIG)")
    p.add_argument("--out", dest="out_port", default=None,
                   help="output port substring (overrides CONFIG)")
    p.add_argument("--probe-channels", default="1-16",
                   help="channels to probe, e.g. '1-16' or '2,3,4' (default 1-16)")
    p.add_argument("--probe-note", type=int, default=60,
                   help="note number to probe with (default 60 = C4)")
    p.add_argument("--probe-hold", type=int, default=600,
                   help="ms to hold each probe note (default 600)")
    return p


def parse_channels(spec):
    out = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    for c in out:
        if not 1 <= c <= 16:
            raise SystemExit(f"channel out of range in --probe-channels: {c}")
    return out


def main():
    args = build_argparser().parse_args()
    in_port = getattr(args, "in_port", None)  # None => use CONFIG / virtual

    if args.list:
        do_list()
    elif args.monitor:
        do_monitor(CONFIG, in_port)
    elif args.probe:
        do_probe(CONFIG, args.out_port, parse_channels(args.probe_channels),
                 args.probe_note, args.probe_hold)
    else:
        do_run(CONFIG, in_port, args.out_port)


if __name__ == "__main__":
    main()
