#include "eth_phy.h"

#include <board.h>
#include <ch32v30x.h>
/* TODO(wch-sdk): SDK v2.7 ch32v30x_eth.h has nested comment openers around
 * its register sections. Remove this local suppression when the vendored SDK
 * fixes those comments; application warnings remain covered by -Werror. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include <ch32v30x_eth.h>
#pragma GCC diagnostic pop
#include <rtthread.h>

#include "firmware_config.h"
#include "wlh/log.h"

#define TAG "wlh-phy"
#define PHY_BMCR_FULL_DUPLEX (1u << 8)
#define PHY_BMCR_SPEED_100M (1u << 13)

static int wait_for_pll3(void) {
    rt_tick_t start = rt_tick_get_millisecond();

    while (RCC_GetFlagStatus(RCC_FLAG_PLL3RDY) == RESET) {
        if ((rt_tick_get_millisecond() - start) > 500u) {
            WLH_LOGE(TAG, "pll3 lock timeout");
            return -1;
        }
    }
    return 0;
}

static int reset_emac_and_phy(void) {
    rt_tick_t start;

    RCC_AHBPeriphClockCmd(
        RCC_AHBPeriph_ETH_MAC | RCC_AHBPeriph_ETH_MAC_Tx |
            RCC_AHBPeriph_ETH_MAC_Rx,
        ENABLE
    );
    ETH_DeInit();
    ETH_SoftwareReset();
    start = rt_tick_get_millisecond();
    while (ETH->DMABMR & ETH_DMABMR_SR) {
        if ((rt_tick_get_millisecond() - start) > 500u) {
            WLH_LOGE(TAG, "emac software reset timeout");
            return -1;
        }
    }

    ETH->MACMIIAR =
        (ETH->MACMIIAR & MACMIIAR_CR_MASK) | (uint32_t)ETH_MACMIIAR_CR_Div42;
    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, PHY_BCR, PHY_Reset);
#if defined(WLH_TARGET_CH32V317)
    rt_thread_mdelay(1u);
#else
    /* Preserve the proven CH32V307 internal-PHY reset settling interval. */
    rt_thread_mdelay(50u);
#endif
    start = rt_tick_get_millisecond();
    while (ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, PHY_BCR) & PHY_Reset) {
        if ((rt_tick_get_millisecond() - start) > 500u) {
            WLH_LOGE(TAG, "phy reset timeout");
            return -1;
        }
        rt_thread_mdelay(10u);
    }
    return 0;
}

#if defined(WLH_TARGET_CH32V317)

static void gpio_output(GPIO_TypeDef *port, uint16_t pin) {
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(port, &gpio);
}

static void gpio_input(GPIO_TypeDef *port, uint16_t pin) {
    GPIO_InitTypeDef gpio = {0};

    gpio.GPIO_Pin = pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(port, &gpio);
}

static void configure_rmii_pins(void) {
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO,
        ENABLE
    );
    GPIO_ETH_MediaInterfaceConfig(GPIO_ETH_MediaInterface_RMII);
    gpio_output(GPIOD, GPIO_Pin_8);  /* MDIO */
    gpio_output(GPIOE, GPIO_Pin_15); /* MDC */
    gpio_output(GPIOE, GPIO_Pin_8);  /* TXEN */
    gpio_output(GPIOE, GPIO_Pin_11); /* TXD0 */
    gpio_output(GPIOE, GPIO_Pin_10); /* TXD1 */
    gpio_output(GPIOE, GPIO_Pin_12); /* REFCLK */
    gpio_input(GPIOE, GPIO_Pin_9);   /* CRSDV */
    gpio_input(GPIOE, GPIO_Pin_14);  /* RXD0 */
    gpio_input(GPIOE, GPIO_Pin_13);  /* RXD1 */
}

