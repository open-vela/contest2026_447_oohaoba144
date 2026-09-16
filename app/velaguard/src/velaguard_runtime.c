#include "velaguard_runtime.h"
#include <string.h>

/* UTC wake conversions are rechecked within a day, without changing due_epoch. */
#define VG_RUNTIME_ABS_RECHECK_SEC 86400u

static bool bounded(const char *s,size_t cap)
{return s && s[0] && memchr(s,0,cap);}
static int usable(const vg_runtime_t *r)
{if(!r)return VG_ERR_INVALID_MESSAGE;return r->ready&&!r->blocked?VG_OK:VG_RUNTIME_BLOCKED;}
static vg_task_t *lookup(vg_runtime_t *r,const char *id,size_t *index,bool *history)
{
 for(size_t i=0;i<r->store.active.count+r->store.history_count;i++) {
  bool old=i>=r->store.active.count;size_t pos=old?(r->store.history_start+i-r->store.active.count)%VG_HISTORY_CAPACITY:i;
  vg_task_t *t=old?&r->store.history[pos]:&r->store.active.tasks[pos];
  if(!strcmp(t->request.request_id,id)){if(index)*index=pos;if(history)*history=old;return t;}
 }
 return NULL;
}
static int cancel_schedule(vg_runtime_t *r)
{
 int rc=r->timed_mode?r->timers.scheduler_reconcile(r->timers.context,false,0):r->deps.scheduler_reconcile(r->deps.context,false,0);
 if(rc==0){r->has_next=false;r->next_epoch=0;r->next_mono_ms=0;}
 return rc==0?VG_OK:VG_RUNTIME_SCHEDULER_ERROR;
}
static int clock_read(vg_runtime_t *r)
{
 int64_t now=0;bool valid=false;
 if(r->deps.clock(r->deps.context,&now,&valid)!=0){r->rtc_valid=false;if(r->timed_mode)r->now_epoch=0;return VG_ERR_INVALID_TIME;}
 r->now_epoch=now;r->rtc_valid=valid&&now>0;return VG_OK;
}
static int timed_clock_read(vg_runtime_t *r)
{
 uint64_t now=0;
 if(r->timers.monotonic_ms(r->timers.context,&now)!=0 || (r->mono_seen&&now<r->now_mono_ms)) {
  r->mono_valid=false;r->blocked=true;int rc=cancel_schedule(r);return rc==VG_OK?VG_ERR_INVALID_TIME:rc;
 }
 r->now_mono_ms=now;r->mono_seen=true;r->mono_valid=true;
 (void)clock_read(r); /* UTC不可用只暂停ABS。 */
 if(r->rtc_valid)for(size_t i=0;i<r->store.active.count;i++) {
  const vg_task_t *task=&r->store.active.tasks[i];
  if(task->timer_domain==VG_TIMER_ABSOLUTE&&r->now_epoch<task->updated_epoch){r->rtc_valid=false;break;}
 }
 return VG_OK;
}
static int reconcile(vg_runtime_t *r)
{
 bool has=false;uint64_t next_mono=0;int64_t next_epoch=0;
 if(r->timed_mode&&(!r->boot_ready||!r->mono_valid))return cancel_schedule(r);
 for(size_t i=0;i<r->store.active.count;i++) {
  const vg_task_t *task=&r->store.active.tasks[i];uint64_t candidate_mono=0;int64_t candidate_epoch=0;
  if(task->state==VG_TASK_NEEDS_RESET)continue;
  if(task->timer_domain==VG_TIMER_RELATIVE) {
   if(!r->timed_mode||task->timer_boot_id!=r->active_boot_id)continue;
   candidate_mono=task->mono_deadline_ms;
   if(task->state==VG_TASK_ALERTING) {
    uint64_t grace=(uint64_t)VG_RUNTIME_MISS_GRACE_SEC*1000u+1u;
    /* No representable first MISSED instant: retain ALERTING. Scheduling
     * MAX here would repeatedly wake without any possible state change. */
    if(candidate_mono>UINT64_MAX-grace)continue;
    candidate_mono+=grace;
   }
  } else {
   if(!r->rtc_valid)continue;
   candidate_epoch=task->request.due_epoch;
   if(task->state==VG_TASK_ALERTING) {
    if(candidate_epoch>INT64_MAX-(VG_RUNTIME_MISS_GRACE_SEC+1))continue;
    candidate_epoch+=VG_RUNTIME_MISS_GRACE_SEC+1;
   }
   if(r->timed_mode) {
    uint64_t delta=candidate_epoch>r->now_epoch?(uint64_t)(candidate_epoch-r->now_epoch):0;
    if(delta>VG_RUNTIME_ABS_RECHECK_SEC)delta=VG_RUNTIME_ABS_RECHECK_SEC;
    uint64_t wait_ms=delta*1000u;
    uint64_t remaining=UINT64_MAX-r->now_mono_ms;
    if(wait_ms>remaining)wait_ms=remaining;
    /* At the end of the monotonic range a future UTC deadline cannot get a
     * later wake. Omit this candidate without blocking independent REL work. */
    if(candidate_epoch>r->now_epoch&&wait_ms==0)continue;
    candidate_mono=r->now_mono_ms+wait_ms;
   }
  }
  if(!has || (r->timed_mode?candidate_mono<next_mono:candidate_epoch<next_epoch)) {
   has=true;next_mono=candidate_mono;next_epoch=candidate_epoch;
  }
 }
 int rc=r->timed_mode?r->timers.scheduler_reconcile(r->timers.context,has,next_mono):r->deps.scheduler_reconcile(r->deps.context,has,next_epoch);
 if(rc!=0){r->blocked=true;if(r->timed_mode)(void)cancel_schedule(r);return VG_RUNTIME_SCHEDULER_ERROR;}
 r->has_next=has;r->next_epoch=next_epoch;r->next_mono_ms=next_mono;return VG_OK;
}
static int64_t relative_event(const vg_runtime_t *r, const vg_task_t *task)
{
  int64_t latest = task->created_epoch;
  const int64_t times[] = {task->updated_epoch, task->acknowledged_epoch,
                          task->snoozed_epoch, task->missed_epoch};
  for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++)
    if (times[i] > latest) latest = times[i];
  return r->rtc_valid && r->now_epoch >= latest ? r->now_epoch : 0;
}
static int transition(vg_runtime_t *r,size_t index,vg_task_state_t state)
{
 char id[VG_REQUEST_ID_CAPACITY];strcpy(id,r->store.active.tasks[index].request.request_id);
 int rc=vg_store_transition(&r->store,index,state);if(rc!=VG_OK)return rc;
 vg_task_t *task=lookup(r,id,NULL,NULL);int64_t event=r->now_epoch;
 if(task->timer_domain==VG_TIMER_RELATIVE) {
  event=relative_event(r,task);
  if(event)task->updated_epoch=event;
 } else task->updated_epoch=event;
 if(state==VG_TASK_ACKNOWLEDGED)task->acknowledged_epoch=event;
 if(state==VG_TASK_MISSED)task->missed_epoch=event;
 return VG_OK;
}
static int apply_tick(vg_runtime_t *r)
{
 size_t i=0;
 while(i<r->store.active.count) {
  vg_task_t *task=&r->store.active.tasks[i];int rc;bool due,missed;
  if(task->state==VG_TASK_NEEDS_RESET){i++;continue;}
  if(task->timer_domain==VG_TIMER_RELATIVE) {
   if(!r->timed_mode)return VG_ERR_INVALID_STATE;
   if(task->timer_boot_id!=r->active_boot_id) {
    if(task->state!=VG_TASK_ALERTING)return VG_RUNTIME_BOOT_CONFLICT;
    i++;continue; /* 不用新boot的uptime推算旧提醒的grace。 */
   }
   due=r->now_mono_ms>=task->mono_deadline_ms;
   missed=due&&r->now_mono_ms-task->mono_deadline_ms>(uint64_t)VG_RUNTIME_MISS_GRACE_SEC*1000u;
  } else {
   if(!r->rtc_valid){i++;continue;}
   if(r->now_epoch<task->updated_epoch){r->rtc_valid=false;if(!r->timed_mode)return VG_ERR_INVALID_TIME;i++;continue;}
   due=r->now_epoch>=task->request.due_epoch;
   missed=due&&r->now_epoch-task->request.due_epoch>VG_RUNTIME_MISS_GRACE_SEC;
  }
  if(task->state==VG_TASK_CREATED||task->state==VG_TASK_SNOOZED){rc=transition(r,i,VG_TASK_SCHEDULED);if(rc!=VG_OK)return rc;}
  if(due&&task->state==VG_TASK_SCHEDULED){rc=transition(r,i,VG_TASK_ALERTING);if(rc!=VG_OK)return rc;}
  if(missed){rc=transition(r,i,VG_TASK_MISSED);if(rc!=VG_OK)return rc;continue;}
  i++;
 }
 return VG_OK;
}
static int commit(vg_runtime_t *r)
{
 int rc;
 if(r->rtc_valid||r->timed_mode){rc=apply_tick(r);if(rc!=VG_OK){r->blocked=true;(void)cancel_schedule(r);return rc;}}
 rc=vg_store_save(&r->store,&r->deps.io);
 if(rc!=VG_OK){r->blocked=true;if(r->timed_mode)(void)cancel_schedule(r);return rc;}
 return reconcile(r);
}
static int trusted_clock(vg_runtime_t *r)
{
 int rc=r->timed_mode?timed_clock_read(r):clock_read(r);
 if(r->timed_mode&&rc!=VG_OK)return rc;
 if(rc!=VG_OK||!r->rtc_valid){rc=r->timed_mode?commit(r):reconcile(r);return rc==VG_OK?VG_ERR_INVALID_TIME:rc;}
 return VG_OK;
}
static bool contains_relative(const vg_store_t *s)
{
 for(size_t i=0;i<s->active.count;i++)if(s->active.tasks[i].timer_domain==VG_TIMER_RELATIVE)return true;
 for(size_t i=0;i<s->history_count;i++)if(s->history[(s->history_start+i)%VG_HISTORY_CAPACITY].timer_domain==VG_TIMER_RELATIVE)return true;
 return false;
}
static int prepare_boot(vg_runtime_t *r)
{
 if(r->boot_ready)return r->store.boot_counter==r->active_boot_id?VG_OK:VG_RUNTIME_BOOT_CONFLICT;
 if(!r->pending_boot_id){if(r->store.boot_counter==UINT64_MAX)return VG_ERR_CAPACITY;r->boot_base_id=r->store.boot_counter;r->pending_boot_id=r->boot_base_id+1;}
 if(r->store.boot_counter==r->boot_base_id){int rc=vg_store_advance_boot(&r->store);if(rc!=VG_OK)return rc;}
 else if(r->store.boot_counter!=r->pending_boot_id)return VG_RUNTIME_BOOT_CONFLICT;
 for(size_t i=0;i<r->store.active.count;i++) {
  vg_task_t *task=&r->store.active.tasks[i];if(task->timer_domain!=VG_TIMER_RELATIVE)continue;
  if(task->timer_boot_id>=r->pending_boot_id)return VG_RUNTIME_BOOT_CONFLICT;
  if(task->state==VG_TASK_CREATED||task->state==VG_TASK_SCHEDULED||task->state==VG_TASK_SNOOZED){int rc=vg_store_transition(&r->store,i,VG_TASK_NEEDS_RESET);if(rc!=VG_OK)return rc;}
 }
 int rc=vg_store_save(&r->store,&r->deps.io);if(rc!=VG_OK)return rc;
 r->active_boot_id=r->pending_boot_id;r->boot_ready=true;return VG_OK;
}
int vg_runtime_start(vg_runtime_t *r,const vg_runtime_deps_t *deps)
{
 if(!r||!deps||!deps->clock||!deps->scheduler_reconcile||!deps->io.read||!deps->io.write_sync)return VG_ERR_INVALID_MESSAGE;
 memset(r,0,sizeof(*r));r->deps=*deps;vg_store_init(&r->store);return vg_runtime_reload(r);
}
int vg_runtime_start_with_timers(vg_runtime_t *r,const vg_runtime_deps_t *deps,const vg_runtime_timer_ops_t *timers)
{
 if(!r||!deps||!deps->clock||!deps->io.read||!deps->io.write_sync||!timers||!timers->monotonic_ms||!timers->scheduler_reconcile)return VG_ERR_INVALID_MESSAGE;
 memset(r,0,sizeof(*r));r->deps=*deps;r->timers=*timers;r->timed_mode=true;vg_store_init(&r->store);return vg_runtime_reload(r);
}
int vg_runtime_reload(vg_runtime_t *r)
{
 if(!r||!r->deps.clock||(!r->timed_mode&&!r->deps.scheduler_reconcile)||(r->timed_mode&&(!r->timers.monotonic_ms||!r->timers.scheduler_reconcile)))return VG_ERR_INVALID_MESSAGE;
 r->blocked=true;r->ready=false;
 int rc=vg_store_load(&r->store,&r->deps.io);if(rc!=VG_OK){(void)cancel_schedule(r);return rc;}
 if(!r->timed_mode&&contains_relative(&r->store)){(void)cancel_schedule(r);return VG_ERR_INVALID_STATE;}
 rc=r->timed_mode?timed_clock_read(r):clock_read(r);if(rc!=VG_OK){(void)cancel_schedule(r);return rc;}
 if(r->timed_mode){rc=prepare_boot(r);if(rc!=VG_OK){(void)cancel_schedule(r);return rc;}}
 rc=commit(r);if(rc!=VG_OK)return rc;
 r->ready=true;r->blocked=false;return VG_OK;
}
int vg_runtime_tick(vg_runtime_t *r)
{
 int rc=usable(r);if(rc!=VG_OK)return rc;
 if(r->timed_mode){rc=timed_clock_read(r);return rc==VG_OK?commit(r):rc;}
 rc=trusted_clock(r);if(rc!=VG_OK)return rc;
 rc=apply_tick(r);if(rc!=VG_OK){r->blocked=true;(void)reconcile(r);return rc;}
 return commit(r);
}
int vg_runtime_find(const vg_runtime_t *r, const char *id, vg_task_t *out,
                     bool *history)
{
  size_t i;
  const vg_task_t *task;
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (!bounded(id, VG_REQUEST_ID_CAPACITY) || out == NULL)
    return VG_ERR_INVALID_MESSAGE;
  for (i = 0; i < r->store.active.count + r->store.history_count; i++)
    {
      bool old = i >= r->store.active.count;
      task = old ? &r->store.history[(r->store.history_start + i -
              r->store.active.count) % VG_HISTORY_CAPACITY] : &r->store.active.tasks[i];
      if (strcmp(task->request.request_id, id) == 0)
        {
          *out = *task;
          if (history != NULL) *history = old;
          return VG_OK;
        }
    }
  return VG_ERR_TASK_NOT_FOUND;
}

