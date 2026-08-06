/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2021/06/06
* Description        : Main Interrupt Service Routines.
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* SPDX-License-Identifier: Apache-2.0
*******************************************************************************/
#include "ch32v30x_it.h"
#include "board.h"
#include <rtthread.h>

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));


/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
	GET_INT_SP();
    rt_interrupt_enter();
    rt_kprintf(" NMI Handler\r\n");
    rt_interrupt_leave();
    FREE_INT_SP();
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
    rt_thread_t tid;
    GET_INT_SP();
    rt_interrupt_enter();
    rt_kprintf(" hardfult\r\n");
    rt_kprintf("mepc:%08x\r\n",__get_MEPC());
    rt_kprintf("mcause:%08x\r\n",__get_MCAUSE());
    rt_kprintf("mtval:%08x\r\n",__get_MTVAL());
    tid = rt_thread_self();
    if (tid != RT_NULL)
        rt_kprintf("thread:%s\r\n", tid->parent.name);
    rt_kprintf("nest:%lu\r\n", (unsigned long)rt_interrupt_get_nest());
    while(1);
    rt_interrupt_leave();
    FREE_INT_SP();
}



