#include "velaguard_store_file.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
# include <process.h>
#else
# include <unistd.h>
#endif

/* 本测试模型只覆盖调用序列，真实文件测试由独立进程分支完成。 */
static struct
{
  char data[2][VG_STORE_MAX_BYTES];
  size_t length[2], position[2];
  int exists[2];
  int short_io, open_eintr, read_eintr, write_eintr, sync_eintr;
  int read_error, write_error, sync_error, close_error, parent_error;
  int zero_write, writes, syncs, closes, parents, directory_error;
} model;

static int fake_open(void *ctx, const char *path, int flags, unsigned mode)
{
  unsigned slot=strstr(path,"slot0.json")?0:1;
  (void)ctx; (void)mode;
  if(model.open_eintr) {model.open_eintr=0;errno=EINTR;return -1;}
  if(!model.exists[slot] && !(flags&O_CREAT)) {errno=ENOENT;return -1;}
  model.exists[slot]=1;model.position[slot]=0;
  if(flags&O_TRUNC) model.length[slot]=0;
  return (int)slot+10;
}
static ptrdiff_t fake_read(void *ctx,int fd,void *buf,size_t n)
{
  unsigned slot=(unsigned)(fd-10);size_t available;
  (void)ctx;
  if(model.read_eintr) {model.read_eintr=0;errno=EINTR;return -1;}
  if(model.read_error) {errno=EIO;return -1;}
  available=model.length[slot]-model.position[slot];
  if(n>available) n=available;
  if(model.short_io && n>3) n=3;
  memcpy(buf,model.data[slot]+model.position[slot],n);model.position[slot]+=n;
  return (ptrdiff_t)n;
}
static ptrdiff_t fake_write(void *ctx,int fd,const void *buf,size_t n)
{
  unsigned slot=(unsigned)(fd-10); (void)ctx;model.writes++;
  if(model.write_eintr) {model.write_eintr=0;errno=EINTR;return -1;}
  if(model.write_error && model.position[slot]>=3) {errno=EIO;return -1;}
  if(model.zero_write) return 0;
  if(model.short_io && n>3) n=3;
  assert(model.position[slot]+n<=VG_STORE_MAX_BYTES);
  memcpy(model.data[slot]+model.position[slot],buf,n);model.position[slot]+=n;
  model.length[slot]=model.position[slot];return (ptrdiff_t)n;
}
static int fake_sync(void *ctx,int fd)
{
  (void)ctx;(void)fd;model.syncs++;
  if(model.sync_eintr) {model.sync_eintr=0;errno=EINTR;return -1;}
  if(model.sync_error) {errno=EIO;return -1;}return 0;
}
static int fake_close(void *ctx,int fd)
{
  (void)ctx;(void)fd;model.closes++;
  if(model.close_error) {errno=EINTR;return -1;}return 0;
}
static int fake_parent(void *ctx,const char *dir)
{
  (void)ctx;(void)dir;model.parents++;
  if(model.parent_error) {errno=EIO;return -1;}return 0;
}
static int fake_directory(void *ctx,const char *dir)
{
  (void)ctx;(void)dir;
  if(model.directory_error) {errno=ENOENT;return -1;}return 0;
}
static vg_store_file_ops_t fake={NULL,fake_open,fake_read,fake_write,fake_sync,fake_close,fake_parent,fake_directory};
static vg_store_file_t file;
static vg_store_t store, restored, before;
static vg_create_request_t request(const char *id)
{
  vg_create_request_t r={0};strcpy(r.request_id,id);strcpy(r.title,"offline reminder");
  r.due_epoch=INT64_MAX;r.priority=1;return r;
}
static void model_tests(void)
{
  vg_store_io_t io;char buf[16];size_t n=99;int calls;vg_create_request_t r;
  assert(vg_store_file_init(&file,"fake",&fake)==VG_OK);io=vg_store_file_io(&file);
  assert(io.read(io.context,0,buf,sizeof(buf),&n)==1);
  model.directory_error=1;assert(io.read(io.context,0,buf,sizeof(buf),&n)<0);model.directory_error=0;
  model.exists[0]=1;assert(io.read(io.context,0,buf,sizeof(buf),&n)==0 && n==0);
  model.short_io=1;model.open_eintr=model.write_eintr=model.sync_eintr=1;
  assert(io.write_sync(io.context,0,"123456789",9)==0);
  assert(model.length[0]==9 && model.writes>3 && model.syncs==2 && model.parents==1);
  model.read_eintr=1;
  assert(io.read(io.context,0,buf,sizeof(buf),&n)==0 && n==9 && !memcmp(buf,"123456789",9));
  assert(io.read(io.context,0,buf,9,&n)==0 && n==9);
  assert(io.read(io.context,0,buf,8,&n)<0);
  model.close_error=1;n=99;calls=model.closes;
  assert(io.read(io.context,0,buf,sizeof(buf),&n)<0 && n==99 && model.closes==calls+1);model.close_error=0;
  model.read_error=1;assert(io.read(io.context,0,buf,sizeof(buf),&n)<0);model.read_error=0;
  model.zero_write=1;assert(io.write_sync(io.context,0,"test",4)<0);model.zero_write=0;
  model.sync_error=1;assert(io.write_sync(io.context,0,"test",4)<0);model.sync_error=0;
  model.close_error=1;calls=model.closes;
  assert(io.write_sync(io.context,0,"test",4)<0 && model.closes==calls+1);model.close_error=0;
  model.parent_error=1;assert(io.write_sync(io.context,0,"test",4)<0);model.parent_error=0;
  assert(io.write_sync(io.context,2,"test",4)<0);
  assert(io.write_sync(io.context,0,"test",0)<0);
  memset(&model,0,sizeof(model));vg_store_init(&store);
  assert(vg_store_load(&store,&io)==VG_OK);
  r=request("survivor");assert(vg_store_create(&store,&r,1,true,NULL)==VG_OK);
  assert(vg_store_save(&store,&io)==VG_OK);
  r=request("torn");assert(vg_store_create(&store,&r,1,true,NULL)==VG_OK);
  model.short_io=1;model.write_error=1;
  assert(vg_store_save(&store,&io)==VG_STORE_IO_ERROR && store.dirty);
  model.write_error=0;assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==1 && restored.dirty);
  model.parent_error=1;
  assert(vg_store_save(&store,&io)==VG_STORE_IO_ERROR && store.dirty);model.parent_error=0;
  assert(vg_store_load(&restored,&io)==VG_OK && restored.active.count==2);
  before=restored;model.read_error=1;
  assert(vg_store_load(&restored,&io)==VG_STORE_IO_ERROR && !memcmp(&restored,&before,sizeof(before)));
  puts("PASS injected file adapter: ENOENT/empty, short IO/EINTR, size, sync/close/parent failures, torn slot recovery");
}
/* Windows CRT没有目录屏障：仅进程恢复测试显式模拟该能力。 */
static int process_test_parent(void *ctx,const char *dir)
{
  (void)ctx;(void)dir;return 0;
}
static int native_inputs(const char *dir)
{
  vg_store_file_ops_t native;vg_store_io_t io;
  char path[VG_STORE_FILE_PATH_CAPACITY],block[4096]={0};
  char *large=malloc(VG_STORE_MAX_BYTES);size_t n,i;int fd;
  assert(large);vg_store_file_native_ops(&native);
  snprintf(path,sizeof(path),"%s/missing-parent",dir);
  assert(vg_store_file_init(&file,path,&native)==VG_STORE_IO_ERROR);
  assert(vg_store_file_init(&file,dir,&native)==VG_OK);io=vg_store_file_io(&file);
  assert(io.read(io.context,0,large,VG_STORE_MAX_BYTES,&n)==1);
  snprintf(path,sizeof(path),"%s/slot0.json",dir);
  fd=native.open_file(NULL,path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
  assert(native.close_file(NULL,fd)==0);
  assert(io.read(io.context,0,large,VG_STORE_MAX_BYTES,&n)==0 && n==0);
  fd=native.open_file(NULL,path,O_WRONLY,0600);assert(fd>=0);
  for(i=0;i<VG_STORE_MAX_BYTES/sizeof(block);i++)
    assert(native.write_file(NULL,fd,block,sizeof(block))==(ptrdiff_t)sizeof(block));
  assert(native.close_file(NULL,fd)==0);
  assert(io.read(io.context,0,large,VG_STORE_MAX_BYTES,&n)==0 && n==VG_STORE_MAX_BYTES);
  fd=native.open_file(NULL,path,O_WRONLY|O_APPEND,0600);assert(fd>=0);
  assert(native.write_file(NULL,fd,"!",1)==1);assert(native.close_file(NULL,fd)==0);
  assert(io.read(io.context,0,large,VG_STORE_MAX_BYTES,&n)<0);
#ifdef _WIN32
  assert(io.write_sync(io.context,1,"complete bytes",14)<0);
  assert(errno==ENOTSUP);
#endif
  free(large);
  puts("PASS native files: missing parent/slot, empty, exact limit, oversized; default Windows namespace barrier rejects");
  return 0;
}
static int process_test(const char *phase,const char *dir)
{
  vg_store_file_ops_t native;vg_store_io_t io;vg_create_request_t r;
  if(!strcmp(phase,"inputs")) return native_inputs(dir);
  vg_store_file_native_ops(&native);
  native.sync_parent=process_test_parent;
  assert(vg_store_file_init(&file,dir,&native)==VG_OK);io=vg_store_file_io(&file);
  vg_store_init(&store);assert(vg_store_load(&store,&io)==VG_OK);
  if(!strcmp(phase,"save"))
    {
      assert(store.active.count==0 && store.history_count==0);
      r=request("history-id");assert(vg_store_create(&store,&r,1,true,NULL)==VG_OK);
      assert(vg_store_transition(&store,0,VG_TASK_SCHEDULED)==VG_OK);
      assert(vg_store_transition(&store,0,VG_TASK_ALERTING)==VG_OK);
      assert(vg_store_transition(&store,0,VG_TASK_ACKNOWLEDGED)==VG_OK);
      r=request("active-id");assert(vg_store_create(&store,&r,1,true,NULL)==VG_OK);
      assert(vg_store_save(&store,&io)==VG_OK);
      puts("PASS process save: native file writes+file sync; namespace barrier explicitly simulated");
    }
  else if(!strcmp(phase,"load"))
    {
      assert(store.active.count==1 && store.history_count==1);
      r=request("history-id");assert(vg_store_create(&store,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
      r=request("active-id");assert(vg_store_create(&store,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
      puts("PASS separate process load: active/history retained and deduplicated");
    }
  else if(!strcmp(phase,"tear"))
    {
      char path[VG_STORE_FILE_PATH_CAPACITY];int fd;
      assert(store.current_slot==0);
      snprintf(path,sizeof(path),"%s/slot1.json",dir);
      fd=native.open_file(NULL,path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
      assert(native.write_file(NULL,fd,"{\"data\":",8)==8);
      puts("Process exits after partial inactive-slot write, before file sync/close");
      fflush(stdout);_exit(0);
    }
  else if(!strcmp(phase,"recover"))
    {
      assert(store.active.count==1 && store.history_count==1 && store.dirty);
      r=request("history-id");assert(vg_store_create(&store,&r,1,true,NULL)==VG_ERR_DUPLICATE_REQUEST);
      assert(vg_store_save(&store,&io)==VG_OK && !store.dirty);
      puts("PASS separate process recovery: old complete slot retained, corrupt slot repaired");
    }
  else return 2;
  return 0;
}
int main(int argc,char **argv)
{
  if(argc==3) return process_test(argv[1],argv[2]);
  model_tests();return 0;
}
