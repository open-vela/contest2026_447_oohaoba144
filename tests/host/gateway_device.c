/* Host-only integration harness. test.advance and no-op parent barrier must
 * never be compiled into firmware. This verifies process recovery, not power loss.
 * argv: store_dir epoch [mono_ms rtc_valid]. Each process is a physical boot;
 * test.advance only samples/ticks when fire is true (default), queries never tick.
 */
#include "velaguard_commands.h"
#include "velaguard_store_file.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static vg_runtime_t runtime;static vg_commands_t commands;
static vg_store_file_t file;static vg_line_reader_t reader;
static int64_t now;static uint64_t mono;static bool rtc_valid;
static unsigned scheduler_calls;
static int clock_read(void *c,int64_t *n,bool *v){(void)c;*n=now;*v=rtc_valid;return 0;}
static int clock_set(void *c,int64_t n){(void)c;now=n;rtc_valid=true;return 0;}
static int mono_read(void *c,uint64_t *n){(void)c;*n=mono;return 0;}
static int schedule(void *c,bool a,uint64_t n){(void)c;(void)a;(void)n;scheduler_calls++;return 0;}
static int test_parent(void *c,const char *p){(void)c;(void)p;return 0;}
static void emit(void *c,const char *s){(void)c;puts(s);fflush(stdout);}
static int read_u64(const char *s,uint64_t *out)
{
  uint64_t value=0;unsigned digit;
  if(!s||!*s||(s[0]=='0'&&s[1]))return -1;
  for(;*s;s++){
    if(*s<'0'||*s>'9')return -1;
    digit=(unsigned)(*s-'0');
    if(value>(UINT64_MAX-digit)/10)return -1;
    value=value*10+digit;
  }
  *out=value;return 0;
}
int main(int argc,char *argv[])
{
  vg_store_file_ops_t ops;vg_runtime_deps_t deps;vg_runtime_timer_ops_t timers;
  char line[2048];cJSON *o,*t,*p,*e,*id,*m,*v,*fire;
  if(argc!=3&&argc!=5)return 2;
  now=strtoll(argv[2],NULL,10);rtc_valid=now>0;
  if(argc==5){if(read_u64(argv[3],&mono)!=0)return 2;rtc_valid=!strcmp(argv[4],"1");}
  vg_store_file_native_ops(&ops);ops.sync_parent=test_parent;
  if(vg_store_file_init(&file,argv[1],&ops)!=0)return 3;
  deps.io=vg_store_file_io(&file);deps.context=NULL;deps.clock=clock_read;deps.scheduler_reconcile=NULL;
  timers.context=NULL;timers.monotonic_ms=mono_read;timers.scheduler_reconcile=schedule;
  if(vg_runtime_start_with_timers(&runtime,&deps,&timers)!=0)return 4;
  vg_commands_init(&commands,&runtime,NULL,clock_set);
  while(fgets(line,sizeof(line),stdin))
    {
      o=cJSON_Parse(line);t=cJSON_GetObjectItemCaseSensitive(o,"type");
      if(cJSON_IsString(t)&&!strcmp(t->valuestring,"test.advance"))
        {
          p=cJSON_GetObjectItemCaseSensitive(o,"payload");e=cJSON_GetObjectItemCaseSensitive(p,"epoch");
          m=cJSON_GetObjectItemCaseSensitive(p,"mono_ms");v=cJSON_GetObjectItemCaseSensitive(p,"rtc_valid");
          fire=cJSON_GetObjectItemCaseSensitive(p,"fire");id=cJSON_GetObjectItemCaseSensitive(o,"request_id");
          if(!cJSON_IsString(id)||(e&&!cJSON_IsNumber(e))||(v&&!cJSON_IsBool(v))||
             (fire&&!cJSON_IsBool(fire))||(m&&(!cJSON_IsString(m)||read_u64(m->valuestring,&mono)!=0)))
            {cJSON_Delete(o);return 5;}
          if(e)now=(int64_t)e->valuedouble;
          if(v)rtc_valid=cJSON_IsTrue(v);
          if((!fire||cJSON_IsTrue(fire))&&vg_runtime_tick(&runtime)!=0){cJSON_Delete(o);return 6;}
          printf("{\"version\":1,\"request_id\":\"%s\",\"type\":\"response.ok\",\"payload\":{\"scheduler_calls\":%u}}\n",id->valuestring,scheduler_calls);
          fflush(stdout);
        }
      else vg_line_feed(&reader,&commands,line,strlen(line),emit,NULL);
      cJSON_Delete(o);
    }
  return 0;
}
