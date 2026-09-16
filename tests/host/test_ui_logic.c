#include "velaguard_ui_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char disk[2][VG_STORE_MAX_BYTES];static size_t sizes[2];
static vg_runtime_t r,rebooted;static unsigned writes,samples,schedules;static uint64_t mono=100;
static int64_t wall_now;static bool wall_valid;
static int rd(void *c,unsigned s,char *o,size_t cap,size_t *n){(void)c;if(!sizes[s])return 1;assert(cap>=sizes[s]);*n=sizes[s];memcpy(o,disk[s],*n);return 0;}
static int wr(void *c,unsigned s,const char *v,size_t n){(void)c;writes++;memcpy(disk[s],v,n);sizes[s]=n;return 0;}
static int wall(void *c,int64_t *n,bool *v){(void)c;samples++;*n=wall_now;*v=wall_valid;return 0;}
static int uptime(void *c,uint64_t *n){(void)c;samples++;*n=mono;return 0;}
static int sched(void *c,bool has,uint64_t n){(void)c;(void)has;(void)n;schedules++;return 0;}
static vg_runtime_deps_t deps={{NULL,rd,wr},NULL,wall,NULL};
static vg_runtime_timer_ops_t timers={NULL,uptime,sched};
static vg_task_t create(const char *id){vg_create_request_t q={0};vg_task_t t;strcpy(q.request_id,id);strcpy(q.title,"Drink water");q.priority=1;assert(vg_runtime_create_relative(&r,&q,1,&t)==0);return t;}
int main(void){
 vg_task_t t,out;vg_runtime_status_t s;vg_ui_selection_t sel={0};vg_ui_target_t target,other,action_target;vg_ui_key_t key={0};vg_ui_view_t view;vg_ui_home_view_t home;uint64_t remaining;uint32_t seq=0;char id[65];
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);t=create("first");create("second");vg_runtime_status(&r,&s);
 /* Watch shell opens the app honeycomb vertically and keeps page navigation separate. */
 assert(vg_ui_shell_swipe(VG_UI_SHELL_FACE,VG_UI_SWIPE_UP)==VG_UI_SHELL_APPS);
 assert(vg_ui_shell_swipe(VG_UI_SHELL_APPS,VG_UI_SWIPE_DOWN)==VG_UI_SHELL_FACE);
 assert(vg_ui_shell_swipe(VG_UI_SHELL_PAGE,VG_UI_SWIPE_DOWN)==VG_UI_SHELL_FACE);
 assert(vg_ui_shell_swipe(VG_UI_SHELL_ABOUT,VG_UI_SWIPE_DOWN)==VG_UI_SHELL_FACE);
 assert(vg_ui_shell_swipe(VG_UI_SHELL_FACE,VG_UI_SWIPE_LEFT)==VG_UI_SHELL_FACE);
 /* The four presentation pages wrap in both directions without mutating runtime. */
 assert(vg_ui_page_move(VG_UI_PAGE_HOME,1)==VG_UI_PAGE_TASKS);
 assert(vg_ui_page_move(VG_UI_PAGE_DEVICE,1)==VG_UI_PAGE_HOME);
 assert(vg_ui_page_move(VG_UI_PAGE_HOME,-1)==VG_UI_PAGE_DEVICE);
 assert(!strcmp(vg_ui_page_name(VG_UI_PAGE_HOME),"守护"));
 assert(!strcmp(vg_ui_page_name(VG_UI_PAGE_TASKS),"任务"));
 assert(!strcmp(vg_ui_page_name(VG_UI_PAGE_HISTORY),"记录"));
 assert(!strcmp(vg_ui_page_name(VG_UI_PAGE_DEVICE),"设备"));
 /* Home face never invents wall time: invalid RTC becomes an explicit state. */
 t.delay_seconds=60;t.state=VG_TASK_SCHEDULED;
 assert(vg_ui_home_present(&t,&s,true,42,480,&home));
 assert(!home.clock_valid&&!strcmp(home.clock,"时间未同步"));
 assert(!strcmp(home.date,"日期待同步"));
 assert(strstr(home.history,"历史记录 0"));
 assert(home.has_task&&!strcmp(home.primary,"Drink water"));
 assert(!strcmp(home.countdown,"42 秒")&&home.progress==70);
 assert(strstr(home.footer,"离线"));
 s.rtc_valid=true;s.now_epoch=1789548223;
 assert(vg_ui_home_present(&t,&s,true,42,480,&home));
 assert(home.clock_valid&&!strcmp(home.clock,"16:43"));
 assert(strstr(home.date,"09月16日")&&strstr(home.date,"周三"));
 t.state=VG_TASK_ALERTING;
 assert(vg_ui_home_present(&t,&s,false,0,480,&home));
 assert(home.alert&&!strcmp(home.countdown,"现在提醒")&&home.progress==100);
 assert(vg_ui_home_present(NULL,&s,false,0,480,&home));
 assert(!home.has_task&&!strcmp(home.primary,"暂无待办")&&home.progress==0);
 assert(vg_runtime_find(&r,"first",&t,NULL)==0);vg_runtime_status(&r,&s);
 unsigned w=writes,c=samples,j=schedules;
 assert(vg_ui_remaining(&t,&s,true,101,&remaining)&&remaining==1);
 assert(vg_ui_remaining(&t,&s,true,1100,&remaining)&&remaining==0);
 assert(!vg_ui_remaining(&t,&s,false,101,&remaining));assert(!vg_ui_remaining(&t,&s,true,99,&remaining));
 s.active_boot_id++;assert(!vg_ui_remaining(&t,&s,true,101,&remaining));s.active_boot_id--;
 assert(vg_ui_demo_id(&s,&seq,id,sizeof(id))==0&&seq==1&&strstr(id,"demo-1-1"));
 seq=UINT32_MAX;assert(vg_ui_demo_id(&s,&seq,id,sizeof(id))<0&&seq==UINT32_MAX);
 seq=3;assert(vg_ui_demo_id(&s,&seq,id,2)<0&&seq==3);
 s.alerting_count=1;s.rtc_valid=false;assert(vg_ui_rgb(&s,true)==0x100000);assert(vg_ui_rgb(&s,false)==0);
 s.blocked=true;assert(vg_ui_rgb(&s,true)==0x080400);s.blocked=false;
 /* Demo view must expose recording-critical state without relying on LVGL. */
 t.state=VG_TASK_SCHEDULED;t.snooze_count=2;
 assert(vg_ui_present(&t,false,&s,true,42,&view));
 assert(strstr(view.status,"任务 2 / 记录 0"));
 assert(!strcmp(view.title,"Drink water"));
 assert(strstr(view.detail,"剩余：42 秒")&&strstr(view.detail,"已延后：2 次"));
 assert(!view.alert&&view.background==0x10252f&&!view.done_enabled&&!view.right_enabled);
 t.state=VG_TASK_ALERTING;s.blocked=false;
 assert(vg_ui_present(&t,false,&s,false,0,&view));
 assert(view.alert&&view.background==0x481f2a&&view.done_enabled&&view.right_enabled);
 assert(strstr(view.detail,"正在提醒")&&!strcmp(view.right_label,"Snooze 60s"));
 assert(vg_ui_present(&t,true,&s,false,0,&view));
 assert(strstr(view.status,"记录 0 / 任务 2"));
 assert(!view.alert&&!view.done_enabled&&!view.right_enabled);
 t.state=VG_TASK_NEEDS_RESET;t.delay_seconds=60;
 assert(vg_ui_present(&t,false,&s,false,0,&view));
 assert(strstr(view.detail,"需要重新计时")&&strstr(view.detail,"60 秒"));
 assert(!strcmp(view.right_label,"Rearm")&&!view.done_enabled&&view.right_enabled);
 assert(vg_ui_pick(&r,&sel,&out)&&!strcmp(out.request.request_id,"first"));vg_ui_next(&r,&sel);assert(vg_ui_pick(&r,&sel,&out)&&!strcmp(out.request.request_id,"second"));
 assert(w==writes&&c==samples&&j==schedules);mono=1100;assert(vg_runtime_tick(&r)==0);
 assert(vg_ui_pick(&r,&sel,&t)&&!strcmp(t.request.request_id,"second"));vg_ui_capture(&target,&t,false);
 /* Press binds the displayed target, release after navigation still operates it. */
 assert(vg_ui_key_sample(&key,true,UINT32_MAX-500,&target,&action_target)==VG_UI_NONE);
 vg_ui_next(&r,&sel);assert(vg_ui_pick(&r,&sel,&out));vg_ui_capture(&other,&out,false);
 assert(vg_ui_key_sample(&key,true,499,&other,&action_target)==VG_UI_RIGHT&&!strcmp(action_target.id,"second"));
 assert(vg_ui_apply(&r,&action_target,VG_UI_RIGHT,&out)==0&&out.state==VG_TASK_SCHEDULED&&out.timer_revision==2);
 assert(vg_ui_key_sample(&key,true,2000,&other,&action_target)==VG_UI_NONE);
 assert(vg_ui_key_sample(&key,false,2001,&other,&action_target)==VG_UI_NONE);
 w=writes;c=samples;j=schedules;assert(vg_ui_apply(&r,&target,VG_UI_RIGHT,&out)==VG_ERR_INVALID_STATE);assert(vg_ui_apply(&r,&target,VG_UI_DONE,&out)==VG_ERR_INVALID_STATE);assert(w==writes&&c==samples&&j==schedules);
 assert(vg_ui_key_sample(&key,true,2100,&other,&action_target)==VG_UI_NONE);assert(vg_ui_key_sample(&key,false,2200,&target,&action_target)==VG_UI_DONE&&!strcmp(action_target.id,"first"));
 assert(vg_ui_apply(&r,&action_target,VG_UI_DONE,&out)==0&&out.state==VG_TASK_ACKNOWLEDGED);
 sel.history=true;sel.id[0]=0;assert(vg_ui_pick(&r,&sel,&out)&&out.state==VG_TASK_ACKNOWLEDGED);vg_ui_capture(&target,&out,true);assert(!target.valid);
 /* Reboot does not pretend elapsed uptime; explicit reset uses saved revision. */
 mono=20;assert(vg_runtime_start_with_timers(&rebooted,&deps,&timers)==0);sel.history=false;sel.id[0]=0;
 assert(vg_ui_pick(&rebooted,&sel,&t)&&t.state==VG_TASK_NEEDS_RESET);vg_ui_capture(&target,&t,false);
 w=writes;assert(vg_ui_apply(&rebooted,&target,VG_UI_DONE,&out)==VG_ERR_INVALID_STATE&&writes==w);
 assert(vg_ui_apply(&rebooted,&target,VG_UI_RIGHT,&out)==0&&out.state==VG_TASK_SCHEDULED&&out.mono_deadline_ms==1020&&out.timer_revision==3);
 assert(vg_ui_apply(&rebooted,&target,VG_UI_RIGHT,&out)==VG_ERR_INVALID_STATE);
 /* Records remain selected while a different task is actively alerting. */
 mono=1020;assert(vg_runtime_tick(&rebooted)==0);sel.history=true;sel.id[0]=0;
 w=writes;c=samples;j=schedules;
 assert(vg_ui_pick(&rebooted,&sel,&out)&&sel.history&&out.state==VG_TASK_ACKNOWLEDGED);
 assert(w==writes&&c==samples&&j==schedules);
 vg_runtime_status(&rebooted,&s);t.timer_domain=VG_TIMER_RELATIVE;t.state=VG_TASK_SCHEDULED;
 t.timer_boot_id=s.active_boot_id;t.mono_deadline_ms=UINT64_MAX;
 assert(vg_ui_remaining(&t,&s,true,1020,&remaining));
 assert(remaining==(UINT64_MAX-1020)/1000+((UINT64_MAX-1020)%1000!=0));
 s.boot_ready=false;seq=0;assert(vg_ui_demo_id(&s,&seq,id,sizeof(id))<0&&seq==0);
 assert(!vg_ui_remaining(&t,&s,true,1020,&remaining));s.boot_ready=true;
 s.active_boot_id=UINT64_MAX;assert(vg_ui_demo_id(&s,&seq,id,sizeof(id))==0&&strlen(id)<=64);
 /* Absolute reminder controls keep the captured due-time guard. */
 memset(sizes,0,sizeof(sizes));wall_valid=true;wall_now=1000;mono=100;
 assert(vg_runtime_start_with_timers(&r,&deps,&timers)==0);
 vg_create_request_t absolute={0};strcpy(absolute.request_id,"absolute");strcpy(absolute.title,"Absolute");
 absolute.due_epoch=1001;absolute.priority=1;assert(vg_runtime_create(&r,&absolute,&out)==0);
 wall_now=1001;mono=1100;assert(vg_runtime_tick(&r)==0);
 assert(vg_runtime_find(&r,"absolute",&out,NULL)==0);vg_ui_capture(&target,&out,false);
 assert(vg_ui_apply(&r,&target,VG_UI_RIGHT,&out)==0&&out.request.due_epoch==1061);
 w=writes;c=samples;j=schedules;
 assert(vg_ui_apply(&r,&target,VG_UI_RIGHT,&out)==VG_ERR_INVALID_STATE);
 assert(vg_ui_apply(&r,&target,VG_UI_DONE,&out)==VG_ERR_INVALID_STATE);
 assert(w==writes&&c==samples&&j==schedules);
 wall_now=1061;mono=61100;assert(vg_runtime_tick(&r)==0);
 assert(vg_runtime_find(&r,"absolute",&out,NULL)==0);vg_ui_capture(&target,&out,false);
 assert(vg_ui_apply(&r,&target,VG_UI_DONE,&out)==0&&out.state==VG_TASK_ACKNOWLEDGED);
 puts("PASS UI selection, remaining, RGB, Demo IDs, captured keys, guarded actions and real reboot/rearm");return 0;
}
