#include "velaguard_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char disk[2][VG_STORE_MAX_BYTES];static size_t sizes[2];
static vg_store_t seed;static vg_runtime_t r,legacy;static unsigned writes,old_calls,mono_calls;
static int fail_write,fail_mono,fail_sched,fail_clock;static bool rtc,armed;static int64_t wall=1000;static uint64_t mono=5000,wake;
static int rd(void *c,unsigned slot,char *out,size_t cap,size_t *n){(void)c;if(!sizes[slot])return 1;assert(cap>=sizes[slot]);*n=sizes[slot];memcpy(out,disk[slot],*n);return 0;}
static int wr(void *c,unsigned slot,const char *data,size_t n){(void)c;writes++;if(fail_write!=1){memcpy(disk[slot],data,n);sizes[slot]=n;}return fail_write?-1:0;}
static int clock_get(void *c,int64_t *now,bool *valid){(void)c;if(fail_clock)return -1;*now=wall;*valid=rtc;return 0;}
static int old_sched(void *c,bool has,int64_t next){(void)c;(void)has;(void)next;old_calls++;return 0;}
static int mono_get(void *c,uint64_t *now){(void)c;if(fail_mono)return -1;*now=mono;return 0;}
static int mono_sched(void *c,bool has,uint64_t next){(void)c;mono_calls++;if(fail_sched)return -1;armed=has;wake=next;return 0;}
static vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clock_get,old_sched};
static vg_runtime_timer_ops_t timers={NULL,mono_get,mono_sched};
static vg_create_request_t request(const char *id,int64_t due){vg_create_request_t q={0};strcpy(q.request_id,id);strcpy(q.title,"timer A");q.priority=1;q.due_epoch=due;return q;}
static void reset(void){memset(sizes,0,sizeof(sizes));writes=old_calls=mono_calls=0;fail_write=fail_mono=fail_sched=fail_clock=0;rtc=false;wall=1000;mono=5000;armed=false;wake=0;}
static void fixture(void){
 reset();vg_store_init(&seed);assert(vg_store_advance_boot(&seed)==0);
 const char *ids[]={"old-created","old-scheduled","old-snoozed","old-alert"};
 for(size_t i=0;i<4;i++){
  vg_create_request_t q=request(ids[i],0);size_t index;assert(vg_store_create_relative(&seed,&q,60,0,&index)==0);
  if(i>=1)assert(vg_store_transition(&seed,index,VG_TASK_SCHEDULED)==0);
  if(i>=2)assert(vg_store_transition(&seed,index,VG_TASK_ALERTING)==0);
  if(i==2)assert(vg_store_transition(&seed,index,VG_TASK_SNOOZED)==0);
 }
 vg_create_request_t a=request("absolute",2000);assert(vg_store_create(&seed,&a,1,true,NULL)==0);assert(vg_store_save(&seed,&deps.io)==0);
 assert(vg_store_transition(&seed,4,VG_TASK_NEEDS_RESET)==VG_ERR_INVALID_STATE);
 assert(vg_store_transition(&seed,3,VG_TASK_NEEDS_RESET)==VG_ERR_INVALID_STATE);
}
static void boot_and_domains(void){
 fixture();unsigned before=writes;assert(vg_runtime_start(&legacy,&deps)<0 && legacy.blocked && !legacy.ready && writes==before);
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && r.ready && r.boot_ready && r.active_boot_id==2 && r.store.boot_counter==2 && !r.rtc_valid);
 assert(old_calls==1); /* 仅上面的 legacy 拒绝需要取消旧 scheduler。 */
 for(size_t i=0;i<3;i++)assert(r.store.active.tasks[i].state==VG_TASK_NEEDS_RESET);
 assert(r.store.active.tasks[3].state==VG_TASK_ALERTING && !armed && !r.has_next && r.next_epoch==0);
 before=writes;assert(vg_runtime_reload(&r)==0 && r.active_boot_id==2 && r.store.boot_counter==2 && writes==before);
 /* A轮不新增业务入口，直接用合法Store API装同boot fixture测试领域tick。 */
 vg_create_request_t q=request("same-boot",0);size_t pos;assert(vg_store_create_relative(&r.store,&q,1,mono,&pos)==0);assert(vg_store_save(&r.store,&deps.io)==0);
 assert(vg_runtime_reload(&r)==0 && r.store.active.tasks[pos].state==VG_TASK_SCHEDULED && armed && wake==6000 && r.next_mono_ms==6000 && r.next_epoch==0);
 mono=6000;assert(vg_runtime_tick(&r)==0 && r.store.active.tasks[pos].state==VG_TASK_ALERTING);
 assert(wake==306001 && r.store.active.tasks[3].state==VG_TASK_ALERTING);
 vg_task_t out;assert(vg_runtime_snooze(&r,"same-boot",1,&out)==VG_ERR_INVALID_STATE);/* B enables explicit ACK for retained REL alerts. */
 mono=5999;assert(vg_runtime_tick(&r)<0 && r.blocked && !armed);
 assert(vg_runtime_reload(&r)<0 && r.blocked && !armed && r.now_mono_ms==6000);
 mono=6000;assert(vg_runtime_reload(&r)==0 && r.active_boot_id==2 && r.store.active.tasks[pos].state==VG_TASK_ALERTING);
 fail_mono=1;assert(vg_runtime_tick(&r)<0 && r.blocked && !armed);fail_mono=0;
 assert(vg_runtime_reload(&r)==0 && r.active_boot_id==2);
 /* 有效wall再倒退仅暂停ABS，不能阻断REL。 */
 rtc=true;assert(vg_runtime_tick(&r)==0);assert(r.store.active.tasks[4].updated_epoch==1000);
 q=request("wall-independent",0);assert(vg_store_create_relative(&r.store,&q,1,mono,&pos)==0);assert(vg_store_save(&r.store,&deps.io)==0);
 wall=900;mono=7000;assert(vg_runtime_reload(&r)==0 && !r.rtc_valid && r.ready && !r.blocked);
 assert(r.store.active.tasks[pos].state==VG_TASK_ALERTING && r.store.active.tasks[4].state==VG_TASK_SCHEDULED);
 before=writes;r.store.boot_counter++;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);assert(vg_runtime_reload(&r)<0 && r.blocked && !r.ready && !armed && writes==before+1);
}
static void pending_boot_recovery(void){
 for(int mode=1;mode<=2;mode++){
  reset();fail_write=mode;
  assert(vg_runtime_start_with_timers(&r,&deps,&timers)==VG_STORE_IO_ERROR && !r.boot_ready && r.pending_boot_id==1 && !armed);
  unsigned before=writes;fail_write=0;assert(vg_runtime_reload(&r)==0 && r.boot_ready && r.active_boot_id==1 && r.store.boot_counter==1);
  assert(writes==before+(mode==1?1u:0u));assert(old_calls==0);
  assert(vg_runtime_reload(&r)==0 && r.store.boot_counter==1);
 }
 reset();fail_sched=1;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==VG_RUNTIME_SCHEDULER_ERROR && r.boot_ready && r.active_boot_id==1);
 fail_sched=0;assert(vg_runtime_reload(&r)==0 && r.active_boot_id==1 && !r.blocked);
 reset();vg_store_init(&seed);seed.boot_counter=UINT64_MAX;seed.dirty=true;assert(vg_store_save(&seed,&deps.io)==0);unsigned before=writes;
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==VG_ERR_CAPACITY && !r.boot_ready && writes==before && !armed);
}

