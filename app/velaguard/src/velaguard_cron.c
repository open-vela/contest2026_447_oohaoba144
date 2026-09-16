/* Include the actual official cron implementation without upstream changes.
 * TaskStore owns business durability; this single job is a rebuildable wake.
 */
#include "velaguard_platform.h"
#include "agent_config.h"
#include "agent_compat.h"
#include <stdatomic.h>
#ifndef VG_CRON_FILE
#define VG_CRON_FILE "/data/velaguard-cron.json"
#endif
#ifndef VG_CRON_RAW_GETTIME
#define VG_CRON_RAW_GETTIME clock_gettime
#endif
#ifndef VG_CRON_RAW_TIME
#define VG_CRON_RAW_TIME time
#endif
#ifndef VG_CRON_BACKOFF
#define VG_CRON_BACKOFF nanosleep
#endif
#undef AGENT_CRON_FILE
#define AGENT_CRON_FILE VG_CRON_FILE
#undef AGENT_CRON_CHECK_INTERVAL_MS
#define AGENT_CRON_CHECK_INTERVAL_MS 1000
#undef AGENT_CRON_MAX_JOBS
#define AGENT_CRON_MAX_JOBS 1
#define cron_service_init vg_agent_cron_service_init
#define cron_service_start vg_agent_cron_service_start
#define cron_service_stop vg_agent_cron_service_stop
#define cron_add_job vg_agent_cron_add_job
#define cron_remove_job vg_agent_cron_remove_job
#define cron_list_jobs vg_agent_cron_list_jobs
#define message_bus_push_outbound vg_agent_message_bus_push_outbound
#define tool_registry_execute vg_agent_tool_registry_execute
/* Headers precede time macros so libc declarations remain untouched. */
#include "infra/cron_service.h"
#include "core/message_bus.h"
#include "tools/tool_registry.h"
#include "cJSON.h"
static atomic_bool vg_pending,vg_clock_fault;
static pthread_mutex_t vg_clock_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t vg_last_mono;
static bool vg_clock_seen,vg_initialized,vg_started;
static time_t vg_cron_clock_time(time_t *);
static int vg_cron_clock_gettime(clockid_t,struct timespec *);
static int vg_cron_cond_wait(pthread_cond_t *,pthread_mutex_t *,const struct timespec *);
#define time vg_cron_clock_time
#define clock_gettime vg_cron_clock_gettime
#define pthread_cond_timedwait vg_cron_cond_wait
#ifdef VG_CRON_OPEN
#define fopen VG_CRON_OPEN
#endif
#ifdef VG_CRON_TASK_CREATE
#define agent_task_create VG_CRON_TASK_CREATE
#endif
#include "infra/cron_service.c"
#undef time
#undef clock_gettime
#undef pthread_cond_timedwait
#ifdef VG_CRON_OPEN
#undef fopen
#endif
#ifdef VG_CRON_TASK_CREATE
#undef agent_task_create
#endif

static void vg_latch_fault(void)
{
  if(!atomic_exchange(&vg_clock_fault,true))atomic_store(&vg_pending,true);
}
/* Lock before reading the OS clock as well as comparing/updating high-water.
 * SF32 is 32-bit; the uint64 state must never be read without this lock.
 * This helper never acquires the official cron mutex. */
