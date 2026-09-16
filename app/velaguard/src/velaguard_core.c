#include "velaguard_core.h"

#include <string.h>

static bool vg_string_is_bounded(const char *value, size_t capacity)
{
  size_t i;

  if (value == NULL || capacity == 0 || value[0] == '\0')
    {
      return false;
    }

  for (i = 0; i < capacity; i++)
    {
      if (value[i] == '\0')
        {
          return true;
        }
    }

  return false;
}

void vg_core_init(vg_core_t *core)
{
  if (core != NULL)
    {
      memset(core, 0, sizeof(*core));
    }
}

vg_result_t vg_core_find_request(const vg_core_t *core,
                                 const char *request_id,
                                 size_t *task_index)
{
  size_t i;

  if (core == NULL || request_id == NULL || request_id[0] == '\0')
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  for (i = 0; i < core->count; i++)
    {
      if (strcmp(core->tasks[i].request.request_id, request_id) == 0)
        {
          if (task_index != NULL)
            {
              *task_index = i;
            }

          return VG_OK;
        }
    }

  return VG_ERR_TASK_NOT_FOUND;
}

vg_result_t vg_core_create(vg_core_t *core,
                           const vg_create_request_t *request,
                           int64_t now_epoch,
                           bool rtc_valid,
                           size_t *task_index)
{
  size_t existing;
  size_t index;

  if (core == NULL || request == NULL ||
      !vg_string_is_bounded(request->request_id,
                            sizeof(request->request_id)) ||
      !vg_string_is_bounded(request->title, sizeof(request->title)) ||
      request->priority < 0 || request->priority > 2)
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  if (vg_core_find_request(core, request->request_id, &existing) == VG_OK)
    {
      if (task_index != NULL)
        {
          *task_index = existing;
        }

      return VG_ERR_DUPLICATE_REQUEST;
    }

  if (!rtc_valid || request->due_epoch <= now_epoch)
    {
      return VG_ERR_INVALID_TIME;
    }

  if (core->count >= VG_MAX_ACTIVE_TASKS)
    {
      return VG_ERR_CAPACITY;
    }

  index = core->count;
  core->tasks[index].request = *request;
  core->tasks[index].state = VG_TASK_CREATED;
  core->tasks[index].created_epoch = now_epoch > 0 ? now_epoch : 0;
  core->tasks[index].updated_epoch = core->tasks[index].created_epoch;
  core->count++;

  if (task_index != NULL)
    {
      *task_index = index;
    }

  return VG_OK;
}

static bool vg_transition_allowed(vg_task_state_t current,
                                  vg_task_state_t next)
{
  switch (current)
    {
      case VG_TASK_CREATED:
        return next == VG_TASK_SCHEDULED;

      case VG_TASK_SCHEDULED:
        return next == VG_TASK_ALERTING;

      case VG_TASK_ALERTING:
        return next == VG_TASK_ACKNOWLEDGED ||
               next == VG_TASK_SNOOZED ||
               next == VG_TASK_MISSED;

      case VG_TASK_SNOOZED:
        return next == VG_TASK_SCHEDULED;

      case VG_TASK_ACKNOWLEDGED:
      case VG_TASK_MISSED:
      default:
        return false;
    }
}

vg_result_t vg_core_transition(vg_core_t *core, size_t task_index,
                               vg_task_state_t next_state)
{
  vg_task_t *task;

  if (core == NULL || task_index >= core->count)
    {
      return VG_ERR_TASK_NOT_FOUND;
    }

  task = &core->tasks[task_index];
  if (next_state == VG_TASK_NEEDS_RESET)
    {
      if (task->timer_domain != VG_TIMER_RELATIVE ||
          (task->state != VG_TASK_CREATED && task->state != VG_TASK_SCHEDULED &&
           task->state != VG_TASK_SNOOZED)) return VG_ERR_INVALID_STATE;
    }
  else if (task->state == VG_TASK_NEEDS_RESET &&
           task->timer_domain == VG_TIMER_RELATIVE &&
           next_state == VG_TASK_SCHEDULED)
    {
      /* Explicit runtime rearm validates revision and replaces the deadline. */
    }
  else if (!vg_transition_allowed(task->state, next_state))
    {
      return VG_ERR_INVALID_STATE;
    }

  task->state = next_state;
  return VG_OK;
}
