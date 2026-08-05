#ifndef WLH_TRANSPORT_H
#define WLH_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "wlh/coproc.h"

/* USB bulk transport for the WL-hosted wire protocol (CherryUSB device on
 * the CH32V307 USBHS controller). Same contract as the ESP-IDF adapter:
 * raw wire frames on the bulk byte stream, reassembled by the 24-byte frame
 * header; USB packet boundaries carry no frame semantics. */

typedef void (*wlh_transport_reset_fn)(void *context);

typedef struct wlh_transport_config {
    wlh_coproc_t *coproc;
    size_t max_frame_size;
    wlh_transport_reset_fn on_reset;
    void *reset_context;
} wlh_transport_config_t;

int wlh_transport_start(const wlh_transport_config_t *config);
int wlh_transport_submit_tx(
    void *context,
    uint8_t *frame,
    size_t size,
    wlh_coproc_tx_complete_fn completion,
    void *completion_context
);
size_t wlh_transport_max_frame_size(void);
size_t wlh_transport_tx_capacity(void);

/* Diagnostics for the usb_stats msh command. */
typedef struct wlh_transport_stats {
    uint32_t rx_bytes;
    uint32_t rx_overruns;
    uint32_t tx_frames;
    uint32_t tx_failures;
    uint32_t bus_resets;
    uint32_t configured;
} wlh_transport_stats_t;
void wlh_transport_get_stats(wlh_transport_stats_t *stats);

#endif
