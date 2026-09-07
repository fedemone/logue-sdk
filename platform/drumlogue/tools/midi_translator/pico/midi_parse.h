/*
 * midi_parse.h -- a byte-at-a-time MIDI parser with running-status support.
 *
 * Feed it raw bytes from a UART (DIN) or a USB-MIDI byte stream; it calls
 * `on_event` for each complete channel/system message, and `on_raw` for the
 * bytes of a SysEx block (passthrough). System real-time bytes (clock, start,
 * ...) are delivered immediately even mid-message, as MIDI requires.
 *
 * Portable: no Pico dependencies (unit-tested on a host).
 */
#ifndef MIDI_PARSE_H
#define MIDI_PARSE_H

#include "midi_defs.h"

typedef void (*midi_event_cb)(void *user, const midi_event_t *ev);
typedef void (*midi_raw_cb)(void *user, uint8_t byte); /* SysEx; may be NULL */

typedef struct {
    uint8_t running_status;
    uint8_t data[2];
    uint8_t data_count;
    uint8_t expected;
    uint8_t in_sysex;
    uint8_t pass_sysex;
    midi_event_cb on_event;
    midi_raw_cb   on_raw;
    void *user;
} midi_parser_t;

void midi_parser_init(midi_parser_t *p, midi_event_cb on_event,
                      midi_raw_cb on_raw, void *user, uint8_t pass_sysex);
void midi_parser_feed(midi_parser_t *p, uint8_t byte);

#endif /* MIDI_PARSE_H */
