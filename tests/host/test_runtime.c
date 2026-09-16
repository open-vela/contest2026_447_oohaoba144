#include "velaguard_runtime.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char disk[2][VG_STORE_MAX_BYTES];
static size_t length[2];
static int save_fail, schedule_fail, clock_fail;
static int64_t now=1000, wake;
static bool rtc=true, armed;
static unsigned writes;
static vg_runtime_t runtime, restarted;
static int rd(void *ctx,unsigned slot,char *buf,size_t cap,size_t *n)
{
  (void)ctx;if(!length[slot]) return 1;
  if(length[slot]>cap) return -1;
  memcpy(buf,disk[slot],length[slot]);*n=length[slot];return 0;
}
static int wr(void *ctx,unsigned slot,const char *buf,size_t n)
{
  (void)ctx;writes++;
  if(save_fail) return -1;
  memcpy(disk[slot],buf,n);length[slot]=n;return 0;
}
static int clock_get(void *ctx,int64_t *epoch,bool *valid)
{
  (void)ctx;if(clock_fail)return -1;*epoch=now;*valid=rtc;return 0;
}
static int schedule(void *ctx,bool has_next,int64_t epoch)
{
  (void)ctx;if(schedule_fail) return -1;armed=has_next;wake=epoch;return 0;
}
static vg_runtime_deps_t deps={{NULL,rd,wr},NULL,clock_get,schedule};
static vg_create_request_t request(const char *id,int64_t due)
{
  vg_create_request_t r={0};strcpy(r.request_id,id);strcpy(r.title,"offline test");
  r.due_epoch=due;r.priority=1;return r;
}
static void resign(cJSON *root,unsigned slot)
{
  cJSON *body=cJSON_GetObjectItemCaseSensitive(root,"data");
  char *text=cJSON_PrintUnformatted(body),*out,checksum[9];const char *p;
  uint32_t crc=UINT32_MAX;unsigned i;
  assert(text);
  for(p=text;*p;p++){crc^=(unsigned char)*p;for(i=0;i<8;i++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
  snprintf(checksum,sizeof(checksum),"%08x",(unsigned)(crc^UINT32_MAX));
  assert(cJSON_ReplaceItemInObjectCaseSensitive(root,"checksum",cJSON_CreateString(checksum)));
  out=cJSON_PrintUnformatted(root);assert(out);length[slot]=strlen(out);memcpy(disk[slot],out,length[slot]);
  cJSON_free(text);cJSON_free(out);
}
static void metadata_validation(void)
{
  static char original[VG_STORE_MAX_BYTES];
  const char *field[]={"updated_epoch","acknowledged_epoch","snooze_count","snoozed_epoch","created_epoch"};
  const char *value[]={"\"0\"","\"2000\"","1","\"2000\"","\"9223372036854775808\""};
  unsigned slot=(unsigned)runtime.store.current_slot,i;
  size_t original_length=length[slot],other_length=length[1-slot];
  cJSON *root,*body,*task;
  memcpy(original,disk[slot],original_length);length[1-slot]=0;
  for(i=0;i<5;i++)
    {
      root=cJSON_ParseWithLength(original,original_length);assert(root);
      body=cJSON_GetObjectItemCaseSensitive(root,"data");
      task=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(body,"active"),0);
      assert(cJSON_ReplaceItemInObjectCaseSensitive(task,field[i],cJSON_Parse(value[i])));
      resign(root,slot);cJSON_Delete(root);
      assert(vg_store_load(&restarted.store,&deps.io)==VG_STORE_CORRUPT);
    }
  root=cJSON_ParseWithLength(original,original_length);assert(root);
  body=cJSON_GetObjectItemCaseSensitive(root,"data");
  task=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(body,"history"),0);
  assert(cJSON_ReplaceItemInObjectCaseSensitive(task,"acknowledged_epoch",cJSON_CreateString("1")));
  resign(root,slot);cJSON_Delete(root);
  assert(vg_store_load(&restarted.store,&deps.io)==VG_STORE_CORRUPT);
  memcpy(disk[slot],original,original_length);length[slot]=original_length;length[1-slot]=other_length;
}
static void migration(void)
{
  static const char *fields[]={"created_epoch","updated_epoch","acknowledged_epoch","snoozed_epoch","missed_epoch","snooze_count","timer_domain","delay_seconds","mono_deadline_ms","timer_boot_id","timer_revision"};
  cJSON *root,*body,*array,*item;unsigned slot,i;vg_store_t *store=&restarted.store;
  slot=(unsigned)runtime.store.current_slot;
  root=cJSON_ParseWithLength(disk[slot],length[slot]);assert(root);
  body=cJSON_GetObjectItemCaseSensitive(root,"data");
  assert(cJSON_ReplaceItemInObjectCaseSensitive(body,"version",cJSON_CreateNumber(1)));
  cJSON_DeleteItemFromObjectCaseSensitive(body,"boot_counter");
  array=cJSON_GetObjectItemCaseSensitive(body,"history");
  cJSON_ArrayForEach(item,array)for(i=0;i<11;i++)cJSON_DeleteItemFromObjectCaseSensitive(item,fields[i]);
  array=cJSON_GetObjectItemCaseSensitive(body,"active");
  cJSON_ArrayForEach(item,array)for(i=0;i<11;i++)cJSON_DeleteItemFromObjectCaseSensitive(item,fields[i]);
  resign(root,slot);cJSON_Delete(root);length[1-slot]=0;
  assert(vg_store_load(store,&deps.io)==VG_OK && store->dirty);
  assert(store->history[0].created_epoch==0 && store->history[0].snooze_count==0);
  assert(vg_store_save(store,&deps.io)==VG_OK && !store->dirty);
  assert(vg_store_load(store,&deps.io)==VG_OK && !store->dirty);
  slot=(unsigned)store->current_slot;root=cJSON_ParseWithLength(disk[slot],length[slot]);assert(root);
  body=cJSON_GetObjectItemCaseSensitive(root,"data");
  assert(cJSON_GetObjectItemCaseSensitive(body,"version")->valueint==VG_STORE_VERSION);
  assert(cJSON_ReplaceItemInObjectCaseSensitive(body,"version",cJSON_CreateNumber(VG_STORE_VERSION+1)));
  resign(root,slot);cJSON_Delete(root);
  assert(vg_store_load(store,&deps.io)==VG_ERR_UNSUPPORTED_VERSION);
}
static void due_during_command(void)
{
  vg_create_request_t r;vg_task_t task;
  length[0]=length[1]=0;now=1000;
  assert(vg_runtime_start(&runtime,&deps)==VG_OK);
  r=request("first",1010);assert(vg_runtime_create(&runtime,&r,&task)==VG_OK);
  now=1015;r=request("second",2000);
  assert(vg_runtime_create(&runtime,&r,&task)==VG_OK);
  assert(!strcmp(task.request.request_id,"second") && wake>now);
  assert(vg_runtime_find(&runtime,"first",&task,NULL)==VG_OK && task.state==VG_TASK_ALERTING);
  clock_fail=1;
  assert(vg_runtime_reload(&runtime)==VG_ERR_INVALID_TIME && !armed);
  clock_fail=0;assert(vg_runtime_reload(&runtime)==VG_OK);
  now=1400;r=request("third",2200);
  assert(vg_runtime_create(&runtime,&r,&task)==VG_OK && !strcmp(task.request.request_id,"third"));
  assert(vg_runtime_find(&runtime,"first",&task,NULL)==VG_OK && task.state==VG_TASK_MISSED);
  now=2300;assert(vg_runtime_tick(&runtime)==VG_OK);
  assert(vg_runtime_find(&runtime,"second",&task,NULL)==VG_OK && task.state==VG_TASK_ALERTING);
  now=2301;assert(vg_runtime_tick(&runtime)==VG_OK);
  assert(vg_runtime_find(&runtime,"second",&task,NULL)==VG_OK && task.state==VG_TASK_MISSED);
  rtc=false;assert(vg_runtime_reload(&runtime)==VG_OK && !armed);
  assert(vg_runtime_tick(&runtime)==VG_ERR_INVALID_TIME);
  rtc=true;assert(vg_runtime_reload(&runtime)==VG_OK && armed);
  runtime.store.active.tasks[0].snooze_count=UINT32_MAX;
  runtime.store.active.tasks[0].snoozed_epoch=2301;
  runtime.store.active.tasks[0].updated_epoch=2301;
  assert(vg_runtime_snooze(&runtime,"third",1,&task)==VG_ERR_CAPACITY);
  now=1000;assert(vg_runtime_reload(&runtime)==VG_ERR_INVALID_TIME && !armed && runtime.blocked);
  now=2301;assert(vg_runtime_reload(&runtime)==VG_OK);
}
int main(void)
{
  vg_task_t task,copy;vg_create_request_t r;vg_runtime_status_t status;unsigned before;
  assert(vg_runtime_start(&runtime,&deps)==VG_OK && !armed);
  r=request("one",1010);assert(vg_runtime_create(&runtime,&r,&task)==VG_OK);
  assert(task.state==VG_TASK_SCHEDULED && task.created_epoch==1000 && task.updated_epoch==1000);
  assert(armed && wake==1010);
  before=writes;r.due_epoch=999;
  assert(vg_runtime_create(&runtime,&r,&copy)==VG_OK && writes==before && copy.request.due_epoch==1010);
  now=1010;assert(vg_runtime_tick(&runtime)==VG_OK);
  assert(vg_runtime_find(&runtime,"one",&task,NULL)==VG_OK && task.state==VG_TASK_ALERTING);
  assert(armed && wake==1311);
  assert(vg_runtime_start(&restarted,&deps)==VG_OK);
  assert(vg_runtime_find(&restarted,"one",&task,NULL)==VG_OK && task.state==VG_TASK_ALERTING);
  assert(vg_runtime_snooze(&runtime,"one",60,&task)==VG_OK);
  assert(task.request.due_epoch==1070 && task.snooze_count==1 && task.snoozed_epoch==1010 && task.state==VG_TASK_SCHEDULED);
  now=1070;assert(vg_runtime_tick(&runtime)==VG_OK);
  now=1072;assert(vg_runtime_ack(&runtime,"one",&task)==VG_OK && task.acknowledged_epoch==1072);
  assert(!armed && task.state==VG_TASK_ACKNOWLEDGED);
  assert(vg_runtime_start(&restarted,&deps)==VG_OK);
  assert(vg_runtime_list(&restarted,true,0,&copy)==VG_OK && !memcmp(&task,&copy,sizeof(task)));
  r=request("one",2000);assert(vg_runtime_create(&restarted,&r,&copy)==VG_OK && copy.state==VG_TASK_ACKNOWLEDGED);
  assert(vg_runtime_ack(&restarted,"one",&copy)==VG_OK);
  r=request("miss",1080);assert(vg_runtime_create(&runtime,&r,&task)==VG_OK);
  now=1381;assert(vg_runtime_tick(&runtime)==VG_OK);
  assert(vg_runtime_find(&runtime,"miss",&task,NULL)==VG_OK && task.state==VG_TASK_MISSED && task.missed_epoch==1381);
  r=request("fail",2000);save_fail=1;
  assert(vg_runtime_create(&runtime,&r,&task)==VG_STORE_IO_ERROR && runtime.blocked);
  assert(vg_runtime_tick(&runtime)==VG_RUNTIME_BLOCKED);
  save_fail=0;assert(vg_runtime_reload(&runtime)==VG_OK && !runtime.blocked);
  assert(vg_runtime_find(&runtime,"fail",&task,NULL)==VG_ERR_TASK_NOT_FOUND);
  schedule_fail=1;
  assert(vg_runtime_create(&runtime,&r,&task)==VG_RUNTIME_SCHEDULER_ERROR && runtime.blocked);
  schedule_fail=0;assert(vg_runtime_reload(&runtime)==VG_OK);
  assert(vg_runtime_find(&runtime,"fail",&task,NULL)==VG_OK);
  rtc=false;r=request("invalid-clock",3000);
  assert(vg_runtime_create(&runtime,&r,&task)==VG_ERR_INVALID_TIME);rtc=true;
  now=2000;assert(vg_runtime_tick(&runtime)==VG_OK);
  assert(vg_runtime_snooze(&runtime,"fail",0,&task)==VG_ERR_INVALID_TIME);
  now=INT64_MAX-10;assert(vg_runtime_snooze(&runtime,"fail",60,&task)==VG_ERR_INVALID_TIME);
  now=2000;assert(vg_runtime_status(&runtime,&status)==VG_OK && status.alerting_count==1);
  metadata_validation();
  migration();
  due_during_command();
  length[0]=length[1]=1; disk[0][0]=disk[1][0]='!';
  assert(vg_runtime_reload(&runtime)!=VG_OK && !armed && runtime.blocked);
  puts("PASS runtime offline loop: create/due/alert/snooze/ack/save/reload, dedup, missed, blocked recovery, metadata, v1 migration/current+1 protection");
  return 0;
}
