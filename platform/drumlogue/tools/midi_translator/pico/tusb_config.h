/*
 * tusb_config.h -- TinyUSB configuration (host mode, MIDI class).
 *
 * Only relevant when USE_USB_HOST_IN = 1 in config.h and the TinyUSB lines in
 * CMakeLists.txt are enabled. For the default DIN-only build TinyUSB is not
 * linked and this file is unused.
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* RP2040/RP2350 full-speed, host role on the native USB controller. */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_RP2040
#endif
#define CFG_TUSB_OS           OPT_OS_PICO
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

/* ---- Host ---- */
#define CFG_TUH_ENABLED       1
#define CFG_TUH_HUB           1   /* allow a hub (harmless if unused)          */
#define CFG_TUH_DEVICE_MAX    (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_MIDI          1
#define CFG_TUH_MIDI_RX_BUFSIZE 64
#define CFG_TUH_MIDI_TX_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
