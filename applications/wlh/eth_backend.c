/* See eth_backend.h for the design notes. */
#include "eth_backend.h"

#include <string.h>

#include <board.h>
#include <ch32v30x.h>
/* TODO(wch-sdk): SDK v2.7 ch32v30x_eth.h has nested comment openers around
 * its register sections. Remove this local suppression when the vendored SDK
 * fixes those comments; application warnings remain covered by -Werror. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include <ch32v30x_eth.h>
#pragma GCC diagnostic pop
#include <ch32v30x_it.h>
#include <rthw.h>
#include <rtthread.h>

#include "eth_phy.h"
#include "firmware_config.h"
#include "wlh/log.h"

#define TAG "wlh-eth"

/* Largest L2 frame accepted from the wire (CRC already stripped by the DMA
 * length field handling below). */
#define WLH_ETH_MIN_FRAME 14u
#define WLH_ETH_MAX_FRAME 1518u
#define FL_SHIFT 16u

typedef struct {
    ETH_DMADESCTypeDef *desc;
    uint32_t session_id;
    uint8_t channel;
    volatile uint8_t in_flight;
} eth_tx_slot_t;
static wlh_coproc_t *s_coproc;
static rt_sem_t s_eth_sem;

rt_align(4) static ETH_DMADESCTypeDef s_rx_desc[WLH_ETH_RX_DESC_NUM];
rt_align(4) static ETH_DMADESCTypeDef s_tx_desc[WLH_ETH_TX_DESC_NUM];
rt_align(4) static uint8_t s_rx_buf[WLH_ETH_RX_DESC_NUM][ETH_MAX_PACKET_SIZE];
rt_align(4) static uint8_t s_tx_buf[WLH_ETH_TX_DESC_NUM][ETH_MAX_PACKET_SIZE];
static ETH_DMADESCTypeDef *s_rx_cur;
static eth_tx_slot_t s_tx_slots[WLH_ETH_TX_DESC_NUM];
/* Ring-order producer index. The TX DMA fetches descriptors strictly in ring
 * order and suspends at the first unowned one, so the CPU must arm exactly
 * the descriptor the DMA will fetch next; any out-of-order allocation leaves
 * an armed descriptor behind the suspended DMA pointer indefinitely. */
static uint32_t s_tx_next;

static volatile uint8_t s_link_up;
static volatile uint8_t s_duplex_full;
static volatile wlh_coproc_eth_speed_t s_link_speed =
    WLH_COPROC_ETH_SPEED_UNSPECIFIED;
/* Set by the USB transport the instant a reset is observed; cleared by the
 * app only after the core has been restarted. While set, no core API may be
 * called from the ETH worker or link thread: the core is being torn down and
 * its queue/mutex objects vanish under any in-flight call. */
static volatile uint8_t s_transport_dead;
static const uint8_t s_mac[6] = WLH_ETH_MAC_ADDR;
static wlh_eth_stats_t s_stats;

/* MAC/DMA register programming copied from drv_eth.c ETH_Init (the PHY
 * reset/link-wait part lives in emac_init and the link thread instead). */
