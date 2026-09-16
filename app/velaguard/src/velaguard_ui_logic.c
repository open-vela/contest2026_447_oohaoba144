#include "velaguard_ui_logic.h"
#include <stdio.h>
#include <string.h>

vg_ui_page_t vg_ui_page_move(vg_ui_page_t page,int direction)
{
  int next=(int)page+(direction<0?-1:1);
  if(next<0)next=VG_UI_PAGE_COUNT-1;
  if(next>=VG_UI_PAGE_COUNT)next=0;
  return (vg_ui_page_t)next;
}
vg_ui_shell_t vg_ui_shell_swipe(vg_ui_shell_t shell,vg_ui_swipe_t swipe)
{
  if(shell==VG_UI_SHELL_FACE&&swipe==VG_UI_SWIPE_UP)return VG_UI_SHELL_APPS;
  if(shell!=VG_UI_SHELL_FACE&&swipe==VG_UI_SWIPE_DOWN)return VG_UI_SHELL_FACE;
  return shell;
}
const char *vg_ui_page_name(vg_ui_page_t page)
{
  static const char *const names[VG_UI_PAGE_COUNT]={"守护","任务","记录","设备"};
  return page>=0&&page<VG_UI_PAGE_COUNT?names[page]:"";
}
bool vg_ui_home_present(const vg_task_t *task,const vg_runtime_status_t *status,
                        bool remaining_valid,uint64_t remaining,int timezone_minutes,
                        vg_ui_home_view_t *out)
{
  int64_t local,second_of_day,days,z,era,doe,yoe,doy,mp;
  int year,month,day,weekday;unsigned hour,minute;
  static const char *const weekdays[]={"周日","周一","周二","周三","周四","周五","周六"};
  if(!status||!out)return false;
  memset(out,0,sizeof(*out));
  out->clock_valid=status->rtc_valid&&status->now_epoch>0;
  if(out->clock_valid)
    {
      local=status->now_epoch+(int64_t)timezone_minutes*60;
      second_of_day=local%86400;if(second_of_day<0)second_of_day+=86400;
      hour=(unsigned)(second_of_day/3600);minute=(unsigned)(second_of_day%3600/60);
      snprintf(out->clock,sizeof(out->clock),"%02u:%02u",hour,minute);
      days=(local-second_of_day)/86400;
      z=days+719468;era=(z>=0?z:z-146096)/146097;doe=z-era*146097;
      yoe=(doe-doe/1460+doe/36524-doe/146096)/365;year=(int)(yoe+era*400);
      doy=doe-(365*yoe+yoe/4-yoe/100);mp=(5*doy+2)/153;
      day=(int)(doy-(153*mp+2)/5+1);month=(int)(mp+(mp<10?3:-9));year+=month<=2;
      weekday=(int)((days+4)%7);if(weekday<0)weekday+=7;
      snprintf(out->date,sizeof(out->date),"%02d月%02d日  %s",month,day,weekdays[weekday]);
    }
  else {snprintf(out->clock,sizeof(out->clock),"时间未同步");snprintf(out->date,sizeof(out->date),"日期待同步");}
  snprintf(out->history,sizeof(out->history),"历史记录 %u",(unsigned)status->history_count);
  out->has_task=task!=NULL;
  out->alert=task&&task->state==VG_TASK_ALERTING;
  snprintf(out->primary,sizeof(out->primary),"%s",task?task->request.title:"暂无待办");
  if(out->alert)
    {
      snprintf(out->countdown,sizeof(out->countdown),"现在提醒");out->progress=100;
    }
  else if(task&&remaining_valid)
    {
      snprintf(out->countdown,sizeof(out->countdown),"%llu 秒",(unsigned long long)remaining);
      if(task->delay_seconds)
        out->progress=(unsigned)(remaining>=task->delay_seconds?100:(remaining*100)/task->delay_seconds);
    }
  else snprintf(out->countdown,sizeof(out->countdown),"%s",task?"等待计时":"可以休息一下");
  snprintf(out->footer,sizeof(out->footer),"上滑进入应用 · %s",status->blocked?"设备需检查":"离线守护中");
  return true;
}

