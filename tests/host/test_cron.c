#include "agent_compat.h"
#include "velaguard_platform.h"
#include <assert.h>
#include <stdatomic.h>
static char cache_path[1024];
static uint64_t fake_ms=1050;
static time_t fake_wall=1000;
static atomic_bool fail_get;
static bool real_clock,fail_open;
static unsigned starts,raw_reads,advance_read;
static uint64_t advance_ms;
static bool start_fail;
static pthread_t worker;
static atomic_uint backoffs;
static int test_gettime(clockid_t id,struct timespec *ts){
 assert(id==CLOCK_MONOTONIC);
 raw_reads++;if(advance_read && raw_reads==advance_read)fake_ms=advance_ms;
 if(atomic_load(&fail_get))return -1;
 if(real_clock)return clock_gettime(id,ts);
 ts->tv_sec=(time_t)(fake_ms/1000);ts->tv_nsec=(long)((fake_ms%1000)*1000000);return 0;
}
static time_t test_time(time_t *out){if(out)*out=fake_wall;return fake_wall;}
static FILE *test_open(const char *path,const char *mode){assert(!strcmp(path,cache_path));if(fail_open && mode[0]=='w')return NULL;return fopen(path,mode);}
static int test_start(void *(*fn)(void*),const char *name,int stack,void *arg,int priority){
 (void)name;(void)stack;(void)priority;starts++;
 return start_fail?-1:(real_clock?pthread_create(&worker,NULL,fn,arg):0);
}
static int test_sleep(const struct timespec *delay,struct timespec *left){atomic_fetch_add(&backoffs,1);return nanosleep(delay,left);}
#define VG_CRON_FILE cache_path
#define VG_CRON_RAW_GETTIME test_gettime
#define VG_CRON_RAW_TIME test_time
#define VG_CRON_OPEN test_open
#define VG_CRON_TASK_CREATE test_start
#define VG_CRON_BACKOFF test_sleep
#include "../../app/velaguard/src/velaguard_cron.c"
static void deterministic(void){
 FILE *f=fopen(cache_path,"w");assert(f);fputs("{\"jobs\":[{\"id\":\"12345678\",\"name\":\"stale\",\"kind\":\"at\",\"message\":\"old\",\"enabled\":true,\"at_epoch\":2,\"next_run\":2}]}",f);fclose(f);
 fail_open=true;assert(vg_cron_init()==0 && starts==1 && s_job_count==0);fail_open=false;assert(vg_cron_init()==0 && starts==1);
 uint64_t ms;assert(vg_cron_monotonic_ms(NULL,&ms)==0 && ms==1050);
 assert(vg_cron_clock_time(NULL)==2);struct timespec ts;assert(vg_cron_clock_gettime(CLOCK_REALTIME,&ts)==0 && ts.tv_sec==1 && ts.tv_nsec==50000000);
 assert(vg_cron_reconcile_mono(NULL,true,1100)==0 && s_job_count==1 && s_jobs[0].next_run==3);
 char id[9];strcpy(id,s_jobs[0].id);assert(vg_cron_reconcile_mono(NULL,true,1100)==0 && !strcmp(id,s_jobs[0].id));
 fake_wall=INT32_MAX;fake_ms=1999;cron_process_due_jobs();assert(!vg_cron_take_event() && s_job_count==1);
 fake_wall=1;fake_ms=2000;cron_process_due_jobs();assert(vg_cron_take_event() && s_job_count==0);assert(!vg_cron_take_event());
 assert(vg_agent_cron_remove_job(id)!=0);assert(vg_cron_reconcile_mono(NULL,false,0)==0); /* fire/remove race */
 assert(vg_cron_reconcile_mono(NULL,true,2000)==0 && vg_cron_take_event());assert(vg_cron_reconcile_mono(NULL,false,0)==0);
 fake_ms=2500;fake_wall=1000;assert(vg_cron_reconcile(NULL,true,1002)==0 && s_jobs[0].next_run==6);
 assert(vg_cron_reconcile(NULL,false,0)==0);fake_wall=0;assert(vg_cron_reconcile(NULL,true,1002)!=0 && s_job_count==0);
 fake_wall=1000;fail_open=true;assert(vg_cron_reconcile_mono(NULL,true,4000)==0 && s_job_count==1); /* cache write not business durability */
 fake_ms=4000;cron_process_due_jobs();assert(vg_cron_take_event() && s_job_count==0);
 fake_ms=4000;advance_read=raw_reads+2;advance_ms=5000;assert(vg_cron_reconcile_mono(NULL,true,4500)==0 && !s_jobs[0].enabled && vg_cron_take_event());
 assert(vg_cron_reconcile_mono(NULL,false,0)==0);
 puts("PASS real official cron deterministic: mono rounding, stale cache cleanup, one worker, wall independence, pending only, remove race, epoch compatibility, cache IO");
}
static void faults(const char *kind){
 assert(vg_cron_init()==0);assert(vg_cron_reconcile_mono(NULL,true,2000)==0);
 if(!strcmp(kind,"rollback"))fake_ms=1049;else atomic_store(&fail_get,true);
 cron_process_due_jobs();assert(atomic_load(&vg_clock_fault));
 uint64_t value=77;assert(vg_cron_monotonic_ms(NULL,&value)!=0 && value==77);
 atomic_store(&fail_get,false);fake_ms=3000;cron_process_due_jobs();assert(s_job_count==1);
 assert(vg_cron_monotonic_ms(NULL,&value)!=0 && vg_cron_init()!=0 && starts==1);
 assert(vg_cron_reconcile_mono(NULL,false,0)==0 && vg_cron_take_event());
 char out[32];assert(vg_agent_tool_registry_execute("velaguard.wake","{}",out,sizeof(out))!=0 && !vg_cron_take_event());
 assert(vg_cron_reconcile_mono(NULL,false,0)==0 && s_job_count==0);
 puts("PASS cron fault latch: no due execution, no recovery on one good sample, main sees same fault, no second worker");
}
static void initial_failure(const char *kind){
 if(!strcmp(kind,"startfail"))start_fail=true;else atomic_store(&fail_get,true);
 assert(vg_cron_init()!=0);unsigned before=starts;assert(vg_cron_init()!=0 && starts==before);
 uint64_t value=42;assert(vg_cron_monotonic_ms(NULL,&value)!=0 && value==42);
 assert(vg_cron_reconcile_mono(NULL,true,2000)!=0 && vg_cron_reconcile_mono(NULL,false,0)==0);
 puts("PASS initial clock/thread failure latched without retrying worker creation");
}
static void ranges(void){
 fake_ms=0;assert(vg_cron_init()==0 && vg_cron_clock_time(NULL)==1);
 assert(vg_cron_reconcile_mono(NULL,true,UINT64_MAX)!=0 && s_job_count==0);
 uint64_t last=(uint64_t)(INT32_MAX-1)*1000;
 assert(vg_cron_reconcile_mono(NULL,true,last)==0 && s_jobs[0].next_run==INT32_MAX);
 assert(vg_cron_reconcile_mono(NULL,false,0)==0);
 assert(vg_cron_reconcile_mono(NULL,true,last+1)!=0 && s_job_count==0);
 fake_ms=last;uint64_t value;assert(vg_cron_monotonic_ms(NULL,&value)==0);
 fake_ms=(uint64_t)INT32_MAX*1000;assert(vg_cron_monotonic_ms(NULL,&value)!=0);
 puts("PASS explicit target int32 time_t bounds on 64-bit host");
}
static void *reader(void *unused){(void)unused;for(int i=0;i<2000;i++){uint64_t now;assert(vg_cron_monotonic_ms(NULL,&now)==0);}return NULL;}
static void smoke(void){
 real_clock=true;assert(vg_cron_init()==0);pthread_t readers[2];for(int i=0;i<2;i++)assert(pthread_create(&readers[i],NULL,reader,NULL)==0);
 for(int i=0;i<200;i++){uint64_t now;assert(vg_cron_monotonic_ms(NULL,&now)==0);assert(vg_cron_reconcile_mono(NULL,true,now+500)==0);}
 for(int i=0;i<2;i++)pthread_join(readers[i],NULL);
 bool fired=false;struct timespec pause={0,20000000};for(int i=0;i<200&&!fired;i++){nanosleep(&pause,NULL);fired=vg_cron_take_event();}assert(fired);
 uint64_t now;assert(vg_cron_monotonic_ms(NULL,&now)==0);assert(vg_cron_reconcile_mono(NULL,true,now+5000)==0);
 atomic_store(&fail_get,true);for(int i=0;i<100 && atomic_load(&backoffs)==0;i++)nanosleep(&pause,NULL);assert(atomic_load(&backoffs)>0);
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);assert(vg_cron_reconcile_mono(NULL,false,0)==0);clock_gettime(CLOCK_MONOTONIC,&end);
 assert((end.tv_sec-begin.tv_sec)*1000000000LL+end.tv_nsec-begin.tv_nsec<500000000LL);
 assert(vg_cron_monotonic_ms(NULL,&now)!=0 && vg_cron_init()!=0 && starts==1);
 /* Test-only shutdown joins explicitly; official stop itself does not join. */
 vg_agent_cron_service_stop();pthread_cond_signal(&s_cron_wake);pthread_join(worker,NULL);
 puts("PASS real worker smoke: concurrent shared mono readers/reconcile, pending event, fault backoff releases cron lock, one worker, explicit test join");
}
int main(int argc,char **argv){assert(argc==3);snprintf(cache_path,sizeof(cache_path),"%s/cron-%s.json",argv[1],argv[2]);if(!strcmp(argv[2],"normal"))deterministic();else if(!strcmp(argv[2],"range"))ranges();else if(!strcmp(argv[2],"thread"))smoke();else if(!strcmp(argv[2],"startfail")||!strcmp(argv[2],"initfault"))initial_failure(argv[2]);else faults(argv[2]);return 0;}
