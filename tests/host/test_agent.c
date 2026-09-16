#include "velaguard_agent.h"
#include "velaguard_tools.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <direct.h>
static vg_runtime_t runtime; static vg_commands_t commands,other;
static char disk[2][VG_STORE_MAX_BYTES],reply[VG_COMMAND_LINE+1];
static size_t sizes[2]; static int64_t now=1000; static int fail;static bool rtc=true;static uint64_t mono=1000; static unsigned emissions;
static int rd(void *c,unsigned s,char *b,size_t cap,size_t *n){(void)c;assert(cap>=sizes[s]);*n=sizes[s];memcpy(b,disk[s],*n);return *n?0:1;}
static int wr(void *c,unsigned s,const char *b,size_t n){(void)c;if(fail)return -1;memcpy(disk[s],b,n);sizes[s]=n;return 0;}
static int clk(void *c,int64_t *n,bool *v){(void)c;*n=now;*v=rtc;return 0;}
static int sched(void *c,bool h,int64_t n){(void)c;(void)h;(void)n;return 0;}
static void emit(void *c,const char *line){(void)c;assert(strlen(line)<sizeof(reply));strcpy(reply,line);emissions++;}
static void cfg(const char *s){FILE *f=fopen("config/config.json","wb");assert(f);assert(fputs(s,f)>=0);assert(fclose(f)==0);}
static int call(const char *s){return vg_agent_line_execute(&commands,s,strlen(s),reply,sizeof(reply));}
static void error(const char *id,bool uncertain){cJSON *r=cJSON_Parse(reply);assert(r);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"request_id")->valuestring,id));assert(!strcmp(cJSON_GetObjectItemCaseSensitive(r,"type")->valuestring,"response.error"));cJSON *p=cJSON_GetObjectItemCaseSensitive(r,"payload");assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(p,"uncertain"))==uncertain);cJSON_Delete(r);}
static int allocation_fail_once;
static void *test_allocate(size_t n){if(allocation_fail_once){allocation_fail_once=0;return NULL;}return malloc(n);}

