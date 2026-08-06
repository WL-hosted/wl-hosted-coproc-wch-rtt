/* Wired Ethernet backend for the WL-hosted CH32V307 coprocessor: EMAC with
 * the internal 10BASE-T PHY, bridged to the host over the ETH service and
 * the ETHERNET_ETH data channel of wl-hosted-core/coproc-core.
 *
 * Register programming follows the RT-Thread ch32 drv_eth.c driver
 * (libraries/ch32_drivers/drv_eth.c, USE10BASE_T path), restructured so that
 * no step blocks indefinitely: PHY reset is bounded and link/autonegotiation
 * is tracked by a 1 s poll thread instead of init-time busy waits. */
#ifndef WLH_ETH_BACKEND_H
#define WLH_ETH_BACKEND_H

#include <stdint.h>

#include "wlh/coproc.h"

typedef struct wlh_eth_stats {
    uint32_t rx_frames;   /* wire -> host, accepted by the core */
    uint32_t rx_dropped;  /* valid frame but core queue full / no credit */
    uint32_t rx_errors;   /* DMA descriptor error status */
    uint32_t tx_frames;   /* host -> wire, transmitted */
    uint32_t tx_errors;   /* DMA reported transmit error */
    uint32_t tx_rejected; /* no free slot / bad size / link down */
    uint32_t link_ups;    /* link down->up transitions */
    uint32_t isr_rx;      /* R interrupts seen in the ETH ISR */
    uint32_t isr_tx;      /* T interrupts seen in the ETH ISR */
    uint32_t worker_wakes;      /* ETH worker loop iterations */
    uint32_t worker_empty_rx;   /* wakes that drained no RX frame */
    uint8_t link_up;
    uint8_t duplex_full;
} wlh_eth_stats_t;

/* Initialises the EMAC + internal PHY and starts the RX/link threads. The
 * MAC perfect-filters unicast to our own address (broadcast/multicast pass);
 * host-side filtering applies on top. Callable once from wlh_app_init after
 * wlh_coproc_init. */
int wlh_eth_backend_init(wlh_coproc_t *coproc);

/* coproc-core ETH service ops (see wlh_coproc_eth_ops_t). Runs on the core
 * task, nonblocking: answers from cached link state. */
int wlh_eth_get_info(void *context, uint32_t operation_id);

/* coproc-core ETHERNET_ETH channel receive callback (see
 * wlh_coproc_port_t.ethernet_eth_rx). Runs on the core task, nonblocking:
 * copies the frame into a TX DMA slot and returns PENDING; the completion is
 * signalled from the DMA TX interrupt via the ETH worker thread. */
wlh_coproc_ethernet_rx_result_t wlh_eth_rx_from_core(
    void *context,
    uint32_t session_id,
    uint8_t channel,
    const uint8_t *frame,
    size_t size
);

void wlh_eth_get_stats(wlh_eth_stats_t *stats);

/* Transport reset lifecycle hooks. wlh_eth_transport_dead() runs on the USB
 * TX thread the moment the transport reports a reset — over 100 ms before
 * the link-control thread stops the core — and blocks every core ingress
 * path (frame forwarding, link events, TX completions) so the ETH worker and
 * link threads can never touch core objects while they are being destroyed.
 * wlh_eth_transport_alive() runs after the core restart succeeds and
 * re-enables them. */
void wlh_eth_transport_dead(void);
void wlh_eth_transport_alive(void);

#endif
