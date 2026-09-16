# 2026-09-14 最新固件 RGB 回归已通过

当前板固件：D:/date/code/openvela/.artifacts/velaguard-rgb-a8ebdd4f.bin，1783376 bytes，SHA256 a8ebdd4f11ef4ee10470e3109837a6cbd6fe346f858b2c100c06112016f4050f。源基线仍为bd7b45c2db3d1467489ab46df77e11200d1e8b70 + 未提交源码，配置SHA不变；完整来源与刷写命令在run-152620/51-rgb-artifact.json。应用write_flash --verify exit0。最终SRAM196772/524288 bytes。

新增RGB：PA32 GPIO + DWT，24bit GRB，发送函数位于SRAM 0x2000ffe0。最终ARM指令复核确认位循环无栈访问/函数调用，sentinel恰24位，失败拉低并恢复IRQ。规划、位序、频率/取整、sentinel测试先RED后GREEN，独立审查通过。板端四次启动均RGB init0；这仅证明原生初始化/发送调用返回成功，灯色和实际波形仍未观察。

新增回归目录：D:/date/code/openvela/logs/hardware_offline_20260914/run-152620/rgb-regression。

- 52-phase1.txt：旧history3保留，新任务hw-rgb-a/b各保存一条，重复create不增加；真实复位后ID/中文标题/due/priority/SCHEDULED全部一致。
- 首次串口关闭1789379632，A/B期限1789379717/1789379722；53-phase2.txt首次纯查询均ALERTING。校时请求往返3128ms，检查上限6000ms，到期后8秒观察。
- A确认、B延后60秒；B新期限1789379822，snooze_count1。串口再次关闭1789379771。
- 54-phase3.txt：B独立再次提醒；提醒中真实复位后恢复；旧snooze按旧due重放返回INVALID_STATE且未改任务；B确认后再次复位，新增两条历史及ack时间/count均保留；历史create重放不复活任务。三阶段均PASS，完成epoch1789379901。
- 最终hw-final-ready-input-preflight：ready=true、blocked=false、rtc_valid=true、storage_errno0、display_ready/touch_ready=true；active0/history5/alerting0/next0。历史5包含3条既有记录和本次2条，没有清空历史。

本轮结论：最新RGB候选上的离线UART与真实软件复位回归 hardware-verified；尚未确认真实触摸/KEY、RGB颜色/波形、物理断电恢复。无活动测试任务，COM7已关闭，等待用户在板旁后继续物理验证。用户已获询问操作时机，尚无回复，不重复请求已授权刷写/复位范围。

51 manifest 的sources数组保留构建时精确哈希；构建后仅改RGB头文件一行过期证据注释，新哈希单独列于post_build_comment_only，无运行逻辑变化。

下方为此前677c3727固件的独立通过证据，不能混淆固件身份。

---
# VelaGuard 离线 UART 与复位恢复实机验证

日期：2026-09-14。结论：hardware-verified，仅限下述实测范围；不代表整个 P0 已完成。

## 固件与环境身份

- 板：SF32LB52-DevKit-LCD，实物 V1.2 系列，1.85 英寸 390×450 AMOLED。
- 队伍基线：bd7b45c2db3d1467489ab46df77e11200d1e8b70 + 未提交源码。完整应用源码/配置哈希见证据目录 36-rtcfix-artifact.json，网关哈希见 44-gateway-manifest.json。
- 配置：官方 sf32lb52_devkit_lcd/configs/nsh 派生，输出 cmake_out/velaguard_core_protocol；config SHA256 210b18f78b86eeeaa9deb3d32da44648e5bd5cb89bf6a74845dc357cc4b3b517。
- 固件：D:/date/code/openvela/.artifacts/velaguard-rtcfix-677c3727.bin，1781332 bytes，SHA256 677c372740beb027d6556e2d12f1652a09ec1c3076a857a9ead361d7b8c7b9c7。
- 最终链接占用：flash 1781332 bytes；SRAM 196452/524288 bytes。不是运行时内存峰值。
- COM7：CH343，VID1A86/PID55D3，serial5ABA071580；UART 1000000/8N1，打开前 DTR/RTS=false。
- 原始证据（本机保留，禁止把大日志/固件加入提交）：D:/date/code/openvela/logs/hardware_offline_20260914/run-152620。

构建命令（Windows 入口）：

```powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/oohb144/workspace/openvela-contest && source build/envsetup.sh && cmake --build cmake_out/velaguard_core_protocol --parallel 8'
```

