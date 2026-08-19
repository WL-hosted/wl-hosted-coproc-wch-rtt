/*
 * WL-hosted wire protocol over a CherryUSB vendor bulk interface on the
 * CH32V307/CH32V317 USBHS controller (PB6/PB7, USB 2.0 High Speed).
 *
 * Mirrors the ESP-IDF adapter (wl-hosted-coproc-esp-idf
 * main/transports/usb/transport_usb.c): raw wire frames on the bulk byte
 * stream, reassembled by the 24-byte frame header; USB packet boundaries
 * carry no frame semantics. RT-Thread edition.
 *
 * RT-Thread constraint: endpoint and event callbacks run in USBHS ISR
 * context, where only plain flag stores and rt_sem_release() are legal
 * (rt_event_recv/rt_sem_take assert even with RT_WAITING_NO). All state
 * consumption happens in the transport threads.
 */
#include "transport.h"

#include <string.h>

#include <ipc/ringbuffer.h>
#include <rtthread.h>

#include "firmware_config.h"
#include "usb_ch32_usbhs_reg.h"
#include "usbd_core.h"
#include "usbhs_lowlevel.h"
#include "wlh/log.h"

#define WLH_USB_OUT_CHUNK WLH_USB_EP_MPS

#define FRAME_HEADER_SIZE 24u
#define FRAME_MAGIC_BYTE0 0x57u
#define FRAME_MAGIC_BYTE1 0x4cu
#define FRAME_PROTOCOL_MAJOR 1u
#define FRAME_FLAGS_MASK 0x03u
#define FRAME_CHANNEL_OFFSET 4u

static const char *TAG = "wlh-usb";

typedef struct tx_job {
    uint8_t *frame;
    size_t size;
    wlh_coproc_tx_complete_fn completion;
    void *completion_context;
} tx_job_t;

typedef struct usb_transport {
    wlh_coproc_t *coproc;
    size_t max_frame_size;
    wlh_transport_reset_fn on_reset;
    void *reset_context;

    rt_mq_t tx_control_queue;
    rt_mq_t tx_data_queue;
    rt_sem_t tx_done;
    rt_sem_t tx_wakeup;
    rt_sem_t rx_avail;
    /* Gate: released by the CONFIGURED ISR; the tx thread blocks here while
     * the device is unconfigured. The `configured` flag is the authority;
     * the semaphore only provides the wakeup. */
    rt_sem_t configured_gate;
    struct rt_ringbuffer rx_ring;
    rt_thread_t tx_thread;
    rt_thread_t rx_thread;
    volatile bool stopping;
    /* Set by the RESET ISR, consumed by the tx thread (thread context). */
    volatile bool reset_pending;
    volatile bool configured;
    /* Set when the RX ring could not take another maximum packet and the OUT
     * endpoint was deliberately left NAKed; the RX thread re-arms it once
     * draining made room. Backpressure instead of silent byte loss. */
    volatile bool out_paused;
    /* The reset that precedes the first CONFIGURED event belongs to initial
     * enumeration, before any WL-hosted session exists. Later resets must
     * still tear down the active session even if reconfiguration is fast. */
    volatile bool initial_configuration_seen;
    wlh_transport_stats_t stats;

    uint8_t rx_frame[FRAME_HEADER_SIZE + WLH_WIRE_MAX_FRAME_SIZE];
    size_t rx_frame_length;
} usb_transport_t;

static usb_transport_t transport;

/* USBHS endpoint DMA requires 4-byte aligned buffers. */
__attribute__((aligned(4)))
static uint8_t out_chunk[WLH_USB_OUT_CHUNK];

static uint8_t rx_ring_pool[WLH_USB_RX_RING_SIZE];
static uint8_t rx_drain[WLH_USB_OUT_CHUNK];

static char serial_string[13];
static const char langid_string[] = {0x09, 0x04};

