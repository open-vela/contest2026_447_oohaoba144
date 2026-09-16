#include <nuttx/config.h>
#include <nuttx/input/buttons.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/timers/rtc.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <lvgl/lvgl.h>
#include "velaguard_commands.h"
#include "velaguard_platform.h"
#include "velaguard_store_file.h"
#include "velaguard_nor.h"
#include "velaguard_clock.h"
#include "velaguard_rgb.h"
#include "velaguard_skill_loader.h"
#include "velaguard_tools.h"
#include "velaguard_agent.h"
#include "velaguard_ui_logic.h"
extern const lv_font_t velaguard_font_cn16;
#include "cJSON.h"
#include <stdlib.h>

#ifndef VG_BUILD_EPOCH
#define VG_BUILD_EPOCH 1789344000LL
#endif
#define VG_MOUNT "/vgdata"
#define VG_DIRECTORY VG_MOUNT "/velaguard"
static vg_runtime_t g_runtime;
static vg_commands_t g_commands;
static vg_line_reader_t g_reader;
static vg_store_file_t g_file;
static vg_store_io_t g_file_io;
static vg_clock_t g_clock;
static bool g_storage, g_nor_mounted, g_display, g_touch;
static vg_ui_selection_t g_selection;
static vg_ui_target_t g_shown, g_ack_press, g_right_press;
static vg_ui_key_t g_key;
static size_t g_previous_alerts;
static char g_notice[96];
static int g_keyfd=-1,g_error,g_storage_errno,g_skill_error,g_ui_error;
static uint32_t g_demo_sequence;
static vg_ui_page_t g_page=VG_UI_PAGE_HOME;
static vg_ui_shell_t g_shell=VG_UI_SHELL_FACE;
static lv_obj_t *g_pages[VG_UI_PAGE_COUNT],*g_nav[VG_UI_PAGE_COUNT];
static lv_obj_t *g_face,*g_apps,*g_about,*g_header_brand;
static lv_obj_t *g_home_arc,*g_home_clock,*g_home_date,*g_home_title,*g_home_countdown,*g_home_footer;
static lv_obj_t *g_home_history,*g_home_health,*g_apps_clock;
static lv_obj_t *g_guard_title,*g_guard_detail,*g_guard_status;
static lv_obj_t *g_task_title,*g_task_detail,*g_task_status,*g_task_action;
static lv_obj_t *g_history_title,*g_history_detail,*g_history_status;
static lv_obj_t *g_device_detail,*g_header_badge;
static lv_obj_t *g_alert_overlay,*g_alert_title,*g_alert_detail,*g_ack,*g_snooze;

static bool g_running, g_skill_loaded;
static int g_rgb_error = -ENODEV;
static unsigned g_rgb_color = 0xffffffffu;
/* 独立只读队列观测原始触摸，不抓取或消费 LVGL 自己的队列。 */
static int g_touch_observer = -1, g_touch_read_errno;
static unsigned g_touch_raw_count, g_touch_polls, g_touch_edges, g_touch_clicks;
static struct touch_sample_s g_touch_last;
static lv_indev_read_cb_t g_touch_original_read;
static lv_indev_data_t g_touch_lv_last;
static void observe_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  g_touch_polls++;
  g_touch_original_read(indev, data);
  if (data->state != g_touch_lv_last.state) g_touch_edges++;
  g_touch_lv_last = *data;
}
static void observe_touch_attach(lv_indev_t *indev)
{
  uint8_t maxpoints = 0;
  if (!indev) return;
  g_touch_original_read = lv_indev_get_read_cb(indev);
  if (g_touch_original_read) lv_indev_set_read_cb(indev, observe_touch_read);
  if (g_touch_observer >= 0) return;
  g_touch_observer = open("/dev/input0", O_RDONLY | O_NONBLOCK);
  if (g_touch_observer < 0) { g_touch_read_errno = errno; return; }
  if (ioctl(g_touch_observer, TSIOC_GETMAXPOINTS, (unsigned long)&maxpoints) < 0 || maxpoints != 1)
    {
      g_touch_read_errno = ENOTSUP;
      close(g_touch_observer); g_touch_observer = -1;
    }
}
static void observe_touch_poll(void)
{
  unsigned budget;
  if (g_touch_observer < 0) return;
  for (budget = 0; budget < 16; budget++)
    {
      struct touch_sample_s sample;
      ssize_t size = read(g_touch_observer, &sample, sizeof(sample));
      if (size == sizeof(sample)) { g_touch_last = sample; g_touch_raw_count++; }
      else
        {
          if (size < 0 && errno != EAGAIN && errno != EINTR) g_touch_read_errno = errno;
          else if (size >= 0) g_touch_read_errno = EIO;
          break;
        }
    }
}
static void observe_click(lv_event_t *event)
{ (void)event; g_touch_clicks++; }

