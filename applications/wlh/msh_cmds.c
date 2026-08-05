/* FinSH/MSH diagnostics for the WL-hosted coprocessor firmware. */
#include <rtthread.h>

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
}
MSH_CMD_EXPORT_ALIAS(cmd_usb_stats, usb_stats, wl-hosted usb transport stats);
