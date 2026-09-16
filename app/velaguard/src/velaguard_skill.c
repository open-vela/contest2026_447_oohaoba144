#include "velaguard_platform.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char skill[]=
"# VelaGuard\n"
"Offline reminders use one TaskStore and official cron wake; network, MiMo and BLE are not prerequisites.\n"
"\n"
"## When to use\n"
"Create, list, confirm, snooze or explicitly restart a reminder.\n"
"\n"
"## How to use\n"
"Supply complete JSON envelopes: version=1, unique request_id, type, payload. Never edit Store files.\n"
"Tools: velaguard_create -> task.create; velaguard_list -> task.list; velaguard_snooze -> task.snooze; velaguard_ack -> task.ack; velaguard_rearm -> task.rearm.\n"
"task.create: title UTF-8 <=192 bytes, priority 0..2, exactly one due_epoch or delay_seconds. Use delay_seconds integer 1..86400 for relative reminders even without RTC; absolute due_epoch requires valid UTC. Never put uptime into due_epoch.\n"
"task.list: offset and optional history. event.sync: offset, history defaults true. Read timer_domain ABS/REL, original delay_seconds and string timer_revision/mono_deadline_ms/timer_boot_id.\n"
"task.ack: task_id. task.snooze: task_id, seconds 1..86400, exactly one guard: ABS expected_due_epoch, or REL expected_revision from the task. Revision is a nonzero decimal string up to 18446744073709551615, not a JSON number.\n"
"NEEDS_RESET means a pending relative timer crossed a restart. Do not infer time while powered off or restart it automatically. Only explicit task.rearm with task_id and expected_revision restarts its original delay. Retained ALERTING tasks can be confirmed or snoozed explicitly.\n"
"Replay the same request_id and envelope. Read response.error and uncertain even when a tool transport succeeds. After uncertainty, reload/query; never change the revision and blindly repeat an old action.\n"
"device.status: {}. device.reload: {}. device.time: epoch only for explicit time setting. A blocked timer requires recovery; never bypass its guard or storage error.\n";
int vg_install_skill(void)
{
  int fd,rc=0;size_t off=0;ssize_t n;
  if(mkdir("/data/agent",0755)<0&&errno!=EEXIST)return -1;
  if(mkdir("/data/agent/skills",0755)<0&&errno!=EEXIST)return -1;
  fd=open("/data/agent/skills/velaguard.md",O_WRONLY|O_CREAT|O_EXCL,0644);
  if(fd<0)return errno==EEXIST?0:-1;
  while(off<sizeof(skill)-1)
    {
      n=write(fd,skill+off,sizeof(skill)-1-off);
      if(n<0&&errno==EINTR)continue;
      if(n<=0){rc=-1;break;}off+=(size_t)n;
    }
  if(close(fd)<0)rc=-1;
  return rc;
}