int vg_runtime_list(const vg_runtime_t *r, bool history, size_t offset,
                     vg_task_t *out)
{
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (out == NULL) return VG_ERR_INVALID_MESSAGE;
  if (offset >= (history ? r->store.history_count : r->store.active.count))
    return VG_ERR_TASK_NOT_FOUND;
  *out = history ? r->store.history[(r->store.history_start + offset) % VG_HISTORY_CAPACITY] :
                  r->store.active.tasks[offset];
  return VG_OK;
}

int vg_runtime_status(const vg_runtime_t *r, vg_runtime_status_t *out)
{
  size_t i;
  if (r == NULL || out == NULL) return VG_ERR_INVALID_MESSAGE;
  memset(out, 0, sizeof(*out));
  out->ready = r->ready;
  out->blocked = r->blocked;
  out->rtc_valid = r->rtc_valid;
  out->has_next = r->has_next;
  out->now_epoch = r->now_epoch;
  out->next_epoch = r->next_epoch;
  out->timed_mode=r->timed_mode;out->mono_valid=r->mono_valid;out->boot_ready=r->boot_ready;
  out->now_mono_ms=r->now_mono_ms;out->next_mono_ms=r->next_mono_ms;out->active_boot_id=r->active_boot_id;
  out->active_count = r->store.active.count;
  out->history_count = r->store.history_count;
  for (i = 0; i < r->store.active.count; i++)
    if (r->store.active.tasks[i].state == VG_TASK_ALERTING) out->alerting_count++;
  return VG_OK;
}