bool vg_ui_pick(const vg_runtime_t *r,vg_ui_selection_t *s,vg_task_t *out)
{
  vg_task_t t;size_t count=0;
  if(!r||!s||!out)return false;
  while(vg_runtime_list(r,s->history,count,&t)==VG_OK){
    if(s->id[0]&&!strcmp(s->id,t.request.request_id)){s->offset=count;*out=t;return true;}
    count++;
  }
  if(!count){s->id[0]=0;s->offset=0;return false;}
  if(s->offset>=count)s->offset=count-1;
  if(vg_runtime_list(r,s->history,s->offset,out)!=VG_OK)return false;
  strcpy(s->id,out->request.request_id);return true;
}
void vg_ui_next(const vg_runtime_t *r,vg_ui_selection_t *s)
{
  vg_runtime_status_t status;vg_task_t t;size_t count;
  if(!r||!s)return;
  (void)vg_ui_pick(r,s,&t);vg_runtime_status(r,&status);
  count=s->history?status.history_count:status.active_count;
  s->offset=count?(s->offset+1)%count:0;s->id[0]=0;
}
void vg_ui_capture(vg_ui_target_t *out,const vg_task_t *t,bool history)
{
  memset(out,0,sizeof(*out));if(!t||history)return;
  out->valid=true;strcpy(out->id,t->request.request_id);out->state=t->state;
  out->domain=t->timer_domain;out->revision=t->timer_revision;out->due_epoch=t->request.due_epoch;
}
int vg_ui_apply(vg_runtime_t *r,const vg_ui_target_t *target,vg_ui_action_t action,vg_task_t *out)
{
  vg_task_t t;bool history;
  if(!r||!target||!target->valid)return VG_ERR_INVALID_STATE;
  if(vg_runtime_find(r,target->id,&t,&history)!=VG_OK||history||t.state!=target->state||
     t.timer_domain!=target->domain||t.timer_revision!=target->revision||t.request.due_epoch!=target->due_epoch)
    return VG_ERR_INVALID_STATE;
  if(action==VG_UI_DONE&&t.state==VG_TASK_ALERTING)return vg_runtime_ack(r,target->id,out);
  if(action==VG_UI_RIGHT){
    if(t.timer_domain==VG_TIMER_RELATIVE){
      if(t.state==VG_TASK_NEEDS_RESET)return vg_runtime_rearm_relative(r,target->id,target->revision,out);
      if(t.state==VG_TASK_ALERTING)return vg_runtime_snooze_relative(r,target->id,60,target->revision,out);
    }else if(t.state==VG_TASK_ALERTING)return vg_runtime_snooze(r,target->id,60,out);
  }
  return VG_ERR_INVALID_STATE;
}
vg_ui_action_t vg_ui_key_sample(vg_ui_key_t *key,bool down,uint32_t now,const vg_ui_target_t *shown,vg_ui_target_t *out)
{
  vg_ui_action_t action=VG_UI_NONE;
  if(down&&!key->pressed){key->pressed_at=now;key->long_fired=false;key->target=*shown;}
  if(key->pressed&&!key->long_fired&&(uint32_t)(now-key->pressed_at)>=1000){key->long_fired=true;action=VG_UI_RIGHT;}
  else if(!down&&key->pressed&&!key->long_fired)action=VG_UI_DONE;
  key->pressed=down;if(action!=VG_UI_NONE)*out=key->target;return action;
}
bool vg_ui_remaining(const vg_task_t *t,const vg_runtime_status_t *s,bool valid,uint64_t now,uint64_t *seconds)
{
  uint64_t delta;
  if(!t||!s||!seconds||!valid||!s->ready||s->blocked||!s->mono_valid||!s->boot_ready||
     t->timer_domain!=VG_TIMER_RELATIVE||t->timer_boot_id!=s->active_boot_id||now<s->now_mono_ms||
     (t->state!=VG_TASK_CREATED&&t->state!=VG_TASK_SCHEDULED&&t->state!=VG_TASK_SNOOZED))return false;
  delta=t->mono_deadline_ms>now?t->mono_deadline_ms-now:0;*seconds=delta/1000+(delta%1000!=0);return true;
}
int vg_ui_demo_id(const vg_runtime_status_t *s,uint32_t *seq,char *out,size_t cap)
{
  char id[VG_REQUEST_ID_CAPACITY];int n;
  if(!s||!seq||!out||!s->ready||s->blocked||!s->boot_ready||!s->active_boot_id)return VG_RUNTIME_BLOCKED;
  if(*seq==UINT32_MAX)return VG_ERR_CAPACITY;
  n=snprintf(id,sizeof(id),"demo-%llu-%lu",(unsigned long long)s->active_boot_id,(unsigned long)(*seq+1));
  if(n<0||(size_t)n>=sizeof(id)||(size_t)n>=cap)return VG_ERR_CAPACITY;
  memcpy(out,id,(size_t)n+1);(*seq)++;return VG_OK;
}
unsigned vg_ui_rgb(const vg_runtime_status_t *s,bool phase)
{
  if(s->blocked||!s->ready)return 0x080400;
  if(s->alerting_count)return phase?0x100000:0;
  return s->rtc_valid?0x000400:0x080400;
}