static bool is_control_frame(const uint8_t *frame, size_t size) {
    if (frame == NULL || size < FRAME_HEADER_SIZE)
        return false;
    return frame[FRAME_CHANNEL_OFFSET] == WLH_CHANNEL_LINK_CONTROL ||
           frame[FRAME_CHANNEL_OFFSET] == WLH_CHANNEL_CONTROL_RPC;
}

/* ------------------------------------------------------------------ */
/* Descriptors                                                         */
/* ------------------------------------------------------------------ */

#define USB_CONFIG_SIZE (9 + 9 + 7 + 7)

static const uint8_t device_descriptor[] = {USB_DEVICE_DESCRIPTOR_INIT(
    USB_2_0, 0x00, 0x00, 0x00, WLH_USB_VID, WLH_USB_PID, 0x0100, 0x01
)};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(
        USB_CONFIG_SIZE,
        0x01,
        0x01,
        USB_CONFIG_BUS_POWERED,
        WLH_USB_MAX_POWER_MA / 2u
    ),
    USB_INTERFACE_DESCRIPTOR_INIT(0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00),
    USB_ENDPOINT_DESCRIPTOR_INIT(
        WLH_USB_EP_OUT, USB_ENDPOINT_TYPE_BULK, WLH_USB_EP_MPS, 0x00
    ),
    USB_ENDPOINT_DESCRIPTOR_INIT(
        WLH_USB_EP_IN, USB_ENDPOINT_TYPE_BULK, WLH_USB_EP_MPS, 0x00
    ),
};

static const uint8_t *device_descriptor_callback(uint8_t speed) {
    (void)speed;
    return device_descriptor;
}
static const uint8_t *config_descriptor_callback(uint8_t speed) {
    (void)speed;
    return config_descriptor;
}
static const char *string_descriptor_callback(uint8_t speed, uint8_t index) {
    static const char *strings[] = {
        langid_string,
        "WL-hosted",
        WLH_USB_PRODUCT_STRING,
        serial_string,
    };
    (void)speed;
    if (index >= sizeof(strings) / sizeof(strings[0]))
        return NULL;
    return strings[index];
}

static const struct usb_descriptor usb_descriptors = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

/* ------------------------------------------------------------------ */
/* RX: bulk OUT -> ring buffer -> reassembly thread -> core            */
/* ------------------------------------------------------------------ */

static void arm_out_read(uint8_t busid) {
    (void)usbd_ep_start_read(
        busid, WLH_USB_EP_OUT, out_chunk, sizeof(out_chunk)
    );
}

/* Arm one maximum-size packet at a time. If a larger buffer is armed,
 * CherryUSB waits for a short packet before completing the read; a wire
 * frame whose size is exactly a multiple of 512 bytes would then remain
 * buffered indefinitely. rx_feed() already reassembles the byte stream.
 *
 * The endpoint is re-armed only while the ring holds room for another
 * maximum packet, so the put below always fits: the arm-time check
 * guarantees the space is still there when the data arrives (the ISR is the
 * only producer). When the ring is too full the endpoint stays NAKed and the
 * host's USB stack retries — backpressure instead of silent byte loss, which
 * would desynchronize the wire stream (decode errors, sequence gaps,
 * stranded credits). The RX thread re-arms after draining. */
static void out_endpoint_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)ep;
    if (nbytes != 0u && !transport.stopping) {
        rt_size_t written =
            rt_ringbuffer_put(&transport.rx_ring, out_chunk, nbytes);
        transport.stats.rx_isr_packets++;
        if (written != (rt_size_t)nbytes) {
            /* Cannot happen while the arm-time space check holds; keep the
             * counter as a tripwire. */
            transport.stats.rx_overruns++;
        } else {
            transport.stats.rx_bytes += nbytes;
        }
        rt_sem_release(transport.rx_avail);
    }
    if (!transport.stopping &&
        rt_ringbuffer_space_len(&transport.rx_ring) >= WLH_USB_OUT_CHUNK) {
        arm_out_read(busid);
    } else {
        if (!transport.out_paused)
            transport.stats.rx_pauses++;
        transport.out_paused = true;
    }
}