static void recovery_faults(void){
 /* A slot rollback after a successfully established boot must never issue a new ID. */
 reset();vg_store_init(&seed);seed.boot_counter=4;seed.dirty=true;assert(vg_store_save(&seed,&deps.io)==0);
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && r.active_boot_id==5);
 r.store.boot_counter=4;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);unsigned before=writes;
 assert(vg_runtime_reload(&r)==VG_RUNTIME_BOOT_CONFLICT && r.blocked && !r.ready && writes==before && !armed);
 /* An unexpected counter while the first save is uncertain also fails closed. */
 reset();fail_write=1;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==VG_STORE_IO_ERROR);
 fail_write=0;vg_store_init(&seed);seed.boot_counter=2;seed.dirty=true;assert(vg_store_save(&seed,&deps.io)==0);before=writes;
 assert(vg_runtime_reload(&r)==VG_RUNTIME_BOOT_CONFLICT && !r.boot_ready && r.pending_boot_id==1 && writes==before);
 /* Clearing one cause cannot clear outstanding persistence, mono, or scheduler failures. */
 reset();fail_write=1;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==VG_STORE_IO_ERROR);before=writes;
 fail_mono=1;assert(vg_runtime_reload(&r)<0 && r.blocked && !r.boot_ready && writes==before);
 fail_mono=0;assert(vg_runtime_reload(&r)==VG_STORE_IO_ERROR && r.blocked && !r.boot_ready);
 fail_write=0;fail_sched=1;assert(vg_runtime_reload(&r)==VG_RUNTIME_SCHEDULER_ERROR && r.blocked && r.boot_ready && r.active_boot_id==1);
 fail_mono=1;fail_sched=0;assert(vg_runtime_reload(&r)<0 && r.blocked && r.boot_ready);
 fail_mono=0;assert(vg_runtime_reload(&r)==0 && !r.blocked && r.active_boot_id==1);
 /* A failed initial mono sample must not allocate or persist a boot identity. */
 reset();fail_mono=1;assert(vg_runtime_start_with_timers(&r,&deps,&timers)<0 && writes==0 && r.pending_boot_id==0);
 fail_mono=0;fail_clock=1;assert(vg_runtime_reload(&r)==0 && !r.rtc_valid && r.active_boot_id==1);
 /* Exact millisecond grace boundary and unknown-UTC history remain domain-correct. */
 vg_create_request_t q=request("grace",0);size_t pos;assert(vg_store_create_relative(&r.store,&q,1,mono,&pos)==0);assert(vg_store_save(&r.store,&deps.io)==0);
 assert(vg_runtime_reload(&r)==0);mono=306000;assert(vg_runtime_tick(&r)==0 && r.store.active.tasks[pos].state==VG_TASK_ALERTING);
 mono++;assert(vg_runtime_tick(&r)==0 && r.store.active.count==0 && r.store.history_count==1);
 vg_task_t out;bool history=false;assert(vg_runtime_find(&r,"grace",&out,&history)==0 && history && out.state==VG_TASK_MISSED && out.missed_epoch==0 && out.updated_epoch==0);
 fail_clock=0;before=writes;assert(vg_runtime_start(&legacy,&deps)==VG_ERR_INVALID_STATE && writes==before);
}


