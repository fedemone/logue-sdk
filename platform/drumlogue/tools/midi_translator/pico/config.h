/*
 * config.h -- edit this to match your setup.
 *
 * This mirrors the CONFIG block of the Python tool (midi_translator.py).
 * Channels are written 1..16 (human numbering) and converted internally.
 * The drum map itself lives in config.c so you edit one array in one place.
 *
 * Portable: included by the unit tests too, so keep it free of Pico headers.
 * The pin / UART macros below are plain tokens; only main.c evaluates them.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------ *
 *  Input layout (what YOU play / sequence)
 * ------------------------------------------------------------------ */
#define DRUMS_IN_CHANNEL        10   /* you trigger drums here (GM drums = 10) */
#define SYNTH_IN_CHANNEL         1   /* you play the MULTI ENGINE synth here    */

/* ------------------------------------------------------------------ *
 *  drumlogue side (multi-channel mode)
 * ------------------------------------------------------------------ */
#define SYNTH_OUT_CHANNEL        1   /* the drumlogue's MULTI ENGINE track ch   */

/* ------------------------------------------------------------------ *
 *  Choke behaviour
 * ------------------------------------------------------------------ */
#define CHOKE_ENABLED            1
#define CHOKE_NOTE_OFF_DELAY_MS  0   /* delay between choke note-off and note-on */
#define RETRIGGER_SAME           1   /* re-choke when the same member retriggers */
#define MAX_CHOKE_GROUPS         8   /* choke_group values must be 0..this-1     */

/* ------------------------------------------------------------------ *
 *  Passthrough
 * ------------------------------------------------------------------ */
#define PASS_CLOCK               1   /* clock / start / stop / continue / spp    */
#define PASS_PROGRAM_CHANGE      1
#define PASS_SYSEX               1
#define FORWARD_UNMAPPED_DRUM_NOTES 0
#define FORWARD_OTHER_CHANNELS   0

/* ------------------------------------------------------------------ *
 *  Hardware I/O (used by main.c only)
 * ------------------------------------------------------------------ */
/* DIN MIDI over a hardware UART. Wire TX through a 220R MIDI-out pair to the
 * drumlogue MIDI IN; wire RX through a 6N138/H11L1 optocoupler from a DIN OUT. */
#define USE_UART_IN              1   /* read DIN MIDI IN  (controller on DIN)    */
#define USE_UART_OUT             1   /* drive DIN MIDI OUT (to drumlogue MIDI IN)*/
#define MIDI_UART                uart0
#define MIDI_UART_TX_PIN         0   /* GP0 -> DIN OUT circuit -> drumlogue IN   */
#define MIDI_UART_RX_PIN         1   /* GP1 <- optocoupler <- controller DIN OUT */
#define MIDI_BAUD                31250

/* USB host input: host a class-compliant USB-MIDI controller via TinyUSB.
 * Set to 1 to enable (needs the TinyUSB host build; see README). Output over
 * USB to the drumlogue's TO HOST port needs a *second* USB port (PIO-USB) and
 * is documented in the README rather than enabled here. */
#define USE_USB_HOST_IN          0

/* The drum map (edit in config.c). */
typedef struct {
    int16_t in_note;      /* the note YOU send on DRUMS_IN_CHANNEL (0..127)     */
    uint8_t out_channel;  /* drumlogue channel that drives this track (1..16)   */
    uint8_t out_note;     /* note sent to the track (often ignored by drums)    */
    int8_t  choke_group;  /* -1 = none; equal values are mutually exclusive     */
} drum_entry_t;

extern const drum_entry_t DRUM_MAP[];
extern const unsigned      DRUM_MAP_LEN;

#endif /* CONFIG_H */