static int vg_read_mono(struct timespec *raw,uint64_t *out)
{
  struct timespec ts={0,0};
  int rc=ERROR;
  if(pthread_mutex_lock(&vg_clock_lock)!=0){vg_latch_fault();return ERROR;}
  if(atomic_load(&vg_clock_fault))goto done;
  if(VG_CRON_RAW_GETTIME(CLOCK_MONOTONIC,&ts)!=0 || ts.tv_sec<0 ||
     (uint64_t)ts.tv_sec>(uint64_t)INT32_MAX-1 || ts.tv_nsec<0 ||
     ts.tv_nsec>=1000000000L){vg_latch_fault();goto done;}
  uint64_t now=(uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
  if(vg_clock_seen && now<vg_last_mono){vg_latch_fault();goto done;}
  vg_last_mono=now;vg_clock_seen=true;
  if(raw)*raw=ts;
  if(out)*out=now;
  rc=OK;
 done:
  pthread_mutex_unlock(&vg_clock_lock);
  return rc;
}
int vg_cron_monotonic_ms(void *context,uint64_t *out)
{
  (void)context;
  return out?vg_read_mono(NULL,out):ERROR;
}
static time_t vg_cron_clock_time(time_t *out)
{
  uint64_t ms;
  /* All derived next_run values are positive; -1 cannot fire a due job. */
  time_t value=vg_read_mono(NULL,&ms)==OK?(time_t)(ms/1000u+1u):(time_t)-1;
  if(out)*out=value;
  return value;
}
static int vg_cron_clock_gettime(clockid_t ignored,struct timespec *ts)
{
  (void)ignored;
  if(!ts)return ERROR;
  /* Official loop ignores rc and adds one second: keep failure initialized.
   * vg_read_mono reserves that INT32 second and does not add the job offset. */
  ts->tv_sec=0;ts->tv_nsec=0;
  return vg_read_mono(ts,NULL);
}
static int vg_fault_wait(pthread_cond_t *condition,pthread_mutex_t *mutex)
{
  struct timespec remaining={1,0};
  pthread_mutex_unlock(mutex);
  int rc;
  do{struct timespec next={0,0};rc=VG_CRON_BACKOFF(&remaining,&next);remaining=next;}while(rc!=0 && errno==EINTR);
  pthread_mutex_lock(mutex);
  /* A failing sleep must not turn the official worker into a busy loop. */
  if(rc!=0)return pthread_cond_wait(condition,mutex);
  return ETIMEDOUT;
}
static int vg_cron_cond_wait(pthread_cond_t *condition,pthread_mutex_t *mutex,
                              const struct timespec *deadline)
{
  if(atomic_load(&vg_clock_fault))return vg_fault_wait(condition,mutex);
  int rc;
#ifdef _WIN32
  /* Host-only relative wait. The NuttX target uses its actual clockwait API. */
  struct timespec now;
  if(vg_read_mono(&now,NULL)!=OK)return vg_fault_wait(condition,mutex);
  struct timespec delay={deadline->tv_sec-now.tv_sec,deadline->tv_nsec-now.tv_nsec};
  if(delay.tv_nsec<0){delay.tv_sec--;delay.tv_nsec+=1000000000L;}
  if(delay.tv_sec<0){delay.tv_sec=0;delay.tv_nsec=0;}
  rc=pthread_cond_timedwait_relative_np(condition,mutex,&delay);
#else
  rc=pthread_cond_clockwait(condition,mutex,CLOCK_MONOTONIC,deadline);
#endif
  if(rc!=0 && rc!=ETIMEDOUT && rc!=EINTR){vg_latch_fault();return vg_fault_wait(condition,mutex);}
  return rc;
}
/* Called under official cron mutex: only queue a notification, never reenter
 * runtime/cron. A fault between the time sample and this callback also blocks. */
int vg_agent_tool_registry_execute(const char *name,const char *input,char *output,size_t size)
{
  (void)input;
  if(atomic_load(&vg_clock_fault)||strcmp(name,"velaguard.wake"))return ERROR;
  atomic_store(&vg_pending,true);
  if(size)snprintf(output,size,"wake queued");
  return OK;
}
int vg_agent_message_bus_push_outbound(const agent_msg_t *msg)
{
  free(msg->content);free(msg->image_b64);return OK;
}
static int vg_clear_job(void)
{
  cron_job_t old;
  if(vg_agent_cron_list_jobs(&old,1)>0)(void)vg_agent_cron_remove_job(old.id);
  /* Missing after a concurrent fire is harmless. Nonempty is never ignored. */
  return vg_agent_cron_list_jobs(&old,1)==0?OK:ERROR;
}
int vg_cron_init(void)
{
  uint64_t now;
  if(atomic_load(&vg_clock_fault))return ERROR;
  if(vg_started)return OK;
  if(vg_read_mono(NULL,&now)!=OK)return ERROR;
  if(!vg_initialized)
    {
      if(vg_agent_cron_service_init()!=OK){vg_latch_fault();return ERROR;}
      vg_initialized=true;
    }
  if(vg_clear_job()!=OK){vg_latch_fault();return ERROR;}
  atomic_store(&vg_pending,false);
  if(vg_agent_cron_service_start()!=OK){vg_latch_fault();return ERROR;}
  vg_started=true;
  /* Never stop/start a worker: official stop neither signals nor joins it. */
  return atomic_load(&vg_clock_fault)?ERROR:OK;
}
bool vg_cron_take_event(void){return atomic_exchange(&vg_pending,false);}
int vg_cron_reconcile_mono(void *context,bool has_next,uint64_t deadline_ms)
{
  (void)context;
  if(!vg_started)return has_next?ERROR:OK;
  if(!has_next)
    {
      int rc=vg_clear_job();
      /* A queued wake is harmless: runtime rechecks its Store. Never clear
       * here, because a concurrent clock fault must still reach the owner. */
      if(rc!=OK)vg_latch_fault();
      return rc;
    }
  uint64_t now;
  if(vg_read_mono(NULL,&now)!=OK){(void)vg_clear_job();return ERROR;}
  /* Explicit INT32 bound even on MinGW time_t64. No ms+999 overflow. */
  uint64_t target=deadline_ms/1000u+(deadline_ms%1000u!=0)+1u;
  if(target>(uint64_t)INT32_MAX){(void)vg_clear_job();return ERROR;}
  uint64_t current=now/1000u+1u;
  if(deadline_ms<=now)
    {
      atomic_store(&vg_pending,true);
      if(current>=(uint64_t)INT32_MAX){(void)vg_clear_job();return ERROR;}
      target=current+1u;
    }
  cron_job_t old,job={0};
  if(vg_agent_cron_list_jobs(&old,1)>0 && old.enabled &&
     old.next_run==(int64_t)target && (uint64_t)old.next_run>current)return OK;
  if(vg_clear_job()!=OK){vg_latch_fault();return ERROR;}
  strcpy(job.name,"VelaGuard reconcile");strcpy(job.action,"velaguard.wake");
  strcpy(job.action_args,"{}");strcpy(job.message,"VelaGuard wake");
  strcpy(job.channel,"system");strcpy(job.chat_id,"velaguard");
  job.kind=CRON_KIND_AT;job.at_epoch=(int64_t)target;job.delete_after_run=true;
  if(vg_agent_cron_add_job(&job)!=OK){vg_latch_fault();return ERROR;}
  if(atomic_load(&vg_clock_fault)){(void)vg_clear_job();return ERROR;}
  if(!job.enabled)atomic_store(&vg_pending,true); /* crossed ceil boundary in add */
  return OK;
}
int vg_cron_reconcile(void *context,bool has_next,int64_t epoch)
{
  if(!has_next)return vg_cron_reconcile_mono(context,false,0);
  int64_t wall=(int64_t)VG_CRON_RAW_TIME(NULL);
  uint64_t now;
  if(epoch<=0 || wall<=0 || vg_cron_monotonic_ms(NULL,&now)!=OK)
    {(void)vg_cron_reconcile_mono(context,false,0);return ERROR;}
  uint64_t seconds=epoch>wall?(uint64_t)(epoch-wall):0;
  if(seconds>86400u)seconds=86400u;
  if(now>UINT64_MAX-seconds*1000u)return ERROR;
  return vg_cron_reconcile_mono(context,true,now+seconds*1000u);
}
