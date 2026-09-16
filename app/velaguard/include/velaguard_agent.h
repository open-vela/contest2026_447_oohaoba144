#ifndef VELAGUARD_AGENT_H
#define VELAGUARD_AGENT_H
#include "velaguard_commands.h"
/* 仅成功后由 main 安装 line_execute hook；失败时保留原离线 UART 路径。 */
int vg_agent_init(vg_commands_t *commands);
int vg_agent_line_execute(vg_commands_t *commands,const char *line,size_t length,
                          char *out,size_t capacity);
/* 实际调用真实 registry bridge 后递增；只读诊断，单主循环。 */
uint32_t vg_agent_tool_calls(void);
#endif