int vg_runtime_create(vg_runtime_t *r, const vg_create_request_t *request,
                       vg_task_t *out)
{
  vg_task_t *existing;
  size_t index;
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (request == NULL || out == NULL ||
      !bounded(request->request_id, sizeof(request->request_id)) ||
      !bounded(request->title, sizeof(request->title)) || request->due_epoch <= 0 ||
      request->priority < 0 || request->priority > 2) return VG_ERR_INVALID_MESSAGE;
  existing = lookup(r, request->request_id, NULL, NULL);
  if (existing != NULL)
    {
      if(existing->timer_domain!=VG_TIMER_ABSOLUTE)return VG_ERR_INVALID_STATE;
      *out = *existing;
      return VG_OK;
    }
  rc = trusted_clock(r);
  if (rc != VG_OK) return rc;
  rc = vg_store_create(&r->store, request, r->now_epoch, true, &index);
  if (rc != VG_OK) return rc;
  rc = transition(r, index, VG_TASK_SCHEDULED);
  if (rc != VG_OK)
    {
      r->blocked = true;
      return rc;
    }
  rc = commit(r);
  if (rc != VG_OK) return rc;
  *out = *lookup(r, request->request_id, NULL, NULL);
  return VG_OK;
}

static int control_target(vg_runtime_t *r, const char *id, size_t *index,
                            vg_task_t **task)
{
  bool history;
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (!bounded(id, VG_REQUEST_ID_CAPACITY)) return VG_ERR_INVALID_MESSAGE;
  *task = lookup(r, id, index, &history);
  if (*task == NULL) return VG_ERR_TASK_NOT_FOUND;
  if((*task)->timer_domain==VG_TIMER_RELATIVE)return VG_ERR_INVALID_STATE;
  if (history || (*task)->state != VG_TASK_ALERTING) return VG_ERR_INVALID_STATE;
  rc = trusted_clock(r);
  if (rc != VG_OK) return rc;
  if (r->now_epoch < (*task)->updated_epoch) return VG_ERR_INVALID_TIME;
  return VG_OK;
}