static const char *ui_state(vg_task_state_t state)
{
  switch(state)
    {
      case VG_TASK_CREATED:return "已创建";
      case VG_TASK_SCHEDULED:return "等待提醒";
      case VG_TASK_ALERTING:return "正在提醒";
      case VG_TASK_ACKNOWLEDGED:return "已完成";
      case VG_TASK_SNOOZED:return "已延后";
      case VG_TASK_MISSED:return "已错过";
      case VG_TASK_NEEDS_RESET:return "需要重新计时";
      default:return "未知状态";
    }
}
bool vg_ui_present(const vg_task_t *task,bool history,const vg_runtime_status_t *status,
                   bool remaining_valid,uint64_t remaining,vg_ui_view_t *out)
{
  const bool found=task!=NULL;
  if(!status||!out)return false;
  memset(out,0,sizeof(*out));
  if(history)snprintf(out->status,sizeof(out->status),"记录 %u / 任务 %u",
                      (unsigned)status->history_count,(unsigned)status->active_count);
  else snprintf(out->status,sizeof(out->status),"任务 %u / 记录 %u",
                (unsigned)status->active_count,(unsigned)status->history_count);
  snprintf(out->title,sizeof(out->title),"%s",found?task->request.title:"暂无任务");
  snprintf(out->right_label,sizeof(out->right_label),"%s",found&&task->state==VG_TASK_NEEDS_RESET?"Rearm":"Snooze 60s");
  out->alert=found&&!history&&task->state==VG_TASK_ALERTING;
  out->background=out->alert?0x481f2a:0x10252f;
  out->done_enabled=out->alert&&!status->blocked;
  out->right_enabled=found&&!history&&!status->blocked&&(task->state==VG_TASK_ALERTING||task->state==VG_TASK_NEEDS_RESET);
  if(!found)snprintf(out->detail,sizeof(out->detail),"点击 Demo 60s 创建喝水提醒\n相对计时无需联网或校时");
  else if(task->state==VG_TASK_NEEDS_RESET)snprintf(out->detail,sizeof(out->detail),"状态：%s\n原计时：%lu 秒\n点击 Rearm 重新开始",ui_state(task->state),(unsigned long)task->delay_seconds);
  else if(out->alert)snprintf(out->detail,sizeof(out->detail),"状态：%s\n请完成或延后\n已延后：%lu 次",ui_state(task->state),(unsigned long)task->snooze_count);
  else if(task->timer_domain==VG_TIMER_RELATIVE&&remaining_valid)snprintf(out->detail,sizeof(out->detail),"状态：%s\n剩余：%llu 秒\n已延后：%lu 次",ui_state(task->state),(unsigned long long)remaining,(unsigned long)task->snooze_count);
  else if(history)snprintf(out->detail,sizeof(out->detail),"状态：%s\n记录已保存\n已延后：%lu 次",ui_state(task->state),(unsigned long)task->snooze_count);
  else snprintf(out->detail,sizeof(out->detail),"状态：%s\n计时：%lu 秒\n已延后：%lu 次",ui_state(task->state),(unsigned long)task->delay_seconds,(unsigned long)task->snooze_count);
  return true;
}
