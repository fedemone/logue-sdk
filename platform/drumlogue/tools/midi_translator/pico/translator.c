/*
 * translator.c -- see translator.h. Ported from midi_translator.py.
 */
#include "translator.h"

#include <string.h>

/* -- active-note bitmap helpers (16 channels x 128 notes) -------------- */
static inline void act_set(translator_t *t, uint8_t ch0, uint8_t note) {
    t->active[ch0][note >> 3] |= (uint8_t)(1u << (note & 7));
}
static inline void act_clr(translator_t *t, uint8_t ch0, uint8_t note) {
    t->active[ch0][note >> 3] &= (uint8_t)~(1u << (note & 7));
}
static inline int act_get(translator_t *t, uint8_t ch0, uint8_t note) {
    return (t->active[ch0][note >> 3] >> (note & 7)) & 1;
}

/* -- output helpers ---------------------------------------------------- */
static inline void emit(translator_t *t, uint8_t st, uint8_t d0, uint8_t d1,
                        uint8_t len) {
    t->sink(st, d0, d1, len, t->user);
}
static inline void send_off(translator_t *t, uint8_t ch0, uint8_t note) {
    emit(t, (uint8_t)(MIDI_NOTE_OFF | ch0), note, 0, 3);
}

static const drum_entry_t *find_entry(translator_t *t, uint8_t note) {
    for (unsigned i = 0; i < t->map_len; i++)
        if (t->map[i].in_note == (int16_t)note)
            return &t->map[i];
    return NULL;
}

/* -- init -------------------------------------------------------------- */
void translator_init(translator_t *t, midi_sink_fn sink, void *user,
                     delay_ms_fn delay) {
    memset(t, 0, sizeof(*t));
    t->drums_in        = DRUMS_IN_CHANNEL;
    t->synth_in        = SYNTH_IN_CHANNEL;
    t->synth_out0      = (uint8_t)(SYNTH_OUT_CHANNEL - 1);
    t->choke_enabled   = CHOKE_ENABLED;
    t->retrigger_same  = RETRIGGER_SAME;
    t->choke_delay_ms  = CHOKE_NOTE_OFF_DELAY_MS;
    t->pass_clock      = PASS_CLOCK;
    t->pass_pc         = PASS_PROGRAM_CHANGE;
    t->forward_unmapped= FORWARD_UNMAPPED_DRUM_NOTES;
    t->forward_other   = FORWARD_OTHER_CHANNELS;
    t->map             = DRUM_MAP;
    t->map_len         = DRUM_MAP_LEN;
    t->sink            = sink;
    t->user            = user;
    t->delay           = delay;
}

/* -- drum fan-out + choke --------------------------------------------- */
static void route_drum(translator_t *t, uint8_t type, uint8_t note,
                       uint8_t vel) {
    const drum_entry_t *e = find_entry(t, note);
    if (e == NULL) {
        if (t->forward_unmapped)
            emit(t, (uint8_t)(type | (t->drums_in - 1)), note, vel, 3);
        return;
    }
    uint8_t out_ch0  = (uint8_t)(e->out_channel - 1);
    uint8_t out_note = e->out_note;
    int is_on = (type == MIDI_NOTE_ON && vel > 0);

    if (is_on) {
        if (t->choke_enabled && e->choke_group >= 0 &&
            e->choke_group < MAX_CHOKE_GROUPS) {
            int g = e->choke_group;
            if (t->group_state[g].active) {
                uint8_t p_ch = t->group_state[g].ch0;
                uint8_t p_note = t->group_state[g].note;
                if ((p_ch != out_ch0 || p_note != out_note) ||
                    t->retrigger_same) {
                    send_off(t, p_ch, p_note);
                    act_clr(t, p_ch, p_note);
                    if (t->choke_delay_ms && t->delay)
                        t->delay(t->choke_delay_ms);
                }
            }
            t->group_state[g].active = 1;
            t->group_state[g].ch0    = out_ch0;
            t->group_state[g].note   = out_note;
        }
        emit(t, (uint8_t)(MIDI_NOTE_ON | out_ch0), out_note, vel, 3);
        act_set(t, out_ch0, out_note);
    } else {
        send_off(t, out_ch0, out_note);
        act_clr(t, out_ch0, out_note);
        if (e->choke_group >= 0 && e->choke_group < MAX_CHOKE_GROUPS) {
            int g = e->choke_group;
            if (t->group_state[g].active &&
                t->group_state[g].ch0 == out_ch0 &&
                t->group_state[g].note == out_note)
                t->group_state[g].active = 0;
        }
    }
}

/* -- main entry point -------------------------------------------------- */
void translator_handle(translator_t *t, const midi_event_t *ev) {
    uint8_t st = ev->status;

    /* system real-time (0xF8..0xFF) */
    if (st >= MIDI_CLOCK) {
        if (t->pass_clock &&
            (st == MIDI_CLOCK || st == MIDI_START || st == MIDI_CONTINUE ||
             st == MIDI_STOP  || st == MIDI_RESET))
            emit(t, st, 0, 0, 1);
        return;
    }
    if (st == MIDI_SONGPOS) {          /* song position pointer */
        if (t->pass_clock) emit(t, st, ev->d0, ev->d1, 3);
        return;
    }
    if (st >= MIDI_SYSEX_START)        /* other system common: sysex is raw-  */
        return;                        /* forwarded in the glue; rest dropped  */

    uint8_t type = st & 0xF0u;
    uint8_t ch1  = (uint8_t)((st & 0x0Fu) + 1);

    if (type == MIDI_PROG) {
        if (t->pass_pc) emit(t, st, ev->d0, 0, 2);
        return;
    }

    if (ch1 == t->drums_in && (type == MIDI_NOTE_ON || type == MIDI_NOTE_OFF)) {
        route_drum(t, type, ev->d0, ev->d1);
        return;
    }

    if (ch1 == t->synth_in) {
        /* full passthrough to the MULTI ENGINE track, channel rewritten:
         * notes, pitch bend, CC, poly/channel pressure -- everything. */
        uint8_t out_st = (uint8_t)(type | t->synth_out0);
        emit(t, out_st, ev->d0, ev->d1, ev->len);
        if (type == MIDI_NOTE_ON && ev->d1 > 0)
            act_set(t, t->synth_out0, ev->d0);
        else if (type == MIDI_NOTE_OFF ||
                 (type == MIDI_NOTE_ON && ev->d1 == 0))
            act_clr(t, t->synth_out0, ev->d0);
        return;
    }

    if (t->forward_other)
        emit(t, st, ev->d0, ev->d1, ev->len);
}

/* -- panic ------------------------------------------------------------- */
void translator_panic(translator_t *t) {
    for (int ch = 0; ch < 16; ch++)
        for (int n = 0; n < 128; n++)
            if (act_get(t, (uint8_t)ch, (uint8_t)n)) {
                send_off(t, (uint8_t)ch, (uint8_t)n);
                act_clr(t, (uint8_t)ch, (uint8_t)n);
            }
    for (int g = 0; g < MAX_CHOKE_GROUPS; g++)
        t->group_state[g].active = 0;
    for (int ch = 0; ch < 16; ch++)
        emit(t, (uint8_t)(MIDI_CC | ch), MIDI_CC_ALL_NOTES_OFF, 0, 3);
}