static void in_endpoint_callback(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    (void)nbytes;
    if (transport.tx_done != RT_NULL)
        rt_sem_release(transport.tx_done);
}

static struct usbd_endpoint out_endpoint = {
    .ep_addr = WLH_USB_EP_OUT,
    .ep_cb = out_endpoint_callback,
};
static struct usbd_endpoint in_endpoint = {
    .ep_addr = WLH_USB_EP_IN,
    .ep_cb = in_endpoint_callback,
};

static struct usbd_interface vendor_interface = {0};

static bool frame_header_plausible(const uint8_t *header) {
    return header[0] == FRAME_MAGIC_BYTE0 && header[1] == FRAME_MAGIC_BYTE1 &&
           header[2] == FRAME_PROTOCOL_MAJOR &&
           header[3] == FRAME_HEADER_SIZE &&
           (header[5] & (uint8_t)~FRAME_FLAGS_MASK) == 0u;
}

static void rx_consume(size_t count) {
    transport.rx_frame_length -= count;
    if (transport.rx_frame_length != 0u) {
        memmove(
            transport.rx_frame,
            transport.rx_frame + count,
            transport.rx_frame_length
        );
    }
}

static void rx_feed(const uint8_t *data, size_t size) {
    static bool resync_active;
    static size_t last_frame_size;

    if (transport.rx_frame_length + size > sizeof(transport.rx_frame)) {
        transport.stats.rx_resync_bytes += transport.rx_frame_length;
        transport.rx_frame_length = 0u;
        return;
    }
    memcpy(transport.rx_frame + transport.rx_frame_length, data, size);
    transport.rx_frame_length += size;

    while (transport.rx_frame_length >= FRAME_HEADER_SIZE) {
        size_t frame_size;
        if (!frame_header_plausible(transport.rx_frame) ||
            FRAME_HEADER_SIZE + (size_t)transport.rx_frame[6] +
                    ((size_t)transport.rx_frame[7] << 8) >
                transport.max_frame_size) {
            if (!resync_active) {
                resync_active = true;
                /* First byte of a desync run: dump what the scanner sees so
                 * the shift source is identifiable (previous frame overrun,
                 * dropped USB bytes, ...). */
                WLH_LOGW(
                    TAG,
                    "rx resync: after_size=%u buffered=%u head=%02x %02x %02x "
                    "%02x %02x %02x %02x %02x",
                    (unsigned)last_frame_size,
                    (unsigned)transport.rx_frame_length,
                    transport.rx_frame[0], transport.rx_frame[1],
                    transport.rx_frame[2], transport.rx_frame[3],
                    transport.rx_frame[4], transport.rx_frame[5],
                    transport.rx_frame[6], transport.rx_frame[7]
                );
            }
            transport.stats.rx_resync_bytes++;
            rx_consume(1u);
            continue;
        }
        frame_size = FRAME_HEADER_SIZE + (size_t)transport.rx_frame[6] +
                     ((size_t)transport.rx_frame[7] << 8);
        /* A long frame spans several 512-byte bulk packets; wait until the
         * whole frame is buffered. Consuming more than rx_frame_length here
         * would underflow the size_t and turn rx_consume's memmove into a
         * RAM-wide wild copy. */
        if (transport.rx_frame_length < frame_size)
            break;
        wlh_coproc_result_t result = wlh_coproc_on_frame(
            transport.coproc, transport.rx_frame, frame_size
        );
        if (result != WLH_COPROC_OK) {
            WLH_LOGW(
                TAG,
                "RX frame rejected: bytes=%u result=%d",
                (unsigned)frame_size,
                (int)result
            );
        } else {
            transport.stats.rx_feed_frames++;
        }
        last_frame_size = frame_size;
        resync_active = false;
        rx_consume(frame_size);
    }
}

