#include "velaguard_store.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __NuttX__
# include <netutils/cJSON.h>
#else
# include "cJSON.h"
#endif
static bool bounded(const char *s, size_t cap)
{
  return s && s[0] && memchr(s,0,cap)!=NULL;
}
static bool terminal(vg_task_state_t state)
{
  return state==VG_TASK_ACKNOWLEDGED || state==VG_TASK_MISSED;
}
static bool identity_valid(const vg_create_request_t *r)
{
  return r && bounded(r->request_id,sizeof(r->request_id)) &&
  bounded(r->title,sizeof(r->title)) &&
  r->priority>=0 && r->priority<=2;
}
static bool request_valid(const vg_create_request_t *r)
{return identity_valid(r) && r->due_epoch>0;}
static bool known_before(int64_t a,int64_t b){return a!=0 && b!=0 && a<b;}
static bool task_valid(const vg_task_t *a,uint64_t boot)
{
 bool rel=a->timer_domain==VG_TIMER_RELATIVE;
 if(!identity_valid(&a->request) || a->state<VG_TASK_CREATED || a->state>VG_TASK_NEEDS_RESET)return false;
 if(rel) {
  if(a->request.due_epoch!=0 || !a->delay_seconds || a->delay_seconds>VG_RELATIVE_MAX_DELAY_SECONDS ||
     a->mono_deadline_ms==0 || !a->timer_boot_id || a->timer_boot_id>boot || !a->timer_revision)return false;
 } else if(a->timer_domain!=VG_TIMER_ABSOLUTE || a->request.due_epoch<=0 ||
           a->delay_seconds || a->mono_deadline_ms || a->timer_boot_id || a->timer_revision || a->state==VG_TASK_NEEDS_RESET)return false;
 if(a->created_epoch<0 || a->updated_epoch<0 || a->acknowledged_epoch<0 || a->snoozed_epoch<0 || a->missed_epoch<0 ||
    (a->acknowledged_epoch!=0 && a->state!=VG_TASK_ACKNOWLEDGED) ||
    (a->missed_epoch!=0 && a->state!=VG_TASK_MISSED))return false;
 if(rel) {
  if(known_before(a->updated_epoch,a->created_epoch) || known_before(a->updated_epoch,a->acknowledged_epoch) ||
     known_before(a->updated_epoch,a->snoozed_epoch) || known_before(a->updated_epoch,a->missed_epoch) ||
     known_before(a->acknowledged_epoch,a->created_epoch) || known_before(a->snoozed_epoch,a->created_epoch) ||
     known_before(a->missed_epoch,a->created_epoch) || (a->snooze_count==0 && a->snoozed_epoch!=0))return false;
 } else if(a->updated_epoch<a->created_epoch || a->acknowledged_epoch>a->updated_epoch ||
           a->snoozed_epoch>a->updated_epoch || a->missed_epoch>a->updated_epoch ||
           known_before(a->acknowledged_epoch,a->created_epoch) || known_before(a->snoozed_epoch,a->created_epoch) ||
           known_before(a->missed_epoch,a->created_epoch) || ((a->snooze_count==0)!=(a->snoozed_epoch==0)))return false;
 return true;
}
static const vg_task_t *hist(const vg_store_t *s,size_t i)
{
  return &s->history[(s->history_start+i)%VG_HISTORY_CAPACITY];
}
static bool valid(const vg_store_t *s)
{
  size_t i,j,total;
  const vg_task_t *a,*b;
  if (!s || s->active.count>VG_MAX_ACTIVE_TASKS ||
  s->history_count>VG_HISTORY_CAPACITY || s->history_start>=VG_HISTORY_CAPACITY ||
  s->current_slot < -1 || s->current_slot>1) return false;
  total=s->active.count+s->history_count;
  for(i=0;i<total;i++)
  {
    a=i<s->active.count?&s->active.tasks[i]:hist(s,i-s->active.count);
    if(!task_valid(a,s->boot_counter) || terminal(a->state)!=(i>=s->active.count))return false;
    for(j=0;j<i;j++)
    {
      b=j<s->active.count?&s->active.tasks[j]:hist(s,j-s->active.count);
      if (!strcmp(a->request.request_id,b->request.request_id)) return false;
    }
  }
  return true;
}
void vg_store_init(vg_store_t *s)
{
  if(s)
  {
    memset(s,0,sizeof(*s));
    s->current_slot=-1;
  }
}
int vg_store_create(vg_store_t *s,const vg_create_request_t *r,int64_t now,bool rtc,size_t *index)
{
  size_t i;
  int rc;
  if(!valid(s) || !request_valid(r)) return VG_ERR_INVALID_MESSAGE;
  for(i=0;i<s->history_count;i++)
  if(!strcmp(hist(s,i)->request.request_id,r->request_id)) return VG_ERR_DUPLICATE_REQUEST;
  rc=vg_core_create(&s->active,r,now,rtc,index);
  if(rc==VG_OK) s->dirty=true;
  return rc;
}
int vg_store_advance_boot(vg_store_t *s)
{
 if(!valid(s))return VG_ERR_INVALID_MESSAGE;
 if(s->boot_counter==UINT64_MAX)return VG_ERR_CAPACITY;
 s->boot_counter++;s->dirty=true;return VG_OK;
}
int vg_store_create_relative(vg_store_t *s,const vg_create_request_t *r,
                              uint32_t delay,uint64_t now,size_t *index)
{
 if(!valid(s) || !identity_valid(r) || r->due_epoch!=0 || !s->boot_counter ||
    !delay || delay>VG_RELATIVE_MAX_DELAY_SECONDS)return VG_ERR_INVALID_MESSAGE;
 for(size_t i=0;i<s->active.count;i++)if(!strcmp(s->active.tasks[i].request.request_id,r->request_id)) {
  if(index)*index=i;
  return VG_ERR_DUPLICATE_REQUEST;
 }
 for(size_t i=0;i<s->history_count;i++)if(!strcmp(hist(s,i)->request.request_id,r->request_id))return VG_ERR_DUPLICATE_REQUEST;
 uint64_t delta=(uint64_t)delay*1000u;
 if(now>UINT64_MAX-delta)return VG_ERR_INVALID_TIME;
 if(s->active.count>=VG_MAX_ACTIVE_TASKS)return VG_ERR_CAPACITY;
 size_t pos=s->active.count;vg_task_t *task=&s->active.tasks[pos];memset(task,0,sizeof(*task));
 task->request=*r;task->state=VG_TASK_CREATED;task->timer_domain=VG_TIMER_RELATIVE;
 task->delay_seconds=delay;task->mono_deadline_ms=now+delta;
 task->timer_boot_id=s->boot_counter;task->timer_revision=1;
 s->active.count++;s->dirty=true;if(index)*index=pos;return VG_OK;
}
int vg_store_transition(vg_store_t *s,size_t index,vg_task_state_t next)
{
  size_t slot;
  int rc;
  if(!valid(s)) return VG_ERR_INVALID_MESSAGE;
  rc=vg_core_transition(&s->active,index,next);
  if(rc!=VG_OK) return rc;
  if(terminal(next))
  {
    slot=(s->history_start+s->history_count)%VG_HISTORY_CAPACITY;
    s->history[slot]=s->active.tasks[index];
    if(s->history_count==VG_HISTORY_CAPACITY)
    s->history_start=(s->history_start+1)%VG_HISTORY_CAPACITY;
    else s->history_count++;
    s->active.count--;
    memmove(&s->active.tasks[index],&s->active.tasks[index+1],
    (s->active.count-index)*sizeof(vg_task_t));
    memset(&s->active.tasks[s->active.count],0,sizeof(vg_task_t));
  }
  s->dirty=true;
  return VG_OK;
}
static uint32_t crc32(const char *s)
{
  uint32_t crc=UINT32_MAX;
  unsigned j;
  while(*s)
  {
    crc^=(unsigned char)*s++;
    for(j=0;j<8;j++) crc=(crc>>1)^((crc&1)?0xedb88320u:0);
  }
  return crc^UINT32_MAX;
}
static bool add_u64(cJSON *o, const char *name, uint64_t epoch)
{
  char text[21];
  uint64_t value = (uint64_t)epoch;
  size_t n = 20;
  text[n] = 0;
  do
    {
      text[--n] = (char)('0' + value % 10);
      value /= 10;
    }
  while (value);
  return cJSON_AddStringToObject(o, name, text + n) != NULL;
}

