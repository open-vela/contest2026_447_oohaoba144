#include "velaguard_commands.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static vg_runtime_t runtime;
static vg_commands_t commands;
static char disk[2][VG_STORE_MAX_BYTES], reply[VG_COMMAND_LINE+1];
static size_t lengths[2]; static int64_t now=1000; static bool valid=true;
static int fail, clock_fault;
static int rd(void *c,unsigned s,char *b,size_t cap,size_t *n)
{(void)c;assert(cap>=lengths[s]);*n=lengths[s];memcpy(b,disk[s],*n);return *n?0:1;}
static int wr(void *c,unsigned s,const char *b,size_t n)
{(void)c;if(fail)return -1;memcpy(disk[s],b,n);lengths[s]=n;return 0;}
static int clk(void *c,int64_t *n,bool *v){(void)c;*n=now;*v=valid;return 0;}
static int set(void *c,int64_t n){(void)c;now=n;valid=!clock_fault;return clock_fault?-1:0;}
static int sched(void *c,bool h,int64_t n){(void)c;(void)h;(void)n;return 0;}
static void diagnostics(void *context,vg_device_diagnostics_t *out)
{
  (void)context;memset(out,0,sizeof(*out));out->storage_errno=5;
  out->lcd_exists=true;out->heap_free=4000000;
}
static int call(const char *line)
{return vg_commands_execute(&commands,line,strlen(line),reply,sizeof(reply));}
static void emit(void *c,const char *s){(void)c;strcpy(reply,s);}
static unsigned dispatch_calls;
static int dispatch(vg_commands_t *c,const char *line,size_t n,char *out,size_t cap)
{
  dispatch_calls++;
  assert(line[n]==0);
  return vg_commands_execute(c,line,n,out,cap);
}
static int empty_dispatch(vg_commands_t *c,const char *line,size_t n,char *out,size_t cap)
{(void)c;(void)line;(void)n;(void)out;(void)cap;return -999;}
static const char explicit_error[]="{\"version\":1,\"request_id\":\"kept\",\"type\":\"response.error\",\"payload\":{\"code\":\"STORE_ERROR\",\"uncertain\":true}}";
static int error_dispatch(vg_commands_t *c,const char *line,size_t n,char *out,size_t cap)
{(void)c;(void)line;(void)n;assert(cap>strlen(explicit_error));strcpy(out,explicit_error);return -999;}
int main(void)
{
  vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clk,sched}; vg_task_t t;
  vg_line_reader_t reader={0}; char before[VG_COMMAND_LINE+1]; size_t i;
  assert(vg_runtime_start(&runtime,&deps)==0);
  vg_commands_init(&commands,&runtime,NULL,set);
  assert(call("{")==VG_ERR_INVALID_JSON);
  assert(call("{\"version\":1,\"version\":1,\"request_id\":\"x\",\"type\":\"device.status\",\"payload\":{}}")==VG_ERR_INVALID_MESSAGE);
  assert(call("{\"version\":2,\"request_id\":\"x\",\"type\":\"device.status\",\"payload\":{}}")==VG_ERR_UNSUPPORTED_VERSION);
  assert(call("{\"version\":1,\"request_id\":\"bad\",\"type\":\"task.create\",\"payload\":{\"title\":\"a\\u0000b\",\"due_epoch\":1010,\"priority\":1}}")==VG_ERR_INVALID_MESSAGE);
  assert(call("{\"version\":1,\"request_id\":\"bad\",\"type\":\"task.create\",\"payload\":{\"title\":\"a\",\"due_epoch\":1010,\"priority\":1,\"extra\":1}}")==VG_ERR_INVALID_MESSAGE);
  assert(call("{\"version\":1,\"request_id\":\"one\",\"type\":\"task.create\",\"payload\":{\"title\":\"喝水\",\"due_epoch\":1010,\"priority\":1}}")==0);
  strcpy(before,reply);
  assert(call("{\"version\":1,\"request_id\":\"one\",\"type\":\"task.create\",\"payload\":{\"title\":\"喝水\",\"due_epoch\":1010,\"priority\":1}}")==0);
  assert(strcmp(before,reply)==0 && runtime.store.active.count==1);
  now=1010;assert(vg_runtime_tick(&runtime)==0);
  assert(call("{\"version\":1,\"request_id\":\"s1\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"one\",\"seconds\":60,\"expected_due_epoch\":1010}}")==0);
  strcpy(before,reply);
  assert(call("{\"version\":1,\"request_id\":\"s1\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"one\",\"seconds\":60,\"expected_due_epoch\":1010}}")==0);
  assert(strcmp(before,reply)==0);
  assert(call("{\"version\":1,\"request_id\":\"s1\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"one\",\"seconds\":120,\"expected_due_epoch\":1010}}")==VG_ERR_DUPLICATE_REQUEST);
  now=1070;assert(vg_runtime_tick(&runtime)==0);
  vg_commands_init(&commands,&runtime,NULL,set); /* simulate cache loss */
  assert(call("{\"version\":1,\"request_id\":\"s1\",\"type\":\"task.snooze\",\"payload\":{\"task_id\":\"one\",\"seconds\":60,\"expected_due_epoch\":1010}}")==VG_ERR_INVALID_STATE);
  assert(call("{\"version\":1,\"request_id\":\"a1\",\"type\":\"task.ack\",\"payload\":{\"task_id\":\"one\"}}")==0);
  assert(call("{\"version\":1,\"request_id\":\"list\",\"type\":\"event.sync\",\"payload\":{\"offset\":0}}")==0);
  assert(strstr(reply,"ACKNOWLEDGED") && strstr(reply,"喝水"));
  assert(vg_runtime_reload(&runtime)==0);
  assert(vg_runtime_find(&runtime,"one",&t,NULL)==0 && t.snooze_count==1);
  fail=1;
  assert(call("{\"version\":1,\"request_id\":\"fail\",\"type\":\"task.create\",\"payload\":{\"title\":\"test\",\"due_epoch\":2000,\"priority\":1}}")==VG_STORE_IO_ERROR);
  assert(strstr(reply,"response.error") && strstr(reply,"uncertain"));
  fail=0;
  assert(call("{\"version\":1,\"request_id\":\"reload\",\"type\":\"device.reload\",\"payload\":{}}")==0);
  assert(call("{\"version\":1,\"request_id\":\"find\",\"type\":\"task.list\",\"payload\":{\"offset\":0,\"history\":false}}")==0);
  assert(strstr(reply,"null"));
  reply[0]=0;
  vg_line_feed(&reader,&commands,"{\"version\":1,",13,emit,NULL);
  assert(!reply[0]);
  {const char *tail="\"request_id\":\"status\",\"type\":\"device.status\",\"payload\":{}}\r\n";
   vg_line_feed(&reader,&commands,tail,strlen(tail),emit,NULL);}
  assert(strstr(reply,"response.ok"));
  for(i=0;i<VG_COMMAND_LINE+20;i++) vg_line_feed(&reader,&commands,"x",1,emit,NULL);
  vg_line_feed(&reader,&commands,"\n",1,emit,NULL);
  assert(strstr(reply,"LINE_TOO_LONG"));
  assert(call("{\"version\":1,\"request_id\":\"late\",\"type\":\"task.create\",\"payload\":{\"title\":\"future\",\"due_epoch\":2147483648,\"priority\":1}}")==0);
  assert(call("{\"version\":1,\"request_id\":\"bad\",\"type\":\"task.create\",\"payload\":{\"title\":\"x\",\"due_epoch\":9007199254740992,\"priority\":1}}")!=0);
  assert(call("{\"version\":01,\"request_id\":\"lex\",\"type\":\"device.status\",\"payload\":{}}")==VG_ERR_INVALID_MESSAGE);
  assert(call("{\"version\":1,\"request_id\":\"lex\",\"type\":\"task.create\",\"payload\":{\"title\":\"x\",\"due_epoch\":1010.,\"priority\":1}}")==VG_ERR_INVALID_MESSAGE);
  strcpy(before,"{\"version\":1,\"request_id\":\"crlf\",\"type\":\"device.status\",\"payload\":{}}");
  i=strlen(before);memset(before+i,' ',VG_COMMAND_LINE-i);
  vg_line_feed(&reader,&commands,before,VG_COMMAND_LINE,emit,NULL);
  vg_line_feed(&reader,&commands,"\r\n",2,emit,NULL);
  assert(strstr(reply,"response.ok"));
  clock_fault=1;
  assert(call("{\"version\":1,\"request_id\":\"clock-fail\",\"type\":\"device.time\",\"payload\":{\"epoch\":1080}}")==VG_ERR_INVALID_TIME);
  assert(!runtime.rtc_valid && strstr(reply,"\"uncertain\":true"));
  commands.get_diagnostics=diagnostics;
  assert(call("{\"version\":1,\"request_id\":\"diag\",\"type\":\"device.status\",\"payload\":{}}")==0);
  assert(strstr(reply,"\"storage_errno\":5") && strstr(reply,"\"lcd_exists\":true"));
  commands.line_execute=dispatch;
  reply[0]=0;
  {const char *line="{\"version\":1,\"request_id\":\"hook\",\"type\":\"device.status\",\"payload\":{}}\r\n";
   vg_line_feed(&reader,&commands,line,8,emit,NULL);
   assert(dispatch_calls==0 && !reply[0]);
   vg_line_feed(&reader,&commands,line+8,strlen(line)-8,emit,NULL);}
  assert(dispatch_calls==1 && strstr(reply,"response.ok"));
  for(i=0;i<VG_COMMAND_LINE+2;i++) vg_line_feed(&reader,&commands,"x",1,emit,NULL);
  vg_line_feed(&reader,&commands,"\n\r\n",3,emit,NULL);
  assert(dispatch_calls==1 && strstr(reply,"LINE_TOO_LONG"));
  commands.line_execute=empty_dispatch;
  vg_line_feed(&reader,&commands,"{}\n",3,emit,NULL);
  assert(strstr(reply,"DISPATCH_ERROR") && strstr(reply,"\"uncertain\":true"));
  commands.line_execute=error_dispatch;
  vg_line_feed(&reader,&commands,"{}\n",3,emit,NULL);
  assert(strcmp(reply,explicit_error)==0);
  commands.line_execute=NULL;
  puts("PASS commands: strict envelopes, UTF-8/NUL, 64-bit epoch, replay, guarded snooze, offline loop, errors, JSON Lines framing");
  return 0;
}
