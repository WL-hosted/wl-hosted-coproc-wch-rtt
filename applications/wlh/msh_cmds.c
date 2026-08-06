/* FinSH/MSH diagnostics for the WL-hosted coprocessor firmware. */
#include <rtthread.h>

#include "eth_backend.h"
#include "transport.h"
#include "wlh/coproc.h"
#include "wlh_app.h"

static const char *coproc_state_name(wlh_coproc_state_t state) {
    switch (state) {
    case WLH_COPROC_STATE_STOPPED:
        return "stopped";
    case WLH_COPROC_STATE_WAITING_FOR_HELLO:
        return "waiting_for_hello";
    case WLH_COPROC_STATE_READY:
        return "ready";
    case WLH_COPROC_STATE_CONGESTED:
        return "congested";
    case WLH_COPROC_STATE_FAILED:
        return "failed";
    default:
        return "?";
    }
}

static void cmd_wlh_status(int argc, char **argv) {
    wlh_coproc_diagnostics_t diag;
    rt_size_t total = 0u, used = 0u, max_used = 0u;
    (void)argc;
    (void)argv;

    wlh_coproc_get_diagnostics(wlh_app_coproc(), &diag);
    rt_kprintf("state=%s session=%lu\n",
               coproc_state_name(diag.state),
               (unsigned long)diag.session_id);
    rt_kprintf("tx_frames=%lu rx_frames=%lu checksum_errors=%lu "
               "sequence_gaps=%lu peer_resets=%lu\n",
               (unsigned long)diag.tx_frames,
               (unsigned long)diag.rx_frames,
               (unsigned long)diag.checksum_errors,
               (unsigned long)diag.sequence_gaps,
               (unsigned long)diag.peer_resets);
    rt_kprintf("rpc_requests=%lu eth_rx_rejected=%lu eth_rx_failed=%lu\n",
               (unsigned long)diag.rpc_requests,
               (unsigned long)diag.ethernet_rx_rejected,
               (unsigned long)diag.ethernet_rx_failed);
    rt_memory_info(&total, &used, &max_used);
    rt_kprintf("heap: total=%lu used=%lu max=%lu\n",
               (unsigned long)total,
               (unsigned long)used,
               (unsigned long)max_used);
}
MSH_CMD_EXPORT_ALIAS(cmd_wlh_status, wlh_status, wl-hosted coproc status);

static void cmd_usb_stats(int argc, char **argv) {
    wlh_transport_stats_t stats;
    (void)argc;
    (void)argv;

    wlh_transport_get_stats(&stats);
    rt_kprintf("configured=%lu bus_resets=%lu\n",
               (unsigned long)stats.configured,
               (unsigned long)stats.bus_resets);
    rt_kprintf("rx_bytes=%lu rx_overruns=%lu tx_frames=%lu tx_failures=%lu\n",
               (unsigned long)stats.rx_bytes,
               (unsigned long)stats.rx_overruns,
               (unsigned long)stats.tx_frames,
               (unsigned long)stats.tx_failures);
    rt_kprintf("rx_isr_packets=%lu rx_feed_frames=%lu rx_resync_bytes=%lu "
               "rx_pauses=%lu\n",
               (unsigned long)stats.rx_isr_packets,
               (unsigned long)stats.rx_feed_frames,
               (unsigned long)stats.rx_resync_bytes,
               (unsigned long)stats.rx_pauses);
    {
        extern volatile uint32_t g_usbhs_rx_tog_drops;
        rt_kprintf("rx_tog_drops=%lu\n", (unsigned long)g_usbhs_rx_tog_drops);
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_usb_stats, usb_stats, wl-hosted usb transport stats);

static void cmd_eth_stats(int argc, char **argv) {
    wlh_eth_stats_t stats;
    (void)argc;
    (void)argv;

    wlh_eth_get_stats(&stats);
    rt_kprintf("link=%s duplex=%s link_ups=%lu\n",
               stats.link_up ? "up" : "down",
               stats.duplex_full ? "full" : "half",
               (unsigned long)stats.link_ups);
    rt_kprintf("rx_frames=%lu rx_dropped=%lu rx_errors=%lu\n",
               (unsigned long)stats.rx_frames,
               (unsigned long)stats.rx_dropped,
               (unsigned long)stats.rx_errors);
    rt_kprintf("tx_frames=%lu tx_errors=%lu tx_rejected=%lu\n",
               (unsigned long)stats.tx_frames,
               (unsigned long)stats.tx_errors,
               (unsigned long)stats.tx_rejected);
    rt_kprintf("isr_rx=%lu isr_tx=%lu wakes=%lu empty_rx=%lu\n",
               (unsigned long)stats.isr_rx,
               (unsigned long)stats.isr_tx,
               (unsigned long)stats.worker_wakes,
               (unsigned long)stats.worker_empty_rx);
}
MSH_CMD_EXPORT_ALIAS(cmd_eth_stats, eth_stats, wl-hosted eth backend stats);
