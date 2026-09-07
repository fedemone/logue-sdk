/*
 * translator.h -- the drumlogue MIDI translation logic.
 *
 * A faithful C port of the Translator class in midi_translator.py:
 *   - fan a "drums" channel out to per-track channels with a note map,
 *   - pass a "synth" channel straight through to the MULTI ENGINE track,
 *   - emulate choke / exclusive groups via injected note-offs,
 *   - panic() = all-notes-off on everything it touched.
 *
 * Portable: no Pico/TinyUSB dependencies (unit-tested on a host). All output
 * goes through a caller-supplied sink so the same logic drives UART, USB, or a
 * test recorder.
 */
#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include "midi_defs.h"
#include "config.h"

/* Output sink: emit one MIDI message. `len` is 1..3. */
typedef void (*midi_sink_fn)(uint8_t status, uint8_t d0, uint8_t d1,
                             uint8_t len, void *user);

/* Optional blocking delay (used only if CHOKE_NOTE_OFF_DELAY_MS > 0).
 * Pass NULL to skip the delay entirely. */
typedef void (*delay_ms_fn)(uint16_t ms);

typedef struct {
    /* resolved config */
    uint8_t drums_in;        /* 1..16 */
    uint8_t synth_in;        /* 1..16 */
    uint8_t synth_out0;      /* 0..15 */
    uint8_t choke_enabled;
    uint8_t retrigger_same;
    uint16_t choke_delay_ms;
    uint8_t pass_clock;
    uint8_t pass_pc;
    uint8_t forward_unmapped;
    uint8_t forward_other;

    const drum_entry_t *map;
    unsigned map_len;

    /* state */
    struct {
        uint8_t active;      /* is a member currently sounding?     */
        uint8_t ch0;         /* its output channel (0..15)          */
        uint8_t note;        /* its output note                     */
    } group_state[MAX_CHOKE_GROUPS];

    /* which (channel,note) we have turned on, for panic(). 16 ch x 128 notes. */
    uint8_t active[16][16];

    midi_sink_fn sink;
    void *user;
    delay_ms_fn delay;
} translator_t;

/* Initialise from config.h. `sink` receives all output; `delay` may be NULL. */
void translator_init(translator_t *t, midi_sink_fn sink, void *user,
                     delay_ms_fn delay);

/* Feed one parsed inbound message. */
void translator_handle(translator_t *t, const midi_event_t *ev);

/* All-notes-off on every channel/note we touched, plus CC123 on all channels. */
void translator_panic(translator_t *t);

#endif /* TRANSLATOR_H */
