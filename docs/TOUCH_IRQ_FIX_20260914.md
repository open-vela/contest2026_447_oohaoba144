# 2026-09-14 触摸 IRQ 修正

## 问题与证据

用户多次点击 Demo 60s 后，UART 仍 active=0/history=7，task.list 与 event.sync(offset=7) 为空。这证明点击未产生任务，不应把 touch_ready=true 当成触摸成功。

SF32LB52-DevKit-LCD 的 README_zh-cn.md:102–113、README.md:113–124 和 src/bsp_pinmux.c:214–220 一致确认 CTP_INT=PA31，PA41 属于 ULP。src/sifli_ap.c:308–310 将 CONFIG_TOUCH_IRQ_PIN 传入 FT6146。官方 LCD configs/nsh/defconfig:173 与原构建 .config 却为 41。

## 最小修正（构建中，物理效果待验）

- 仅将现有 cmake_out/velaguard_core_protocol/.config 的 CONFIG_TOUCH_IRQ_PIN=41 改为 31；原配置已单独备份。没有改 vendor 源码或 defconfig。
- team app/velaguard/CMakeLists.txt 添加 LCD FT6146/PA31 检查，防止再次带错误 IRQ 构建。
- team tests/host/check_lcd_touch_config.py 检查目标板、触摸驱动和 IRQ；旧配置 RED 失败，修正后 GREEN 通过。
- team main 保留临时观测：每个 open 独立的只读触摸队列、官方 LVGL 回调原样包装，以及按钮点击计数；不写输入设备，不通过串口模拟触摸。
- 诊断构建 9527ecae 已先刷入并正常启动，历史7保留。静置观测 raw=0、LVGL polls 持续增长。该数据没有伴随用户在此固件上的指定点击，不能单独作为硬件失效证据。

## 在现有环境复现

1. 使用已验证的 LCD/VelaGuard 构建目录，不重新安装工具链或覆盖上游。
2. 在该构建目录 .config 中只修改 CONFIG_TOUCH_IRQ_PIN=31。原始官方 defconfig 若仍为41，每次从它新建配置后都必须修正。
3. 通过 `wsl.exe -d Ubuntu -- bash -lc` 调用团队 `python3 contest2026_447_oohaoba144/tests/host/check_lcd_touch_config.py cmake_out/velaguard_core_protocol/.config`。
4. 同一路由执行 `source build/envsetup.sh && cmake --build cmake_out/velaguard_core_protocol --parallel 8`，并复查生成 include/nuttx/config.h 中 CONFIG_TOUCH_IRQ_PIN 为31。
5. 保存固件SHA/配置SHA/命令/日志并刷入；读取只读诊断，实际点击后须 raw、edges、clicks 有对应变化，且生成 demo-* 任务。仅编译通过不能认定修复通过。

## 当前证据位置

Windows logs/hardware_offline_20260914/run-152620/physical-20260914-180601：诊断固件清单、刷写/启动日志、原main/config备份、RPC和用户观察。上级 touch-config-red.txt、touch-config-green.txt、touch_pa31_build.log。

RGB 红色闪烁已有用户确认；触摸 ACK/Snooze、KEY2、真正断电恢复和完整Agent集成仍需推进。
## 构建与上板结果

PA31固件693d4a6e：1784564 bytes，最终静态SRAM196900 bytes。完整构建退出0，.config差异仅41→31；生成config.h确认31。独立CMake测试31通过、41/缺FT6146失败。刷写带verify退出0，真实启动日志及状态正常：active0/history7、RTC有效、NOR errno0、display/touch初始化成功。

实际点击验收仍待用户在此固件上操作。构建/刷写成功不等于物理触摸成功。新的配置SHA为2749e2f85b99c9feb60678e4dbae41b4c823906d328bef1a4ecdae616781a4c7；完整固件SHA和命令见physical run的touch-pa31-artifact.json。

# 触摸实机已通过创建任务；等待物理 Snooze

2026-09-14 22:36：用户确认此前拔插过 USB 电源，故先前串口无响应不能据此认定触摸导致死锁。主控复位恢复日志显示RTC raw_year0、时钟无效；NOR历史7完整恢复。随后真实诊断 raw140→175、LVGL edges34→44、button clicks13→18；无效时间下 Demo被拒绝 app_error=-4。保存失效证据后已明确校时。

校时后用户触摸产生 `demo-1789396576-125377-0`（喝水提醒），SCHEDULED，due1789396636（22:37:16），active1/history7。未通过 UART task.create 代建。至此PA31修复已实机证明 原始触摸→LVGL→按钮→任务创建。已提示红闪后点一次 Snooze60，下一步只读核对 snooze_count/newdue，再验证Done/KEY2。不要再刷机或复位打断此任务。

当前固件693d4a6e。日志physical-20260914-180601/physical-state.json为最新唯一测试任务，touch-investigation.jsonl保存原文及时间。真实断电后的历史保留已观察；断电后RTC连续计时未通过，必须区分，不把校时后结果冒充断电计时成功。临时无响应具体原因未定，不改IRQ/HPWORK等无证据代码。

---


# 触摸创建/Snooze/Done实机通过，当前验证KEY2

2026-09-14 22:47，固件693d4a6e。用户真实触摸创建demo任务已通过；实际Snooze和Done闭环为 demo-1789396676-225444-1：原due1789396736，snooze_count1，新due1789397092；再次到期后用户点Done，历史ACKNOWLEDGED，acknowledged_epoch1789397096。触点Snooze307,260、Done124,247，点击计数各增加1；没有通过UART代执行snooze/ack。最终active0/history16（其中本轮多个Demo自动过期，不能把16条都算完成）。此前拔插USB后历史仍保留，但RTC失效须校时，不能声称断电连续计时通过。

当前已创建唯一KEY2测试 hw-key2-20260914-224730，due按 key2-state.json（预计22:48:05），baseline_history16。已请求红闪后长按实体KEY2约2秒并松开，等待用户；下一步只读task.list验证count1/newdue，再测短按确认。用户交互期间不刷写或复位、不用UART代按键。日志目录通过 .artifacts/active-physical-run.txt 找，触摸最终状态在physical-state.json，按键单独在key2-state.json。

---


# KEY2实机通过；待执行任务断电恢复测试已准备

KEY2任务 hw-key2-20260914-224730 经用户长按延后1次、再次提醒后短按确认。history offset16为ACKNOWLEDGED，snooze_count1、due1789397354、acknowledged_epoch1789397474；最终active0/history17。触摸Demo/Snooze/Done、KEY2长/短按和RGB红闪均已有物理证据。

当前另建待执行任务 hw-power-20260914-225259（断电恢复验证，priority2，due1789398179），创建后SCHEDULED，baseline_history17。已关闭串口并请求用户拔掉两根USB，屏幕和灯熄灭后等5秒再插回，待回复已重新上电。不要用软件复位替代此测试；收到回复后先只读status/list和历史核验，严禁先校时掩盖RTC掉电状态。原任务和17历史应保留；RTC若invalid，则如实记录时钟连续性未通过，再显式校时恢复调度。power-cycle-state.json是此步骤当前状态，文件夹见active-physical-run.txt。当前无串口后台进程。

---