实际应用刷写命令（已获用户授权，verify exit0）：

```powershell
& 'D:\date\code\openvela\.tools\sftool\0.2.5\sftool.exe' -c SF32LB52 -p COM7 -b 1000000 --before default_reset --after no_reset --compat true write_flash --verify 'D:\date\code\openvela\.artifacts\velaguard-rtcfix-677c3727.bin@0x12010000'
```

复位通过官方 RAM stub 的只读16字节加载，再 burn_reset；每次要求日志同时含 SFBL 与 VelaGuard 启动行。单独 RTS 脉冲未证明真实复位，不作为本次通过证据。

## 预期与实际

| 检查 | 预期 | 实际证据 |
| --- | --- | --- |
| NOR | 挂载、任务保存、重启可读，无自动格式化 | /dev/vgnor代理/dev/config0，start0/storage_errno0；44与46通过 |
| RTC | 正确世纪，显式写入与双读回一致，复位恢复 | boot raw_year26→year2026、rc0/fault0；device.time response.ok |
| 保存/幂等 | A2/B2各一条，原请求重发不增加 | 44-offline-phase1.txt，active2 |
| 未到期复位 | ID/中文标题/due/priority/状态完整 | 27-pending-tasks-reboot-boot.txt + 44，均SCHEDULED |
| 离线到期 | 无串口连接期间由设备cron触发 | 关闭1789377851，期限A1789377941/B1789377946；45首次纯查询均ALERTING |
| 确认/延后 | A完成、B延后60秒，重复延后仅一次 | 45，B新due1789378048/snooze_count1 |
| 延后再提醒 | 第二次无串口连接期间独立触发 | 关闭1789377996，新due1789378048；46首次纯查询ALERTING |
| 提醒中复位 | B恢复ALERTING与snooze_count1 | 29-alerting-task-reboot-boot.txt + 46 |
| 旧请求保护 | 旧due的延后重放不改新周期 | 46返回INVALID_STATE/uncertainfalse，前后due/count/state一致 |
| 完成后复位 | 两条历史保留，活动任务0，调度0 | 30-completed-tasks-reboot-boot.txt + 46，active0/history3/next0 |
| 完成后create重放 | 不复活历史任务 | 46原A2请求response.ok，active0/history3不变 |

history3 包含先前调试任务 hw-offline-a-20260914，以及正式 A2/B2；没有删除历史。完成epoch1789378128。26-offline-test-state.json记录各时点；44/45/46均真实执行PASS。

状态中的 now_epoch 是 runtime 最近采样值，不能与若干秒后的PC返回时刻直接比较以判断时钟停止。本轮校时请求往返检查≤6秒、到期后8秒观察；这不是时钟精度或长期漂移指标。

## 本轮修复与测试

1. NOR DMA源缓冲区缓存未同步：队伍内MTD代理在委托写之前调用up_clean_dcache；host RED/GREEN、独立审查及实机保存/恢复通过。共享上游未修改。
2. RTC世纪不对称：HAL写126会转26，底层读回未恢复世纪；队伍clock适配严格校验原始年份和日期。显式RTC_SET_TIME→系统设时→RTC/系统双读回，启动在cron/runtime之前；失败保留fault。host测试与实机通过。
3. 终端SGR污染响应：cron日志尾部ESC[0m出现在JSON行前。网关只剥行首SGR/空白，保留JSON内容和2048分帧限制，拒绝任意日志/非SGR前缀。PS5.1/7测试、Python9项及UTF8多字节分片测试通过，正式实机三阶段使用修复后的PS路径。

NOR首次初始化已单独获准，仅重建[0x129A0000,0x12DA0000)的4MiB；初始化前完整分区备份有设备CRC验证，初始化后全区字节比对通过。备份不是整片16MiB镜像。后续应用修复没有再次格式化。

## 尚未验收

- 物理触摸与KEY2确认/延后；touch_ready仅表示驱动初始化。重启初期touch_ready短暂false，稍后预检true，不能把驱动就绪当物理点击通过。
- 真正断电再上电的任务持久化及RTC保持；本次为真实软件复位，不能替代断电测试。
- RGB驱动与波形/颜色显示；当前尚未接入。
- 完整ai_agent/Skill loader、相对计时等剩余P0范围，见项目规划。

设备当前无活动任务，保留3条已完成记录。未提交固件或大日志，未commit/push；原官方基线未回刷。