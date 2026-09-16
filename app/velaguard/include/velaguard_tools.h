#ifndef VELAGUARD_TOOLS_H
#define VELAGUARD_TOOLS_H
#include "velaguard_commands.h"
/* 单主循环、单 commands/TaskStore；重复绑定同实例幂等，不可换实例。 */
int vg_tools_init(vg_commands_t *commands);
/* 调用者 free 返回的真实 registry schema JSON。 */
char *vg_tools_schema(void);
/* 完整原始协议 envelope；返回 0 表示已识别，业务结果以 response.* 为准。
 * 非零表示适配参数错误、真实 guard 阻止或 registry 未找到工具。
 * 输出至少 VG_COMMAND_LINE+1 字节，避免命令已生效却无法完整返回结果。 */
int vg_tools_execute(const char *name,const char *envelope,char *out,size_t capacity);
#endif