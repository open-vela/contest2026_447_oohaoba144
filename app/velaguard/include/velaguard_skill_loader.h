#ifndef VELAGUARD_SKILL_LOADER_H
#define VELAGUARD_SKILL_LOADER_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#define VG_SKILL_BODY_MAX 8192u
/* 单主循环调用；目录在构建时由官方 AGENT_DATA_DIR 确定。 */
typedef struct {
  size_t skill_count;
  bool velaguard_found;
  size_t summary_bytes;
  bool truncated;
  uint32_t refresh_generation;
} vg_skill_loader_report_t;
/* 真实官方 loader 枚举/摘要；不安装 builtin、不启动线程。 */
int vg_skill_loader_probe(vg_skill_loader_report_t *report,
                          char *summary, size_t capacity);
/* 队伍正文读取适配器。name 不含 .md，仅允许字母数字、连字符、下划线。 */
int vg_skill_loader_read(const char *name, char *body, size_t capacity,
                         size_t *body_bytes);
#endif