static void configure_v317_phy(void) {
    uint16_t value;

    /* WCH CH32V317 integrated 100M PHY setup from the official MAC_RAW
     * example: repeater mode plus the required RMII RX/TX clock phase. */
    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, 0x1fu, 0x00u);
    value = ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, 28u);
    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, 28u, value | (1u << 13));

    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, 0x1fu, 0x07u);
    value = ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, 16u);
    value &= (uint16_t)~(0xffu << 4);
    value |= (uint16_t)(0x23u << 4);
    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, 16u, value);
    ETH_WritePHYRegister(WLH_ETH_PHY_ADDR, 0x1fu, 0x00u);
}

int wlh_eth_phy_init(void) {
    configure_rmii_pins();

    /* HSE 8 MHz -> PREDIV2 /1 -> PLL3 x12.5 = 100 MHz, as required by the
     * CH32V317 integrated 100M PHY. */
    RCC_PLL3Cmd(DISABLE);
    RCC_PREDIV2Config(RCC_PREDIV2_Div1);
    RCC_PLL3Config(RCC_PLL3Mul_12_5);
    RCC_PLL3Cmd(ENABLE);
    if (wait_for_pll3() != 0)
        return -1;
    rt_hw_us_delay(300u);
    if (reset_emac_and_phy() != 0)
        return -1;
    configure_v317_phy();
    return 0;
}

void wlh_eth_phy_configure_mac(void) {
    /* The V317 uses its integrated 10/100 PHY through RMII; the V307-only
     * internal 10BASE-T pull-up must stay disabled. */
    ETH->MACCR &= ~(uint32_t)ETH_Internal_Pull_Up_Res_Enable;
}

const char *wlh_eth_phy_name(void) {
    return "CH32V317 integrated 10/100M PHY";
}

#else

int wlh_eth_phy_init(void) {
    /* HSE 8 MHz -> PREDIV2 /2 -> PLL3 x15 = 60 MHz. */
    RCC_PLL3Cmd(DISABLE);
    RCC_PREDIV2Config(RCC_PREDIV2_Div2);
    RCC_PLL3Config(RCC_PLL3Mul_15);
    RCC_PLL3Cmd(ENABLE);
    if (wait_for_pll3() != 0)
        return -1;

    EXTEN->EXTEN_CTR |= EXTEN_ETH_10M_EN;
    return reset_emac_and_phy();
}

void wlh_eth_phy_configure_mac(void) {
    ETH->MACCR |= ETH_Internal_Pull_Up_Res_Enable;
}

const char *wlh_eth_phy_name(void) {
    return "CH32V307 internal 10M PHY";
}

#endif

void wlh_eth_phy_read_status(wlh_eth_phy_status_t *status) {
    uint16_t bsr;
    uint16_t bmcr;

    /* BSR link is latched low, so read twice to obtain the current value. */
    (void)ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, PHY_BSR);
    bsr = ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, PHY_BSR);
    status->link_up = (bsr & PHY_Linked_Status) != 0u ? 1u : 0u;
    if (!status->link_up) {
        status->speed_mbps = 0u;
        status->full_duplex = 0u;
        return;
    }

    bmcr = ETH_ReadPHYRegister(WLH_ETH_PHY_ADDR, PHY_BMCR);
    status->full_duplex = (bmcr & PHY_BMCR_FULL_DUPLEX) != 0u ? 1u : 0u;
#if defined(WLH_TARGET_CH32V317)
    status->speed_mbps = (bmcr & PHY_BMCR_SPEED_100M) != 0u ? 100u : 10u;
#else
    status->speed_mbps = 10u;
#endif

    ETH->MACCR &= ~((uint32_t)ETH_Speed_100M | (uint32_t)ETH_Speed_1000M |
                    (uint32_t)ETH_Mode_FullDuplex);
    if (status->speed_mbps == 100u)
        ETH->MACCR |= ETH_Speed_100M;
    if (status->full_duplex)
        ETH->MACCR |= ETH_Mode_FullDuplex;
}
