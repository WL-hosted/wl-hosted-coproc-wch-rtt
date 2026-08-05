#ifndef WLH_USBHS_LOWLEVEL_H
#define WLH_USBHS_LOWLEVEL_H

#include <stdint.h>

/* Strong override of the CherryUSB ch32 port's weak hook, plus the
 * detach/attach helper the transport needs for stack restarts.
 * SDK headers only in this unit: the CherryUSB reg header
 * (usb_ch32_usbhs_reg.h) redefines the same USBHS register types, so the two
 * worlds must not be included into one translation unit. */

/* Called by the CherryUSB port (usb_dc_init). Configures the USBHS PLL/PHY
 * clocks (HSE 8 MHz) and enables the USBHS interrupt. */
void usb_dc_low_level_init(void);

/* Force an IN endpoint back to NAK without a pending transfer. Used after a
 * bulk-IN timeout: the host went away mid-transfer, and the armed DMA buffer
 * must never complete late (its completion was already reported). */
void wlh_usbhs_ep_in_halt(uint8_t ep_idx);

#endif