static int mon(void *c,uint64_t *n){(void)c;*n=mono;return 0;}
static int sched_mono(void *c,bool has,uint64_t next){(void)c;(void)has;(void)next;return 0;}
static void relative_agent(const vg_runtime_deps_t *deps){
 memset(sizes,0,sizeof(sizes));fail=0;rtc=false;mono=1000;cfg("{}");vg_runtime_timer_ops_t timers={NULL,mon,sched_mono};
 assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);commands.line_execute=vg_agent_line_execute;
 const char *create="{ \"version\":1, \"request_id\":\"agent-relative\", \"type\":\"task.create\", \"payload\":{\"title\":\"timer\",\"delay_seconds\":1,\"priority\":1}}";
 uint32_t calls=vg_agent_tool_calls();assert(call(create)==0 && strstr(reply,"response.ok") && vg_agent_tool_calls()==calls+1);
 mono=2000;assert(vg_runtime_tick(&runtime)==0);
 const char *snooze="{\"version\":1,\"request_id\":\"relative-snooze\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"agent-relative\",\"seconds\":30,\"expected_revision\":\"1\"}}";
 assert(call(snooze)==0 && runtime.store.active.tasks[0].timer_revision==2);
 mono=0;assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);commands.line_execute=vg_agent_line_execute;
 const char *rearm="{\"version\":1,\"request_id\":\"agent-rearm\",\"type\":\"task.rearm\",\"payload\":{\"task_id\":\"agent-relative\",\"expected_revision\":\"2\"}}";
 assert(runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET);cfg("{\"tool_enabled_velaguard_rearm\":\"0\"}");calls=vg_agent_tool_calls();
 /* RED: old generic fallback executes this valid Rearm despite disabled guard. */
 int guard_rc=call(rearm);fprintf(stderr,"Rearm disabled guard: rc=%d state=%d revision=%llu registry_delta=%u\n",guard_rc,(int)runtime.store.active.tasks[0].state,(unsigned long long)runtime.store.active.tasks[0].timer_revision,(unsigned)(vg_agent_tool_calls()-calls));
 assert(guard_rc<0);error("agent-rearm",false);assert(strstr(reply,"AGENT_TOOL_REJECTED") && vg_agent_tool_calls()==calls+1 && runtime.store.active.tasks[0].state==VG_TASK_NEEDS_RESET && runtime.store.active.tasks[0].timer_revision==2);
 cfg("{}");vg_line_reader_t reader={0};unsigned before=emissions;calls=vg_agent_tool_calls();
 vg_line_feed(&reader,&commands,rearm,13,emit,NULL);assert(emissions==before);
 vg_line_feed(&reader,&commands,rearm+13,strlen(rearm)-13,emit,NULL);vg_line_feed(&reader,&commands,"\r\n",2,emit,NULL);
 assert(emissions==before+1 && vg_agent_tool_calls()==calls+1 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].timer_revision==3 && runtime.store.active.tasks[0].mono_deadline_ms==1000);
 mono=400;assert(call(rearm)==0 && strstr(reply,"response.ok") && runtime.store.active.tasks[0].mono_deadline_ms==1000 && runtime.store.active.tasks[0].timer_revision==3);
 cfg("{\"tool_enabled_velaguard_rearm\":\"0\"}");assert(call(rearm)<0);error("agent-rearm",false);assert(runtime.store.active.tasks[0].timer_revision==3);cfg("{}");
 vg_commands_init(&commands,&runtime,NULL,NULL);assert(call(rearm)==0);error("agent-rearm",false);assert(strstr(reply,"INVALID_STATE"));
 mono=0;assert(vg_runtime_start_with_timers(&runtime,deps,&timers)==0);vg_commands_init(&commands,&runtime,NULL,NULL);fail=1;
 assert(call("{\"version\":1,\"request_id\":\"rearm-failure\",\"type\":\"task.rearm\",\"payload\":{\"task_id\":\"agent-relative\",\"expected_revision\":\"3\"}}") ==0);error("rearm-failure",true);assert(strstr(reply,"STORE_ERROR"));fail=0;
}
int main(int argc,char **argv){
 assert(argc==2 && chdir(argv[1])==0);assert(_mkdir("config")==0);cfg("{}");
 vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clk,sched};assert(vg_runtime_start(&runtime,&deps)==0);vg_commands_init(&commands,&runtime,NULL,NULL);
 const char *create="{\"version\":1,\"request_id\":\"agent-one\",\"type\":\"task.create\",\"payload\":{\"title\":\"喝水\",\"due_epoch\":1010,\"priority\":1}}";
 assert(call(create)<0);error("agent-one",false);assert(runtime.store.active.count==0);
 assert(vg_agent_init(&commands)==0);assert(vg_agent_init(&commands)==0);
 vg_commands_init(&other,&runtime,NULL,NULL);assert(vg_agent_init(&other)<0);
 commands.line_execute=vg_agent_line_execute;vg_line_reader_t reader={0};
 vg_line_feed(&reader,&commands,create,17,emit,NULL);assert(emissions==0 && vg_agent_tool_calls()==0);
 vg_line_feed(&reader,&commands,create+17,strlen(create)-17,emit,NULL);assert(emissions==0);
 vg_line_feed(&reader,&commands,"\r\n",2,emit,NULL);assert(emissions==1 && vg_agent_tool_calls()==1 && strstr(reply,"response.ok"));
 char saved[sizeof(reply)];strcpy(saved,reply);assert(call(create)==0 && !strcmp(reply,saved) && runtime.store.active.count==1);
 assert(call("{\"version\":1,\"request_id\":\"list\",\"type\":\"task.list\",\"payload\":{\"offset\":0}}") ==0 && strstr(reply,"喝水"));
 now=1010;assert(vg_runtime_tick(&runtime)==0);
 assert(call("{\"version\":1,\"request_id\":\"snooze\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"agent-one\",\"seconds\":60,\"expected_due_epoch\":1010}}") ==0 && strstr(reply,"response.ok"));
 now=1070;assert(vg_runtime_tick(&runtime)==0);
 assert(call("{\"version\":1,\"request_id\":\"ack\",\"type\":\"task.ack\",\"payload\":{\"task_id\":\"agent-one\"}}") ==0 && strstr(reply,"response.ok"));
 assert(vg_agent_tool_calls()==5 && runtime.store.history_count==1);
 assert(call("{\"version\":1,\"request_id\":\"status\",\"type\":\"device.status\",\"payload\":{}}") ==0);
 assert(call("{\"version\":1,\"request_id\":\"history\",\"type\":\"event.sync\",\"payload\":{\"offset\":0}}") ==0 && strstr(reply,"ACKNOWLEDGED"));
 assert(vg_agent_tool_calls()==5);
 cfg("{\"tool_enabled_velaguard_create\":\"0\"}");
 const char *blocked="{\"version\":1,\"request_id\":\"blocked\",\"type\":\"task.create\",\"payload\":{\"title\":\"must not create\",\"due_epoch\":2000,\"priority\":1}}";
 assert(call(blocked)<0);error("blocked",false);assert(runtime.store.active.count==0 && vg_agent_tool_calls()==6);
 /* 第一次解析 OOM 后禁止回退再次解析并绕过禁用 guard。 */
 cJSON_Hooks hooks={test_allocate,free}; cJSON_InitHooks(&hooks); allocation_fail_once=1;
 assert(call(blocked)<0); cJSON_InitHooks(NULL); error("",false);
 assert(runtime.store.active.count==0 && vg_agent_tool_calls()==6);cfg("{}");
 /* 有效前缀+嵌入 NUL+垃圾不能经 strlen 截断后创建。 */
 char binary[VG_COMMAND_LINE+1];size_t len=strlen(blocked);memcpy(binary,blocked,len);binary[len]=0;memcpy(binary+len+1,"junk",4);
 assert(vg_agent_line_execute(&commands,binary,len+5,reply,sizeof(reply))<0);error("",false);assert(runtime.store.active.count==0 && vg_agent_tool_calls()==6);
 vg_line_feed(&reader,&commands,binary,len+5,emit,NULL);vg_line_feed(&reader,&commands,"\n",1,emit,NULL);error("",false);assert(runtime.store.active.count==0 && vg_agent_tool_calls()==6);
 assert(call("{\"version\":1,\"request_id\":\"dup\",\"type\":\"task.create\",\"type\":\"task.ack\",\"payload\":{}}") ==0);error("",false);
 assert(call("{\"version\":01,\"request_id\":\"lex\",\"type\":\"task.create\",\"payload\":{}}") ==0);error("",false);
 char deep[100];memset(deep,'[',40);memset(deep+40,']',40);deep[80]=0;assert(call(deep)<0);error("",false);
 assert(vg_agent_line_execute(&commands,blocked,strlen(blocked),reply,10)<0 && runtime.store.active.count==0);
 fail=1;assert(call(blocked)==0);error("blocked",true);assert(strstr(reply,"STORE_ERROR"));fail=0;assert(vg_runtime_reload(&runtime)==0);
 /* main 不装 hook 时保持原始 UART 命令路径。 */
 commands.line_execute=NULL;uint32_t calls=vg_agent_tool_calls();
 vg_line_feed(&reader,&commands,blocked,strlen(blocked),emit,NULL);vg_line_feed(&reader,&commands,"\n",1,emit,NULL);
 assert(strstr(reply,"response.ok") && runtime.store.active.count==1 && vg_agent_tool_calls()==calls);
 relative_agent(&deps);
 puts("PASS agent line adapter: real registry observed, fragmented UART, five task routes with guarded Rearm, device/event fallback, guard no-bypass, embedded-NUL/depth/size, original strict parsing, uncertain persistence, plain UART fallback");return 0;
}