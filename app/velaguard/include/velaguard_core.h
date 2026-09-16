#ifndef VELAGUARD_CORE_H
#define VELAGUARD_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_MAX_ACTIVE_TASKS 32
#define VG_REQUEST_ID_MAX_BYTES 64
#define VG_REQUEST_ID_CAPACITY (VG_REQUEST_ID_MAX_BYTES + 1)
#define VG_TITLE_MAX_BYTES 192
#define VG_TITLE_CAPACITY (VG_TITLE_MAX_BYTES + 1)

typedef enum
{
  VG_OK = 0,
  VG_ERR_INVALID_JSON = -1,
  VG_ERR_UNSUPPORTED_VERSION = -2,
  VG_ERR_INVALID_MESSAGE = -3,
  VG_ERR_INVALID_TIME = -4,
  VG_ERR_DUPLICATE_REQUEST = -5,
  VG_ERR_CAPACITY = -6,
  VG_ERR_INVALID_STATE = -7,
  VG_ERR_TASK_NOT_FOUND = -8
} vg_result_t;

typedef enum
{
  VG_TASK_CREATED = 0,
  VG_TASK_SCHEDULED,
  VG_TASK_ALERTING,
  VG_TASK_ACKNOWLEDGED,
  VG_TASK_SNOOZED,
  VG_TASK_MISSED,
  VG_TASK_NEEDS_RESET
} vg_task_state_t;

typedef struct
{
  char request_id[VG_REQUEST_ID_CAPACITY];
  char title[VG_TITLE_CAPACITY];
  int64_t due_epoch;
  int priority;
} vg_create_request_t;

typedef enum { VG_TIMER_ABSOLUTE=0, VG_TIMER_RELATIVE=1 } vg_timer_domain_t;

typedef struct
{
  vg_create_request_t request;
  vg_task_state_t state;
  /* 0 表示旧版迁移后的未知时间，不伪造历史时间。 */
  int64_t created_epoch;
  int64_t updated_epoch;
  int64_t acknowledged_epoch;
  int64_t snoozed_epoch;
  int64_t missed_epoch;
  uint32_t snooze_count;
  /* REL 的 monotonic 毫秒和 boot 身份不混入 due_epoch。 */
  vg_timer_domain_t timer_domain;
  uint32_t delay_seconds;
  uint64_t mono_deadline_ms;
  uint64_t timer_boot_id;
  uint64_t timer_revision;
} vg_task_t;

typedef struct
{
  vg_task_t tasks[VG_MAX_ACTIVE_TASKS];
  size_t count;
} vg_core_t;

void vg_core_init(vg_core_t *core);
vg_result_t vg_core_find_request(const vg_core_t *core,
                                 const char *request_id,
                                 size_t *task_index);
vg_result_t vg_core_create(vg_core_t *core,
                           const vg_create_request_t *request,
                           int64_t now_epoch,
                           bool rtc_valid,
                           size_t *task_index);
vg_result_t vg_core_transition(vg_core_t *core, size_t task_index,
                               vg_task_state_t next_state);

#endif
