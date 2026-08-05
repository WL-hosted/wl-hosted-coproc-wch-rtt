#ifndef WLH_FIRMWARE_CONFIG_H
#define WLH_FIRMWARE_CONFIG_H

/* Single source for the tunables of the WL-hosted CH32V307 coprocessor
 * firmware. The USB identity must stay in sync with the host side
 * (wl-hosted-host-macos-sim --usb <vid:pid>, hardcoded interface 0 and
 * endpoints 0x01/0x81). */

#define WLH_MCU_NAME "CH32V307VC"
#define WLH_TRANSPORT_NAME "usb-hs"
#define WLH_BOARD_PROFILE "wch.ch32v307.custom.usb-eth"
#define WLH_IMPLEMENTATION_VERSION "0.1.0"

#define WLH_USB_VID 0x1A86u
#define WLH_USB_PID 0x8210u
#define WLH_USB_MAX_POWER_MA 500u

/* USBHS device on PB6/PB7, USB 2.0 High Speed: bulk MPS is 512. */
#define WLH_USB_EP_OUT 0x01u
#define WLH_USB_EP_IN 0x81u
#define WLH_USB_EP_MPS 512u
#define WLH_USB_BUS_ID 0u

/* 64 KiB RAM budget: a 2048-byte wire frame still fits a full 1518-byte L2
 * frame plus raw-record and wire headers; the host clamps its TX to the
 * negotiated minimum, so nothing larger ever arrives. */
#define WLH_WIRE_MAX_FRAME_SIZE 2048u

/* USB transport queueing (all bounded, all static). */
#define WLH_USB_RX_RING_SIZE 4096u
#define WLH_USB_CONTROL_TX_QUEUE_DEPTH 4u
#define WLH_USB_DATA_TX_QUEUE_DEPTH 12u
#define WLH_USB_TX_TIMEOUT_MS 2000u

/* Core sizing. initial_credit is the host->device in-flight window per data
 * channel; 8 x ~1.6 KiB worst-case buffers stay affordable. */
#define WLH_CORE_QUEUE_DEPTH 16u
#define WLH_INITIAL_CREDIT 8u
#define WLH_ETHERNET_TX_DEPTH WLH_USB_DATA_TX_QUEUE_DEPTH
#define WLH_ETHERNET_TX_AGGREGATION_LIMIT 1u
#define WLH_HEARTBEAT_INTERVAL_MS 1000u
#define WLH_STOP_TIMEOUT_MS 3000u

/* Task/RTOS priorities. RT-Thread priorities are inverted (lower value wins)
 * and the OSAL maps osal_prio -> RT_THREAD_PRIORITY_MAX - 1 - osal_prio, so
 * OSAL 21 == RT-Thread 10. */
#define WLH_CORE_TASK_STACK 4096u
#define WLH_CORE_TASK_OSAL_PRIORITY 21u /* RT-Thread 10 */
#define WLH_USB_TX_TASK_PRIORITY 8u
#define WLH_USB_RX_TASK_PRIORITY 9u
#define WLH_LINK_CTRL_TASK_PRIORITY 12u

/* Locally administered MAC. This chip has no factory unique ID; deployments
 * with more than one board on the same LAN must give each its own address. */
#define WLH_ETH_MAC_ADDR \
    { 0x02, 0x57, 0x4c, 0x00, 0x00, 0x01 }

#endif
