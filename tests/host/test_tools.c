#include "velaguard_tools.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <direct.h>
static vg_runtime_t runtime;
static vg_commands_t commands,other;
static char disk[2][VG_STORE_MAX_BYTES],reply[VG_COMMAND_LINE+1];
static size_t lengths[2]; static int64_t now=1000; static int fail;static bool rtc=true;static uint64_t mono=1000;
static int rd(void *c,unsigned s,char *b,size_t cap,size_t *n)
{(void)c;assert(cap>=lengths[s]);*n=lengths[s];memcpy(b,disk[s],*n);return *n?0:1;}
static int wr(void *c,unsigned s,const char *b,size_t n)
{(void)c;if(fail)return -1;memcpy(disk[s],b,n);lengths[s]=n;return 0;}
static int clk(void *c,int64_t *n,bool *v){(void)c;*n=now;*v=rtc;return 0;}
static int sched(void *c,bool h,int64_t n){(void)c;(void)h;(void)n;return 0;}
static int call(const char *name,const char *input)
{return vg_tools_execute(name,input,reply,sizeof(reply));}
static void config(const char *text) { FILE *f=fopen("config/config.json","wb"); assert(f); assert(fputs(text,f)>=0); assert(fclose(f)==0); }
static void error(bool uncertain) {
 cJSON *r=cJSON_Parse(reply); assert(r);
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"type")->valuestring,"response.error"));
 cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"payload");
 assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p,"uncertain"))==uncertain);
 assert(!strstr(reply,"unknown tool")); cJSON_Delete(r);
}

