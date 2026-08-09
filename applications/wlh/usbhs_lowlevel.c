/* CH32V307/CH32V317 USBHS low-level glue for the CherryUSB ch32 port.
 * Clock recipe follows the WCH EVT USBHS device examples and the
 * cherryusb_wch demo (HSE = 8 MHz on this board). */
#include "usbhs_lowlevel.h"

#include <board.h>
#include <ch32v30x.h>
#include <ch32v30x_usb.h>
#include <rtthread.h>

void usb_dc_low_level_init(void) {
    /* USBHS PLL: HSE -> 4 MHz reference -> PLL -> Div2; PHY PLL kept alive. */
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);
    rt_hw_us_delay(100u);
}

void wlh_usbhs_ep_in_halt(uint8_t ep_idx) {
    /* UEPn_TX_CTRL sits at 0xD8 + 4*n + 2 for n >= 1 (EP0 has its own
     * registers and is never halted here). */
    if (ep_idx == 0u || ep_idx > 15u)
        return;
    *(volatile uint8_t *)((volatile uint8_t *)USBHSD + 0xD8u + 4u * ep_idx + 2u) =
        USBHS_UEP_T_RES_NAK;
}
