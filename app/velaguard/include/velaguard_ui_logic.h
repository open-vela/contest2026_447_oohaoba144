#ifndef VELAGUARD_UI_LOGIC_H
#define VELAGUARD_UI_LOGIC_H
#include "velaguard_runtime.h"
typedef struct {bool history;size_t offset;char id[VG_REQUEST_ID_CAPACITY];} vg_ui_selection_t;
typedef struct {
  bool valid;char id[VG_REQUEST_ID_CAPACITY];vg_task_state_t state;
  vg_timer_domain_t domain;uint64_t revision;int64_t due_epoch;
} vg_ui_target_t;
typedef enum {VG_UI_NONE,VG_UI_DONE,VG_UI_RIGHT} vg_ui_action_t;
typedef struct {bool pressed,long_fired;uint32_t pressed_at;vg_ui_target_t target;} vg_ui_key_t;
typedef struct {
  char status[64];
  char title[VG_TITLE_CAPACITY];
  char detail[192];
  char right_label[20];
  bool alert,done_enabled,right_enabled;
  unsigned background;
} vg_ui_view_t;
typedef enum {
  VG_UI_PAGE_HOME,
  VG_UI_PAGE_TASKS,
  VG_UI_PAGE_HISTORY,
  VG_UI_PAGE_DEVICE,
  VG_UI_PAGE_COUNT
} vg_ui_page_t;
typedef enum {
  VG_UI_SHELL_FACE,
  VG_UI_SHELL_APPS,
  VG_UI_SHELL_PAGE,
  VG_UI_SHELL_ABOUT
} vg_ui_shell_t;
typedef enum {
  VG_UI_SWIPE_LEFT,
  VG_UI_SWIPE_RIGHT,
  VG_UI_SWIPE_UP,
  VG_UI_SWIPE_DOWN
} vg_ui_swipe_t;
typedef struct {
  char clock[24];
  char date[32];
  char history[32];
  char primary[VG_TITLE_CAPACITY];
  char countdown[32];
  char footer[64];
  bool clock_valid,has_task,alert;
  unsigned progress;
} vg_ui_home_view_t;
/* Selection and remaining are read-only: never sample runtime clock, tick or save. */
bool vg_ui_pick(const vg_runtime_t *,vg_ui_selection_t *,vg_task_t *);
void vg_ui_next(const vg_runtime_t *,vg_ui_selection_t *);
void vg_ui_capture(vg_ui_target_t *,const vg_task_t *,bool);
/* Compare displayed identity/state/revision before touching the single runtime. */
int vg_ui_apply(vg_runtime_t *,const vg_ui_target_t *,vg_ui_action_t,vg_task_t *);
vg_ui_action_t vg_ui_key_sample(vg_ui_key_t *,bool,uint32_t,const vg_ui_target_t *,vg_ui_target_t *);
bool vg_ui_remaining(const vg_task_t *,const vg_runtime_status_t *,bool,uint64_t,uint64_t *);
bool vg_ui_present(const vg_task_t *,bool,const vg_runtime_status_t *,bool,uint64_t,vg_ui_view_t *);
vg_ui_page_t vg_ui_page_move(vg_ui_page_t,int);
vg_ui_shell_t vg_ui_shell_swipe(vg_ui_shell_t,vg_ui_swipe_t);
const char *vg_ui_page_name(vg_ui_page_t);
bool vg_ui_home_present(const vg_task_t *,const vg_runtime_status_t *,bool,uint64_t,int,vg_ui_home_view_t *);
int vg_ui_demo_id(const vg_runtime_status_t *,uint32_t *,char *,size_t);
unsigned vg_ui_rgb(const vg_runtime_status_t *,bool);
#endif