static int mon(void *c,uint64_t *n){(void)c;*n=mono;return 0;}
static int sched_mono(void *c,bool has,uint64_t next){(void)c;(void)has;(void)next;return 0;}
static cJSON *get(cJSON *o,const char *name){return cJSON_GetObjectItemCaseSensitive(o,name);}
static cJSON *payload_schema(cJSON *a,int index){return get(get(get(cJSON_GetArrayItem(a,index),"input_schema"),"properties"),"payload");}
static void xor_schema(cJSON *p,const char *first,const char *second){
 cJSON *branches=get(p,"oneOf");assert(cJSON_IsArray(branches) && cJSON_GetArraySize(branches)==2);
 const char *keys[]={first,second};
 for(int i=0;i<2;i++){cJSON *required=get(cJSON_GetArrayItem(branches,i),"required");assert(cJSON_GetArraySize(required)==1 && !strcmp(cJSON_GetArrayItem(required,0)->valuestring,keys[i]));}
 /* Exactly one of these required-key branches can match; both/none reject. */
 assert(cJSON_IsFalse(get(p,"additionalProperties")));
}
static void relative_tools(const vg_runtime_deps_t *deps){
 memset(lengths,0,sizeof(lengths));fail=0;rtc=false;mono=1000;config("{}");vg_runtime_timer_ops_t timers={NULL,mon,sched_mono};
 assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);
 const char *create="{\"version\":1,\"request_id\":\"relative\",\"type\":\"task.create\",\"payload\":{\"title\":\"countdown\",\"delay_seconds\":1,\"priority\":1}}";
 assert(call("velaguard_create",create)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].timer_domain==VG_TIMER_RELATIVE);
 mono=2000;assert(vg_runtime_tick(&runtime)==0);
 const char *snooze="{\"version\":1,\"request_id\":\"relative-snooze\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"relative\",\"seconds\":30,\"expected_revision\":\"1\"}}";
 assert(call("velaguard_snooze",snooze)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].timer_revision==2);
 uint64_t deadline=runtime.store.active.tasks[0].mono_deadline_ms;mono=2100;assert(call("velaguard_snooze",snooze)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].mono_deadline_ms==deadline);
 mono=0;assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);assert(runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET);
 const char *rearm="{\"version\":1,\"request_id\":\"relative-rearm\",\"type\":\"task.rearm\",\"payload\":{\"task_id\":\"relative\",\"expected_revision\":\"2\"}}";
 assert(call("velaguard_ack",rearm)==0);error(false);assert(strstr(reply,"TOOL_TYPE_MISMATCH") && runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET);
 config("{\"tool_enabled_velaguard_rearm\":\"0\"}");assert(call("velaguard_rearm",rearm)<0 && strstr(reply,"disabled") && runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET);config("{}");
 assert(call("velaguard_rearm","{\"version\":1,\"request_id\":\"bad-rearm\",\"type\":\"task.rearm\",\"payload\":{\"task_id\":\"relative\",\"expected_revision\":\"2\",\"expected_revision\":\"2\"}}") ==0);error(false);assert(runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET);
 assert(call("velaguard_rearm",rearm)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].timer_revision==3 && runtime.store.active.tasks[0].mono_deadline_ms==1000);
 mono=400;assert(call("velaguard_rearm",rearm)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].mono_deadline_ms==1000 && runtime.store.active.tasks[0].timer_revision==3);
 vg_commands_init(&commands,&runtime,NULL,NULL);assert(call("velaguard_rearm",rearm)==0);error(false);assert(strstr(reply,"INVALID_STATE"));
 mono=0;assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);fail=1;
 assert(call("velaguard_rearm","{\"version\":1,\"request_id\":\"fail-rearm\",\"type\":\"task.rearm\",\"payload\":{\"task_id\":\"relative\",\"expected_revision\":\"3\"}}") ==0);error(true);assert(strstr(reply,"STORE_ERROR"));fail=0;
}
int main(int argc,char **argv) {
 assert(argc==2 && chdir(argv[1])==0); assert(_mkdir("config")==0); config("{}");
 vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clk,sched};
 assert(vg_runtime_start(&runtime,&deps)==0); vg_commands_init(&commands,&runtime,NULL,NULL);
 assert(vg_tools_schema()==NULL); assert(vg_tools_init(NULL)==-EINVAL);
 for(int i=0;i<8;i++) assert(vg_tools_init(&commands)==0);
 other.runtime=&runtime; assert(vg_tools_init(&other)==-EBUSY);
 char *schema=vg_tools_schema(); assert(schema); cJSON *a=cJSON_Parse(schema); free(schema);
 assert(cJSON_IsArray(a) && cJSON_GetArraySize(a)==5);
 const char *names[]={"velaguard_create","velaguard_list","velaguard_snooze","velaguard_ack","velaguard_rearm"};
 for(int i=0;i<5;i++) {
  cJSON *tool=cJSON_GetArrayItem(a,i); assert(!strcmp(cJSON_GetObjectItemCaseSensitive(tool,"name")->valuestring,names[i]));
  cJSON *s=cJSON_GetObjectItemCaseSensitive(tool,"input_schema"); assert(cJSON_IsObject(s));
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(s,"required"))==4);
 }
 cJSON *list_schema=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a,1),"input_schema");
 cJSON *list_payload=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(list_schema,"properties"),"payload");
 cJSON *required=cJSON_GetObjectItemCaseSensitive(list_payload,"required");
 assert(cJSON_GetArraySize(required)==1 && !strcmp(cJSON_GetArrayItem(required,0)->valuestring,"offset"));
 const char *bound_keys[]={"due_epoch","offset","seconds","expected_due_epoch","priority"};
 const int tool_indices[]={0,1,2,2,0};
 const double maxima[]={9007199254740991.0,VG_HISTORY_CAPACITY,VG_RUNTIME_MAX_SNOOZE_SEC,9007199254740991.0,2};
 for(int i=0;i<5;i++) {
  cJSON *sc=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a,tool_indices[i]),"input_schema");
  cJSON *pp=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(sc,"properties"),"payload"),"properties");
  cJSON *max=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(pp,bound_keys[i]),"maximum");
  assert(cJSON_IsNumber(max) && max->valuedouble==maxima[i]);
 }
 xor_schema(payload_schema(a,0),"due_epoch","delay_seconds");xor_schema(payload_schema(a,2),"expected_due_epoch","expected_revision");
 cJSON *delay=get(get(payload_schema(a,0),"properties"),"delay_seconds");assert(get(delay,"maximum")->valuedouble==86400);
 for(int i=2;i<=4;i+=2){cJSON *revision=get(get(payload_schema(a,i),"properties"),"expected_revision");assert(!strcmp(get(revision,"type")->valuestring,"string") && get(revision,"maxLength")->valuedouble==20 && !strcmp(get(revision,"pattern")->valuestring,"^[1-9][0-9]{0,19}$"));}
 cJSON *rearm_required=get(payload_schema(a,4),"required");assert(cJSON_GetArraySize(rearm_required)==2);
 cJSON_Delete(a);
 const char *create="{\"version\":1,\"request_id\":\"one\",\"type\":\"task.create\",\"payload\":{\"title\":\"喝水\",\"due_epoch\":1010,\"priority\":1}}";
 assert(call("velaguard_list",create)==0); error(false); assert(runtime.store.active.count==0);
 assert(call("velaguard_create",create)==0 && strstr(reply,"response.ok"));
 char replay[sizeof(reply)]; strcpy(replay,reply);
 assert(vg_tools_init(&commands)==0);
 assert(vg_commands_execute(&commands,create,strlen(create),reply,sizeof(reply))==0 && !strcmp(replay,reply));
 assert(call("velaguard_create",create)==0 && !strcmp(replay,reply) && runtime.store.active.count==1);
 assert(call("velaguard_list","{\"version\":1,\"request_id\":\"list\",\"type\":\"task.list\",\"payload\":{\"offset\":0,\"history\":false}}") ==0);
 assert(strstr(reply,"喝水"));
 now=1010; assert(vg_runtime_tick(&runtime)==0);
 const char *snooze="{\"version\":1,\"request_id\":\"s1\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"one\",\"seconds\":60,\"expected_due_epoch\":1010}}";
 assert(call("velaguard_snooze",snooze)==0 && strstr(reply,"response.ok")); strcpy(replay,reply);
 assert(call("velaguard_snooze",snooze)==0 && !strcmp(replay,reply));
 now=1070; assert(vg_runtime_tick(&runtime)==0);
 assert(call("velaguard_ack","{\"version\":1,\"request_id\":\"a1\",\"type\":\"task.ack\",\"payload\":{\"task_id\":\"one\"}}") ==0 && strstr(reply,"response.ok"));
 assert(runtime.store.active.count==0 && runtime.store.history_count==1);
 assert(call("velaguard_create","{")==0); error(false);
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"dup\",\"type\":\"task.create\",\"type\":\"task.create\",\"payload\":{\"title\":\"x\",\"due_epoch\":2000,\"priority\":1}}") ==0); error(false);
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"bad\",\"type\":5,\"payload\":{}}") ==0); error(false);
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"nul\",\"type\":\"task.create\\u0000bad\",\"payload\":{}}") ==0); error(false);
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"pdup\",\"type\":\"task.create\",\"payload\":{\"title\":\"x\",\"title\":\"y\",\"due_epoch\":2000,\"priority\":1}}") ==0); error(false);
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"ptype\",\"type\":\"task.create\",\"payload\":{\"title\":\"x\",\"due_epoch\":2000,\"priority\":\"1\"}}") ==0); error(false);
 char deep[80]; memset(deep,'[',30); memset(deep+30,']',30); deep[60]=0;
 assert(call("velaguard_create",deep)==0); error(false);
 char huge[VG_COMMAND_LINE+2]; memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0;
 assert(call("velaguard_create",huge)<0 && runtime.store.active.count==0);
 assert(vg_tools_execute("velaguard_create",create,reply,10)<0 && runtime.store.active.count==0);
 config("{\"tool_enabled_velaguard_create\":\"0\"}");
 assert(call("velaguard_create",create)<0 && strstr(reply,"disabled") && runtime.store.active.count==0);
 config("{}");
 fail=1;
 assert(call("velaguard_create","{\"version\":1,\"request_id\":\"fail\",\"type\":\"task.create\",\"payload\":{\"title\":\"test\",\"due_epoch\":2000,\"priority\":1}}") ==0);
 error(true); assert(strstr(reply,"STORE_ERROR"));
 assert(call("not_a_tool","{}")<0 && strstr(reply,"unknown tool"));
 assert(call(NULL,"{}")<0);
 relative_tools(&deps);
 puts("PASS real registry/provider + guard/config + commands/runtime: five schemas including REL and Rearm, once-only init, create/list/snooze/ack, replay, strict errors, uncertain store failure, disabled guard, no builtin/network init");
 return 0;
}