static void emac_mac_config(void) {
    ETH_InitTypeDef init;
    uint32_t tmpreg;

    ETH_StructInit(&init);
    init.ETH_Mode = ETH_Mode_FullDuplex;
    init.ETH_Speed = ETH_Speed_10M;
    init.ETH_AutoNegotiation = ETH_AutoNegotiation_Enable;
    init.ETH_LoopbackMode = ETH_LoopbackMode_Disable;
    init.ETH_RetryTransmission = ETH_RetryTransmission_Disable;
    init.ETH_AutomaticPadCRCStrip = ETH_AutomaticPadCRCStrip_Disable;
    /* Perfect unicast filtering on MAC Address0 instead of promiscuous mode:
     * the host's netif is the only consumer of frames addressed elsewhere, so
     * forwarding general LAN chatter would just spend device->host credits
     * (and CONGESTED windows) on traffic the host stack drops anyway.
     * Broadcast stays enabled (ARP/DHCP) and multicast unfiltered (IPv6
     * NS/mDNS) without programming the hash table. */
    init.ETH_ReceiveAll = ETH_ReceiveAll_Disable;
    init.ETH_BroadcastFramesReception = ETH_BroadcastFramesReception_Enable;
    init.ETH_PromiscuousMode = ETH_PromiscuousMode_Disable;
    init.ETH_MulticastFramesFilter = ETH_MulticastFramesFilter_None;
    init.ETH_UnicastFramesFilter = ETH_UnicastFramesFilter_Perfect;
    init.ETH_DropTCPIPChecksumErrorFrame =
        ETH_DropTCPIPChecksumErrorFrame_Enable;
    init.ETH_ReceiveStoreForward = ETH_ReceiveStoreForward_Enable;
    init.ETH_TransmitStoreForward = ETH_TransmitStoreForward_Enable;
    init.ETH_ForwardErrorFrames = ETH_ForwardErrorFrames_Enable;
    init.ETH_ForwardUndersizedGoodFrames =
        ETH_ForwardUndersizedGoodFrames_Enable;
    init.ETH_SecondFrameOperate = ETH_SecondFrameOperate_Disable;
    init.ETH_AddressAlignedBeats = ETH_AddressAlignedBeats_Enable;
    init.ETH_FixedBurst = ETH_FixedBurst_Enable;
    init.ETH_RxDMABurstLength = ETH_RxDMABurstLength_32Beat;
    init.ETH_TxDMABurstLength = ETH_TxDMABurstLength_32Beat;
    init.ETH_DMAArbitration = ETH_DMAArbitration_RoundRobin_RxTx_2_1;

    tmpreg = ETH->MACCR;
    tmpreg &= MACCR_CLEAR_MASK;
    tmpreg |= (uint32_t)(init.ETH_Watchdog | init.ETH_Jabber |
                         init.ETH_InterFrameGap | init.ETH_CarrierSense |
                         init.ETH_Speed | init.ETH_ReceiveOwn |
                         init.ETH_LoopbackMode | init.ETH_Mode |
                         init.ETH_ChecksumOffload | init.ETH_RetryTransmission |
                         init.ETH_AutomaticPadCRCStrip |
                         init.ETH_BackOffLimit | init.ETH_DeferralCheck);
    ETH->MACCR = tmpreg;
    wlh_eth_phy_configure_mac();

    ETH->MACFFR = (uint32_t)(init.ETH_ReceiveAll | init.ETH_SourceAddrFilter |
                             init.ETH_PassControlFrames |
                             init.ETH_BroadcastFramesReception |
                             init.ETH_DestinationAddrFilter |
                             init.ETH_PromiscuousMode |
                             init.ETH_MulticastFramesFilter |
                             init.ETH_UnicastFramesFilter);
    ETH->MACHTHR = init.ETH_HashTableHigh;
    ETH->MACHTLR = init.ETH_HashTableLow;

    tmpreg = ETH->MACFCR;
    tmpreg &= MACFCR_CLEAR_MASK;
    tmpreg |= (uint32_t)((init.ETH_PauseTime << 16) | init.ETH_ZeroQuantaPause |
                         init.ETH_PauseLowThreshold |
                         init.ETH_UnicastPauseFrameDetect |
                         init.ETH_ReceiveFlowControl |
                         init.ETH_TransmitFlowControl);
    ETH->MACFCR = tmpreg;
    ETH->MACVLANTR = (uint32_t)(init.ETH_VLANTagComparison |
                                init.ETH_VLANTagIdentifier);

    tmpreg = ETH->DMAOMR;
    tmpreg &= DMAOMR_CLEAR_MASK;
    tmpreg |= (uint32_t)(init.ETH_DropTCPIPChecksumErrorFrame |
                         init.ETH_ReceiveStoreForward |
                         init.ETH_FlushReceivedFrame |
                         init.ETH_TransmitStoreForward |
                         init.ETH_TransmitThresholdControl |
                         init.ETH_ForwardErrorFrames |
                         init.ETH_ForwardUndersizedGoodFrames |
                         init.ETH_ReceiveThresholdControl |
                         init.ETH_SecondFrameOperate);
    ETH->DMAOMR = tmpreg;
    ETH->DMABMR = (uint32_t)(init.ETH_AddressAlignedBeats |
                             init.ETH_FixedBurst | init.ETH_RxDMABurstLength |
                             init.ETH_TxDMABurstLength |
                             (init.ETH_DescriptorSkipLength << 2) |
                             init.ETH_DMAArbitration | ETH_DMABMR_USP);
}