static void relative_business(void){
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
 vg_create_request_t q=request("relative-business",0);vg_task_t out;
 assert(vg_runtime_create_relative(&r,&q,60,&out)==0 && out.timer_revision==1 && out.timer_boot_id==1 && out.mono_deadline_ms==65000 && out.created_epoch==0 && out.updated_epoch==0);
 unsigned before=writes;mono=6000;
 assert(vg_runtime_create_relative(&r,&q,60,&out)==0 && out.mono_deadline_ms==65000 && writes==before);
 q.priority=2;assert(vg_runtime_create_relative(&r,&q,60,&out)==VG_ERR_DUPLICATE_REQUEST && writes==before);q.priority=1;
 assert(vg_runtime_create_relative(&r,&q,61,&out)==VG_ERR_DUPLICATE_REQUEST && writes==before);
 mono=65000;assert(vg_runtime_tick(&r)==0);
 assert(vg_runtime_snooze_relative(&r,q.request_id,30,1,&out)==0 && out.timer_revision==2 && out.delay_seconds==60 && out.mono_deadline_ms==95000 && out.snooze_count==1 && out.snoozed_epoch==0);
 before=writes;assert(vg_runtime_snooze_relative(&r,q.request_id,30,1,&out)==VG_ERR_INVALID_STATE && writes==before);
 assert(vg_runtime_snooze(&r,q.request_id,30,&out)==VG_ERR_INVALID_STATE);
 mono=95000;assert(vg_runtime_tick(&r)==0);
 assert(vg_runtime_ack(&r,q.request_id,&out)==0 && out.state==VG_TASK_ACKNOWLEDGED && out.acknowledged_epoch==0 && out.timer_revision==2);
 before=writes;assert(vg_runtime_ack(&r,q.request_id,&out)==0 && writes==before);
 assert(vg_runtime_create_relative(&r,&q,60,&out)==0 && out.state==VG_TASK_ACKNOWLEDGED && writes==before);
 /* Actual new-start API creates a new identity. Old alerts remain actionable. */
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
 q=request("old-snooze",0);assert(vg_runtime_create_relative(&r,&q,60,&out)==0);
 q=request("old-ack",0);assert(vg_runtime_create_relative(&r,&q,60,&out)==0);
 mono=65000;assert(vg_runtime_tick(&r)==0);mono=1;
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && r.active_boot_id==2);
 assert(vg_runtime_snooze_relative(&r,"old-snooze",30,1,&out)==0 && out.mono_deadline_ms==30001 && out.delay_seconds==60 && out.timer_boot_id==2 && out.timer_revision==2);
 assert(vg_runtime_ack(&r,"old-ack",&out)==0 && out.state==VG_TASK_ACKNOWLEDGED && out.acknowledged_epoch==0 && out.timer_boot_id==1);
 mono=1;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && r.active_boot_id==3);
 assert(vg_runtime_find(&r,"old-snooze",&out,NULL)==0 && out.state==VG_TASK_NEEDS_RESET);
 before=writes;assert(vg_runtime_rearm_relative(&r,"old-snooze",1,&out)==VG_ERR_INVALID_STATE && writes==before);
 assert(vg_runtime_rearm_relative(&r,"old-snooze",2,&out)==0 && out.state==VG_TASK_SCHEDULED && out.timer_revision==3 && out.timer_boot_id==3 && out.mono_deadline_ms==60001 && out.delay_seconds==60 && out.snooze_count==1);
 before=writes;assert(vg_runtime_rearm_relative(&r,"old-snooze",2,&out)==VG_ERR_INVALID_STATE && writes==before);
}
static void relative_preflight(void){
 static vg_store_t before_store;vg_task_t out;vg_create_request_t q=request("limits",0);
 reset();assert(vg_runtime_start(&legacy,&deps)==0);assert(vg_runtime_create_relative(&legacy,&q,1,&out)==VG_ERR_INVALID_STATE);
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);unsigned before=writes;
 assert(vg_runtime_create_relative(&r,&q,0,&out)==VG_ERR_INVALID_TIME);
 assert(vg_runtime_create_relative(&r,&q,86401,&out)==VG_ERR_INVALID_TIME);
 mono=UINT64_MAX-999;assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_ERR_INVALID_TIME && writes==before && r.store.active.count==0);
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);mono=6000;assert(vg_runtime_tick(&r)==0);
 vg_task_t *task=&r.store.active.tasks[0];task->timer_revision=UINT64_MAX;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);before_store=r.store;before=writes;
 assert(vg_runtime_snooze_relative(&r,"limits",1,UINT64_MAX,&out)==VG_ERR_CAPACITY && writes==before && !memcmp(&before_store,&r.store,sizeof(before_store)));
 /* ACK does not generate a new timer revision and stays usable at MAX. */
 assert(vg_runtime_ack(&r,"limits",&out)==0 && out.timer_revision==UINT64_MAX);
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);mono=6000;assert(vg_runtime_tick(&r)==0);
 task=&r.store.active.tasks[0];task->snooze_count=UINT32_MAX;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);before_store=r.store;before=writes;
 assert(vg_runtime_snooze_relative(&r,"limits",1,1,&out)==VG_ERR_CAPACITY && writes==before && !memcmp(&before_store,&r.store,sizeof(before_store)));
 task->snooze_count=0;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);mono=UINT64_MAX-999;before_store=r.store;before=writes;
 assert(vg_runtime_snooze_relative(&r,"limits",1,1,&out)==VG_ERR_INVALID_TIME && writes==before && !memcmp(&before_store,&r.store,sizeof(before_store)));
 /* Rearm checks revision and deadline before changing NEEDS_RESET. */
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);mono=0;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
 task=&r.store.active.tasks[0];task->timer_revision=UINT64_MAX;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);before_store=r.store;before=writes;
 assert(vg_runtime_rearm_relative(&r,"limits",UINT64_MAX,&out)==VG_ERR_CAPACITY && writes==before && !memcmp(&before_store,&r.store,sizeof(before_store)));
 task->timer_revision=1;r.store.dirty=true;assert(vg_store_save(&r.store,&deps.io)==0);mono=UINT64_MAX-999;before_store=r.store;before=writes;
 assert(vg_runtime_rearm_relative(&r,"limits",1,&out)==VG_ERR_INVALID_TIME && writes==before && !memcmp(&before_store,&r.store,sizeof(before_store)));
}
static void relative_persist_and_utc(void){
 vg_task_t out;vg_create_request_t q=request("uncertain-create",0);
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);fail_write=2;
 assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_STORE_IO_ERROR && r.blocked && !armed);fail_write=0;
 assert(vg_runtime_reload(&r)==0);unsigned before=writes;assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && writes==before && out.timer_revision==1);
 mono=6000;assert(vg_runtime_tick(&r)==0);fail_write=2;
 assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==VG_STORE_IO_ERROR && r.blocked && !armed);fail_write=0;
 assert(vg_runtime_reload(&r)==0);before=writes;assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==VG_ERR_INVALID_STATE && writes==before);
 assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.timer_revision==2 && out.snooze_count==1);
 mono=7000;assert(vg_runtime_tick(&r)==0);fail_write=2;assert(vg_runtime_ack(&r,q.request_id,&out)==VG_STORE_IO_ERROR && r.blocked);fail_write=0;
 assert(vg_runtime_reload(&r)==0);before=writes;assert(vg_runtime_ack(&r,q.request_id,&out)==0 && writes==before);
 q=request("uncertain-rearm",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);mono=0;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);fail_write=2;
 assert(vg_runtime_rearm_relative(&r,q.request_id,1,&out)==VG_STORE_IO_ERROR && r.blocked);fail_write=0;
 assert(vg_runtime_reload(&r)==0);before=writes;assert(vg_runtime_rearm_relative(&r,q.request_id,1,&out)==VG_ERR_INVALID_STATE && writes==before);
 /* Known UTC at creation then unavailable or regressed at control: event=0,
    updated keeps last known time and subsequent snapshots remain valid. */
 reset();rtc=true;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);q=request("known-unknown",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && out.created_epoch==1000);
 mono=6000;rtc=false;assert(vg_runtime_tick(&r)==0);assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==0 && out.snoozed_epoch==0 && out.updated_epoch==1000);
 mono=7000;rtc=true;wall=999;assert(vg_runtime_tick(&r)==0);assert(vg_runtime_ack(&r,q.request_id,&out)==0 && out.acknowledged_epoch==0 && out.updated_epoch==1000);assert(vg_runtime_reload(&r)==0);
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);q=request("unknown-known",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && out.created_epoch==0);
 mono=6000;rtc=true;assert(vg_runtime_tick(&r)==0);assert(vg_runtime_ack(&r,q.request_id,&out)==0 && out.acknowledged_epoch==1000 && out.updated_epoch==1000);assert(vg_runtime_reload(&r)==0);
}


