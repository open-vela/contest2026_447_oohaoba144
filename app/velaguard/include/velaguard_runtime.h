#ifndef VELAGUARD_RUNTIME_H
#define VELAGUARD_RUNTIME_H

#include "velaguard_store.h"

#define VG_RUNTIME_BLOCKED (-30)
#define VG_RUNTIME_SCHEDULER_ERROR (-31)
#define VG_RUNTIME_BOOT_CONFLICT (-32)
#define VG_RUNTIME_MISS_GRACE_SEC 300
#define VG_RUNTIME_MAX_SNOOZE_SEC 86400

typedef struct
{
  vg_store_io_t io;
  void *context;
  int (*clock)(void *, int64_t *, bool *);
  /* 单一唤醒源：has_next=false 表示取消；回调不得重入 runtime。 */
  int (*scheduler_reconcile)(void *, bool has_next, int64_t next_epoch);
} vg_runtime_deps_t;

typedef struct
{
  void *context;
  /* Milliseconds since this physical boot; zero is valid. Never UTC.
   * Failure or regression blocks the runtime until a successful reload. */
  int (*monotonic_ms)(void *, uint64_t *);
  /* The only scheduler used in timed mode; false cancels the derived wake.
   * Callbacks must not reenter runtime or retain pointers into its store. */
  int (*scheduler_reconcile)(void *, bool has_next, uint64_t next_mono_ms);
} vg_runtime_timer_ops_t;
typedef struct
{
  vg_store_t store;
  vg_runtime_deps_t deps;
  bool ready;
  bool blocked;
  bool rtc_valid;
  bool has_next;
  int64_t now_epoch;
  int64_t next_epoch;
  vg_runtime_timer_ops_t timers;
  bool timed_mode, mono_valid, mono_seen, boot_ready;
  uint64_t now_mono_ms, next_mono_ms;
  uint64_t active_boot_id, pending_boot_id, boot_base_id;
} vg_runtime_t;

typedef struct
{
  bool ready, blocked, rtc_valid, has_next;
  int64_t now_epoch, next_epoch;
  size_t active_count, history_count, alerting_count;
  bool timed_mode, mono_valid, boot_ready;
  uint64_t now_mono_ms, next_mono_ms, active_boot_id;
} vg_runtime_status_t;

/* 唯一所有者，静态/堆分配，所有操作必须在一个串行工作线程执行。
 * Store 保存或 scheduler 对账失败后阻塞写操作，仅 reload 可恢复。
 * 错误可能已有持久效果，不得响应“确定未执行”；成功才可 response.ok。
 * 所有输出 task 为值拷贝；稳定引用为 request_id，不暴露长期 index。
 */
int vg_runtime_start(vg_runtime_t *, const vg_runtime_deps_t *);
/* 只在真正新启动时使用；reload 保持本次boot，保存成功前不启动调度。
 * On an error retry reload on this same object, not start_with_timers.
 * next_epoch remains UTC (zero for a relative wake); next_mono_ms is uptime.
 * Legacy start rejects any REL snapshot. */
int vg_runtime_start_with_timers(vg_runtime_t *, const vg_runtime_deps_t *,
                                 const vg_runtime_timer_ops_t *);
int vg_runtime_reload(vg_runtime_t *);
int vg_runtime_create(vg_runtime_t *, const vg_create_request_t *, vg_task_t *);
/* REL creation keeps request.due_epoch=0. delay is immutable replay identity.
 * ACK handles both domains; only timer-changing operations need a revision.
 * A stale expected_revision is rejected, including after an uncertain write
 * recovered by reload. Callers must inspect the saved task, never retry with
 * a freshly fetched revision as though that were the original request. */
int vg_runtime_create_relative(vg_runtime_t *, const vg_create_request_t *,
                               uint32_t delay_seconds, vg_task_t *);
int vg_runtime_snooze_relative(vg_runtime_t *, const char *request_id,
                               uint32_t seconds, uint64_t expected_revision,
                               vg_task_t *);
int vg_runtime_rearm_relative(vg_runtime_t *, const char *request_id,
                              uint64_t expected_revision, vg_task_t *);
int vg_runtime_ack(vg_runtime_t *, const char *request_id, vg_task_t *);
int vg_runtime_snooze(vg_runtime_t *, const char *request_id, uint32_t seconds,
                       vg_task_t *);
int vg_runtime_tick(vg_runtime_t *);
int vg_runtime_find(const vg_runtime_t *, const char *, vg_task_t *, bool *history);
int vg_runtime_list(const vg_runtime_t *, bool history, size_t offset, vg_task_t *);
int vg_runtime_status(const vg_runtime_t *, vg_runtime_status_t *);

#endif
