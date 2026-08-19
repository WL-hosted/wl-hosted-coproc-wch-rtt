/*
 * WL-hosted coprocessor application assembly for CH32V30x + RT-Thread.
 * Mirrors wl-hosted-coproc-esp-idf main/app/app_main.c: OSAL -> config ->
 * coproc init/start -> transport start -> link-reset thread.
 */
#include "wlh_app.h"

#include <string.h>

#include <rtthread.h>

#include "eth_backend.h"
#include "firmware_config.h"
#include "transport.h"
#include "wlh/log.h"
#include "wlh/rtt_osal.h"

#define LINK_EVENT_TRANSPORT_RESET (1u << 0)

static const char *TAG = "wlh-coproc";

static wlh_coproc_t coproc;
static wlh_rtt_osal_t rtt_osal;
static rt_event_t link_events;

static uint8_t *buffer_alloc(void *context, size_t size) {
    (void)context;
    return rt_malloc(size);
}
static void buffer_free(void *context, uint8_t *buffer) {
    (void)context;
    rt_free(buffer);
}

static int get_device_info(void *context, wlh_coproc_device_info_t *info) {
    (void)context;
    memset(info, 0, sizeof(*info));
    strncpy(info->vendor, "WCH", sizeof(info->vendor) - 1u);
    strncpy(info->mcu_model, WLH_MCU_NAME, sizeof(info->mcu_model) - 1u);
    strncpy(
        info->board_profile, WLH_BOARD_PROFILE, sizeof(info->board_profile) - 1u
    );
    /* Keep uid hidden until the adapter has a stable per-device identifier. */
    info->uid_size = 0u;
    return 0;
}

/* Called in task context after the active transport has reset. */
static void on_transport_reset(void *context) {
    (void)context;
    /* Runs on the USB TX thread, over 100 ms before the link-control thread
     * below stops the core (its settle delay): cut every core ingress path in
     * the ETH backend first, so its worker/link threads cannot touch core
     * objects while they are being destroyed. */
    wlh_eth_transport_dead();
    rt_event_send(link_events, LINK_EVENT_TRANSPORT_RESET);
}

/* Restarts the Core after a transport reset so the link renegotiates Hello
 * with a fresh session. */
static void link_control_thread(void *parameter) {
    rt_uint32_t bits = 0u;
    (void)parameter;
    for (;;) {
        bits = 0u;
        rt_event_recv(
            link_events,
            LINK_EVENT_TRANSPORT_RESET,
            RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
            RT_WAITING_FOREVER,
            &bits
        );
        /* Let the re-enumeration settle before re-accepting frames. */
        rt_thread_mdelay(100u);
        WLH_LOGW(TAG, "transport reset: restarting link core");
        if (wlh_coproc_stop(&coproc) != WLH_COPROC_OK)
            WLH_LOGW(TAG, "core stop failed during restart");
        /* The stopped core task's TCB/stack leave via the defunct queue and
         * are reclaimed by the idle thread; give it a slice before the
         * restart allocates the replacement task on this 64 KiB part. */
        rt_thread_mdelay(20u);
        if (wlh_coproc_start(&coproc) != WLH_COPROC_OK)
            WLH_LOGE(TAG, "core restart failed");
        else
            wlh_eth_transport_alive();
    }
}

const wlh_coproc_t *wlh_app_coproc(void) {
    return &coproc;
}

int wlh_app_init(void) {
    wlh_coproc_config_t config;
    wlh_transport_config_t transport_config;
    rt_thread_t link_thread;

    WLH_LOG_INIT();
    WLH_LOGI(
        TAG,
        "wl-hosted %s coprocessor (transport %s, profile %s)",
        WLH_MCU_NAME,
        WLH_TRANSPORT_NAME,
        WLH_BOARD_PROFILE
    );

    link_events = rt_event_create("wlh-lnk", RT_IPC_FLAG_PRIO);
    if (link_events == RT_NULL) {
        WLH_LOGE(TAG, "link event creation failed");
        return -1;
    }

    wlh_rtt_osal_init(&rtt_osal);

    memset(&config, 0, sizeof(config));
    config.port.context = RT_NULL;
    config.port.submit_tx = wlh_transport_submit_tx;
    config.port.ethernet_eth_rx = wlh_eth_rx_from_core;
    config.port.ethernet_tx_ready = wlh_eth_tx_ready;
    config.buffers = (wlh_coproc_buffer_ops_t){RT_NULL, buffer_alloc, buffer_free};
    config.osal = wlh_rtt_osal_ops(&rtt_osal);
    config.device_info =
        (wlh_coproc_device_info_ops_t){RT_NULL, get_device_info};
    config.eth = (wlh_coproc_eth_ops_t){RT_NULL, wlh_eth_get_info};
    strncpy(
        config.implementation_version,
        WLH_IMPLEMENTATION_VERSION,
        sizeof(config.implementation_version) - 1u
    );
    config.max_frame_size = wlh_transport_max_frame_size();
    config.heartbeat_interval_ms = WLH_HEARTBEAT_INTERVAL_MS;
    /* Credit is the in-flight window for the data path; it must not exceed
     * the EMAC TX ring depth (see firmware_config.h). */
    config.initial_credit = WLH_INITIAL_CREDIT;
    config.core_queue_depth = WLH_CORE_QUEUE_DEPTH;
    config.ethernet_tx_depth = (uint8_t)wlh_transport_tx_capacity();
    config.ethernet_tx_aggregation_limit = WLH_ETHERNET_TX_AGGREGATION_LIMIT;
    config.stop_timeout_ms = WLH_STOP_TIMEOUT_MS;
    config.core_task = (wlh_osal_task_attributes_t){
        "wlh-core", WLH_CORE_TASK_STACK, WLH_CORE_TASK_OSAL_PRIORITY
    };

    if (wlh_coproc_init(&coproc, &config) != WLH_COPROC_OK) {
        WLH_LOGE(TAG, "coproc init failed");
        return -1;
    }

    memset(&transport_config, 0, sizeof(transport_config));
    transport_config.coproc = &coproc;
    transport_config.max_frame_size = config.max_frame_size;
    transport_config.on_reset = on_transport_reset;
    transport_config.reset_context = RT_NULL;

    if (wlh_coproc_start(&coproc) != WLH_COPROC_OK) {
        WLH_LOGE(TAG, "coproc start failed");
        return -1;
    }
    if (wlh_eth_backend_init(&coproc) != 0) {
        WLH_LOGE(TAG, "eth backend init failed");
        return -1;
    }
    if (wlh_transport_start(&transport_config) != 0) {
        WLH_LOGE(TAG, "%s transport start failed", WLH_TRANSPORT_NAME);
        return -1;
    }
    link_thread = rt_thread_create(
        "wlh-linkctl",
        link_control_thread,
        RT_NULL,
        1024u,
        WLH_LINK_CTRL_TASK_PRIORITY,
        10
    );
    if (link_thread == RT_NULL) {
        WLH_LOGE(TAG, "link control thread creation failed");
        return -1;
    }
    rt_thread_startup(link_thread);
    WLH_LOGI(TAG, "coprocessor ready, waiting for host");
    return 0;
}
