/* SPDX-License-Identifier: Apache-2.0 */
#include "velaguard_skill_loader.h"
#ifdef VG_SKILL_HOST_TEST
#define CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR "."
#endif
#include "agent_config.h"
#include "agent_compat.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

#define VG_SUMMARY_MAX 8192u
static bool vg_format_truncated;
static uint32_t vg_generation;
static size_t vg_enumerated;
static bool vg_found;
static bool vg_collect;
static FILE *vg_first_line;
static bool vg_first_is_velaguard;
static int vg_io_error;

/* 只改返回值语义，防止官方 summary 累计 would-have-written 后越界。 */
static int vg_bounded_snprintf(char *out, size_t cap, const char *format, ...)
{
  va_list ap; va_start(ap,format);
  int n=vsnprintf(out,cap,format,ap); va_end(ap);
  if(n<0) { vg_io_error=EIO; if(cap) out[0]=0; return 0; }
  if((size_t)n>=cap) {
    vg_format_truncated=true;
    /* 路径截断时清空路径，绝不打开截断后指向的另一文件。 */
    if(strcmp(format,"%s%s")==0 || strcmp(format,"%s%s.md")==0) {
      if(cap) out[0]=0;
      return 0;
    }
    return cap ? (int)(cap-1) : 0;
  }
  return n;
}

/* 有界受控普通文件；打开前避免 FIFO，打开后重新核对，正文拒绝 NUL。 */
static FILE *vg_readonly_fopen(const char *path,const char *mode)
{
  struct stat st;
  if(strcmp(mode,"r")!=0) { errno=EROFS; return NULL; }
#ifdef _WIN32
  DWORD attr=GetFileAttributesA(path);
  if(attr==INVALID_FILE_ATTRIBUTES) { errno=ENOENT; return NULL; }
  if(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) { errno=EINVAL; return NULL; }
#else
  if(lstat(path,&st)!=0) return NULL;
  if(!S_ISREG(st.st_mode)) { errno=EINVAL; return NULL; }
#endif
  int flags=O_RDONLY;
#ifdef O_NONBLOCK
  flags|=O_NONBLOCK;
#endif
#ifdef O_NOFOLLOW
  flags|=O_NOFOLLOW;
#endif
#ifdef O_BINARY
  flags|=O_BINARY;
#endif
  int fd=open(path,flags);
  if(fd<0) return NULL;
  if(fstat(fd,&st)!=0) { int e=errno; close(fd); errno=e; return NULL; }
  if(!S_ISREG(st.st_mode) || st.st_size<0) { close(fd); errno=EINVAL; return NULL; }
  if((uint64_t)st.st_size>VG_SKILL_BODY_MAX) { close(fd); errno=EFBIG; return NULL; }
  FILE *f=fdopen(fd,"rb"); if(!f) { int e=errno; close(fd); errno=e; return NULL; }
  size_t scanned=0; int c;
  while((c=fgetc(f))!=EOF) {
    if(c==0 || ++scanned>VG_SKILL_BODY_MAX) { fclose(f); errno=c==0 ? EILSEQ : EFBIG; return NULL; }
  }
  if(ferror(f)) { fclose(f); errno=EIO; vg_io_error=EIO; return NULL; }
  rewind(f);
  if(vg_collect) {
    vg_first_line=f;
    vg_first_is_velaguard=strcmp(path,AGENT_SKILLS_DIR "velaguard.md")==0;
  }
  return f;
}
static char *vg_counted_fgets(char *s,int size,FILE *f)
{
  char *r=fgets(s,size,f);
  if(ferror(f)) vg_io_error=EIO;
  if(vg_collect && f==vg_first_line) {
    vg_first_line=NULL;
    if(r) { vg_enumerated++; if(vg_first_is_velaguard) vg_found=true; }
  }
  return r;
}
static int vg_no_mkdir(const char *p,unsigned mode) { (void)p; (void)mode; errno=EROFS; return -1; }
static void vg_loader_registry_invalidate(void) { vg_generation++; }

/* 引入官方实际实现；公共符号隔离，不调用会安装 builtin 的 init。 */
#define skill_loader_init vg_skill_upstream_unused_init
#define skill_loader_build_summary vg_skill_upstream_summary
#define skill_loader_check_changed vg_skill_upstream_check_changed
#define skill_loader_refresh vg_skill_upstream_refresh
#define tool_registry_invalidate vg_loader_registry_invalidate
#define snprintf vg_bounded_snprintf
#define fopen vg_readonly_fopen
#define fgets vg_counted_fgets
#define mkdir vg_no_mkdir
#include "tools/skill_loader.c"
#undef mkdir
#undef fgets
#undef fopen
#undef snprintf
#undef tool_registry_invalidate
#undef skill_loader_refresh
#undef skill_loader_check_changed
#undef skill_loader_build_summary
#undef skill_loader_init

int vg_skill_loader_probe(vg_skill_loader_report_t *report,char *summary,size_t capacity)
{
  if(!report || !summary || !capacity) return -EINVAL;
  memset(report,0,sizeof(*report)); summary[0]=0;
  DIR *dir=opendir(AGENT_SKILLS_DIR);
  if(!dir) return -errno;
  closedir(dir);
  char *scratch=malloc(VG_SUMMARY_MAX);
  if(!scratch) return -ENOMEM;
  vg_enumerated=0; vg_found=false; vg_first_line=NULL;
  vg_format_truncated=false; vg_io_error=0; vg_collect=true;
  size_t bytes=vg_skill_upstream_summary(scratch,VG_SUMMARY_MAX);
  vg_collect=false;
  if(vg_io_error) { free(scratch); return -vg_io_error; }
  size_t copy=bytes<capacity-1 ? bytes : capacity-1;
  memcpy(summary,scratch,copy); summary[copy]=0;
  report->summary_bytes=copy;
  report->truncated=vg_format_truncated || copy!=bytes;
  report->skill_count=vg_enumerated;
  report->velaguard_found=vg_found;
  vg_skill_upstream_refresh();
  report->refresh_generation=vg_generation;
  free(scratch); return 0;
}
int vg_skill_loader_read(const char *name,char *body,size_t capacity,size_t *body_bytes)
{
  if(body_bytes) *body_bytes=0;
  if(!name || !body || !capacity || !body_bytes) return -EINVAL;
  body[0]=0;
  size_t len=strlen(name);
  if(len==0 || len>63) return -EINVAL;
  for(size_t i=0;i<len;i++) {
    char c=name[i];
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return -EINVAL;
  }
  char path[256];
  int n=snprintf(path,sizeof(path),"%s%s.md",AGENT_SKILLS_DIR,name);
  if(n<0 || (size_t)n>=sizeof(path)) return -ENAMETOOLONG;
  FILE *f=vg_readonly_fopen(path,"r"); if(!f) return -errno;
  size_t count=fread(body,1,capacity-1,f);
  int extra=fgetc(f); bool failed=ferror(f); fclose(f);
  if(failed || extra!=EOF) { body[0]=0; return failed ? -EIO : -ENOSPC; }
  body[count]=0; *body_bytes=count; return 0;
}