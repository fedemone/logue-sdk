/*
 * midi_parse.c -- see midi_parse.h.
 */
#include "midi_parse.h"

#include <string.h>

/* Number of data bytes that follow a given status byte. */
static uint8_t data_len_for_status(uint8_t st) {
    if (st < 0x80u) return 0;                 /* not a status byte */
    if (st < 0xF0u) {                         /* channel voice */
        switch (st & 0xF0u) {
            case MIDI_PROG:      return 1;    /* program change   */
            case MIDI_CHAN_AT:   return 1;    /* channel pressure */
            default:             return 2;    /* note/CC/poly/bend */
        }
    }
    switch (st) {                             /* system common */
        case MIDI_MTC_QF:  return 1;
        case MIDI_SONGSEL: return 1;
        case MIDI_SONGPOS: return 2;
        default:           return 0;          /* F6 tune req, F7, ... */
    }
}

void midi_parser_init(midi_parser_t *p, midi_event_cb on_event,
                      midi_raw_cb on_raw, void *user, uint8_t pass_sysex) {
    memset(p, 0, sizeof(*p));
    p->on_event   = on_event;
    p->on_raw     = on_raw;
    p->user       = user;
    p->pass_sysex = pass_sysex;
}

static void deliver(midi_parser_t *p, uint8_t st, uint8_t d0, uint8_t d1,
                    uint8_t len) {
    midi_event_t ev = { st, d0, d1, len };
    if (p->on_event) p->on_event(p->user, &ev);
}

void midi_parser_feed(midi_parser_t *p, uint8_t b) {
    /* System real-time may interleave anywhere and never disturbs state. */
    if (b >= MIDI_CLOCK) {
        deliver(p, b, 0, 0, 1);
        return;
    }

    if (p->in_sysex) {
        if (b == MIDI_SYSEX_END) {
            if (p->pass_sysex && p->on_raw) p->on_raw(p->user, b);
            p->in_sysex = 0;
            return;
        }
        if (b < 0x80u) {                      /* normal SysEx data byte */
            if (p->pass_sysex && p->on_raw) p->on_raw(p->user, b);
            return;
        }
        p->in_sysex = 0;                      /* stray status aborts SysEx */
        /* fall through and handle it as a status byte */
    }

    if (b == MIDI_SYSEX_START) {
        p->in_sysex = 1;
        p->running_status = 0;
        if (p->pass_sysex && p->on_raw) p->on_raw(p->user, b);
        return;
    }

    if (b & 0x80u) {                          /* a status byte */
        p->running_status = b;
        p->expected = data_len_for_status(b);
        p->data_count = 0;
        if (p->expected == 0) {               /* e.g. F6 tune request */
            deliver(p, b, 0, 0, 1);
            if (b >= 0xF0u) p->running_status = 0;
        }
        return;
    }

    /* a data byte */
    if (p->running_status == 0) return;       /* no status seen yet */
    p->data[p->data_count++] = b;
    if (p->data_count >= p->expected) {
        deliver(p, p->running_status, p->data[0],
                p->expected > 1 ? p->data[1] : 0,
                (uint8_t)(1 + p->expected));
        p->data_count = 0;
        if (p->running_status >= 0xF0u)       /* system common: no running   */
            p->running_status = 0;            /* status; channel msgs keep it */
    }
}
