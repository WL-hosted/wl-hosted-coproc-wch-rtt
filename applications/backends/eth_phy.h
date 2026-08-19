#ifndef WLH_ETH_PHY_H
#define WLH_ETH_PHY_H

#include <stdint.h>

typedef struct wlh_eth_phy_status {
    uint8_t link_up;
    uint8_t full_duplex;
    uint16_t speed_mbps;
} wlh_eth_phy_status_t;

/* Configure the target-specific ETH clock/interface, reset the EMAC and PHY,
 * and apply the PHY vendor register setup. All hardware waits are bounded. */
int wlh_eth_phy_init(void);

/* Apply PHY-specific MAC bits which are not part of the common EMAC setup. */
void wlh_eth_phy_configure_mac(void);

/* Read the current negotiated link state and update MAC speed/duplex when up. */
void wlh_eth_phy_read_status(wlh_eth_phy_status_t *status);

const char *wlh_eth_phy_name(void);

#endif
