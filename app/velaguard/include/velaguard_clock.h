#ifndef VELAGUARD_CLOCK_H
#define VELAGUARD_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct
{
  void *context;
  int (*rtc_read)(void *, struct tm *);
  int (*rtc_write)(void *, const struct tm *);
  int (*system_set)(void *, int64_t);
  int (*system_read)(void *, int64_t *);
} vg_clock_ops_t;

typedef enum
{
  VG_CLOCK_IDLE = 0,
  VG_CLOCK_RTC_READ,
  VG_CLOCK_DECODE,
  VG_CLOCK_SYSTEM_SET,
  VG_CLOCK_RTC_WRITE,
  VG_CLOCK_VERIFY
} vg_clock_stage_t;

typedef struct
{
  vg_clock_ops_t ops;
  int64_t build_epoch;
  int64_t rtc_epoch;
  int raw_year;
  int normalized_year;
  int last_error;
  vg_clock_stage_t stage;
  bool fault;
} vg_clock_t;

/* 返回 0 或负 errno。世纪转换仅针对本板 HAL raw 0..69 ；其他原始年份拒绝。
 * 再用 build-1天..build+366天窗口校验，不信任已知2036默认日期。
 * 任一设置/恢复失败维持 fault；不得仅因系统time看似合理而恢复可信。
 */
int vg_clock_decode_rtc(const struct tm *, int64_t build_epoch, int64_t *);
int vg_clock_start(vg_clock_t *, const vg_clock_ops_t *, int64_t build_epoch);
int vg_clock_set(vg_clock_t *, int64_t epoch);
int vg_clock_read(vg_clock_t *, int64_t *, bool *valid);
int vg_clock_native_ops(vg_clock_ops_t *);

#endif
