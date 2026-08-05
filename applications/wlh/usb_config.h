/*
 * CherryUSB stack configuration for the CH32V307 USBHS device build.
 * Referenced by rt-thread/components/drivers/usb/cherryusb (usb_config.h is
 * resolved through the applications/wlh include path).
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include <rtthread.h>

#define CONFIG_USB_PRINTF(...) rt_kprintf(__VA_ARGS__)

#ifndef CONFIG_USB_DBG_LEVEL
#define CONFIG_USB_DBG_LEVEL USB_DBG_INFO
#endif

/* No D-Cache on the CH32V307; DMA buffers only need 4-byte alignment. */
#define CONFIG_USB_ALIGN_SIZE 4

/* No non-cacheable RAM region on this part; the attribute expands to
 * nothing so the core's global state lands in normal SRAM. */
#define USB_NOCACHE_RAM_SECTION

/* CONFIG_USB_HS is supplied by the build (-DCONFIG_USB_HS from
 * RT_CHERRYUSB_DEVICE_SPEED_HS). Do not redefine it here. */

#ifndef CONFIG_USBDEV_REQUEST_BUFFER_LEN
#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512
#endif

#ifndef CONFIG_USBDEV_MSC_MAX_BUFSIZE
#define CONFIG_USBDEV_MSC_MAX_BUFSIZE 512
#endif

#ifndef CONFIG_USBDEV_MAX_BUS
#define CONFIG_USBDEV_MAX_BUS 1
#endif

#ifndef CONFIG_USBDEV_EP_NUM
#define CONFIG_USBDEV_EP_NUM 8
#endif

#ifndef usb_phyaddr2ramaddr
#define usb_phyaddr2ramaddr(addr) (addr)
#endif

#ifndef usb_ramaddr2phyaddr
#define usb_ramaddr2phyaddr(addr) (addr)
#endif

#endif
