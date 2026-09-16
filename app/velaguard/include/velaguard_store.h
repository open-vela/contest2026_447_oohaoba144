#ifndef VELAGUARD_STORE_H
#define VELAGUARD_STORE_H

#include "velaguard_core.h"

#define VG_HISTORY_CAPACITY 128
#define VG_STORE_VERSION 3
#define VG_RELATIVE_MAX_DELAY_SECONDS 86400u
#define VG_STORE_MAX_BYTES (320u * 1024u)
#define VG_STORE_IO_ERROR (-20)
#define VG_STORE_CORRUPT (-21)

/* 所有调用必须串行；本模块无锁，也不是中断上下文 API。
 * read: 0=完整读取成功，1=文件不存在，负数=读取错误。
 * 空文件不是不存在；超出 cap 必须返回错误，不得截断后返回成功。
 * write_sync: 只有完整写入并完成耐久性屏障后才返回 0。
 * 失败允许目标槽部分覆盖，但两个槽必须独立，不能破坏另一个槽。
 * 文件适配见 velaguard_store_file.h；实际 NOR 屏障仍需落实上述保证。
 */
typedef struct
{
  void *context;
  int (*read)(void *, unsigned, char *, size_t, size_t *);
  int (*write_sync)(void *, unsigned, const char *, size_t);
} vg_store_io_t;

typedef struct
{
  vg_core_t active;
  vg_task_t history[VG_HISTORY_CAPACITY];
  size_t history_count;
  size_t history_start;
  uint64_t boot_counter;
  uint32_t generation;
  int current_slot;
  bool dirty;
} vg_store_t;

/* store 必须静态/堆分配，大小以 sizeof 为准，不要放入 8 KiB 栈。
 * index 只是本次操作的活动数组位置，终态回收会移动其他元素。
 * 跨调用身份请用 request_id 重新查找，不得长期保存 index。
 * 去重仅覆盖当前活动任务及最近 128 个终态任务；环外历史不保证去重。
 * 必须先 load 成功才允许修改/保存现有存储。load 失败保持内存不变，
 * 调用方必须停止写入，不能将旧内存或空 store 保存覆盖故障/未来版本。
 * 字段公开用于诊断；正常调用方不得直接修改内部状态。
 */
void vg_store_init(vg_store_t *store);
int vg_store_create(vg_store_t *, const vg_create_request_t *, int64_t,
                    bool, size_t *);
/* 只修改内存并置 dirty；调用方必须成功持久化后才能使用新 boot。
 * load/reload 不自动递增；不同 boot 的相对项保持快照供 runtime 协调。 */
int vg_store_advance_boot(vg_store_t *);
/* 新入口要求 request.due_epoch=0；原始 delay 不随 snooze 改动。
 * revision 初始固定 1；本轮只建模/持久化，不承诺已能调度 REL。 */
int vg_store_create_relative(vg_store_t *, const vg_create_request_t *,
                             uint32_t delay_seconds, uint64_t mono_now_ms,
                             size_t *index);
int vg_store_transition(vg_store_t *, size_t, vg_task_state_t);
int vg_store_save(vg_store_t *, const vg_store_io_t *);
int vg_store_load(vg_store_t *, const vg_store_io_t *);

#endif
