#ifndef WLH_APP_H
#define WLH_APP_H

#include "wlh/coproc.h"

/* Initialize the WL-hosted coprocessor application: OSAL, log, coproc-core,
 * USB transport, and the link-reset thread. Call from the RT-Thread main
 * thread after system startup. Returns 0 on success. */
int wlh_app_init(void);

/* The live coproc instance, for the msh diagnostics commands. */
const wlh_coproc_t *wlh_app_coproc(void);

#endif
