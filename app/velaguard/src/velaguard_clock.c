#include "velaguard_clock.h"
#include <errno.h>
#include <string.h>

static bool leap(int year)
{
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static int month_days(int year, int month)
{
  static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return days[month] + (month == 1 && leap(year));
}
static bool in_window(int64_t epoch, int64_t build)
{
  return build >= 86400 && build <= INT64_MAX - 366LL * 86400 &&
         epoch >= build - 86400 && epoch <= build + 366LL * 86400;
}
int vg_clock_decode_rtc(const struct tm *raw, int64_t build, int64_t *epoch)
{
  int year, y, m;
  int64_t days = 0, value;
  if (!raw || !epoch) return -EINVAL;
  /* 本板 HAL 只返回两位年份；0x80 是旧世纪标志，不得映射到 20xx。 */
  if (raw->tm_year < 0 || raw->tm_year > 69) return -ERANGE;
  year = 2000 + raw->tm_year;
  if (raw->tm_mon < 0 || raw->tm_mon > 11 || raw->tm_mday < 1 ||
      raw->tm_mday > month_days(year, raw->tm_mon) ||
      raw->tm_hour < 0 || raw->tm_hour > 23 || raw->tm_min < 0 ||
      raw->tm_min > 59 || raw->tm_sec < 0 || raw->tm_sec > 59) return -EINVAL;
  for (y = 1970; y < year; y++) days += leap(y) ? 366 : 365;
  for (m = 0; m < raw->tm_mon; m++) days += month_days(year, m);
  days += raw->tm_mday - 1;
  value = days * 86400 + raw->tm_hour * 3600 + raw->tm_min * 60 + raw->tm_sec;
  if (!in_window(value, build)) return -ERANGE;
  *epoch = value;
  return 0;
}
static int encode(int64_t epoch, struct tm *out)
{
  int year = 1970, month = 0;
  int64_t days, remaining;
  if (epoch < 946684800LL || epoch >= 3155760000LL) return -ERANGE;
  memset(out, 0, sizeof(*out));
  days = epoch / 86400;
  remaining = days;
  while (remaining >= (leap(year) ? 366 : 365))
    remaining -= leap(year++) ? 366 : 365;
  out->tm_yday = (int)remaining;
  while (remaining >= month_days(year, month))
    remaining -= month_days(year, month++);
  out->tm_year = year - 1900;
  out->tm_mon = month;
  out->tm_mday = (int)remaining + 1;
  out->tm_wday = (int)((days + 4) % 7);
  out->tm_hour = (int)(epoch % 86400 / 3600);
  out->tm_min = (int)(epoch % 3600 / 60);
  out->tm_sec = (int)(epoch % 60);
  return 0;
}
static int failed(vg_clock_t *clock, int rc)
{
  clock->last_error = rc < 0 ? rc : -EIO;
  clock->fault = true;
  return clock->last_error;
}
static int read_rtc(vg_clock_t *clock, int64_t *epoch)
{
  struct tm raw;
  int rc = clock->ops.rtc_read(clock->ops.context, &raw);
  if (rc) return rc;
  clock->raw_year = raw.tm_year;
  clock->normalized_year = raw.tm_year >= 0 && raw.tm_year <= 69 ?
                           raw.tm_year + 2000 : -1;
  rc = vg_clock_decode_rtc(&raw, clock->build_epoch, epoch);
  if (!rc) clock->rtc_epoch = *epoch;
  return rc;
}
int vg_clock_set(vg_clock_t *clock, int64_t epoch)
{
  struct tm rtc;
  int64_t rtc_epoch, system_epoch;
  int rc;
  if (!clock) return -EINVAL;
  clock->fault = true;
  clock->stage = VG_CLOCK_DECODE;
  if (!clock->ops.rtc_write || !clock->ops.rtc_read ||
      !clock->ops.system_set || !clock->ops.system_read)
    return failed(clock, -EINVAL);
  if (!in_window(epoch, clock->build_epoch)) return failed(clock, -ERANGE);
  rc = encode(epoch, &rtc);
  if (rc) return failed(clock, rc);
  /* RTC_SET_TIME 会用未修正年份同步 OS，故必须先写 RTC 再恢复 OS。 */
  clock->stage = VG_CLOCK_RTC_WRITE;
  rc = clock->ops.rtc_write(clock->ops.context, &rtc);
  if (rc) return failed(clock, rc);
  clock->stage = VG_CLOCK_SYSTEM_SET;
  rc = clock->ops.system_set(clock->ops.context, epoch);
  if (rc) return failed(clock, rc);
  clock->stage = VG_CLOCK_VERIFY;
  rc = read_rtc(clock, &rtc_epoch);
  if (rc) return failed(clock, rc);
  rc = clock->ops.system_read(clock->ops.context, &system_epoch);
  if (rc) return failed(clock, rc);
  if (rtc_epoch < epoch - 1 || rtc_epoch > epoch + 2 ||
      system_epoch < epoch - 1 || system_epoch > epoch + 2)
    return failed(clock, -EIO);
  clock->fault = false;
  clock->last_error = 0;
  clock->stage = VG_CLOCK_IDLE;
  return 0;
}
int vg_clock_start(vg_clock_t *clock, const vg_clock_ops_t *ops, int64_t build)
{
  int64_t epoch;
  int rc;
  if (!clock) return -EINVAL;
  memset(clock, 0, sizeof(*clock));
  clock->fault = true;
  clock->raw_year = -1;
  clock->normalized_year = -1;
  if (!ops || !ops->rtc_read || !ops->rtc_write || !ops->system_set ||
      !ops->system_read) return failed(clock, -EINVAL);
  clock->ops = *ops;
  clock->build_epoch = build;
  clock->stage = VG_CLOCK_RTC_READ;
  rc = read_rtc(clock, &epoch);
  if (rc) return failed(clock, rc);
  return vg_clock_set(clock, epoch);
}
int vg_clock_read(vg_clock_t *clock, int64_t *epoch, bool *valid)
{
  int rc;
  if (!clock || !epoch || !valid || !clock->ops.system_read) return -EINVAL;
  *valid = false;
  *epoch = 0;
  rc = clock->ops.system_read(clock->ops.context, epoch);
  if (rc) return failed(clock, rc);
  *valid = !clock->fault && in_window(*epoch, clock->build_epoch);
  return 0;
}

#ifdef __NuttX__
#include <nuttx/config.h>
#include <nuttx/timers/rtc.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int native_rtc(void *context, struct tm *tm, bool write)
{
  struct rtc_time rtc = {0};
  int fd, rc = 0;
  (void)context;
  fd = open("/dev/rtc0", write ? O_RDWR : O_RDONLY);
  if (fd < 0) return -errno;
  if (write)
    {
      rtc.tm_year = tm->tm_year; rtc.tm_mon = tm->tm_mon;
      rtc.tm_mday = tm->tm_mday; rtc.tm_hour = tm->tm_hour;
      rtc.tm_min = tm->tm_min; rtc.tm_sec = tm->tm_sec;
      rtc.tm_wday = tm->tm_wday; rtc.tm_yday = tm->tm_yday;
    }
  if (ioctl(fd, write ? RTC_SET_TIME : RTC_RD_TIME, (unsigned long)&rtc) < 0)
    rc = -errno;
  if (close(fd) < 0 && !rc) rc = -errno;
  if (!rc && !write)
    {
      memset(tm, 0, sizeof(*tm));
      tm->tm_year = rtc.tm_year; tm->tm_mon = rtc.tm_mon;
      tm->tm_mday = rtc.tm_mday; tm->tm_hour = rtc.tm_hour;
      tm->tm_min = rtc.tm_min; tm->tm_sec = rtc.tm_sec;
    }
  return rc;
}
static int native_read(void *context, struct tm *tm)
{
  return native_rtc(context, tm, false);
}
static int native_write(void *context, const struct tm *tm)
{
  struct tm copy = *tm;
  return native_rtc(context, &copy, true);
}
static int native_set(void *context, int64_t epoch)
{
  struct timespec ts;
  (void)context;
  if ((int64_t)(time_t)epoch != epoch) return -ERANGE;
  ts.tv_sec = (time_t)epoch;
  ts.tv_nsec = 0;
  return clock_settime(CLOCK_REALTIME, &ts) < 0 ? -errno : 0;
}
static int native_now(void *context, int64_t *epoch)
{
  time_t now;
  (void)context;
  now = time(NULL);
  if (now == (time_t)-1) return -EIO;
  *epoch = (int64_t)now;
  return 0;
}
#endif
int vg_clock_native_ops(vg_clock_ops_t *ops)
{
  if (!ops) return -EINVAL;
  memset(ops, 0, sizeof(*ops));
#ifdef __NuttX__
  ops->rtc_read = native_read;
  ops->rtc_write = native_write;
  ops->system_set = native_set;
  ops->system_read = native_now;
  return 0;
#else
  return -ENOSYS;
#endif
}