/* 加载失败只影响 Skill 状态，不阻止离线任务恢复和调度。 */
static void load_skill(void)
{
  vg_skill_loader_report_t report = {0};
  char summary[1024], body[2048];
  size_t body_bytes = 0;
  g_skill_error = vg_install_skill();
  if (!g_skill_error)
    g_skill_error = vg_skill_loader_probe(&report, summary, sizeof(summary));
  if (!g_skill_error && report.truncated) g_skill_error = -EOVERFLOW;
  if (!g_skill_error && !report.velaguard_found) g_skill_error = -ENOENT;
  if (!g_skill_error)
    g_skill_error = vg_skill_loader_read("velaguard", body, sizeof(body), &body_bytes);
  if (!g_skill_error && !body_bytes) g_skill_error = -ENODATA;
  g_skill_loaded = g_skill_error == 0;
  printf("VelaGuard Skill load: rc=%d loaded=%d count=%u summary_bytes=%u body_bytes=%u truncated=%d generation=%lu\n",
         g_skill_error, g_skill_loaded, (unsigned)report.skill_count,
         (unsigned)report.summary_bytes, (unsigned)body_bytes, report.truncated,
         (unsigned long)report.refresh_generation);
}

/* 精简工具入口失败时保留旧串口命令和独立的 cron/TaskStore。 */
static void load_agent(void)
{
  static const char *const names[]={"velaguard_create","velaguard_list","velaguard_snooze","velaguard_ack","velaguard_rearm"};
  int rc=vg_agent_init(&g_commands);
  char *schema=NULL;
  cJSON *array=NULL;
  unsigned count=0;
  size_t bytes=0;
  if(!rc)
    {
      schema=vg_tools_schema();
      if(schema){bytes=strlen(schema);array=cJSON_Parse(schema);}
      if(!cJSON_IsArray(array)||cJSON_GetArraySize(array)!=(int)(sizeof(names)/sizeof(names[0])))rc=-EINVAL;
      else for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
        {
          const cJSON *name=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(array,i),"name");
          if(!cJSON_IsString(name)||strcmp(name->valuestring,names[i])){rc=-EINVAL;break;}
          count++;
        }
    }
  cJSON_Delete(array);free(schema);
  if(!rc)g_commands.line_execute=vg_agent_line_execute;
  printf("VelaGuard Agent init: rc=%d tools=%u schema_bytes=%u route=%s\n",
         rc,count,(unsigned)bytes,rc?"direct_fallback":"registry");
  if(!rc)printf("VelaGuard Agent names: velaguard_create velaguard_list velaguard_snooze velaguard_ack velaguard_rearm\n");
}

static int directory(const char *path)
{
  struct stat st;
  if(mkdir(path,0755)<0&&errno!=EEXIST)return -1;
  if(stat(path,&st)<0||!S_ISDIR(st.st_mode)){errno=ENOTDIR;return -1;}return 0;
}
/* Source-backed LittleFS barrier: lfs_file_sync commits its parent directory
 * through lfs_dir_commit before returning. Only accept this exact filesystem.
 * Generic directory-fd fsync is unsupported on this NuttX VFS. Physical NOR
 * power-cut verification remains pending; this is not a generic no-op.
 */