static int ack_relative(vg_runtime_t *r, const char *id, vg_task_t *out)
{
  bool history;
  size_t index;
  vg_task_t *task = lookup(r, id, &index, &history);
  if (!r->timed_mode || !r->boot_ready) return VG_ERR_INVALID_STATE;
  if (task->state == VG_TASK_ACKNOWLEDGED)
    {
      *out = *task;
      return VG_OK;
    }
  if (history || task->state != VG_TASK_ALERTING ||
      task->timer_boot_id > r->active_boot_id) return VG_ERR_INVALID_STATE;
  int rc = timed_clock_read(r);
  if (rc != VG_OK) return rc;
  if (task->timer_boot_id == r->active_boot_id &&
      r->now_mono_ms >= task->mono_deadline_ms &&
      r->now_mono_ms - task->mono_deadline_ms >
        (uint64_t)VG_RUNTIME_MISS_GRACE_SEC * 1000u)
    {
      rc = commit(r);
      return rc == VG_OK ? VG_ERR_INVALID_STATE : rc;
    }
  rc = transition(r, index, VG_TASK_ACKNOWLEDGED);
  if (rc != VG_OK) return rc;
  rc = commit(r);
  if (rc == VG_OK) *out = *lookup(r, id, NULL, NULL);
  return rc;
}

