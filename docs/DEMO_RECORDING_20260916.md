# VelaGuard 完赛演示录制步骤

更新时间：2026-09-16

## 当前状态

- 最新表盘候选已经 `host-tested + build-verified + flash-verified`。
- 最终只读基线：`ready=true`、`active_count=0`、`history_count=41`、`alerting_count=0`。
- 当前 RTC 有效；Demo 60s 仍使用相对单调时钟，不依赖网络。
- 不格式化 NOR，不清理历史，不使用 UART 代替触摸完成演示动作。

## 录制前准备

1. 关闭所有占用 COM7 的终端、网关和串口工具。
2. 确认板卡仍为 SF32LB52-DevKit-LCD V1.2，实际端口仍为 COM7。
3. 镜头同时覆盖 390x450 AMOLED、RGB 方灯和手指操作区域。
4. 先启动屏幕录制或相机，再启动只读 UART 采集：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "\\wsl.localhost\Ubuntu\home\oohb144\workspace\openvela-contest\contest2026_447_oohaoba144\scripts\capture_demo_serial.ps1" -Port COM7 -Baud 1000000 -DurationSeconds 360
```

采集器只读取串口，不发送 RPC。运行期间不要再打开第二个 COM7 客户端。

## 连续镜头脚本

1. 展示默认表盘的大时间、日期、提醒圆环、历史记录和设备健康。
2. 上滑进入六入口应用列表，依次展示守护、任务、记录、设备、Demo 和关于。
3. 点击 `Demo`，展示中文标题“喝水提醒”、状态“等待提醒”和剩余秒数。
4. 保持网关关闭，连续拍摄倒计时；允许加速剪辑等待段，但不要剪断到期前后状态转换。
5. 到期后展示红色 ALERTING 背景和 RGB 方灯红色闪烁。
6. 点击“延后 60 秒”，展示任务恢复倒计时，且“已延后”计数为 1。
7. 再次到期后点击“完成”，展示完成提示。
8. 进入“记录”，找到刚完成的“喝水提醒”，展示“已完成”和延后次数。
9. 在 UART 采集仍运行时按已确认的复位方式重启，重新进入 VelaGuard。
10. 再次打开“记录”，展示同一历史仍存在；同时保留启动、storage、timed mode、Skill/五工具日志。
11. 关闭串口采集，记录脚本输出的 run 目录；随后才可用 RPC 做只读状态核对。

## 通过标准

- 正常页和提醒页背景可一眼区分。
- 中文标题、状态、剩余秒数和 Snooze 次数无截断。
- `Done`、`Snooze 60s`、`Tasks/Log`、`Next`、`Demo 60s` 可点击。
- 只有当前 ALERTING 任务启用 Done/Snooze；历史页不会误操作任务。
- 网关断开时仍完成到期、Snooze、再次到期和 Done。
- 重启后历史可读；若 RTC 仍无效，按实际情况记录，不宣称断电连续计时通过。

## 日志边界

- `uart-raw.txt` 留在本地 `logs/`，不直接上传比赛仓库。
- 上传包只放必要的脱敏摘录、产物清单、测试结果、截图和视频。
- 不记录或索取密码、Cookie、Token、API Key。
- 每次实机运行记录 commit、配置 SHA、固件路径/SHA/大小、刷写命令、端口参数、预期和实际结果。
