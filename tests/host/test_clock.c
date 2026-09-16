#include "velaguard_clock.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BUILD 1789344000LL
static vg_clock_t clock_state;
static struct tm raw={0};
static int fail_read,fail_write,fail_system,reads,writes,system_sets;
static int64_t system_epoch=2090000000LL;
static bool mismatch;
static int rtc_skew, system_skew, fail_system_read;
static int rtc_read(void *ctx,struct tm *out)
{
  (void)ctx;reads++;
  if(fail_read)return -EIO;
  *out=raw;if(mismatch)out->tm_min++;out->tm_sec+=rtc_skew;return 0;
}
static int rtc_write(void *ctx,const struct tm *in)
{
  (void)ctx;writes++;
  if(fail_write)return -EIO;
  assert(in->tm_year==126 || in->tm_year==127);
  raw=*in;raw.tm_year-=100;system_epoch=-1400000000LL;return 0;
}
static int system_set(void *ctx,int64_t epoch)
{
  (void)ctx;system_sets++;
  if(fail_system)return -EPERM;
  system_epoch=epoch;return 0;
}
static int system_read(void *ctx,int64_t *epoch)
{(void)ctx;if(fail_system_read)return -EIO;*epoch=system_epoch+system_skew;return 0;}
static vg_clock_ops_t ops={NULL,rtc_read,rtc_write,system_set,system_read};
static void reset_raw(void)
{
  memset(&raw,0,sizeof(raw));raw.tm_year=26;raw.tm_mon=8;raw.tm_mday=14;
}
int main(void)
{
  int64_t epoch;bool valid;int count;
  reset_raw();assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)==0 && epoch==BUILD);
  raw.tm_year=126;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  raw.tm_year=27;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)==0 && epoch==BUILD+365LL*86400);
  raw.tm_year=127;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  raw.tm_year=36;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  raw.tm_year=136;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  raw.tm_year=0x80|96;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  reset_raw();raw.tm_mon=1;raw.tm_mday=30;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  reset_raw();raw.tm_sec=60;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  reset_raw();assert(vg_clock_start(&clock_state,&ops,BUILD)==0);
  assert(system_epoch==BUILD && !clock_state.fault && clock_state.raw_year==26 && clock_state.normalized_year==2026);
  assert(vg_clock_read(&clock_state,&epoch,&valid)==0 && valid && epoch==BUILD);
  assert(vg_clock_set(&clock_state,1789375984LL)==0 && !clock_state.fault);
  assert(clock_state.rtc_epoch==1789375984LL && system_epoch==1789375984LL);
  fail_write=1;
  assert(vg_clock_set(&clock_state,BUILD+100)<0 && clock_state.fault && clock_state.stage==VG_CLOCK_RTC_WRITE);
  assert(vg_clock_read(&clock_state,&epoch,&valid)==0 && !valid);fail_write=0;
  fail_system=1;count=writes;
  assert(vg_clock_set(&clock_state,BUILD+100)==-EPERM && clock_state.fault && writes==count+1);fail_system=0;
  fail_read=1;assert(vg_clock_set(&clock_state,BUILD+100)<0 && clock_state.fault);fail_read=0;
  mismatch=true;assert(vg_clock_set(&clock_state,BUILD+100)<0 && clock_state.fault);mismatch=false;
  assert(vg_clock_set(&clock_state,BUILD+100)==0 && !clock_state.fault);
  count=system_sets;assert(vg_clock_set(&clock_state,BUILD+400LL*86400)<0 && clock_state.fault && system_sets==count);
  reset_raw();raw.tm_year=36;count=system_sets;
  assert(vg_clock_start(&clock_state,&ops,BUILD)<0 && clock_state.fault && system_sets==count);
  reset_raw();fail_read=1;assert(vg_clock_start(&clock_state,&ops,BUILD)<0 && clock_state.fault);fail_read=0;
  reset_raw();assert(vg_clock_start(&clock_state,&ops,BUILD)==0 && !clock_state.fault);
  rtc_skew=-1;assert(vg_clock_set(&clock_state,BUILD+100)==0);
  rtc_skew=2;assert(vg_clock_set(&clock_state,BUILD+100)==0);
  rtc_skew=3;assert(vg_clock_set(&clock_state,BUILD+100)<0 && clock_state.fault);rtc_skew=0;
  system_skew=3;assert(vg_clock_set(&clock_state,BUILD+100)<0 && clock_state.fault);system_skew=0;
  fail_system_read=1;assert(vg_clock_set(&clock_state,BUILD+100)==-EIO && clock_state.fault);fail_system_read=0;
  assert(vg_clock_set(&clock_state,BUILD+100)==0);
  system_epoch=BUILD+400LL*86400;
  assert(vg_clock_read(&clock_state,&epoch,&valid)==0 && !valid);
  reset_raw();raw.tm_year=69;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  reset_raw();raw.tm_mon=-1;assert(vg_clock_decode_rtc(&raw,BUILD,&epoch)<0);
  reset_raw();raw.tm_year=24;raw.tm_mon=1;raw.tm_mday=29;
  assert(vg_clock_decode_rtc(&raw,1709164800LL,&epoch)==0 && epoch==1709164800LL);
  raw.tm_year=25;assert(vg_clock_decode_rtc(&raw,1740787200LL,&epoch)<0);
  assert(vg_clock_start(&clock_state,NULL,BUILD)==-EINVAL && clock_state.fault);
  assert(vg_clock_set(&clock_state,BUILD)==-EINVAL);
  puts("PASS RTC clock: HAL century/ambiguous year rejection, default rejection, calendar validation, boot recovery, explicit write errors/readback mismatch/fault recovery");
  return 0;
}