static void rx_thread_entry(void *parameter) {
    (void)parameter;
    for (;;) {
        rt_sem_take(transport.rx_avail, RT_WAITING_FOREVER);
        for (;;) {
            rt_size_t size = rt_ringbuffer_get(
                &transport.rx_ring, rx_drain, sizeof(rx_drain)
            );
            if (size == 0)
                break;
            rx_feed(rx_drain, size);
        }
        /* Re-arm the OUT endpoint once draining made room for another
         * maximum packet. While out_paused the endpoint is NAKed and no ISR
         * runs, so this thread owns the re-arm. */
        if (transport.out_paused && !transport.stopping &&
            rt_ringbuffer_space_len(&transport.rx_ring) >= WLH_USB_OUT_CHUNK) {
            transport.out_paused = false;
            arm_out_read(WLH_USB_BUS_ID);
        }
    }
}

/* ------------------------------------------------------------------ */
/* TX: core submit -> queue -> thread -> bulk IN                       */
/* ------------------------------------------------------------------ */

int wlh_transport_submit_tx(
    void *context,
    uint8_t *frame,
    size_t size,
    wlh_coproc_tx_complete_fn completion,
    void *completion_context
) {
    tx_job_t job;
    rt_mq_t queue;
    (void)context;
    job.frame = frame;
    job.size = size;
    job.completion = completion;
    job.completion_context = completion_context;
    if (transport.stopping)
        return -1;
    queue = is_control_frame(frame, size) ? transport.tx_control_queue
                                          : transport.tx_data_queue;
    if (rt_mq_send_wait(queue, &job, sizeof(job), 0) != RT_EOK) {
        WLH_LOGW(
            TAG,
            "%s tx queue full: dropping %u bytes",
            is_control_frame(frame, size) ? "control" : "data",
            (unsigned)size
        );
        transport.stats.tx_failures++;
        return -1;
    }
    rt_sem_release(transport.tx_wakeup);
    return 0;
}

static void flush_tx_queue(int status) {
    tx_job_t job;
    rt_mq_t queues[] = {
        transport.tx_control_queue,
        transport.tx_data_queue,
    };
    size_t index;
    for (index = 0u; index < sizeof(queues) / sizeof(queues[0]); ++index) {
        while (rt_mq_recv(queues[index], &job, sizeof(job), RT_WAITING_NO) ==
               (rt_size_t)sizeof(job)) {
            if (job.frame == RT_NULL)
                continue;
            job.completion(job.completion_context, job.frame, job.size, status);
        }
    }
}

static void usb_event_handler(uint8_t busid, uint8_t event);

static void usb_stack_register(void) {
    usbd_desc_register(WLH_USB_BUS_ID, &usb_descriptors);
    usbd_add_interface(WLH_USB_BUS_ID, &vendor_interface);
    usbd_add_endpoint(WLH_USB_BUS_ID, &out_endpoint);
    usbd_add_endpoint(WLH_USB_BUS_ID, &in_endpoint);
}

