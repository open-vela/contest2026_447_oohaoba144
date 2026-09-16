/* SPDX-License-Identifier: Apache-2.0 */
#include "velaguard_agent.h"
#include "velaguard_tools.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h"
#endif
static vg_commands_t *vg_agent_commands;
static uint32_t vg_registry_calls;
static const char *const vg_types[]={"task.create","task.list","task.snooze","task.ack","task.rearm"};
static const char *const vg_names[]={"velaguard_create","velaguard_list","velaguard_snooze","velaguard_ack","velaguard_rearm"};
#define VG_AGENT_TOOL_COUNT (sizeof(vg_types)/sizeof(vg_types[0]))
_Static_assert(VG_AGENT_TOOL_COUNT==sizeof(vg_names)/sizeof(vg_names[0]),"agent tool/type count");
static int agent_error(char *out,size_t cap,const char *id,const char *code,int rc,bool uncertain)
{
 snprintf(out,cap,"{\"version\":1,\"request_id\":\"%s\",\"type\":\"response.error\",\"payload\":{\"code\":\"%s\",\"result\":%d,\"uncertain\":%s}}",id,code,rc,uncertain?"true":"false");
 return rc;
}
/* 解析前限制原始长度/NUL/递归；不替代 commands 的严格协议验证。 */
static bool bounded_raw(const char *s,size_t n)
{
 bool quoted=false;int depth=0;
 if(!n || n>VG_COMMAND_LINE || memchr(s,0,n))return false;
 for(size_t i=0;i<n;i++) {
  if(quoted && s[i]=='\\') {
   if(n-i>=6 && !memcmp(s+i+1,"u0000",5))return false;
   if(++i>=n)return false;
   continue;
  }
  if(s[i]=='"')quoted=!quoted;
  else if(!quoted && (s[i]=='{' || s[i]=='[')) {if(++depth>8)return false;}
  else if(!quoted && (s[i]=='}' || s[i]==']')) {if(--depth<0)return false;}
 }
 return true;
}
static void request_id(const cJSON *root,char *id)
{
 id[0]=0;
 const cJSON *v=cJSON_GetObjectItemCaseSensitive(root,"request_id");
 if(!cJSON_IsString(v) || !v->valuestring)return;
 size_t n=strlen(v->valuestring);if(!n || n>VG_REQUEST_ID_MAX_BYTES)return;
 for(size_t i=0;i<n;i++) {
  char c=v->valuestring[i];
  if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return;
 }
 memcpy(id,v->valuestring,n+1);
}
int vg_agent_init(vg_commands_t *commands)
{
 int rc=vg_tools_init(commands);
 if(rc==0)vg_agent_commands=commands;
 return rc;
}
uint32_t vg_agent_tool_calls(void){return vg_registry_calls;}
int vg_agent_line_execute(vg_commands_t *commands,const char *line,size_t length,char *out,size_t cap)
{
 if(out && cap)out[0]=0;
 if(!out || cap<VG_COMMAND_LINE+1)return -EINVAL;
 if(!commands || !commands->runtime || !line || !bounded_raw(line,length))
  return agent_error(out,cap,"","INVALID_MESSAGE",VG_ERR_INVALID_MESSAGE,false);
 char raw[VG_COMMAND_LINE+1];memcpy(raw,line,length);raw[length]=0;
 cJSON *root=cJSON_Parse(raw);
 /* 解析失败可能是 OOM；不能再次走 commands 解析后意外绕过 guard。 */
 if(!root)return agent_error(out,cap,"","INVALID_JSON",VG_ERR_INVALID_JSON,false);
 const cJSON *type=cJSON_GetObjectItemCaseSensitive(root,"type");size_t which=VG_AGENT_TOOL_COUNT;
 if(cJSON_IsString(type))for(size_t i=0;i<VG_AGENT_TOOL_COUNT;i++)if(!strcmp(type->valuestring,vg_types[i])){which=i;break;}
 char id[VG_REQUEST_ID_CAPACITY];request_id(root,id);cJSON_Delete(root);
 if(which==VG_AGENT_TOOL_COUNT)return vg_commands_execute(commands,line,length,out,cap);
 if(vg_agent_commands!=commands)
  return agent_error(out,cap,id,"AGENT_UNAVAILABLE",-ENODEV,false);
 int rc=vg_tools_execute(vg_names[which],raw,out,cap);
 vg_registry_calls++;
 printf("VelaGuard Agent tool: name=%s rc=%d\n",vg_names[which],rc);
 if(rc==0)return 0; /* 原业务 response.error 和 uncertain 均保留。 */
 /* 当前 bridge 的 -1 是执行前 guard/unknown 拒绝，参数错误也未执行。
  * 未知未来错误保守标 uncertain，绝不改走原 commands 重试。 */
 bool uncertain=rc!=-1 && rc!=-EINVAL && rc!=-EMSGSIZE;
 return agent_error(out,cap,id,"AGENT_TOOL_REJECTED",rc,uncertain);
}