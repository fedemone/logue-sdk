/*
 * midi_defs.h -- MIDI status constants and the parsed-event struct.
 *
 * Portable: no Pico/TinyUSB dependencies, so this compiles on a host for the
 * unit tests as well as on the RP2040/RP2350.
 */
#ifndef MIDI_DEFS_H
#define MIDI_DEFS_H

#include <stdint.h>

/* A normalized MIDI message. For channel voice messages `status` includes the
 * channel in its low nibble. `len` is the total message length in bytes. */
typedef struct {
    uint8_t status;  /* full status byte (channel msgs include channel)      */
    uint8_t d0;      /* first data byte  (0 if none)                          */
    uint8_t d1;      /* second data byte (0 if none)                          */
    uint8_t len;     /* total message length, 1..3                            */
} midi_event_t;

/* Channel voice message high nibbles */
#define MIDI_NOTE_OFF   0x80u
#define MIDI_NOTE_ON    0x90u
#define MIDI_POLY_AT    0xA0u
#define MIDI_CC         0xB0u
#define MIDI_PROG       0xC0u
#define MIDI_CHAN_AT    0xD0u
#define MIDI_PITCH      0xE0u

/* System messages */
#define MIDI_SYSEX_START 0xF0u
#define MIDI_MTC_QF      0xF1u
#define MIDI_SONGPOS     0xF2u
#define MIDI_SONGSEL     0xF3u
#define MIDI_TUNE_REQ    0xF6u
#define MIDI_SYSEX_END   0xF7u
#define MIDI_CLOCK       0xF8u
#define MIDI_START       0xFAu
#define MIDI_CONTINUE    0xFBu
#define MIDI_STOP        0xFCu
#define MIDI_ACTIVE_SENS 0xFEu
#define MIDI_RESET       0xFFu

#define MIDI_CC_ALL_NOTES_OFF 123u

#endif /* MIDI_DEFS_H */
