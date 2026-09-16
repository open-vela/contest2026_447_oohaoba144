#include "velaguard_store.h"
#include <assert.h>
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char disk[2][VG_STORE_MAX_BYTES];
static size_t lengths[2];
static int fault;
static unsigned writes;
static int read_fault;
static int unreadable_slot=-1;
static int empty_slot=-1;
static int rd(void *ctx, unsigned slot, char *buf, size_t cap, size_t *n) {
  (void)ctx; if(read_fault || (int)slot==unreadable_slot) return -1;
  if((int)slot==empty_slot) {*n=0;return 0;}
  if (!lengths[slot]) return 1;
  if (lengths[slot]>cap) return -1;
  memcpy(buf,disk[slot],lengths[slot]); *n=lengths[slot]; return 0;
}
static int wr(void *ctx,unsigned slot,const char *buf,size_t n) {
  (void)ctx; writes++;
  if (fault) { memcpy(disk[slot],buf,n/2); lengths[slot]=n/2; return -1; }
  memcpy(disk[slot],buf,n); lengths[slot]=n; return 0;
}
static vg_store_io_t io={NULL,rd,wr};
static vg_store_t s, restored, before;
static vg_create_request_t req(unsigned i) {
  vg_create_request_t r={0}; snprintf(r.request_id,sizeof(r.request_id),"r%u",i);
  strcpy(r.title,"reminder"); r.due_epoch=INT64_MAX; r.priority=2; return r;
}
static void finish(void) {
  assert(vg_store_transition(&s,0,VG_TASK_SCHEDULED)==VG_OK);
  assert(vg_store_transition(&s,0,VG_TASK_ALERTING)==VG_OK);
  assert(vg_store_transition(&s,0,VG_TASK_ACKNOWLEDGED)==VG_OK);
}
/* Re-sign semantic mutations so checks exercise schema, not only CRC. */
static void mutate(const char *field, const char *value) {
  cJSON *root=cJSON_ParseWithLength(disk[s.current_slot],lengths[s.current_slot]);
  cJSON *body=cJSON_GetObjectItemCaseSensitive(root,"data");
  cJSON *target=body,*replacement=cJSON_Parse(value);char *str,*out,crc[9];
  uint32_t c=UINT32_MAX;unsigned j;const char *p;
  assert(root && replacement);
  if(strcmp(field,"version") && strcmp(field,"generation") && strcmp(field,"boot_counter"))
    target=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(body,"active"),0);
  assert(cJSON_ReplaceItemInObjectCaseSensitive(target,field,replacement));
  str=cJSON_PrintUnformatted(body);assert(str);
  for(p=str;*p;p++) {c^=(unsigned char)*p;for(j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0);}
  snprintf(crc,sizeof(crc),"%08x",(unsigned)(c^UINT32_MAX));
  assert(cJSON_ReplaceItemInObjectCaseSensitive(root,"checksum",cJSON_CreateString(crc)));
  out=cJSON_PrintUnformatted(root);assert(out);lengths[s.current_slot]=strlen(out);
  memcpy(disk[s.current_slot],out,lengths[s.current_slot]);
  cJSON_free(str);cJSON_free(out);cJSON_Delete(root);
}
static void seed(void) {
  vg_create_request_t r=req(777);lengths[0]=lengths[1]=0;
  vg_store_init(&s);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);
  assert(vg_store_save(&s,&io)==VG_OK);
}
static void seal(cJSON *root) {
 cJSON *body=cJSON_GetObjectItemCaseSensitive(root,"data");char *text=cJSON_PrintUnformatted(body),crc[9],*out;uint32_t c=UINT32_MAX;assert(text);
 for(const char *p=text;*p;p++){c^=(unsigned char)*p;for(unsigned j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}
 snprintf(crc,sizeof(crc),"%08x",(unsigned)(c^UINT32_MAX));assert(cJSON_ReplaceItemInObjectCaseSensitive(root,"checksum",cJSON_CreateString(crc)));
 out=cJSON_PrintUnformatted(root);assert(out);lengths[s.current_slot]=strlen(out);memcpy(disk[s.current_slot],out,lengths[s.current_slot]);cJSON_free(out);cJSON_free(text);
}
static void old_version(unsigned version) {
 cJSON *root=cJSON_ParseWithLength(disk[s.current_slot],lengths[s.current_slot]);assert(root);
 cJSON *body=cJSON_GetObjectItemCaseSensitive(root,"data");assert(cJSON_ReplaceItemInObjectCaseSensitive(body,"version",cJSON_CreateNumber(version)));cJSON_DeleteItemFromObjectCaseSensitive(body,"boot_counter");
 const char *extra[]={"timer_domain","delay_seconds","mono_deadline_ms","timer_boot_id","timer_revision","created_epoch","updated_epoch","acknowledged_epoch","snoozed_epoch","missed_epoch","snooze_count"};
 for(unsigned a=0;a<2;a++){cJSON *array=cJSON_GetObjectItemCaseSensitive(body,a?"history":"active"),*task;
  cJSON_ArrayForEach(task,array)for(unsigned k=0;k<(version==1?11u:5u);k++)cJSON_DeleteItemFromObjectCaseSensitive(task,extra[k]);}
 seal(root);cJSON_Delete(root);
}
static void rel_seed(void) {
 lengths[0]=lengths[1]=0;vg_store_init(&s);assert(vg_store_advance_boot(&s)==VG_OK);
 vg_create_request_t r=req(500);r.due_epoch=0;
 assert(vg_store_create_relative(&s,&r,60,1234,NULL)==VG_OK);assert(vg_store_save(&s,&io)==VG_OK);
}
static void v3_tests(void) {
 for(unsigned ver=1;ver<=2;ver++) {
  seed();s.active.tasks[0].created_epoch=100;s.active.tasks[0].updated_epoch=180;
  s.active.tasks[0].snoozed_epoch=150;s.active.tasks[0].snooze_count=2;
  assert(vg_store_transition(&s,0,VG_TASK_SCHEDULED)==VG_OK);assert(vg_store_transition(&s,0,VG_TASK_ALERTING)==VG_OK);
  assert(vg_store_transition(&s,0,VG_TASK_ACKNOWLEDGED)==VG_OK);s.history[0].acknowledged_epoch=180;
  vg_create_request_t r=req(778);assert(vg_store_create(&s,&r,200,true,NULL)==VG_OK);
  assert(vg_store_save(&s,&io)==VG_OK);lengths[1-s.current_slot]=0;before=s;old_version(ver);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.dirty && restored.boot_counter==0);
  assert(restored.active.tasks[0].timer_domain==VG_TIMER_ABSOLUTE && restored.history[0].timer_domain==VG_TIMER_ABSOLUTE);
  if(ver==2){assert(!memcmp(&restored.active,&before.active,sizeof(before.active)));assert(!memcmp(&restored.history[0],&before.history[0],sizeof(vg_task_t)));}
  else assert(restored.history[0].created_epoch==0 && restored.history[0].acknowledged_epoch==0 && restored.history[0].snooze_count==0);
  assert(vg_store_save(&restored,&io)==VG_OK);cJSON *root=cJSON_ParseWithLength(disk[restored.current_slot],lengths[restored.current_slot]);assert(root);
  assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"data"),"version")->valueint==3);cJSON_Delete(root);
 }
 lengths[0]=lengths[1]=0;vg_store_init(&s);vg_create_request_t r=req(500);r.due_epoch=0;
 before=s;assert(vg_store_create_relative(&s,&r,60,0,NULL)==VG_ERR_INVALID_MESSAGE && !memcmp(&s,&before,sizeof(s)));
 assert(vg_store_advance_boot(&s)==VG_OK && s.boot_counter==1 && s.dirty);
 before=s;assert(vg_store_create_relative(&s,&r,0,0,NULL)!=0 && !memcmp(&s,&before,sizeof(s)));
 assert(vg_store_create_relative(&s,&r,86401,0,NULL)!=0 && !memcmp(&s,&before,sizeof(s)));
 assert(vg_store_create_relative(&s,&r,60,UINT64_MAX-59999,NULL)==VG_ERR_INVALID_TIME && !memcmp(&s,&before,sizeof(s)));
 assert(vg_store_create(&s,&r,0,false,NULL)==VG_ERR_INVALID_MESSAGE);
 assert(vg_store_create_relative(&s,&r,60,UINT64_MAX-60000,NULL)==VG_OK);
 assert(s.active.tasks[0].mono_deadline_ms==UINT64_MAX && s.active.tasks[0].timer_revision==1 && s.active.tasks[0].timer_boot_id==1 && s.active.tasks[0].request.due_epoch==0);
 before=s;size_t index=99;assert(vg_store_create_relative(&s,&r,60,0,&index)==VG_ERR_DUPLICATE_REQUEST && index==0 && !memcmp(&s,&before,sizeof(s)));
 s.active.tasks[0].timer_revision=UINT64_MAX;s.dirty=true;assert(vg_store_save(&s,&io)==VG_OK);assert(vg_store_load(&restored,&io)==VG_OK && restored.active.tasks[0].timer_revision==UINT64_MAX && restored.active.tasks[0].mono_deadline_ms==UINT64_MAX);
 uint64_t boot=restored.boot_counter;assert(vg_store_load(&restored,&io)==VG_OK && restored.boot_counter==boot);
 assert(vg_store_advance_boot(&s)==VG_OK);assert(vg_store_save(&s,&io)==VG_OK);assert(vg_store_load(&restored,&io)==VG_OK && restored.active.tasks[0].timer_boot_id==1 && restored.active.tasks[0].state==VG_TASK_CREATED);
 s.boot_counter=UINT64_MAX;s.dirty=false;before=s;assert(vg_store_advance_boot(&s)==VG_ERR_CAPACITY && !memcmp(&s,&before,sizeof(s)));
 rel_seed();s.active.tasks[0].created_epoch=100;s.active.tasks[0].updated_epoch=0;s.active.tasks[0].snooze_count=1;s.active.tasks[0].snoozed_epoch=0;
 assert(vg_store_transition(&s,0,VG_TASK_SCHEDULED)==VG_OK);assert(vg_store_transition(&s,0,VG_TASK_ALERTING)==VG_OK);assert(vg_store_transition(&s,0,VG_TASK_ACKNOWLEDGED)==VG_OK);assert(vg_store_save(&s,&io)==VG_OK);assert(vg_store_load(&restored,&io)==VG_OK && restored.history[0].acknowledged_epoch==0 && restored.history[0].snooze_count==1);
 rel_seed();s.active.tasks[0].created_epoch=0;s.active.tasks[0].updated_epoch=100;s.active.tasks[0].snoozed_epoch=100;s.active.tasks[0].snooze_count=1;s.active.tasks[0].state=VG_TASK_NEEDS_RESET;s.dirty=true;assert(vg_store_save(&s,&io)==VG_OK);assert(vg_store_load(&restored,&io)==VG_OK && restored.active.tasks[0].state==VG_TASK_NEEDS_RESET);
 s.history[0]=s.active.tasks[0];s.history_count=1;s.active.count=0;s.dirty=true;unsigned count=writes;assert(vg_store_save(&s,&io)==VG_ERR_INVALID_MESSAGE && writes==count);
 /* 原始delay不可变；新boot早期对保留ALERTING延后时deadline可更短。 */
 rel_seed();assert(vg_store_transition(&s,0,VG_TASK_SCHEDULED)==VG_OK);
 assert(vg_store_transition(&s,0,VG_TASK_ALERTING)==VG_OK);assert(vg_store_advance_boot(&s)==VG_OK);
 s.active.tasks[0].state=VG_TASK_SNOOZED;s.active.tasks[0].timer_boot_id=s.boot_counter;
 s.active.tasks[0].timer_revision=2;s.active.tasks[0].snooze_count=1;s.active.tasks[0].mono_deadline_ms=30001;
 s.dirty=true;assert(vg_store_save(&s,&io)==VG_OK);
 assert(vg_store_load(&restored,&io)==VG_OK && restored.active.tasks[0].delay_seconds==60 && restored.active.tasks[0].mono_deadline_ms==30001 && restored.active.tasks[0].timer_boot_id==2 && restored.active.tasks[0].timer_revision==2 && restored.active.tasks[0].snooze_count==1);
 const char *fields[]={"timer_boot_id","timer_revision","mono_deadline_ms","delay_seconds","due_epoch","timer_domain","boot_counter","created_epoch"};
 const char *values[]={"\"2\"","\"18446744073709551616\"","\"+60000\"","0","\"1\"","2","\"0\"","\"200\""};
 for(unsigned i=0;i<8;i++){rel_seed();if(i==7){s.active.tasks[0].updated_epoch=100;s.dirty=true;assert(vg_store_save(&s,&io)==0);lengths[1-s.current_slot]=0;}mutate(fields[i],values[i]);before=restored;count=writes;assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT && writes==count && !memcmp(&restored,&before,sizeof(before)));}
 const char *bad_u64[]={"18446744073709551616","-1"," 1","01","1 ",""};
 for(unsigned i=0;i<6;i++){rel_seed();char json[64];snprintf(json,sizeof(json),"\"%s\"",bad_u64[i]);mutate("timer_revision",json);assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);}
 rel_seed();mutate("timer_revision","\"0\"");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 rel_seed();mutate("timer_boot_id","\"0\"");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 rel_seed();mutate("mono_deadline_ms","\"0\"");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 rel_seed();mutate("delay_seconds","86401");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 rel_seed();mutate("timer_boot_id","1");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 seed();mutate("timer_domain","1");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 seed();mutate("state","6");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 seed();mutate("delay_seconds","1");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
 puts("PASS v3: v1/v2 lossless ABS migration; REL uint64 domains/boot/revision; unknown event times; NEEDS_RESET; load never advances boot");
}
int main(void) {
  vg_create_request_t r; unsigned i; size_t n;
  assert(VG_STORE_VERSION==3);
  vg_store_init(&s);
  assert(vg_store_load(&s,&io)==VG_OK && s.active.count==0);
  for(i=0;i<32;i++){r=req(i);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);}
  r=req(32); assert(vg_store_create(&s,&r,1,true,NULL)==VG_ERR_CAPACITY);
  assert(vg_store_transition(&s,0,VG_TASK_ACKNOWLEDGED)==VG_ERR_INVALID_STATE);
  finish(); assert(s.active.count==31 && s.history_count==1);
  assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);
  assert(vg_store_save(&s,&io)==VG_OK && !s.dirty);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==32);
  assert(restored.active.tasks[0].request.due_epoch==INT64_MAX);
  r=req(0);assert(vg_store_create(&restored,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
  r=req(1);assert(vg_store_create(&restored,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
  finish(); fault=1; assert(vg_store_save(&s,&io)==VG_STORE_IO_ERROR && s.dirty);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==32 && restored.dirty);
  fault=0; assert(vg_store_save(&s,&io)==VG_OK && !s.dirty);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==31);
  disk[s.current_slot][lengths[s.current_slot]/2]^=1;
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==32);
  disk[1-s.current_slot][0]='!'; n=restored.active.count;
  assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT && restored.active.count==n);
  lengths[0]=lengths[1]=0; vg_store_init(&s);
  for(i=0;i<140;i++){r=req(i);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);finish();}
  assert(s.active.count==0 && s.history_count==128);
  r=req(12);assert(vg_store_create(&s,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
  r=req(0);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);
  assert(vg_store_save(&s,&io)==VG_OK);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.history_count==128);
  r=req(139);assert(vg_store_create(&restored,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
  r=req(999);memset(r.request_id,'x',sizeof(r.request_id));
  assert(vg_store_create(&s,&r,1,true,NULL)==VG_ERR_INVALID_MESSAGE);
  s.generation=UINT32_MAX;s.dirty=true;
  assert(vg_store_save(&s,&io)!=VG_OK && s.dirty);
  seed(); unreadable_slot=1;
  before=restored;
  assert(vg_store_load(&restored,&io)==VG_STORE_IO_ERROR && !memcmp(&restored,&before,sizeof(before)));
  unreadable_slot=-1;
  seed(); n=writes;
  assert(vg_store_save(&s,&io)==VG_OK && writes==n);
  assert(vg_store_transition(&s,0,VG_TASK_SCHEDULED)==VG_OK);
  assert(vg_store_transition(&s,0,VG_TASK_ALERTING)==VG_OK);
  assert(vg_store_transition(&s,0,VG_TASK_MISSED)==VG_OK);
  assert(s.active.count==0 && s.history_count==1 && s.history[0].state==VG_TASK_MISSED);
  seed(); empty_slot=s.current_slot;
  assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);empty_slot=-1;
  read_fault=1;assert(vg_store_load(&restored,&io)==VG_STORE_IO_ERROR);read_fault=0;
  seed();mutate("version","4");assert(vg_store_load(&restored,&io)==VG_ERR_UNSUPPORTED_VERSION);
  /* Even a valid older slot must not silently hide a future schema. */
  memcpy(disk[1],disk[0],lengths[0]);lengths[1]=lengths[0];
  mutate("version","3");assert(vg_store_load(&restored,&io)==VG_ERR_UNSUPPORTED_VERSION);
  seed();mutate("due_epoch","\"9223372036854775808\"");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();mutate("due_epoch","9223372036854775807");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();mutate("due_epoch","\"01\"");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();mutate("priority","0.5");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();mutate("state","3");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();mutate("generation","4294967296");assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  seed();
  {
    char *end_title;size_t offset;
    disk[s.current_slot][lengths[s.current_slot]]=0;
    end_title=strstr(disk[s.current_slot],"reminder");assert(end_title);
    offset=(size_t)(end_title-disk[s.current_slot])+strlen("reminder");
    memmove(disk[s.current_slot]+offset+12,disk[s.current_slot]+offset,lengths[s.current_slot]-offset);
    memcpy(disk[s.current_slot]+offset,"\\u0000suffix",12);
    lengths[s.current_slot]+=12;
    assert(vg_store_load(&restored,&io)==VG_STORE_CORRUPT);
  }
  seed();
  for(i=0;i<128;i++){finish();r=req(i);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);}
  for(i=1;i<32;i++){r=req(1000+i);assert(vg_store_create(&s,&r,1,true,NULL)==VG_OK);}
  assert(s.history_count==128 && s.active.count==32);
  assert(vg_store_save(&s,&io)==VG_OK);
  printf("Full ordinary snapshot=%u bytes\n",(unsigned)lengths[s.current_slot]);
  /* All fields at maximum byte length with worst-case JSON control escaping. */
  for(i=0;i<160;i++) {
    vg_task_t *task=i<32?&s.active.tasks[i]:&s.history[i-32];
    memset(task->request.title,1,VG_TITLE_MAX_BYTES);task->request.title[VG_TITLE_MAX_BYTES]=0;
    memset(task->request.request_id,1,VG_REQUEST_ID_MAX_BYTES);
    task->request.request_id[VG_REQUEST_ID_MAX_BYTES]=0;
    task->request.request_id[0]=(char)('A'+i/26);
    task->request.request_id[1]=(char)('A'+i%26);
  }
  s.dirty=true;assert(vg_store_save(&s,&io)==VG_OK);
  printf("Full worst-escaped snapshot=%u bytes\n",(unsigned)lengths[s.current_slot]);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==32 && restored.history_count==128);
  assert(!memcmp(&s.active,&restored.active,sizeof(s.active)));
  for(i=0;i<128;i++) assert(!memcmp(&s.history[(s.history_start+i)%128],&restored.history[i],sizeof(vg_task_t)));
  /* REL 也测满容量、最坏 JSON 转义与每个 uint64/事件字段的上界。 */
  s.boot_counter=UINT64_MAX;
  for(i=0;i<160;i++) {
    vg_task_t *task=i<32?&s.active.tasks[i]:&s.history[i-32];
    task->timer_domain=VG_TIMER_RELATIVE;task->request.due_epoch=0;
    task->delay_seconds=VG_RELATIVE_MAX_DELAY_SECONDS;task->mono_deadline_ms=UINT64_MAX;
    task->timer_boot_id=UINT64_MAX;task->timer_revision=UINT64_MAX;
    task->created_epoch=INT64_MAX;task->updated_epoch=INT64_MAX;
    task->snoozed_epoch=INT64_MAX;task->snooze_count=UINT32_MAX;
    task->acknowledged_epoch=task->state==VG_TASK_ACKNOWLEDGED?INT64_MAX:0;
    task->missed_epoch=task->state==VG_TASK_MISSED?INT64_MAX:0;
  }
  s.dirty=true;assert(vg_store_save(&s,&io)==VG_OK);
  printf("Full REL worst-escaped/max-value snapshot=%u bytes\n",(unsigned)lengths[s.current_slot]);
  assert(vg_store_load(&restored,&io)==VG_OK && restored.boot_counter==UINT64_MAX);
  assert(!memcmp(&s.active,&restored.active,sizeof(s.active)));
  for(i=0;i<128;i++)assert(!memcmp(&s.history[(s.history_start+i)%128],&restored.history[i],sizeof(vg_task_t)));
  v3_tests();
  printf("PASS TaskStore: slots, history, restart dedup window, int64, torn write, corruption, generation bounds\n");
  printf("sizeof(vg_store_t)=%u, snapshot limit=%u\n",(unsigned)sizeof(s),VG_STORE_MAX_BYTES);
  return 0;
}