static bool add_epoch(cJSON *o,const char *name,int64_t epoch)
{return add_u64(o,name,(uint64_t)epoch);}
static cJSON *task_json(const vg_task_t *t)
{
  cJSON *o = cJSON_CreateObject();
  /* 时间戳用十进制字符串，完整保留 int64 精度。 */
  if (!o || !cJSON_AddStringToObject(o, "request_id", t->request.request_id) ||
      !cJSON_AddStringToObject(o, "title", t->request.title) ||
      !add_epoch(o, "due_epoch", t->request.due_epoch) ||
      !cJSON_AddNumberToObject(o, "priority", t->request.priority) ||
      !cJSON_AddNumberToObject(o, "state", t->state) ||
      !add_epoch(o, "created_epoch", t->created_epoch) ||
      !add_epoch(o, "updated_epoch", t->updated_epoch) ||
      !add_epoch(o, "acknowledged_epoch", t->acknowledged_epoch) ||
      !add_epoch(o, "snoozed_epoch", t->snoozed_epoch) ||
      !add_epoch(o, "missed_epoch", t->missed_epoch) ||
      !cJSON_AddNumberToObject(o, "snooze_count", t->snooze_count) ||
      !cJSON_AddNumberToObject(o,"timer_domain",t->timer_domain) ||
      !cJSON_AddNumberToObject(o,"delay_seconds",t->delay_seconds) ||
      !add_u64(o,"mono_deadline_ms",t->mono_deadline_ms) ||
      !add_u64(o,"timer_boot_id",t->timer_boot_id) ||
      !add_u64(o,"timer_revision",t->timer_revision))
    {
      cJSON_Delete(o);
      return NULL;
    }
  return o;
}
static char *encode(const vg_store_t *s,uint32_t generation)
{
  cJSON *root=cJSON_CreateObject(),*body=NULL,*active,*history,*t;
  char *canonical=NULL,*output=NULL,crc[9];
  size_t i;
  if(!root) return NULL;
  body=cJSON_AddObjectToObject(root,"data");
  if(!body) goto out;
  if(!cJSON_AddNumberToObject(body,"version",VG_STORE_VERSION) ||
  !cJSON_AddNumberToObject(body,"generation",generation) ||
  !add_u64(body,"boot_counter",s->boot_counter)) goto out;
  active=cJSON_AddArrayToObject(body,"active");
  history=cJSON_AddArrayToObject(body,"history");
  if(!active || !history) goto out;
  for(i=0;i<s->active.count;i++)
  {
    t=task_json(&s->active.tasks[i]);
    if(!t) goto out;
    if(!cJSON_AddItemToArray(active,t))
    {
      cJSON_Delete(t);
      goto out;
    }
  }
  for(i=0;i<s->history_count;i++)
  {
    t=task_json(hist(s,i));
    if(!t) goto out;
    if(!cJSON_AddItemToArray(history,t))
    {
      cJSON_Delete(t);
      goto out;
    }
  }
  canonical=cJSON_PrintUnformatted(body);
  if(!canonical) goto out;
  snprintf(crc,sizeof(crc),"%08x",(unsigned)crc32(canonical));
  if(!cJSON_AddStringToObject(root,"checksum",crc)) goto out;
  output=cJSON_PrintUnformatted(root);
  if(output && strlen(output)>VG_STORE_MAX_BYTES)
  {
    cJSON_free(output);
    output=NULL;
  }
  out:
  cJSON_free(canonical);
  cJSON_Delete(root);
  return output;
}
/* 精确字段集合拒绝未知字段和重复键，防止不同读取方产生歧义。 */
static bool keys(const cJSON *o,const char *const *names,size_t count)
{
  size_t i;
  const cJSON *p,*q;
  if(!cJSON_IsObject(o) || (size_t)cJSON_GetArraySize(o)!=count) return false;
  for(p=o->child;p;p=p->next)
  {
    if(!p->string) return false;
    for(i=0;i<count;i++) if(!strcmp(p->string,names[i])) break;
    if(i==count) return false;
    for(q=o->child;q!=p;q=q->next) if(!strcmp(q->string,p->string)) return false;
  }
  return true;
}
static bool integer(const cJSON *o,uint32_t max,uint32_t *out)
{
  double d;
  if(!cJSON_IsNumber(o)) return false;
  d=o->valuedouble;
  if(!(d>=0 && d<=(double)max)) return false;
  *out=(uint32_t)d;
  return (double)*out==d;
}
static bool string_copy(char *dst,size_t cap,const cJSON *o)
{
  size_t n;
  if(!cJSON_IsString(o) || !o->valuestring) return false;
  n=strlen(o->valuestring);
  if(!n || n>=cap) return false;
  memcpy(dst,o->valuestring,n+1);
  return true;
}
static bool parse_u64(const cJSON *o, bool allow_zero, uint64_t *out)
{
  const char *p;
  uint64_t value = 0;
  if (!cJSON_IsString(o) || !o->valuestring || !o->valuestring[0]) return false;
  p = o->valuestring;
  if (p[0] == '0' && (p[1] != 0 || !allow_zero)) return false;
  for (; *p; p++)
    {
      if (*p < '0' || *p > '9' ||
          value > (UINT64_MAX - (unsigned)(*p - '0')) / 10) return false;
      value = value * 10 + (unsigned)(*p - '0');
    }
  *out = value;
  return true;
}