static int littlefs_parent(void *context,const char *path)
{
  struct statfs fs;(void)context;
  if(statfs(path,&fs)<0)return -1;
  if(fs.f_type!=LITTLEFS_SUPER_MAGIC){errno=ENOTSUP;return -1;}return 0;
}
static int storage_init(void)
{
  struct statfs fs;vg_store_file_ops_t ops;int rc;
  if(g_storage)return 0;
  rc=vg_nor_prepare();
  if(rc<0){errno=-rc;goto fail;}
  if(directory(VG_MOUNT)<0)goto fail;
  if(!g_nor_mounted)
    {
      /* 不沿用来源未知的已有挂载，避免绕过缓存同步代理。 */
      if(statfs(VG_MOUNT,&fs)==0&&fs.f_type==LITTLEFS_SUPER_MAGIC)
        {errno=EBUSY;goto fail;}
      /* NULL options: never autoformat, forceformat, or erase. */
      if(mount(VG_NOR_DEVICE,VG_MOUNT,"littlefs",0,NULL)<0)goto fail;
      g_nor_mounted=true;
    }
  if(littlefs_parent(NULL,VG_MOUNT)<0||directory(VG_DIRECTORY)<0)goto fail;
  vg_store_file_native_ops(&ops);ops.sync_parent=littlefs_parent;
  if(vg_store_file_init(&g_file,VG_DIRECTORY,&ops)!=0)goto fail;
  g_file_io=vg_store_file_io(&g_file);g_storage=true;g_storage_errno=0;return 0;
fail:g_storage_errno=errno;return -1;
}
static int store_read(void *context,unsigned slot,char *buf,size_t cap,size_t *n)
{(void)context;if(storage_init()<0)return -1;return g_file_io.read(g_file_io.context,slot,buf,cap,n);}
static int store_write(void *context,unsigned slot,const char *buf,size_t n)
{(void)context;if(!g_storage)return -1;return g_file_io.write_sync(g_file_io.context,slot,buf,n);}
static void clock_diagnostic(const char *operation, int rc)
{
  printf("VelaGuard RTC %s: rc=%d stage=%d raw_year=%d year=%d rtc_epoch=%lld fault=%d\n",
         operation, rc, (int)g_clock.stage, g_clock.raw_year,
         g_clock.normalized_year, (long long)g_clock.rtc_epoch, g_clock.fault);
}
static int read_clock(void *context, int64_t *epoch, bool *valid)
{
  (void)context;
  return vg_clock_read(&g_clock, epoch, valid);
}
static int set_clock(void *context, int64_t epoch)
{
  int rc;
  (void)context;
  rc = vg_clock_set(&g_clock, epoch);
  clock_diagnostic("set", rc);
  /* 校时成功后清除先前因无有效时间而留下的操作提示。 */
  if (!rc && g_error == VG_ERR_INVALID_TIME) g_error = 0;
  return rc;
}
static void diagnostics(void *context,vg_device_diagnostics_t *out)
{
  struct mallinfo info=mallinfo();(void)context;
  printf("VelaGuard input: tick=%lu raw_fd=%d raw=%u flags=%u xy=%d,%d err=%d polls=%u edges=%u state=%d lvxy=%ld,%ld clicks=%u key=%d\n",
         (unsigned long)(g_display ? lv_tick_get() : 0), g_touch_observer, g_touch_raw_count,
         (unsigned)g_touch_last.point[0].flags, g_touch_last.point[0].x, g_touch_last.point[0].y,
         g_touch_read_errno, g_touch_polls, g_touch_edges, (int)g_touch_lv_last.state,
         (long)g_touch_lv_last.point.x, (long)g_touch_lv_last.point.y, g_touch_clicks, g_key.pressed);
  memset(out,0,sizeof(*out));out->display_ready=g_display;out->touch_ready=g_touch;
  out->lcd_exists=access("/dev/lcd0",F_OK)==0;out->rtc_exists=access("/dev/rtc0",F_OK)==0;
  out->storage_ready=g_storage;out->storage_errno=g_storage_errno;
  out->app_error=g_error;out->ui_error=g_ui_error;out->heap_free=info.fordblks;
}
static void emit(void *context,const char *line)
{(void)context;printf("%s\n",line);fflush(stdout);}
static void update_rgb(void)
{
  vg_runtime_status_t status;struct timespec now;unsigned color;
  if(g_rgb_error)return;
  vg_runtime_status(&g_runtime,&status);
  color=vg_ui_rgb(&status,clock_gettime(CLOCK_MONOTONIC,&now)<0||now.tv_nsec<500000000);
  if(color==g_rgb_color)return;
  g_rgb_error=vg_rgb_set((uint8_t)(color>>16),(uint8_t)(color>>8),0);
  if(!g_rgb_error)g_rgb_color=color;
  else printf("VelaGuard RGB write failed: %d\n",g_rgb_error);
}
static void render(void)
{
  vg_task_t task,alert_task;vg_runtime_status_t status;vg_ui_view_t view,alert_view;
  vg_ui_home_view_t home;bool found,has_alert=false;char text[512];
  bool remaining_valid=false;uint64_t now=0,remaining=0;
  update_rgb();if(!g_display)return;
  vg_runtime_status(&g_runtime,&status);
  for(size_t i=0;vg_runtime_list(&g_runtime,false,i,&alert_task)==VG_OK;i++)
    if(alert_task.state==VG_TASK_ALERTING){has_alert=true;break;}
  /* A fresh alert returns to the watch face; the overlay remains authoritative. */
  if(has_alert&&!g_previous_alerts){g_shell=VG_UI_SHELL_FACE;g_page=VG_UI_PAGE_HOME;g_selection.offset=0;g_selection.id[0]=0;}
  g_previous_alerts=status.alerting_count;
  g_selection.history=g_shell==VG_UI_SHELL_PAGE&&g_page==VG_UI_PAGE_HISTORY;
  found=vg_ui_pick(&g_runtime,&g_selection,&task);
  if(found&&task.timer_domain==VG_TIMER_RELATIVE&&vg_cron_monotonic_ms(NULL,&now)==0)
    remaining_valid=vg_ui_remaining(&task,&status,true,now,&remaining);
  (void)vg_ui_present(found?&task:NULL,g_selection.history,&status,remaining_valid,remaining,&view);
  (void)vg_ui_home_present(has_alert?&alert_task:(found&&!g_selection.history?&task:NULL),&status,
                           has_alert?false:remaining_valid,has_alert?0:remaining,480,&home);
  for(int i=0;i<VG_UI_PAGE_COUNT;i++)
    {
      if(g_shell==VG_UI_SHELL_PAGE&&i==(int)g_page)lv_obj_remove_flag(g_pages[i],LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(g_pages[i],LV_OBJ_FLAG_HIDDEN);
      if(g_shell==VG_UI_SHELL_PAGE)lv_obj_remove_flag(g_nav[i],LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(g_nav[i],LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(g_nav[i],lv_color_hex(i==(int)g_page?0x167c80:0x111c2c),0);
      lv_obj_set_style_text_color(g_nav[i],lv_color_hex(i==(int)g_page?0xf3ffff:0x88a0ad),0);
    }
  if(g_shell==VG_UI_SHELL_FACE)lv_obj_remove_flag(g_face,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_face,LV_OBJ_FLAG_HIDDEN);
  if(g_shell==VG_UI_SHELL_APPS)lv_obj_remove_flag(g_apps,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_apps,LV_OBJ_FLAG_HIDDEN);
  if(g_shell==VG_UI_SHELL_ABOUT)lv_obj_remove_flag(g_about,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(g_about,LV_OBJ_FLAG_HIDDEN);
  if(g_shell==VG_UI_SHELL_PAGE){lv_obj_remove_flag(g_header_brand,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(g_header_badge,LV_OBJ_FLAG_HIDDEN);}
  else {lv_obj_add_flag(g_header_brand,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(g_header_badge,LV_OBJ_FLAG_HIDDEN);}
  snprintf(text,sizeof(text),"● %s",status.blocked?"需要检查":"离线守护");
  lv_label_set_text(g_header_badge,text);
  lv_label_set_text(g_home_clock,home.clock);lv_label_set_text(g_home_date,home.date);
  lv_label_set_text(g_home_title,home.primary);
  lv_label_set_text(g_home_countdown,home.countdown);lv_label_set_text(g_home_footer,home.footer);
  lv_label_set_text(g_home_history,home.history);
  lv_label_set_text(g_home_health,status.blocked?"设备需检查":"设备状态正常");
  lv_label_set_text(g_apps_clock,home.clock);
  lv_arc_set_value(g_home_arc,(int32_t)home.progress);
  lv_obj_set_style_arc_color(g_home_arc,lv_color_hex(home.alert?0xff5364:0x2ed3e6),LV_PART_INDICATOR);
  lv_label_set_text(g_guard_status,view.status);lv_label_set_text(g_guard_title,view.title);
  lv_label_set_text(g_guard_detail,view.detail);
  lv_label_set_text(g_task_status,view.status);lv_label_set_text(g_task_title,view.title);
  lv_label_set_text(g_task_detail,view.detail);
  lv_label_set_text(lv_obj_get_child(g_task_action,0),view.right_label);
  if(view.right_enabled)lv_obj_remove_state(g_task_action,LV_STATE_DISABLED);
  else lv_obj_add_state(g_task_action,LV_STATE_DISABLED);
  lv_label_set_text(g_history_status,view.status);lv_label_set_text(g_history_title,view.title);
  lv_label_set_text(g_history_detail,view.detail);
  snprintf(text,sizeof(text),"运行状态  %s\n\n存储空间  %s\n显示屏幕  %s\n触摸输入  %s\n设备时间  %s\n\n任务 %u · 记录 %u\n%s",
           status.blocked?"需要检查":"正常",g_storage?"就绪":"不可用",g_display?"就绪":"不可用",
           g_touch?"就绪":"等待设备",status.rtc_valid?"已同步":"未同步",
           (unsigned)status.active_count,(unsigned)status.history_count,g_notice);
  lv_label_set_text(g_device_detail,text);
  if(has_alert)
    {
      (void)vg_ui_present(&alert_task,false,&status,false,0,&alert_view);
      vg_ui_capture(&g_shown,&alert_task,false);
      lv_label_set_text(g_alert_title,alert_view.title);lv_label_set_text(g_alert_detail,alert_view.detail);
      lv_label_set_text(lv_obj_get_child(g_snooze,0),alert_view.right_label);
      lv_obj_remove_flag(g_alert_overlay,LV_OBJ_FLAG_HIDDEN);lv_obj_move_foreground(g_alert_overlay);
      if(alert_view.done_enabled)lv_obj_remove_state(g_ack,LV_STATE_DISABLED);else lv_obj_add_state(g_ack,LV_STATE_DISABLED);
      if(alert_view.right_enabled)lv_obj_remove_state(g_snooze,LV_STATE_DISABLED);else lv_obj_add_state(g_snooze,LV_STATE_DISABLED);
    }
  else
    {
      vg_ui_capture(&g_shown,found?&task:NULL,g_selection.history);
      lv_obj_add_flag(g_alert_overlay,LV_OBJ_FLAG_HIDDEN);
    }
  lv_obj_invalidate(lv_screen_active());
}
static void user_action(const vg_ui_target_t *target,vg_ui_action_t action)
{
  vg_task_t task;
  g_error=vg_ui_apply(&g_runtime,target,action,&task);
  if(!g_error)snprintf(g_notice,sizeof(g_notice),"%s",action==VG_UI_DONE?"Completed. Saved in Records.":
                      target->state==VG_TASK_NEEDS_RESET?"Timer restarted.":"Snoozed for 60 seconds.");
  else snprintf(g_notice,sizeof(g_notice),"Action failed (%d). Check task.",g_error);
  render();
}
static void press_ack(lv_event_t *event){(void)event;g_ack_press=g_shown;}
static void press_right(lv_event_t *event){(void)event;g_right_press=g_shown;}
static void click_ack(lv_event_t *event){(void)event;user_action(&g_ack_press,VG_UI_DONE);}
static void click_snooze(lv_event_t *event){(void)event;user_action(&g_right_press,VG_UI_RIGHT);}
static void click_next(lv_event_t *event)
{(void)event;vg_ui_next(&g_runtime,&g_selection);render();}
static void select_page(vg_ui_page_t page)
{g_shell=VG_UI_SHELL_PAGE;g_page=page;g_selection.offset=0;g_selection.id[0]=0;render();}
static void click_nav(lv_event_t *event)
{select_page((vg_ui_page_t)(uintptr_t)lv_event_get_user_data(event));}
static void swipe_page(lv_event_t *event)
{
  lv_indev_t *indev=lv_indev_active();lv_dir_t direction;vg_ui_swipe_t swipe;(void)event;
  if(!indev||g_previous_alerts)return;
  direction=lv_indev_get_gesture_dir(indev);
  if(direction==LV_DIR_LEFT)swipe=VG_UI_SWIPE_LEFT;
  else if(direction==LV_DIR_RIGHT)swipe=VG_UI_SWIPE_RIGHT;
  else if(direction==LV_DIR_TOP)swipe=VG_UI_SWIPE_UP;
  else if(direction==LV_DIR_BOTTOM)swipe=VG_UI_SWIPE_DOWN;
  else return;
  if(g_shell==VG_UI_SHELL_PAGE&&swipe==VG_UI_SWIPE_LEFT)g_page=vg_ui_page_move(g_page,1);
  else if(g_shell==VG_UI_SHELL_PAGE&&swipe==VG_UI_SWIPE_RIGHT)g_page=vg_ui_page_move(g_page,-1);
  g_shell=vg_ui_shell_swipe(g_shell,swipe);g_selection.offset=0;g_selection.id[0]=0;render();
}
static void click_demo(lv_event_t *event)
{
  vg_create_request_t req={0};vg_task_t task;vg_runtime_status_t status;(void)event;
  vg_runtime_status(&g_runtime,&status);
  g_error=vg_ui_demo_id(&status,&g_demo_sequence,req.request_id,sizeof(req.request_id));
  if(!g_error){strcpy(req.title,"喝水提醒");req.priority=1;
    g_error=vg_runtime_create_relative(&g_runtime,&req,60,&task);}
  if(!g_error){g_shell=VG_UI_SHELL_FACE;g_selection.history=false;strcpy(g_selection.id,task.request.request_id);
    snprintf(g_notice,sizeof(g_notice),"60 second timer created.");}
  else snprintf(g_notice,sizeof(g_notice),"Create failed (%d).",g_error);
  render();
}
static void click_about(lv_event_t *event)
{(void)event;g_shell=VG_UI_SHELL_ABOUT;render();}
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int width,const char *text)
{
  lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
  lv_label_set_text(o,text);lv_obj_set_style_text_color(o,lv_color_hex(0xf0f4f5),0);return o;
}
static lv_obj_t *button(lv_obj_t *parent,int x,int y,int width,int height,const char *text,lv_event_cb_t cb)
{
  lv_obj_t *o=lv_button_create(parent),*l;lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,45);
  lv_obj_set_height(o,height);lv_obj_set_style_bg_color(o,lv_color_hex(0x167c80),0);
  lv_obj_set_style_radius(o,14,0);lv_obj_set_style_shadow_width(o,0,0);
  l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_center(l);
  lv_obj_add_event_cb(o,observe_click,LV_EVENT_CLICKED,NULL);
  if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,NULL);
  return o;
}
static lv_obj_t *card(lv_obj_t *parent,int x,int y,int width,int height,unsigned color)
{
  lv_obj_t *o=lv_obj_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
  lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
  lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_border_width(o,1,0);
  lv_obj_set_style_border_color(o,lv_color_hex(0x263c4c),0);lv_obj_set_style_radius(o,20,0);
  lv_obj_set_style_pad_all(o,0,0);return o;
}
static lv_obj_t *round_app(lv_obj_t *parent,int x,int y,int size,const char *text,unsigned color,
                           lv_event_cb_t cb,void *data)
{
  lv_obj_t *o=button(parent,x,y,size,size,text,NULL);
  lv_obj_set_style_radius(o,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
  if(cb)lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,data);
  return o;
}
static void ui_init(void)
{
  lv_nuttx_dsc_t info;lv_nuttx_result_t result;lv_obj_t *screen,*o,*c;
  /* LCD registration is asynchronous and may finish after application start.
   * Do not initialise LVGL until the device exists; main keeps UART alive. */
  if(access("/dev/lcd0",F_OK)<0){g_ui_error=-102;return;}
  if(lv_is_initialized()){g_ui_error=-100;return;}
  lv_init();lv_nuttx_dsc_init(&info);info.fb_path="/dev/lcd0";info.input_path="/dev/input0";
  lv_nuttx_init(&info,&result);
  if(!result.disp){g_ui_error=-101;lv_nuttx_deinit(&result);lv_deinit();return;}
  g_ui_error=0;g_display=true;g_touch=result.indev!=NULL;screen=lv_screen_active();
  observe_touch_attach(result.indev);
  lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_font(screen,&velaguard_font_cn16,0);
  lv_obj_set_style_text_line_space(screen,0,0);
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x070b14),0);
  lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
  g_header_brand=label(screen,18,13,180,"VELAGUARD");lv_obj_set_style_text_color(g_header_brand,lv_color_hex(0x2ed3e6),0);
  g_header_badge=label(screen,235,13,138,"");lv_obj_set_style_text_align(g_header_badge,LV_TEXT_ALIGN_RIGHT,0);
  for(int i=0;i<VG_UI_PAGE_COUNT;i++)
    {
      g_pages[i]=lv_obj_create(screen);lv_obj_set_pos(g_pages[i],14,45);lv_obj_set_size(g_pages[i],362,332);
      lv_obj_remove_flag(g_pages[i],LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_opa(g_pages[i],LV_OPA_TRANSP,0);
      lv_obj_set_style_border_width(g_pages[i],0,0);lv_obj_set_style_pad_all(g_pages[i],0,0);
      g_nav[i]=button(screen,12+i*92,389,86,49,vg_ui_page_name((vg_ui_page_t)i),NULL);
      lv_obj_add_event_cb(g_nav[i],click_nav,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
    }
  /* Guard page: task detail remains available from the launcher. */
  g_guard_status=label(g_pages[VG_UI_PAGE_HOME],12,8,338,"");lv_obj_set_style_text_color(g_guard_status,lv_color_hex(0x7893a5),0);
  c=card(g_pages[VG_UI_PAGE_HOME],12,38,338,196,0x101a29);
  o=label(c,18,16,302,"离线守护");lv_obj_set_style_text_color(o,lv_color_hex(0x2ed3e6),0);
  g_guard_title=label(c,18,54,302,"");g_guard_detail=label(c,18,90,302,"");
  button(g_pages[VG_UI_PAGE_HOME],12,246,158,54,"Demo 60s",click_demo);
  button(g_pages[VG_UI_PAGE_HOME],182,246,168,54,"查看下一项",click_next);
  /* Default watch face. */
  g_face=lv_obj_create(screen);lv_obj_set_pos(g_face,0,0);lv_obj_set_size(g_face,390,450);
  lv_obj_remove_flag(g_face,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_border_width(g_face,0,0);
  lv_obj_set_style_pad_all(g_face,0,0);lv_obj_set_style_bg_color(g_face,lv_color_hex(0x07151e),0);
  lv_obj_set_style_bg_opa(g_face,LV_OPA_COVER,0);
  g_home_date=label(g_face,20,22,350,"");lv_obj_set_style_text_align(g_home_date,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_color(g_home_date,lv_color_hex(0x8ca6b4),0);
  g_home_clock=label(g_face,20,58,350,"");lv_obj_set_style_text_align(g_home_clock,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_transform_scale_x(g_home_clock,760,0);lv_obj_set_style_transform_scale_y(g_home_clock,760,0);
  g_home_arc=lv_arc_create(g_face);lv_obj_set_pos(g_home_arc,110,125);lv_obj_set_size(g_home_arc,170,170);
  lv_arc_set_range(g_home_arc,0,100);lv_arc_set_rotation(g_home_arc,135);lv_arc_set_bg_angles(g_home_arc,0,270);
  lv_obj_remove_style(g_home_arc,NULL,LV_PART_KNOB);lv_obj_remove_flag(g_home_arc,LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(g_home_arc,12,LV_PART_MAIN);lv_obj_set_style_arc_width(g_home_arc,12,LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_home_arc,lv_color_hex(0x17283a),LV_PART_MAIN);
  g_home_countdown=label(g_face,120,181,150,"");lv_obj_set_style_text_align(g_home_countdown,LV_TEXT_ALIGN_CENTER,0);
  g_home_title=label(g_face,120,218,150,"");lv_obj_set_style_text_align(g_home_title,LV_TEXT_ALIGN_CENTER,0);
  c=card(g_face,18,316,170,76,0x102837);g_home_history=label(c,12,27,146,"");
  o=label(c,12,8,146,"守护记录");lv_obj_set_style_text_color(o,lv_color_hex(0x7893a5),0);
  c=card(g_face,202,316,170,76,0x152337);g_home_health=label(c,12,27,146,"");
  o=label(c,12,8,146,"设备健康");lv_obj_set_style_text_color(o,lv_color_hex(0x7893a5),0);
  g_home_footer=label(g_face,20,416,350,"上滑进入应用");lv_obj_set_style_text_align(g_home_footer,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_color(g_home_footer,lv_color_hex(0x7893a5),0);
  /* Honeycomb launcher: six clear touch targets, all implemented with LVGL primitives. */
  g_apps=lv_obj_create(screen);lv_obj_set_pos(g_apps,0,0);lv_obj_set_size(g_apps,390,450);
  lv_obj_remove_flag(g_apps,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_border_width(g_apps,0,0);
  lv_obj_set_style_pad_all(g_apps,0,0);lv_obj_set_style_bg_color(g_apps,lv_color_hex(0x05090f),0);
  o=label(g_apps,18,18,220,"应用");lv_obj_set_style_text_color(o,lv_color_hex(0x2ed3e6),0);
  g_apps_clock=label(g_apps,260,18,110,"");lv_obj_set_style_text_align(g_apps_clock,LV_TEXT_ALIGN_RIGHT,0);
  round_app(g_apps,54,88,82,"守护",0x167c80,click_nav,(void *)(uintptr_t)VG_UI_PAGE_HOME);
  round_app(g_apps,154,64,82,"任务",0x3a6fc4,click_nav,(void *)(uintptr_t)VG_UI_PAGE_TASKS);
  round_app(g_apps,254,88,82,"记录",0x4a9157,click_nav,(void *)(uintptr_t)VG_UI_PAGE_HISTORY);
  round_app(g_apps,70,194,82,"设备",0x7352ad,click_nav,(void *)(uintptr_t)VG_UI_PAGE_DEVICE);
  round_app(g_apps,170,218,82,"Demo",0xb96335,click_demo,NULL);
  round_app(g_apps,270,194,82,"关于",0x42627b,click_about,NULL);
  o=label(g_apps,20,410,350,"下滑返回表盘");lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_color(o,lv_color_hex(0x7893a5),0);
  /* About panel for competition presentation. */
  g_about=lv_obj_create(screen);lv_obj_set_pos(g_about,0,0);lv_obj_set_size(g_about,390,450);
  lv_obj_remove_flag(g_about,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_border_width(g_about,0,0);
  lv_obj_set_style_pad_all(g_about,0,0);lv_obj_set_style_bg_color(g_about,lv_color_hex(0x07151e),0);
  o=label(g_about,24,36,342,"VelaGuard");lv_obj_set_style_text_color(o,lv_color_hex(0x2ed3e6),0);
  lv_obj_set_style_transform_scale_x(o,420,0);lv_obj_set_style_transform_scale_y(o,420,0);
  c=card(g_about,20,104,350,224,0x102332);
  o=label(c,20,22,310,"离线任务守护助手\n\n定时提醒 · 本地确认\n掉电持久化 · 设备自检\n\nOpenVela 2026 参赛作品");
  lv_obj_set_style_text_line_space(o,9,0);
  o=label(g_about,20,410,350,"下滑返回表盘");lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_color(o,lv_color_hex(0x7893a5),0);
  /* Task page. */
  g_task_status=label(g_pages[VG_UI_PAGE_TASKS],12,8,338,"");lv_obj_set_style_text_color(g_task_status,lv_color_hex(0x7893a5),0);
  c=card(g_pages[VG_UI_PAGE_TASKS],12,38,338,196,0x101a29);
  g_task_title=label(c,18,18,302,"");g_task_detail=label(c,18,56,302,"");lv_obj_set_height(g_task_detail,112);
  g_task_action=button(g_pages[VG_UI_PAGE_TASKS],12,246,158,54,"重新计时",click_snooze);
  lv_obj_add_event_cb(g_task_action,press_right,LV_EVENT_PRESSED,NULL);
  button(g_pages[VG_UI_PAGE_TASKS],182,246,76,54,"下一个",click_next);
  button(g_pages[VG_UI_PAGE_TASKS],270,246,80,54,"Demo",click_demo);
  /* History page. */
  g_history_status=label(g_pages[VG_UI_PAGE_HISTORY],12,8,338,"");lv_obj_set_style_text_color(g_history_status,lv_color_hex(0x7893a5),0);
  c=card(g_pages[VG_UI_PAGE_HISTORY],12,38,338,210,0x101a29);
  o=label(c,18,16,302,"守护记录");lv_obj_set_style_text_color(o,lv_color_hex(0x8be28b),0);
  g_history_title=label(c,18,56,302,"");g_history_detail=label(c,18,92,302,"");
  button(g_pages[VG_UI_PAGE_HISTORY],12,260,338,54,"查看下一条记录",click_next);
  /* Device page. */
  c=card(g_pages[VG_UI_PAGE_DEVICE],12,8,338,292,0x101a29);
  o=label(c,18,18,302,"设备健康");lv_obj_set_style_text_color(o,lv_color_hex(0x8be28b),0);
  g_device_detail=label(c,18,56,302,"");lv_obj_set_style_text_line_space(g_device_detail,8,0);
  /* Full-screen alert owns the task target until ACK or Snooze completes. */
  g_alert_overlay=lv_obj_create(screen);lv_obj_set_pos(g_alert_overlay,0,0);lv_obj_set_size(g_alert_overlay,390,450);
  lv_obj_remove_flag(g_alert_overlay,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(g_alert_overlay,lv_color_hex(0x3a101b),0);
  lv_obj_set_style_bg_opa(g_alert_overlay,LV_OPA_COVER,0);lv_obj_set_style_border_width(g_alert_overlay,0,0);
  o=label(g_alert_overlay,20,42,350,"● 现在提醒");lv_obj_set_style_text_color(o,lv_color_hex(0xff8b95),0);
  g_alert_title=label(g_alert_overlay,20,102,350,"");lv_obj_set_style_transform_scale_x(g_alert_title,360,0);
  lv_obj_set_style_transform_scale_y(g_alert_title,360,0);
  g_alert_detail=label(g_alert_overlay,20,190,350,"");lv_obj_set_style_text_line_space(g_alert_detail,6,0);
  g_ack=button(g_alert_overlay,20,342,165,62,"完成",click_ack);
  g_snooze=button(g_alert_overlay,205,342,165,62,"延后 60 秒",click_snooze);
  lv_obj_set_style_bg_color(g_ack,lv_color_hex(0x2b9a74),0);lv_obj_set_style_bg_color(g_snooze,lv_color_hex(0x9b5535),0);
  lv_obj_add_event_cb(g_ack,press_ack,LV_EVENT_PRESSED,NULL);lv_obj_add_event_cb(g_snooze,press_right,LV_EVENT_PRESSED,NULL);
  lv_obj_add_event_cb(screen,swipe_page,LV_EVENT_GESTURE,NULL);
  lv_obj_add_flag(g_alert_overlay,LV_OBJ_FLAG_HIDDEN);render();
}
int main(int argc,char *argv[])
{
  vg_runtime_deps_t deps={{NULL,store_read,store_write},NULL,read_clock,vg_cron_reconcile};
  vg_runtime_timer_ops_t timers={NULL,vg_cron_monotonic_ms,vg_cron_reconcile_mono};
  vg_clock_ops_t clock_ops;
  int clock_rc;
  struct termios term;btn_buttonset_t supported=0,buttons=0;
  unsigned service_cycles=0;
  uint32_t last_render=0;char input[128];ssize_t n;int flags;
  (void)argc;(void)argv;
  if(g_running){fprintf(stderr,"VelaGuard already running\n");return 1;}g_running=true;
  (void)directory("/data");
  load_skill();
  /* 在调度器和持久任务恢复前修正 RTC 世纪；失败保留同步命令入口。 */
  clock_rc = vg_clock_native_ops(&clock_ops);
  if (!clock_rc) clock_rc = vg_clock_start(&g_clock, &clock_ops, VG_BUILD_EPOCH);
  else g_clock.fault = true;
  clock_diagnostic("boot", clock_rc);
  /* Initialize the XIP-backed NOR before starting the cron worker.
   * Keep the result visible; runtime still binds its complete timer deps. */
  int storage_rc=storage_init();
  printf("VelaGuard storage preflight: rc=%d errno=%d\n",storage_rc,g_storage_errno);
  /* Always bind timed dependencies: a latched cron fault must survive reload. */
  int cron_rc=vg_cron_init();
  g_error=vg_runtime_start_with_timers(&g_runtime,&deps,&timers);
  printf("VelaGuard timed startup: cron=%d runtime=%d\n",cron_rc,g_error);
  g_rgb_error = vg_rgb_init();
  printf("VelaGuard RGB init: %d\n", g_rgb_error);
  printf("VelaGuard: UART JSON Lines v1, NOR=%s source=%s backing=%s, start=%d, store_bytes=%u\n",
         VG_DIRECTORY,VG_NOR_DEVICE,VG_NOR_SOURCE,g_error,(unsigned)sizeof(g_runtime.store));
  flags=fcntl(STDIN_FILENO,F_GETFL,0);
  if(flags>=0)fcntl(STDIN_FILENO,F_SETFL,flags|O_NONBLOCK);
  if(tcgetattr(STDIN_FILENO,&term)==0)
    {
      term.c_lflag&=~(ICANON|ECHO);term.c_cc[VMIN]=0;term.c_cc[VTIME]=0;
      tcsetattr(STDIN_FILENO,TCSANOW,&term);
    }
  g_keyfd=open("/dev/buttons",O_RDONLY|O_NONBLOCK);
  if(g_keyfd>=0 && (ioctl(g_keyfd,BTNIOC_SUPPORTED,(unsigned long)&supported)<0 || !(supported&1)))
    {close(g_keyfd);g_keyfd=-1;}
  vg_commands_init(&g_commands,&g_runtime,NULL,set_clock);
  g_commands.get_diagnostics=diagnostics;
  load_agent();
  ui_init();
  for(;;)
    {
      /* UI polling is not a reminder scheduler. Only the official cron wake
       * asks the runtime to reconcile time-triggered transitions. */
      if(vg_cron_take_event()){g_error=vg_runtime_tick(&g_runtime);render();}
      n=read(STDIN_FILENO,input,sizeof(input));
      if(n>0)vg_line_feed(&g_reader,&g_commands,input,(size_t)n,emit,NULL);
      if(++service_cycles%20==0)
        {
          update_rgb();
          if(!g_display)ui_init();
          else if(!g_touch&&access("/dev/input0",F_OK)==0)
            {
              lv_indev_t *indev=lv_nuttx_touchscreen_create("/dev/input0");
              g_touch=indev!=NULL;observe_touch_attach(indev);
            }
        }
      observe_touch_poll();
      if(g_display)
        {
          uint32_t now=lv_tick_get();
          if(g_keyfd>=0 && read(g_keyfd,&buttons,sizeof(buttons))==sizeof(buttons))
            {
              vg_ui_target_t target;
              vg_ui_action_t action=vg_ui_key_sample(&g_key,(buttons&1)!=0,now,&g_shown,&target);
              if(action!=VG_UI_NONE)user_action(&target,action);
            }
          /* Keep a held key progressing even if the driver reports only edges. */
          if(g_key.pressed){
            vg_ui_target_t target;
            vg_ui_action_t action=vg_ui_key_sample(&g_key,true,now,&g_shown,&target);
            if(action!=VG_UI_NONE)user_action(&target,action);
          }
          if(now-last_render>=500){last_render=now;render();}
          lv_timer_handler();
        }
      usleep(10000);
    }
  return 0;
}
