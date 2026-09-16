#include "velaguard_commands.h"
#include "cJSON.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static vg_runtime_t runtime;
static vg_commands_t commands;
static char disk[2][VG_STORE_MAX_BYTES],line[VG_COMMAND_LINE+1];
static struct {unsigned char before[8];char text[VG_COMMAND_LINE+1];unsigned char after[8];} reply;
static size_t lengths[2];static int64_t wall=1000;static uint64_t mono=1000;
static bool valid;static unsigned writes,scheduled,clock_reads,sets;static int fail_write,fail_mono;
static cJSON *parsed;
static int rd(void *c,unsigned i,char *b,size_t cap,size_t *n){(void)c;assert(cap>=lengths[i]);*n=lengths[i];memcpy(b,disk[i],*n);return *n?0:1;}
static int wr(void *c,unsigned i,const char *b,size_t n){(void)c;writes++;if(fail_write!=1){memcpy(disk[i],b,n);lengths[i]=n;}return fail_write?-1:0;}
static int clk(void *c,int64_t *n,bool *v){(void)c;clock_reads++;*n=wall;*v=valid;return 0;}
static int set(void *c,int64_t n){(void)c;sets++;wall=n;valid=true;return 0;}
static int mon(void *c,uint64_t *n){(void)c;clock_reads++;if(fail_mono)return -1;*n=mono;return 0;}
static int sched(void *c,bool has,uint64_t next){(void)c;(void)has;(void)next;scheduled++;return 0;}
static vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clk,NULL};
static vg_runtime_timer_ops_t timers={NULL,mon,sched};
static void boot(void){assert(vg_runtime_start_with_timers(&runtime,&deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,set);}
static void reset(void){memset(lengths,0,sizeof(lengths));writes=scheduled=clock_reads=sets=0;fail_write=fail_mono=0;mono=1000;wall=1000;valid=false;boot();}
static int call(const char *id,const char *type,const char *payload){
 int n=snprintf(line,sizeof(line),"{\"version\":1,\"request_id\":\"%s\",\"type\":\"%s\",\"payload\":%s}",id,type,payload);assert(n>0 && n<=VG_COMMAND_LINE);
 memset(&reply,0xa5,sizeof(reply));int rc=vg_commands_execute(&commands,line,(size_t)n,reply.text,sizeof(reply.text));
 for(unsigned i=0;i<8;i++)assert(reply.before[i]==0xa5 && reply.after[i]==0xa5);
 assert(memchr(reply.text,0,sizeof(reply.text)) && strlen(reply.text)<=VG_COMMAND_LINE);
 cJSON_Delete(parsed);parsed=cJSON_Parse(reply.text);assert(parsed);return rc;
}
static const cJSON *payload(void){return cJSON_GetObjectItemCaseSensitive(parsed,"payload");}
static const cJSON *item(const cJSON *o,const char *name){return cJSON_GetObjectItemCaseSensitive(o,name);}
static void string_is(const cJSON *o,const char *name,const char *expected){const cJSON *v=item(o,name);assert(cJSON_IsString(v) && !strcmp(v->valuestring,expected));}
static const char create[]="{\"title\":\"喝水提醒\",\"delay_seconds\":1,\"priority\":1}";
static void lifecycle(void){
 reset();assert(call("rel","task.create",create)==0 && !runtime.rtc_valid);unsigned before=writes;uint64_t deadline=runtime.store.active.tasks[0].mono_deadline_ms;
 assert(call("rel","task.create",create)==0 && writes==before && runtime.store.active.tasks[0].mono_deadline_ms==deadline);
 assert(call("rel","task.create","{\"title\":\"changed\",\"delay_seconds\":1,\"priority\":1}")==VG_ERR_DUPLICATE_REQUEST);
 assert(call("list","task.list","{\"offset\":0}")==0);const cJSON *t=item(payload(),"task");string_is(t,"timer_domain","REL");string_is(t,"timer_revision","1");string_is(t,"timer_boot_id","1");string_is(t,"mono_deadline_ms","2000");assert(item(t,"due_epoch")->valuedouble==0);
 unsigned reads=clock_reads,calls=scheduled;before=writes;mono=2000;
 assert(call("status","device.status","{}")==0 && writes==before && clock_reads==reads && scheduled==calls);assert(cJSON_IsTrue(item(payload(),"timed_mode")));string_is(payload(),"now_mono_ms","1000");
 assert(call("list","task.list","{\"offset\":0}")==0 && runtime.store.active.tasks[0].state==VG_TASK_SCHEDULED && writes==before && clock_reads==reads && scheduled==calls);
 assert(vg_runtime_tick(&runtime)==0);
 const char *snooze="{\"task_id\":\"rel\",\"seconds\":2,\"expected_revision\":\"1\"}";
 assert(call("s1","task.snooze",snooze)==0 && runtime.store.active.tasks[0].timer_revision==2);before=writes;
 assert(call("s1","task.snooze",snooze)==0 && writes==before);assert(call("s2","task.snooze",snooze)==VG_ERR_INVALID_STATE);
 mono=0;boot();assert(call("nr","task.list","{\"offset\":0}")==0);string_is(item(payload(),"task"),"state","NEEDS_RESET");
 assert(call("a","task.ack","{\"task_id\":\"rel\"}")==VG_ERR_INVALID_STATE);
 assert(call("s3","task.snooze",snooze)==VG_ERR_INVALID_STATE);
 assert(call("r0","task.rearm","{\"task_id\":\"rel\",\"expected_revision\":\"1\"}")==VG_ERR_INVALID_STATE);
 const char *rearm="{\"task_id\":\"rel\",\"expected_revision\":\"2\"}";
 assert(call("r1","task.rearm",rearm)==0 && runtime.store.active.tasks[0].mono_deadline_ms==1000 && runtime.store.active.tasks[0].timer_revision==3);
 before=writes;assert(call("r1","task.rearm",rearm)==0 && writes==before);
 assert(call("r1","task.rearm","{\"task_id\":\"rel\",\"expected_revision\":\"3\"}")==VG_ERR_DUPLICATE_REQUEST && writes==before);
 vg_commands_init(&commands,&runtime,NULL,set);assert(call("r1","task.rearm",rearm)==VG_ERR_INVALID_STATE);
 mono=1000;assert(vg_runtime_tick(&runtime)==0);assert(call("a1","task.ack","{\"task_id\":\"rel\"}")==0);
 assert(call("history","event.sync","{\"offset\":0}")==0);string_is(item(payload(),"task"),"state","ACKNOWLEDGED");string_is(item(payload(),"task"),"timer_revision","3");
}
static void invalid_shapes(void){
 reset();const char *bad_create[]={"{\"title\":\"x\",\"priority\":1}","{\"title\":\"x\",\"priority\":1,\"due_epoch\":1002,\"delay_seconds\":1}","{\"title\":\"x\",\"priority\":1,\"due_epoch\":null,\"delay_seconds\":1}","{\"title\":\"x\",\"priority\":1,\"delay_seconds\":0}","{\"title\":\"x\",\"priority\":1,\"delay_seconds\":86401}","{\"title\":\"x\",\"priority\":1,\"delay_seconds\":true}","{\"title\":\"x\",\"priority\":1,\"delay_seconds\":1.0}","{\"title\":\"x\",\"priority\":1,\"delay_seconds\":1,\"delay_seconds\":1}"};
 unsigned before=writes;for(size_t i=0;i<sizeof(bad_create)/sizeof(bad_create[0]);i++)assert(call("bad","task.create",bad_create[i])!=0 && writes==before);
 assert(call("rel","task.create",create)==0);mono=2000;assert(vg_runtime_tick(&runtime)==0);before=writes;
 const char *bad_revision[]={"1","1.0","true","null","{}","[]","\"\"","\"0\"","\"01\"","\"+1\"","\"-1\"","\" 1\"","\"1 \"","\"1e2\"","\"18446744073709551616\"","\"1\\u0000x\""};
 char p[400];for(size_t i=0;i<sizeof(bad_revision)/sizeof(bad_revision[0]);i++){snprintf(p,sizeof(p),"{\"task_id\":\"rel\",\"seconds\":1,\"expected_revision\":%s}",bad_revision[i]);assert(call("bad","task.snooze",p)!=0 && writes==before);}
 assert(call("bad","task.snooze","{\"task_id\":\"rel\",\"seconds\":1,\"expected_due_epoch\":1,\"expected_revision\":\"1\"}")!=0 && writes==before);
 assert(call("bad","task.snooze","{\"task_id\":\"rel\",\"seconds\":1,\"expected_due_epoch\":1}")==VG_ERR_INVALID_STATE && writes==before);
 assert(call("bad","task.rearm","{\"task_id\":\"rel\",\"expected_revision\":\"1\",\"expected_revision\":\"1\"}")!=0 && writes==before);
 assert(call("bad","task.rearm","{\"task_id\":\"rel\",\"expected_revision\":\"1\",\"seconds\":5}")!=0 && writes==before);
 runtime.store.active.tasks[0].timer_revision=UINT64_MAX;runtime.store.dirty=true;assert(vg_store_save(&runtime.store,&deps.io)==0);
 assert(call("max","task.snooze","{\"task_id\":\"rel\",\"seconds\":1,\"expected_revision\":\"18446744073709551615\"}")==VG_ERR_CAPACITY);
 /* Revision above 2^53 remains exact and usable rather than double-rounded. */
 runtime.store.active.tasks[0].timer_revision=9007199254740993ULL;runtime.store.dirty=true;assert(vg_store_save(&runtime.store,&deps.io)==0);
 assert(call("wide","task.snooze","{\"task_id\":\"rel\",\"seconds\":1,\"expected_revision\":\"9007199254740993\"}")==0 && runtime.store.active.tasks[0].timer_revision==9007199254740994ULL);
 valid=true;assert(call("abs","task.create","{\"title\":\"x\",\"priority\":1,\"due_epoch\":1001}")==0);wall=1001;assert(vg_runtime_tick(&runtime)==0);
 assert(call("wrong","task.snooze","{\"task_id\":\"abs\",\"seconds\":1,\"expected_revision\":\"1\"}")==VG_ERR_INVALID_STATE);
}
static void uncertain(void){
 reset();fail_write=2;assert(call("rel","task.create",create)==VG_STORE_IO_ERROR && cJSON_IsTrue(item(payload(),"uncertain")));fail_write=0;assert(vg_runtime_reload(&runtime)==0);
 unsigned before=writes;assert(call("rel","task.create",create)==0 && writes==before);mono=2000;assert(vg_runtime_tick(&runtime)==0);
 fail_write=2;const char *s="{\"task_id\":\"rel\",\"seconds\":1,\"expected_revision\":\"1\"}";
 assert(call("s","task.snooze",s)==VG_STORE_IO_ERROR && cJSON_IsTrue(item(payload(),"uncertain")));fail_write=0;assert(vg_runtime_reload(&runtime)==0);assert(call("s","task.snooze",s)==VG_ERR_INVALID_STATE);
 mono=0;boot();fail_write=2;
 const char *rearm="{\"task_id\":\"rel\",\"expected_revision\":\"2\"}";
 assert(call("rearm-uncertain","task.rearm",rearm)==VG_STORE_IO_ERROR && cJSON_IsTrue(item(payload(),"uncertain")));fail_write=0;
 assert(vg_runtime_reload(&runtime)==0);before=writes;assert(call("rearm-uncertain","task.rearm",rearm)==VG_ERR_INVALID_STATE && writes==before);
 fail_mono=1;assert(call("time","device.time","{\"epoch\":2000}")==VG_ERR_INVALID_TIME && sets==1 && wall==2000 && cJSON_IsTrue(item(payload(),"uncertain")));fail_mono=0;
 assert(vg_runtime_reload(&runtime)==0);runtime.store.boot_counter++;runtime.store.dirty=true;assert(vg_store_save(&runtime.store,&deps.io)==0);
 assert(call("time-conflict","device.time","{\"epoch\":3000}")==VG_RUNTIME_BOOT_CONFLICT && cJSON_IsTrue(item(payload(),"uncertain")));string_is(payload(),"code","BOOT_CONFLICT");
}
static void longest_utf8(void){
 reset();char title[VG_TITLE_CAPACITY];for(unsigned i=0;i<64;i++)memcpy(title+3*i,"醒",3);title[192]=0;
 cJSON *p=cJSON_CreateObject();assert(p);assert(cJSON_AddStringToObject(p,"title",title));assert(cJSON_AddNumberToObject(p,"delay_seconds",1));assert(cJSON_AddNumberToObject(p,"priority",1));char *json=cJSON_PrintUnformatted(p);assert(json);
 assert(call("utf8","task.create",json)==0);cJSON_free(json);cJSON_Delete(p);
 assert(call("utf8-list","task.list","{\"offset\":0}")==0);string_is(item(payload(),"task"),"title",title);
}
static void diag(void *c,vg_device_diagnostics_t *d){(void)c;memset(d,0,sizeof(*d));d->storage_errno=d->app_error=d->ui_error=INT_MIN;d->heap_free=SIZE_MAX;}
static void longest(void){
 reset();runtime.store.boot_counter=UINT64_MAX;runtime.active_boot_id=UINT64_MAX;runtime.store.dirty=true;assert(vg_store_save(&runtime.store,&deps.io)==0);
 vg_create_request_t q={0};memset(q.request_id,'A',64);for(unsigned i=0;i<VG_TITLE_MAX_BYTES;i++)q.title[i]=(i%2)?'\\':'"';q.priority=2;
 mono=UINT64_MAX-86400000u;valid=true;wall=INT64_MAX;vg_task_t out;
 assert(vg_runtime_create_relative(&runtime,&q,86400,&out)==0);runtime.store.active.tasks[0].timer_revision=UINT64_MAX;runtime.store.active.tasks[0].snooze_count=UINT32_MAX;runtime.store.dirty=true;
 mono=UINT64_MAX;assert(vg_runtime_tick(&runtime)==0);assert(vg_runtime_ack(&runtime,q.request_id,&out)==0);
 assert(call(q.request_id,"event.sync","{\"offset\":0}")==0);size_t task_bytes=strlen(reply.text);assert(task_bytes<=VG_COMMAND_LINE);const cJSON *t=item(payload(),"task");string_is(t,"title",q.title);string_is(t,"mono_deadline_ms","18446744073709551615");string_is(t,"timer_boot_id","18446744073709551615");string_is(t,"timer_revision","18446744073709551615");
 /* Construct a valid full Store: 31 prior-boot alerts and one huge ABS.
    The real Runtime then derives next_epoch=INT64_MAX / next_mono=UINT64_MAX. */
 vg_task_t maximum=out;reset();runtime.store.boot_counter=UINT64_MAX;runtime.active_boot_id=UINT64_MAX;valid=true;wall=INT64_MAX-1;mono=UINT64_MAX-1000u;
 runtime.store.history_count=VG_HISTORY_CAPACITY;runtime.store.history_start=0;
 for(unsigned i=0;i<VG_HISTORY_CAPACITY;i++){runtime.store.history[i]=maximum;snprintf(runtime.store.history[i].request.request_id,VG_REQUEST_ID_CAPACITY,"h%03u",i);runtime.store.history[i].timer_boot_id=UINT64_MAX-1;}
 runtime.store.active.count=VG_MAX_ACTIVE_TASKS-1;
 for(unsigned i=0;i<VG_MAX_ACTIVE_TASKS-1;i++){runtime.store.active.tasks[i]=maximum;vg_task_t *a=&runtime.store.active.tasks[i];snprintf(a->request.request_id,VG_REQUEST_ID_CAPACITY,"a%03u",i);a->state=VG_TASK_ALERTING;a->acknowledged_epoch=0;a->timer_boot_id=UINT64_MAX-1;}
 vg_create_request_t absolute={0};strcpy(absolute.request_id,"huge");strcpy(absolute.title,"absolute");absolute.priority=2;absolute.due_epoch=INT64_MAX;
 assert(vg_store_create(&runtime.store,&absolute,wall,true,NULL)==0);assert(vg_store_save(&runtime.store,&deps.io)==0);assert(vg_runtime_reload(&runtime)==0);
 assert(runtime.next_epoch==INT64_MAX && runtime.next_mono_ms==UINT64_MAX);
 commands.get_diagnostics=diag;assert(call(q.request_id,"device.status","{}")==0);size_t status_bytes=strlen(reply.text);assert(status_bytes<=VG_COMMAND_LINE);
 assert(item(payload(),"active_count")->valuedouble==32 && item(payload(),"history_count")->valuedouble==128 && item(payload(),"alerting_count")->valuedouble==31);
 printf("Longest real cJSON responses: escaped task=%u bytes, diagnostic timer status=%u bytes (limit=%u)\n",(unsigned)task_bytes,(unsigned)status_bytes,VG_COMMAND_LINE);
 /* Insufficient response storage must fail before an otherwise valid mutation. */
 reset();assert(call("probe","device.status","{}")==0);snprintf(line,sizeof(line),"{\"version\":1,\"request_id\":\"small\",\"type\":\"task.create\",\"payload\":%s}",create);unsigned before=writes;
 assert(vg_commands_execute(&commands,line,strlen(line),reply.text,VG_COMMAND_LINE)==VG_ERR_INVALID_MESSAGE && writes==before && runtime.store.active.count==0);
}
int main(void){lifecycle();invalid_shapes();uncertain();longest_utf8();longest();cJSON_Delete(parsed);puts("PASS timer commands: domain XOR, strict uint64 revision, no-RTC lifecycle/rearm, replay, readonly status, uncertain effects, bounded real cJSON");return 0;}
