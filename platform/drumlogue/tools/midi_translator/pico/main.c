/*
 * main.c -- Pico (RP2040 / RP2350) hardware glue for the drumlogue MIDI
 * translator. The translation logic itself lives in the portable, unit-tested
 * translator.c / midi_parse.c; this file only wires up I/O.
 *
 * Default build (USE_USB_HOST_IN = 0 in config.h): DIN MIDI in and out over one
 * hardware UART. This needs nothing but the Pico SDK and always compiles.
 *
 * Optional (USE_USB_HOST_IN = 1): also host a class-compliant USB-MIDI
 * controller via TinyUSB host. See README.md -- you must enable the TinyUSB
 * lines in CMakeLists.txt, and the host callback signatures below may need
 * tweaking to match your TinyUSB version (the host MIDI API has changed over
 * time). Output over USB to the drumlogue's TO HOST port needs a *second* USB
 * port (PIO-USB); that variant is described in the README, not wired here.
 */
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "config.h"
#include "midi_defs.h"
#include "midi_parse.h"
#include "translator.h"

#if USE_USB_HOST_IN
#include "bsp/board_api.h"
#include "tusb.h"
#endif

static translator_t  g_tr;
static midi_parser_t g_parser;

/* ---- output -------------------------------------------------------- */
static inline void uart_write_msg(uint8_t st, uint8_t d0, uint8_t d1,
                                  uint8_t len) {
#if USE_UART_OUT
    uart_putc_raw(MIDI_UART, (char)st);
    if (len > 1) uart_putc_raw(MIDI_UART, (char)d0);
    if (len > 2) uart_putc_raw(MIDI_UART, (char)d1);
#else
    (void)st; (void)d0; (void)d1; (void)len;
#endif
}

/* Translator output sink. */
static void out_sink(uint8_t st, uint8_t d0, uint8_t d1, uint8_t len,
                     void *user) {
    (void)user;
    uart_write_msg(st, d0, d1, len);
    /* A USB-host output (drumlogue over USB) would also stream here; it needs
     * a second USB port via PIO-USB -- see README.md. */
}

/* SysEx passthrough: raw byte straight to the output. */
static void raw_sink(void *user, uint8_t b) {
    (void)user;
#if USE_UART_OUT
    uart_putc_raw(MIDI_UART, (char)b);
#else
    (void)b;
#endif
}

static void on_event(void *user, const midi_event_t *ev) {
    translator_handle((translator_t *)user, ev);
}

static void delay_ms_impl(uint16_t ms) { sleep_ms(ms); }

/* ---- setup --------------------------------------------------------- */
static void uart_midi_init(void) {
#if USE_UART_IN || USE_UART_OUT
    uart_init(MIDI_UART, MIDI_BAUD);
    uart_set_format(MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(MIDI_UART, true);
#if USE_UART_OUT
    gpio_set_function(MIDI_UART_TX_PIN, GPIO_FUNC_UART);
#endif
#if USE_UART_IN
    gpio_set_function(MIDI_UART_RX_PIN, GPIO_FUNC_UART);
#endif
#endif
}

int main(void) {
    uart_midi_init();

    translator_init(&g_tr, out_sink, NULL, delay_ms_impl);
    midi_parser_init(&g_parser, on_event, raw_sink, &g_tr, PASS_SYSEX);

#if USE_USB_HOST_IN
    board_init();
    tusb_init();
#endif

    for (;;) {
#if USE_UART_IN
        while (uart_is_readable(MIDI_UART)) {
            uint8_t b = (uint8_t)uart_getc(MIDI_UART);
            midi_parser_feed(&g_parser, b);
        }
#endif
#if USE_USB_HOST_IN
        tuh_task();   /* drives the mount / rx callbacks below */
#endif
    }
}

/* ---- TinyUSB host MIDI callbacks (only compiled with USB host on) ----
 * NOTE: these signatures follow the widely used TinyUSB host MIDI API. If your
 * TinyUSB reports mismatched prototypes, adjust to the version in your tree
 * (check tinyusb/src/class/midi/midi_host.h). */
#if USE_USB_HOST_IN
void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
                       uint8_t num_cables_rx, uint16_t num_cables_tx) {
    (void)dev_addr; (void)in_ep; (void)out_ep;
    (void)num_cables_rx; (void)num_cables_tx;
    /* controller connected */
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr; (void)instance;
    translator_panic(&g_tr);   /* controller unplugged: kill stuck notes */
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    (void)num_packets;
    uint8_t cable;
    uint8_t buf[64];
    for (;;) {
        uint32_t n = tuh_midi_stream_read(dev_addr, &cable, buf, sizeof(buf));
        if (n == 0) break;
        for (uint32_t i = 0; i < n; i++)
            midi_parser_feed(&g_parser, buf[i]);
    }
}
#endif
