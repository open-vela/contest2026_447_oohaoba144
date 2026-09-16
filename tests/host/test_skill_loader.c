#define _GNU_SOURCE
#include "velaguard_skill_loader.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
static void put(const char *path, const char *data) {
  FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(data,1,strlen(data),f)==strlen(data)); assert(fclose(f)==0);
}
int main(int argc, char **argv) {
  assert(argc==2); const char *root=argv[1]; assert(chdir(root)==0);
  printf("fixture retained: %s\n",root);
  vg_skill_loader_report_t r; char body[9000],summary[20000]; size_t n=999;
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==-ENOENT);
  assert(vg_skill_loader_read("velaguard",body,sizeof(body),&n)==-ENOENT && n==0);
  assert(mkdir("skills",0700)==0);
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==0 && r.skill_count==0 && summary[0]==0);
  const char *original="# VelaGuard\nOffline reminder adapter.\n\nUse task.create. 中文提醒\n";
  put("skills/velaguard.md",original);
  put("skills/.hidden.md","# Hidden\nHidden\n"); put("skills/ignore.txt","# Ignore\n");
  assert(mkdir("skills/directory.md",0700)==0);
  put("outside.md","# Outside\nDo not load\n");
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==0);
  assert(r.skill_count==1 && r.velaguard_found && !r.truncated);
  assert(strstr(summary,"**VelaGuard**") && strstr(summary,"Offline reminder adapter."));
  assert(!strstr(summary,"Hidden") && !strstr(summary,"Outside"));
  assert(r.summary_bytes==strlen(summary) && r.refresh_generation>0);
  assert(vg_skill_loader_read("velaguard",body,sizeof(body),&n)==0);
  assert(n==strlen(original) && strcmp(body,original)==0);
  assert(vg_skill_loader_read("../outside",body,sizeof(body),&n)==-EINVAL);
  assert(vg_skill_loader_read("escape",body,sizeof(body),&n)<0);
  assert(vg_skill_loader_read("velaguard",body,4,&n)==-ENOSPC && body[0]==0);
  assert(vg_skill_loader_read("velaguard",NULL,0,&n)==-EINVAL);
  assert(vg_skill_loader_probe(&r,NULL,0)==-EINVAL);
  /* 各小容量保持两端哨兵，必须只写 capacity 内。 */
  for(size_t cap=1;cap<300;cap++) {
    unsigned char *p=malloc(cap+2); assert(p); memset(p,0xa5,cap+2);
    assert(vg_skill_loader_probe(&r,(char*)p+1,cap)==0);
    assert(p[0]==0xa5 && p[cap+1]==0xa5 && memchr(p+1,0,cap));
    assert(r.summary_bytes<cap);
    if(cap==1) assert(r.truncated);
    free(p);
  }
  memset(body,'x',VG_SKILL_BODY_MAX); body[VG_SKILL_BODY_MAX]=0; put("skills/large.md",body);
  assert(vg_skill_loader_read("large",summary,sizeof(summary),&n)==0 && n==VG_SKILL_BODY_MAX);
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==0 && r.skill_count==2);
  body[VG_SKILL_BODY_MAX]='x'; body[VG_SKILL_BODY_MAX+1]=0; put("skills/huge.md",body);
  assert(vg_skill_loader_read("huge",summary,sizeof(summary),&n)==-EFBIG);
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==0 && r.skill_count==2);
    FILE *binary=fopen("skills/binary.md","wb"); assert(binary);
  const char nuldata[]={'#',' ','N','\0','X'};
  assert(fwrite(nuldata,1,sizeof(nuldata),binary)==sizeof(nuldata)); fclose(binary);
  assert(vg_skill_loader_read("binary",summary,sizeof(summary),&n)==-EILSEQ);
  put("skills/empty.md","");
  assert(vg_skill_loader_probe(&r,summary,sizeof(summary))==0 && r.skill_count==2);
  /* 原文件保留，probe 没有安装 builtin 或覆盖正文。 */
  assert(access("skills/weather.md",F_OK)!=0);
  assert(vg_skill_loader_read("velaguard",body,sizeof(body),&n)==0 && strcmp(body,original)==0);
  /* 40 条长标题/描述必定填满真实官方 8192 字节内部摘要。 */
  char title[64],desc[201],long_skill[300],path[64];
  memset(title,'T',63); title[63]=0;
  memset(desc,'D',200); desc[200]=0;
  assert(snprintf(long_skill,sizeof(long_skill),"# %s\n%s\n\n",title,desc)>0);
  for(unsigned i=0;i<40;i++) {
    assert(snprintf(path,sizeof(path),"skills/overflow-%02u.md",i)>0);
    put(path,long_skill);
  }
  unsigned char *guard=malloc(20002); assert(guard); memset(guard,0xa5,20002);
  puts("ENTER real upstream internal-summary overflow boundary (40 long skills, outer capacity=20000)"); fflush(stdout);
  assert(vg_skill_loader_probe(&r,(char*)guard+1,20000)==0);
  printf("internal summary bytes=%zu, truncated=%d, enumerated=%zu\n",r.summary_bytes,(int)r.truncated,r.skill_count); fflush(stdout);
  assert(r.truncated);
  assert(r.summary_bytes<8192);
  assert(r.summary_bytes==8191);
  assert(r.skill_count>1 && r.skill_count<42);
  assert(guard[0]==0xa5 && guard[20001]==0xa5);
  assert(strlen((char*)guard+1)==r.summary_bytes);
  for(size_t i=r.summary_bytes+2;i<20002;i++) assert(guard[i]==0xa5);
  free(guard);
  puts("PASS actual official loader summary/enumeration; internal 8192-byte overflow boundary; team body adapter; tiny buffers; missing/oversized/nonregular/NUL files; no builtin writes");
  return 0;
}