static int emac_init(void) {
    if (wlh_eth_phy_init() != 0)
        return -1;

    emac_mac_config();
    ETH_MACAddressConfig(ETH_MAC_Address0, (uint8_t *)s_mac);

    ETH_DMATxDescChainInit(s_tx_desc, &s_tx_buf[0][0], WLH_ETH_TX_DESC_NUM);
    ETH_DMARxDescChainInit(s_rx_desc, &s_rx_buf[0][0], WLH_ETH_RX_DESC_NUM);
    s_rx_cur = s_rx_desc;
    for (uint32_t i = 0u; i < WLH_ETH_TX_DESC_NUM; i++) {
        s_tx_slots[i].desc = &s_tx_desc[i];
        s_tx_slots[i].in_flight = 0u;
    }

    /* NIS covers R/T; AIS|RBU lets the worker recover a stalled RX DMA. */
    ETH_DMAITConfig(
        ETH_DMA_IT_NIS | ETH_DMA_IT_R | ETH_DMA_IT_T | ETH_DMA_IT_AIS |
            ETH_DMA_IT_RBU,
        ENABLE
    );
    NVIC_SetPriority(ETH_IRQn, 1u << 4);
    NVIC_EnableIRQ(ETH_IRQn);
    ETH_Start();
    return 0;
}

void ETH_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void ETH_IRQHandler(void) {
    uint32_t sr;
    GET_INT_SP();
    rt_interrupt_enter();
    sr = ETH->DMASR;
    if (sr & ETH_DMA_IT_R) {
        s_stats.isr_rx++;
        ETH_DMAClearITPendingBit(ETH_DMA_IT_R);
    }
    if (sr & ETH_DMA_IT_T) {
        s_stats.isr_tx++;
        ETH_DMAClearITPendingBit(ETH_DMA_IT_T);
    }
    if (sr & ETH_DMA_IT_RBU)
        ETH_DMAClearITPendingBit(ETH_DMA_IT_RBU);
    if (sr & ETH_DMA_IT_AIS)
        ETH_DMAClearITPendingBit(ETH_DMA_IT_AIS);
    if (sr & ETH_DMA_IT_NIS)
        ETH_DMAClearITPendingBit(ETH_DMA_IT_NIS);
    rt_sem_release(s_eth_sem);
    rt_interrupt_leave();
    FREE_INT_SP();
}

/* Releases TX slots whose DMA has finished and completes the matching core
 * records. Runs on the ETH worker thread. */
static void eth_tx_complete_scan(void) {
    for (uint32_t i = 0u; i < WLH_ETH_TX_DESC_NUM; i++) {
        eth_tx_slot_t *slot = &s_tx_slots[i];
        uint32_t st;
        uint32_t session_id;
        uint8_t channel;
        int status;

        if (!slot->in_flight)
            continue;
        st = slot->desc->Status;
        if (st & ETH_DMATxDesc_OWN)
            continue;
        session_id = slot->session_id;
        channel = slot->channel;
        status = (st & ETH_DMATxDesc_ES) != 0u ? -1 : 0;
        slot->in_flight = 0u;
        if (status == 0)
            s_stats.tx_frames++;
        else
            s_stats.tx_errors++;
        /* Late completions from a dead session are dropped by the core. */
        wlh_coproc_ethernet_rx_complete(
            s_coproc, session_id, channel, 1u, status
        );
    }
}