static void tx_thread_entry(void *parameter) {
    tx_job_t job;
    (void)parameter;
    for (;;) {
        rt_sem_take(transport.tx_wakeup, RT_WAITING_FOREVER);
        for (;;) {
            int status = 0;
            if (transport.reset_pending) {
                /* Bus reset/re-enumeration: queued frames belong to the dead
                 * session; complete them as failed in thread context and let
                 * the app restart the Core. The CONFIGURED ISR re-arms the
                 * OUT endpoint. */
                transport.reset_pending = false;
                flush_tx_queue(WLH_COPROC_TX_CANCELLED);
                if (transport.on_reset != RT_NULL)
                    transport.on_reset(transport.reset_context);
                continue;
            }
            /* Control RPC/link frames must not wait behind a sustained
             * Ethernet burst. Process all work already admitted without a
             * fixed polling delay; new submissions wake this thread. */
            if (rt_mq_recv(
                    transport.tx_control_queue, &job, sizeof(job), RT_WAITING_NO
                ) != (rt_size_t)sizeof(job) &&
                rt_mq_recv(
                    transport.tx_data_queue, &job, sizeof(job), RT_WAITING_NO
                ) != (rt_size_t)sizeof(job))
                break;
            if (job.frame == RT_NULL)
                continue;

            /* Wait for the host to configure the device. A reset arriving
             * during the wait is handled before touching the endpoint: the
             * job belongs to the dead session and must not be written. */
            while (!transport.configured && !transport.reset_pending) {
                rt_sem_take(transport.configured_gate, RT_WAITING_FOREVER);
            }
            if (transport.reset_pending) {
                job.completion(
                    job.completion_context,
                    job.frame,
                    job.size,
                    WLH_COPROC_TX_CANCELLED
                );
                continue;
            }

            /* Drop stale IN completions from transfers that already timed
             * out, so the wait below only observes the current transfer. */
            (void)rt_sem_take(transport.tx_done, RT_WAITING_NO);
            if (usbd_ep_start_write(
                    WLH_USB_BUS_ID, WLH_USB_EP_IN, job.frame, job.size
                ) != 0) {
                WLH_LOGW(
                    TAG, "bulk IN write rejected (%u bytes)", (unsigned)job.size
                );
                status = WLH_COPROC_TX_CANCELLED;
            } else if (rt_sem_take(
                           transport.tx_done,
                           rt_tick_from_millisecond(WLH_USB_TX_TIMEOUT_MS)
                       ) != RT_EOK) {
                /* The host went away without a bus reset (e.g. its process
                 * closed libusb while this write was armed). NAK the endpoint
                 * so the armed DMA buffer can never complete late, cancel the
                 * frame and restart the session below: the device stays
                 * enumerated and the next host process re-Helloes on the same
                 * configuration. */
                WLH_LOGW(
                    TAG,
                    "bulk IN transfer timed out (%u bytes)",
                    (unsigned)job.size
                );
                wlh_usbhs_ep_in_halt(WLH_USB_EP_IN & 0x7fu);
                /* No host is reading: every queued frame would hit the same
                 * 2 s timeout. Drain both queues now so the device settles
                 * into WAITING_FOR_HELLO immediately instead of looping one
                 * frame per timeout period. */
                flush_tx_queue(WLH_COPROC_TX_CANCELLED);
                status = WLH_COPROC_TX_CANCELLED;
            } else if (transport.reset_pending) {
                /* The RESET ISR interrupted the completion wait. */
                status = WLH_COPROC_TX_CANCELLED;
            }
            if (status == 0)
                transport.stats.tx_frames++;
            else
                transport.stats.tx_failures++;
            job.completion(job.completion_context, job.frame, job.size, status);
            if (status != 0 && transport.on_reset != RT_NULL) {
                /* Restart the Core session (re-Hello) after any failed
                 * transfer; the link-control thread coalesces repeats. */
                transport.on_reset(transport.reset_context);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* USB device events (ISR context: flag stores and rt_sem_release only) */
/* ------------------------------------------------------------------ */

static void usb_event_handler(uint8_t busid, uint8_t event) {
    switch (event) {
    case USBD_EVENT_RESET:
        transport.stats.bus_resets++;
        transport.configured = false;
        transport.reset_pending = true;
        /* The stack tears the endpoints down; the next CONFIGURED re-arms
         * the OUT endpoint unconditionally, so the pause flag must not
         * survive the reset. */
        transport.out_paused = false;
        /* Interrupt an active bulk-IN completion wait so the old session is
         * torn down before a newly configured host can negotiate. */
        if (transport.tx_done != RT_NULL)
            rt_sem_release(transport.tx_done);
        if (transport.tx_wakeup != RT_NULL)
            rt_sem_release(transport.tx_wakeup);
        break;
    case USBD_EVENT_CONFIGURED:
        if (!transport.initial_configuration_seen) {
            transport.initial_configuration_seen = true;
            transport.reset_pending = false;
            arm_out_read(busid);
        } else if (!transport.reset_pending) {
            arm_out_read(busid);
        }
        transport.configured = true;
        if (transport.configured_gate != RT_NULL)
            rt_sem_release(transport.configured_gate);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void fill_serial_string(void) {
    static const uint8_t mac[6] = WLH_ETH_MAC_ADDR;
    static const char hex[] = "0123456789abcdef";
    size_t index;
    for (index = 0u; index < sizeof(mac) / sizeof(mac[0]); ++index) {
        serial_string[index * 2u] = hex[mac[index] >> 4];
        serial_string[index * 2u + 1u] = hex[mac[index] & 0x0fu];
    }
    serial_string[12] = '\0';
}

int wlh_transport_start(const wlh_transport_config_t *config) {
    if (config == RT_NULL || config->coproc == RT_NULL ||
        config->max_frame_size > WLH_WIRE_MAX_FRAME_SIZE)
        return -1;

    memset(&transport, 0, sizeof(transport));
    transport.coproc = config->coproc;
    transport.max_frame_size = config->max_frame_size;
    transport.on_reset = config->on_reset;
    transport.reset_context = config->reset_context;

    transport.tx_control_queue = rt_mq_create(
        "wlh-txc",
        sizeof(tx_job_t),
        WLH_USB_CONTROL_TX_QUEUE_DEPTH,
        RT_IPC_FLAG_PRIO
    );
    transport.tx_data_queue = rt_mq_create(
        "wlh-txd",
        sizeof(tx_job_t),
        WLH_USB_DATA_TX_QUEUE_DEPTH,
        RT_IPC_FLAG_PRIO
    );
    transport.tx_done = rt_sem_create("wlh-txd", 0u, RT_IPC_FLAG_PRIO);
    transport.tx_wakeup = rt_sem_create("wlh-txw", 0u, RT_IPC_FLAG_PRIO);
    transport.rx_avail = rt_sem_create("wlh-rxa", 0u, RT_IPC_FLAG_PRIO);
    transport.configured_gate = rt_sem_create("wlh-cfg", 0u, RT_IPC_FLAG_PRIO);
    rt_ringbuffer_init(&transport.rx_ring, rx_ring_pool, sizeof(rx_ring_pool));
    if (transport.tx_control_queue == RT_NULL ||
        transport.tx_data_queue == RT_NULL || transport.tx_done == RT_NULL ||
        transport.tx_wakeup == RT_NULL || transport.rx_avail == RT_NULL ||
        transport.configured_gate == RT_NULL) {
        WLH_LOGE(TAG, "transport allocation failed");
        return -1;
    }

    transport.tx_thread = rt_thread_create(
        "wlh-usb-tx",
        tx_thread_entry,
        RT_NULL,
        2048u,
        WLH_USB_TX_TASK_PRIORITY,
        10
    );
    transport.rx_thread = rt_thread_create(
        "wlh-usb-rx",
        rx_thread_entry,
        RT_NULL,
        2048u,
        WLH_USB_RX_TASK_PRIORITY,
        10
    );
    if (transport.tx_thread == RT_NULL || transport.rx_thread == RT_NULL) {
        WLH_LOGE(TAG, "transport thread creation failed");
        return -1;
    }
    rt_thread_startup(transport.tx_thread);
    rt_thread_startup(transport.rx_thread);

    fill_serial_string();
    usb_stack_register();
    if (usbd_initialize(WLH_USB_BUS_ID, USBHS_BASE, usb_event_handler) != 0) {
        WLH_LOGE(TAG, "usbd_initialize failed");
        return -1;
    }
    WLH_LOGI(
        TAG, "usb device started (vid=%04x pid=%04x)", WLH_USB_VID, WLH_USB_PID
    );
    return 0;
}

size_t wlh_transport_max_frame_size(void) {
    return WLH_WIRE_MAX_FRAME_SIZE;
}

size_t wlh_transport_tx_capacity(void) {
    return WLH_USB_DATA_TX_QUEUE_DEPTH;
}

void wlh_transport_get_stats(wlh_transport_stats_t *stats) {
    if (stats == RT_NULL)
        return;
    *stats = transport.stats;
    stats->configured = transport.configured ? 1u : 0u;
}
