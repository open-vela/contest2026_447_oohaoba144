#include "velaguard_commands.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif

static const cJSON *field(const cJSON *o,const char *k)
{return cJSON_GetObjectItemCaseSensitive(o,k);}
static bool integer(const cJSON *v,int64_t min,int64_t max,int64_t *out)
{
  if(!cJSON_IsNumber(v) || !(v->valuedouble >= (double)min) ||
     !(v->valuedouble <= (double)max)) return false;
  *out=(int64_t)v->valuedouble;
  return (double)*out==v->valuedouble;
}
/* Revisions are canonical strings, never cJSON doubles. */
static bool revision_value(const cJSON *v,uint64_t *out)
{
  if(!cJSON_IsString(v)||!v->valuestring)return false;
  const unsigned char *p=(const unsigned char *)v->valuestring;
  if(*p<'1'||*p>'9'||strlen(v->valuestring)>20)return false;
  uint64_t value=0;
  for(;*p;p++)
    {
      if(*p<'0'||*p>'9')return false;
      unsigned digit=*p-'0';
      if(value>(UINT64_MAX-digit)/10u)return false;
      value=value*10u+digit;
    }
  *out=value;return true;
}
static bool add_u64(cJSON *o,const char *name,uint64_t value)
{
  char text[21];size_t n=20;text[n]=0;
  do{text[--n]=(char)('0'+value%10u);value/=10u;}while(value);
  return cJSON_AddStringToObject(o,name,text+n)!=NULL;
}
static bool identifier(const cJSON *v)
{
  const unsigned char *p;size_t n;
  if(!cJSON_IsString(v) || !v->valuestring) return false;
  n=strlen(v->valuestring);if(!n || n>VG_REQUEST_ID_MAX_BYTES)return false;
  for(p=(const unsigned char *)v->valuestring;*p;p++)
    if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
         (*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p=='.'))return false;
  return true;
}
static bool utf8(const char *str)
{
  const unsigned char *p=(const unsigned char *)str;uint32_t c,min;int n;
  if(!p)return false;
  while(*p)
    {
      if(*p<0x80){if(*p<32)return false;p++;continue;}
      if(*p>=0xc2&&*p<=0xdf){c=*p++&31;n=1;min=0x80;}
      else if(*p>=0xe0&&*p<=0xef){c=*p++&15;n=2;min=0x800;}
      else if(*p>=0xf0&&*p<=0xf4){c=*p++&7;n=3;min=0x10000;}
      else return false;
      while(n--){if((*p&0xc0)!=0x80)return false;c=(c<<6)|(*p++&63);}
      if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return false;
    }
  return true;
}
/* Reject decoded NUL before cJSON loses its length; cap nesting before parsing. */
static bool preflight(const char *s,size_t n)
{
  size_t i;bool quoted=false;int depth=0;
  for(i=0;i<n;i++)
    {
      if(!s[i])return false;
      if(quoted&&s[i]=='\\')
        {
          if(n-i>=6 && !memcmp(s+i+1,"u0000",5))return false;
          if(++i>=n)return false;
          continue;
        }
      if(!quoted && (s[i]=='-' || (s[i]>='0' && s[i]<='9')))
        {
          size_t start=i;
          if(s[i]=='-'){if(++i==n)return false;}
          if(s[i]<'0'||s[i]>'9')return false;
          if(s[i]=='0' && i+1<n && s[i+1]>='0' && s[i+1]<='9')return false;
          while(i+1<n && s[i+1]>='0' && s[i+1]<='9')i++;
          if(i+1<n && !isspace((unsigned char)s[i+1]) &&
             s[i+1]!=',' && s[i+1]!=']' && s[i+1]!='}')return false;
          if(i-start>16)return false;
          continue;
        }
      if(s[i]=='"')quoted=!quoted;
      else if(!quoted&&(s[i]=='{'||s[i]=='[')){if(++depth>8)return false;}
      else if(!quoted&&(s[i]=='}'||s[i]==']')){if(--depth<0)return false;}
    }
  return true;
}
static bool object_keys(const cJSON *o,const char *const *keys,size_t count)
{
  const cJSON *p,*q;size_t i;
  if(!cJSON_IsObject(o))return false;
  cJSON_ArrayForEach(p,o)
    {
      if(!p->string)return false;
      for(i=0;i<count;i++)if(!strcmp(p->string,keys[i]))break;
      if(i==count)return false;
      for(q=p->next;q;q=q->next)if(q->string&&!strcmp(p->string,q->string))return false;
    }
  return true;
}
#define KEYS(o,...) object_keys(o,(const char *const[]){__VA_ARGS__},sizeof((const char *const[]){__VA_ARGS__})/sizeof(char *))
const char *vg_state_name(vg_task_state_t s)
{
  static const char *const names[]={"CREATED","SCHEDULED","ALERTING","ACKNOWLEDGED","SNOOZED","MISSED","NEEDS_RESET"};
  return (unsigned)s<sizeof(names)/sizeof(names[0])?names[s]:"UNKNOWN";
}
static const char *error_name(int rc)
{
  switch(rc)
    {
      case VG_ERR_INVALID_JSON:return "INVALID_JSON";
      case VG_ERR_UNSUPPORTED_VERSION:return "UNSUPPORTED_VERSION";
      case VG_ERR_INVALID_TIME:return "INVALID_TIME";
      case VG_ERR_TASK_NOT_FOUND:return "TASK_NOT_FOUND";
      case VG_ERR_DUPLICATE_REQUEST:return "DUPLICATE_REQUEST";
      case VG_ERR_CAPACITY:return "CAPACITY_REACHED";
      case VG_ERR_INVALID_STATE:return "INVALID_STATE";
      case VG_STORE_IO_ERROR:case VG_STORE_CORRUPT:return "STORE_ERROR";
      case VG_RUNTIME_BLOCKED:return "RUNTIME_BLOCKED";
      case VG_RUNTIME_BOOT_CONFLICT:return "BOOT_CONFLICT";
      case VG_RUNTIME_SCHEDULER_ERROR:return "SCHEDULER_ERROR";
      default:return "INVALID_MESSAGE";
    }
}
static bool task_json(cJSON *p,const vg_task_t *t)
{
  return cJSON_AddStringToObject(p,"task_id",t->request.request_id)&&
    cJSON_AddStringToObject(p,"title",t->request.title)&&
    cJSON_AddStringToObject(p,"state",vg_state_name(t->state))&&
    cJSON_AddNumberToObject(p,"due_epoch",(double)t->request.due_epoch)&&
    cJSON_AddNumberToObject(p,"priority",t->request.priority)&&
    cJSON_AddNumberToObject(p,"snooze_count",t->snooze_count)&&
    cJSON_AddNumberToObject(p,"acknowledged_epoch",(double)t->acknowledged_epoch)&&
    cJSON_AddStringToObject(p,"timer_domain",t->timer_domain==VG_TIMER_RELATIVE?"REL":"ABS")&&
    cJSON_AddNumberToObject(p,"delay_seconds",t->delay_seconds)&&
    add_u64(p,"mono_deadline_ms",t->mono_deadline_ms)&&
    add_u64(p,"timer_boot_id",t->timer_boot_id)&&
    add_u64(p,"timer_revision",t->timer_revision);
}
void vg_commands_init(vg_commands_t *c,vg_runtime_t *r,void *ctx,int (*set)(void *,int64_t))
{memset(c,0,sizeof(*c));c->runtime=r;c->context=ctx;c->set_clock=set;}
int vg_commands_execute(vg_commands_t *c,const char *line,size_t length,char *out,size_t cap)
{
  cJSON *root=NULL,*payload=NULL,*response=NULL,*data=NULL;
  const cJSON *v,*id,*type,*p;const char *end=NULL,*name,*reqid="";
  char *canonical=NULL;int rc=VG_ERR_INVALID_MESSAGE;int64_t number=0,seconds=0,expected=0;
  vg_task_t task;vg_create_request_t create;vg_runtime_status_t status;uint64_t revision=0;
  bool mutation=false,history=false,effect_uncertain=false;unsigned i;
  if(!out||cap<VG_COMMAND_LINE+1)return VG_ERR_INVALID_MESSAGE;
  out[0]=0;
  if(!c||!c->runtime||!line||!length||length>VG_COMMAND_LINE||!preflight(line,length))goto finish;
  root=cJSON_ParseWithLengthOpts(line,length,&end,false);
  if(!root||!end){rc=VG_ERR_INVALID_JSON;goto finish;}
  while(end<line+length&&isspace((unsigned char)*end))end++;
  if(end!=line+length){rc=VG_ERR_INVALID_JSON;goto finish;}
  if(!KEYS(root,"version","request_id","type","payload"))goto finish;
  id=field(root,"request_id");if(!identifier(id))goto finish;reqid=id->valuestring;
  if(!integer(field(root,"version"),1,1,&number)){rc=VG_ERR_UNSUPPORTED_VERSION;goto finish;}
  type=field(root,"type");p=field(root,"payload");
  if(!cJSON_IsString(type)||!type->valuestring||!cJSON_IsObject(p))goto finish;
  name=type->valuestring;
  mutation=!strcmp(name,"task.create")||!strcmp(name,"task.ack")||
           !strcmp(name,"task.snooze")||!strcmp(name,"task.rearm")||!strcmp(name,"device.time")||!strcmp(name,"device.reload");
  canonical=cJSON_PrintUnformatted(root);if(!canonical){rc=VG_STORE_IO_ERROR;goto finish;}
  if(mutation)
    for(i=0;i<VG_COMMAND_REPLAYS;i++)if(!strcmp(reqid,c->replay[i].id))
      {
        if(strcmp(canonical,c->replay[i].request)){rc=VG_ERR_DUPLICATE_REQUEST;goto finish;}
        strcpy(out,c->replay[i].response);rc=VG_OK;goto done;
      }
  data=cJSON_CreateObject();if(!data){rc=VG_STORE_IO_ERROR;goto finish;}
  if(!strcmp(name,"task.create"))
    {
      if(!KEYS(p,"title","due_epoch","delay_seconds","priority"))goto finish;
      const cJSON *due=field(p,"due_epoch"),*delay=field(p,"delay_seconds");
      if((due==NULL)==(delay==NULL))goto finish;
      bool relative=delay!=NULL;
      v=field(p,"title");
      if(!cJSON_IsString(v)||!v->valuestring||!v->valuestring[0]||
         strlen(v->valuestring)>VG_TITLE_MAX_BYTES||!utf8(v->valuestring))goto finish;
      memset(&create,0,sizeof(create));strcpy(create.request_id,reqid);strcpy(create.title,v->valuestring);
      if(relative)
        {if(!integer(delay,1,VG_RELATIVE_MAX_DELAY_SECONDS,&seconds))goto finish;}
      else if(!integer(due,1,9007199254740991LL,&create.due_epoch))goto finish;
      if(!integer(field(p,"priority"),0,2,&number))goto finish;
#ifdef __NuttX__
      if(!relative && (int64_t)(time_t)create.due_epoch!=create.due_epoch){rc=VG_ERR_INVALID_TIME;goto finish;}
#endif
      create.priority=(int)number;
      rc=relative?vg_runtime_create_relative(c->runtime,&create,(uint32_t)seconds,&task):
                  vg_runtime_create(c->runtime,&create,&task);
      if(rc==VG_OK&&!cJSON_AddStringToObject(data,"task_id",task.request.request_id))rc=VG_STORE_IO_ERROR;
    }
  else if(!strcmp(name,"task.ack")||!strcmp(name,"task.snooze")||!strcmp(name,"task.rearm"))
    {
      bool snooze=!strcmp(name,"task.snooze"),rearm=!strcmp(name,"task.rearm");
      if(snooze)
        {if(!KEYS(p,"task_id","seconds","expected_due_epoch","expected_revision"))goto finish;}
      else if(rearm)
        {if(!KEYS(p,"task_id","expected_revision"))goto finish;}
      else if(!KEYS(p,"task_id"))goto finish;
      v=field(p,"task_id");if(!identifier(v))goto finish;
      if(rearm)
        {
          if(!revision_value(field(p,"expected_revision"),&revision))goto finish;
          rc=vg_runtime_rearm_relative(c->runtime,v->valuestring,revision,&task);
        }
      else if(snooze)
        {
          const cJSON *due=field(p,"expected_due_epoch"),*rev=field(p,"expected_revision");
          if((due==NULL)==(rev==NULL)||
             !integer(field(p,"seconds"),1,VG_RUNTIME_MAX_SNOOZE_SEC,&seconds))goto finish;
          if(rev)
            {if(!revision_value(rev,&revision))goto finish;}
          else if(!integer(due,1,9007199254740991LL,&expected))goto finish;
          rc=vg_runtime_find(c->runtime,v->valuestring,&task,NULL);
          if(rc==VG_OK)
            {
              if(rev)
                rc=task.timer_domain!=VG_TIMER_RELATIVE?VG_ERR_INVALID_STATE:
                   vg_runtime_snooze_relative(c->runtime,v->valuestring,(uint32_t)seconds,revision,&task);
              else
                rc=task.timer_domain!=VG_TIMER_ABSOLUTE||task.request.due_epoch!=expected?VG_ERR_INVALID_STATE:
                   vg_runtime_snooze(c->runtime,v->valuestring,(uint32_t)seconds,&task);
            }
        }
      else rc=vg_runtime_ack(c->runtime,v->valuestring,&task);
      if(rc==VG_OK&&!cJSON_AddStringToObject(data,"task_id",task.request.request_id))rc=VG_STORE_IO_ERROR;
    }
  else if(!strcmp(name,"task.list")||!strcmp(name,"event.sync"))
    {
      if(!KEYS(p,"offset","history"))goto finish;
      v=field(p,"offset");if(!integer(v,0,VG_HISTORY_CAPACITY,&number))goto finish;
      v=field(p,"history");history=!strcmp(name,"event.sync");
      if(v){if(!cJSON_IsBool(v))goto finish;history=cJSON_IsTrue(v);}
      rc=vg_runtime_list(c->runtime,history,(size_t)number,&task);
      if(rc==VG_ERR_TASK_NOT_FOUND){rc=VG_OK;if(!cJSON_AddNullToObject(data,"task"))rc=VG_STORE_IO_ERROR;}
      else if(rc==VG_OK)
        {
          payload=cJSON_AddObjectToObject(data,"task");
          if(!payload||!task_json(payload,&task))rc=VG_STORE_IO_ERROR;
        }
      if(rc==VG_OK&&!cJSON_AddNumberToObject(data,"next_offset",(double)(number+1)))rc=VG_STORE_IO_ERROR;
    }
  else if(!strcmp(name,"device.status"))
    {
      if(cJSON_GetArraySize(p)!=0)goto finish;
      rc=vg_runtime_status(c->runtime,&status);
      if(rc==VG_OK&&!(cJSON_AddBoolToObject(data,"ready",status.ready)&&
         cJSON_AddBoolToObject(data,"blocked",status.blocked)&&
         cJSON_AddBoolToObject(data,"rtc_valid",status.rtc_valid)&&
         cJSON_AddNumberToObject(data,"now_epoch",(double)status.now_epoch)&&
         cJSON_AddNumberToObject(data,"next_epoch",(double)status.next_epoch)&&
         cJSON_AddNumberToObject(data,"active_count",status.active_count)&&
         cJSON_AddNumberToObject(data,"history_count",status.history_count)&&
         cJSON_AddNumberToObject(data,"alerting_count",status.alerting_count)&&
         cJSON_AddBoolToObject(data,"timed_mode",status.timed_mode)&&
         cJSON_AddBoolToObject(data,"mono_valid",status.mono_valid)&&
         cJSON_AddBoolToObject(data,"boot_ready",status.boot_ready)&&
         cJSON_AddBoolToObject(data,"has_next",status.has_next)&&
         add_u64(data,"now_mono_ms",status.now_mono_ms)&&
         add_u64(data,"next_mono_ms",status.next_mono_ms)&&
         add_u64(data,"active_boot_id",status.active_boot_id)))rc=VG_STORE_IO_ERROR;
      if(rc==VG_OK&&c->get_diagnostics)
        {
          vg_device_diagnostics_t diag={0};
          c->get_diagnostics(c->context,&diag);
          payload=cJSON_AddObjectToObject(data,"platform");
          if(!payload||!(cJSON_AddBoolToObject(payload,"display_ready",diag.display_ready)&&
             cJSON_AddBoolToObject(payload,"touch_ready",diag.touch_ready)&&
             cJSON_AddBoolToObject(payload,"lcd_exists",diag.lcd_exists)&&
             cJSON_AddBoolToObject(payload,"rtc_exists",diag.rtc_exists)&&
             cJSON_AddBoolToObject(payload,"storage_ready",diag.storage_ready)&&
             cJSON_AddNumberToObject(payload,"storage_errno",diag.storage_errno)&&
             cJSON_AddNumberToObject(payload,"app_error",diag.app_error)&&
             cJSON_AddNumberToObject(payload,"ui_error",diag.ui_error)&&
             cJSON_AddNumberToObject(payload,"heap_free",diag.heap_free)))rc=VG_STORE_IO_ERROR;
        }
    }
  else if(!strcmp(name,"device.time"))
    {
      if(!KEYS(p,"epoch")||!integer(field(p,"epoch"),1,9007199254740991LL,&number))goto finish;
      if(!c->set_clock)rc=VG_ERR_INVALID_TIME;
      else
        {
          /* Even a successful clock setter can precede a failed reload. */
          effect_uncertain=true;
          int clock_rc=c->set_clock(c->context,number);
          rc=vg_runtime_reload(c->runtime);
          if(clock_rc){effect_uncertain=true;rc=VG_ERR_INVALID_TIME;}
        }
    }
  else if(!strcmp(name,"device.reload"))
    {
      if(cJSON_GetArraySize(p)!=0)goto finish;
      rc=vg_runtime_reload(c->runtime);
    }
finish:
  if(rc==VG_OK)
    {
      response=cJSON_CreateObject();
      if(!response||!cJSON_AddNumberToObject(response,"version",1)||
         !cJSON_AddStringToObject(response,"request_id",reqid)||
         !cJSON_AddStringToObject(response,"type","response.ok")||
         !cJSON_AddItemToObject(response,"payload",data))
        {rc=VG_STORE_IO_ERROR;}
      else
        {
          data=NULL;
          if(!cJSON_PrintPreallocated(response,out,VG_COMMAND_LINE+1,false))rc=VG_STORE_IO_ERROR;
        }
    }
  if(rc!=VG_OK)
    snprintf(out,cap,"{\"version\":1,\"request_id\":\"%s\",\"type\":\"response.error\",\"payload\":{\"code\":\"%s\",\"result\":%d,\"uncertain\":%s}}",
             reqid,error_name(rc),rc,(effect_uncertain||rc==VG_STORE_IO_ERROR||rc==VG_RUNTIME_BLOCKED||rc==VG_RUNTIME_BOOT_CONFLICT||rc==VG_RUNTIME_SCHEDULER_ERROR)?"true":"false");
  else if(mutation&&canonical)
    {
      vg_command_replay_t *r=&c->replay[c->cursor++%VG_COMMAND_REPLAYS];
      strcpy(r->id,reqid);strcpy(r->request,canonical);strcpy(r->response,out);
    }
done:
  cJSON_Delete(data);cJSON_Delete(response);cJSON_free(canonical);cJSON_Delete(root);return rc;
}
void vg_line_feed(vg_line_reader_t *r,vg_commands_t *c,const char *bytes,size_t n,
                  void (*emit)(void *,const char *),void *context)
{
  size_t i;char output[VG_COMMAND_LINE+1];
  for(i=0;i<n;i++)
    {
      if(bytes[i]=='\n')
        {
          if(r->overflow)emit(context,"{\"version\":1,\"request_id\":\"\",\"type\":\"response.error\",\"payload\":{\"code\":\"LINE_TOO_LONG\"}}");
          else if(r->length)
            {
              if(r->line[r->length-1]=='\r')r->length--;
              if(r->length)
                {
                  r->line[r->length]=0;
                  output[0]=0;output[sizeof(output)-1]=0;
                  if(c->line_execute)c->line_execute(c,r->line,r->length,output,sizeof(output));
                  else vg_commands_execute(c,r->line,r->length,output,sizeof(output));
                  output[sizeof(output)-1]=0;
                  if(!output[0])
                    snprintf(output,sizeof(output),"{\"version\":1,\"request_id\":\"\",\"type\":\"response.error\",\"payload\":{\"code\":\"DISPATCH_ERROR\",\"uncertain\":true}}");
                  emit(context,output);
                }
            }
          r->length=0;r->overflow=false;
        }
      else if(!r->overflow)
        {
          if(r->length>VG_COMMAND_LINE || (r->length==VG_COMMAND_LINE && bytes[i]!='\r'))r->overflow=true;
          else r->line[r->length++]=bytes[i];
        }
    }
}
