/*
 * test_core.c -- host unit tests for the portable core (translator + parser).
 *
 * Compiles and runs on a normal PC with cc; no Pico/TinyUSB needed. Verifies
 * the logic ported from midi_translator.py against the placeholder config:
 *   Bass Drum 36->ch2, Snare 38->ch3, Closed Hat 42->ch4 (grp1),
 *   Open Hat 46->ch5 (grp1), ... ; drums on ch10, synth on ch1 -> ch1.
 *
 *   cc -Wall -Wextra -o test_core test/test_core.c \
 *       translator.c midi_parse.c config.c -I.
 */
#include <stdio.h>
#include <string.h>
#include "../translator.h"
#include "../midi_parse.h"

/* -- recording sink ---------------------------------------------------- */
typedef struct { uint8_t st, d0, d1, len; } rec_t;
static rec_t  g_rec[256];
static int    g_n;

static void rec_sink(uint8_t st, uint8_t d0, uint8_t d1, uint8_t len, void *u) {
    (void)u;
    if (g_n < (int)(sizeof(g_rec) / sizeof(g_rec[0])))
        g_rec[g_n++] = (rec_t){ st, d0, d1, len };
}
static void rec_reset(void) { g_n = 0; }

/* -- tiny assert framework -------------------------------------------- */
static int g_fail;
#define CHECK(cond, msg) do {                                            \
    if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail++; }              \
    else         { printf("  ok:   %s\n", msg); }                       \
} while (0)

static int msg_eq(int i, uint8_t st, uint8_t d0, uint8_t d1) {
    return i < g_n && g_rec[i].st == st && g_rec[i].d0 == d0 &&
           g_rec[i].d1 == d1;
}

/* -- send one (status,d0,d1) message into the translator -------------- */
static void send(translator_t *t, uint8_t st, uint8_t d0, uint8_t d1) {
    midi_event_t e = { st, d0, d1, 3 };
    translator_handle(t, &e);
}

/* ===================================================================== */
static void test_drum_fanout(translator_t *t) {
    printf("[drum fan-out]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    /* note-on 36 vel100 on ch10 (0x99) -> note-on ch2 (0x91) note36 */
    send(t, MIDI_NOTE_ON | 9, 36, 100);
    CHECK(g_n == 1, "one message out");
    CHECK(msg_eq(0, MIDI_NOTE_ON | 1, 36, 100), "36->ch2 note-on");
    /* note-off */
    send(t, MIDI_NOTE_OFF | 9, 36, 0);
    CHECK(msg_eq(1, MIDI_NOTE_OFF | 1, 36, 0), "36->ch2 note-off");
}

static void test_split(translator_t *t) {
    printf("[synth split / passthrough]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    /* note on ch1 (0x90) note60 -> ch1 out (synth_out=1 -> 0x90) note60 */
    send(t, MIDI_NOTE_ON | 0, 60, 90);
    CHECK(msg_eq(0, MIDI_NOTE_ON | 0, 60, 90), "synth note passthrough ch1");
    /* pitch bend on ch1 passes through with same channel */
    send(t, MIDI_PITCH | 0, 0x00, 0x40);
    CHECK(msg_eq(1, MIDI_PITCH | 0, 0x00, 0x40), "synth pitch-bend passthrough");
    /* CC on ch1 passes through */
    send(t, MIDI_CC | 0, 74, 64);
    CHECK(msg_eq(2, MIDI_CC | 0, 74, 64), "synth CC passthrough");
}

static void test_choke(translator_t *t) {
    printf("[choke group]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    /* Open Hat on (note46 -> ch5, group1) */
    send(t, MIDI_NOTE_ON | 9, 46, 100);
    CHECK(msg_eq(0, MIDI_NOTE_ON | 4, 46, 100), "open hat -> ch5 on");
    /* Closed Hat on (note42 -> ch4, group1) must choke the open hat first */
    send(t, MIDI_NOTE_ON | 9, 42, 110);
    CHECK(g_n == 3, "choke emits off + on (2 msgs)");
    CHECK(msg_eq(1, MIDI_NOTE_OFF | 4, 46, 0), "choke: open hat ch5 note-off");
    CHECK(msg_eq(2, MIDI_NOTE_ON | 3, 42, 110), "closed hat -> ch4 on");
}

static void test_program_and_clock(translator_t *t) {
    printf("[program change + clock passthrough]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    send(t, MIDI_PROG | 5, 7, 0);                          /* PC on ch6 */
    CHECK(msg_eq(0, MIDI_PROG | 5, 7, 0), "program change forwarded");
    midi_event_t clk = { MIDI_CLOCK, 0, 0, 1 };
    translator_handle(t, &clk);
    CHECK(g_rec[1].st == MIDI_CLOCK && g_rec[1].len == 1, "clock forwarded");
    midi_event_t start = { MIDI_START, 0, 0, 1 };
    translator_handle(t, &start);
    CHECK(g_rec[2].st == MIDI_START, "start forwarded");
}

static void test_panic(translator_t *t) {
    printf("[panic]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    /* light up two drum voices, then panic */
    send(t, MIDI_NOTE_ON | 9, 36, 100);   /* ch2 */
    send(t, MIDI_NOTE_ON | 9, 38, 100);   /* ch3 */
    int before = g_n;
    translator_panic(t);
    int off = 0, cc123 = 0;
    for (int i = before; i < g_n; i++) {
        if ((g_rec[i].st & 0xF0) == MIDI_NOTE_OFF) off++;
        if ((g_rec[i].st & 0xF0) == MIDI_CC && g_rec[i].d0 == 123) cc123++;
    }
    CHECK(off == 2, "panic sends exactly 2 note-offs (no leak)");
    CHECK(cc123 == 16, "panic sends CC123 on all 16 channels");
}

/* Parser + translator together: raw bytes -> events -> output.
 * Uses running status (two note-ons share one 0x99 status byte). */
static void on_ev(void *user, const midi_event_t *ev) {
    translator_handle((translator_t *)user, ev);
}
static void test_parser_running_status(translator_t *t) {
    printf("[parser: running status + realtime interleave]\n");
    rec_reset();
    translator_init(t, rec_sink, NULL, NULL);   /* isolate per-test state */
    midi_parser_t p;
    midi_parser_init(&p, on_ev, NULL, t, 1);
    /* 0x99 36 64  | (running) 38 64, with an 0xF8 clock byte injected mid-run */
    uint8_t stream[] = { 0x99, 36, 64, 0xF8, 38, 64 };
    for (size_t i = 0; i < sizeof(stream); i++)
        midi_parser_feed(&p, stream[i]);
    /* expect: note36->ch2 on, clock, note38->ch3 on */
    CHECK(msg_eq(0, MIDI_NOTE_ON | 1, 36, 64), "parsed note 36 -> ch2");
    CHECK(g_rec[1].st == MIDI_CLOCK, "clock passed through mid running-status");
    CHECK(msg_eq(2, MIDI_NOTE_ON | 2, 38, 64), "running-status note 38 -> ch3");
}

int main(void) {
    translator_t t;
    translator_init(&t, rec_sink, NULL, NULL);

    test_drum_fanout(&t);
    test_split(&t);
    test_choke(&t);
    test_program_and_clock(&t);
    test_panic(&t);
    test_parser_running_status(&t);

    printf("\n%s\n", g_fail ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return g_fail ? 1 : 0;
}