static void relative_invalid_and_retry(void){
 vg_task_t out;vg_create_request_t q=request("reject",0);
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);unsigned before=writes;
 q.due_epoch=100;assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_ERR_INVALID_MESSAGE);q.due_epoch=0;
 q.title[0]=0;assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_ERR_INVALID_MESSAGE);q=request("reject",0);
 assert(vg_runtime_create_relative(&r,&q,1,NULL)==VG_ERR_INVALID_MESSAGE && writes==before);
 assert(vg_runtime_create_relative(&r,&q,86400,&out)==0 && out.delay_seconds==86400);
 before=writes;assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==VG_ERR_INVALID_STATE);
 assert(vg_runtime_rearm_relative(&r,q.request_id,1,&out)==VG_ERR_INVALID_STATE);
 assert(vg_runtime_ack(&r,q.request_id,&out)==VG_ERR_INVALID_STATE && writes==before);
 mono=out.mono_deadline_ms;assert(vg_runtime_tick(&r)==0);before=writes;
 assert(vg_runtime_snooze_relative(&r,q.request_id,0,1,&out)==VG_ERR_INVALID_TIME);
 assert(vg_runtime_snooze_relative(&r,q.request_id,86401,1,&out)==VG_ERR_INVALID_TIME);
 assert(vg_runtime_snooze_relative(&r,q.request_id,1,0,&out)==VG_ERR_INVALID_MESSAGE);
 assert(vg_runtime_snooze_relative(&r,q.request_id,1,2,&out)==VG_ERR_INVALID_STATE && writes==before);
 assert(vg_runtime_snooze_relative(&r,q.request_id,86400,1,&out)==0 && out.timer_revision==2);
 /* Name identity is shared across ABS/REL; domains cannot reinterpret a task. */
 reset();rtc=true;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
 q=request("absolute-id",1001);assert(vg_runtime_create(&r,&q,&out)==0);q.due_epoch=0;before=writes;
 assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_ERR_DUPLICATE_REQUEST);
 assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==VG_ERR_INVALID_STATE);
 assert(vg_runtime_rearm_relative(&r,q.request_id,1,&out)==VG_ERR_INVALID_STATE && writes==before);
 /* A known current-boot expired alert is archived, not acknowledged or snoozed. */
 for(int op=0;op<2;op++){
  reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);q=request("expired",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);mono=6000;assert(vg_runtime_tick(&r)==0);mono=306001;
  int rc=op?vg_runtime_snooze_relative(&r,q.request_id,1,1,&out):vg_runtime_ack(&r,q.request_id,&out);
  assert(rc==VG_ERR_INVALID_STATE);assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==VG_TASK_MISSED && out.timer_revision==1);
 }
 /* Definitely unwritten transactions recover the old revision and can retry. */
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);q=request("not-written",0);fail_write=1;
 memset(&out,0x5a,sizeof(out));vg_task_t sentinel=out;
 assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_STORE_IO_ERROR && !memcmp(&out,&sentinel,sizeof(out)));fail_write=0;
 assert(vg_runtime_reload(&r)==0 && r.store.active.count==0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);
 mono=6000;assert(vg_runtime_tick(&r)==0);fail_write=1;assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==VG_STORE_IO_ERROR);fail_write=0;
 assert(vg_runtime_reload(&r)==0);assert(vg_runtime_snooze_relative(&r,q.request_id,1,1,&out)==0 && out.timer_revision==2);
 mono=0;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);fail_write=1;assert(vg_runtime_rearm_relative(&r,q.request_id,2,&out)==VG_STORE_IO_ERROR);fail_write=0;
 assert(vg_runtime_reload(&r)==0);assert(vg_runtime_rearm_relative(&r,q.request_id,2,&out)==0 && out.timer_revision==3);
 /* Scheduler failure after durable create must keep the persisted identity. */
 reset();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);q=request("scheduler-error",0);fail_sched=1;
 assert(vg_runtime_create_relative(&r,&q,1,&out)==VG_RUNTIME_SCHEDULER_ERROR && r.blocked);fail_sched=0;
 assert(vg_runtime_reload(&r)==0);before=writes;assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && out.timer_revision==1 && writes==before && r.store.active.count==1);
}