static bool parse_epoch(const cJSON *o,bool zero,int64_t *out)
{uint64_t value;if(!parse_u64(o,zero,&value) || value>(uint64_t)INT64_MAX)return false;*out=(int64_t)value;return true;}
static bool parse_task(const cJSON *o,vg_task_t *t,uint32_t version)
{
 static const char *const names[]={"request_id","title","due_epoch","priority","state","created_epoch","updated_epoch","acknowledged_epoch","snoozed_epoch","missed_epoch","snooze_count","timer_domain","delay_seconds","mono_deadline_ms","timer_boot_id","timer_revision"};
 uint32_t v;
 if(!keys(o,names,version==1?5:(version==2?11:16)) ||
    !string_copy(t->request.request_id,sizeof(t->request.request_id),cJSON_GetObjectItemCaseSensitive(o,"request_id")) ||
    !string_copy(t->request.title,sizeof(t->request.title),cJSON_GetObjectItemCaseSensitive(o,"title")) ||
    !parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"due_epoch"),version==3,&t->request.due_epoch))return false;
 if(!integer(cJSON_GetObjectItemCaseSensitive(o,"priority"),2,&v))return false;
 t->request.priority=(int)v;
 if(!integer(cJSON_GetObjectItemCaseSensitive(o,"state"),version==3?VG_TASK_NEEDS_RESET:VG_TASK_MISSED,&v))return false;
 t->state=(vg_task_state_t)v;
 if(version==1)return true;
 if(!(parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"created_epoch"),true,&t->created_epoch) &&
      parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"updated_epoch"),true,&t->updated_epoch) &&
      parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"acknowledged_epoch"),true,&t->acknowledged_epoch) &&
      parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"snoozed_epoch"),true,&t->snoozed_epoch) &&
      parse_epoch(cJSON_GetObjectItemCaseSensitive(o,"missed_epoch"),true,&t->missed_epoch) &&
      integer(cJSON_GetObjectItemCaseSensitive(o,"snooze_count"),UINT32_MAX,&t->snooze_count)))return false;
 if(version==2)return true;
 if(!integer(cJSON_GetObjectItemCaseSensitive(o,"timer_domain"),VG_TIMER_RELATIVE,&v))return false;
 t->timer_domain=(vg_timer_domain_t)v;
 return integer(cJSON_GetObjectItemCaseSensitive(o,"delay_seconds"),VG_RELATIVE_MAX_DELAY_SECONDS,&t->delay_seconds) &&
        parse_u64(cJSON_GetObjectItemCaseSensitive(o,"mono_deadline_ms"),true,&t->mono_deadline_ms) &&
        parse_u64(cJSON_GetObjectItemCaseSensitive(o,"timer_boot_id"),true,&t->timer_boot_id) &&
        parse_u64(cJSON_GetObjectItemCaseSensitive(o,"timer_revision"),true,&t->timer_revision);
}
/* 解析前限制嵌套深度，并拒绝解码后含 NUL 的字符串。 */
static bool safe_json(const char *buf,size_t n)
{
  size_t i;
  unsigned depth=0;
  bool in=false;
  for(i=0;i<n;i++)
  {
    char c=buf[i];
    if(!c) return false;
    if(in && c=='\\')
    {
      if(i+1>=n) return false;
      if(n-i>=6 && !memcmp(buf+i+1,"u0000",5)) return false;
      i++;
      continue;
    }
    if(c=='"') in=!in;
    if(!in)
    {
      if(c=='{' || c=='[')
      {
        if(++depth>8) return false;
      }
      if(c=='}' || c==']')
      {
        if(!depth) return false;
        depth--;
      }
    }
  }
  return !in && !depth;
}
static int decode(const char *buf,size_t n,vg_store_t *s)
{
  static const char *const outer[]=
  {
    "data","checksum"
  };
  static const char *const inner[]=
  {
    "version","generation","active","history","boot_counter"
  };
  cJSON *root=NULL;
  const cJSON *body,*check,*active,*history,*p;
  const char *end;
  char *canonical=NULL,crc[9];
  uint32_t v, version;
  int rc=VG_STORE_CORRUPT;
  size_t i;
  if(!n || n>VG_STORE_MAX_BYTES || !safe_json(buf,n)) return rc;
  root=cJSON_ParseWithLengthOpts(buf,n,&end,false);
  /* cJSON 无法区分分配失败与语法错误：保守停止，禁止覆盖另一槽。 */
  if(!root) return VG_STORE_IO_ERROR;
  while(end<buf+n && isspace((unsigned char)*end)) end++;
  if(end!=buf+n || !keys(root,outer,2)) goto out;
  body=cJSON_GetObjectItemCaseSensitive(root,"data");
  check=cJSON_GetObjectItemCaseSensitive(root,"checksum");
  if(!cJSON_IsObject(body) || !cJSON_IsString(check) || !check->valuestring) goto out;
  canonical=cJSON_PrintUnformatted(body);
  if(!canonical)
  {
    rc=VG_STORE_IO_ERROR;
    goto out;
  }
  snprintf(crc,sizeof(crc),"%08x",(unsigned)crc32(canonical));
  if(strcmp(crc,check->valuestring)) goto out;
  if(!integer(cJSON_GetObjectItemCaseSensitive(body,"version"),UINT32_MAX,&v)) goto out;
  version = v;
  if(version != 1 && version != 2 && version != VG_STORE_VERSION)
  {
    rc=VG_ERR_UNSUPPORTED_VERSION;
    goto out;
  }
  if(!keys(body,inner,version==3?5:4) || !integer(cJSON_GetObjectItemCaseSensitive(body,"generation"),UINT32_MAX,&v) || !v) goto out;
  vg_store_init(s);
  s->generation=v;
  s->dirty = version != VG_STORE_VERSION;
  if(version==3 && !parse_u64(cJSON_GetObjectItemCaseSensitive(body,"boot_counter"),true,&s->boot_counter))goto out;
  active=cJSON_GetObjectItemCaseSensitive(body,"active");
  history=cJSON_GetObjectItemCaseSensitive(body,"history");
  if(!cJSON_IsArray(active) || !cJSON_IsArray(history) ||
  cJSON_GetArraySize(active)>VG_MAX_ACTIVE_TASKS || cJSON_GetArraySize(history)>VG_HISTORY_CAPACITY) goto out;
  i=0;
  cJSON_ArrayForEach(p,active)
  {
    if(!parse_task(p,&s->active.tasks[i++],version)) goto out;
  }
  s->active.count=i;
  i=0;
  cJSON_ArrayForEach(p,history)
  {
    if(!parse_task(p,&s->history[i++],version)) goto out;
  }
  s->history_count=i;
  if(!valid(s)) goto out;
  rc=VG_OK;
  out:cJSON_free(canonical);
  cJSON_Delete(root);
  return rc;
}
int vg_store_save(vg_store_t *s,const vg_store_io_t *io)
{
  char *buf;
  unsigned slot;
  int rc;
  if(!valid(s) || !io || !io->write_sync) return VG_ERR_INVALID_MESSAGE;
  if(!s->dirty) return VG_OK;
  if(s->generation==UINT32_MAX) return VG_ERR_CAPACITY;
  buf=encode(s,s->generation+1);
  if(!buf) return VG_STORE_IO_ERROR;
  slot=s->current_slot==0?1:0;
  rc=io->write_sync(io->context,slot,buf,strlen(buf));
  cJSON_free(buf);
  if(rc!=0) return VG_STORE_IO_ERROR;
  s->generation++;
  s->current_slot=(int)slot;
  s->dirty=false;
  return VG_OK;
}
int vg_store_load(vg_store_t *s,const vg_store_io_t *io)
{
  vg_store_t *candidate,*best;
  char *buf;
  int status[2],rc=VG_STORE_CORRUPT;
  unsigned slot;
  size_t n;
  if(!s || !io || !io->read) return VG_ERR_INVALID_MESSAGE;
  candidate=malloc(sizeof(*candidate));
  best=malloc(sizeof(*best));
  buf=malloc(VG_STORE_MAX_BYTES);
  if(!candidate || !best || !buf)
  {
    rc=VG_STORE_IO_ERROR;
    goto out;
  }
  vg_store_init(best);
  for(slot=0;slot<2;slot++)
  {
    n=0;
    status[slot]=io->read(io->context,slot,buf,VG_STORE_MAX_BYTES,&n);
    if(status[slot]!=0 && status[slot]!=1)
    {
      rc=VG_STORE_IO_ERROR;
      goto out;
    }
    if(status[slot]==0)
    {
      status[slot]=decode(buf,n,candidate);
      if(status[slot]==VG_STORE_IO_ERROR)
      {
        rc=status[slot];
        goto out;
      }
      if(status[slot]==VG_ERR_UNSUPPORTED_VERSION)
      {
        rc=status[slot];
        goto out;
      }
      if(status[slot]==VG_OK && (best->current_slot<0 || candidate->generation>best->generation))
      {
        *best=*candidate;
        best->current_slot=(int)slot;
      }
    }
  }
  if(best->current_slot>=0)
  {
    best->dirty=best->dirty || (status[0]!=VG_OK && status[0]!=1)||(status[1]!=VG_OK && status[1]!=1);
    *s=*best;
    rc=VG_OK;
  }
  else if(status[0]==1 && status[1]==1)
  {
    vg_store_init(s);
    rc=VG_OK;
  }
  else if((status[0]<0 && status[0]!=VG_STORE_CORRUPT) || (status[1]<0 && status[1]!=VG_STORE_CORRUPT)) rc=VG_STORE_IO_ERROR;
  out:free(candidate);
  free(best);
  free(buf);
  return rc;
}