/* Moves every received frame into the core. Runs on the ETH worker thread;
 * the core copies the frame synchronously, so the descriptor can be re-armed
 * immediately after wlh_coproc_ethernet_eth_send returns.
 *
 * Frames are only forwarded while a host session is READY. The MAC still
 * receives broadcast/multicast LAN chatter even with no host attached;
 * forwarding it would fail on the creditless ETHERNET_ETH channel and flip
 * the core into CONGESTED for nothing. Descriptors are still drained and
 * re-armed, otherwise the RX DMA stalls on RBUS. */
static void eth_rx_drain(void) {
    wlh_coproc_diagnostics_t diag;
    int ready;
    int drained = 0;

    /* While the transport is dead the core is being stopped and even
     * get_diagnostics would touch destroyed objects; drain and re-arm
     * without forwarding. */
    if (s_transport_dead) {
        ready = 0;
    } else {
        wlh_coproc_get_diagnostics(s_coproc, &diag);
        ready = diag.state == WLH_COPROC_STATE_READY;
    }
    for (;;) {
        ETH_DMADESCTypeDef *desc = s_rx_cur;
        uint32_t st = desc->Status;
        uint32_t len;

        if (st & ETH_DMARxDesc_OWN) {
            /* All descriptors owned by the DMA. A reported buffer-unavailable
             * condition stalls reception until explicitly resumed. */
            if (ETH->DMASR & ETH_DMASR_RBUS) {
                ETH->DMASR = ETH_DMASR_RBUS;
                ETH->DMARPDR = 0u;
            }
            break;
        }
        drained++;
        if ((st & ETH_DMARxDesc_ES) == 0u && (st & ETH_DMARxDesc_FS) != 0u &&
            (st & ETH_DMARxDesc_LS) != 0u) {
            len = (st & ETH_DMARxDesc_FL) >> FL_SHIFT;
            if (len >= 4u)
                len -= 4u; /* strip the trailing CRC */
            if (ready && len >= WLH_ETH_MIN_FRAME && len <= WLH_ETH_MAX_FRAME) {
                if (wlh_coproc_ethernet_eth_send(
                        s_coproc, (uint8_t *)desc->Buffer1Addr, len
                    ) == WLH_COPROC_OK) {
                    s_stats.rx_frames++;
                } else {
                    s_stats.rx_dropped++;
                }
            } else {
                s_stats.rx_dropped++;
            }
        } else {
            s_stats.rx_errors++;
        }
        desc->Status = ETH_DMARxDesc_OWN;
        s_rx_cur = (ETH_DMADESCTypeDef *)desc->Buffer2NextDescAddr;
    }
    if (drained == 0)
        s_stats.worker_empty_rx++;
}

static void eth_worker_thread(void *parameter) {
    (void)parameter;
    for (;;) {
        rt_sem_take(s_eth_sem, RT_WAITING_FOREVER);
        s_stats.worker_wakes++;
        eth_tx_complete_scan();
        eth_rx_drain();
    }
}

/* Tracks the selected PHY link and negotiated mode once per second.
 *
 * Link events are only reported while a host session is READY: pre-session
 * the CONTROL_RPC channel has no credit, and a failed send would flip the
 * core into CONGESTED for nothing. A host that connects later learns the
 * current state through GET_INFO, so nothing is lost by gating. */
static void eth_link_report(int up) {
    wlh_coproc_diagnostics_t diag;

    if (s_transport_dead)
        return;
    wlh_coproc_get_diagnostics(s_coproc, &diag);
    if (diag.state != WLH_COPROC_STATE_READY)
        return;
    if (up) {
        wlh_coproc_eth_link_state_changed(
            s_coproc,
            WLH_COPROC_ETH_LINK_STATE_UP,
            s_link_speed,
            s_duplex_full ? WLH_COPROC_ETH_DUPLEX_FULL
                          : WLH_COPROC_ETH_DUPLEX_HALF
        );
    } else {
        wlh_coproc_eth_link_state_changed(
            s_coproc,
            WLH_COPROC_ETH_LINK_STATE_DOWN,
            WLH_COPROC_ETH_SPEED_UNSPECIFIED,
            WLH_COPROC_ETH_DUPLEX_UNSPECIFIED
        );
    }
}

