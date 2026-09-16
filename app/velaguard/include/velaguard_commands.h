#ifndef VELAGUARD_COMMANDS_H
#define VELAGUARD_COMMANDS_H
#include "velaguard_runtime.h"
#define VG_COMMAND_LINE 1024
#define VG_COMMAND_REPLAYS 8
typedef struct {
  char id[VG_REQUEST_ID_CAPACITY];
  char request[VG_COMMAND_LINE+1],response[VG_COMMAND_LINE+1];
} vg_command_replay_t;
typedef struct {
  bool display_ready,touch_ready,lcd_exists,rtc_exists,storage_ready;
  int storage_errno,app_error,ui_error;
  size_t heap_free;
} vg_device_diagnostics_t;
typedef struct vg_commands {
  vg_runtime_t *runtime; void *context; int (*set_clock)(void *,int64_t);
  void (*get_diagnostics)(void *,vg_device_diagnostics_t *);
  /* 仅 line_feed 调用；provider 直接执行 commands，避免递归。
   * 回调应填写完整 NUL 结尾响应；负返回值不能覆盖已填写的业务错误。
   * 空输出会生成 DISPATCH_ERROR/uncertain=true，不假定未执行。 */
  int (*line_execute)(struct vg_commands *,const char *,size_t,char *,size_t);
  vg_command_replay_t replay[VG_COMMAND_REPLAYS]; unsigned cursor;
} vg_commands_t;
typedef struct {char line[VG_COMMAND_LINE+2];size_t length;bool overflow;} vg_line_reader_t;
void vg_commands_init(vg_commands_t *,vg_runtime_t *,void *,int (*)(void *,int64_t));
int vg_commands_execute(vg_commands_t *,const char *,size_t,char *,size_t);
void vg_line_feed(vg_line_reader_t *,vg_commands_t *,const char *,size_t,
                  void (*emit)(void *,const char *),void *);
const char *vg_state_name(vg_task_state_t);
#endif
