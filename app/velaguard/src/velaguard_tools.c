/* SPDX-License-Identifier: Apache-2.0 */
#include "velaguard_tools.h"
#ifdef VG_TOOLS_HOST_TEST
#define CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR "."
#endif
#include "agent_compat.h"
#include "agent_config.h"
#include "cJSON.h"
#include <ctype.h>
#include <sys/stat.h>
#ifdef CONFIG_AI_AGENT_MCP
#error VelaGuard provider-only bridge requires CONFIG_AI_AGENT_MCP disabled
#endif
#ifdef _WIN32
#include <direct.h>
static int vg_tools_host_mkdir(const char *path,unsigned mode)
{(void)mode;return _mkdir(path);}
#define mkdir vg_tools_host_mkdir
static char *vg_tools_host_strcasestr(const char *hay,const char *needle)
{
 size_t n=strlen(needle);
 for(;*hay;hay++) if(strncasecmp(hay,needle,n)==0) return (char*)hay;
 return n?NULL:(char*)hay;
}
#define strcasestr vg_tools_host_strcasestr
#endif

/* 三份官方实际源码的公共符号及 TAG 独立命名空间。 */
#define config_store_init vg_tools_config_init_unused
#define claw_config_get vg_tools_config_get
#define claw_config_set vg_tools_config_set_unused
#define config_del vg_tools_config_del_unused
#define config_erase_all vg_tools_config_erase_unused
#define TAG vg_tools_config_tag
#include "infra/config_store.c"
#undef TAG
#define tool_guard_init vg_tools_guard_init
#define tool_guard_cleanup vg_tools_guard_cleanup_unused
#define tool_guard_check vg_tools_guard_check
#define tool_guard_record_call vg_tools_guard_record_call
#define tool_guard_set_enabled vg_tools_guard_set_enabled_unused
#define tool_guard_is_enabled vg_tools_guard_is_enabled
#define tool_guard_check_injection vg_tools_guard_check_injection_unused
#define tool_guard_sanitize_log vg_tools_guard_sanitize_log_unused
#define TAG vg_tools_guard_tag
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "tools/tool_guard.c"
#pragma GCC diagnostic pop
#undef TAG
static int vg_tools_registry_init_unused(void) __attribute__((unused));
#define tool_registry_init vg_tools_registry_init_unused
#define tool_registry_register_provider vg_tools_registry_register_provider
#define tool_registry_rebuild_json vg_tools_registry_rebuild_json_unused
#define tool_registry_get_tools_json vg_tools_registry_get_tools_json
#define tool_registry_invalidate vg_tools_registry_invalidate
#define tool_registry_cleanup vg_tools_registry_cleanup_unused
#define tool_registry_execute vg_tools_registry_execute
#define TAG vg_tools_registry_tag
/* provider-only 下 builtin 表恒空；GCC 对不可达 strcmp(NULL,...) 的推导诊断。 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
#include "tools/tool_registry.c"
#pragma GCC diagnostic pop
#undef TAG

static vg_commands_t *vg_bound_commands;
static const char *const vg_tool_names[]={"velaguard_create","velaguard_list","velaguard_snooze","velaguard_ack","velaguard_rearm"};
static const char *const vg_command_types[]={"task.create","task.list","task.snooze","task.ack","task.rearm"};
#define VG_TOOL_COUNT (sizeof(vg_tool_names)/sizeof(vg_tool_names[0]))
#define VG_TOOLS_STRING_INNER(v) #v
#define VG_TOOLS_STRING(v) VG_TOOLS_STRING_INNER(v)
static const char *const vg_payload_schemas[]={
 "{\"type\":\"object\",\"properties\":{\"title\":{\"type\":\"string\"},\"due_epoch\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991},\"delay_seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":86400},\"priority\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2}},\"required\":[\"title\",\"priority\"],\"oneOf\":[{\"required\":[\"due_epoch\"]},{\"required\":[\"delay_seconds\"]}],\"additionalProperties\":false}",
 "{\"type\":\"object\",\"properties\":{\"offset\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":" VG_TOOLS_STRING(VG_HISTORY_CAPACITY) "},\"history\":{\"type\":\"boolean\"}},\"required\":[\"offset\"],\"additionalProperties\":false}",
 "{\"type\":\"object\",\"properties\":{\"task_id\":{\"type\":\"string\"},\"seconds\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":86400},\"expected_due_epoch\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991},\"expected_revision\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":20,\"pattern\":\"^[1-9][0-9]{0,19}$\",\"description\":\"Canonical uint64 revision 1..18446744073709551615; commands enforces the numeric upper bound.\"}},\"required\":[\"task_id\",\"seconds\"],\"oneOf\":[{\"required\":[\"expected_due_epoch\"]},{\"required\":[\"expected_revision\"]}],\"additionalProperties\":false}",
 "{\"type\":\"object\",\"properties\":{\"task_id\":{\"type\":\"string\"}},\"required\":[\"task_id\"],\"additionalProperties\":false}",
 "{\"type\":\"object\",\"properties\":{\"task_id\":{\"type\":\"string\"},\"expected_revision\":{\"type\":\"string\",\"minLength\":1,\"maxLength\":20,\"pattern\":\"^[1-9][0-9]{0,19}$\",\"description\":\"Canonical uint64 revision 1..18446744073709551615; commands enforces the numeric upper bound.\"}},\"required\":[\"task_id\",\"expected_revision\"],\"additionalProperties\":false}"
};
_Static_assert(VG_TOOL_COUNT==sizeof(vg_command_types)/sizeof(vg_command_types[0]),"tool/type count");
_Static_assert(VG_TOOL_COUNT==sizeof(vg_payload_schemas)/sizeof(vg_payload_schemas[0]),"tool/schema count");
static char *vg_provider_schema(void)
{
 cJSON *array=cJSON_CreateArray(); if(!array) return NULL;
 for(size_t i=0;i<VG_TOOL_COUNT;i++) {
  char schema[1200];
  int n=snprintf(schema,sizeof(schema),"{\"type\":\"object\",\"properties\":{\"version\":{\"type\":\"integer\",\"enum\":[1]},\"request_id\":{\"type\":\"string\"},\"type\":{\"type\":\"string\",\"enum\":[\"%s\"]},\"payload\":%s},\"required\":[\"version\",\"request_id\",\"type\",\"payload\"],\"additionalProperties\":false}",vg_command_types[i],vg_payload_schemas[i]);
  if(n<0 || (size_t)n>=sizeof(schema)) {cJSON_Delete(array);return NULL;}
  cJSON *tool=cJSON_CreateObject(),*input=cJSON_Parse(schema);
  if(!tool || !input) {cJSON_Delete(tool);cJSON_Delete(input);cJSON_Delete(array);return NULL;}
  if(!cJSON_AddStringToObject(tool,"name",vg_tool_names[i]) ||
     !cJSON_AddStringToObject(tool,"description","VelaGuard offline task command. Supply the complete version/request_id/type/payload envelope; read response.error and uncertain before retrying.")) {
   cJSON_Delete(input);cJSON_Delete(tool);cJSON_Delete(array);return NULL;
  }
  if(!cJSON_AddItemToObject(tool,"input_schema",input)) {cJSON_Delete(input);cJSON_Delete(tool);cJSON_Delete(array);return NULL;}
  if(!cJSON_AddItemToArray(array,tool)) {cJSON_Delete(tool);cJSON_Delete(array);return NULL;}
 }
 char *json=cJSON_PrintUnformatted(array);cJSON_Delete(array);return json;
}
/* 在第二次 cJSON 解析前限制递归深度。完整语法仍交给既有 commands。 */
static bool vg_shallow(const char *s)
{
 bool quoted=false;int depth=0;
 for(size_t i=0;s[i];i++) {
  if(quoted && s[i]=='\\') {if(!s[++i]) return false;continue;}
  if(s[i]=='"') quoted=!quoted;
  else if(!quoted && (s[i]=='{'||s[i]=='[')) {if(++depth>8)return false;}
  else if(!quoted && (s[i]=='}'||s[i]==']')) {if(--depth<0)return false;}
 }
 return true;
}
static int vg_provider_execute(const char *name,const char *input,char *out,size_t cap)
{
 size_t i;for(i=0;i<VG_TOOL_COUNT;i++) if(!strcmp(name,vg_tool_names[i]))break;
 if(i==VG_TOOL_COUNT) return ERROR;
 if(!vg_shallow(input)) {vg_commands_execute(vg_bound_commands,"",0,out,cap);return OK;}
 cJSON *root=cJSON_Parse(input);
 const cJSON *type=cJSON_GetObjectItemCaseSensitive(root,"type");
 bool mismatch=cJSON_IsString(type) && strcmp(type->valuestring,vg_command_types[i])!=0;
 cJSON_Delete(root);
 if(mismatch) {
  snprintf(out,cap,"{\"version\":1,\"request_id\":\"\",\"type\":\"response.error\",\"payload\":{\"code\":\"TOOL_TYPE_MISMATCH\",\"uncertain\":false}}");
  return OK;
 }
 /* 已识别的业务错误必须返回 OK 给 registry，避免其 unknown fallback 覆盖响应。 */
 vg_commands_execute(vg_bound_commands,input,strlen(input),out,cap);
 return OK;
}
int vg_tools_init(vg_commands_t *commands)
{
 if(!commands || !commands->runtime) return -EINVAL;
 if(vg_bound_commands) return vg_bound_commands==commands ? 0 : -EBUSY;
 if(vg_tools_guard_init()!=OK) return -EIO;
 vg_tools_registry_register_provider("velaguard",vg_provider_schema,vg_provider_execute);
 vg_tools_registry_invalidate();vg_bound_commands=commands;return 0;
}
char *vg_tools_schema(void)
{return vg_bound_commands ? vg_tools_registry_get_tools_json() : NULL;}
int vg_tools_execute(const char *name,const char *envelope,char *out,size_t cap)
{
 if(out && cap) out[0]=0;
 if(!vg_bound_commands || !name || !envelope || !out || cap<VG_COMMAND_LINE+1) return -EINVAL;
 size_t n=0;while(name[n] && n<64)n++;
 if(!n || n==64)return -EINVAL;
 n=0;while(envelope[n] && n<=VG_COMMAND_LINE)n++;
 if(n>VG_COMMAND_LINE)return -EMSGSIZE;
 return vg_tools_registry_execute(name,envelope,out,cap);
}