static void huge_absolute_seed(void){
 vg_store_init(&seed);vg_create_request_t q=request("huge-absolute",INT64_MAX);
 assert(vg_store_create(&seed,&q,wall,true,NULL)==0);assert(vg_store_save(&seed,&deps.io)==0);
}
static void mixed_clock_boundaries(void){
 reset();rtc=true;huge_absolute_seed();
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && !r.blocked && armed && wake==mono+86400000u && r.next_epoch==INT64_MAX);
 vg_task_t absolute;assert(vg_runtime_find(&r,"huge-absolute",&absolute,NULL)==0 && absolute.request.due_epoch==INT64_MAX);
 /* A wake is only a bounded recheck. It never changes the actual UTC due. */
 mono+=86400000u;wall+=86400;assert(vg_runtime_tick(&r)==0 && wake==mono+86400000u && r.next_epoch==INT64_MAX);
 vg_task_t out;vg_create_request_t q=request("near-relative",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0);
 const uint64_t deadline=out.mono_deadline_ms,revision=out.timer_revision;
 assert(armed && wake==deadline && r.next_epoch==0 && !r.blocked);
 /* Queries must not sample clocks, process due, persist, or touch scheduler. */
 unsigned before_writes=writes,before_calls=mono_calls;uint64_t sampled=r.now_mono_ms;int64_t sampled_wall=r.now_epoch;
 mono=deadline+1;wall=INT64_MAX;fail_mono=1;vg_runtime_status_t status;
 assert(vg_runtime_status(&r,&status)==0 && status.now_mono_ms==sampled && status.now_epoch==sampled_wall);
 assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==VG_TASK_SCHEDULED);
 assert(vg_runtime_list(&r,false,1,&out)==0 && out.timer_revision==revision && out.mono_deadline_ms==deadline);
 assert(writes==before_writes && mono_calls==before_calls && !r.blocked && r.now_mono_ms==sampled);
 fail_mono=0;mono=deadline-500;
 assert(vg_runtime_tick(&r)==0 && wake==deadline); /* UTC jumps to huge ABS due. */
 assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==VG_TASK_SCHEDULED && out.mono_deadline_ms==deadline && out.timer_revision==revision);
 wall=900;mono=deadline-400;assert(vg_runtime_tick(&r)==0 && !r.rtc_valid && wake==deadline);
 rtc=false;mono=deadline-300;assert(vg_runtime_tick(&r)==0 && wake==deadline);
 rtc=true;wall=INT64_MAX;mono=deadline-200;assert(vg_runtime_tick(&r)==0 && r.rtc_valid && wake==deadline);
 assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.mono_deadline_ms==deadline && out.timer_revision==revision && out.state==VG_TASK_SCHEDULED);
 mono=deadline;assert(vg_runtime_tick(&r)==0);assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==VG_TASK_ALERTING && out.timer_revision==revision && out.mono_deadline_ms==deadline);
 /* Near uint64 exhaustion, shorten only the derived ABS recheck; a nearer
    REL still wins. At MAX a future ABS has no representable later wake. */
 reset();rtc=true;mono=UINT64_MAX-5000;huge_absolute_seed();assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0 && wake==UINT64_MAX && !r.blocked);
 q=request("max-near-relative",0);assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && wake==UINT64_MAX-4000 && r.next_epoch==0);
 mono=UINT64_MAX-4000;assert(vg_runtime_tick(&r)==0 && wake==UINT64_MAX);
 mono=UINT64_MAX;assert(vg_runtime_tick(&r)==0 && !r.blocked && !r.has_next && !armed);
 assert(vg_runtime_find(&r,"huge-absolute",&absolute,NULL)==0 && absolute.state==VG_TASK_SCHEDULED && absolute.request.due_epoch==INT64_MAX);
 assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==VG_TASK_ALERTING);
 before_writes=writes;assert(vg_runtime_tick(&r)==0 && !armed && writes==before_writes);
}
static void exact_uint64_grace(void){
 vg_task_t out;vg_create_request_t q=request("max-grace",0);
 for(unsigned representable=0;representable<2;representable++){
  reset();mono=UINT64_MAX-301000u-representable;assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
  assert(vg_runtime_create_relative(&r,&q,1,&out)==0 && out.mono_deadline_ms==UINT64_MAX-300000u-representable);
  mono=out.mono_deadline_ms;assert(vg_runtime_tick(&r)==0 && r.store.active.tasks[0].state==VG_TASK_ALERTING);
  if(representable)assert(armed && wake==UINT64_MAX);else assert(!armed && !r.has_next);
  mono=UINT64_MAX;assert(vg_runtime_tick(&r)==0 && !armed && !r.has_next);
  assert(vg_runtime_find(&r,q.request_id,&out,NULL)==0 && out.state==(representable?VG_TASK_MISSED:VG_TASK_ALERTING));
  assert(r.store.history_count==representable);
  unsigned before=writes;assert(vg_runtime_tick(&r)==0 && writes==before && r.store.history_count==representable && !armed);
  if(!representable)assert(vg_runtime_ack(&r,q.request_id,&out)==0 && out.state==VG_TASK_ACKNOWLEDGED);
 }
}

int main(void){boot_and_domains();pending_boot_recovery();recovery_faults();relative_business();relative_preflight();relative_persist_and_utc();relative_invalid_and_retry();mixed_clock_boundaries();exact_uint64_grace();puts("PASS mixed clocks C: bounded ABS recheck; near REL priority; UTC changes preserve relative identity; readonly queries; exact uint64 grace boundaries");puts("PASS relative runtime B: create/ACK/revision Snooze/Rearm; original delay; old-boot control; numeric preflight; persistence uncertain/retry; UTC-independent metadata");puts("PASS timed runtime A: boot identity/pending durable recovery; domain isolation; NEEDS_RESET; old alert retained; mono failure/rollback; ABS rollback does not starve REL; legacy API fail closed");return 0;}