int vg_runtime_ack(vg_runtime_t *r, const char *id, vg_task_t *out)
{
  vg_task_t *task;
  size_t index;
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (out == NULL || !bounded(id, VG_REQUEST_ID_CAPACITY)) return VG_ERR_INVALID_MESSAGE;
  task = lookup(r, id, NULL, NULL);
  if(task != NULL && task->timer_domain==VG_TIMER_RELATIVE)return ack_relative(r,id,out);
  if (task != NULL && task->state == VG_TASK_ACKNOWLEDGED)
    {
      *out = *task;
      return VG_OK;
    }
  rc = control_target(r, id, &index, &task);
  if (rc != VG_OK) return rc;
  if (r->now_epoch - task->request.due_epoch > VG_RUNTIME_MISS_GRACE_SEC)
    {
      rc = vg_runtime_tick(r);
      return rc == VG_OK ? VG_ERR_INVALID_STATE : rc;
    }
  rc = transition(r, index, VG_TASK_ACKNOWLEDGED);
  if (rc != VG_OK) return rc;
  rc = commit(r);
  if (rc != VG_OK) return rc;
  *out = *lookup(r, id, NULL, NULL);
  return VG_OK;
}

int vg_runtime_snooze(vg_runtime_t *r, const char *id, uint32_t seconds,
                       vg_task_t *out)
{
  vg_task_t *task;
  size_t index;
  int rc;
  if (out == NULL) return VG_ERR_INVALID_MESSAGE;
  if (seconds == 0 || seconds > VG_RUNTIME_MAX_SNOOZE_SEC) return VG_ERR_INVALID_TIME;
  rc = control_target(r, id, &index, &task);
  if (rc != VG_OK) return rc;
  if (r->now_epoch > INT64_MAX - seconds) return VG_ERR_INVALID_TIME;
  if (task->snooze_count == UINT32_MAX) return VG_ERR_CAPACITY;
  if (r->now_epoch - task->request.due_epoch > VG_RUNTIME_MISS_GRACE_SEC)
    {
      rc = vg_runtime_tick(r);
      return rc == VG_OK ? VG_ERR_INVALID_STATE : rc;
    }
  rc = transition(r, index, VG_TASK_SNOOZED);
  if (rc != VG_OK) return rc;
  task->request.due_epoch = r->now_epoch + seconds;
  task->snoozed_epoch = r->now_epoch;
  task->snooze_count++;
  rc = transition(r, index, VG_TASK_SCHEDULED);
  if (rc != VG_OK)
    {
      r->blocked = true;
      return rc;
    }
  rc = commit(r);
  if (rc != VG_OK) return rc;
  *out = *lookup(r, id, NULL, NULL);
  return VG_OK;
}


