#include "velaguard_store.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char original[2][VG_STORE_MAX_BYTES],slots[2][VG_STORE_MAX_BYTES];
static size_t original_size[2],sizes[2];static unsigned writes;
static vg_store_t loaded,reloaded;
static int rd(void *ctx,unsigned slot,char *out,size_t cap,size_t *n){(void)ctx;if(!sizes[slot])return 1;assert(cap>=sizes[slot]);memcpy(out,slots[slot],sizes[slot]);*n=sizes[slot];return 0;}
static int wr(void *ctx,unsigned slot,const char *data,size_t n){(void)ctx;assert(n<=sizeof(slots[slot]));memcpy(slots[slot],data,n);sizes[slot]=n;writes++;return 0;}
static vg_store_io_t io={NULL,rd,wr};
static const cJSON *get(const cJSON *o,const char *name){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,name);assert(v);return v;}
static int64_t epoch(const cJSON *o,const char *name){const cJSON *v=get(o,name);assert(cJSON_IsString(v));errno=0;char *end;long long n=strtoll(v->valuestring,&end,10);assert(!errno && !*end && n>=0);return (int64_t)n;}
static void compare_task(const vg_task_t *task,const cJSON *json){
 assert(!strcmp(task->request.request_id,get(json,"request_id")->valuestring));
 assert(!strcmp(task->request.title,get(json,"title")->valuestring));
 assert(task->request.due_epoch==epoch(json,"due_epoch"));
 assert(task->request.priority==get(json,"priority")->valueint && (int)task->state==get(json,"state")->valueint);
 assert(task->created_epoch==epoch(json,"created_epoch") && task->updated_epoch==epoch(json,"updated_epoch"));
 assert(task->acknowledged_epoch==epoch(json,"acknowledged_epoch") && task->snoozed_epoch==epoch(json,"snoozed_epoch") && task->missed_epoch==epoch(json,"missed_epoch"));
 assert(task->snooze_count==(uint32_t)get(json,"snooze_count")->valuedouble);
 assert(task->timer_domain==VG_TIMER_ABSOLUTE && !task->delay_seconds && !task->mono_deadline_ms && !task->timer_boot_id && !task->timer_revision);
}
static void check(const vg_store_t *store,const cJSON *body){
 const cJSON *active=get(body,"active"),*history=get(body,"history");
 assert(store->boot_counter==0 && store->active.count==(size_t)cJSON_GetArraySize(active) && store->history_count==(size_t)cJSON_GetArraySize(history));
 for(size_t i=0;i<store->active.count;i++)compare_task(&store->active.tasks[i],cJSON_GetArrayItem(active,(int)i));
 for(size_t i=0;i<store->history_count;i++)compare_task(&store->history[(store->history_start+i)%VG_HISTORY_CAPACITY],cJSON_GetArrayItem(history,(int)i));
}
static void exercise(int only){
 for(unsigned i=0;i<2;i++){sizes[i]=(only<0 || only==(int)i)?original_size[i]:0;memcpy(slots[i],original[i],sizes[i]);}
 unsigned selected=only==1?1:0;cJSON *old=cJSON_ParseWithLength(original[selected],original_size[selected]);assert(old);
 const cJSON *old_body=get(old,"data");uint32_t generation=(uint32_t)get(old_body,"generation")->valuedouble;
 assert(get(old_body,"version")->valueint==2);writes=0;vg_store_init(&loaded);
 assert(vg_store_load(&loaded,&io)==0 && writes==0 && loaded.dirty && loaded.current_slot==(int)selected && loaded.generation==generation);
 check(&loaded,old_body);if(only<0)assert(loaded.history_count==20 && loaded.active.count==0 && generation==67);
 assert(vg_store_save(&loaded,&io)==0 && writes==1 && !loaded.dirty && loaded.generation==generation+1);
 cJSON *new_root=cJSON_ParseWithLength(slots[loaded.current_slot],sizes[loaded.current_slot]);assert(new_root);
 const cJSON *new_body=get(new_root,"data");assert(get(new_body,"version")->valueint==3);
 const char *extra[]={"timer_domain","delay_seconds","mono_deadline_ms","timer_boot_id","timer_revision"};
 for(unsigned a=0;a<2;a++){
  const cJSON *old_array=get(old_body,a?"history":"active"),*new_array=get(new_body,a?"history":"active");assert(cJSON_GetArraySize(old_array)==cJSON_GetArraySize(new_array));
  for(int i=0;i<cJSON_GetArraySize(old_array);i++){
   cJSON *copy=cJSON_Duplicate(cJSON_GetArrayItem(new_array,i),true);assert(copy);
   for(unsigned k=0;k<5;k++)cJSON_DeleteItemFromObjectCaseSensitive(copy,extra[k]);
   assert(cJSON_Compare(copy,cJSON_GetArrayItem(old_array,i),true));cJSON_Delete(copy);
  }
 }
 assert(vg_store_load(&reloaded,&io)==0 && writes==1 && !reloaded.dirty && reloaded.generation==generation+1);
 check(&reloaded,old_body);assert(!memcmp(&loaded.active,&reloaded.active,sizeof(loaded.active)));
 assert(!memcmp(loaded.history,reloaded.history,sizeof(loaded.history)));
 printf("PASS board fixture mode=%d selected_slot=%u generation=%u->%u active=%u history=%u all legacy fields and order preserved; original files read-only\n",only,selected,generation,generation+1,(unsigned)reloaded.active.count,(unsigned)reloaded.history_count);
 cJSON_Delete(new_root);cJSON_Delete(old);
}
int main(int argc,char **argv){assert(argc==3);for(unsigned i=0;i<2;i++){FILE *f=fopen(argv[i+1],"rb");assert(f);original_size[i]=fread(original[i],1,sizeof(original[i]),f);assert(original_size[i]>0 && original_size[i]<sizeof(original[i]) && feof(f) && !ferror(f));assert(fclose(f)==0);}exercise(-1);exercise(0);exercise(1);return 0;}