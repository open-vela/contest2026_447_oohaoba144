# VelaGuard 演示证据索引

状态：最新表盘 UI 已刷写并启动；串口健康状态通过，实体 UI 连续演示待用户观察和录制。

## 构建身份

- Team HEAD：`bd7b45c2db3d1467489ab46df77e11200d1e8b70` + 本地未提交改动
- Board：`SF32LB52-DevKit-LCD V1.2`
- Config：`sf32lb52_devkit_lcd/configs/nsh`
- Config SHA256：`2749e2f85b99c9feb60678e4dbae41b4c823906d328bef1a4ecdae616781a4c7`
- 最终表盘候选：`.artifacts/velaguard-watch-ui-91153ef6.bin`（本地留存，不提交仓库）
- 最终候选 SHA256：`91153ef6c5a7eb326753324439efc2ef66c1bff83af43467fd24c4275db82f5a`
- 最终源码复现构建 SHA256：`ca627e881c1b0a96be20ef6b4f703c0f03122f64f87662eed6b2529b678c4ddf`（未重复刷写）
- 最终候选大小：`2669780` bytes
- 加载范围：`[0x12010000, 0x1229BCD4)`，低于数据区 `0x129A0000`
- 证据等级：刷写、启动、设备健康、存储和工具注册为 `hardware-verified`；实体 UI 流程待验证

## 已有验证

| 项目 | 证据 | 结果 |
| --- | --- | --- |
| 开始前只读状态 | 2026-09-16 `device.status` | ready，active 0，history 37，alerting 0；RTC invalid |
| 开始前活动任务 | 2026-09-16 `task.list(offset=0)` | task=null |
| UI 先行测试 | `tests/host/run_ui_logic_tests.ps1` | RED 后 GREEN |
| 串口采集器测试 | `tests/host/test_demo_capture.ps1` | ValidateOnly PASS，未打开串口 |
| ARM 增量构建 | `cmake_out/velaguard_core_protocol` | 20/20，退出 0 |
| 固件写入 | `02-flash.txt`、`03-flash-exit.txt` | compat + verify，退出 0 |
| 真实启动 | `04-reset-stub*`、`06-boot.txt` | SFBL、storage/timed/RGB、Skill、五工具通过 |
| 启动后只读状态 | 本轮 agent-rpc 记录 | ready，active0/history37，display/touch/storage ready，app/ui error0；RTC invalid |
| 表盘 UI 主机测试 | `tests/host/run_ui_logic_tests.ps1` | FACE/APPS/PAGE/ABOUT、日期和历史摘要 PASS |
| 中文字体 | `tests/host/test_font_cn.py` | 7540 glyphs PASS |
| 最终表盘 ARM 构建 | `cmake_out/velaguard_core_protocol` | Flash 2669780，SRAM 203844 |
| 最终源码复现构建 | `cmake_out/velaguard_core_protocol/nuttx.bin` | SHA256 `ca627e...c4ddf`，退出 0 |
| 最终固件写入 | Windows 本地 `run-watch-ui-flash/02-flash.txt` | compat + verify，退出 0 |
| 最终恢复启动 | Windows 本地 `run-watch-ui-flash/08-recovery-boot.txt` | storage/timed/RGB/Skill/五工具通过 |
| 最终只读状态 | Windows 本地 `run-watch-ui-flash/09-read-only-status-list.txt` | ready，active0/history41，RTC/display/touch/storage ready，app/ui error0 |

## 待实机填写

- 实际串口：`COM7`，CH343，1,000,000 baud，8N1，DTR/RTS=false
- 刷写命令与退出码：`sftool --compat true write_flash --verify`，退出 0
- 启动日志：本机 `D:\date\code\openvela\logs\demo_first_20260916\run-watch-ui-flash\08-recovery-boot.txt`
- Demo 任务 ID：待录制
- 首次到期时间：待录制
- Snooze 后 revision/倒计时：待录制
- Done 历史：待录制
- 重启后历史恢复：待录制
- 视频与截图：待录制

## 上传包边界

只收录脱敏摘录、构建清单、测试结果、关键截图和最终视频。完整 UART 原始日志、固件大文件、本地绝对路径清单和任何凭据保留在本地，不进入仓库提交包。