int vg_runtime_create_relative(vg_runtime_t *r,
                               const vg_create_request_t *request,
                               uint32_t delay, vg_task_t *out)
{
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (!r->timed_mode || !r->boot_ready) return VG_ERR_INVALID_STATE;
  if (!request || !out ||
      !bounded(request->request_id, sizeof(request->request_id)) ||
      !bounded(request->title, sizeof(request->title)) ||
      request->due_epoch != 0 || request->priority < 0 || request->priority > 2)
    return VG_ERR_INVALID_MESSAGE;
  if (!delay || delay > VG_RELATIVE_MAX_DELAY_SECONDS) return VG_ERR_INVALID_TIME;
  vg_task_t *existing = lookup(r, request->request_id, NULL, NULL);
  if (existing)
    {
      if (existing->timer_domain != VG_TIMER_RELATIVE ||
          strcmp(existing->request.title, request->title) ||
          existing->request.priority != request->priority ||
          existing->delay_seconds != delay) return VG_ERR_DUPLICATE_REQUEST;
      *out = *existing;
      return VG_OK;
    }
  rc = timed_clock_read(r);
  if (rc != VG_OK) return rc;
  size_t index;
  rc = vg_store_create_relative(&r->store, request, delay, r->now_mono_ms, &index);
  if (rc != VG_OK) return rc;
  vg_task_t *task = &r->store.active.tasks[index];
  task->created_epoch = r->rtc_valid ? r->now_epoch : 0;
  task->updated_epoch = task->created_epoch;
  rc = transition(r, index, VG_TASK_SCHEDULED);
  if (rc != VG_OK)
    {
      r->blocked = true;
      (void)cancel_schedule(r);
      return rc;
    }
  rc = commit(r);
  if (rc == VG_OK) *out = *lookup(r, request->request_id, NULL, NULL);
  return rc;
}

/* Validate the complete timer change before altering state or metadata. The
 * current-boot grace check runs only after numeric preflight; an old-boot alert
 * cannot be expired by this boot's uptime and can be explicitly rebound. */
