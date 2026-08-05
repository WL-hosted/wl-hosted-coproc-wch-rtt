/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-23     liYony       first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "ch32v30x.h"
#include "wlh/wlh_app.h"

/* defined the LED0 pin: PB5 */
#define LED0_PIN              rt_pin_get("PB.5")

int main(void)
{
    /* set LED0 pin mode to output */
    rt_pin_mode(LED0_PIN, PIN_MODE_OUTPUT);

    /* Start the WL-hosted coprocessor: coproc-core + USB bulk transport.
     * Runs its own threads; the main thread stays on the LED heartbeat. */
    if (wlh_app_init() != 0)
    {
        rt_kprintf("wlh_app_init failed\n");
    }

    while (1)
    {
        rt_pin_write(LED0_PIN, PIN_HIGH);
        rt_thread_mdelay(500);
        rt_pin_write(LED0_PIN, PIN_LOW);
        rt_thread_mdelay(500);
    }
}
