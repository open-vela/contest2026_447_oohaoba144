#ifndef VELAGUARD_PLATFORM_H
#define VELAGUARD_PLATFORM_H
#include <stdbool.h>
#include <stdint.h>
int vg_cron_init(void);
/* UTC compatibility entrance for the existing main; never treats epoch as uptime. */
int vg_cron_reconcile(void *,bool,int64_t);
/* Shared worker/main clock health is latched until application restart.
 * Reconcile/init have one main-thread owner; clock reads may be concurrent. */
int vg_cron_monotonic_ms(void *,uint64_t *);
int vg_cron_reconcile_mono(void *,bool,uint64_t);
bool vg_cron_take_event(void);
int vg_install_skill(void);
#endif