static void eth_link_thread(void *parameter) {
    wlh_eth_phy_status_t status;
    uint8_t last_up = 0u;
    uint8_t last_duplex = 0u;
    uint16_t last_speed = 0u;
    (void)parameter;
    for (;;) {
        int changed;
        uint8_t was_up;

        rt_thread_mdelay(WLH_ETH_LINK_POLL_MS);
        wlh_eth_phy_read_status(&status);
        was_up = last_up;
        changed = status.link_up != last_up;
        if (status.link_up &&
            (status.speed_mbps != last_speed ||
             status.full_duplex != last_duplex)) {
            changed = 1;
        }
        if (!changed)
            continue;
        last_up = status.link_up;
        last_speed = status.speed_mbps;
        last_duplex = status.full_duplex;
        s_link_up = status.link_up;
        s_duplex_full = status.full_duplex;
        if (status.speed_mbps == 100u)
            s_link_speed = WLH_COPROC_ETH_SPEED_100M;
        else if (status.speed_mbps == 10u)
            s_link_speed = WLH_COPROC_ETH_SPEED_10M;
        else
            s_link_speed = WLH_COPROC_ETH_SPEED_UNSPECIFIED;
        if (status.link_up) {
            if (!was_up)
                s_stats.link_ups++;
            WLH_LOGI(
                TAG,
                "link up (%uM %s duplex)",
                (unsigned int)status.speed_mbps,
                s_duplex_full ? "full" : "half"
            );
        } else {
            s_link_speed = WLH_COPROC_ETH_SPEED_UNSPECIFIED;
            WLH_LOGI(TAG, "link down");
        }
        eth_link_report(status.link_up);
    }
}

int wlh_eth_get_info(void *context, uint32_t operation_id) {
    wlh_coproc_eth_info_t info;
    (void)context;

    memset(&info, 0, sizeof(info));
    info.link_state = s_link_up ? WLH_COPROC_ETH_LINK_STATE_UP
                                : WLH_COPROC_ETH_LINK_STATE_DOWN;
    memcpy(info.mac_address, s_mac, sizeof(info.mac_address));
    info.speed = s_link_up ? s_link_speed : WLH_COPROC_ETH_SPEED_UNSPECIFIED;
    info.duplex = s_link_up ? (s_duplex_full ? WLH_COPROC_ETH_DUPLEX_FULL
                                             : WLH_COPROC_ETH_DUPLEX_HALF)
                            : WLH_COPROC_ETH_DUPLEX_UNSPECIFIED;
    /* Runs on the core task; the completion is queued, so answering inline is
     * safe and keeps the whole operation nonblocking. */
    wlh_coproc_eth_info_ready(s_coproc, operation_id, 0, &info);
    return 0;
}