static int relative_target(vg_runtime_t *r, const char *id,
                           vg_task_state_t required, uint64_t revision,
                           size_t *index, vg_task_t **task)
{
  int rc = usable(r);
  if (rc != VG_OK) return rc;
  if (!r->timed_mode || !r->boot_ready) return VG_ERR_INVALID_STATE;
  if (!bounded(id, VG_REQUEST_ID_CAPACITY) || !revision)
    return VG_ERR_INVALID_MESSAGE;
  bool history;
  *task = lookup(r, id, index, &history);
  if (!*task) return VG_ERR_TASK_NOT_FOUND;
  if (history || (*task)->timer_domain != VG_TIMER_RELATIVE ||
      (*task)->state != required || (*task)->timer_revision != revision ||
      (*task)->timer_boot_id > r->active_boot_id) return VG_ERR_INVALID_STATE;
  if (revision == UINT64_MAX) return VG_ERR_CAPACITY;
  return VG_OK;
}

int vg_runtime_snooze_relative(vg_runtime_t *r, const char *id, uint32_t seconds,
                               uint64_t revision, vg_task_t *out)
{
  if (!out) return VG_ERR_INVALID_MESSAGE;
  if (!seconds || seconds > VG_RUNTIME_MAX_SNOOZE_SEC) return VG_ERR_INVALID_TIME;
  size_t index;
  vg_task_t *task;
  int rc = relative_target(r, id, VG_TASK_ALERTING, revision, &index, &task);
  if (rc != VG_OK) return rc;
  if (task->snooze_count == UINT32_MAX) return VG_ERR_CAPACITY;
  rc = timed_clock_read(r);
  if (rc != VG_OK) return rc;
  uint64_t delta = (uint64_t)seconds * 1000u;
  if (r->now_mono_ms > UINT64_MAX - delta) return VG_ERR_INVALID_TIME;
  if (task->timer_boot_id == r->active_boot_id &&
      r->now_mono_ms >= task->mono_deadline_ms &&
      r->now_mono_ms - task->mono_deadline_ms >
        (uint64_t)VG_RUNTIME_MISS_GRACE_SEC * 1000u)
    {
      rc = commit(r);
      return rc == VG_OK ? VG_ERR_INVALID_STATE : rc;
    }
  int64_t event = relative_event(r, task);
  rc = transition(r, index, VG_TASK_SNOOZED);
  if (rc != VG_OK) return rc;
  task->mono_deadline_ms = r->now_mono_ms + delta;
  task->timer_boot_id = r->active_boot_id;
  task->timer_revision++;
  task->snooze_count++;
  task->snoozed_epoch = event;
  rc = transition(r, index, VG_TASK_SCHEDULED);
  if (rc != VG_OK)
    {
      r->blocked = true;
      (void)cancel_schedule(r);
      return rc;
    }
  rc = commit(r);
  if (rc == VG_OK) *out = *lookup(r, id, NULL, NULL);
  return rc;
}

int vg_runtime_rearm_relative(vg_runtime_t *r, const char *id, uint64_t revision,
                              vg_task_t *out)
{
  if (!out) return VG_ERR_INVALID_MESSAGE;
  size_t index;
  vg_task_t *task;
  int rc = relative_target(r, id, VG_TASK_NEEDS_RESET, revision, &index, &task);
  if (rc != VG_OK) return rc;
  rc = timed_clock_read(r);
  if (rc != VG_OK) return rc;
  uint64_t delta = (uint64_t)task->delay_seconds * 1000u;
  if (r->now_mono_ms > UINT64_MAX - delta) return VG_ERR_INVALID_TIME;
  task->mono_deadline_ms = r->now_mono_ms + delta;
  task->timer_boot_id = r->active_boot_id;
  task->timer_revision++;
  rc = transition(r, index, VG_TASK_SCHEDULED);
  if (rc != VG_OK)
    {
      r->blocked = true;
      (void)cancel_schedule(r);
      return rc;
    }
  rc = commit(r);
  if (rc == VG_OK) *out = *lookup(r, id, NULL, NULL);
  return rc;
}