wlh_coproc_ethernet_rx_result_t wlh_eth_rx_from_core(
    void *context,
    uint32_t session_id,
    uint8_t channel,
    const uint8_t *frame,
    size_t size
) {
    eth_tx_slot_t *slot;
    rt_base_t level;
    (void)context;

    /* s_transport_dead: the core is being stopped; arming a slot here would
     * strand its completion past the point where the core is destroyed. */
    if (frame == RT_NULL || size < WLH_ETH_MIN_FRAME ||
        size > ETH_MAX_PACKET_SIZE || !s_link_up || s_transport_dead) {
        s_stats.tx_rejected++;
        return WLH_COPROC_ETHERNET_RX_REJECTED;
    }

    /* The IRQ-off section serialises slot allocation against the worker
     * thread's completion scan on this uniprocessor target. Allocation is in
     * ring order (see s_tx_next), never first-free. */
    level = rt_hw_interrupt_disable();
    slot = &s_tx_slots[s_tx_next];
    if (slot->in_flight) {
        rt_hw_interrupt_enable(level);
        s_stats.tx_rejected++;
        return WLH_COPROC_ETHERNET_RX_REJECTED;
    }
    slot->in_flight = 1u;
    slot->session_id = session_id;
    slot->channel = channel;
    memcpy((void *)slot->desc->Buffer1Addr, frame, size);
    slot->desc->ControlBufferSize = (uint32_t)size & ETH_DMATxDesc_TBS1;
    /* Full overwrite, not |=: the DMA write-back on completion replaces the
     * whole Status word, so the control bits must be restored on every arm —
     * TCH keeps the chain intact and IC raises ETH_DMA_IT_T, without which
     * TX completions are only seen when an unrelated RX interrupt happens to
     * wake the worker (credit return then stalls for seconds). */
    slot->desc->Status = ETH_DMATxDesc_TCH | ETH_DMATxDesc_IC |
                         ETH_DMATxDesc_LS | ETH_DMATxDesc_FS | ETH_DMATxDesc_OWN;
    s_tx_next = (s_tx_next + 1u) % WLH_ETH_TX_DESC_NUM;
    /* Resume the TX DMA. The poll demand is unconditional: a suspended DMA
     * refetches the (now owned) current descriptor, while a running DMA is
     * unaffected. A TBUS check race could otherwise strand this frame until
     * the next unrelated transmit. */
    if (ETH->DMASR & ETH_DMASR_TBUS)
        ETH->DMASR = ETH_DMASR_TBUS;
    ETH->DMATPDR = 0u;
    rt_hw_interrupt_enable(level);
    return WLH_COPROC_ETHERNET_RX_PENDING;
}

void wlh_eth_transport_dead(void) {
    uint32_t i;
    rt_base_t level = rt_hw_interrupt_disable();
    s_transport_dead = 1u;
    /* Pending host->wire frames die with the old session; the stopping core
     * frees their jobs itself, so the completion scan must not report them. */
    for (i = 0u; i < WLH_ETH_TX_DESC_NUM; i++)
        s_tx_slots[i].in_flight = 0u;
    rt_hw_interrupt_enable(level);
}

void wlh_eth_transport_alive(void) {
    s_transport_dead = 0u;
}

void wlh_eth_get_stats(wlh_eth_stats_t *stats) {
    rt_base_t level = rt_hw_interrupt_disable();
    *stats = s_stats;
    rt_hw_interrupt_enable(level);
    stats->link_up = s_link_up;
    stats->duplex_full = s_duplex_full;
}

int wlh_eth_backend_init(wlh_coproc_t *coproc) {
    rt_thread_t thread;

    s_coproc = coproc;
    /* The semaphore must exist before emac_init enables the IRQ and starts
     * the DMA: with a cable attached, RX interrupts can fire immediately. */
    s_eth_sem = rt_sem_create("wlh-eth", 0u, RT_IPC_FLAG_PRIO);
    if (s_eth_sem == RT_NULL) {
        WLH_LOGE(TAG, "eth semaphore creation failed");
        return -1;
    }
    if (emac_init() != 0)
        return -1;

    thread = rt_thread_create(
        "wlh-eth",
        eth_worker_thread,
        RT_NULL,
        WLH_ETH_RX_TASK_STACK,
        WLH_ETH_RX_TASK_PRIORITY,
        10
    );
    if (thread == RT_NULL) {
        WLH_LOGE(TAG, "eth worker thread creation failed");
        return -1;
    }
    rt_thread_startup(thread);
    thread = rt_thread_create(
        "wlh-ethlnk",
        eth_link_thread,
        RT_NULL,
        WLH_ETH_LINK_TASK_STACK,
        WLH_ETH_LINK_TASK_PRIORITY,
        10
    );
    if (thread == RT_NULL) {
        WLH_LOGE(TAG, "eth link thread creation failed");
        return -1;
    }
    rt_thread_startup(thread);
    WLH_LOGI(
        TAG,
        "eth backend started (%s, mac %02x:%02x:%02x:%02x:%02x:%02x)",
        wlh_eth_phy_name(),
        s_mac[0],
        s_mac[1],
        s_mac[2],
        s_mac[3],
        s_mac[4],
        s_mac[5]
    );
    return 0;
}
